#include "node.h"
#include <unistd.h>

bool setting_video_sender_func(seeder::nmos_node::VideoSender *video) {
  video->ip = "0.0.0.0";
  video->port = 5004;
  video->source_ip = "0.0.0.0";
  video->enable = false;
  video->redudancy.enable = false;
  video->redudancy.ip = "0.0.0.0";
  video->redudancy.port = 5004;
  return true;
}

int main() {

  seeder::nmos_node::VideoSender videoSender;
  videoSender.enable = false;
  videoSender.width = 3840;
  videoSender.height = 2160;
  videoSender.fps = 50;
  videoSender.id="askldfjalk;sjflkasjflkajsdlkfjlasjdfl";
  videoSender.name = "输出1";
  videoSender.id = "1";
  videoSender.redudancy.enable = false;
  videoSender.pg_format = ST20_FMT_YUV_422_10BIT;
  videoSender.redudancy.ip="";
  videoSender.redudancy.port=5004;

  seeder::nmos_node::VideoReceiver video;
  video.enable = false;
  video.width = 3840;
  video.height = 2160;
  video.fps = 50;
  video.id="askldfjalk;sjflkasjflkajsdlkfjlasjdfl";
  video.name = "输入1";
  video.redudancy.enable = false;
  video.pg_format = ST20_FMT_YUV_422_10BIT;

  seeder::nmos_node::AudioReceiver audio;
  audio.enable = false;
  audio.channel_count = 1;
  audio.bit_depth = 0;
  audio.packet_time = 1;
  audio.simple_rate = 0;
  audio.fps = 1;
  audio.id="askldfjalk;sjflkasjflkajsdlkfjlasjdfl";
  audio.name = "输入1";
  audio.ip = "";
  audio.redudancy.enable = false;
  audio.redudancy.ip = "0.0.0.0";
  audio.redudancy.port = 5004;

  seeder::nmos_node::Node node(
      "/home/seeder/dev/seeder_switcher_config/node_config_dev.json");
  node.start();
  node.setting_video_sender_func = setting_video_sender_func;
  sleep(3);
  node.add_video_receiver(video);

  node.add_audio_receiver(audio);
  // node.remove_video_receiver(video.id);
  // node.add_video_receiver(video);

  node.add_video_sender(videoSender);

  while (true) {
  }
  return 0;
}