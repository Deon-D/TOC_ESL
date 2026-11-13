# CHI 接口函数说明

CHI 模块为 RN、HN、SN 和后续 Fabric 提供 CA（周期精确）级连接。端口仅凭名字构造，由例化端口的模块调用 `Work()` 和 `Xfer()`。接口自行维护周期计数、延迟队列和 credit，不依赖 ModelContext、平台时钟、日志、仿真内核或配置文件。

实现使用 C++17。类型和函数位于 `esl` 命名空间，opcode 为全局宏。CMake 的独立库目标为 `esl_chi`，包含路径为本目录的 `include/`，实现源文件为 `src/chi_port.cpp`；只依赖 C++ 标准库。项目规范见 [C++ 代码规范](../../doc/cpp_style.md)。

## 文件与类型

| 文件 | 内容 |
| --- | --- |
| [include/chi_common.h](include/chi_common.h) | Opcode 宏、四通道 flit 及复位和打印函数 |
| [include/chi_channel.h](include/chi_channel.h) | CHIChannelParams、ChannelStats、CHIChannelBase、CHIChannel |
| [include/chi_port.h](include/chi_port.h) | CHIPortParams、CHIPort 及通道分派接口 |
| [src/chi_port.cpp](src/chi_port.cpp) | 端口构造、连接、参数设置和 Work/Xfer 调度 |

`CHIPort` 的通道模板参数为 `CHIReqFlit`、`CHIRspFlit`、`CHISnpFlit`、`CHIDatFlit`。端口对称，每个通道都具有独立的 TX/RX FIFO，不限制节点角色；路由和事务合法性由组件检查。当前端口的 DAT 宽度为 256 bit。

## 通道参数

`CHIChannelParams` 是普通 C++ 参数结构，通过外部代码设置。四通道参数分别保存在 `CHIPortParams::req/rsp/snp/dat` 中，两端、各通道均可不同。

| 字段 | 默认值 | 含义与限制 |
| --- | --- | --- |
| `tx_depth` | `4` | 当前通道本端 TX FIFO 深度，必须大于零，可超过 15；只约束本端待发送队列 |
| `rx_depth` | `4` | 当前通道本端 RX 容量，范围为 1..15；RX 中和到达本端的在途 flit 合计不能超过此值，同时决定授予对端的 credit 总量 |
| `flit_delay_cycles` | `1` | 本端 Xfer 发出的 flit 到达对端的延迟，单位为调用周期，必须大于零 |
| `credit_delay_cycles` | `1` | 本端 Xfer 发出的 credit 到达对端的延迟，单位为调用周期，必须大于零 |

`CHIChannelParams::Validate() const` 检查上述范围，非法时抛出 `std::runtime_error`。发送端可获得的 credit 总数对应对端同一通道的 RX 深度。数据延迟由数据发送端的参数决定，credit 延迟由 credit 发出端的参数决定。

原 `depth` 字段已拆为 `tx_depth`、`rx_depth`。采用列表初始化时，四项顺序为 `{tx_depth, rx_depth, flit_delay_cycles, credit_delay_cycles}`；原三项写法需要同步修改。也可默认构造参数后分别赋值各字段。

参数只允许在连接两端首次 Work 之前设置。接口不读取 TOML、命令行或任何配置文件，不提供 `[chi]`、`--depth`、`--flit-delay`、`--credit-delay` 入口。装配代码对每个节点拥有的端口调用 `SetParams`；RN/SN 包装通过 `Port()` 获取端口，HN 通过 `Port(peer_node_id)` 按对端选择。示例见 [RN/HN/SN 连接](../chi_vip/SYSTEM.md)。

## 错误处理

模块内部的 `chi_detail::Require(bool condition, const char* message)` 和 `chi_detail::Require(bool condition, const std::string& name, const char* message)` 是普通 C++17 检查函数。条件为假时抛出 `std::runtime_error`，带名字的重载仅在失败分支拼接 `name: message`；正常周期检查不构造错误字符串。它们不依赖平台的 `PlatformError`。在本项目模型中，异常由仿真内核捕获并补充模型和阶段信息。

队列满、队列空属于正常背压，分别通过 Send、Recv 返回 `false` 表示。未绑定、错误调用顺序、运行中修改参数、周期溢出或 credit 不守恒属于异常，不作为背压重试。

## Flit 函数

下表中的 `Flit` 代表 `CHIReqFlit`、`CHIRspFlit`、`CHISnpFlit` 或 `CHIDatFlitT<DataWidth>`。它们是按值复制的载荷结构，字段均使用 `flit_` 前缀，标量和数据字至少为 `uint32_t`。

| 函数 | 输入与返回 | 行为 |
| --- | --- | --- |
| `void reset() noexcept` | 无参数、无返回值 | 将全部字段恢复为默认值，包括可选控制字段、DAT 数据数组和掩码；当前默认值均为零 |
| `std::string to_string() const` | 返回控制信息字符串 | 使用独立字符串流生成摘要，数值采用带 `0x` 前缀的十六进制；不修改 flit |
| `std::ostream& operator<<(std::ostream& stream, const Flit& flit)` | 输入输出流和 flit，返回原流引用 | 将 `to_string()` 的结果写入流，支持连续输出；保留调用者原有的进制、精度和填充设置 |

REQ 摘要包含 opcode、源和目标节点、事务号、地址、长度及请求控制信息；RSP 包含响应状态、错误码和 DBID 等；SNP 包含转发信息、地址和 snoop 控制信息；DAT 包含响应状态、DataID、DBID、字节使能和数据检查掩码等。`to_string()` 和 `operator<<` 不展开打印 DAT 的 `flit_data` 数组。

`CHIDatFlitT<128>`、`CHIDatFlitT<256>`、`CHIDatFlitT<512>` 支持三种数据宽度，其他宽度会触发编译期检查。`CHIDatFlit` 是 256-bit 类型别名，当前 `CHIPort` 仅接入该宽度。`kDataWidth`、`kDataBytes`、`kDataWords` 分别表示位数、字节数和 32-bit 数据字数量。

默认构造或 `reset()` 后的 flit 不代表有效事务。发送端必须显式设置 opcode、路由和有效字段；DAT 的 `flit_be` 默认不使能任何字节。Flit 类型不进行字段位宽截断、事务合法性检查或位流序列化。Opcode 宏的命名形如 `CHI_REQ_Opcode_ReadNoSnp (0x04)`，不是函数。

## CHIPort 函数

### 构造与连接

| 函数 | 行为与约束 |
| --- | --- |
| `explicit CHIPort(std::string name)` | 创建四个默认通道，初始周期为零、发送 credit 为零；名字不能为空 |
| `~CHIPort()` | 清除对端指向本端的指针，不销毁对端、不自动排空队列 |
| `void Connect(CHIPort& peer)` | 一次调用建立双向连接，互持非拥有指针；禁止自连接、重复连接、相同名字和任一端已开始 Work 后连接 |
| `CHIPort* Peer() const` | 返回借用的对端指针，未连接或对端已析构时返回 nullptr |
| `void CheckBound() const` | 检查对端存在且反向指针一致，不正确时抛出异常 |

组件拥有端口，两个端口必须覆盖整个传输运行期。端口禁止复制，不提供运行中复位、断开或重连接口，也不注册到内核。接口按单线程同步周期使用，不提供跨时钟域或并发线程同步能力。

### 参数设置与查询

| 函数 | 行为与返回值 |
| --- | --- |
| `void SetParams(const CHIPortParams& params)` | 先校验四通道参数，再整体复制；参数校验失败时不修改任何通道 |
| `void SetParams<Flit>(const CHIChannelParams& params)` | 只设置所选通道，保留其他通道的参数 |
| `CHIPortParams Params() const` | 返回全部通道参数的副本 |
| `const CHIChannelParams& Params<Flit>() const` | 返回所选通道参数的只读引用 |

SetParams 可以在 Connect 前或后调用，但一旦连接的任一端开始 Work，两端参数均不可再修改。修改 Params 返回的整体副本不会影响端口，必须通过 SetParams 提交。

### 发送与接收

以下收发操作只允许在本端 `Work()` 返回之后、`Xfer()` 调用之前。Send 和 Recv 可从实参推导 Flit 类型，其他模板接口需显式指定类型。

| 函数 | 返回值与状态变化 |
| --- | --- |
| `bool CanSend<Flit>() const` | 本端 TX 未满时为 true，不要求已有对端 credit |
| `bool Send<Flit>(const Flit& flit)` | 复制 flit 入本端 TX；成功返回 true，满时返回 false、不改变队列，并累计一次 send_rejects |
| `bool CanRecv<Flit>() const` | 本端 RX 非空时为 true；在途数据不计入 |
| `bool Recv<Flit>(Flit& flit)` | 复制 RX 队首到输出并出队，累积一个待返还 credit；空时返回 false，输出不变 |
| `const Flit* Peek<Flit>() const` | 借用 RX 队首，空时为 nullptr；不出队、不返还 credit，指针不能跨 Recv 或下一次 Work 保留 |

Send 成功仅表示本地队列已接管副本，调用方可修改原对象，不能因为尚未传输就在下一周期重复提交。同一 Work 可多次入队或接收，但实际链路每通道、每方向每周期最多发送一个 flit。

### 周期推进与发送结果

| 函数 | 所属模块的调用位置 | 处理内容 |
| --- | --- | --- |
| `void Work()` | 模块 Work 开头，任何收发操作之前 | 检查连接和周期同步；到期 flit 移入 RX，到期 credit 放入待生效计数，清除上周期发送快照 |
| `void Xfer()` | 模块 Xfer 内，每周期一次 | 根据本端 TX 和已有 credit 发送至对端延迟队列；本端出队并扣减 credit；发出一个初始或归还 credit；最后使新到 credit 生效并推进本端周期 |
| `const Flit* Transmitted<Flit>() const` | 本端 Xfer 返回之后，下一次 Work 之前 | 返回刚完成的发送快照；本周期未发送时为 nullptr；Xfer 之前查询会抛出异常 |

每个模块在自己的 Work/Xfer 中推进其拥有的全部端口。所有模块完成本周期 Work 后，才开始任何模块的 Xfer。即使当前没有事务，端口仍须每周期调用一次 Work 和一次 Xfer，以处理 credit 和在途数据。

端口检查重复 Work、缺少 Work 的 Xfer，以及连接两端周期错位。端口内部周期从零开始，每完成一次 Xfer 加一，不读取或推进平台 Clock。仅靠局部检查不能替代调用方对全体模块的阶段排序。

若在周期 n 的 Xfer 发出 flit，数据延迟为 d，对端最早在 n+d 的 Work 接收。若在周期 n 的 Xfer 发出 credit，credit 延迟为 c，对端在 n+c 的 Work 收到，至该周期 Xfer 完成后才可用，因此最早在 n+c+1 的 Xfer 支持发送。

| 默认参数下的周期 | 关键动作 |
| --- | --- |
| 0 | A 在 Work 入队请求；B 在 Xfer 发出第一个初始 credit；A 无 credit，暂不发送 |
| 1 | A 在 Work 收到 credit，本周期 Xfer 之后才生效 |
| 2 | A 在 Xfer 发送请求 |
| 3 | B 在 Work 收到并可 Recv 请求，在 Xfer 发出归还 credit |
| 4 | A 在 Work 收到归还 credit，Xfer 后生效，最早周期 5 使用 |

初始 credit 按本端 RX 深度逐个授予，每通道每周期最多一个。归还 credit 优先于尚未授予的初始 credit。该过程由计数和延迟队列完成，组件不需要发送 LCrdReturn flit。Xfer 不直接改变对端当前 RX 或可用 credit，因此反转模块遍历顺序不产生同周期穿透。

### 状态与统计

| 函数 | 返回值含义 |
| --- | --- |
| `unsigned Credits<Flit>() const` | 当前可用的发送 credit，不包含本周期 Work 刚收到、尚未生效的 credit |
| `std::size_t TxSize<Flit>() const` | 本端 TX FIFO 数量，实际发送后才出队 |
| `std::size_t RxSize<Flit>() const` | 本端 RX FIFO 数量，不包含在途数据 |
| `const ChannelStats& Stats<Flit>() const` | 本端通道统计的只读引用，需要快照时复制 |
| `bool IsIdle() const` | 四通道数据、credit 和队列全部排空且 credit 归齐时为 true；未连接时为 false |
| `std::string DescribeState() const` | 端口名字、对端名字、本端周期、阶段，以及四通道队列和 credit 状态 |

这些查询读取调用时状态，不受 Work/Xfer 阶段限制。模块的 IsIdle 应包含其端口的 IsIdle，确保模块业务结束后继续归还 credit。日志和 Trace 由模块按需记录，接口本身不访问日志服务。

| ChannelStats 字段 | 含义 |
| --- | --- |
| `tx_flits` | 本端实际发送数量，在 Xfer 更新；Send 入队不计入 |
| `rx_flits` | 到达本端 RX 的数量，在 Work 更新；不包含在途数据，不等同于模块已 Recv 的数量 |
| `send_rejects` | 因本端 TX 满而失败的 Send 调用次数；同周期多次失败分别计数，CanSend 查询和错误阶段调用不计数 |
| `credit_stalls` | TX 非空但缺少 credit 的周期数，在 Xfer 更新 |
| `tx_peak` | 本端 TX 历史峰值，成功 Send 时更新 |
| `rx_peak` | 本端 RX 历史峰值，到达本端 Work 时更新，在 Recv 消费之前采样，不包含在途数据 |

所有统计只由所属端点更新。`tx_flits` 表示本端实际发送数，`rx_peak` 表示本端接收峰值。系统若需汇总一条传输方向，应分别读取发送端和接收端的 Stats，避免把对端统计归入本端。当前应用的 JSON `flits` 汇总四通道实际发送流量，详细端点统计仍通过 `Stats` 访问。

## 通道类与内部辅助函数

`CHIChannelBase` 提供虚析构，以及纯虚 `Work(std::uint64_t cycle)`、`Xfer(std::uint64_t cycle)`、`IsIdle() const`、`DescribeState() const`。周期参数由所属 CHIPort 提供，组件调用无参数的 CHIPort::Work/Xfer。

`CHIChannel<Flit>` 通过 `explicit CHIChannel(std::string name)` 构造，实现基础阶段函数，以及与端口同名的 Send、Recv、CanSend、CanRecv、Peek、Transmitted、Credits、TxSize、RxSize、Stats、Params。Flit 类型在通道实例化时固定。通道不提供公开连接接口，绑定和参数修改由 CHIPort 管理。

| 内部函数 | 职责 |
| --- | --- |
| `CHIPort::CheckSetup() const` | 确认本端及已连接对端均尚未开始 Work |
| `CHIPort::Channel<Flit>()` 及 const 重载 | 返回对应通道引用；不支持的类型在编译期拒绝 |
| `CHIChannel::SetParams(const CHIChannelParams&)` | 接收端口已校验的参数，并按 rx_depth 初始化待授予 credit |
| `CHIChannel::CheckWork() const` | 检查已连接且处于本端 Work 与 Xfer 之间 |
| `CHIChannel::Due(std::uint64_t cycle, std::uint64_t delay) const` | 计算到期周期，检查无符号加法溢出 |
| `CHIChannel::CheckCredits() const` | 检查 FIFO 上界和当前 RX 方向的 credit 守恒 |

守恒关系为：本端待授予 credit + 本端 RX 数据 + 到达本端的在途数据 + 本端待归还 credit + 对端可用 credit + 对端待生效 credit + 到达对端的在途 credit = 本端 RX 深度。

## 模块调用示例

下面的最小示例只演示一个 REQ 的传输，不生成完整读事务响应。RN 和 HN 各自拥有并推进端口。实际 Fabric 若有多个端口，应在其 Work 开头先调用所有端口 Work，再进行转发，最后在其 Xfer 中调用所有端口 Xfer。

```cpp
#include "chi_port.h"

struct ExampleRN {
    esl::CHIPort chi{"rn.chi"};
    bool queued = false;

    void Work() {
        chi.Work();
        if (!queued) {
            esl::CHIReqFlit request;
            request.flit_opcode = CHI_REQ_Opcode_ReadNoSnp;
            request.flit_tgt = 1;
            request.flit_txn = 1;
            request.flit_address = 0x1000;
            request.flit_size = 6;
            queued = chi.Send(request);
        }
    }
    void Xfer() {
        chi.Xfer();
        // Query chi.Transmitted<esl::CHIReqFlit>() here to record actual sends.
    }
};

struct ExampleHN {
    esl::CHIPort chi{"hn.chi"};
    bool received = false;

    void Work() {
        chi.Work();
        esl::CHIReqFlit request;
        if (chi.Recv(request))
            received = request.flit_txn == 1;
    }
    void Xfer() {
        chi.Xfer();
    }
};

int main() {
    ExampleRN rn;
    ExampleHN hn;
    // Parameters: TX depth, RX depth, flit delay, credit delay.
    rn.chi.SetParams<esl::CHIReqFlit>({16, 2, 3, 1});
    hn.chi.SetParams<esl::CHIReqFlit>({4, 3, 1, 2});
    rn.chi.Connect(hn.chi);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        rn.Work();
        hn.Work();
        rn.Xfer();
        hn.Xfer();
        if (rn.queued && hn.received && rn.chi.IsIdle() && hn.chi.IsIdle())
            return 0;
    }
    return 1;
}
```

源码级接入只需要包含 `chi_port.h` 并编译 `src/chi_port.cpp`，无需平台代码或第三方库。项目内完整装配见 [System](../chi_vip/system/src/system.cpp)；RN/HN/SN 的适配层在各自 Work/Xfer 中推进端口和核心。核心在 Work 计算工作状态，Xfer 后 Commit 发布；实际发送确认于下一周期交付。

独立验证见 [chi_port_test.cpp](../../tests/chi_port_test.cpp)，覆盖无平台驱动、四通道不同参数、双向背压、精确延迟、调用次序、credit 守恒、TX/RX 不同深度、本端统计和 Fabric 两跳转发。Flit 验证见 [chi_common_test.cpp](../../tests/chi_common_test.cpp)。
