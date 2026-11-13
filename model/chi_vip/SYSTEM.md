# CHI G.b RN/HN/SN 连接与系统测试

本目录实现独立的 C++17 CHI G.b `GbCore` 协议层模型。RN、HN、SN
分别放在 `rn/`、`hn/`、`sn/`，每个子目录使用 `include/` 和 `src/`。
公共字段、规则及运输适配位于顶层 `include/`、`src/`。命名空间统一为
`esl::chi_vip`。这是仓库当前唯一的系统演进主线，`system/` 负责应用装配和工作负载；
协议核心及运输层仍可脱离公共平台独立构建。

三个组件的实现架构说明：

- [RN：双引擎、缓存与行协调](rn/ARCHITECTURE.md)
- [HN：目录、事务推进与数据汇聚](hn/ARCHITECTURE.md)
- [SN：请求服务、写缓冲与异步内存](sn/ARCHITECTURE.md)

## 连接关系

```text
RN0 (NodeID 0) -- CHIPort -- HN (NodeID 1) -- CHIPort -- SN (NodeID 3)
RN1 (NodeID 2) -- CHIPort --       |
                               目录/逐行事务
```

三个链路都有一对真实 `CHIPort`。RN 使用 `TransportAdapter`；HN 和 SN
使用 `NodeTransportAdapter`。HN 按对端 NodeID 管理端口集合，新增 RN 时同时
修改 `HNConfig::requester_ids` 和端口映射。SNP 的目的节点保存在发送动作的运输
元信息中，`CHISnpFlit` 没有新增非标准 TgtID。

HN/SN 只从本节点收到的 flit 推进事务，不读取另一节点的缓存或事务表。
系统测试也不利用另一节点内部状态计算协议响应；预期数据由测试独立维护。

## 组件职责和当前策略

| 组件 | 职责 | 关键资源 |
|---|---|---|
| RN-F | 请求、snoop、自动 Load/Store、全相联 LRU、部分有效/脏字节 | 普通请求槽、保留维护槽、snoop 槽 |
| HN-F | 每行受理顺序、目录、snoop 汇聚、上/下游 ID 映射、最新数据保护 | 事务槽、独立 snoop ID、DBID、逐行等待队列 |
| SN-F | 非一致性下游请求、写 DBID/数据组包、异步内存服务 | 请求槽、DBID、到期内存操作 |

HN 没有 LLC。目录只能记录协议可观察的持有者和权限，不能把 RN 的本地
UC→UD 写入当成已知事件；需要最新数据时仍查询潜在持有者。实现采用保守的清洁
策略：获取/失效其它缓存副本时，转交给 HN 的脏数据先写入 SN，再授予请求方清洁
数据或权限。ReadOnce 使用 SnpOnce 获取临时数据快照；原 RN 可以继续承担脏数据
责任，若响应选择 PassDirty，HN 同样会先写回 SN。
部分脏数据按 BE 与内存中其余字节合并。该策略增加下游流量，便于
清楚地维护脏数据责任；它不是 LLC 性能模型。

同一地址的后继事务在前一事务所需 CompAck 或写 DAT 完成之后推进，不同地址
可并行。协议资源释放不等同于输出动作生成或本地 Send 入队成功，适配器另外报告
实际发送。普通请求容量耗尽时，响应、DAT 和 snoop 的完成路径仍能推进。

SN 的字节存储提供可配置服务延迟。`Memory().Initialize()` 用于启动初始化；
`Memory().Read()` 用于排空后的最终检查。正常读写走异步内存操作，读操作和写操作
都需要经历服务完成事件。初始化/最终检查不参与节点之间的协议通信。

## 事务范围

| 上游 RN→HN 请求 | HN 处理 |
|---|---|
| ReadNoSnp | 访问 SN；应用程序必须满足非一致性访问约束 |
| ReadOnce、ReadShared、ReadClean、ReadUnique、ReadNotSharedDirty | 查询持有者、获取数据、按请求授予权限 |
| WriteNoSnpFull/Ptl | 接收写数据并送至 SN |
| WriteUniqueFull/Ptl | 处理其它缓存副本，再合并和写入 SN |
| WriteBackFull/Ptl、WriteCleanFull、WriteEvictFull | 接收 CopyBack 数据，更新目录并维护内存内容 |
| CleanUnique、MakeUnique、Evict、CleanShared、CleanInvalid、MakeInvalid | 权限取得、替换通知、清洁或失效 |

SN 下游只接受 `ReadNoSnp`、`WriteNoSnpFull/Ptl` 和协议 credit 配套事务。
`ReadShared` 等 RN 一致性请求由 HN 处理，不能直接发送至 SN。读数据经过 HN
返回 RN；所有链路使用 64B 行、256-bit DAT 和 DataID 0/2。

配套流程包含 DBID、Comp、CompData、CopyBack/NonCopyBack DAT、CompAck、
RetryAck、PCrdGrant、PCrdReturn，以及有序请求的受理响应。Protocol credit
按授予节点和 PCrdType 处理，收到重试许可的重发使用 AllowRetry=0；link credit
由 `CHIPort` 自行管理。

默认 Normal memory、Non-secure、单时钟、启动复位。RN 支持矩阵及合法缓存
初始状态见 [README.md](README.md)。本实现没有启用 DMT、DCT、DWT、OWO、
分离读响应、Atomic、Exclusive、DVM、Stash、持久化或完整 G.b 引脚层检查。
不在支持集合内的合法功能和违反协议的行为分别报告 `UnsupportedFeature` 和
`ProtocolViolation`，不能解释成成功完成。

## 最小连接示例

```cpp
#include "hn.hpp"
#include "node_transport_adapter.hpp"
#include "rn.hpp"
#include "sn.hpp"
#include "transport_adapter.hpp"

using namespace esl;
using namespace esl::chi_vip;

RNConfig c0, c1;
c0.node_id = 0;
c1.node_id = 2;
RN rn0(c0), rn1(c1);
HN hn;
SN sn;

CHIPort r0("rn0"), r1("rn1"), h0("hn.rn0"), h1("hn.rn1");
CHIPort hs("hn.sn"), s("sn");
r0.Connect(h0);
r1.Connect(h1);
hs.Connect(s);

TransportAdapter a0(rn0, r0), a1(rn1, r1);
NodeTransportAdapter ah(hn, {{0, &h0}, {2, &h1}, {3, &hs}});
NodeTransportAdapter as(sn, s);

Line initial{};
initial[0] = 0x5a;
sn.Memory().Initialize(0, initial);
const auto request = rn0.Load(0, 64);
if (!request.accepted)
    throw std::runtime_error("RN submission was backpressured");

bool complete = false;
for (std::uint64_t cycle = 0; cycle != 10000; ++cycle) {
    // 所有节点先完成 Work，再开始任一节点的 Xfer。
    a0.Work(cycle);
    a1.Work(cycle);
    ah.Work(cycle);
    as.Work(cycle);
    a0.Xfer();
    a1.Xfer();
    ah.Xfer();
    as.Xfer();

    Completion result;
    while (rn0.PollCompletion(result)) {
        if (result.handle == request.handle) {
            complete = true;
            if (result.error != AccessError::kOK || result.data != initial)
                throw std::runtime_error("read mismatch");
        }
    }
    while (rn1.PollCompletion(result)) {}
    if (complete && a0.IsIdle() && a1.IsIdle() && ah.IsIdle() && as.IsIdle())
        break;
    if (cycle == 9999)
        throw std::runtime_error("system did not drain");
}
```

测试驱动必须轮询完成队列，即使已观察到期望数据，也要继续推进到所有适配器和
节点空闲。否则可能遗留 CompAck、实际发送确认或 credit 返回。空闲 RN 可以仍
持有缓存行；若要把最新脏数据与 SN 存储逐字节比较，必须先按协议清洁这些缓存行。

嵌入现有仿真平台时使用 `RNModel`、`HNModel`、`SNModel`，分别公开 `Core()`
和端口访问接口。HN 的接口为 `Port(peer_node_id)`。三类 ModelBase 包装共享相同
Work/Xfer 契约，内核必须保证所有 Work 在所有 Xfer 之前。

## 应用系统入口

[`System`](system/include/system.hpp) 在这些包装之上构造当前 `esl_sim` 系统：默认
RN NodeID 为 0、2，HN 为 1，SN 为 3。增加 RN 时依次使用 4、5……，同时扩展
HN 请求方列表及点到点端口。运行入口接受强类型平台配置，应用负载在内核 Prepare
阶段提交，避免依赖模型遍历顺序。

默认混合负载调用自动 Load/Store，写比例为 25%；顺序地址让各 RN 依次访问同一行
再推进下一行，随机模式由种子决定。应用保留独立字节参考，按同行因果关系检查读取。
启动内存内容由地址和 seed 生成，Load 读取完整 64B，Store 覆盖整行或随机部分字节。
结束前通过 RN Submit 发出所需 WriteBack/Evict 清理触及行，等待核心、适配器和
端口全部空闲后比较 SN 存储。

`System(config, trace_stream)` 接受 `SimConfig` 和可选的输出流指针，`Run()` 返回
`RunResult`，每个系统实例只运行一次。配置错误抛异常；运行中的协议、超时或数据
检查错误返回 `success=false` 并带诊断。`RunResult::JSON()` 输出结果，
`Throughput()` 为用户完成数除以包括清理/排空的总周期数。

命令行、默认 TOML、JSON 和实际发送 Trace 的含义见[构建与运行](../../doc/build_and_run.md)。
本层是应用装配和验证驱动；不在 RN/HN/SN 核心中加入日志或配置解析依赖。

## 配置

| 配置 | 默认值 | 作用 |
|---|---|---|
| HN node_id / slave_id / requester_ids | 1 / 3 / {0, 2} | 拓扑角色及路由 |
| HN request_slots | 16 | 受理事务容量 |
| SN node_id / home_id | 3 / 1 | 下游链路两端 |
| SN request_slots | 16 | 服务中的请求容量 |
| SN service_latency | 5 周期 | 内存服务完成延迟 |
| memory_size | 1 MiB | 字节寻址存储范围 |
| timeout_cycles | 10000 | 测试诊断超时，不是协议固定期限 |
| grant_before_retry | false | 容量重试时覆盖 credit/RetryAck 的不同到达次序 |

HN 初发下游请求使用 `Order=1`（Request Accepted），明确设置 ReturnNID 和
ReturnTxnID；RN→HN 不使用该 Order 编码。HN 对有序请求采用保守的逐请求方门槛，
同一请求方的有序事务完成后才推进其后继；这比最早允许放行的时间更晚。
初发请求使用 AllowRetry=1；AllowRetry=0 必须消耗此前实际发送的 Type 1
protocol credit，本版本不配置初始预授予 credit。HN 默认使用 7-bit NodeID，
事务槽最多 1024，事务槽数乘 RN 数量不得超过 4096 个独立 snoop ID。

RN 默认缓存 64 行、普通事务槽 16、维护槽 1、snoop 上下文 8、待处理操作 64；
链路的 TX/RX 容量和 flit/credit 延迟可在首次 Work 前分别设置。

## 构建和测试

从仓库根目录构建当前项目即可包含这些目标及 `esl_sim` 应用。也可以独立构建协议层：

```sh
cmake -S model/chi_vip -B build/chi_vip -DCMAKE_BUILD_TYPE=Debug
cmake --build build/chi_vip
ctest --test-dir build/chi_vip --output-on-failure
```

`tests/chi_vip_system_test.cpp` 连接真实 RN/HN/SN，没有以测试替身替换 HN 或 SN。
程序 `esl_chi_vip_system_tests` 接受下面的用例参数：

| 参数 | 检查内容 |
|---|---|
| basic | 非一致性整行/部分写、窄读、可选 CompAck、最终字节存储 |
| coherence | 双 RN 共享、静默本地脏写、唯一权限抢占、UDP 部分脏数据合并 |
| opcodes | 独立列出的全部 20 个请求 opcode 及其真实下游效果 |
| flow | HN/SN 分别容量耗尽、两种 credit 配置、有序读、Request Accepted、反转遍历 |
| stress | 小缓存替换、320 个并行批次内操作、独立字节参考、固定种子及正反遍历重现 |
| error | DERR 分拍传播、缓存脏替换后的错误持久性、部分写保留未覆盖错误、覆盖后清除 |

每组使用各通道容量 1、不同 flit 延迟和 credit 延迟，逐拍核对实际传输的节点
路由。独立总线审计从实际传输重建 TxnID、DBID、CompAck、snoop 和协议 credit
关联，并检查重复拍和资源排空，不调用主动模型的事务规则。压力用例只在不同地址间故意重叠操作；同地址访问按已知因果顺序比较，避免
强行假设并发访问必须产生某个唯一赢家。结束时检查核心、适配器、链路和协议资源
全部排空，然后验证 SN 最终数据。

这些集成用例与 RN 的请求/snoop/竞争/负向单元测试互补。通过它们代表指定事务
子集的功能闭环，不代表完整 CHI G.b 兼容性认证或性能时序校准。

HN/SN 独立事件测试还覆盖未实际发送时提前返回数据/CompAck、重复或未知标识、
非法响应状态、缺拍超时，以及 credit 按节点/type 记账而不绑定原 TxnID。全工程还
验证公共平台和应用负载的配置、结果及排空；不再保留旧 AXI 读系统专属测试。
严格编译和 clang-format 18 格式检查均纳入验证；ASan/UBSan 使用单独构建运行，
WSL 环境关闭 LeakSanitizer。
