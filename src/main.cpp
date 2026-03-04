#include "mtl/st20_api.h"
#include "node.h"
#include <cstdio>
#include <unistd.h>

bool setting_video_sender_func(seeder::nmos_node::VideoSender *video) {
  video->ip = "239.10.10.11";
  video->port = 5004;
  video->source_ip = "192.168.20.12";
  video->enable = false;
  video->redudancy.enable = false;
  video->redudancy.ip = "239.10.10.12";
  video->redudancy.port = 5004;
  return true;
}

bool setting_video_re_func(seeder::nmos_node::VideoSender *video) {
  video->ip = "239.10.10.11";
  video->port = 5004;
  video->source_ip = "192.168.20.12";
  video->enable = false;
  video->redudancy.enable = false;
  video->redudancy.ip = "239.10.10.12";
  video->redudancy.port = 5004;
  return true;
}

int main() {

  seeder::nmos_node::VideoSender videoSender;
  videoSender.enable = true;
  videoSender.width = 3840;
  videoSender.height = 2160;
  videoSender.fps = 50;
  videoSender.fps_denominator=50000;
  videoSender.fps_numerator=1000;
  videoSender.id = "1";
  videoSender.name = "IP1";
  videoSender.interlace = false;
  videoSender.redudancy.enable = false;
  videoSender.pg_format = ST20_FMT_YUV_422_10BIT;
  videoSender.source_ip = "192.168.10.133";
  videoSender.ip = "239.15.10.1";
  videoSender.port = 20000;

  seeder::nmos_node::VideoSender videoSender2;
  videoSender2.enable = true;
  videoSender2.width = 1920;
  videoSender2.height = 1080;
  videoSender2.fps = 30;
  videoSender2.id = "2";
  videoSender2.name = "输出2";
  videoSender2.redudancy.enable = false;
  videoSender2.pg_format = ST20_FMT_YUV_422_10BIT;
  videoSender2.redudancy.ip = "239.10.10.22";
  videoSender2.redudancy.port = 5004;
  videoSender2.source_ip = "192.168.10.167";
  videoSender2.ip = "239.10.10.22";
  videoSender2.port = 5004;

  seeder::nmos_node::AudioSender audioSender;
  audioSender.enable = true;
  audioSender.channel_count = 2;
  audioSender.bit_depth = 2;
  audioSender.simple_rate = 0;
  audioSender.fps = 1;
  audioSender.id = "57a90a9b-45f3-4d33-989d-cfe5f05561b5";
  audioSender.name = "SDI4";
  audioSender.source_ip = "192.168.10.107";
  audioSender.ip = "239.6.11.111";
  audioSender.port = 30000;

  seeder::nmos_node::VideoReceiver video;
  video.enable = false;
  video.width = 4096;
  video.height = 2160;
  video.fps = 60;
  video.id = "3";
  video.name = "输入1";
  video.ip = "";
  video.redudancy.enable = false;
  video.port = 5005;
  video.pg_format = ST20_FMT_YUV_422_10BIT;
  video.ip = "239.10.10.11";
  video.redudancy.ip = "239.10.10.12";
  video.redudancy.port = 5004;
  video.source_ip = "192.168.10.109";
  video.enable = true;

  seeder::nmos_node::VideoReceiver video_receiver2;
  video_receiver2.enable = false;
  video_receiver2.width = 1080;
  video_receiver2.height = 720;
  video_receiver2.fps = 24;
  video_receiver2.id = "4";
  video_receiver2.name = "输入2";
  video_receiver2.redudancy.enable = false;
  video_receiver2.port = 5005;
  video_receiver2.pg_format = ST20_FMT_YUV_422_10BIT;
  video_receiver2.ip = "239.10.10.11";
  video_receiver2.redudancy.ip = "239.10.10.12";
  video_receiver2.redudancy.port = 5004;
  video_receiver2.source_ip = "192.168.10.109";
  video_receiver2.enable = true;

  // seeder::nmos_node::AudioReceiver audio;
  // audio.enable = false;
  // audio.channel_count = 1;
  // audio.bit_depth = 0;
  // audio.packet_time = 1;
  // audio.simple_rate = 0;
  // audio.fps = 1;
  // audio.id="3";
  // audio.name = "输入1";
  // audio.ip = "";
  // audio.redudancy.enable = false;
  // audio.redudancy.ip = "0.0.0.0";
  // audio.redudancy.port = 5004;

  seeder::nmos_node::Node node(
      "/home/seeder/dev/nmos-client/node_config_dev.json");
  node.start();
  sleep(3);
  node.add_video_receiver(video);
  node.add_video_sender(videoSender);

  // node.add_audio_receiver(audio);
  // node.remove_video_receiver(video.id);
  // node.add_video_receiver(video);
  // node.add_audio_sender(audioSender);
  // node.add_video_sender(videoSender);
  // node.add_video_sender(videoSender2);
  // node.set_update_video_receiver_callback(
  //     [](seeder::nmos_node::VideoReceiver* video) {
  //       printf("update video receiver: %s\n", video->id.c_str());
  //     });

  // node.set_update_audio_receiver_callback(
  //     [](seeder::nmos_node::AudioReceiver* audio) {
  //       printf("update audio receiver: %s\n", audio->id.c_str());
  //     });
  // node.remove_video_receiver(video_receiver2.id);


  node.set_ptp_clock("1231313123", false);
  while (true) {
  }
  return 0;
}