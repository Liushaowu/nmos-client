#pragma once

#include <functional>
#include <memory>
#include <string>

const unsigned int delay_millis{0};
namespace seeder {
namespace nmos_node {

struct Redudancy {
  bool enable = false;
  std::string ip;
  int port;
};

struct VideoSender {
  double fps;
  std::string id;
  std::string sender_id;
  std::string name;
  bool enable = false;
  bool interlace = false;
  int fps_numerator = 50000;
  int fps_denominator = 1000;
  int width;
  int height;
  std::string source_ip;
  std::string ip;
  int port;
  Redudancy redudancy;
  int pg_format;
};

struct AudioSender {
  double fps;
  int fps_numerator = 50000;
  int fps_denominator = 1000;
  std::string id;
  std::string sender_id;
  std::string name;
  std::string source_ip;
  bool enable = false;
  int channel_count;
  int bit_depth;
  int simple_rate;
  std::string ip;
  int port;
  Redudancy redudancy;
};
struct VideoReceiver {
  double fps;
  std::string id;
  std::string receiver_id;
  std::string name;
  bool interlace = false;
  bool enable = false;
  int fps_numerator = 50000;
  int fps_denominator = 1000;
  int width;
  int height;
  std::string source_ip;
  std::string ip;
  int port;
  Redudancy redudancy;
  int pg_format;
};
struct AudioReceiver {
  double fps;
  int fps_numerator = 50000;
  int fps_denominator = 1000;
  std::string id;
  std::string receiver_id;
  std::string name;
  bool enable = false;
  int channel_count;
  int bit_depth;
  int simple_rate;
  double packet_time;
  std::string ip;
  std::string source_ip;
  int port;
  Redudancy redudancy;
};
class Node {
private:
  class Impl;
  std::unique_ptr<Impl> p_impl;

public:
  explicit Node(std::string node_config_path_);
  ~Node();

  void start();
  void add_video_sender(VideoSender video);
  void add_audio_sender(AudioSender audio);

  void add_video_receiver(VideoReceiver video);
  void add_audio_receiver(AudioReceiver audio);

  void remove_video_sender(std::string id);
  void remove_audio_sender(std::string id);

  void remove_video_receiver(std::string id);
  void remove_audio_receiver(std::string id);

  void set_update_video_receiver_callback(
      std::function<void(VideoReceiver *video)> func);
  void set_update_audio_receiver_callback(
      std::function<void(AudioReceiver *audio)> func);

  void set_ptp_clock(std::string gmtid, bool locked);
};
} // namespace nmos_node
} // namespace seeder
