#pragma once

#include <string>
#include <vector>

namespace seeder
{
  namespace nmos_node
  {
    struct Redundancy
    {
      bool present = false;
      bool enable = false;
      std::string source_ip;
      std::string ip;
      int port;
    };

    struct VideoSender
    {
      std::string id;
      std::string sender_id;
      std::string name;
      bool enable = false;
      bool parent_enable = true;
      std::string video_format;
      std::string colorspace = "BT709";
      std::string transfer_characteristics = "SDR";
      std::string source_ip;
      std::string ip;
      int port;
      Redundancy redundancy;
      int pg_format;
    };

    struct AudioSender
    {
      std::string id;
      std::string sender_id;
      std::string name;
      std::string source_ip;
      bool enable = false;
      bool parent_enable = true;
      int channel_count;
      int bit_depth;
      int sample_rate;
      std::string ip;
      int port;
      Redundancy redundancy;
    };

    struct VideoReceiverCaps
    {
      std::vector<std::string> formats;
      std::vector<std::string> colorspaces;
      std::vector<std::string> transfer_characteristics;
    };

    struct VideoReceiver
    {
      std::string id;
      std::string name;
      std::string source_ip;
      std::string ip;
      int port;
      bool enable = false;
      bool parent_enable = true;
      Redundancy redundancy;
      VideoReceiverCaps caps;
      std::string format;
      std::string colorspace;
      std::string transfer_characteristics;
    };

    struct AudioReceiver
    {
      std::string id;
      std::string name;
      bool enable = false;
      bool parent_enable = true;
      int channel_count;
      int bit_depth;
      int sample_rate;
      double packet_time;
      std::string ip;
      std::string source_ip;
      int port;
      Redundancy redundancy;
    };

    struct AncillarySender
    {
      std::string id;
      std::string sender_id;
      std::string name;
      std::string format;
      std::string source_ip;
      bool enable = false;
      bool parent_enable = true;
      std::string ip;
      int port;
      Redundancy redundancy;
    };

    struct AncillaryReceiver
    {
      std::string id;
      std::string name;
      std::string format;
      bool enable = false;
      bool parent_enable = true;
      std::string ip;
      std::string source_ip;
      int port;
      Redundancy redundancy;
    };

    struct RegistrationStatus
    {
      bool connected = false;
      std::string uri;
      std::string scheme;
      std::string host;
      int port = 0;
      std::string version;
    };
  }
}
