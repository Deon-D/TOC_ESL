# CHI RN/HN/SN 系统构建与运行

`esl_sim` 是当前系统入口，使用 `esl::chi_vip::System` 装配多个 RN、一个 HN 和一个 SN，链路均为 CHI。默认两个 RN，运行一致性 Load/Store 混合负载；负载结束后通过协议回写或驱逐缓存，再与独立字节参考比较 SN 存储。

## 环境与依赖

Linux/WSL 需要 C++17 编译器、CMake 3.16 及以上、Python 3.10.x。Python 工具和回归统一使用 3.10 系列；Python 不参与 C++ 仿真的运行时。第三方依赖固定为 spdlog 1.15.3、toml++ 3.4.0。准备脚本校验 SHA-256，CMake 检查版本标记，构建和仿真不自动联网。

当前 WSL 发行版为 `TOC-ESL-Debian12`，仓库路径为 `/mnt/c/Explore/TOC_ESL`。Python 3.10 入口为 `/usr/local/bin/python3.10`，已准备依赖位于 `/tmp/esl-deps/sources`。这些路径是本机配置，迁移环境时需要相应调整。

在仓库根目录执行：

```sh
python3.10 tools/prepare_dependencies.py --dest /tmp/esl-deps/sources --cache /tmp/esl-deps
cmake -S . -B /tmp/esl-foundation-build \
  -DESL_DEPS_DIR=/tmp/esl-deps/sources -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/local/bin/python3.10
cmake --build /tmp/esl-foundation-build -j2
ctest --test-dir /tmp/esl-foundation-build --output-on-failure
/tmp/esl-foundation-build/esl_sim --config config/default.toml \
  --json /tmp/result.json --trace /tmp/events.csv
```

离线环境预先将 `spdlog.tar.gz` 和 `tomlplusplus.tar.gz` 放入缓存目录，再给依赖准备命令添加 `--offline`。不能用任意版本的系统库代替锁定版本；依赖源码被手工修改后应重新准备。

## 构建目标

| 目标 | 内容 |
| --- | --- |
| `esl` | foundation、kernel 公共平台 |
| `esl_chi` | 标准库依赖的 CHI 四通道端口 |
| `esl_chi_vip` | RN/HN/SN 协议核心、规则和内存后端 |
| `esl_chi_vip_transport` | 核心到 CHIPort 的运输适配 |
| `esl_chi_vip_adapter` | RNModel、HNModel、SNModel 生命周期封装 |
| `esl_chi_vip_system` | 应用装配、工作负载、数据参考和统计 |
| `esl_sim` | 命令行程序 |

仅验证协议核心与端口时，可以独立构建，不需要 spdlog、toml++、ModelBase 或全工程配置：

```sh
cmake -S model/chi_vip -B /tmp/esl-vip -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/esl-vip -j2
ctest --test-dir /tmp/esl-vip --output-on-failure
```

## 配置与负载

优先级为内建默认值 → TOML → 显式命令行，无论 `--config` 位于何处。启动时拒绝未知字段、类型错误、非法范围和输出路径冲突。数值命令行参数使用无符号十进制。

| 配置字段 | 默认值 | 命令行覆盖 |
| --- | --- | --- |
| `rn.count` | 2 | `--rn-count` |
| `rn.cache_lines` | 64 | `--cache-lines` |
| `rn.request_slots` | 16 | `--rn-slots` |
| `rn.snoop_slots` | 8 | `--snoop-slots` |
| `rn.pending_operations` | 64 | `--pending-operations` |
| `hn.request_slots` | 16 | `--hn-slots` |
| `sn.request_slots` | 16 | `--sn-slots` |
| `sn.service_latency` | 5 周期 | `--sn-latency` |
| `workload.requests` | 256 | `--requests` |
| `workload.write_percent` | 25 | `--write-percent`，范围 0..100 |
| `workload.seed` | 1 | `--seed` |
| `workload.memory_size` | 1048576 字节 | `--memory-size` |
| `workload.start_address` | 0 | `--start-address` |
| `workload.issue_interval_cycles` | 1 周期 | `--issue-interval` |
| `workload.address_mode` | `sequential` | `--address-mode sequential/random` |
| `clock.period_ps` | 1000 ps | `--period-ps` |
| `platform.max_cycles` | 1000000 | `--max-cycles` |
| `platform.timeout_cycles` | 10000 | `--timeout-cycles` |

`hn.grant_before_retry` 和 `sn.grant_before_retry` 通过 TOML 配置，用于控制协议 credit 与 RetryAck 的次序。详细约束由启动校验及各节点配置定义。

系统固定 HN NodeID=1、SN NodeID=3；RN 依次使用 0、2、4、5……，避开保留给 HN/SN 的编号。顺序模式中，每行由各 RN 依次访问，再推进到下一行；随机模式在配置的合法地址范围内选行。写比例和地址选择由固定种子驱动。

SN 在启动时按行地址与 seed 生成确定性字节内容。应用的 Load 读取完整 64B 行，Store 使用行相对数据和全行或随机部分 BE；所有操作通过 RN 的 Load/Store 前端进入一致性流程。应用按已知的同行因果关系维护独立预期，完成阶段检查 Load 数据；不同地址允许并发。结束阶段对触及的缓存行执行 WriteBack/Evict，等待所有组件和端口排空后核对 SN 字节存储。最终清理不计入用户请求数量。

例如，以较小事务容量运行四 RN 混合负载：

```sh
/tmp/esl-foundation-build/esl_sim --config config/default.toml \
  --rn-count 4 --cache-lines 4 --rn-slots 2 --hn-slots 2 --sn-slots 1 \
  --sn-latency 8 --requests 512 --write-percent 40 --seed 7 \
  --json /tmp/mixed.json --log-dir /tmp/mixed-log
```

`--reverse-registration` 反转模型 Work/Xfer 遍历，用于确定性验证。旧 `--outstanding`、`--hn-entries`、`--ddr-*` 及其旧配置字段不再受理。CHI 通道容量和 flit/credit 延迟仍仅通过 C++ `SetParams` 设置；不提供 `[chi]`、`--depth`、`--flit-delay` 或 `--credit-delay`。

## 日志、Trace 与结果

日志支持全局和模型级别，默认写入工作目录下的 `log/`。模型路径为 `vip.rn0`、`vip.rn1`、`vip.hn`、`vip.sn` 和应用驱动 `vip.workload`，新增 RN 按索引扩展。可通过 `--log-level`、`--log-dir` 覆盖；空目录字符串关闭文件输出。

```toml
[log]
level = "warn"
console = false
directory = "log"
[log.models]
"vip.hn" = "debug"
```

每次运行覆盖相应日志文件，多个实验应使用不同目录。日志携带时间、周期、阶段和模型身份；Trace 不受日志级别过滤。

JSON 的 `model` 为 `chi-vip-gbcore`，包含 `success`、`error`、`diagnostics`、`completed`、`loads`、`stores`、`cycles`、`last_time_ps`、`throughput`、`nodes`、`flits` 和 `configuration_toml`。`nodes` 分别报告 RN/HN/SN 数量；`flits` 报告四通道实际发送数量，包括最终清理消息，不能直接等同于用户 Load/Store 数量。`throughput = completed / cycles`，单位为用户操作/周期；周期数包含启动、清理与排空开销，不是稳态吞吐。

Trace CSV 字段为 `time_ps,cycle,model,channel,txn_id,opcode,data_id`，只记录 flit 实际发送，发送确认稍后交付不改变该时间戳。记录在 `Run()` 结束时按发送周期、NodeID 和节点内稳定序号排序输出；失败运行也尝试输出已收集记录。修改时钟周期只改变 ps 时间，不改变周期行为。性能数值来自当前模型的执行，不代表真实芯片时序精度。

正常结束要求数据检查通过且业务、事件、发送确认和 credit 全部排空。错误返回非零退出码并保存失败结果；配置失败时使用默认或显式 `--json` 路径记录，结果文件写入失败同样报告错误。

## 验证

变更应执行哪些检查，统一按[项目规范的验收矩阵](project_standards.md#7-测试与变更验收)选择；本节维护具体操作方法。纯文档修改核对事实、示例和链接即可。

全工程 CTest 包含公共时间/生命周期/配置/日志/CHI 端口测试、RN/HN/SN 单元测试、真实 CHI 系统测试及 `esl_sim` 应用回归。节点测试覆盖合法 opcode、状态转换、竞争、重试、错误注入和资源回收；系统测试覆盖独立字节参考、协议关联审计、小容量压力及反转遍历。

构建使用 `-Wall -Wextra -Wpedantic -Werror`。格式检查使用 clang-format 18：

```sh
python3.10 tools/check_format.py --formatter clang-format-18
```

也可在 CMake 配置时设置 `CLANG_FORMAT_EXECUTABLE`，再构建 `format-check` 目标。当前 WSL 可用路径为 `/tmp/esl-chi-format/bin/clang-format`。格式检查不修改文件，也不扫描第三方源码。

内存安全检查使用单独构建：

```sh
cmake -S . -B /tmp/esl-vip-full-sanitize \
  -DESL_DEPS_DIR=/tmp/esl-deps/sources \
  -DPython3_EXECUTABLE=/usr/local/bin/python3.10 \
  -DCMAKE_BUILD_TYPE=Debug -DESL_SANITIZERS=ON \
  -DCMAKE_CXX_FLAGS=-fno-pie -DCMAKE_EXE_LINKER_FLAGS=-no-pie
cmake --build /tmp/esl-vip-full-sanitize -j2
ASAN_OPTIONS=detect_leaks=0:handle_segv=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/esl-vip-full-sanitize --output-on-failure
```

该 WSL 组合保留编译插桩的地址和未定义行为检测，关闭 LeakSanitizer 及 ASan 的 SIGSEGV 处理器，不应表述为泄漏检测通过。当前验证针对 G.b 指定事务集合，不代表完整协议或引脚级认证。
