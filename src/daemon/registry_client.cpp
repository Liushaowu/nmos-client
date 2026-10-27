#include "registry_client.h"
#include "sdp_format.h"

#include <cpprest/http_client.h>
#include <algorithm>
#include <cctype>
#include <future>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <vector>

namespace seeder::nmos_sync {
namespace {
using web::json::value;
utility::string_t to_t(const std::string &s) {
  return utility::conversions::to_string_t(s);
}
std::string utf8(const utility::string_t &s) {
  return utility::conversions::to_utf8string(s);
}
std::string field(const value &v, const char *key) {
  const auto k = to_t(key);
  return v.has_string_field(k) ? utf8(v.at(k).as_string()) : "";
}
std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(c < 128 ? std::tolower(c) : c);
  });
  return s;
}
web::http::http_response get(const std::string &url, int timeout_ms) {
  const web::uri uri(to_t(url));
  if ((uri.scheme() != U("http") && uri.scheme() != U("https")) || uri.host().empty())
    throw std::runtime_error("expected an absolute HTTP(S) URL: " + url);
  web::http::client::http_client_config config;
  config.set_timeout(std::chrono::milliseconds(timeout_ms));
  web::http::client::http_client client(uri, config);
  auto response = client.request(web::http::methods::GET).get();
  if (response.status_code() != web::http::status_codes::OK)
    throw std::runtime_error("GET " + url + " returned HTTP " +
                             std::to_string(response.status_code()));
  return response;
}
std::size_t positive_integer(const std::string &s, const char *name) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument(std::string(name) + " must be a positive integer");
  try {
    const auto n = std::stoull(s);
    if (!n || n > std::numeric_limits<std::size_t>::max()) throw std::out_of_range(name);
    return static_cast<std::size_t>(n);
  } catch (const std::exception &) {
    throw std::invalid_argument(std::string(name) + " is out of range");
  }
}
bool matches(const value &sender, const value &flow, const std::string &keyword) {
  if (keyword.empty()) return true;
  for (const auto *object : {&sender, &flow}) {
    for (const auto *key : {"id", "label", "description", "device_id", "flow_id", "source_id"})
      if (lowercase(field(*object, key)).find(keyword) != std::string::npos) return true;
    if (object->has_field(U("tags")) &&
        lowercase(utf8(object->at(U("tags")).serialize())).find(keyword) != std::string::npos)
      return true;
  }
  return false;
}
}

SenderQuery SenderQuery::parse(const utility::string_t &query) {
  SenderQuery result;
  const auto params = web::uri::split_query(query);
  bool paged = false;
  for (const auto &param : params) {
    const auto key = utf8(web::uri::decode(param.first));
    // Form-style query strings accept '+' as a space; preserve encoded %2B.
    auto encoded = param.second;
    std::replace(encoded.begin(), encoded.end(), U('+'), U(' '));
    const auto text = utf8(web::uri::decode(encoded));
    if (key == "page") { result.page = positive_integer(text, "page"); paged = true; }
    else if (key == "page_size") {
      result.page_size = positive_integer(text, "page_size"); paged = true;
      if (result.page_size > 1000) throw std::invalid_argument("page_size must be <= 1000");
    }
    else if (key == "keyword") result.keyword = text;
    else if (key == "type") result.type = lowercase(text);
    else throw std::invalid_argument("unknown query parameter: " + key);
  }
  if (paged && !result.page) result.page = 1;
  if (!result.type.empty() && result.type != "video" && result.type != "audio" && result.type != "data")
    throw std::invalid_argument("type must be video, audio or data");
  if (result.page && (*result.page - 1) > std::numeric_limits<std::size_t>::max() / result.page_size)
    throw std::invalid_argument("page is out of range");
  return result;
}

RegistryClient::RegistryClient(std::string query_url, int timeout_ms)
    : query_url_(std::move(query_url)), timeout_ms_(timeout_ms) {
  while (!query_url_.empty() && query_url_.back() == '/') query_url_.pop_back();
  const web::uri uri(to_t(query_url_));
  if ((uri.scheme() != U("http") && uri.scheme() != U("https")) ||
      uri.host().empty() || !uri.query().empty() || !uri.fragment().empty() ||
      !std::regex_match(utf8(uri.path()), std::regex(".*/x-nmos/query/v1\\.[0-3]")))
    throw std::runtime_error("invalid registry Query API URL '" + query_url_ +
        "': expected an absolute HTTP(S) URL ending with /x-nmos/query/v1.x (x=0..3), without query or fragment");
}

value RegistryClient::collection(const std::string &resource) const {
  std::string url = query_url_ + "/" + resource + "/?paging.since=0:0&paging.limit=100";
  const web::uri base(to_t(url));
  std::set<std::string> visited;
  std::map<std::string, value> resources;
  while (true) {
    if (!visited.insert(url).second || visited.size() > 10000)
      throw std::runtime_error("registry pagination did not terminate");
    const auto response = get(url, timeout_ms_);
    const auto page = response.extract_json().get();
    if (!page.is_array()) throw std::runtime_error("registry " + resource + " response must be an array");
    if (page.as_array().size() == 0) break;
    for (const auto &item : page.as_array()) {
      const auto id = field(item, "id");
      if (id.empty()) throw std::runtime_error("registry resource is missing id");
      resources[id] = item;
    }
    const auto header = response.headers().find(U("Link"));
    if (header == response.headers().end()) break; // Non-paginating IS-04 v1.0 implementations.
    const auto links = utf8(header->second);
    const std::regex next_link(R"re(<([^>]+)>\s*;\s*rel\s*=\s*"?next"?(?:\s*[,;]|\s*$))re", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(links, match, next_link)) break;
    const web::uri next(web::uri(to_t(url)).resolve_uri(to_t(match[1].str())));
    // A registry may advertise its DNS name instead of the address used by the
    // client. Keep our selected origin and accept links only for this collection.
    if (next.path() != base.path()) throw std::runtime_error("unexpected registry pagination path");
    url = utf8(web::uri_builder(base).set_query(next.query()).to_string());
  }
  auto result = value::array();
  std::size_t index = 0;
  for (auto &entry : resources) result[index++] = std::move(entry.second);
  return result;
}

value RegistryClient::with_sdp(value sender) const {
  auto result = value::object();
  result[U("sdp")] = value::null();
  result[U("name")] = sender.has_string_field(U("label"))
      ? sender.at(U("label")) : value::null();
  result[U("ip")] = value::null();
  result[U("port")] = value::null();
  result[U("format")] = value::null();
  result[U("transports")] = value::array();
  const auto manifest = field(sender, "manifest_href");
  if (manifest.empty()) {
    result[U("error")] = value::string(U("manifest_href is missing or null"));
    return result;
  }
  try {
    // SDP uses application/sdp, which cpprest does not classify as text.
    result[U("sdp")] = value::string(get(manifest, timeout_ms_).extract_string(true).get());
  } catch (const std::exception &error) {
    result[U("error")] = value::string(to_t(error.what()));
    return result;
  }
  try {
    const auto parsed = parse_sdp_sender(utf8(result.at(U("sdp")).as_string()));
    for (const auto &entry : parsed.as_object()) {
      // The displayed Sender name is its registry label, not the SDP session name.
      if (entry.first != U("name")) result[entry.first] = entry.second;
    }
  } catch (const std::exception &error) {
    result[U("error")] = value::string(to_t(error.what()));
  }
  return result;
}

value RegistryClient::senders(const SenderQuery &query) const {
  const auto senders = collection("senders");
  const auto flows = collection("flows");
  std::map<std::string, value> by_id;
  for (const auto &flow : flows.as_array()) by_id[field(flow, "id")] = flow;
  std::vector<value> filtered;
  const auto keyword = lowercase(query.keyword);
  for (auto sender : senders.as_array()) {
    const auto it = by_id.find(field(sender, "flow_id"));
    const auto flow = it == by_id.end() ? value::null() : it->second;
    const auto format = field(flow, "format");
    std::string type;
    for (const auto *candidate : {"video", "audio", "data"})
      if (format == std::string("urn:x-nmos:format:") + candidate) type = candidate;
    if ((!query.type.empty() && type != query.type) || !matches(sender, flow, keyword)) continue;
    filtered.push_back(std::move(sender));
  }
  // Stable ID order is independent of the registry's cursor order.
  const auto total = filtered.size();
  const auto offset = query.page ? (*query.page - 1) * query.page_size : 0;
  const auto begin = std::min(offset, total);
  const auto count = query.page ? std::min(query.page_size, total - begin) : total;
  auto items = value::array();
  constexpr std::size_t concurrency = 8;
  for (std::size_t i = 0; i < count; i += concurrency) {
    std::vector<std::future<value>> batch;
    for (std::size_t j = i; j < std::min(i + concurrency, count); ++j)
      batch.push_back(std::async(std::launch::async, [this, &filtered, begin, j] {
        return with_sdp(filtered[begin + j]);
      }));
    for (std::size_t j = 0; j < batch.size(); ++j) items[i + j] = batch[j].get();
  }
  auto result = value::object();
  result[U("items")] = std::move(items);
  result[U("total")] = value::number(static_cast<std::uint64_t>(total));
  result[U("page")] = query.page ? value::number(static_cast<std::uint64_t>(*query.page)) : value::null();
  result[U("page_size")] = query.page ? value::number(static_cast<std::uint64_t>(query.page_size)) : value::null();
  result[U("has_more")] = value::boolean(begin + count < total);
  return result;
}
}
