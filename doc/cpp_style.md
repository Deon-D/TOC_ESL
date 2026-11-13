# C++ 与工具代码规范

本文约束语言、命名、格式和接口表达。项目目录、依赖边界、变更范围及验证要求统一见[项目规范](project_standards.md)；构建和检查命令见[构建与运行](build_and_run.md)。

这些要求用于新增代码和本次实质修改的代码。仓库已有 `r_`、`c` 等简写以及部分紧凑声明，不能据此宣称全部存量代码已符合本文，也不应在无关功能改动中顺带批量重命名。修复相关代码时可局部改进；公开接口和协议专用名称按既有契约维护。

## 语言与编译

| 项目 | 要求 |
| --- | --- |
| C++ | C++17；不使用 C++20 及以上接口或编译器专用语言扩展 |
| CMake | 至少 3.16；`CMAKE_CXX_STANDARD=17`、`CMAKE_CXX_STANDARD_REQUIRED=ON`、`CMAKE_CXX_EXTENSIONS=OFF` |
| 编译诊断 | 项目 C++ 目标使用 `-Wall -Wextra -Wpedantic -Werror` |
| Python | 工具与测试使用 Python 3.10.x，不引入需要 3.11 及以上的语法、标准库接口或依赖要求 |
| 格式工具 | clang-format 18，以仓库配置为准 |

根[构建文件](../CMakeLists.txt)定义全工程设置，独立 VIP 构建保持同一 C++ 标准和警告要求。`if constexpr`、内联变量、结构化绑定、`std::optional` 和 `std::variant` 属于可用的 C++17 特性。Python 是工具和测试依赖，不是 C++ 仿真程序的运行依赖。

项目采用 Google 命名与格式作为基线，不整体引入其语言限制，也不增加外部风格库依赖。应修复实际编译诊断，不通过整体关闭警告掩盖问题。

## 命名与文件组织

| 对象 | 规则 | 示例 |
| --- | --- | --- |
| 命名空间 | 小写，按模块分层 | `esl`、`esl::chi_vip` |
| 类型、普通函数 | UpperCamelCase，保留领域缩写 | `RN`、`CHIPort`、`Work`、`GetTxnID` |
| 局部变量、参数 | snake_case | `request_count`、`line_address` |
| 类成员变量 | snake_case，末尾加下划线 | `config_`、`request_slots_` |
| 配置、事件等数据结构字段 | snake_case，无末尾下划线 | `node_id`、`timeout_cycles` |
| 常量、枚举值 | kUpperCamelCase | `kAllBytes`、`kReadNoSnp`、`CacheState::kUD` |
| 普通源文件 | snake_case，`.hpp` / `.cpp` | `rn.hpp`、`request_engine.cpp` |

名称应说明角色，尤其是跨多个函数保存的状态、容器和资源。新增代码使用 `runtime_`、`context` 等可辨认名称，避免 `r_`、`c` 这种需要反复查找声明的缩写；局部计数器和领域已有名称可按上下文简写。RN、HN、SN、CHI、DBID 等协议术语无需强行展开成冗长名称。

RN/HN/SN 核心分别位于 `model/chi_vip/rn`、`hn`、`sn`，各自区分 `include/` 与 `src/`。协议核心类使用 `RN`、`HN`、`SN`；`RNModel`、`HNModel`、`SNModel` 是明确的 ModelBase 适配类型，不因命名整理去掉其后缀。

时间和数据单位应从类型或名称中可辨认：物理时间使用 `_ps`，周期延迟使用 `_cycles`；已有公开配置名如 `service_latency` 保持契约，并在接口文档中注明单位。区分字节地址、行地址、SNP 编码地址和行相对字节掩码，不能都用含义不明的 `value` 表达。

## CHI 载荷专用约定

公共 CHI 载荷保留协议接口的现有命名，不套用普通函数改名规则：

| 对象 | 约定 |
| --- | --- |
| 类型 | `CHIReqFlit`、`CHIRspFlit`、`CHISnpFlit`、`CHIDatFlit` |
| 字段 | `flit_xxx`，例如 `flit_txn`、`flit_address` |
| Opcode | `CHI_REQ_Opcode_XX` 等宏，值为带括号的十六进制常量 |
| 内置方法 | `reset()`、`to_string()`，以及 `operator<<` |
| 头文件 | `chi_common.h`、`chi_channel.h`、`chi_port.h` |

标量控制字段和数据字至少使用 `uint32_t`，地址和宽掩码按需要使用 `uint64_t`；协议位宽、可选字段和复用关系写入字段注释。容器大小不等于线上字段合法范围，接收和构造请求仍须校验编码。

DAT 数据字与字节通过显式移位和掩码转换，不依赖主机字节序。SNP 地址保存协议的 `Addr[PA-1:3]` 编码，目的节点属于运输元信息，不向 SNP 添加非标准 TgtID。完整接口语义见 [CHI 接口说明](../model/chi_interface/README.md)。

## 格式

[`.clang-format`](../.clang-format) 是排版依据，使用 clang-format 18 执行。当前显式覆盖项如下：

| 设置 | 值或效果 |
| --- | --- |
| `BasedOnStyle` | Google |
| `IndentWidth`、`ContinuationIndentWidth` | 4 空格 |
| `ColumnLimit` | 100 列，折行交给格式工具处理 |
| `AccessModifierOffset` | -4，访问限定符相对成员向前缩进 |
| `AllowShortFunctionsOnASingleLine` | 仅空函数允许单行 |
| `AllowShortIfStatementsOnASingleLine` | Never |
| `AllowShortLoopsOnASingleLine` | false |
| `SortIncludes` | CaseSensitive |

新增声明一次定义一个变量，不将多条状态修改压到同一行。不要为视觉对齐添加与格式工具冲突的空格。格式检查覆盖项目 `.h`、`.hpp`、`.cpp`，不扫描第三方源码；检查模式只报告问题，不修改文件。Python 保持相邻代码的缩进和组织方式，不在本规范中引入额外格式工具。

## 头文件与接口

头文件使用 `#pragma once`，能够独立包含，并直接包含其声明所需的类型定义或合法前向声明，不依赖偶然的传递包含。普通实现放在 `.cpp`；模板和必要的内联定义可以留在头文件。公开头文件不使用全局 `using namespace`，避免把名称引入调用方作用域。

接口声明应让调用者在不阅读实现的情况下了解以下内容：

- 参数和返回值含义、单位、地址或掩码的坐标系，以及合法范围。
- 调用阶段及生命周期约束，例如启动前配置、Work 内使用、Commit 后可见。
- 返回值表示受理、入队、实际发送还是事务完成；这些状态不能混称为“成功”。
- 对象所有权、借用期限和视图失效条件。
- 普通背压、合法访问错误和异常分别如何报告。

只读查询使用适当的 `const` 限定。能独立返回诊断快照时，不暴露可修改的内部状态；必须返回借用视图时，说明何时失效。事件、动作和 Observer 记录的接口需明确复制或移交语义，不能依赖调用者猜测缓冲区寿命。

## 所有权与资源

使用 RAII 表达资源生命周期。按值成员或 `std::unique_ptr` 适合独占对象；需要共享所有权时再使用 `std::shared_ptr`。指针或引用用于借用时，必须保证被借用对象覆盖使用期，并在公共接口中说明这一关系。

模型借用 `ModelContext` 的平台服务，平台时钟不由模型推进或释放。运输适配器借用核心和端口，相连端口需要覆盖整个运行期。拥有状态及资源的模型是否可复制或移动应显式定义，不能让编译器生成的复制意外复制链路关系或活动事务。

协议资源释放按协议条件实现，不能依靠 C++ 对象析构替代事务完成。TxnID、DBID、snoop 上下文、行锁和 credit 可能有不同回收点；相关代码应说明决定回收时机的响应或实际发送条件。

## 背压、错误与异常

| 情况 | 表达方式 |
| --- | --- |
| FIFO 满、事务槽不足、暂不可受理 | 前端返回 `false` 或未受理结果；HN/SN 按协议重试及资源保证处理，不抛协议异常 |
| 协议允许的访问错误 | 通过响应、完成结果及数据错误标记传播，继续完成必要消息流 |
| 不可恢复的配置、调用阶段或协议错误 | 使用所在层的异常类型终止当前运行或测试，并保留上下文 |
| 合法但未启用的协议功能 | VIP 使用 `UnsupportedFeature` 分类，不当作成功或保留编码违规 |
| 非法字段、未知/重复标识、违规消息流 | VIP 使用 `ProtocolViolation` 分类 |

VIP 核心使用 `VIPError`，公共平台使用 `PlatformError`，独立 CHIPort 使用自身检查并抛标准异常；不要为了统一异常类型让独立库反向依赖平台。捕获异常应补充诊断或转换为明确的失败结果，不吞掉错误继续生成完成。超时用于诊断，不自动伪造 Comp 或释放仍承担协议义务的资源。

错误消息使用英文，包含足以定位的节点、事务、地址或等待原因；不要求每条消息机械列出全部字段。核心不直接使用第三方日志类型或全局默认 logger。

## 注释与文档

代码注释、异常消息和运行日志使用英文；设计、使用和项目规范文档使用中文。类型名、字段名和规范术语保留原文，避免翻译后无法与代码对应。

接口注释解释契约，实现注释解释时序、仲裁、状态变化和资源回收的理由。避免逐字复述代码。依赖 CHI 规则的分支应在相关规则记录或代码注释中保留规范章节；不要仅写“按规范处理”却没有可追踪出处。

修改接口或可见行为时同步更新相关注释和文档。纯文档整理不要求全工程回归；具体改动所需的格式、构建、定向测试、全量测试和内存检查由[项目规范的验证要求](project_standards.md)统一决定，不在本文另设批准流程。
