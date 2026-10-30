# CHI 公共接口

## 连接范围

所有数据通路组件通过 `toc::chi::CHIPort` 连接，包括 System/RN、NoC、HA、L3 和 Memory。HA 与 L3 之间使用同一套 CHIPort，不定义专用后端接口。

实现位于 `model/ChiCommon`：

- `include/ChiCommon.h`、`src/ChiCommon.cpp`：CHI opcode、公共常量和四类 flit 结构体。
- `include/CHIPort.h`、`src/CHIPort.cpp`：端口配对、flit 发送/接收和 link credit。

`include` 下直接放头文件，不再增加命名空间子目录。定义主要 C++ 类型的文件与类型同名，类型文件使用 UpperCamelCase。

## Opcode 和 flit

REQ、RSP、SNP、DAT opcode 分别使用 `CHI_REQ_*`、`CHI_RSP_*`、`CHI_SNP_*`、`CHI_DAT_*` 宏，以十六进制表示。当前编码依据本地 AMBA CHI Issue G.b 的 B13.10.18.1 至 B13.10.18.4。

四类 flit 类型为：

- `CHIReqFlit`
- `CHIRspFlit`
- `CHISnpFlit`
- `CHIDatFlit`

普通标量字段最小使用 `uint32_t`。地址使用 `uint64_t`，64-byte 数据按 16 个 `uint32_t` 保存，byte enable 按两个 `uint32_t` 保存。

四种 flit 均重载 `operator<<` 并提供 `toString()`。输出包含结构体内全部字段，数值统一使用带 `0x` 前缀的十六进制格式，供 Trace 和日志直接使用。

## CHIPort 规则

两个 CHIPort 使用 `connect()` 对接，并互相保存对端指针。端口不能连接自身，已连接的端口不能再连接第三个端口。

每个 CHIPort 固定包含一个 REQ、一个 RSP、一个 SNP 和一个 DAT 通道，不再配置通道数量或传入 channel 下标。CHIPort 只保存当前周期和下一周期的接口信号状态，不设置接收 buffer，也不模拟额外链路 delay。

- `sendXXXCredit()`：本端在对应通道驱动一个 credit。credit 只能由连接两侧显式发送，CHIPort 不自动产生或返还 credit；同一通道每周期最多发送一个 credit。
- `getXXXCredit()`：返回本端对应通道当前可用于发送的 credit 数量。
- `hasXXXCredit()`：查询本端对应通道是否至少有一个可用 credit。
- `sendXXXFlit(flit)`：消耗一个本端 credit，并在对应通道驱动一个 flit；无 credit 或未连接时返回失败。同一通道每周期只能发送一个 flit。
- `getXXXFlit(flit)`：取得并消费本端对应通道当前周期的输入 flit；无 flit 时失败。
- `hasXXXFlit()`：查询本端对应通道当前周期是否有待取走的输入 flit。
- `work()`：把本周期驱动的 flit 和 credit 连接到对端，不提交周期状态。
- `xfer()`：在周期边界提交输入 flit 和 credit，使其在下一轮模块 `work()` 中可见。

每个仿真周期先调用两侧模块及端口的 `work()`，再调用两侧模块及端口的 `xfer()`。如果旧输入 flit 尚未消费而新 flit 已在周期边界到达，`xfer()` 抛出运行时错误，并返还新 flit 已经消耗的 credit。link credit 可以累计到 CHI 协议允许的上限 15，但不代表 CHIPort 内部存在同等深度的 buffer。
