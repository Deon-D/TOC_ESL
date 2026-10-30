# 编译运行环境

平台在 Windows 的 WSL2 中使用 Debian 12 Bookworm，目标工具链为 GCC/G++ 12.2、CMake/CTest、Ninja，附带 GDB、Git 和 Python 3。Windows 工作区通过 `/mnt/c/Explore/TOC_ESL` 访问。

## 当前进度（2026-10-29）

- 微软 WSL 3.0.3.0 已安装成功，安装程序退出码为 0；安装包的 SHA256 和微软数字签名验证通过。`wsl --version` 确认 Linux 内核版本为 `6.18.54.1-1`。
- Windows 重启已完成，WSL2 发行版 `TOC-ESL-Debian12` 已导入并可运行，Debian 版本为 12.15 Bookworm。
- 默认用户为普通用户 `esl`。实际完成了 CMake 配置、Ninja 编译和 CTest 验收，`gcc12Cpp17Runtime` 为 **1/1 通过**。
- 安装脚本重复执行成功，环境状态为 `ready`。实际工具版本保存在 `top/build/environment/toolVersions.json`，安装状态和日志保存在同一目录。

| 工具 | 已安装版本 |
| --- | --- |
| GCC/G++ | 12.2.0，Debian 包版本 `12.2.0-14+deb12u1` |
| CMake/CTest | 3.25.1 |
| Ninja | 1.11.1 |
| GDB | 13.1 |
| Git | 2.39.5 |
| Python | 3.11.2 |

## Windows 安装

安装材料和日志保存在 `top/build/environment/`，该目录不纳入版本管理。管理员 PowerShell 执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File C:\Explore\TOC_ESL\scrpits\setupWslHost.ps1
```

该脚本安装已校验的 WSL MSI，并启用 `VirtualMachinePlatform`。它不会自动重启；若 `hostState.json` 的状态为 `restartRequired`，保存工作后重启 Windows。`-ExecutionPolicy RemoteSigned` 仅作用于该次 PowerShell 进程，不修改全局脚本策略。本机安装和重启均已完成，日常开发直接使用后文的进入环境命令即可。

安装包来自 [Microsoft WSL 官方发布](https://github.com/microsoft/WSL/releases/tag/3.0.3)。WSL 安装和重启要求见 [Microsoft 安装说明](https://learn.microsoft.com/en-us/windows/wsl/install)。

## 导入 Debian 和安装工具链

Windows 安装完成并执行必要的重启后，在当前 Windows 用户的普通 PowerShell 中运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File C:\Explore\TOC_ESL\scrpits\setupEnvironment.ps1
```

脚本创建名为 `TOC-ESL-Debian12` 的 WSL2 发行版，磁盘文件位于 `%LOCALAPPDATA%\TOC_ESL\WSL\Debian12`。它复用同名发行版，不注销或覆盖已有发行版；若未注册的目标目录非空则停止。

首次导入会校验并解压固定版本的 Debian 镜像，然后以 root 安装工具链、建立普通用户 `esl`，最后以 `esl` 编译运行测试。如果默认用户发生变化，脚本会重启该专用发行版，使 Windows 目录按新用户挂载；不会重启 Windows。软件包安装需要联网。`environmentState.json` 的状态只有在编译和 CTest 均成功后才会写为 `ready`。

本机普通软件包源已切换至清华镜像，`bookworm-security` 保留 Debian 官方源。首次安装中部分安全更新包通过镜像下载，逐个使用官方签名索引提供的 SHA256 校验后放入 APT 缓存。原始源配置备份为 `/etc/apt/debian.sources.esl-original`。镜像用法见 [清华 Debian 镜像说明](https://mirrors.tuna.tsinghua.edu.cn/help/debian/)。

安装脚本将最小镜像自带的容器缓存清理配置移至 `/etc/apt/esl-backups/docker-clean`，保留下载缓存以支持失败重试，并使用 `C.UTF-8` 执行安装过程。

Debian 镜像来自 [Debian Docker 官方镜像构建仓库](https://github.com/debuerreotype/docker-debian-artifacts/tree/69faaf492e77aff83e4c7fc401017c9d25ecde24/bookworm)，固定提交为 `69faaf492e77aff83e4c7fc401017c9d25ecde24`。压缩文件 SHA256 为：

```text
a4aa977c0d0eb2336dad68941e458308ebf32a717c19ea72e475b9eda8035506
```

该镜像是最小 Debian 用户空间，工具链通过 Bookworm 软件源安装；安装脚本和 CMake 检查 GCC 版本必须为 12.2.x。导入方式依据 [Microsoft 自定义发行版说明](https://learn.microsoft.com/en-us/windows/wsl/use-custom-distro)。

## 日常使用与验收

进入编译环境：

```powershell
wsl -d TOC-ESL-Debian12 -u esl --cd /mnt/c/Explore/TOC_ESL
```

在 Debian 中重复检查工具链：

```bash
bash scrpits/checkToolchain.sh
```

检查项目位于 `top/tests/toolchainSmoke`，构建输出位于 `top/build/toolchainSmoke`。它验证 GCC 12.2、C++17 标准库、线程链接和运行，并通过 CTest 检查程序退出码。该检查只验收编译环境；平台主工程 `top/CMakeLists.txt` 将在 M0 后续开发中建立。

普通用户 `esl` 没有设置登录密码；维护软件包时可显式进入 root：

```powershell
wsl -d TOC-ESL-Debian12 -u root
```

不要在 Windows 和 Linux 之间复用同一个 CMake 构建缓存。项目的 Linux 构建和运行产物统一放在 `top/build`、`top/run`。
