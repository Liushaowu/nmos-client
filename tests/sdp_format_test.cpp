#include "daemon/sdp_format.h"
#include "sdp_fixtures.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using seeder::nmos_sync::parse_sdp_format;
using seeder::nmos_sync::parse_sdp_sender;
using namespace sdp_fixtures;
namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
}

int main() {
  try {
    const auto video = parse_sdp_format(session(video_media()));
    check(video.at(U("type")).as_string() == U("video") && video.at(U("media_type")).as_string() == U("video/raw"), "video media type");
    check(video.at(U("name")).as_string() == U("1080p5000") && video.at(U("display_name")).as_string() == U("1080p50"), "video format preset");
    check(video.at(U("width")).as_integer() == 1920 && video.at(U("height")).as_integer() == 1080 && video.at(U("bit_depth")).as_integer() == 10, "video dimensions and depth");
    check(video.at(U("sampling")).as_string() == U("YCbCr-4:2:2") && video.at(U("colorspace")).as_string() == U("BT709"), "video sampling and colorimetry");
    check(video.at(U("transfer_characteristics")).as_string() == U("SDR"), "default SDR");
    const auto fractional = parse_sdp_format(session(video_media("60000/1001")));
    check(fractional.at(U("frame_rate")).at(U("numerator")).as_integer() == 60000 && fractional.at(U("frame_rate")).at(U("denominator")).as_integer() == 1001, "exact fractional frame rate");
    check(std::fabs(fractional.at(U("fps")).as_double() - 59.94005994) < 1e-7, "fractional fps");
    const auto interlaced = parse_sdp_format(session(video_media("25", "; interlace")));
    check(interlaced.at(U("name")).as_string() == U("1080i5000") && interlaced.at(U("fps")).as_double() == 25 && interlaced.at(U("field_rate")).as_double() == 50, "interlaced field rate");
    const auto psf = parse_sdp_format(session(video_media("25", "; interlace; segmented")));
    check(psf.at(U("scan")).as_string() == U("psf") && psf.at(U("display_name")).as_string() == U("1920x1080PsF25"), "PsF is distinct from interlace");
    const auto redundant = parse_sdp_format(session("a=group:DUP A B\r\n" + video_media() + "a=mid:A\r\n" + video_media() + "a=mid:B\r\n"));
    check(redundant.at(U("name")) == video.at(U("name")), "DUP legs use one format");

    const auto audio = parse_sdp_format(session(audio_media()));
    check(audio.at(U("name")).as_string() == U("L24/48000/2") && audio.at(U("type")).as_string() == U("audio"), "audio format name");
    check(audio.at(U("sample_rate")).as_integer() == 48000 && audio.at(U("channel_count")).as_integer() == 2 && audio.at(U("bit_depth")).as_integer() == 24, "PCM audio format");
    check(audio.at(U("packet_time_ms")).as_double() == 1 && audio.at(U("channel_order")).as_string() == U("SMPTE2110.(ST)"), "audio packet time and channel order");
    const auto mono = parse_sdp_format(session(audio_media("l16/96000")));
    check(mono.at(U("channel_count")).as_integer() == 1 && mono.at(U("bit_depth")).as_integer() == 16, "default mono and lowercase PCM");
    const auto compressed = parse_sdp_format(session(audio_media("opus/48000/2")));
    check(compressed.at(U("bit_depth")).is_null() && compressed.at(U("encoding_name")).as_string() == U("opus"), "compressed audio must not invent PCM depth");

    auto jpeg = video_media();
    jpeg.replace(jpeg.find("raw/90000"), 9, "jxsv/90000");
    jpeg.replace(jpeg.find("a=fmtp:96 "), 10, "a=fmtp:96 packetmode=0; profile=Main422.10; level=2k-1; sublevel=Sublev3bpp; ");
    const auto xs = parse_sdp_format(session(jpeg));
    check(xs.at(U("media_type")).as_string() == U("video/jxsv") && xs.at(U("profile")).as_string() == U("Main422.10") && xs.at(U("width")).as_integer() == 1920, "JPEG XS format");
    const auto xs_minimal = parse_sdp_format(session("m=video 5004 RTP/AVP 96\r\na=rtpmap:96 jxsv/90000\r\na=fmtp:96 packetmode=0\r\n"));
    check(xs_minimal.at(U("width")).is_null() && xs_minimal.at(U("fps")).is_null(), "JPEG XS does not invent omitted codestream parameters");

    const auto parsed = parse_sdp_sender(session(audio_media()));
    check(parsed.at(U("name")).as_string() == U("Test") && parsed.at(U("ip")).as_string() == U("239.1.1.2") && parsed.at(U("port")).as_integer() == 5004, "session name, destination IP and RTP port");
    auto default_media = audio_media();
    const std::string connection = "c=IN IP4 239.1.1.2/32\r\n";
    default_media.erase(default_media.find(connection), connection.size());
    const auto inherited = parse_sdp_sender(session(default_media, "c=IN IP4 239.9.8.7/64\r\n"));
    check(inherited.at(U("ip")).as_string() == U("239.9.8.7"), "session connection is inherited and TTL removed");
    const auto overridden = parse_sdp_sender(session(audio_media(), "c=IN IP4 239.9.8.7/64\r\n"));
    check(overridden.at(U("ip")).as_string() == U("239.1.1.2"), "media connection overrides session connection");
    const auto ipv6 = parse_sdp_sender(session(default_media, "c=IN IP6 ff15::1234/1\r\n"));
    check(ipv6.at(U("ip")).as_string() == U("ff15::1234"), "IPv6 destination excludes address count");
    const auto unicast = parse_sdp_sender(session(default_media, "c=IN IP4 192.0.2.20\r\n"));
    check(unicast.at(U("ip")).as_string() == U("192.0.2.20"), "unicast destination differs from origin");
    auto second_leg = video_media();
    second_leg.replace(second_leg.find("239.1.1.1"), 9, "239.2.2.2");
    second_leg.replace(second_leg.find("5004"), 4, "6004");
    const auto dup = parse_sdp_sender(session("a=group:DUP A B\r\n" + video_media() + "a=mid:A\r\n" + second_leg + "a=mid:B\r\n"));
    check(dup.at(U("transports")).size() == 2 && dup.at(U("transports")).at(1).at(U("ip")).as_string() == U("239.2.2.2") && dup.at(U("transports")).at(1).at(U("port")).as_integer() == 6004, "DUP keeps both destinations");
    const auto missing_connection = parse_sdp_sender(session(default_media));
    check(missing_connection.at(U("ip")).is_null() && missing_connection.at(U("port")).as_integer() == 5004, "missing connection does not use origin IP");
    const auto incomplete = parse_sdp_sender(session("m=video 5004 RTP/AVP 96\r\nc=IN IP4 239.1.1.1/32\r\na=rtpmap:96 raw/90000\r\n"));
    check(incomplete.at(U("name")).as_string() == U("Test") && incomplete.at(U("ip")).as_string() == U("239.1.1.1") && incomplete.at(U("format")).is_null() && incomplete.at(U("error")).is_string(), "format failure preserves name and endpoint");

    for (const auto &bad : {std::string("bad SDP"), session(""), session("m=video 5004 RTP/AVP 96\r\na=rtpmap:96 raw/90000\r\n"), session(video_media("0"))}) {
      bool failed = false;
      try { parse_sdp_format(bad); } catch (const std::exception &) { failed = true; }
      check(failed, "invalid or incomplete SDP must fail");
    }
    std::cout << "SDP format tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
