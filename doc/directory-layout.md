# ESL 平台目录规划

项目根目录下统一为五个目录：`model`、`top`、`benchmark`、`doc`、`scrpits`。构建采用 CMake 和 GCC 12.2，仿真使用本项目开发的事务级测试 HA/L3。

变量、函数、CMake 目标、测试名、脚本和内部目录采用小驼峰命名；C++ 类型及其同名文件采用大驼峰。已确定的根目录和模型组件目录保持现状，具体见 [编码与命名规范](C:/Explore/TOC_ESL/doc/coding-style.md)。

以下目录已按现有结构补齐。保留已有 `model/SoC/include`、`lib`、`src`，其他模型沿用相同布局；代码、构建文件和脚本按里程碑实现。总体开发任务见 [实现计划](C:/Explore/TOC_ESL/doc/implementation-plan.md)。

## 目录结构

```text
TOC_ESL/
├── model/                       模型代码与共享接口
│   ├── CMakeLists.txt           模型库构建
│   ├── Common/                  非协议公共类型与仿真服务接口
│   ├── ChiCommon/               CHI opcode、flit、CHIPort 与 credit
│   ├── SoC/                     已有模型目录，保留 include/lib/src
│   ├── RN/                      私有缓存、协议状态与参数化时序
│   ├── HA/                      测试 HA：目录、事务、snoop、Retry
│   ├── L3/                      测试 L3：tag/data、miss、writeback
│   ├── NoC/                     四通道传输、带宽、排队与背压
│   ├── Memory/                  Memory 协议端点、数据与时序
│   └── MemoryEngine/            后续 Memory Op Engine
│
├── top/                         仿真构建与运行所需内容
│   ├── CMakeLists.txt           完整工程入口，链接 model，注册 CTest
│   ├── CMakePresets.json        Debug/Release 等共享构建配置
│   ├── cmake/                  工具链、依赖和编译选项
│   ├── src/                    main、调度、组装、驱动、检查与观测
│   ├── config/                 平台参数、回归批次、扫描配置及格式约束
│   ├── tests/                  单元与集成测试程序、测试夹具
│   ├── build/                  CMake 生成物和可执行文件
│   └── run/                    配置快照、日志、Trace 与报告
│
├── benchmark/                  测例输入、操作序列与期望结果
│   ├── smoke/                  最小读写闭环
│   ├── coherence/              共享、失效、owner 转移与写回
│   ├── stress/                 资源耗尽、Retry、背压与竞争
│   ├── negative/               非法输入与故障注入测例
│   ├── performance/            仿真效率、吞吐与延迟测例
│   └── litmus/                 后续 Memory Consistency 测例
│
├── doc/                        计划、架构、接口与参考资料
│   ├── implementation-plan.md
│   ├── directory-layout.md
│   ├── chi-interface.md
│   ├── architecture.md
│   ├── interfaces.md
│   └── references/             CHI 等参考文档
│
└── scrpits/                    构建、运行、回归和分析脚本
    ├── build.py
    ├── run.py
    ├── regression.py
    ├── sweep.py
    ├── replay.py
    └── report.py
```

根目录可保留 `README.md` 和 `.gitignore` 等工程说明文件。完整构建入口位于 `top/CMakeLists.txt`。文件名表示职责划分，未实现部分无需提前创建空文件。

## 模型与仿真的边界

`model` 保存可实例化的组件及模型状态；`top` 负责创建、连接和驱动这些组件。测试 HA/L3 分别放在 `model/HA` 和 `model/L3`。所有数据通路组件统一通过 `model/ChiCommon` 提供的 `CHIPort` 连接，包括 HA–L3。

`model/ChiCommon` 保存 CHI opcode、REQ/RSP/SNP/DAT flit 和 CA 级端口接口；具体规则见 [CHI 公共接口](C:/Explore/TOC_ESL/doc/chi-interface.md)。`model/SoC` 提供参考时钟环境 `EnvCfg`，SoC 持有唯一实例并负责推进 cycle。`model/Common` 提供所有模型继承的 `BaseModule` 和非协议共享类型；`BaseModule` 构造时必须传入模块名、`EnvCfg` 引用和整数分频配置，并要求派生模块实现组合逻辑 `work()` 和打拍逻辑 `xfer()`。事件队列、日志输出和 Checker 的实现位于 `top/src`。模型不能包含 top 的实现头文件或读取 top 的全局状态。

`top/src` 初期以文件区分职责，无需继续拆出大量子模块：

| 文件职责 | 内容 |
| --- | --- |
| `main` | CLI 参数、运行入口与退出码 |
| `simKernel` | 时间推进、事件队列、稳定排序和随机源管理 |
| `system` | 模型创建、拓扑连接、复位与结束条件 |
| `homeComplex` | 测试 HA/L3 组装、能力描述与模型替换边界 |
| `workload` | 加载 benchmark、触发请求与脚本流量生成 |
| `checker` | 协议、权限、数据、资源守恒与进展检查 |
| `trace`、`metrics` | 事务记录、统计和报告输出 |
| `config` | 配置读取、校验、覆盖及最终配置快照 |

每个模型目录统一包含 `include/`、`lib/`、`src/`。`include` 保存公开头文件，`src` 保存实现和私有头文件，`lib` 为模型库文件预留；后续新增对应的 `CMakeLists.txt`。公开头文件由对应 CMake target 导出，内部状态机保持私有。Checker 只使用公开观察契约，不复用 HA/RN 的状态转换实现。

## 测例与运行数据

- `top/config` 描述运行环境：模型参数、拓扑、seed、测例选择和扫描范围。
- `benchmark` 描述测试内容：初始化数据、操作序列、触发条件、期望值和必要说明。测例特有的参数覆盖随测例保存，运行时合并进最终配置快照。
- `top/tests` 保存自动化检查程序和夹具，引用 `benchmark` 的场景。简单单元测试的输入可直接写在测试程序中。
- `top/run/<batch-id>/<run-id>` 保存 `run.json`、最终配置、场景快照、`events.jsonl`、`metrics.json` 和失败上下文；批次汇总也保存在 `top/run`。
- `scrpits` 调用构建和仿真程序、组织回归并分析结果，生成数据写入 `top/build` 或 `top/run`。

仿真程序通过显式配置与测例路径运行。文件引用的解析基准固定为声明该引用的配置文件所在目录，避免依赖进程启动目录。运行记录保存最终解析的输入及版本，以支持重放。

`top/build/`、`top/run/`、`top/CMakeUserPresets.json` 和 Python 缓存不纳入版本管理。

## 构建组织

`model/CMakeLists.txt` 聚合 `eslCommon`、`eslRn`、`eslTestHa`、`eslTestL3`、`eslNoc`、`eslMemory` 等 target，后续增加 `eslMemoryEngine`。模型不链接 top 的具体运行库。

`top/CMakeLists.txt` 通过 `add_subdirectory` 引入 `model`，为其指定独立 binary directory；将仿真支撑代码构建为 `eslRuntime`，由 `eslSim` 与测试程序复用。`eslRuntime` 实现 model 中声明的服务接口，并链接具体模型库。初期无需为 top 中每项职责单独建立一个库。

从项目根目录执行的预期构建方式为：

```sh
cmake -S top -B top/build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build top/build/debug
ctest --test-dir top/build/debug --output-on-failure
```

工程配置检查 GCC 12.2；编译器实际位置由工具链或本机 preset 指定。WSL2 + Debian 12 编译环境已就绪，实际版本和用法见 [编译运行环境](C:/Explore/TOC_ESL/doc/build-environment.md)。环境安装脚本和 `top/tests/toolchainSmoke` 独立检查项目已运行成功，CTest 1/1 通过；平台主工程尚未实现，以上命令为主工程构建入口就绪后的用法。

## 建立顺序

先建立 `model/Common`、`top` 的构建与运行骨架，再实现 `model/Memory`、`model/L3`、`model/HA` 和 `benchmark/smoke`，打通单 RN 脚本读闭环。之后增加 RN、多节点一致性、有限资源、完整 NoC 及批量回归脚本。MemoryEngine 和 Litmus 随 M6 引入。

未来外部 HN/L3 的接口适配代码归入 `model`，系统选择和组装归入 `top`，保持五个根目录的分类。当前开发与验收均不依赖外部 HN/L3 源码。
