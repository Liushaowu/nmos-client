# Sisyphus C++ 重构执行计划

本文档供未来 Sisyphus 执行 NMOS client C++ 重构使用。目标是把 `src/node.cpp` 中过于集中的内部逻辑拆开，让代码更短、更清晰、更容易验证，同时保持现有行为和公共接口稳定。

## 目标

* 第一轮重构保持 `src/node.h` 公共 API 稳定，不做 public API 优先 redesign。
* 保留 `Node` 外观类和 PImpl 边界，内部实现可以拆分，外部调用方式不变。
* 降低 `Node::Impl` 的职责密度，把生命周期、配置、资源构造、缓存、IS 05 activation、auto resolver、SDP、PTP、callback 分离到更明确的内部模块。
* 在可行处减少重复代码和总代码量，同时保持显式可读。
* 对关键复杂逻辑添加简洁中文注释，说明意图、不变量和时序约束，而不是逐行解释代码。

## 非目标

* 不修改 `src/node.h` 的公共类型、方法签名和调用语义。
* 不更改 CMake 目标、安装规则、包管理规则或构建目录策略。
* 不引入新框架、插件系统、依赖注入框架、事件总线、数据库、UI 或媒体数据平面功能。
* 不把控制面逻辑改造成媒体处理引擎。
* 不建议把 `redudancy` 改名为 `redundancy`，该拼写需要继续兼容现有代码路径。
* 不把清晰的显式流程替换成难读的通用模板、宏技巧或过度泛型抽象。

## 硬约束

* 只在内部实现边界内重构。第一轮不得破坏 `src/node.h` 公共 API。
* 必须保留 PImpl 边界。`Node` 仍是外观入口，重逻辑仍藏在实现层。
* 发送端生命周期必须保持为 `source -> flow -> sender -> connection_sender`。
* 接收端生命周期必须保持为 `receiver -> connection_receiver`。
* 必须保持 add、remove、update 的资源对称性，不能出现只新增不删除、只更新一侧状态的路径。
* 必须保持 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义，不得绕过这些 helper 复制元数据逻辑。
* 必须保持 `redudancy` 拼写兼容。代码中如已有该字段，继续按现状处理。
* 不修改生成目录，不把 `build/`、`build_initdeep/`、`.cache/` 里的内容当作源码依据。
* 每次实施后必须运行验证命令。实现完成后必须使用 `review-work` skill 做后置审查。

## 当前重构热点

* `src/node.cpp` 中的 `Node::Impl` 是主热点，当前同时承载运行时生命周期、settings、资源构造、缓存、IS 05 activation、auto resolver、SDP、PTP 和 callbacks。
* sender 路径存在重复的 ID、label、flow、sender、connection resource 组装逻辑。
* receiver 路径存在与 sender 相似但不完全相同的资源注册、移除和连接处理逻辑。
* callback 注册、触发和状态访问需要更清楚的边界，避免业务流程和通知逻辑互相穿插。
* SDP、PTP、auto resolver 属于独立服务逻辑，适合从 `Node::Impl` 的主流程中抽出。

## 目标模块边界

* `NodeServerRuntime` 负责启动、停止、线程、NMOS server 生命周期和顶层协调。
* `NodeSettings` 负责读取、保存和提供运行配置，不承载资源创建逻辑。
* `StreamStore` 负责保存 sender、receiver、source、flow、connection resource 的内部映射和一致性检查。
* `SenderResourceFactory` 负责构造 sender 侧的 source、flow、sender、connection_sender。
* `ReceiverResourceFactory` 负责构造 receiver 和 connection_receiver。
* `ResourceLifecycleService` 负责 add、remove、update 的对称编排，统一资源插入、移除和回滚边界。
* `ConnectionHandlers` 负责 IS 05 activation、staged、active 连接处理和传输参数回调。
* `SdpService` 负责 SDP 生成、解析或与 SDP 相关的内部辅助逻辑。
* `CallbackDispatcher` 负责用户 callback 的保存、调度和线程安全边界。

## 执行阶段

### 阶段 1，建立基线与保护线

* 目标：确认当前行为、构建状态和高风险路径，形成重构前基线。
* 允许变更：只允许新增面向重构的内部测试或最小验证脚本，前提是项目已有合适位置。若没有测试基础设施，本阶段不强行补测试框架。
* 禁止变更：不得修改公共 API，不得移动大段实现，不得调整 CMake，不得改动 `redudancy` 拼写。
* 验证：运行 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` 和 `cmake --build build`。如果 preset 可用，再运行 `cmake --workflow --preset debug`。
* 停止条件：构建基线通过，或发现与重构无关的既有失败并记录清楚，不把它混入重构提交。

### 阶段 2，抽出配置与运行时外壳

* 目标：把 settings 读取和 server 生命周期协调从 `Node::Impl` 主体中分离，形成 `NodeSettings` 和 `NodeServerRuntime`。
* 允许变更：移动内部私有逻辑，保持调用顺序和外部行为不变。可减少重复的 settings 访问代码。
* 禁止变更：不得改变 `Node::start`、`Node::stop`、构造函数或 public model 的语义。不得改变配置文件键名。
* 验证：构建通过，并用最小驱动确认 `Node` 可按原入口构造和启动路径编译。
* 停止条件：`Node::Impl` 不再直接混杂大量配置解析细节，启动链仍保持 `Node::start -> Node::Impl::start -> 内部 runtime` 的可追踪路径。

### 阶段 3，抽出 sender 与 receiver 资源工厂

* 目标：把资源构造逻辑拆进 `SenderResourceFactory` 和 `ReceiverResourceFactory`，减少重复构造代码。
* 允许变更：集中 ID、label、description、group hint 的调用位置，复用公共小函数，压缩重复分支。
* 禁止变更：不得改变 sender 生命周期 `source -> flow -> sender -> connection_sender`。不得改变 receiver 生命周期 `receiver -> connection_receiver`。不得绕过 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint`。
* 验证：构建通过。检查 sender 和 receiver 的新增路径中资源创建顺序、ID 生成规则和 label 语义与重构前一致。
* 停止条件：sender 和 receiver 的工厂职责清晰，重复构造代码减少，但调用处仍能直接看出资源生命周期。

### 阶段 4，抽出资源生命周期服务与存储

* 目标：把 add、remove、update 的对称编排放入 `ResourceLifecycleService`，把内部资源映射放入 `StreamStore`。
* 允许变更：合并重复的插入、删除、查找、状态更新流程。可用小型 helper 表达共同步骤。
* 禁止变更：不得制造只处理 sender 或只处理 receiver 的单边路径。不得用难读的泛型算法隐藏生命周期顺序。
* 验证：构建通过。逐项核对 add、remove、update 是否仍成对维护 source、flow、sender、receiver、connection resource 和缓存状态。
* 停止条件：任意资源类型的新增、删除、更新都能在一个明确服务中追踪，且失败路径不会留下半更新状态。

### 阶段 5，抽出连接处理、SDP 和 callback

* 目标：把 IS 05 activation、transport 参数、SDP 逻辑和用户 callback 调度从 `Node::Impl` 中分离。
* 允许变更：新增 `ConnectionHandlers`、`SdpService`、`CallbackDispatcher` 内部类或文件，保留原有线程安全假设。
* 禁止变更：不得改变 callback 触发时机和入参语义。不得新增事件总线或插件系统。不得引入外部依赖。
* 验证：构建通过。检查 activation 到 callback 的调用链，确认 staged、active 和 transport 参数处理顺序保持一致。
* 停止条件：`Node::Impl` 只保留协调职责，不直接堆叠连接细节、SDP 细节和 callback 分发细节。

### 阶段 6，减代码量与注释整理

* 目标：在行为稳定后删除重复实现，收紧局部 helper，并补充关键中文注释。
* 允许变更：删除重复代码、合并等价分支、缩小变量作用域、提取清晰小函数、补充简洁中文注释。
* 禁止变更：不得为了减少行数牺牲可读性。不得把显式的 NMOS 生命周期改成难以追踪的通用配置表。不得添加逐行字面注释。
* 验证：构建通过。人工阅读主要调用链，确认代码更短但执行顺序仍清楚。
* 停止条件：总代码量在可行处下降，复杂逻辑的意图和不变量有中文注释，且没有新增抽象层噪音。

### 阶段 7，后置审查与收尾

* 目标：确认重构没有引入行为回归，提交拆分清楚，审查发现已处理。
* 允许变更：只修复本轮重构引入的问题，补充缺失验证或注释。
* 禁止变更：不得借收尾阶段继续扩大架构范围。不得修复无关历史问题，除非它阻塞本次验证。
* 验证：再次运行完整验证命令，并使用 `review-work` skill 做实现后审查。
* 停止条件：构建和审查通过，所有保留风险已记录，提交边界清晰。

## 验证命令

优先使用以下命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug ; cmake --build build ; cmake --workflow --preset debug
```

如果 preset 在当前环境不可用，必须至少运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

验证要求：

* 每个阶段完成后都要运行与改动范围匹配的构建验证。
* 如果失败来自既有环境或既有代码，记录错误命令、退出码和关键日志，不在重构提交中混入无关修复。
* 如果失败由本轮改动引起，必须先修复，再进入下一阶段。

## 可读性与减代码量规则

* 严格减少重复代码和总代码量，在可实践范围内优先删掉复制粘贴逻辑。
* 不为了减少行数替换成难读的泛型抽象、宏、过度模板或隐式数据驱动流程。
* 提取函数时，函数名必须表达业务意图，而不是只描述语法动作。
* 保留关键生命周期的显式顺序，让 reviewer 能直接看出 sender 和 receiver 资源如何被创建、插入、更新和删除。
* 合并分支前先确认语义完全一致。只相似但不相同的路径不要硬合并。
* 新文件和新类数量要克制。模块边界用于降低复杂度，不用于堆叠层级。

## 中文注释规则

* 关键复杂代码必须有简洁中文注释，说明意图、不变量、生命周期顺序或线程安全假设。
* 注释写为什么这样做，以及必须保持什么约束，不写逐行字面解释。
* sender 和 receiver 生命周期、add/remove/update 对称性、activation 回调边界等高风险点，适合保留短注释。
* 简单赋值、显而易见的 getter、局部变量搬运不加注释。
* 注释必须随代码同步更新。若注释只能复述旧实现，应该删除或重写。

## 提交拆分建议

* 提交 1：建立基线验证和必要的内部保护线，不改行为。
* 提交 2：抽出 `NodeSettings` 和 `NodeServerRuntime`。
* 提交 3：抽出 `SenderResourceFactory` 和 `ReceiverResourceFactory`。
* 提交 4：抽出 `StreamStore` 和 `ResourceLifecycleService`。
* 提交 5：抽出 `ConnectionHandlers`、`SdpService`、`CallbackDispatcher`。
* 提交 6：减代码量、清理重复逻辑、补充关键中文注释。
* 每个提交都必须能独立构建。不要把无关格式化、CMake 调整或历史问题修复混入这些提交。

## Sisyphus 执行清单

* 开始前阅读 `AGENTS.md`、`src/AGENTS.md`、`src/node.h`、`src/node.cpp`、`src/node_implementation.h`、`src/node_implementation.cpp`。
* 明确当前阶段，只改该阶段允许的内部边界。
* 每次编辑前确认不会修改公共 API、CMake 或生成目录。
* 保留 PImpl 边界，保留 sender 和 receiver 生命周期顺序。
* 保留 add、remove、update 对称性。
* 保留 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义。
* 保留 `redudancy` 拼写兼容。
* 优先删除重复代码，但拒绝难读的通用抽象。
* 给关键复杂逻辑补简洁中文注释。
* 阶段结束后运行验证命令。
* 实现全部完成后，使用 `review-work` skill 做后置审查，并处理审查指出的本轮问题。

## 验收标准

* `src/node.h` 第一轮保持 public API 稳定，没有 public API 优先 redesign。
* PImpl 边界保留，`Node` 仍是外观入口。
* sender 生命周期仍为 `source -> flow -> sender -> connection_sender`。
* receiver 生命周期仍为 `receiver -> connection_receiver`。
* add、remove、update 路径保持资源和缓存状态对称。
* `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义保持不变。
* `redudancy` 拼写兼容保持不变，没有建议或实施重命名。
* `Node::Impl` 职责减少，内部模块边界能对应目标模块边界。
* 重复代码和总代码量在可行处减少，可读性没有下降。
* 关键复杂逻辑有简洁中文注释，注释解释意图和不变量，不做逐行字面说明。
* 验证命令通过，或存在已记录的非本轮既有失败。
* 实现后已使用 `review-work` skill，审查中属于本轮的问题已修复。
