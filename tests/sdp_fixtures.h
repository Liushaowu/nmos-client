#pragma once

#include <string>

namespace sdp_fixtures {
inline std::string session(const std::string &media, const std::string &connection = "") {
  return "v=0\r\no=- 1 1 IN IP4 192.0.2.10\r\ns=Test\r\n" + connection + "t=0 0\r\n" + media;
}
inline std::string video_media(const std::string &rate = "50", const std::string &scan = "") {
  return "m=video 5004 RTP/AVP 96\r\nc=IN IP4 239.1.1.1/32\r\na=rtpmap:96 raw/90000\r\n"
         "a=fmtp:96 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=" + rate +
         "; depth=10; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN" + scan + "\r\n";
}
inline std::string audio_media(const std::string &rtpmap = "L24/48000/2") {
  return "m=audio 5004 RTP/AVP 97\r\nc=IN IP4 239.1.1.2/32\r\na=rtpmap:97 " + rtpmap +
         "\r\na=fmtp:97 channel-order=SMPTE2110.(ST)\r\na=ptime:1\r\n";
}
}
