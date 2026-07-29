# 架构分析：daemon 层与 node 层接口重复问题

> 分析工具: `codegraph explore`（3 次查询覆盖全部 daemon ↔ node 调用路径）

## 1. 核心问题：NodeRuntime 是 Node 的纯转发层

**位置**: `src/daemon/node_runtime.h` / `node_runtime.cpp`

`NodeRuntime`（214 行）中约 180 行是纯转发，直接调用 `Node` 的同名或近名方法：

### 1.1 纯转发方法（26 个，每行一个 `node_.xxx()` 调用）

| NodeRuntime 方法 | 转发到 Node | 行数 |
|---|---|---|
| `apply_video_sender()` | `add_video_sender()` | 3 |
| `apply_audio_sender()` | `add_audio_sender()` | 3 |
| `apply_ancillary_sender()` | `add_ancillary_sender()` | 3 |
| `apply_video_receiver()` | `add_video_receiver()` | 3 |
| `apply_audio_receiver()` | `add_audio_receiver()` | 3 |
| `apply_ancillary_receiver()` | `add_ancillary_receiver()` | 3 |
| `update_video_sender()` | `update_video_sender()` | 3 |
| `update_audio_sender()` | `update_audio_sender()` | 3 |
| `update_ancillary_sender()` | `update_ancillary_sender()` | 3 |
| `update_video_receiver()` | `update_video_receiver()` | 3 |
| `update_audio_receiver()` | `update_audio_receiver()` | 3 |
| `update_ancillary_receiver()` | `update_ancillary_receiver()` | 3 |
| `remove_video_sender()` | `remove_video_sender()` | 3 |
| `remove_audio_sender()` | `remove_audio_sender()` | 3 |
| `remove_ancillary_sender()` | `remove_ancillary_sender()` | 3 |
| `remove_video_receiver()` | `remove_video_receiver()` | 3 |
| `remove_audio_receiver()` | `remove_audio_receiver()` | 3 |
| `remove_ancillary_receiver()` | `remove_ancillary_receiver()` | 3 |
| `effective_settings()` | `effective_settings()` | 3 |
| `persisted_settings()` | `persisted_settings()` | 3 |
| `discover_registration_apis()` | `discover_registration_apis()` | 3 |
| `write_persisted_settings()` | `write_persisted_settings()` | 3 |
| `start()` | `start()` | 1 |
| `stop()` | `stop()` | 1 |

**小计**: 26 个方法，~70 行纯转发代码。

### 1.2 有少量实际逻辑的方法（3 个）

| 方法 | 逻辑 | 行数 |
|---|---|---|
| `set_ptp_clock()` | 从 `PtpClockDto` 提取 effective_gmid/locked/domain → `node_.set_ptp_clock()` | 4 |
| `set_runtime_devices()` | `DeviceDto` → `host_interface` 转换 → `node_.set_runtime_interfaces()` | 8 |
| 构造函数 | 18 行 per-type callback → variant event 包装 | 20 |

**小计**: 3 个方法，~32 行实际逻辑。

### 1.3 Event callback 机制（仅有的独立功能）

`NodeRuntime` 唯一独立于 `Node` 的功能是 variant-based event callback：

```cpp
// NodeRuntime 构造函数：把 7 个 per-type callback 包装成 3 个 variant event
node_.set_update_video_sender_callback([&](auto& s) { publish_event(SenderEvent{s}); });
node_.set_update_audio_sender_callback(  [&](auto& s) { publish_event(SenderEvent{s}); });
// ... 共 18 行
```

但这个包装过程本身也是冗余的——`Node` 完全可以直接支持 variant callback。

---

## 2. RegistrySnapshot 与 RegistrationStatus 重复定义

**位置**: `src/daemon/state_store.h:22` vs `src/node_types.h:112`

两个结构体字段完全一致：

```cpp
// node_types.h
struct RegistrationStatus {
    bool connected = false;
    std::string uri;
    std::string scheme;
    std::string host;
    int port = 0;
    std::string version;
};

// state_store.h — 完全相同的字段
struct RegistrySnapshot {
    bool connected{false};
    std::string uri;
    std::string scheme;
    std::string host;
    int port{0};
    std::string version;
};
```

`StateStore::set_registry_status()` 做了逐字段拷贝（6 行赋值）：

```cpp
void StateStore::set_registry_status(const nmos_node::RegistrationStatus &status) {
    registry_status_.connected = status.connected;
    registry_status_.uri = status.uri;
    // ... 共 6 行
}
```

**影响**: 消除重复后，`RegistrySnapshot` 可以删除，`StateStore` 直接使用 `nmos_node::RegistrationStatus`。

---

## 3. ReceiverEvent / SenderEvent / RegistrationEvent 包装层

**位置**: `src/daemon/node_runtime.h:15-29`

这三个结构体只是把 `nmos_node::*` 类型包装在 `std::variant` 里：

```cpp
struct ReceiverEvent { using Payload = std::variant<VideoReceiver, AudioReceiver, AncillaryReceiver>; Payload payload; };
struct SenderEvent   { using Payload = std::variant<VideoSender,   AudioSender,   AncillarySender>;   Payload payload; };
struct RegistrationEvent { nmos_node::RegistrationStatus status; };
```

**数据流**:
```
Node per-type callback → NodeRuntime 构造函数包装 → variant event → App::handle_*_event → std::visit 拆开 → per-type 处理
```

中间两步（包装 + 拆开）是纯开销。如果 `Node` 直接暴露 variant callback，整个链路可以简化为：

```
Node variant callback → App::handle_*_event → std::visit → per-type 处理
```

---

## 4. ReconcileEngine 依赖 NodeRuntime

**位置**: `src/daemon/reconcile_engine.h:14-16`

```cpp
void apply_snapshot(const std::optional<SnapshotDto> &current_snapshot,
                    const SnapshotDto &new_snapshot, NodeRuntime &runtime);
void drain_all(const std::optional<SnapshotDto> &current_snapshot,
               NodeRuntime &runtime);
```

调用 `runtime.apply_*`、`runtime.update_*`、`runtime.remove_*`、`runtime.set_ptp_clock`、`runtime.set_runtime_devices`。

合并后改为 `Node&`，调用 `node.add_*`、`node.update_*`、`node.remove_*` 等。

---

## 5. App 直接依赖 NodeRuntime

**位置**: `src/daemon/app.h:55` / `app.cpp`

```cpp
std::unique_ptr<NodeRuntime> node_runtime_;
```

调用方式：
- `node_runtime_->start()` / `stop()`
- `node_runtime_->set_*_event_handler()`
- `node_runtime_->persisted_settings()` / `effective_settings()` / `write_persisted_settings()` / `discover_registration_apis()`

合并后改为 `std::unique_ptr<nmos_node::Node> node_`。

---

## 6. 合并方案（方向 B）

### 步骤

| 步骤 | 变更 | 影响文件 |
|------|------|----------|
| 0 | 将 `ReceiverEvent`/`SenderEvent`/`RegistrationEvent` 迁入 `node_callbacks.h` | `node_callbacks.h` |
| 1 | `Node` 增加 `set_receiver_event_handler`/`set_sender_event_handler`/`set_registration_event_handler` | `node.h`, `node.cpp` |
| 2 | `Node` 增加 `set_runtime_devices(std::vector<DeviceDto>)` | `node.h`, `node.cpp` |
| 3 | `StateStore` 删除 `RegistrySnapshot`，改用 `nmos_node::RegistrationStatus` | `state_store.h`, `state_store.cpp` |
| 4 | 删除 `NodeRuntime` 类 | `node_runtime.h`, `node_runtime.cpp` |
| 5 | `app.cpp` / `app.h` 改用 `Node` 替代 `NodeRuntime` | `app.h`, `app.cpp` |
| 6 | `reconcile_engine.h` / `.cpp` 改用 `Node&` 替代 `NodeRuntime&` | `reconcile_engine.h`, `reconcile_engine.cpp` |
| 7 | `device_to_interface` 迁入 `Node` 或 utility | `app.cpp` 或 `node.cpp` |

### 预期收益

| 指标 | 变化 |
|------|------|
| 删除 `node_runtime.h` | -85 行 |
| 删除 `node_runtime.cpp` | -214 行 |
| `Node` 新增方法 | +~40 行 |
| `app.cpp` 简化 | -10 行 |
| 删除 `RegistrySnapshot` | -7 行 |
| 删除 `StateStore::set_registry_status` 逐字段拷贝 | -6 行 |
| **净减少代码** | **~280 行** |

### 不变量

- `Node` 的 PImpl 边界保持不变
- daemon 的 callback 语义不变（仍然是 variant-based event）
- `ReconcileEngine` 的 reconcile 逻辑不变
- `dto.h` 的 `SnapshotDto`、`equivalent` 不变；`make_*_observed_changed_message` 默认兼容旧调用，receiver 消息允许携带用于连接确认的可选 `request_id`
- receiver 连接确认发生在 nmos-cpp `on_validate_connection_resource_patch` 阶段：HTTP IS-05 receiver immediate activation 在 active 变更前发送带随机 `receiver-validation-*` request_id 和顶层 `receiver_id` 的 observed_changed，外部失败或超时会作为 HTTP 400 debug 原因返回；post-activation observed notification 仅 fire-and-forget
