# OpenCode 任务拆解文档：nmos-sync-daemon V1

## 0. 任务目标

在当前仓库中新增一个可执行程序 `nmos-sync-daemon`，实现以下能力：

1. 持有并管理一个 `seeder::nmos_node::Node` 实例
2. Node 启动成功后，连接外部工程 WebSocket，并等待 `snapshot.changed` 消息
3. 只有在 wsClient 收到 `snapshot.changed` 后，才从外部工程 REST API 拉取全量 snapshot
4. 对 snapshot 做 reconcile，并同步 sender / receiver / ptp 到本地 Node
5. 外部工程在 WebSocket 刚建立连接时，必须主动发送一条 `snapshot.changed` 作为首次同步触发信号
6. 收到后续 `snapshot.changed` 时重新全量拉取 snapshot
7. 将 receiver callback 产生的状态变化通过 WebSocket 回推给外部工程

## 1. V1 边界

### 必须实现
- 单实例 Node
- 单配置源
- 全量 snapshot 拉取（且只能由 `snapshot.changed` 触发）
- WebSocket 通知触发重拉
- 差异 reconcile
- receiver 状态变化回推
- 与外部工程失去 WebSocket 连接时，删除当前已注册到注册中心的全部 sender / receiver 流
- 基础状态/健康接口（建议）

### 明确不实现
- auto bind 对外协议
- sender/receiver binding 事件
- subscription 管理
- 多 Node 实例
- 本地持久化恢复
- 面向业务的写 REST API
- 增量 patch 同步

## 2. 建议新增目录结构

建议新增如下目录和文件：

```text
src/daemon/
  main.cpp
  app.h
  app.cpp
  config.h
  config.cpp
  dto.h
  dto.cpp
  node_runtime.h
  node_runtime.cpp
  snapshot_client.h
  snapshot_client.cpp
  ws_client.h
  ws_client.cpp
  reconcile_engine.h
  reconcile_engine.cpp
  state_store.h
  state_store.cpp
```

如有必要，可新增：

```text
src/daemon/http_debug_server.h
src/daemon/http_debug_server.cpp
```

## 3. 阶段拆解总览

### Phase 1：构建与程序骨架
### Phase 2：配置与 DTO 定义
### Phase 3：Node Runtime 封装
### Phase 4：Snapshot 拉取
### Phase 5：State Store
### Phase 6：Reconcile 引擎
### Phase 7：WebSocket 客户端
### Phase 8：receiver callback -> WS 事件转发
### Phase 9：本地调试接口（可选但建议）
### Phase 10：端到端验证与清理

## 4. Phase 1：构建与程序骨架

### 运行期关键规则（必须遵守）

1. 外部工程是唯一真相源
2. daemon 只有在满足以下条件时才允许保留已注册流：
   - Node 已启动成功
   - 与外部工程 WebSocket 已连接
   - 已收到外部工程发送的 `snapshot.changed`
   - 已根据该消息成功拉取并应用至少一版 snapshot
3. 一旦与外部工程 WebSocket 断开连接，daemon 必须进入保护模式：
   - 立即删除当前已注册到注册中心的全部 sender / receiver 流
   - 保留 daemon 进程和 Node 运行能力
   - 等待 WebSocket 恢复
   - WebSocket 恢复后等待外部工程再次发送 `snapshot.changed`
   - 收到该消息后再拉取 snapshot，并重新注册
4. 本规则优先级高于“保持当前状态等待重连”
5. daemon 不允许在“仅建立 WS 连接但尚未收到 `snapshot.changed`”的情况下主动拉取 snapshot

### Task 1.1：新增 daemon 可执行目标
**目标**
- 在 `src/CMakeLists.txt` 中新增 `nmos-sync-daemon` 可执行目标
- 可执行目标链接已有 `nmos-client` 库

**涉及文件**
- `src/CMakeLists.txt`

**要求**
- 不破坏现有 `nmos-client` 和 `nmos-client-demo`
- 新目标可独立编译

**完成定义**
- `cmake --workflow --preset debug` 能生成 `nmos-sync-daemon`

### Task 1.2：新增 daemon 入口文件
**目标**
- 创建 `src/daemon/main.cpp`
- 实现最小 main 函数
- 可启动并退出

**涉及文件**
- `src/daemon/main.cpp`

**要求**
- main 只负责：
  - 解析启动参数（后续可扩展）
  - 初始化 `App`
  - 调用 `run()`

**完成定义**
- 程序可执行
- 输出基本启动日志

### Task 1.3：新增 `App` 骨架
**目标**
- 创建 `App` 类，作为 daemon 顶层协调器

**涉及文件**
- `src/daemon/app.h`
- `src/daemon/app.cpp`

**职责**
- 加载配置
- 初始化各模块
- 控制启动顺序
- 处理主生命周期

**完成定义**
- `main.cpp` 可调用 `App::run()`

## 5. Phase 2：配置与 DTO 定义

### Task 2.1：实现 daemon 配置结构
**目标**
定义并加载如下配置：

```json
{
  "node_config_path": "/etc/nmos-client/node_config.json",
  "snapshot_url": "http://127.0.0.1:8080/api/nmos/snapshot",
  "ws_url": "ws://127.0.0.1:8080/api/nmos/ws",
  "pull_timeout_ms": 3000,
  "reconnect_interval_ms": 1000,
  "snapshot_debounce_ms": 300
}
```

**涉及文件**
- `src/daemon/config.h`
- `src/daemon/config.cpp`

**要求**
- 支持从 JSON 文件读取
- 缺失字段时报明确错误
- 不要把业务配置和 node_config 混为一体

**完成定义**
- `App` 能成功加载 daemon 配置

### Task 2.2：定义 snapshot DTO
**目标**
定义外部工程 snapshot 对应的 C++ 结构。

**涉及文件**
- `src/daemon/dto.h`
- `src/daemon/dto.cpp`

**需要定义**
- `SnapshotDto`
- `PtpClockDto`
- `VideoSenderDto`
- `AudioSenderDto`
- `AncillarySenderDto`
- `VideoReceiverDto`
- `AudioReceiverDto`
- `AncillaryReceiverDto`
- `RedudancyDto`

**要求**
- 字段尽量与 `node.h` 对齐
- 保留 `redudancy` 拼写，不要改名
- snapshot DTO 本体不需要 `revision` 字段
- `revision` 只存在于 WebSocket 的 `snapshot.changed` 消息中，用于事件去重、debounce 和状态跟踪

**完成定义**
- 可从 JSON 解析出完整 snapshot DTO

### Task 2.3：定义 WebSocket 消息 DTO
**目标**
定义 WebSocket 收发消息结构。

**涉及文件**
- `src/daemon/dto.h`
- `src/daemon/dto.cpp`

**至少包含**
- 入站消息：
  - `SnapshotChangedMessage`
  - `PingMessage`
- 出站消息：
  - `NodeLifecycleChangedMessage`
  - `ReceiverVideoObservedChangedMessage`
  - `ReceiverAudioObservedChangedMessage`
  - `ReceiverAncillaryObservedChangedMessage`
  - `SyncFailedMessage`

**完成定义**
- DTO 可序列化/反序列化 JSON

## 6. Phase 3：Node Runtime 封装

### Task 3.1：实现 `NodeRuntime`
**目标**
封装 `seeder::nmos_node::Node`，对外提供统一调用接口。

**涉及文件**
- `src/daemon/node_runtime.h`
- `src/daemon/node_runtime.cpp`

**对外接口建议**
- `start()`
- `stop()`
- `set_ptp_clock(...)`
- `apply_video_sender(...)`
- `apply_audio_sender(...)`
- `apply_ancillary_sender(...)`
- `apply_video_receiver(...)`
- `apply_audio_receiver(...)`
- `apply_ancillary_receiver(...)`
- `remove_video_sender(...)`
- `remove_audio_sender(...)`
- `remove_ancillary_sender(...)`
- `remove_video_receiver(...)`
- `remove_audio_receiver(...)`
- `remove_ancillary_receiver(...)`

**要求**
- 不把上层 DTO 直接塞进业务层，做清晰转换
- 封装 Node 的启动/停止错误

**完成定义**
- 可通过 `NodeRuntime` 成功调用现有 `Node` API

### Task 3.2：接管 receiver callback
**目标**
在 `NodeRuntime` 中注册：
- `set_update_video_receiver_callback`
- `set_update_audio_receiver_callback`
- `set_update_ancillary_receiver_callback`

**要求**
- 回调结果转成内部事件对象
- 不在 `NodeRuntime` 中直接耦合 WebSocket 实现
- 使用回调函数 / observer / event sink 方式向上层报告

**完成定义**
- 上层可以订阅 receiver observed update 事件

## 7. Phase 4：Snapshot 拉取

### Task 4.1：实现 `SnapshotClient`
**目标**
通过 HTTP GET 获取 `/api/nmos/snapshot`

**涉及文件**
- `src/daemon/snapshot_client.h`
- `src/daemon/snapshot_client.cpp`

**要求**
- 支持超时
- HTTP 非 200 时返回明确错误
- JSON 格式错误时返回明确错误
- 返回 `SnapshotDto`

**完成定义**
- 能从配置中的 `snapshot_url` 拉到并解析出 snapshot

### Task 4.2：首次启动拉取逻辑
**目标**
Node 启动完成后，不主动拉取 snapshot，而是等待 wsClient 收到外部工程发送的首条 `snapshot.changed`，再触发首次 snapshot 拉取

**依赖**
- `Config`
- `NodeRuntime`
- `SnapshotClient`
- `WsClient`

**要求**
- 启动后不能绕过 WebSocket 直接拉取 snapshot
- 必须由 wsClient 收到的 `snapshot.changed` 触发拉取
- 外部工程负责在连接建立后主动发送首条 `snapshot.changed`
- 拉取失败时打日志
- 首次同步若迟迟未收到 `snapshot.changed`，daemon 保持已连接但未同步状态，不注册任何流

**完成定义**
- 程序启动后会等待首条 `snapshot.changed`，并在收到后完成首次 snapshot 拉取

## 8. Phase 5：State Store

### Task 5.1：实现 `StateStore`
**目标**
在内存中保存当前同步状态。

**涉及文件**
- `src/daemon/state_store.h`
- `src/daemon/state_store.cpp`

**至少保存**
- `last_seen_revision`
- `last_applied_revision`
- `last_sync_error`
- `last_snapshot`
- 当前已应用资源索引（按 type + id）
- 当前是否存在“已注册流”
- 当前是否处于“保护模式（WS 断链后已清流）”

**要求**
- 线程安全
- 不做持久化

**完成定义**
- 其他模块可以查询和更新当前同步状态

### Task 5.2：建立资源索引
**目标**
为 reconcile 提供快速查找。

**要求**
- 按以下维度建 key：
  - `video_sender:{id}`
  - `audio_sender:{id}`
  - `ancillary_sender:{id}`
  - `video_receiver:{id}`
  - `audio_receiver:{id}`
  - `ancillary_receiver:{id}`

**完成定义**
- Reconcile 时可直接对比 old/new map

## 9. Phase 6：Reconcile 引擎

### Task 6.1：实现 `ReconcileEngine`
**目标**
比较旧 snapshot 与新 snapshot，生成操作计划。

**涉及文件**
- `src/daemon/reconcile_engine.h`
- `src/daemon/reconcile_engine.cpp`

**操作类型**
- add
- remove
- update（same-id 内容变化时执行显式 update 或原地资源变更）
- ptp_update
- remove_all_streams

**要求**
- 不允许每次全删全建
- 必须按 `type + id` 做差异比较
- 对象内容完全相同则 no-op
- 但当进入“WS 断链保护模式”时，必须无条件执行 `remove_all_streams`

**完成定义**
- 输入 old/new snapshot，可得到明确的操作列表

### Task 6.2：实现 sender reconcile
**目标**
对：
- video senders
- audio senders
- ancillary senders

分别进行：
- add
- remove
- update

**要求**
- 缺失 ID 执行 remove，新 ID 执行 add，same-id 内容变化执行 update，完全等价则 no-op
- 失败时记录具体资源 id 和类型

**完成定义**
- sender 差异同步可正常执行

### Task 6.3：实现 receiver reconcile
**目标**
对：
- video receivers
- audio receivers
- ancillary receivers

分别进行：
- add
- remove
- update

**要求**
- 缺失 ID 执行 remove，新 ID 执行 add，same-id 内容变化执行 update，完全等价则 no-op
- 注意 receiver observed callback 可能在 apply 后触发

**完成定义**
- receiver 差异同步可正常执行

### Task 6.4：实现 ptp reconcile
**目标**
比较旧 `ptp_clock` 和新 `ptp_clock`

**要求**
- 变化时调用 `NodeRuntime::set_ptp_clock`
- 不变化时 no-op

**完成定义**
- ptp 可随 snapshot 同步更新

### Task 6.5：应用结果写回 `StateStore`
**目标**
当 reconcile 成功完成后：
- 更新 `last_applied_revision`
- 更新 `last_snapshot`
- 清空 `last_sync_error`
- 标记当前已注册流存在
- 若此前处于保护模式，则退出保护模式

若失败：
- 保留旧 `last_applied_revision`
- 更新 `last_sync_error`

**完成定义**
- 状态与应用结果一致

## 10. Phase 7：WebSocket 客户端

### Task 7.1：实现 `WsClient`
**目标**
实现到外部工程的 WebSocket 客户端连接。

**涉及文件**
- `src/daemon/ws_client.h`
- `src/daemon/ws_client.cpp`

**要求**
- 连接 `ws_url`
- 自动重连
- 支持接收文本消息
- 支持发送文本消息

**完成定义**
- 可建立连接并收发 JSON 消息

### Task 7.2：处理 `snapshot.changed`
**目标**
收到：

```json
{
  "type": "snapshot.changed",
  "revision": 44,
  "reason": "..."
}
```

后，触发后续重拉流程。

**要求**
- 这是唯一允许触发 snapshot 拉取的入口
- 若 `revision <= last_applied_revision`，忽略
- 若更大，进入 debounce 阶段
- 若是建连后的第一条 `snapshot.changed`，则执行首次拉取与首次 reconcile

**完成定义**
- daemon 可正确识别新 revision

### Task 7.3：实现 debounce 逻辑
**目标**
避免短时间内重复全量拉取。

**要求**
- 默认 300ms
- 始终只拉取最新 revision
- debounce 期间多次通知只保留最新目标 revision

**完成定义**
- 高频 `snapshot.changed` 下只触发有限次全量拉取

### Task 7.4：WS 断线重连
**目标**
WS 断开后自动重连。

**要求**
- 使用 `reconnect_interval_ms`
- 在检测到 WS 断开后，必须先触发“清空当前已注册流”动作
- 清空动作范围包括当前已注册到注册中心的全部 sender / receiver
- 清空完成后再进入重连循环
- 重连成功后不能直接认为系统恢复，必须等待外部工程重新发送 `snapshot.changed`
- 只有在“重连成功 + 收到 `snapshot.changed` + snapshot 拉取成功 + reconcile 成功”后，才恢复 running
- 更新 daemon 状态为 degraded / draining / resyncing / running（命名可调整，但语义必须覆盖）

**完成定义**
- 手动断开 WS 后：
  - daemon 会删除当前已注册流
  - daemon 会自动重连
  - daemon 会在重连后等待新的 `snapshot.changed`
  - daemon 会在收到新的 `snapshot.changed` 后重新拉取 snapshot 并恢复注册

## 11. Phase 8：receiver callback -> WS 事件转发

### Task 8.1：video receiver 事件转发
**目标**
把 `VideoReceiver` callback 转成：

```json
{
  "type": "receiver.video.observed_changed",
  "payload": { ... }
}
```

**要求**
- 发送完整快照
- 包含：
  - id
  - receiver_id
  - enable
  - ip
  - port
  - source_ip
  - redudancy

**完成定义**
- video receiver 状态变化能成功回推到外部工程

### Task 8.2：audio receiver 事件转发
同 Task 8.1，事件类型为：
- `receiver.audio.observed_changed`

### Task 8.3：ancillary receiver 事件转发
同 Task 8.1，事件类型为：
- `receiver.ancillary.observed_changed`

### Task 8.4：同步失败事件转发
**目标**
当 snapshot 拉取失败 / reconcile 失败时，发送：

```json
{
  "type": "sync.failed",
  "revision": 44,
  "message": "..."
}
```

**完成定义**
- 外部工程能感知同步失败

### Task 8.6：WS 断链清流事件转发
**目标**
当与外部工程 WebSocket 断开连接并触发保护模式时，发送一条明确事件，说明 daemon 已开始或已完成清空当前已注册流。

**建议事件**

```json
{
  "type": "streams.drained_due_to_ws_disconnect",
  "message": "WebSocket disconnected, all registered senders/receivers removed"
}
```

**要求**
- 若断链瞬间无法发出该消息，可在重连成功后补发一条状态事件说明此前发生过 drain
- 至少需要在本地日志和状态接口中可见该行为

**完成定义**
- WS 断链触发清流时，有明确的外部可观测信号

### Task 8.5：node 生命周期事件转发
**目标**
在关键状态变化时发送：

```json
{
  "type": "node.lifecycle_changed",
  "payload": {
    "old_state": "...",
    "new_state": "..."
  }
}
```

**至少覆盖**
- starting -> running
- running -> stopping
- stopping -> stopped

**完成定义**
- 外部工程可感知 daemon/node 生命周期状态

## 12. Phase 9：本地调试接口（建议实现）

### Task 9.1：`GET /health`
**目标**
返回基础健康状态。

**响应示例**
```json
{
  "ok": true
}
```

### Task 9.2：`GET /status`
**目标**
返回当前 daemon / node / sync 状态。

**响应示例**
```json
{
  "daemon_state": "running",
  "node_state": "running",
  "last_seen_revision": 44,
  "last_applied_revision": 44,
  "last_sync_error": null,
  "streams_registered": true,
  "protection_mode": false
}
```

### Task 9.3：`GET /debug/snapshot`
**目标**
返回当前内存中保存的 `last_snapshot`

## 13. Phase 10：验证与收尾

### Task 10.1：启动链验证
**验收**
- daemon 启动
- Node 启动
- daemon 建立 WebSocket 连接
- 外部工程在建连后发送首条 `snapshot.changed`
- daemon 在收到该消息后首次拉取 snapshot
- reconcile 成功
- 进入 running 状态

### Task 10.2：WS 变更通知验证
**验收**
- 外部工程发送 `snapshot.changed`
- daemon debounce 后重新拉取
- reconcile 生效

### Task 10.3：receiver 回推验证
**验收**
- receiver callback 触发
- daemon 成功发送 `receiver.*.observed_changed`

### Task 10.4：异常链路验证
**验收**
- snapshot REST 不可用时，产生 `sync.failed`
- WS 断线时，daemon 自动删除当前已注册流，并进入重连
- JSON 非法时，记录明确错误

### Task 10.6：WS 断链清流验证
**验收**
- 在 sender / receiver 已成功注册后，主动断开外部工程 WebSocket
- daemon 检测到断链
- daemon 删除当前已注册到注册中心的全部 sender / receiver
- 本地状态接口中 `streams_registered=false`
- 重连成功后外部工程重新发送 `snapshot.changed`
- daemon 收到消息后重新拉取 snapshot，流重新注册

### Task 10.5：构建与静态验证
**验收**
- `cmake --workflow --preset debug` 通过
- 所有新增文件无明显编译错误
- 不破坏现有库和 demo 编译

## 14. 推荐任务依赖顺序

推荐按以下依赖执行：

1. Phase 1：构建与程序骨架
2. Phase 2：配置与 DTO
3. Phase 3：Node Runtime
4. Phase 4：Snapshot 拉取
5. Phase 5：State Store
6. Phase 6：Reconcile 引擎
7. Phase 7：WebSocket 客户端
8. Phase 8：receiver 事件转发
9. Phase 9：调试接口
10. Phase 10：联调验证

## 15. 执行时必须遵守的实现规则

1. 不修改现有 `Node` 的公开语义
2. 不在 V1 中实现 auto bind 协议
3. 不暴露业务写 REST API
4. 所有业务配置变更必须通过：
   - 外部工程修改数据
   - WS 通知 daemon
   - daemon 重新全量拉取 snapshot
5. snapshot 拉取的唯一触发源是 wsClient 收到的 `snapshot.changed`
6. 外部工程必须在 WS 建连成功后主动发送一条 `snapshot.changed` 作为首次同步触发
7. `redudancy` 字段拼写保持现状
8. reconcile 必须做差异同步，不允许粗暴全删全建
9. receiver callback 与 WS 转发解耦，不要在 NodeRuntime 里直接写网络逻辑
10. 一旦与外部工程 WS 断链，必须删除当前已注册流，不能继续保留旧注册状态等待重连

## 16. 最终完成标准（Definition of Done）

当以下条件全部满足时，V1 视为完成：

1. `nmos-sync-daemon` 可编译、可启动
2. 能启动 `Node`
3. 只有在收到 wsClient 的 `snapshot.changed` 后才会拉取 snapshot
4. 能在建连后收到首条 `snapshot.changed` 并完成首次同步
5. 能对 sender / receiver / ptp 做差异同步
6. 能在收到后续 `snapshot.changed` 后重新全量拉取
7. 能把 receiver observed 变化回推到外部工程
8. WS 断线时会删除当前已注册流
9. WS 恢复后会等待新的 `snapshot.changed`，并在收到后恢复注册
10. 有基本健康与状态查询能力
11. 不包含 auto bind 对外协议
12. 构建通过，代码结构清晰，模块分层明确
