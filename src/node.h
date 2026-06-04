#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <cpprest/json.h>
#include <cpprest/host_utils.h>

const unsigned int delay_millis{0};
namespace seeder
{
  namespace nmos_node
  {

    struct Redudancy
    {
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
      std::string video_format;
      std::string colorspace = "BT709";
      std::string transfer_characteristics = "SDR";
      std::string source_ip;
      std::string ip;
      int port;
      Redudancy redudancy;
      int pg_format;
    };

    struct AudioSender
    {
      std::string id;
      std::string sender_id;
      std::string name;
      std::string source_ip;
      bool enable = false;
      int channel_count;
      int bit_depth;
      int sample_rate;
      std::string ip;
      int port;
      Redudancy redudancy;
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
      Redudancy redudancy;
      // 能力
      VideoReceiverCaps caps;
      //连接信息
      std::string format;
      std::string colorspace;
      std::string transfer_characteristics;
    };
    struct AudioReceiver
    {
      std::string id;
      std::string name;
      bool enable = false;
      int channel_count;
      int bit_depth;
      int sample_rate;
      double packet_time;
      std::string ip;
      std::string source_ip;
      int port;
      Redudancy redudancy;
    };
        struct AncillarySender
    {
      std::string id;
      std::string sender_id;
      std::string name;
      std::string format;
      std::string source_ip;
      bool enable = false;
      std::string ip;
      int port;
      Redudancy redudancy;
    };
    struct AncillaryReceiver
    {
      std::string id;
      std::string name;
      std::string format;
      bool enable = false;
      std::string ip;
      std::string source_ip;
      int port;
      Redudancy redudancy;
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
    class Node
    {
    private:
      class Impl;
      std::unique_ptr<Impl> p_impl;

    public:
      explicit Node(std::string node_config_path_);
      ~Node();

      bool start();
      bool stop();
      void add_video_sender(VideoSender video);
      void add_audio_sender(AudioSender audio);
      void add_ancillary_sender(AncillarySender ancillary);

      void add_video_receiver(VideoReceiver video);
      void add_audio_receiver(AudioReceiver audio);
      void add_ancillary_receiver(AncillaryReceiver ancillary);

      void update_video_sender(VideoSender video);
      void update_audio_sender(AudioSender audio);
      void update_ancillary_sender(AncillarySender ancillary);

      void update_video_receiver(VideoReceiver video);
      void update_audio_receiver(AudioReceiver audio);
      void update_ancillary_receiver(AncillaryReceiver ancillary);

      void remove_video_sender(std::string id);
      void remove_audio_sender(std::string id);
      void remove_ancillary_sender(std::string id);

      void remove_video_receiver(std::string id);
      void remove_audio_receiver(std::string id);
      void remove_ancillary_receiver(std::string id);

      void set_update_video_receiver_callback(
          std::function<void(const VideoReceiver &video)> func);
      void set_update_audio_receiver_callback(
          std::function<void(const AudioReceiver &audio)> func);
      void set_update_ancillary_receiver_callback(
          std::function<void(const AncillaryReceiver &ancillary)> func);
      void set_registration_changed_callback(
          std::function<void(const RegistrationStatus &status)> func);
      web::json::value effective_settings() const;
      web::json::value persisted_settings() const;
      web::json::value discover_registration_apis() const;
      void write_persisted_settings(const web::json::value &settings);
      void set_runtime_interfaces(
          const web::hosts::experimental::host_interface &primary,
          const web::hosts::experimental::host_interface &secondary);

      void set_ptp_clock(std::string gmtid, bool locked, int ptp_domain = 127);
    };
  } // namespace nmos_node
} // namespace seeder
