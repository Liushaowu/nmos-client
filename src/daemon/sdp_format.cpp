#include "sdp_format.h"

#include "video_format.h"
#include <nmos/sdp_utils.h>
#include <nmos/video_jxsv.h>
#include <sdp/sdp.h>
#include <sdp/json.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace seeder::nmos_sync {
namespace {
using web::json::value;
utility::string_t to_t(const std::string &text) {
  return utility::conversions::to_string_t(text);
}
std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}
value optional_string(const utility::string_t &text) {
  return text.empty() ? value::null() : value::string(text);
}

template <typename Video>
void add_video_format(value &result, const Video &video, bool required = true) {
  if (required && (!video.width || !video.height || !video.depth || video.exactframerate <= nmos::rational(0)))
    throw std::runtime_error("video dimensions, bit depth and frame rate must be positive");
  const auto fps = static_cast<double>(video.exactframerate.numerator()) /
                   static_cast<double>(video.exactframerate.denominator());
  result[U("width")] = video.width ? value::number(video.width) : value::null();
  result[U("height")] = video.height ? value::number(video.height) : value::null();
  result[U("bit_depth")] = video.depth ? value::number(video.depth) : value::null();
  result[U("sampling")] = optional_string(video.sampling.name);
  result[U("colorspace")] = optional_string(video.colorimetry.name);
  result[U("transfer_characteristics")] = value::string(video.tcs.name.empty() ? U("SDR") : video.tcs.name);
  result[U("interlace")] = value::boolean(video.interlace);
  result[U("segmented")] = value::boolean(video.segmented);
  result[U("scan")] = value::string(video.segmented ? U("psf") : video.interlace ? U("interlaced") : U("progressive"));
  result[U("frame_rate")] = value::null();
  if (fps > 0) {
    result[U("frame_rate")] = value::object();
    result[U("frame_rate")][U("numerator")] = value::number(video.exactframerate.numerator());
    result[U("frame_rate")][U("denominator")] = value::number(video.exactframerate.denominator());
  }
  result[U("fps")] = fps > 0 ? value::number(fps) : value::null();
  result[U("field_rate")] = video.interlace && fps > 0 ? value::number(fps * 2) : value::null();
  if (!video.width || !video.height || fps <= 0) return;

  // The repository's interlaced format table uses the field rate (i50 = 25 fps).
  const auto naming_rate = video.exactframerate * (video.interlace ? 2 : 1);
  if (!video.segmented) {
    for (const auto &desc : seeder::core::format_descs) {
      if (desc.width == static_cast<int>(video.width) && desc.height == static_cast<int>(video.height) &&
          desc.field_count == (video.interlace ? 2 : 1) &&
          nmos::rational(desc.framerate.numerator(), desc.framerate.denominator()) == naming_rate) {
        result[U("name")] = value::string(to_t(desc.name));
        result[U("display_name")] = value::string(to_t(desc.display_name));
        return;
      }
    }
  }
  // Preserve nonstandard dimensions rather than mapping them to an HD/UHD preset.
  std::ostringstream name;
  name << video.width << 'x' << video.height << (video.segmented ? "PsF" : video.interlace ? "i" : "p")
       << std::setprecision(8) << (video.segmented ? fps : fps * (video.interlace ? 2 : 1));
  result[U("name")] = result[U("display_name")] = value::string(to_t(name.str()));
}
}

namespace {
value format_from_session(const value &session) {
  const auto params = nmos::get_session_description_sdp_parameters(session);
  if (params.rtpmap.encoding_name.empty() || !params.rtpmap.clock_rate)
    throw std::runtime_error("SDP RTP encoding and clock rate are required");
  const auto encoding = utility::conversions::to_utf8string(params.rtpmap.encoding_name);
  const auto codec = lower(encoding);
  const auto media = utility::conversions::to_utf8string(params.media_type.name);
  const auto type = codec == "smpte291" ? "data" : media;
  auto result = value::object();
  result[U("type")] = value::string(to_t(type));
  result[U("media_type")] = value::string(nmos::get_media_type(params).name);
  result[U("encoding_name")] = value::string(params.rtpmap.encoding_name);
  result[U("payload_type")] = value::number(params.rtpmap.payload_type);
  result[U("clock_rate")] = value::number(params.rtpmap.clock_rate);
  result[U("name")] = result[U("display_name")] = value::string(params.rtpmap.encoding_name);
  auto fmtp = value::object();
  for (const auto &entry : params.fmtp) fmtp[entry.first] = value::string(entry.second);
  result[U("fmtp")] = std::move(fmtp);

  if (media == "video" && codec == "raw") {
    add_video_format(result, nmos::get_video_raw_parameters(params));
  } else if (media == "video" && codec == "jxsv") {
    const auto video = nmos::get_video_jxsv_parameters(params);
    // JPEG XS may carry these optional parameters in its codestream only.
    add_video_format(result, video, false);
    result[U("profile")] = optional_string(video.profile.name);
    result[U("level")] = optional_string(video.level.name);
    result[U("sublevel")] = optional_string(video.sublevel.name);
  } else if (media == "audio") {
    const auto channels = params.rtpmap.encoding_parameters ? params.rtpmap.encoding_parameters : 1;
    result[U("sample_rate")] = value::number(params.rtpmap.clock_rate);
    result[U("channel_count")] = value::number(channels);
    result[U("packet_time_ms")] = params.packet_time > 0 ? value::number(params.packet_time) : value::null();
    result[U("bit_depth")] = value::null();
    result[U("channel_order")] = value::null();
    if (codec == "l8" || codec == "l16" || codec == "l24") {
      // Older nmos-cpp versions expect uppercase L despite RTP encoding names
      // being case-insensitive. Preserve the advertised name in the response.
      auto pcm = params;
      pcm.rtpmap.encoding_name = to_t("L" + codec.substr(1));
      const auto audio = nmos::get_audio_L_parameters(pcm);
      result[U("bit_depth")] = value::number(audio.bit_depth);
      result[U("channel_order")] = optional_string(audio.channel_order);
    }
    result[U("name")] = value::string(to_t(encoding + "/" + std::to_string(params.rtpmap.clock_rate) + "/" + std::to_string(channels)));
    result[U("display_name")] = value::string(to_t(encoding + " " + std::to_string(params.rtpmap.clock_rate) + " Hz " + std::to_string(channels) + " ch"));
  }
  return result;
}
}

value parse_sdp_format(const std::string &sdp) {
  return format_from_session(sdp::parse_session_description(sdp));
}

value parse_sdp_sender(const std::string &text) {
  const auto session = sdp::parse_session_description(text);
  auto result = value::object();
  result[U("name")] = optional_string(sdp::fields::session_name(session));
  result[U("ip")] = value::null();
  result[U("port")] = value::null();
  result[U("format")] = value::null();
  auto transports = value::array();
  for (const auto &description : sdp::fields::media_descriptions(session).as_array()) {
    // A media-level c= overrides the session-level default. Do not use the
    // origin (o=) address: it identifies the creator, not the RTP destination.
    auto connection = sdp::fields::connection_data(session);
    const auto media_connections = sdp::fields::connection_data(description);
    if (media_connections.is_array() && media_connections.size() > 0)
      connection = media_connections.at(0);
    auto endpoint = value::object();
    endpoint[U("ip")] = value::null();
    if (!connection.is_null()) {
      const auto address = sdp::fields::connection_address(connection);
      // IPv4 multicast addresses may include /ttl[/count], IPv6 /count.
      endpoint[U("ip")] = value::string(address.substr(0, address.find(U('/'))));
    }
    const auto port = sdp::fields::port(sdp::fields::media(description));
    if (port > 65535) throw std::runtime_error("SDP media port is out of range");
    endpoint[U("port")] = value::number(port);
    transports[transports.size()] = std::move(endpoint);
  }
  if (transports.size() > 0) {
    result[U("ip")] = transports.at(0).at(U("ip"));
    result[U("port")] = transports.at(0).at(U("port"));
  }
  result[U("transports")] = std::move(transports);
  try {
    result[U("format")] = format_from_session(session);
  } catch (const std::exception &error) {
    result[U("error")] = value::string(to_t(error.what()));
  }
  return result;
}
}
