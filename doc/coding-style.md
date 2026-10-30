# 编码与命名规范

项目自 M0 起采用小驼峰（lowerCamelCase）作为默认命名风格。变量、函数、构建目标、测试名、脚本和项目内部目录遵守该规则；定义主要 C++ 类型的源文件和头文件采用与类型相同的 UpperCamelCase 名称。

| 对象 | 规则 | 示例 |
| --- | --- | --- |
| 变量、参数、函数、方法 | lowerCamelCase | `requestCount`、`scheduleEvent()` |
| 命名空间 | 统一以 `toc` 为根，子命名空间使用 lowerCamelCase | `toc::coherence`、`toc::toolchainSmoke` |
| CMake target、CTest 名 | lowerCamelCase | `eslCommon`、`gcc12Cpp17Runtime` |
| C++ 类型文件 | 与文件中的主要类、结构体或枚举同名，使用 UpperCamelCase | `SimKernel.h`、`SimKernel.cpp` |
| 脚本、非类型文件、内部目录 | lowerCamelCase | `runRegression.py`、`moduleInterface/` |
| 类、结构体、枚举、类型别名 | UpperCamelCase | `SimKernel`、`ChiRequest` |
| 编译期常量 | lowerCamelCase | `maxOutstanding` |
| 宏、include guard | UPPER_SNAKE_CASE | `ESL_COMMON_MESSAGE_H` |

缩写作为普通单词处理：变量使用 `transactionId`、`homeAgent`，类型及对应文件使用 `TransactionId`、`HomeAgent`、`HomeAgent.h`。协议规范规定的名称保留原拼写，例如 `REQ`、`RSP`、`SNP`、`DAT`、`RetryAck` 和缓存状态 `UC`、`UD`、`SC`、`SD`。

所有项目自有的 C++ 符号都放在 `toc` 根命名空间下，再按职责划分子命名空间，例如 `toc::coherence`、`toc::memory`。禁止新建与 `toc` 平级的项目命名空间。语言规定必须位于全局作用域的 `main()`、第三方扩展点和标准明确要求的特化除外。

公共头文件直接放在对应组件的 `include` 目录，不增加 `toc/chi` 等多级路径。代码中使用 `using namespace std;`，避免为标准库类型重复添加 `std::` 前缀。

以下名称因工具约定或已确定的工程边界保留：`CMakeLists.txt`、根目录 `model`、`top`、`benchmark`、`doc`、`scrpits`，以及现有模型组件目录 `SoC`、`Common`、`ChiCommon`、`RN`、`HA`、`L3`、`NoC`、`Memory`、`MemoryEngine`。后续不要仅为风格修改这些路径，以免破坏脚本和模型边界。

C++ 私有成员与公共成员使用相同的小驼峰规则，不增加前后缀；通过作用域和类型表达职责。布尔量使用能直接读出条件的名称，如 `isReady`、`hasCredit`、`canAccept()`。避免把类型信息编码进变量名。
