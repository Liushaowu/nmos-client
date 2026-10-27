#include "daemon/registry_client.h"
#include "sdp_fixtures.h"

#include <cpprest/http_listener.h>
#include <atomic>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace seeder::nmos_sync;
using web::json::value;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
utility::string_t to_t(const std::string &s) { return utility::conversions::to_string_t(s); }

struct Registry {
  std::unique_ptr<web::http::experimental::listener::http_listener> listener;
  std::string origin;
  std::atomic<int> sdp_hits{0};
  std::atomic<int> mode{0};
  Registry() {
    std::mt19937 random(std::random_device{}());
    for (int attempt = 0; attempt < 30; ++attempt) {
      origin = "http://127.0.0.1:" + std::to_string(20000 + random() % 30000);
      listener = std::make_unique<web::http::experimental::listener::http_listener>(to_t(origin));
      listener->support(web::http::methods::GET, [this](web::http::http_request request) {
        const auto path = request.relative_uri().path();
        if (path.find(U("/sdp/")) == 0) {
          ++sdp_hits;
          if (path == U("/sdp/v2")) request.reply(503);
          else if (path == U("/sdp/a1")) request.reply(200, to_t(sdp_fixtures::session(sdp_fixtures::audio_media())), U("application/sdp"));
          else if (path == U("/sdp/v1")) request.reply(200, to_t(sdp_fixtures::session(sdp_fixtures::video_media())), U("application/sdp"));
          else request.reply(200, U("v=0\r\ns=Test\r\n"), U("application/sdp"));
          return;
        }
        const bool senders = path == U("/x-nmos/query/v1.3/senders/");
        if (!senders && path != U("/x-nmos/query/v1.3/flows/")) { request.reply(404); return; }
        if (mode == 1) { request.reply(500); return; }
        if (mode == 2) { request.reply(200, value::object()); return; }
        const auto params = web::uri::split_query(request.relative_uri().query());
        const auto cursor = params.at(U("paging.since"));
        value body = value::array();
        if (senders) {
          if (cursor == U("0:0")) {
            body = value::parse(U(R"([{"id":"v1","label":"Camera 测试","flow_id":"fv","tags":{"group":["Main"]}}, {"id":"a1","label":"Mic","flow_id":"fa"}])"));
          } else if (cursor == U("1:0")) {
            body = value::parse(U(R"([{"id":"d1","label":"Ancillary","flow_id":"fd"}, {"id":"no","label":"No flow","flow_id":null,"manifest_href":null}, {"id":"v2","label":"Camera two","flow_id":"fv"}])"));
          }
          for (auto &sender : body.as_array())
            if (sender.at(U("id")).as_string() != U("no"))
              sender[U("manifest_href")] = value::string(to_t(origin + "/sdp/") + sender.at(U("id")).as_string());
        } else {
          if (cursor == U("0:0"))
            body = value::parse(U(R"([{"id":"fv","format":"urn:x-nmos:format:video","description":"Studio output"}, {"id":"fa","format":"urn:x-nmos:format:audio"}])"));
          else if (cursor == U("1:0"))
            body = value::parse(U(R"([{"id":"fd","format":"urn:x-nmos:format:data"}])"));
        }
        const auto next = mode == 3 ? cursor : cursor == U("0:0") ? U("1:0") : U("2:0");
        web::http::http_response response(200);
        response.set_body(body);
        // Real registries can advertise a DNS name different from the request host.
        response.headers().add(U("Link"), U("<http://registry.local") + path +
          U("?paging.since=") + next + U("&paging.limit=100>; rel=\"next\""));
        request.reply(response);
      });
      try { listener->open().get(); return; }
      catch (...) { if (attempt == 29) throw; }
    }
  }
  ~Registry() { if (listener) listener->close().wait(); }
  RegistryClient client() { return RegistryClient(origin + "/x-nmos/query/v1.3", 500); }
};
}

int main() {
  try {
    for (const auto *invalid : {"page=0", "page=-1", "page=1x", "page_size=0", "page_size=1001", "type=mux", "foo=1",
                               "page=18446744073709551615&page_size=1000"}) {
      bool rejected = false;
      try { SenderQuery::parse(to_t(invalid)); } catch (const std::invalid_argument &) { rejected = true; }
      check(rejected, "invalid parameters must be rejected");
    }
    check(SenderQuery::parse(U("keyword=Camera+two")).keyword == "Camera two", "query plus decoding");
    check(SenderQuery::parse(U("keyword=A%2BB")).keyword == "A+B", "encoded plus decoding");
    Registry registry;
    auto all = registry.client().senders(SenderQuery::parse(U("")));
    check(all.at(U("total")).as_integer() == 5 && all.at(U("items")).size() == 5, "fetch all upstream pages");
    check(all.at(U("page")).is_null(), "unpaged metadata");
    const auto &items = all.at(U("items"));
    check(items.at(0).at(U("format")).at(U("type")).as_string() == U("audio"), "stable ID order");
    check(items.at(0).at(U("sdp")).as_string() == to_t(sdp_fixtures::session(sdp_fixtures::audio_media())), "SDP payload is preserved");
    check(items.at(0).at(U("format")).at(U("name")).as_string() == U("L24/48000/2") && !items.at(0).has_field(U("error")), "parsed audio format in response");
    check(items.at(0).at(U("name")).as_string() == U("Mic") && items.at(0).at(U("ip")).as_string() == U("239.1.1.2") && items.at(0).at(U("port")).as_integer() == 5004, "name comes from registry label while endpoint comes from SDP");
    check(items.at(3).at(U("name")).as_string() == U("Camera 测试"), "SDP session name must not overwrite Sender label");
    check(items.at(0).size() == 6 && !items.at(0).has_field(U("id")) && !items.at(0).has_field(U("flow")) && !all.has_field(U("query_url")), "only SDP and parsed fields are returned");
    check(items.at(3).at(U("format")).at(U("display_name")).as_string() == U("1080p50"), "parsed video format in response");
    check(items.at(1).at(U("format")).is_null() && items.at(1).at(U("error")).is_string() && items.at(1).at(U("sdp")).is_string(), "bad SDP retains original without failing list");
    check(items.at(2).at(U("sdp")).is_null() && items.at(2).at(U("error")).is_string(), "sender without flow or manifest is retained");
    check(items.at(2).at(U("name")).as_string() == U("No flow") && items.at(4).at(U("name")).as_string() == U("Camera two"), "label survives missing manifest and SDP HTTP failure");
    check(items.at(4).at(U("sdp")).is_null() && items.at(4).at(U("error")).is_string(), "SDP HTTP failure does not fail list");
    check(items.at(4).at(U("format")).is_null() && items.at(4).at(U("transports")).size() == 0, "download failure does not invent parsed content");
    registry.sdp_hits = 0;
    auto page = registry.client().senders(SenderQuery::parse(U("page=2&page_size=1&type=video&keyword=CAMERA")));
    check(page.at(U("total")).as_integer() == 2 && page.at(U("items")).at(0).at(U("sdp")).is_null() && page.at(U("items")).at(0).at(U("error")).as_string().find(U("503")) != utility::string_t::npos, "filter before pagination selects v2");
    check(!page.at(U("has_more")).as_bool() && registry.sdp_hits == 1, "only selected page fetches SDP");
    page = registry.client().senders(SenderQuery::parse(U("page_size=1&type=video")));
    check(page.at(U("page")).as_integer() == 1 && page.at(U("has_more")).as_bool(), "default page and has_more");
    for (const auto *type : {"audio", "data"}) {
      auto result = registry.client().senders(SenderQuery::parse(to_t(std::string("type=") + type)));
      check(result.at(U("total")).as_integer() == 1, "audio/data filter");
    }
    check(registry.client().senders(SenderQuery::parse(U("keyword=%E6%B5%8B%E8%AF%95"))).at(U("total")).as_integer() == 1, "UTF-8 keyword");
    check(registry.client().senders(SenderQuery::parse(U("keyword=studio"))).at(U("total")).as_integer() == 2, "flow description search");
    check(registry.client().senders(SenderQuery::parse(U("keyword=MAIN"))).at(U("total")).as_integer() == 1, "tag search");
    registry.sdp_hits = 0;
    page = registry.client().senders(SenderQuery::parse(U("page=999&page_size=10")));
    check(page.at(U("items")).size() == 0 && page.at(U("total")).as_integer() == 5 && registry.sdp_hits == 0, "out of range page");
    check(registry.client().senders(SenderQuery::parse(U("keyword=missing"))).at(U("total")).as_integer() == 0, "empty search");
    for (int mode : {1, 2, 3}) {
      registry.mode = mode;
      bool failed = false;
      try { registry.client().senders(SenderQuery{}); } catch (const std::exception &) { failed = true; }
      check(failed, "upstream error, invalid JSON shape or pagination loop must fail");
    }
    std::cout << "registry client tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
