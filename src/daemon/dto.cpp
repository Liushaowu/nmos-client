#include "../node_types.h"
#include "dto.h"

#include <cpprest/details/basic_types.h>

#include <stdexcept>
#include <string>

using web::json::value;

namespace
{

  utility::string_t to_t(const std::string &value)
  {
    return utility::conversions::to_string_t(value);
  }

  std::string to_utf8(const utility::string_t &value)
  {
    return utility::conversions::to_utf8string(value);
  }

  std::string field_path(const std::string &base, const char *name)
  {
    return base.empty() ? std::string(name) : base + "." + name;
  }

  std::string index_path(const std::string &base, std::size_t index)
  {
    return base + "[" + std::to_string(index) + "]";
  }

  std::runtime_error invalid_field_error(const std::string &path,
                                         const std::string &expected,
                                         const std::string &detail)
  {
    return std::runtime_error("invalid JSON field '" + path + "': expected " +
                              expected + ": " + detail);
  }

  std::runtime_error invalid_field_error(const std::string &path,
                                         const std::string &expected,
                                         const std::exception &error)
  {
    return invalid_field_error(path, expected, error.what());
  }

  void require_field(const value &object, const utility::string_t &name)
  {
    if (!object.is_object() || !object.has_field(name))
    {
      throw std::runtime_error("missing required field: " + to_utf8(name));
    }
  }

  const value &require_object_value(const value &object, const std::string &path)
  {
    if (!object.is_object())
    {
      throw invalid_field_error(path, "object", "not an object");
    }
    return object;
  }

  value require_array_field(const value &object, const char *name,
                            const std::string &path_base = {})
  {
    const auto field = to_t(name);
    const auto path = field_path(path_base, name);
    require_field(object, field);
    if (!object.at(field).is_array())
    {
      throw invalid_field_error(path, "array", "not an array");
    }
    return object.at(field);
  }

  std::string get_string_or(const value &object, const char *name,
                            const std::string &fallback = {},
                            const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return fallback;
    }
    const auto path = field_path(path_base, name);
    try
    {
      return to_utf8(object.at(field).as_string());
    }
    catch (const std::exception &error)
    {
      throw invalid_field_error(path, "string", error);
    }
  }

  bool get_bool_or(const value &object, const char *name, bool fallback = false,
                   const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return fallback;
    }
    const auto path = field_path(path_base, name);
    try
    {
      const auto &raw = object.at(field);
      if (raw.is_boolean())
      {
        return raw.as_bool();
      }
      if (raw.is_integer())
      {
        return 0 != raw.as_integer();
      }
      return raw.as_bool();
    }
    catch (const std::exception &error)
    {
      throw invalid_field_error(path, "boolean", error);
    }
  }

  int get_int_or(const value &object, const char *name, int fallback = 0,
                 const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return fallback;
    }
    const auto path = field_path(path_base, name);
    try
    {
      return object.at(field).as_integer();
    }
    catch (const std::exception &error)
    {
      throw invalid_field_error(path, "integer", error);
    }
  }

  std::int64_t get_int64_or(const value &object, const char *name,
                            std::int64_t fallback = 0,
                            const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return fallback;
    }
    const auto path = field_path(path_base, name);
    try
    {
      return object.at(field).as_number().to_int64();
    }
    catch (const std::exception &error)
    {
      throw invalid_field_error(path, "integer", error);
    }
  }

  double get_double_or(const value &object, const char *name,
                       double fallback = 0.0,
                       const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return fallback;
    }
    const auto path = field_path(path_base, name);
    try
    {
      return object.at(field).as_double();
    }
    catch (const std::exception &error)
    {
      throw invalid_field_error(path, "number", error);
    }
  }

  value json_string(const std::string &text) { return value::string(to_t(text)); }

  std::vector<std::string> string_vector_from_json(const value &object,
                                                   const char *name,
                                                   const std::string &path_base = {})
  {
    const auto field = to_t(name);
    if (!object.is_object() || !object.has_field(field) ||
        object.at(field).is_null())
    {
      return {};
    }

    const auto path = field_path(path_base, name);
    if (!object.at(field).is_array())
    {
      throw invalid_field_error(path, "array", "not an array");
    }

    std::vector<std::string> items;
    std::size_t index = 0;
    for (const auto &item : object.at(field).as_array())
    {
      if (!item.is_null())
      {
        try
        {
          items.push_back(to_utf8(item.as_string()));
        }
        catch (const std::exception &error)
        {
          throw invalid_field_error(index_path(path, index), "string", error);
        }
      }
      ++index;
    }
    return items;
  }

  value string_vector_to_json(const std::vector<std::string> &items)
  {
    value array = value::array();
    for (std::size_t index = 0; index < items.size(); ++index)
    {
      array[index] = json_string(items[index]);
    }
    return array;
  }

  seeder::nmos_sync::PtpClockDto::Entry ptp_entry_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_sync::PtpClockDto::Entry entry;
    entry.clock_accuracy = get_int_or(object, "clock_accuracy", 0, path);
    entry.clock_class = get_int_or(object, "clock_class", 0, path);
    entry.grandmaster_identity =
        get_string_or(object, "grandmaster_identity", {}, path);
    entry.grandmaster_priority1 =
        get_int_or(object, "grandmaster_priority1", 0, path);
    entry.grandmaster_priority2 =
        get_int_or(object, "grandmaster_priority2", 0, path);
    entry.is_active = get_bool_or(object, "is_active", false, path);
    entry.is_connected = get_bool_or(object, "is_connected", false, path);
    entry.is_locked = get_bool_or(object, "is_locked", false, path);
    entry.master_initialized =
        get_bool_or(object, "master_initialized", false, path);
    entry.master_port_id = get_string_or(object, "master_port_id", {}, path);
    entry.master_utc_offset = get_int_or(object, "master_utc_offset", 0, path);
    entry.offset = get_double_or(object, "offset", 0.0, path);
    entry.offset_scaled_log_variance =
        get_int_or(object, "offset_scaled_log_variance", 0, path);
    entry.t1_domain_number = get_int_or(object, "t1_domain_number", 127, path);
    return entry;
  }

  value ptp_entry_to_json(const seeder::nmos_sync::PtpClockDto::Entry &entry)
  {
    value object = value::object();
    object[to_t("clock_accuracy")] = value::number(entry.clock_accuracy);
    object[to_t("clock_class")] = value::number(entry.clock_class);
    object[to_t("grandmaster_identity")] =
        json_string(entry.grandmaster_identity);
    object[to_t("grandmaster_priority1")] =
        value::number(entry.grandmaster_priority1);
    object[to_t("grandmaster_priority2")] =
        value::number(entry.grandmaster_priority2);
    object[to_t("is_active")] = value::boolean(entry.is_active);
    object[to_t("is_connected")] = value::boolean(entry.is_connected);
    object[to_t("is_locked")] = value::boolean(entry.is_locked);
    object[to_t("master_initialized")] =
        value::boolean(entry.master_initialized);
    object[to_t("master_port_id")] = json_string(entry.master_port_id);
    object[to_t("master_utc_offset")] = value::number(entry.master_utc_offset);
    object[to_t("offset")] = value::number(entry.offset);
    object[to_t("offset_scaled_log_variance")] =
        value::number(entry.offset_scaled_log_variance);
    object[to_t("t1_domain_number")] = value::number(entry.t1_domain_number);
    return object;
  }

  std::vector<seeder::nmos_sync::PtpClockDto::Entry> ptp_entries_from_json(
      const value &value, const std::string &path)
  {
    std::vector<seeder::nmos_sync::PtpClockDto::Entry> entries;
    if (value.is_array())
    {
      std::size_t index = 0;
      for (const auto &item : value.as_array())
      {
        if (item.is_object())
        {
          entries.push_back(ptp_entry_from_json(item, index_path(path, index)));
        }
        ++index;
      }
      return entries;
    }

    if (value.is_object() && value.has_field(to_t("entries")) &&
        value.at(to_t("entries")).is_array())
    {
      std::size_t index = 0;
      for (const auto &item : value.at(to_t("entries")).as_array())
      {
        if (item.is_object())
        {
          entries.push_back(ptp_entry_from_json(
              item, index_path(field_path(path, "entries"), index)));
        }
        ++index;
      }
      return entries;
    }

    if (value.is_object())
    {
      const auto gmid = get_string_or(value, "gmid", {}, path);
      if (!gmid.empty() || value.has_field(to_t("locked")))
      {
        seeder::nmos_sync::PtpClockDto::Entry entry;
        entry.grandmaster_identity = gmid;
        entry.is_locked = get_bool_or(value, "locked", false, path);
        entry.is_active = true;
        entry.is_connected = true;
        entry.master_initialized = true;
        entries.push_back(entry);
      }
    }

    return entries;
  }

  value ptp_entries_to_json(const std::vector<seeder::nmos_sync::PtpClockDto::Entry> &entries)
  {
    value array = value::array();
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
      array[index] = ptp_entry_to_json(entries[index]);
    }
    return array;
  }

  seeder::nmos_sync::SnapshotDto::DeviceDto device_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_sync::SnapshotDto::DeviceDto device;
    device.device = get_string_or(object, "device", {}, path);
    device.display_name = get_string_or(object, "display_name", {}, path);
    device.enable = get_bool_or(object, "enable", false, path);
    device.id = get_int_or(object, "id", 0, path);
    device.sip = get_string_or(object, "sip", {}, path);
    return device;
  }

  value device_to_json(const seeder::nmos_sync::SnapshotDto::DeviceDto &device)
  {
    value object = value::object();
    object[to_t("device")] = json_string(device.device);
    object[to_t("display_name")] = json_string(device.display_name);
    object[to_t("enable")] = value::boolean(device.enable);
    object[to_t("id")] = value::number(device.id);
    object[to_t("sip")] = json_string(device.sip);
    return object;
  }

  template <typename T>
  value vector_to_json(const std::vector<T> &items,
                       value (*mapper)(const T &item))
  {
    value array = value::array();
    for (std::size_t index = 0; index < items.size(); ++index)
    {
      array[index] = mapper(items[index]);
    }
    return array;
  }

  seeder::nmos_node::Redundancy redundancy_from_json(
      const value &object, const std::string &path)
  {
    if (!object.is_object())
    {
      throw invalid_field_error(path, "object", "not an object");
    }

    seeder::nmos_node::Redundancy redundancy;
    redundancy.present = true;
    redundancy.enable = get_bool_or(object, "enable", false, path);
    redundancy.source_ip = get_string_or(object, "source_ip", {}, path);
    redundancy.ip = get_string_or(object, "ip", {}, path);
    redundancy.port = get_int_or(object, "port", 0, path);
    return redundancy;
  }

  value redundancy_to_json(const seeder::nmos_node::Redundancy &redundancy)
  {
    value object = value::object();
    object[to_t("enable")] = value::boolean(redundancy.enable);
    object[to_t("source_ip")] = json_string(redundancy.source_ip);
    object[to_t("ip")] = json_string(redundancy.ip);
    object[to_t("port")] = value::number(redundancy.port);
    return object;
  }

  seeder::nmos_node::VideoSender video_sender_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::VideoSender sender;
    sender.id = get_string_or(object, "id", {}, path);
    sender.name = get_string_or(object, "name", {}, path);
    sender.enable = get_bool_or(object, "enable", false, path);
    sender.video_format = get_string_or(object, "video_format", {}, path);
    sender.colorspace =
        get_string_or(object, "colorspace", sender.colorspace, path);
    sender.transfer_characteristics = get_string_or(
        object, "transfer_characteristics", sender.transfer_characteristics, path);
    sender.source_ip = get_string_or(object, "source_ip", {}, path);
    sender.ip = get_string_or(object, "ip", {}, path);
    sender.port = get_int_or(object, "port", 0, path);
    sender.pg_format = get_int_or(object, "pg_format", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      sender.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                             field_path(path, "redundancy"));
    }
    return sender;
  }

  value video_sender_to_json(const seeder::nmos_node::VideoSender &sender)
  {
    value object = value::object();
    object[to_t("id")] = json_string(sender.id);
    object[to_t("sender_id")] = json_string(sender.sender_id);
    object[to_t("name")] = json_string(sender.name);
    object[to_t("enable")] = value::boolean(sender.enable);
    object[to_t("video_format")] = json_string(sender.video_format);
    object[to_t("colorspace")] = json_string(sender.colorspace);
    object[to_t("transfer_characteristics")] =
        json_string(sender.transfer_characteristics);
    object[to_t("source_ip")] = json_string(sender.source_ip);
    object[to_t("ip")] = json_string(sender.ip);
    object[to_t("port")] = value::number(sender.port);
    if (sender.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(sender.redundancy);
    }
    object[to_t("pg_format")] = value::number(sender.pg_format);
    return object;
  }

  seeder::nmos_node::AudioSender audio_sender_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::AudioSender sender;
    sender.id = get_string_or(object, "id", {}, path);
    sender.name = get_string_or(object, "name", {}, path);
    sender.enable = get_bool_or(object, "enable", false, path);
    sender.channel_count = get_int_or(object, "channel_count", 0, path);
    sender.bit_depth = get_int_or(object, "bit_depth", 0, path);
    sender.sample_rate = get_int_or(object, "sample_rate", 0, path);
    sender.source_ip = get_string_or(object, "source_ip", {}, path);
    sender.ip = get_string_or(object, "ip", {}, path);
    sender.port = get_int_or(object, "port", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      sender.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                             field_path(path, "redundancy"));
    }
    return sender;
  }

  value audio_sender_to_json(const seeder::nmos_node::AudioSender &sender)
  {
    value object = value::object();
    object[to_t("id")] = json_string(sender.id);
    object[to_t("sender_id")] = json_string(sender.sender_id);
    object[to_t("name")] = json_string(sender.name);
    object[to_t("enable")] = value::boolean(sender.enable);
    object[to_t("channel_count")] = value::number(sender.channel_count);
    object[to_t("bit_depth")] = value::number(sender.bit_depth);
    object[to_t("sample_rate")] = value::number(sender.sample_rate);
    object[to_t("source_ip")] = json_string(sender.source_ip);
    object[to_t("ip")] = json_string(sender.ip);
    object[to_t("port")] = value::number(sender.port);
    if (sender.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(sender.redundancy);
    }
    return object;
  }

  seeder::nmos_node::AncillarySender ancillary_sender_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::AncillarySender sender;
    sender.id = get_string_or(object, "id", {}, path);
    sender.name = get_string_or(object, "name", {}, path);
    sender.format = get_string_or(object, "format", {}, path);
    sender.enable = get_bool_or(object, "enable", false, path);
    sender.source_ip = get_string_or(object, "source_ip", {}, path);
    sender.ip = get_string_or(object, "ip", {}, path);
    sender.port = get_int_or(object, "port", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      sender.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                             field_path(path, "redundancy"));
    }
    return sender;
  }

  value ancillary_sender_to_json(
      const seeder::nmos_node::AncillarySender &sender)
  {
    value object = value::object();
    object[to_t("id")] = json_string(sender.id);
    object[to_t("sender_id")] = json_string(sender.sender_id);
    object[to_t("name")] = json_string(sender.name);
    object[to_t("format")] = json_string(sender.format);
    object[to_t("enable")] = value::boolean(sender.enable);

    object[to_t("source_ip")] = json_string(sender.source_ip);
    object[to_t("ip")] = json_string(sender.ip);
    object[to_t("port")] = value::number(sender.port);
    if (sender.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(sender.redundancy);
    }
    return object;
  }

  seeder::nmos_node::VideoReceiver video_receiver_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::VideoReceiver receiver;
    receiver.id = get_string_or(object, "id", {}, path);
    receiver.name = get_string_or(object, "name", {}, path);
    receiver.enable = get_bool_or(object, "enable", false, path);
    receiver.source_ip = get_string_or(object, "source_ip", {}, path);
    receiver.ip = get_string_or(object, "ip", {}, path);
    receiver.port = get_int_or(object, "port", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      receiver.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                               field_path(path, "redundancy"));
    }
    else
    {
      receiver.redundancy.enable =
          get_bool_or(object, "redundancy_enable", false, path);
    }
    if (object.has_field(to_t("caps")) && object.at(to_t("caps")).is_object())
    {
      const auto &caps = object.at(to_t("caps"));
      const auto caps_path = field_path(path, "caps");
      receiver.caps.formats = string_vector_from_json(caps, "formats", caps_path);
      receiver.caps.colorspaces =
          string_vector_from_json(caps, "colorspaces", caps_path);
      receiver.caps.transfer_characteristics =
          string_vector_from_json(caps, "transfer_characteristics", caps_path);
    }
    receiver.format = get_string_or(object, "format", {}, path);
    receiver.colorspace = get_string_or(object, "colorspace", {}, path);
    receiver.transfer_characteristics =
        get_string_or(object, "transfer_characteristics", {}, path);
    return receiver;
  }

  value video_receiver_to_json(const seeder::nmos_node::VideoReceiver &receiver)
  {
    value object = value::object();
    object[to_t("id")] = json_string(receiver.id);
    object[to_t("name")] = json_string(receiver.name);
    object[to_t("enable")] = value::boolean(receiver.enable);
    object[to_t("source_ip")] = json_string(receiver.source_ip);
    object[to_t("ip")] = json_string(receiver.ip);
    object[to_t("port")] = value::number(receiver.port);
    if (receiver.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(receiver.redundancy);
    }
    if (!receiver.caps.formats.empty() || !receiver.caps.colorspaces.empty() ||
        !receiver.caps.transfer_characteristics.empty())
    {
      value caps = value::object();
      caps[to_t("formats")] = string_vector_to_json(receiver.caps.formats);
      caps[to_t("colorspaces")] =
          string_vector_to_json(receiver.caps.colorspaces);
      caps[to_t("transfer_characteristics")] =
          string_vector_to_json(receiver.caps.transfer_characteristics);
      object[to_t("caps")] = std::move(caps);
    }
    object[to_t("format")] = json_string(receiver.format);
    object[to_t("colorspace")] = json_string(receiver.colorspace);
    object[to_t("transfer_characteristics")] =
        json_string(receiver.transfer_characteristics);
    return object;
  }

  seeder::nmos_node::AudioReceiver audio_receiver_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::AudioReceiver receiver;
    receiver.id = get_string_or(object, "id", {}, path);
    receiver.name = get_string_or(object, "name", {}, path);
    receiver.enable = get_bool_or(object, "enable", false, path);
    receiver.channel_count = get_int_or(object, "channel_count", 0, path);
    receiver.bit_depth = get_int_or(object, "bit_depth", 0, path);
    receiver.sample_rate = get_int_or(object, "sample_rate", 0, path);
    receiver.packet_time = get_double_or(object, "packet_time", 0.0, path);
    receiver.source_ip = get_string_or(object, "source_ip", {}, path);
    receiver.ip = get_string_or(object, "ip", {}, path);
    receiver.port = get_int_or(object, "port", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      receiver.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                               field_path(path, "redundancy"));
    }
    return receiver;
  }

  value audio_receiver_to_json(const seeder::nmos_node::AudioReceiver &receiver)
  {
    value object = value::object();
    object[to_t("id")] = json_string(receiver.id);
    object[to_t("name")] = json_string(receiver.name);
    object[to_t("enable")] = value::boolean(receiver.enable);
    object[to_t("channel_count")] = value::number(receiver.channel_count);
    object[to_t("bit_depth")] = value::number(receiver.bit_depth);
    object[to_t("sample_rate")] = value::number(receiver.sample_rate);
    object[to_t("packet_time")] = value::number(receiver.packet_time);
    object[to_t("source_ip")] = json_string(receiver.source_ip);
    object[to_t("ip")] = json_string(receiver.ip);
    object[to_t("port")] = value::number(receiver.port);
    if (receiver.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(receiver.redundancy);
    }
    return object;
  }

  seeder::nmos_node::AncillaryReceiver ancillary_receiver_from_json(
      const value &object, const std::string &path)
  {
    seeder::nmos_node::AncillaryReceiver receiver;
    receiver.id = get_string_or(object, "id", {}, path);
    receiver.name = get_string_or(object, "name", {}, path);
    receiver.format = get_string_or(object, "format", {}, path);
    receiver.enable = get_bool_or(object, "enable", false, path);
    receiver.source_ip = get_string_or(object, "source_ip", {}, path);
    receiver.ip = get_string_or(object, "ip", {}, path);
    receiver.port = get_int_or(object, "port", 0, path);
    if (object.has_field(to_t("redundancy")))
    {
      receiver.redundancy = redundancy_from_json(object.at(to_t("redundancy")),
                                               field_path(path, "redundancy"));
    }
    return receiver;
  }

  value ancillary_receiver_to_json(
      const seeder::nmos_node::AncillaryReceiver &receiver)
  {
    value object = value::object();
    object[to_t("id")] = json_string(receiver.id);
    object[to_t("name")] = json_string(receiver.name);
    object[to_t("format")] = json_string(receiver.format);
    object[to_t("enable")] = value::boolean(receiver.enable);
    object[to_t("source_ip")] = json_string(receiver.source_ip);
    object[to_t("ip")] = json_string(receiver.ip);
    object[to_t("port")] = value::number(receiver.port);
    if (receiver.redundancy.present)
    {
      object[to_t("redundancy")] = redundancy_to_json(receiver.redundancy);
    }
    return object;
  }

  template <typename Receiver>
  value make_receiver_changed_message(const std::string &type_name,
                                      const Receiver &receiver,
                                      value (*serializer)(const Receiver &))
  {
    value object = value::object();
    object[to_t("type")] = json_string(type_name);
    object[to_t("payload")] = serializer(receiver);
    return object;
  }

  template <typename Receiver>
  value make_receiver_validation_message(const std::string &type_name,
                                         const Receiver &receiver,
                                         value (*serializer)(const Receiver &),
                                         const std::string &request_id,
                                         const std::string &receiver_id)
  {
    value object = value::object();
    object[to_t("type")] = json_string(type_name);
    object[to_t("request_id")] = json_string(request_id);
    object[to_t("receiver_id")] = json_string(receiver_id);
    object[to_t("payload")] = serializer(receiver);
    return object;
  }

  template <typename Sender>
  value make_sender_message(const std::string &type_name, const Sender &sender,
                            value (*serializer)(const Sender &))
  {
    value object = value::object();
    object[to_t("type")] = json_string(type_name);
    object[to_t("payload")] = serializer(sender);
    return object;
  }

}

namespace seeder::nmos_sync
{

  std::string PtpClockDto::effective_gmid() const
  {
    for (const auto &entry : entries)
    {
      if (entry.is_active && entry.is_connected && entry.master_initialized &&
          !entry.grandmaster_identity.empty())
      {
        return entry.grandmaster_identity;
      }
    }
    return {};
  }

  bool PtpClockDto::effective_locked() const
  {
    for (const auto &entry : entries)
    {
      if (entry.is_active && entry.is_connected && entry.master_initialized)
      {
        return entry.is_locked;
      }
    }
    return false;
  }

  int PtpClockDto::effective_ptp_domain() const
  {
    for (const auto &entry : entries)
    {
      if (entry.is_active && entry.is_connected && entry.master_initialized)
      {
        return 0 <= entry.t1_domain_number && entry.t1_domain_number <= 127
                   ? entry.t1_domain_number
                   : 127;
      }
    }
    return 127;
  }

  bool PtpClockDto::empty() const
  {
    return entries.empty();
  }

  bool SnapshotDto::has_any_streams() const
  {
    return !video_senders.empty() || !audio_senders.empty() ||
           !ancillary_senders.empty() || !video_receivers.empty() ||
           !audio_receivers.empty() || !ancillary_receivers.empty();
  }

  SnapshotDto snapshot_from_json(const value &root)
  {
    require_object_value(root, "snapshot");
    const auto senders = require_array_field(root, "senders");
    const auto receivers = require_array_field(root, "receivers");

    SnapshotDto snapshot;
    if (root.has_field(to_t("ptp_clock")))
    {
      snapshot.ptp_clock.entries =
          ptp_entries_from_json(root.at(to_t("ptp_clock")), "ptp_clock");
    }
    if (root.has_field(to_t("devices")) && !root.at(to_t("devices")).is_null())
    {
      const auto devices = require_array_field(root, "devices");
      std::size_t index = 0;
      for (const auto &device : devices.as_array())
      {
        if (device.is_object())
        {
          snapshot.devices.push_back(
              device_from_json(device, index_path("devices", index)));
        }
        ++index;
      }
    }

    std::size_t sender_index = 0;
    for (const auto &sender : senders.as_array())
    {
      const auto sender_path = index_path("senders", sender_index);
      const auto &sender_object = require_object_value(sender, sender_path);
      if (sender_object.has_field(to_t("video")))
      {
        const auto video_path = field_path(sender_path, "video");
        snapshot.video_senders.push_back(video_sender_from_json(
            require_object_value(sender_object.at(to_t("video")), video_path),
            video_path));
      }
      if (sender_object.has_field(to_t("audio")))
      {
        const auto audio_path = field_path(sender_path, "audio");
        snapshot.audio_senders.push_back(audio_sender_from_json(
            require_object_value(sender_object.at(to_t("audio")), audio_path),
            audio_path));
      }
      if (sender_object.has_field(to_t("ancillary")))
      {
        const auto ancillary_path = field_path(sender_path, "ancillary");
        snapshot.ancillary_senders.push_back(ancillary_sender_from_json(
            require_object_value(sender_object.at(to_t("ancillary")),
                                 ancillary_path),
            ancillary_path));
      }
      ++sender_index;
    }

    std::size_t receiver_index = 0;
    for (const auto &receiver : receivers.as_array())
    {
      const auto receiver_path = index_path("receivers", receiver_index);
      const auto &receiver_object = require_object_value(receiver, receiver_path);
      if (receiver_object.has_field(to_t("video")))
      {
        const auto video_path = field_path(receiver_path, "video");
        snapshot.video_receivers.push_back(video_receiver_from_json(
            require_object_value(receiver_object.at(to_t("video")), video_path),
            video_path));
      }
      if (receiver_object.has_field(to_t("audio")))
      {
        const auto audio_path = field_path(receiver_path, "audio");
        snapshot.audio_receivers.push_back(audio_receiver_from_json(
            require_object_value(receiver_object.at(to_t("audio")), audio_path),
            audio_path));
      }
      if (receiver_object.has_field(to_t("ancillary")))
      {
        const auto ancillary_path = field_path(receiver_path, "ancillary");
        snapshot.ancillary_receivers.push_back(ancillary_receiver_from_json(
            require_object_value(receiver_object.at(to_t("ancillary")),
                                 ancillary_path),
            ancillary_path));
      }
      ++receiver_index;
    }

    return snapshot;
  }

  web::json::value snapshot_to_json(const SnapshotDto &snapshot)
  {
    value root = value::object();
    root[to_t("ptp_clock")] = ptp_entries_to_json(snapshot.ptp_clock.entries);
    root[to_t("devices")] = vector_to_json(snapshot.devices, device_to_json);

    value senders = value::object();
    senders[to_t("video")] =
        vector_to_json(snapshot.video_senders, video_sender_to_json);
    senders[to_t("audio")] =
        vector_to_json(snapshot.audio_senders, audio_sender_to_json);
    senders[to_t("ancillary")] =
        vector_to_json(snapshot.ancillary_senders, ancillary_sender_to_json);
    root[to_t("senders")] = senders;

    value receivers = value::object();
    receivers[to_t("video")] =
        vector_to_json(snapshot.video_receivers, video_receiver_to_json);
    receivers[to_t("audio")] =
        vector_to_json(snapshot.audio_receivers, audio_receiver_to_json);
    receivers[to_t("ancillary")] =
        vector_to_json(snapshot.ancillary_receivers, ancillary_receiver_to_json);
    root[to_t("receivers")] = receivers;
    return root;
  }

  std::optional<SnapshotChangedMessage>
  snapshot_changed_message_from_json(const value &root)
  {
    if (!root.is_object() || !root.has_field(to_t("type")))
    {
      return std::nullopt;
    }
    if (get_string_or(root, "type") != "data.changed")
    {
      return std::nullopt;
    }

    SnapshotChangedMessage message;
    message.revision = get_int64_or(root, "revision", 0);
    message.reason = get_string_or(root, "reason");
    return message;
  }

  std::optional<ConnectionResultMessage>
  connection_validation_result_message_from_json(const value &root)
  {
    if (!root.is_object() || !root.has_field(to_t("type")))
    {
      return std::nullopt;
    }
if (get_string_or(root, "type") != "connection.validation.result")
  {
    return std::nullopt;
  }

  require_field(root, to_t("request_id"));
  require_field(root, to_t("success"));

  ConnectionResultMessage message;
  message.request_id = get_string_or(root, "request_id");
  if (message.request_id.empty())
  {
    throw std::runtime_error("connection.validation.result request_id is empty");
  }
    message.receiver_id = get_string_or(root, "receiver_id");
    message.success = get_bool_or(root, "success");
    message.reason = get_string_or(root, "reason");
    return message;
  }

  web::json::value make_node_lifecycle_message(const std::string &old_state,
                                               const std::string &new_state)
  {
    value root = value::object();
    root[to_t("type")] = json_string("node.lifecycle_changed");
    value payload = value::object();
    payload[to_t("old_state")] = json_string(old_state);
    payload[to_t("new_state")] = json_string(new_state);
    root[to_t("payload")] = payload;
    return root;
  }

  web::json::value make_sync_failed_message(const SyncFailedMessage &message)
  {
    value root = value::object();
    root[to_t("type")] = json_string("sync.failed");
    root[to_t("revision")] = value::number(message.revision);
    root[to_t("message")] = json_string(message.message);
    return root;
  }

  web::json::value make_streams_drained_message()
  {
    value root = value::object();
    root[to_t("type")] =
        json_string("streams.drained_due_to_ws_disconnect");
    root[to_t("message")] =
        json_string("WebSocket disconnected, all registered senders/receivers removed");
    return root;
  }

  web::json::value make_sender_video_observed_changed_message(
      const nmos_node::VideoSender &sender)
  {
    return make_sender_message("sender.video.observed_changed", sender,
                               video_sender_to_json);
  }

  web::json::value make_sender_audio_observed_changed_message(
      const nmos_node::AudioSender &sender)
  {
    return make_sender_message("sender.audio.observed_changed", sender,
                               audio_sender_to_json);
  }

  web::json::value make_sender_ancillary_observed_changed_message(
      const nmos_node::AncillarySender &sender)
  {
    return make_sender_message("sender.ancillary.observed_changed", sender,
                               ancillary_sender_to_json);
  }

  web::json::value make_receiver_video_observed_changed_message(
      const nmos_node::VideoReceiver &receiver)
  {
    return make_receiver_changed_message("receiver.video.observed_changed",
                                         receiver, video_receiver_to_json);
  }

  web::json::value make_receiver_audio_observed_changed_message(
      const nmos_node::AudioReceiver &receiver)
  {
    return make_receiver_changed_message("receiver.audio.observed_changed",
                                         receiver, audio_receiver_to_json);
  }

  web::json::value make_receiver_ancillary_observed_changed_message(
      const nmos_node::AncillaryReceiver &receiver)
  {
    return make_receiver_changed_message("receiver.ancillary.observed_changed",
                                         receiver, ancillary_receiver_to_json);
  }

  web::json::value make_receiver_video_observed_validation_message(
      const nmos_node::VideoReceiver &receiver,
      const std::string &request_id,
      const std::string &receiver_id)
  {
    return make_receiver_validation_message(
        "receiver.video.observed_validation", receiver, video_receiver_to_json,
        request_id, receiver_id);
  }

  web::json::value make_receiver_audio_observed_validation_message(
      const nmos_node::AudioReceiver &receiver,
      const std::string &request_id,
      const std::string &receiver_id)
  {
    return make_receiver_validation_message(
        "receiver.audio.observed_validation", receiver, audio_receiver_to_json,
        request_id, receiver_id);
  }

  web::json::value make_receiver_ancillary_observed_validation_message(
      const nmos_node::AncillaryReceiver &receiver,
      const std::string &request_id,
      const std::string &receiver_id)
  {
    return make_receiver_validation_message(
        "receiver.ancillary.observed_validation", receiver,
        ancillary_receiver_to_json, request_id, receiver_id);
  }

  bool equivalent(const nmos_node::Redundancy &lhs,
                 const nmos_node::Redundancy &rhs)
  {
    return lhs.present == rhs.present && lhs.enable == rhs.enable &&
           lhs.source_ip == rhs.source_ip && lhs.ip == rhs.ip &&
           lhs.port == rhs.port;
  }

  bool equivalent(const nmos_node::VideoSender &lhs,
                  const nmos_node::VideoSender &rhs)
  {
    return  lhs.id == rhs.id && lhs.name == rhs.name &&
           lhs.enable == rhs.enable  && lhs.video_format == rhs.video_format &&
           lhs.colorspace == rhs.colorspace &&
           lhs.transfer_characteristics == rhs.transfer_characteristics &&
           lhs.source_ip == rhs.source_ip &&
           lhs.ip == rhs.ip && lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy) &&
           lhs.pg_format == rhs.pg_format;
  }

  bool equivalent(const nmos_node::AudioSender &lhs,
                  const nmos_node::AudioSender &rhs)
  {
    return lhs.id == rhs.id &&
           lhs.name == rhs.name && lhs.source_ip == rhs.source_ip &&
           lhs.enable == rhs.enable && lhs.channel_count == rhs.channel_count &&
           lhs.bit_depth == rhs.bit_depth && lhs.sample_rate == rhs.sample_rate &&
           lhs.ip == rhs.ip && lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy);
  }

  bool equivalent(const nmos_node::AncillarySender &lhs,
                  const nmos_node::AncillarySender &rhs)
  {
    return lhs.id == rhs.id &&
           lhs.name == rhs.name && lhs.source_ip == rhs.source_ip &&
           lhs.format == rhs.format && lhs.enable == rhs.enable &&
           lhs.ip == rhs.ip && lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy);
  }

  bool equivalent(const nmos_node::VideoReceiver &lhs,
                  const nmos_node::VideoReceiver &rhs)
  {
    return  lhs.id == rhs.id && lhs.name == rhs.name &&
           lhs.enable == rhs.enable &&
           lhs.source_ip == rhs.source_ip && lhs.ip == rhs.ip &&
           lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy) &&
           lhs.format == rhs.format &&
           lhs.colorspace == rhs.colorspace &&
           lhs.transfer_characteristics == rhs.transfer_characteristics &&
           lhs.caps.formats == rhs.caps.formats &&
           lhs.caps.colorspaces == rhs.caps.colorspaces &&
           lhs.caps.transfer_characteristics ==
               rhs.caps.transfer_characteristics;
  }

  bool equivalent(const nmos_node::AudioReceiver &lhs,
                  const nmos_node::AudioReceiver &rhs)
  {
    return lhs.id == rhs.id &&
           lhs.name == rhs.name && lhs.enable == rhs.enable &&
           lhs.channel_count == rhs.channel_count &&
           lhs.bit_depth == rhs.bit_depth && lhs.sample_rate == rhs.sample_rate &&
           lhs.packet_time == rhs.packet_time && lhs.ip == rhs.ip &&
           lhs.source_ip == rhs.source_ip && lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy);
  }

  bool equivalent(const nmos_node::AncillaryReceiver &lhs,
                  const nmos_node::AncillaryReceiver &rhs)
  {
    return  lhs.id == rhs.id &&
           lhs.name == rhs.name && lhs.format == rhs.format &&
           lhs.enable == rhs.enable && lhs.ip == rhs.ip &&
           lhs.source_ip == rhs.source_ip && lhs.port == rhs.port &&
           equivalent(lhs.redundancy, rhs.redundancy);
  }

  bool equivalent(const PtpClockDto &lhs, const PtpClockDto &rhs)
  {
    return lhs.entries == rhs.entries;
  }

  bool equivalent(const SnapshotDto::DeviceDto &lhs,
                  const SnapshotDto::DeviceDto &rhs)
  {
    return lhs.device == rhs.device &&
           lhs.display_name == rhs.display_name &&
           lhs.enable == rhs.enable && lhs.id == rhs.id &&
           lhs.sip == rhs.sip;
  }
}
