#include "node.h"
#include "st_fmt.h"
#include "node_activation_context.h"
#include "node_callback_dispatcher.h"
#include "node_connection_transport_params.h"
#include "node_event_bridge.h"
#include "nmos/api_utils.h" // for make_api_listener
#include "nmos/authorization_behaviour.h"
#include "nmos/authorization_redirect_api.h"
#include "nmos/control_protocol_state.h"
#include "nmos/jwks_uri_api.h"
#include "nmos/log_gate.h"
#include "nmos/model.h"
#include "nmos/node_server.h"
#include "nmos/mdns.h"
#include "nmos/ocsp_behaviour.h"
#include "nmos/ocsp_response_handler.h"
#include "nmos/process_utils.h"
#include "nmos/sdp_utils.h"
#include "nmos/server.h"
#include "nmos/server_utils.h" // for make_http_listener_config
#include "node_implementation.h"
#include "node_resource_controller.h"
#include "node_resource_factory.h"
#include "node_resource_lifecycle.h"
#include "node_runtime_interface_updater.h"
#include "node_sdp_service.h"
#include "node_server_runtime.h"
#include "node_settings.h"
#include "node_stream_store.h"
#include <asm-generic/errno.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <cpprest/json_ops.h>
#include <cpprest/json_utils.h>
#include <functional>
#include <ifaddrs.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <nmos/capabilities.h>
#include <nmos/connection_api.h>
#include <nmos/id.h>
#include <nmos/interlace_mode.h>
#include <nmos/json_fields.h>
#include <nmos/media_type.h>
#include <nmos/mutex.h>
#include <nmos/node_resource.h>
#include <nmos/is04_versions.h>
#include <nmos/rational.h>
#include <nmos/resource.h>
#include <nmos/resources.h>
#include <nmos/tai.h>
#include <nmos/transport.h>
#include <nmos/type.h>
#include <nmos/version.h>
#include <sdp/json.h>
#include <slog/all_in_one.h>
#include <mdns/service_discovery.h>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef HAVE_LLDP
#include "nmos/lldp_manager.h"
#include "lldp/lldp_manager.h"
#endif
#include <nmos/id.h>
#include <nmos/mutex.h>
#include <algorithm>
using web::json::value;
using web::json::value_from_elements;
using web::json::value_of;

const unsigned int delay_millis{0};

namespace
{
  bool is_valid_ptp_gmid(const std::string &gmid)
  {
    if (23 != gmid.size())
    {
      return false;
    }
    for (std::size_t index = 0; index < gmid.size(); ++index)
    {
      if (2 == index % 3)
      {
        if ('-' != gmid[index])
        {
          return false;
        }
        continue;
      }
      const char value = gmid[index];
      if (!std::isdigit(static_cast<unsigned char>(value)) &&
          !(value >= 'a' && value <= 'f'))
      {
        return false;
      }
    }
    return true;
  }

  std::string normalize_ptp_gmid(std::string gmid)
  {
    gmid.erase(std::remove_if(gmid.begin(), gmid.end(),
                              [](unsigned char value)
                              { return ':' == value || '-' == value || std::isspace(value); }),
               gmid.end());

    std::transform(gmid.begin(), gmid.end(), gmid.begin(),
                   [](unsigned char value)
                   {
                     return static_cast<char>(std::tolower(value));
                   });

    if (16 != gmid.size())
    {
      return {};
    }

    if (!std::all_of(gmid.begin(), gmid.end(), [](unsigned char value)
                     { return std::isxdigit(value); }))
    {
      return {};
    }

    std::string normalized;
    normalized.reserve(23);
    for (std::size_t index = 0; index < gmid.size(); index += 2)
    {
      if (!normalized.empty())
      {
        normalized.push_back('-');
      }
      normalized.append(gmid.substr(index, 2));
    }

    return normalized;
  }

}

nmos::interlace_mode get_interlace_mode(int fps_numerator, int fps_denominator,
                                        int height)
{
  const auto frame_rate = nmos::parse_rational(
      web::json::value_of({{nmos::fields::numerator, fps_numerator},
                           {nmos::fields::denominator, fps_denominator}}));
  const auto frame_height = height;
  return (nmos::rates::rate25 == frame_rate ||
          nmos::rates::rate29_97 == frame_rate) &&
                 1080 == frame_height
             ? nmos::interlace_modes::interlaced_tff
             : nmos::interlace_modes::progressive;
}

namespace seeder
{
  namespace nmos_node
  {
    class Node::Impl
    {
    public:
      using LifecycleState = internal::LifecycleState;

      std::thread thread_;
      internal::NodeSettings settings_;
      nmos::experimental::log_gate *gate_ = nullptr;
      nmos::id node_id_;
      nmos::id device_id_;
      utility::string_t seed_id_;
      std::vector<nmos::id> node_ids_;
      std::vector<nmos::id> device_ids_;
      RuntimeInterfaces runtime_interfaces_;

      internal::StreamStore stream_store_;

      nmos::connection_sender_transportfile_setter set_transportfile;
      nmos::connection_resource_auto_resolver resolve_auto;
      nmos::node_model node_model_;

      std::mutex receiver_mutex_;
      std::mutex sender_mutex_;
      std::mutex runtime_interfaces_mutex_;
      std::mutex lifecycle_mutex_;
      std::mutex thread_mutex_;
      std::condition_variable lifecycle_cv_;
      bool stop_requested_{false};
      std::atomic<LifecycleState> lifecycle_state_{LifecycleState::stopped};
      bool needs_model_reset_{false};
      int ptp_domain_number_ = 127;

      internal::CallbackDispatcher callbacks_;
      internal::NodeEventBridge event_bridge_;
      internal::NodeResourceController resource_controller_;
      internal::NodeRuntimeInterfaceUpdater runtime_interface_updater_;
      std::shared_ptr<lldp::lldp_manager> lldp_manager_;
      std::shared_ptr<lldp::lldp_manager_guard> lldp_manager_guard_;

      Impl()
          : event_bridge_(internal::NodeEventBridgeContext{callbacks_}),
            resource_controller_(internal::NodeResourceControllerContext{
                stream_store_, node_model_, gate_, sender_mutex_, receiver_mutex_,
                runtime_interfaces_, runtime_interfaces_mutex_, seed_id_,
                device_id_, set_transportfile}),
            runtime_interface_updater_(internal::NodeRuntimeInterfaceUpdaterContext{
                runtime_interfaces_, runtime_interfaces_mutex_, stream_store_,
                sender_mutex_, receiver_mutex_, node_model_, set_transportfile,
                resource_controller_})
      {
      }
      ~Impl() { stop(); }

      RuntimeInterfaces runtime_interfaces_snapshot()
      {
        std::lock_guard<std::mutex> lock(runtime_interfaces_mutex_);
        return runtime_interfaces_;
      }

      bool stop()
      {
        const auto stop_started = std::chrono::steady_clock::now();
        const auto log_elapsed = [&stop_started](const char *step)
        {
          internal::NodeServerRuntime::log_stop_elapsed(stop_started, step);
        };

        log_elapsed("begin");
        bool stop_completed = false;
        bool should_join_thread = false;
        std::thread::id worker_thread_id;

        {
          std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
          if (LifecycleState::stopped != lifecycle_state_)
          {
            lifecycle_state_ = LifecycleState::stopping;
            stop_requested_ = true;
            lifecycle_cv_.notify_all();
          }
          else
          {
            stop_completed = true;
          }
        }

        {
          auto lock = node_model_.write_lock();
          node_model_.shutdown = true;
        }
        node_model_.notify();
        node_model_.shutdown_condition.notify_all();

        {
          std::lock_guard<std::mutex> thread_lock(thread_mutex_);
          if (thread_.joinable())
          {
            worker_thread_id = thread_.get_id();
            should_join_thread = std::this_thread::get_id() != worker_thread_id;
            if (should_join_thread)
            {
              log_elapsed("joining worker thread");
              thread_.join();
              log_elapsed("worker thread joined");
              stop_completed = true;
            }
          }
          else
          {
            stop_completed = true;
          }
        }

        if (should_join_thread || std::thread::id{} == worker_thread_id)
        {
          std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
          lifecycle_state_ = LifecycleState::stopped;
          stop_requested_ = false;
          needs_model_reset_ = true;
          stop_completed = true;
        }

        log_elapsed("complete");
        return stop_completed;
      }

      void node_implementation_run()
      {
        auto lock = node_model_.read_lock();
        // wait for the thread to be interrupted because the server is being shut
        // down
        node_model_.shutdown_condition.wait(lock,
                                            [&]
                                            { return node_model_.shutdown; });
        nmos::details::reverse_lock_guard<nmos::read_lock> unlock{lock};
      }

      // This constructs all the callbacks used to integrate the example
      // device-specific underlying implementation into the server instance for the
      // NMOS Node.
      nmos::experimental::node_implementation make_node_implementation()
      {

        return nmos::experimental::node_implementation()
            .on_parse_transport_file(
                make_node_implementation_transport_file_parser())
            .on_resolve_auto(
                make_node_implementation_auto_resolver(node_model_.settings))
            .on_set_transportfile(make_node_implementation_transportfile_setter(
                node_model_.node_resources, node_model_.settings))
            .on_registration_changed(
                make_node_implementation_registration_handler())
            .on_connection_activated(
                make_node_implementation_connection_activation_handler());
      }

      nmos::registration_handler make_node_implementation_registration_handler()
      {
        internal::ActivationContext ctx{stream_store_, callbacks_, gate_,
                                        receiver_mutex_, sender_mutex_, node_id_,
                                        ptp_domain_number_, runtime_interfaces_,
                                        runtime_interfaces_mutex_};
        return internal::make_registration_handler(ctx);
      }

      nmos::id make_video_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
      }

      nmos::id make_audio_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
      }

      nmos::id make_ancillary_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::data, id);
      }

      // Example Connection API activation callback to perform application-specific
      // operations to complete activation
      nmos::connection_activation_handler
      make_node_implementation_connection_activation_handler()
      {
        internal::ActivationContext ctx{stream_store_, callbacks_, gate_,
                                        receiver_mutex_, sender_mutex_, node_id_,
                                        ptp_domain_number_, runtime_interfaces_,
                                        runtime_interfaces_mutex_};
        return internal::make_activation_handler(ctx);
      }

      nmos::transport_file_parser make_node_implementation_transport_file_parser()
      {
        return internal::make_transport_file_parser();
      }

      // Example Connection API activation callback to resolve "auto" values when
      // /staged is transitioned to /active
      nmos::connection_resource_auto_resolver
      make_node_implementation_auto_resolver(const nmos::settings &settings)
      {
        internal::ActivationContext ctx{stream_store_, callbacks_, gate_,
                                        receiver_mutex_, sender_mutex_, node_id_,
                                        ptp_domain_number_, runtime_interfaces_,
                                        runtime_interfaces_mutex_};
        return internal::make_auto_resolver(settings, ctx);
      }

      nmos::connection_sender_transportfile_setter
      make_node_implementation_transportfile_setter(
          const nmos::resources &node_resources, const nmos::settings &settings)
      {
        internal::ActivationContext ctx{stream_store_, callbacks_, gate_,
                                        receiver_mutex_, sender_mutex_, node_id_,
                                        ptp_domain_number_, runtime_interfaces_,
                                        runtime_interfaces_mutex_};
        return internal::make_transportfile_setter(node_resources, settings, ctx);
      }

      bool insert_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources,
                                 nmos::resource &&resource, slog::base_gate &gate,
                                 nmos::write_lock &lock)
      {
        return lifecycle_service().insert_after(milliseconds, resources,
                                                std::move(resource), gate,
                                                lock);
      }

      bool remove_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources, nmos::id id,
                                 slog::base_gate &gate, nmos::write_lock &lock)
      {
        return lifecycle_service().remove_after(milliseconds, resources, id,
                                                gate, lock);
      }

      void erase_resource_if_present(nmos::resources &resources, const nmos::id &id)
      {
        lifecycle_service().erase_if_present(resources, id);
      }
      int nmos_node_start()
      {
        internal::NodeServerRuntime::ServerContext ctx{
            node_model_,
            settings_,
            gate_,
            lifecycle_mutex_,
            lifecycle_state_,
            lifecycle_cv_,
            stop_requested_};
        auto node_impl = make_node_implementation();
        return internal::NodeServerRuntime::start(
            ctx, [this] { thread_run(); }, node_impl);
      }
      bool start()
      {
        {
          std::scoped_lock<std::mutex, std::mutex> lock(lifecycle_mutex_,
                                                        thread_mutex_);
          if (LifecycleState::running == lifecycle_state_)
          {
            return true;
          }

          if (LifecycleState::starting == lifecycle_state_ ||
              LifecycleState::stopping == lifecycle_state_ || thread_.joinable())
          {
            if (gate_)
            {
              slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
                  << nmos::stash_category(impl::categories::node_implementation)
                  << "start ignored: node thread already active";
            }
            return false;
          }

          stop_requested_ = false;
          if (needs_model_reset_)
          {
            reset_model_state();
            needs_model_reset_ = false;
          }
          lifecycle_state_ = LifecycleState::starting;

          try
          {
            thread_ = std::thread([this]
                                  {
          nmos_node_start();
          {
            std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
            lifecycle_state_ = LifecycleState::stopped;
            stop_requested_ = false;
          }
          lifecycle_cv_.notify_all(); });
          }
          catch (...)
          {
            lifecycle_state_ = LifecycleState::stopped;
            stop_requested_ = false;
            lifecycle_cv_.notify_all();
            return false;
          }
        }

        std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
        lifecycle_cv_.wait(lifecycle_lock, [&]
                           { return LifecycleState::starting != lifecycle_state_; });

        return LifecycleState::running == lifecycle_state_;
      }

      void thread_run()
      {
        nmos::details::omanip_gate gate{
            *gate_, nmos::stash_category(impl::categories::node_implementation)};
        try
        {
          init();
          node_implementation_run();
        }
        catch (const node_implementation_init_exception &)
        {
          // node_implementation_init writes the log message
        }
        catch (const web::json::json_exception &e)
        {
          // most likely from incorrect value types in the command line settings
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "JSON error: " << e.what();
        }
        catch (const std::system_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "System error: " << e.what() << " [" << e.code() << "]";
        }
        catch (const std::runtime_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Implementation error: " << e.what();
        }
        catch (const std::exception &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Unexpected exception: " << e.what();
        }
        catch (...)
        {
          slog::log<slog::severities::severe>(gate, SLOG_FLF)
              << "Unexpected unknown exception";
        }
      }

      void init()
      {
        init_device();
        reinsert_cached_resources();
        resolve_auto = make_node_implementation_auto_resolver(node_model_.settings);
        set_transportfile = make_node_implementation_transportfile_setter(
            node_model_.node_resources, node_model_.settings);
      }

      void init_device()
      {
        nmos::write_lock lock = node_model_.write_lock();

        const auto seed_id =
            nmos::experimental::fields::seed_id(node_model_.settings);
        const auto node_id = impl::make_id(seed_id, nmos::types::node);
        const auto device_id = impl::make_id(seed_id, nmos::types::device);
        node_id_ = node_id;
        device_id_ = device_id;
        seed_id_ = seed_id;                                                                            
        const auto clocks = web::json::value_of({nmos::make_ptp_clock(nmos::clock_names::clk0, false, "00-00-00-00-00-00-00-00", false)});

        // filter network interfaces to those that correspond to the specified
        // host_addresses
        const auto host_interfaces =
            nmos::get_host_interfaces(node_model_.settings);
        {
          std::lock_guard<std::mutex> runtime_interfaces_lock(
              runtime_interfaces_mutex_);
          if (runtime_interfaces_.empty())
          {
            runtime_interfaces_ = host_interfaces;
          }
        }
        const auto interfaces =
            nmos::experimental::node_interfaces(host_interfaces);


        // example node
        {
          auto node = nmos::make_node(node_id, clocks,
                                      nmos::make_node_interfaces(interfaces),
                                      node_model_.settings);
          const auto& interfaces = node.data[nmos::fields::interfaces];
        slog::log<slog::severities::info>(*gate_, SLOG_FLF) << "node interfaces : "<<  interfaces.serialize();
          utility::string_t clocks = node.data[nmos::fields::clocks].serialize();
          node.data[nmos::fields::tags] =
              impl::fields::node_tags(node_model_.settings);
          if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                     std::move(node), *gate_, lock))
            throw node_implementation_init_exception("insert node failed!");
        }
        #ifdef HAVE_LLDP
          slog::log<slog::severities::info>(*gate_, SLOG_FLF) << "Attempting to configure LLDP";
          auto lldp_manager = nmos::experimental::make_lldp_manager(node_model_, interfaces, true, *gate_);
          lldp_manager_= std::make_shared<lldp::lldp_manager>(std::move(lldp_manager));
          // hm, open may potentially throw?
          // lldp::lldp_manager_guard lldp_manager_guard(lldp_manager);
          lldp_manager_guard_ = std::make_shared<lldp::lldp_manager_guard>(*lldp_manager_);
       #endif
        {
          std::vector<nmos::id> empty;
          auto device = nmos::make_device(device_id, node_id, empty, empty,
                                          node_model_.settings);
          device.data[nmos::fields::tags] =
              impl::fields::device_tags(node_model_.settings);
          if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                     std::move(device), *gate_, lock))
            throw node_implementation_init_exception("insert device failed!");
        }
      }

      void set_ptp_clock(std::string gmid_, bool locked_, int ptp_domain)
      {
        const auto normalized_gmid = normalize_ptp_gmid(gmid_);
        auto lock = node_model_.write_lock();
        ptp_domain_number_ = 0 <= ptp_domain && ptp_domain <= 127 ? ptp_domain : 127;
        if (is_valid_ptp_gmid(normalized_gmid))
        {
          slog::log<slog::severities::info>(*gate_, SLOG_FLF) << "Setting PTP clock GMID to " << normalized_gmid << " and locked to " << locked_ << " with domain number " << ptp_domain_number_;
               nmos::modify_resource(node_model_.node_resources, node_id_, ([&](nmos::resource &node)
                                                                     { node.data[nmos::fields::clocks] = web::json::value_of(
                                                                           { nmos::make_ptp_clock(nmos::clock_names::clk0, false,
                                                                                                       utility::s2us(normalized_gmid), locked_)}); })); 
        }
        
  
        for (const auto &sender_id : stream_store_.sender_ids())
        {
          auto sender = nmos::find_resource(node_model_.node_resources,
                                            {sender_id, nmos::types::sender});
          if (node_model_.node_resources.end() == sender)
          {
            continue;
          }

          nmos::modify_resource(node_model_.connection_resources, sender_id,
                                [&](nmos::resource &connection_sender)
                                {
                                  auto &endpoint_transportfile =
                                      connection_sender.data[nmos::fields::endpoint_transportfile];
                                  set_transportfile(*sender, connection_sender,
                                                    endpoint_transportfile);
                        std::string transportfile_json = endpoint_transportfile.serialize();
                        slog::log<slog::severities::info>(*gate_, SLOG_FLF) << "Updated transportfile for sender " << sender_id << ": " << transportfile_json;
                                                  });
        }
      }

      void reset_model_state()
      {
        auto lock = node_model_.write_lock();
        node_model_.shutdown = false;
        node_model_.node_resources.clear();
        node_model_.connection_resources.clear();
        node_model_.events_resources.clear();
        node_model_.channelmapping_resources.clear();
        node_model_.control_protocol_resources.clear();
        node_model_.settings = web::json::value::object();

        stream_store_.clear_resource_ids();
        node_ids_.clear();
        device_ids_.clear();
      }

      void reinsert_cached_resources()
      {
        bool has_senders = false;
        bool has_receivers = false;
        {
          std::lock_guard<std::mutex> sender_lock(sender_mutex_);
          has_senders = stream_store_.has_senders();
        }
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          has_receivers = stream_store_.has_receivers();
        }
        if (!has_senders && !has_receivers)
        {
          return;
        }

        std::vector<VideoSender> cached_video_senders;
        std::vector<AudioSender> cached_audio_senders;
        std::vector<AncillarySender> cached_ancillary_senders;
        {
          std::lock_guard<std::mutex> sender_lock(sender_mutex_);
          cached_video_senders = stream_store_.video_senders();
          cached_audio_senders = stream_store_.audio_senders();
          cached_ancillary_senders = stream_store_.ancillary_senders();
          stream_store_.clear_senders();
        }

        std::vector<VideoReceiver> cached_video_receivers;
        std::vector<AudioReceiver> cached_audio_receivers;
        std::vector<AncillaryReceiver> cached_ancillary_receivers;
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          cached_video_receivers = stream_store_.video_receivers();
          cached_audio_receivers = stream_store_.audio_receivers();
          cached_ancillary_receivers = stream_store_.ancillary_receivers();
          stream_store_.clear_receivers();
        }

        for (const auto &video : cached_video_senders)
        {
          add_video_sender(video);
        }
        for (const auto &audio : cached_audio_senders)
        {
          add_audio_sender(audio);
        }
        for (const auto &ancillary : cached_ancillary_senders)
        {
          add_ancillary_sender(ancillary);
        }
        for (const auto &video : cached_video_receivers)
        {
          add_video_receiver(video);
        }
        for (const auto &audio : cached_audio_receivers)
        {
          add_audio_receiver(audio);
        }
        for (const auto &ancillary : cached_ancillary_receivers)
        {
          add_ancillary_receiver(ancillary);
        }
      }

      void add_video_sender(VideoSender video)
      {
        resource_controller_.add_video_sender(std::move(video));
      }

      void add_audio_sender(AudioSender audio)
      {
        resource_controller_.add_audio_sender(std::move(audio));
      }

      void add_ancillary_sender(AncillarySender ancillary)
      {
        resource_controller_.add_ancillary_sender(std::move(ancillary));
      }

      /**
        初始化设备
      */

      void add_video_receiver(VideoReceiver video)
      {
        resource_controller_.add_video_receiver(std::move(video));
      }

      void add_audio_receiver(AudioReceiver audio)
      {
        resource_controller_.add_audio_receiver(std::move(audio));
      }

      void add_ancillary_receiver(AncillaryReceiver ancillary)
      {
        resource_controller_.add_ancillary_receiver(std::move(ancillary));
      }

      void remove_audio_sender(std::string id)
      {
        resource_controller_.remove_audio_sender(std::move(id));
      }

      void remove_video_sender(std::string id)
      {
        resource_controller_.remove_video_sender(std::move(id));
      }

      void remove_ancillary_sender(std::string id)
      {
        resource_controller_.remove_ancillary_sender(std::move(id));
      }

      void remove_video_receiver(std::string id)
      {
        resource_controller_.remove_video_receiver(std::move(id));
      }

      void remove_audio_receiver(std::string id)
      {
        resource_controller_.remove_audio_receiver(std::move(id));
      }
      void remove_ancillary_receiver(std::string id)
      {
        resource_controller_.remove_ancillary_receiver(std::move(id));
      }
      using SenderResources = internal::SenderResources;
      using ReceiverResources = internal::ReceiverResources;

      internal::ResourceLifecycleService lifecycle_service()
      {
        return internal::ResourceLifecycleService(node_model_);
      }

      bool replace_node_resource(nmos::resource replacement)
      {
        return lifecycle_service().replace_node_resource(std::move(replacement));
      }

      bool replace_connection_resource(nmos::resource replacement)
      {
        return lifecycle_service().replace_connection_resource(
            std::move(replacement));
      }

      void update_video_sender(VideoSender video)
      {
        resource_controller_.update_video_sender(std::move(video));
      }

      void update_audio_sender(AudioSender audio)
      {
        resource_controller_.update_audio_sender(std::move(audio));
      }

      void update_video_receiver(VideoReceiver video)
      {
        resource_controller_.update_video_receiver(std::move(video));
      }

      void update_audio_receiver(AudioReceiver audio)
      {
        resource_controller_.update_audio_receiver(std::move(audio));
      }

      void update_ancillary_sender(AncillarySender ancillary)
      {
        resource_controller_.update_ancillary_sender(std::move(ancillary));
      }

      void update_ancillary_receiver(AncillaryReceiver ancillary)
      {
        resource_controller_.update_ancillary_receiver(std::move(ancillary));
      }

      void set_update_video_sender_callback(
          VideoSenderCallback func)
      {
        event_bridge_.set_update_video_sender_callback(std::move(func));
      }

      void set_update_audio_sender_callback(
          AudioSenderCallback func)
      {
        event_bridge_.set_update_audio_sender_callback(std::move(func));
      }

      void set_update_ancillary_sender_callback(
          AncillarySenderCallback func)
      {
        event_bridge_.set_update_ancillary_sender_callback(std::move(func));
      }

      void set_update_video_receiver_callback(
          VideoReceiverCallback func)
      {
        event_bridge_.set_update_video_receiver_callback(std::move(func));
      }

      void set_update_audio_receiver_callback(
          AudioReceiverCallback func)
      {
        event_bridge_.set_update_audio_receiver_callback(std::move(func));
      }

      void set_update_ancillary_receiver_callback(
          AncillaryReceiverCallback func)
      {
        event_bridge_.set_update_ancillary_receiver_callback(std::move(func));
      }

      void set_registration_changed_callback(
          RegistrationChangedCallback func)
      {
        event_bridge_.set_registration_changed_callback(std::move(func));
      }

      void set_receiver_event_handler(ReceiverEventHandler handler)
      {
        event_bridge_.set_receiver_event_handler(std::move(handler));
      }

      void set_sender_event_handler(SenderEventHandler handler)
      {
        event_bridge_.set_sender_event_handler(std::move(handler));
      }

      void set_registration_event_handler(RegistrationEventHandler handler)
      {
        event_bridge_.set_registration_event_handler(std::move(handler));
      }

      void set_runtime_interfaces(RuntimeInterfaces interfaces)
      {
        runtime_interface_updater_.set_runtime_interfaces(std::move(interfaces));
      }

      NodeSettingsJson effective_settings() const
      {
        auto lock = node_model_.read_lock();
        return node_model_.settings;
      }

      NodeSettingsJson persisted_settings() const
      {
        return settings_.persisted_settings();
      }

      NodeSettingsJson discover_registration_apis() const
      {
        const auto settings = effective_settings();
        return settings_.discover_registration_apis(settings, *gate_);
      }

      void write_persisted_settings(const NodeSettingsJson &settings)
      {
        settings_.write_persisted_settings(settings);
      }

      void update_subscription_for_pair(const nmos::id &receiver_resource_id,
                                        const nmos::id &sender_id)
      {
        if (receiver_resource_id.empty() || sender_id.empty())
        {
          return;
        }
        const auto activation_time = nmos::tai_now();
        bool updated = nmos::modify_resource(
            node_model_.node_resources, receiver_resource_id,
            [&](nmos::resource &resource)
            {
              nmos::set_resource_subscription(resource, true, sender_id,
                                              activation_time);
            });
        updated = nmos::modify_resource(
                      node_model_.node_resources, sender_id,
                      [&](nmos::resource &resource)
                      {
                        nmos::set_resource_subscription(resource, true, receiver_resource_id,
                                                        activation_time);
                      }) ||
                  updated;
        if (updated)
        {
          node_model_.notify();
        }
      }

      };

    Node::Node(std::string node_config_path_) : p_impl(new Impl)
    {
      p_impl->settings_ = internal::NodeSettings(std::move(node_config_path_));
    }
    Node::~Node() {}
    bool Node::start() { return p_impl->start(); }
    bool Node::stop() { return p_impl->stop(); }

    void Node::add_video_sender(VideoSender video)
    {
      p_impl->add_video_sender(video);
    }
    void Node::add_audio_sender(AudioSender audio)
    {
      p_impl->add_audio_sender(audio);
    }
    void Node::add_ancillary_sender(AncillarySender ancillary)
    {
      p_impl->add_ancillary_sender(ancillary);
    }

    void Node::remove_video_sender(std::string id)
    {
      p_impl->remove_video_sender(id);
    }
    void Node::remove_audio_sender(std::string id)
    {
      p_impl->remove_audio_sender(id);
    }
    void Node::remove_ancillary_sender(std::string id)
    {
      p_impl->remove_ancillary_sender(id);
    }

    void Node::update_video_sender(VideoSender video)
    {
      p_impl->update_video_sender(video);
    }
    void Node::update_audio_sender(AudioSender audio)
    {
      p_impl->update_audio_sender(audio);
    }
    void Node::update_ancillary_sender(AncillarySender ancillary)
    {
      p_impl->update_ancillary_sender(ancillary);
    }

    void Node::add_video_receiver(VideoReceiver video)
    {
      p_impl->add_video_receiver(video);
    }
    void Node::add_audio_receiver(AudioReceiver audio)
    {
      p_impl->add_audio_receiver(audio);
    }
    void Node::add_ancillary_receiver(AncillaryReceiver ancillary)
    {
      p_impl->add_ancillary_receiver(ancillary);
    }

    void Node::remove_video_receiver(std::string id)
    {
      p_impl->remove_video_receiver(id);
    }
    void Node::remove_audio_receiver(std::string id)
    {
      p_impl->remove_audio_receiver(id);
    }
    void Node::remove_ancillary_receiver(std::string id)
    {
      p_impl->remove_ancillary_receiver(id);
    }

    void Node::update_video_receiver(VideoReceiver video)
    {
      p_impl->update_video_receiver(video);
    }
    void Node::update_audio_receiver(AudioReceiver audio)
    {
      p_impl->update_audio_receiver(audio);
    }
    void Node::update_ancillary_receiver(AncillaryReceiver ancillary)
    {
      p_impl->update_ancillary_receiver(ancillary);
    }

    void Node::set_update_video_sender_callback(
        VideoSenderCallback func)
    {
      p_impl->set_update_video_sender_callback(std::move(func));
    }

    void Node::set_update_audio_sender_callback(
        AudioSenderCallback func)
    {
      p_impl->set_update_audio_sender_callback(std::move(func));
    }

    void Node::set_update_ancillary_sender_callback(
        AncillarySenderCallback func)
    {
      p_impl->set_update_ancillary_sender_callback(std::move(func));
    }

    void Node::set_update_video_receiver_callback(
        VideoReceiverCallback func)
    {
      p_impl->set_update_video_receiver_callback(std::move(func));
    }

    void Node::set_update_audio_receiver_callback(
        AudioReceiverCallback func)
    {
      p_impl->set_update_audio_receiver_callback(std::move(func));
    }
    void Node::set_update_ancillary_receiver_callback(
        AncillaryReceiverCallback func)
    {
      p_impl->set_update_ancillary_receiver_callback(std::move(func));
    }
    void Node::set_registration_changed_callback(
        RegistrationChangedCallback func)
    {
      p_impl->set_registration_changed_callback(std::move(func));
    }
    void Node::set_receiver_event_handler(ReceiverEventHandler handler)
    {
      p_impl->set_receiver_event_handler(std::move(handler));
    }
    void Node::set_sender_event_handler(SenderEventHandler handler)
    {
      p_impl->set_sender_event_handler(std::move(handler));
    }
    void Node::set_registration_event_handler(RegistrationEventHandler handler)
    {
      p_impl->set_registration_event_handler(std::move(handler));
    }
    NodeSettingsJson Node::effective_settings() const
    {
      return p_impl->effective_settings();
    }
    NodeSettingsJson Node::persisted_settings() const
    {
      return p_impl->persisted_settings();
    }
    NodeSettingsJson Node::discover_registration_apis() const
    {
      return p_impl->discover_registration_apis();
    }
    void Node::write_persisted_settings(const NodeSettingsJson &settings)
    {
      p_impl->write_persisted_settings(settings);
    }
    void Node::set_runtime_interfaces(RuntimeInterfaces interfaces)
    {
      p_impl->set_runtime_interfaces(std::move(interfaces));
    }
    void Node::set_ptp_clock(std::string gmtid, bool locked, int ptp_domain)
    {
      p_impl->set_ptp_clock(gmtid, locked, ptp_domain);
    }

  } // namespace nmos_node
} // namespace seeder
