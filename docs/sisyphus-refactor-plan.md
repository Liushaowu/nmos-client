# Sisyphus C++ 重构执行计划

本文档供未来 Sisyphus 执行 NMOS client C++ 重构使用。目标是把 `src/node.cpp` 和 `src/node.h` 中过于集中的节点控制面逻辑拆开，让代码更短、更清晰、更容易验证，同时保持 daemon 当前运行行为稳定。

## 目标

* `src/node.h` 不再按外部库 public API 保护，可以拆分、改名、收窄和简化；目标是形成清晰的 daemon 内部类型边界。
* 保留一个轻量 `Node` 外观入口和 PImpl 边界，除非后续切片能证明删除 PImpl 会让调用链更简单且行为验证完整。
* 降低 `Node::Impl` 的职责密度，把生命周期、配置、资源构造、缓存、IS 05 activation、auto resolver、SDP、PTP、callback 分离到更明确的内部模块。
* 在可行处减少重复代码和总代码量，同时保持显式可读。
* 对关键复杂逻辑添加简洁中文注释，说明意图、不变量和时序约束，而不是逐行解释代码。

## 非目标

* 不更改 CMake 目标、安装规则、包管理规则或构建目录策略。
* 不引入新框架、插件系统、依赖注入框架、事件总线、数据库、UI 或媒体数据平面功能。
* 不把控制面逻辑改造成媒体处理引擎。
* 使用正确的 `redundancy` 拼写，不再保留旧误拼兼容要求。
* 不把清晰的显式流程替换成难读的通用模板、宏技巧或过度泛型抽象。

## 硬约束

* `src/node.h` 可重构，但每次 header/type-surface 变更必须同步更新所有 include 和 call site，并通过 `cmake --workflow --preset debug` 验证。
* 默认保留 PImpl 边界。`Node` 仍是 daemon 内部外观入口，重逻辑仍藏在实现层或明确的内部模块中。
* 发送端生命周期必须保持为 `source -> flow -> sender -> connection_sender`。
* 接收端生命周期必须保持为 `receiver -> connection_receiver`。
* 必须保持 add、remove、update 的资源对称性，不能出现只新增不删除、只更新一侧状态的路径。
* 必须保持 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义，不得绕过这些 helper 复制元数据逻辑。
* 必须保持 `redundancy` 拼写一致。代码中如已有该字段，继续使用正确拼写。
* 不修改生成目录，不把 `build/`、`build_initdeep/`、`.cache/` 里的内容当作源码依据。
* 每次实施后必须运行验证命令。实现完成后必须使用 `review-work` skill 做后置审查。

## 当前重构热点

* `src/node.cpp` 中的 `Node::Impl` 是主热点，当前同时承载运行时生命周期、settings、资源构造、缓存、IS 05 activation、auto resolver、SDP、PTP 和 callbacks。
* sender 路径存在重复的 ID、label、flow、sender、connection resource 组装逻辑。
* receiver 路径存在与 sender 相似但不完全相同的资源注册、移除和连接处理逻辑。
* callback 注册、触发和状态访问需要更清楚的边界，避免业务流程和通知逻辑互相穿插。
* SDP、PTP、auto resolver 属于独立服务逻辑，适合从 `Node::Impl` 的主流程中抽出。
* `src/node.h` 同时混放外观类、sender/receiver 数据模型、callback 类型依赖、cpprest settings/runtime interface 依赖和全局常量；现在应优先拆出内部类型头，降低重编译和耦合。

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
* `NodeTypes` 负责 sender/receiver/registration 等 daemon 内部数据模型。
* `NodeFacade` 或保留的 `node.h` 只负责声明 `Node` 外观入口和它直接需要的最小类型/include。

## 模块依赖方向

* `NodeServerRuntime` 可以协调各内部模块，但不直接构造 sender、receiver、flow、connection resource 的细节。
* `NodeSettings` 只提供配置读取和访问能力，不依赖资源工厂、生命周期服务、连接处理或 callback 调度。
* `SenderResourceFactory` 和 `ReceiverResourceFactory` 只负责资源对象构造，不写入 `StreamStore`，不触发 callback，不直接操作 NMOS server 资源集合。
* `StreamStore` 只保存和校验内部映射，不调用资源工厂、连接处理或 callback 调度。
* `ResourceLifecycleService` 负责把 factory、store 和 NMOS resource insert/remove/update 编排在一起，是资源生命周期对称性的唯一主入口。
* `ConnectionHandlers` 可以读取必要的 store 状态并触发 callback 调度，但不得反向承担资源创建职责。
* `CallbackDispatcher` 不直接修改 NMOS resources，不直接读写配置文件，不参与 sender/receiver 生命周期编排。
* 数据模型头不依赖 `cpprest/json.h`、`cpprest/host_utils.h` 或 NMOS runtime 头；这些依赖留在外观/实现/服务边界。
* `Node` 外观头不得继续承载无关 helper 常量；例如 `delay_millis` 这类实现细节应迁入实现文件或 lifecycle 相关内部头。
* 新模块之间不得形成循环依赖。若出现循环依赖，优先收紧数据结构或提取更小的纯 helper，而不是新增中介层。

## 执行阶段

### 阶段 1，建立基线与保护线

* 目标：确认当前行为、构建状态和高风险路径，形成重构前基线。
* 允许变更：只允许新增面向重构的内部测试或最小验证脚本，前提是项目已有合适位置；可记录 `node.h` 当前类型和 include 形态作为后续拆分基线。若没有测试基础设施，本阶段不强行补测试框架。
* 禁止变更：不得移动大段实现，不得调整 CMake，不得改动 `redundancy` 字段语义。
* 验证：优先运行 `cmake --workflow --preset debug`。
* 行为快照：记录重构前 sender/receiver 资源 JSON 或关键字段形态、ID 生成样例、label/description/group hint 结果、add/remove/update 资源数量变化、activation 到 callback 的触发顺序。快照可以是文档、测试 fixture 或最小验证脚本输出，但必须能被阶段 8 对照。
* 停止条件：构建基线通过，或发现与重构无关的既有失败并记录清楚，不把它混入重构提交；高风险路径的行为快照已记录到可复查的位置。

### 阶段 2，拆分 `node.h` 类型表面

* 目标：把 `src/node.h` 从“大而全入口头”改成轻量外观头，并把 sender/receiver/registration 数据模型拆到内部类型头中。
* 允许变更：新增 `node_types.h`、`node_callbacks.h`、`node_runtime_interface.h` 等小头；调整 include；把 `delay_millis` 等实现常量移出 `node.h`；按当前 daemon 调用点同步改名或收窄字段。
* 禁止变更：不得改变运行行为、资源 ID 语义、配置键名、callback 触发时机或 `redundancy` 字段语义。不得为了拆头引入新依赖或生成代码。
* 推荐顺序：先移动纯数据模型和 callback alias，再移动 cpprest/json/settings 相关声明，最后收缩 `node.h` include，只保留 `Node` 声明所需内容。
* 验证：运行 `cmake --workflow --preset debug`。检查 `src/main.cpp` 和所有 `node_*` 模块 include 不再依赖 `node.h` 的偶然传递 include。
* 停止条件：`node.h` 只表达 `Node` 外观入口；数据模型和 callback 类型有清晰归属；后续模块可直接 include 所需窄头。

### 阶段 3，抽出配置与运行时外壳

* 目标：把 settings 读取和 server 生命周期协调从 `Node::Impl` 主体中分离，形成 `NodeSettings` 和 `NodeServerRuntime`。
* 允许变更：移动内部私有逻辑，保持调用顺序和外部行为不变。可减少重复的 settings 访问代码。
* 禁止变更：不得改变 `Node::start`、`Node::stop`、构造函数或 daemon 当前调用语义。不得改变配置文件键名。
* 验证：构建通过，并用最小驱动确认 `Node` 可按原入口构造和启动路径编译。
* 停止条件：`Node::Impl` 不再直接混杂大量配置解析细节，启动链仍保持 `Node::start -> Node::Impl::start -> 内部 runtime` 的可追踪路径。

### 阶段 4，抽出 sender 与 receiver 资源工厂

* 目标：把资源构造逻辑拆进 `SenderResourceFactory` 和 `ReceiverResourceFactory`，减少重复构造代码。
* 允许变更：集中 ID、label、description、group hint 的调用位置，复用公共小函数，压缩重复分支。
* 禁止变更：不得改变 sender 生命周期 `source -> flow -> sender -> connection_sender`。不得改变 receiver 生命周期 `receiver -> connection_receiver`。不得绕过 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint`。
* 验证：构建通过。检查 sender 和 receiver 的新增路径中资源创建顺序、ID 生成规则和 label 语义与重构前一致。
* 停止条件：sender 和 receiver 的工厂职责清晰，重复构造代码减少，但调用处仍能直接看出资源生命周期。

### 阶段 5，抽出资源生命周期服务与存储

* 目标：把 add、remove、update 的对称编排放入 `ResourceLifecycleService`，把内部资源映射放入 `StreamStore`。
* 允许变更：合并重复的插入、删除、查找、状态更新流程。可用小型 helper 表达共同步骤。
* 禁止变更：不得制造只处理 sender 或只处理 receiver 的单边路径。不得用难读的泛型算法隐藏生命周期顺序。
* 验证：构建通过。逐项核对 add、remove、update 是否仍成对维护 source、flow、sender、receiver、connection resource 和缓存状态。
* 停止条件：任意资源类型的新增、删除、更新都能在一个明确服务中追踪，且失败路径不会留下半更新状态。

### 阶段 6，抽出连接处理、SDP 和 callback

* 目标：把 IS 05 activation、transport 参数、SDP 逻辑和用户 callback 调度从 `Node::Impl` 中分离。
* 允许变更：新增 `ConnectionHandlers`、`SdpService`、`CallbackDispatcher` 内部类或文件，保留原有线程安全假设。
* 禁止变更：不得改变 callback 触发时机和入参语义。不得新增事件总线或插件系统。不得引入外部依赖。
* 验证：构建通过。检查 activation 到 callback 的调用链，确认 staged、active 和 transport 参数处理顺序保持一致。
* 停止条件：`Node::Impl` 只保留协调职责，不直接堆叠连接细节、SDP 细节和 callback 分发细节。

### 阶段 7，减代码量与注释整理

* 目标：在行为稳定后删除重复实现，收紧局部 helper，并补充关键中文注释。
* 允许变更：删除重复代码、合并等价分支、缩小变量作用域、提取清晰小函数、补充简洁中文注释。
* 禁止变更：不得为了减少行数牺牲可读性。不得把显式的 NMOS 生命周期改成难以追踪的通用配置表。不得添加逐行字面注释。
* 验证：构建通过。人工阅读主要调用链，确认代码更短但执行顺序仍清楚。
* 停止条件：总代码量在可行处下降，复杂逻辑的意图和不变量有中文注释，且没有新增抽象层噪音。

### 阶段 8，基于新架构补充测试代码

* 目标：在架构边界稳定后，按新的内部模块边界补充测试代码，覆盖重构后的关键行为和高风险路径。
* 允许变更：新增或补充面向 `NodeSettings`、资源工厂、`StreamStore`、`ResourceLifecycleService`、`ConnectionHandlers`、`SdpService`、`CallbackDispatcher` 的测试或最小验证驱动。若项目尚无测试框架，先补充最小可维护的内部验证代码，不为测试引入重量级框架。
* 测试位置：优先放在仓库源码管理范围内的 `tests/` 或 `src/` 附近的内部测试目录。不得放入 `build/`、`build_initdeep/`、`.cache/` 或只适合人工演示的样例路径。
* CMake 策略：如果新增正式测试目标，使用 CMake 管理并尽量支持 `ctest --test-dir build`。如果暂时只能提供最小验证驱动，也必须写清固定运行命令，不能只依赖人工点击或临时命令。
* 禁止变更：不得为了方便测试扩大 `Node` 外观入口或重新制造宽头依赖。不得为了让测试通过改变生产行为。不得把测试代码写进生成目录或运行时样例路径中混用。
* 验证：构建通过，并运行新增测试或最小验证驱动。测试至少覆盖 sender/receiver 资源创建顺序、add/remove/update 对称性、ID/label/group hint 语义、activation 到 callback 的关键路径，并对照阶段 1 的行为快照。
* 停止条件：新架构的核心模块已有对应测试保护，测试能在本地通过一条固定命令稳定运行，失败时能定位到具体模块边界或生命周期约束。

### 阶段 9，后置审查与收尾

* 目标：确认重构没有引入行为回归，提交拆分清楚，审查发现已处理。
* 允许变更：只修复本轮重构引入的问题，补充缺失验证或注释。
* 禁止变更：不得借收尾阶段继续扩大架构范围。不得修复无关历史问题，除非它阻塞本次验证。
* 验证：再次运行完整验证命令和新增测试，并使用 `review-work` skill 做实现后审查。
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

如果新增了 CMake 测试目标，必须追加运行：

```bash
ctest --test-dir build --output-on-failure
```

如果暂时只有最小验证驱动，必须在本节或阶段记录中写明唯一推荐运行命令，并保证后续执行者不需要重新猜测参数。

验证要求：

* 每个阶段完成后都要运行与改动范围匹配的构建验证。
* 架构重构完成后必须运行基于新模块边界补充的测试或最小验证驱动。
* 新增测试或最小验证驱动必须能被一条固定命令运行，并优先接入 CMake/CTest，使本地和 CI 能复用同一验证入口。
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

## 线程安全与失败处理规则

* 明确每个新模块是否持有状态、是否需要锁、是否只在 NMOS server 线程中调用。不能把原本隐式的线程假设在拆分时丢掉。
* callback 不应在持有资源生命周期锁或 store 写锁时直接调用，避免用户 callback 反向调用 `Node` 时造成死锁或重入问题。
* `CallbackDispatcher` 负责收口 callback 调度边界，调用前应明确所需数据快照，调用后不得依赖 callback 未修改内部状态的假设。
* activation、staged、active 和 transport 参数处理的顺序必须保持与重构前一致；如果需要改变锁边界，必须用注释和测试说明新边界的安全性。
* `ResourceLifecycleService` 必须定义 add、remove、update 的失败边界。任一步失败时，要么完整回滚到调用前状态，要么保持一个文档化且可测试的一致状态。
* sender 新增失败不能留下孤立的 source、flow、sender 或 connection_sender；receiver 新增失败不能留下孤立 receiver 或 connection_receiver。
* remove 或 update 遇到缺失资源时，必须延续现有行为语义。不得为了简化重构把缺失资源从可恢复状态改成未定义状态。
* 测试或最小验证驱动应覆盖至少一个新增失败路径和一个 callback 调度路径，确认不会留下半更新状态或锁内 callback 调用。

## 完成度指标

* `Node::Impl` 完成后只承担外观入口背后的协调职责，不直接堆叠 settings 解析、sender/receiver resource 构造、SDP 细节或 callback 分发细节。
* `Node::Impl` 中允许保留启动链、模块组合和少量跨模块编排代码，但新增资源的具体构造顺序应主要出现在资源工厂和生命周期服务中。
* 每个目标模块都应能用一句职责描述解释清楚；如果一个模块需要用“以及”连续描述三类以上职责，说明边界仍需收紧。
* 新增文件和类必须服务于降低复杂度。若拆分后调用链更难追踪，应优先合并或改名，而不是继续增加层级。
* reviewer 应能从 `ResourceLifecycleService` 追踪任意 sender/receiver 的 add、remove、update 主路径，从 `ConnectionHandlers` 追踪 activation 到 callback 主路径。

## 提交拆分建议

* 提交 1：建立基线验证和必要的内部保护线，不改行为。
* 提交 2：拆分 `node.h` 类型表面，形成轻量 `Node` 外观头和内部数据模型头。
* 提交 3：抽出 `NodeSettings` 和 `NodeServerRuntime`。
* 提交 4：抽出 `SenderResourceFactory` 和 `ReceiverResourceFactory`。
* 提交 5：抽出 `StreamStore` 和 `ResourceLifecycleService`。
* 提交 6：抽出 `ConnectionHandlers`、`SdpService`、`CallbackDispatcher`。
* 提交 7：减代码量、清理重复逻辑、补充关键中文注释。
* 提交 8：基于新架构补充测试代码或最小验证驱动，覆盖关键模块边界和生命周期约束。
* 每个提交都必须能独立构建。不要把无关格式化、CMake 调整或历史问题修复混入这些提交。

## Sisyphus 执行清单

* 开始前阅读 `AGENTS.md`、`src/AGENTS.md`、`src/node.h`、`src/node.cpp`、`src/node_implementation.h`、`src/node_implementation.cpp`。
* 明确当前阶段，只改该阶段允许的内部边界。
* 每次编辑前确认不会修改生成目录；CMake 只在新增源码或测试目标时做最小必要调整。
* `node.h` 可拆分简化，但每次拆分必须同步 include/call site，并保持 daemon 当前行为。
* 默认保留 PImpl 边界，保留 sender 和 receiver 生命周期顺序。
* 保留 add、remove、update 对称性。
* 保留 `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义。
* 保持 `redundancy` 正确拼写。
* 优先删除重复代码，但拒绝难读的通用抽象。
* 给关键复杂逻辑补简洁中文注释。
* 架构边界稳定后，基于新模块边界补充测试代码，覆盖资源生命周期、连接处理和 callback 高风险路径。
* 阶段结束后运行验证命令。
* 实现全部完成后，使用 `review-work` skill 做后置审查，并处理审查指出的本轮问题。

## 验收标准

* `src/node.h` 已收缩为轻量外观头，数据模型、callback 类型和运行时辅助类型已拆到职责清晰的内部头。
* PImpl 边界默认保留，`Node` 仍是 daemon 内部外观入口；若未来删除 PImpl，必须有单独计划和完整验证。
* sender 生命周期仍为 `source -> flow -> sender -> connection_sender`。
* receiver 生命周期仍为 `receiver -> connection_receiver`。
* add、remove、update 路径保持资源和缓存状态对称。
* `impl::make_id`、`impl::set_label_description`、`impl::insert_group_hint` 的语义保持不变。
* `redundancy` 正确拼写保持不变，没有保留旧误拼字段。
* `Node::Impl` 职责减少，内部模块边界能对应目标模块边界。
* `Node::Impl` 不再直接承担 sender/receiver resource 构造、SDP 细节或 callback 分发细节，主要保留启动链和模块协调职责。
* 新模块依赖方向清晰，没有循环依赖，factory、store、lifecycle、connection、callback 的职责没有反向泄漏。
* 窄头之间没有依赖倒灌：数据模型头不依赖 cpprest/NMOS runtime；外观头不暴露实现常量；实现模块不依赖 `node.h` 的偶然传递 include。
* 线程安全边界和失败回滚边界已明确，callback 不在资源生命周期锁或 store 写锁内直接调用。
* 重复代码和总代码量在可行处减少，可读性没有下降。
* 关键复杂逻辑有简洁中文注释，注释解释意图和不变量，不做逐行字面说明。
* 架构重构完成后已基于新模块边界补充测试代码或最小验证驱动，覆盖核心资源生命周期和连接回调路径。
* 新增测试或最小验证驱动可通过固定命令运行；若接入 CMake 测试目标，`ctest --test-dir build --output-on-failure` 通过。
* 验证命令通过，或存在已记录的非本轮既有失败。
* 实现后已使用 `review-work` skill，审查中属于本轮的问题已修复。
