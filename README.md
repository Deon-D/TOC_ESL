# CHI RN/HN/SN ESL 仿真平台

当前主线是 C++17、事件驱动的 CHI G.b `GbCore` 系统：默认 **2 RN-F + 1 HN-F + 1 SN-F**，节点之间使用真实 `CHIPort`。RN 提供缓存及 Load/Store，HN 负责目录和一致性，SN 提供异步字节内存；`esl_sim` 运行混合读写负载并检查数据。

- [项目规范](doc/project_standards.md) · [协作速查](AGENTS.md)
- [构建、运行与验证](doc/build_and_run.md)
- [总体架构与演进边界](doc/platform_architecture.md)
- [基础组件设计](doc/foundation_design.md) · [时钟与阶段契约](doc/clock_design.md) · [代码规范](doc/cpp_style.md)
- [CHI 接口函数说明](model/chi_interface/README.md)
- [VIP 接口与 RN 支持矩阵](model/chi_vip/README.md) · [RN/HN/SN 连接与系统测试](model/chi_vip/SYSTEM.md)
- [RN 架构](model/chi_vip/rn/ARCHITECTURE.md) · [HN 架构](model/chi_vip/hn/ARCHITECTURE.md) · [SN 架构](model/chi_vip/sn/ARCHITECTURE.md)
- [默认配置](config/default.toml)

## 代码组织

| 目录 | 职责 |
| --- | --- |
| `foundation/` | 强类型时间、配置、日志和 Trace |
| `kernel/` | ModelBase 生命周期、单时钟 Work/Xfer 调度和定时事件 |
| `model/chi_interface/` | 标准 CHI flit、四通道 FIFO、link credit 和独立 CHIPort |
| `model/chi_vip/rn/` | RN 请求引擎、snoop 引擎、缓存及行协调 |
| `model/chi_vip/hn/` | HN 目录、逐行排序、snoop 汇聚和下游事务映射 |
| `model/chi_vip/sn/` | SN 请求服务、写缓冲及异步内存后端 |
| `model/chi_vip/include/`、`src/` | 公共类型、规则、运输层及平台适配 |
| `model/chi_vip/system/` | 当前系统装配、混合读写负载、独立数据参考与统计 |
| `config/`、`tests/`、`tools/` | 运行配置、回归、依赖准备和格式检查 |

RN/HN/SN 分别使用 `include/` 和 `src/`，统一命名空间为 `esl::chi_vip`。公共平台和 CHI 接口继续复用，后续功能围绕这套组件演进。旧只读 CHI→AXI 子系统、其专用模型和 AXI 接口已移除，不再提供第二套系统入口。

## 构建边界

`esl` 提供公共平台，`esl_chi` 提供独立端口，`esl_chi_vip` 提供三个协议核心。运输层、ModelBase 适配和 `esl_chi_vip_system` 依次连接核心与应用；`esl_sim` 是当前系统入口。协议核心和运输层可以独立构建，不依赖日志库、TOML 或 ModelBase。

全工程使用固定版本的 spdlog 和 toml++，通过 `tools/prepare_dependencies.py` 校验并准备，构建和仿真不自动联网。Python 工具与测试统一使用 Python 3.10.x；C++ 使用 C++17 和严格编译警告。具体命令见[构建文档](doc/build_and_run.md)。

## 协议与周期边界

支持集合包括 20 种主动请求、8 种 snoop、七种 RN 缓存状态，以及对应 DBID、CompAck、重试和排序流程。采用 64B 行、256-bit DAT、单时钟、Normal memory 和 Non-secure 默认配置；具体合法组合见 VIP 支持矩阵。HN 不含 LLC，SN 的固定延迟内存不模拟 DRAM bank/row 时序。

所有模型先完成 Work，再执行 Xfer；跨节点消息最早下一周期可见。`Send()` 成功只表示本地 TX 入队，实际发送由 `Transmitted()` 确认。链路参数只通过 C++ API 设置，不提供 `[chi]` 配置项或 CHI 命令行参数。

当前验收范围为 **G.b 指定事务集合的协议层系统**，不声明完整 G.b、引脚级覆盖或经过芯片校准的性能精度。单元测试、真实链路集成和应用回归分别验证节点行为、资源生命周期、数据一致性及系统可复现性。
