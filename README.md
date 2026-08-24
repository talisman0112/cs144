# Minnow TCP/IP Stack

Minnow 是一个以 C++ 实现的教学型 TCP/IP 协议栈项目，源自 Stanford CS144 Lab。项目从字节流开始，逐步实现 TCP 的序列号、重组、可靠发送与接收，并通过 IPv4/TUN 适配器连接到用户态网络实验环境。

当前仓库的 `main` 分支已包含 Lab 0 至 Checkpoint 3 的 TCP 核心，以及 NetworkInterface 和 Router 的第一轮恢复实现。`tcp_eth_udp`、`fun_router` 等后续应用仍未恢复，详见[项目状态](#项目状态)。

## 目录

- [项目目标](#项目目标)
- [整体架构](#整体架构)
- [数据流程](#数据流程)
- [目录说明](#目录说明)
- [核心模块](#核心模块)
- [当前状态](#当前状态)
- [构建环境](#构建环境)
- [构建与测试](#构建与测试)
- [应用程序](#应用程序)
- [开发流程](#开发流程)
- [排查构建问题](#排查构建问题)
- [优化路线](#优化路线)

## 项目目标

项目按以下层次构建一个可运行的网络协议栈：

1. 用 `ByteStream` 提供有容量限制的可靠字节流。
2. 用 `Reassembler` 将乱序、重叠、分片的数据恢复为连续字节流。
3. 用 `Wrap32` 在 32 位 TCP 序列号与绝对序列号之间转换。
4. 用 `TCPReceiver` 实现 SYN、ACK、FIN、窗口通告和接收端重组。
5. 用 `TCPSender` 实现分段、发送窗口、可靠重传和 RST 处理。
6. 用 `TCPPeer` 协调 sender 与 receiver，形成双向 TCP 端点。
7. 通过 TCP segment、IPv4 datagram 和 TUN 设备适配真实的用户态网络实验。

## 整体架构

```text
+----------------------+      +----------------------+      +----------------------+
| 应用程序             | ---> | Socket API           | ---> | TCPPeer              |
| webget / tcp_native  |      | TCPSocket            |      | 双向连接协调         |
| tcp_ipv4             |      | TCPMinnowSocket      |      |                      |
+----------------------+      +----------------------+      +----------+-----------+
                                                                         |
                                                        +----------------+----------------+
                                                        |                                 |
                                                        v                                 v
                                           +----------------------+         +----------------------+
                                           | TCPSender            |         | TCPReceiver          |
                                           | 发送窗口 / ACK       |         | SYN / ACK / FIN       |
                                           | RTO 重传             |         | 接收窗口             |
                                           +----------+-----------+         +----------+-----------+
                                                      |                                 |
                                                      v                                 v
                                           +----------------------+         +----------------------+
                                           | TCP segment          |         | Reassembler          |
                                           | 序列化 / 解析        |         | 乱序 / 重叠 / FIN    |
                                           | checksum             |         +----------+-----------+
                                           +----------+-----------+                    |
                                                      |                               v
                                                      |                    +----------------------+
                                                      |                    | ByteStream           |
                                                      |                    | 有界字节流           |
                                                      |                    +----------------------+
                                                      v
                                           +----------------------+
                                           | IPv4 adapter         |
                                           | TCPOverIPv4Adapter   |
                                           +----------+-----------+
                                                      |
                                                      v
                                           +----------------------+
                                           | TUN / FD adapter     |
                                           | 用户态网络设备       |
                                           +----------+-----------+
                                                      |
                                                      v
                                           +----------------------+
                                           | 外部网络 / 测试对端  |
                                           +----------------------+
```

## 数据流程

### 发送流程

```text
+----------------+     +----------------+     +----------------+     +----------------+
| 应用           |     | ByteStream     |     | TCPSender      |     | TCP/IPv4/TUN   |
+-------+--------+     +-------+--------+     +-------+--------+     +-------+--------+
        |                      |                      |                      |
        | write(data)          |                      |                      |
        +--------------------->|                      |                      |
        |                      |                      |                      |
        | push()               |                      |                      |
        +------------------------------------------->|                      |
                               |                      |                      |
                               |              peek()  |                      |
                               |<---------------------+                      |
                               |                      |                      |
                               |                      | 组装 SYN/payload/FIN |
                               |                      |                      |
                               |                      | 发送 segment         |
                               |                      +--------------------->|
                               |                      | 记录 outstanding/RTO |
                               |                      |                      |
                               |                      |<---------------------+
                               |                      | 返回 ACK + window    |
                               |                      |                      |
                               |                      | 清理已确认序列号     |
                               |                      |                      |
                               |                      | (RTO 到期)           |
                               |                      +--------------------->|
                               |                      | 重传最早未确认段     |
```

### 接收流程

```text
+----------------------+      +----------------------+      +----------------------+
| 收到 TCP segment     | ---> | 解析字段与校验和     | ---> | RST?                 |
+----------------------+      +----------------------+      +----------+-----------+
                                                                         |
                                           +-----------------------------+-----------------------------+
                                           |                                                           |
                                           | 是                                                        | 否
                                           v                                                           v
                                +----------------------+                                  +----------------------+
                                | 设置流错误           |                                  | 已收到 SYN?           |
                                +----------+-----------+                                  +----------+-----------+
                                           |                                                         |
                                           |                                                         v
                                           |                                             +----------------------+
                                           |                                             | Wrap32 -> absolute   |
                                           |                                             | sequence             |
                                           |                                             +----------+-----------+
                                           |                                                        |
                                           |                                                        v
                                           |                                             +----------------------+
                                           |                                             | Reassembler.insert   |
                                           |                                             +----------+-----------+
                                           |                                                        |
                                           |                                                        v
                                           |                                             +----------------------+
                                           |                                             | ByteStream.push      |
                                           |                                             +----------+-----------+
                                           |                                                        |
                                           |                                                        v
                                           |                                             +----------------------+
                                           |                                             | FIN 已连续到达?      |
                                           |                                             +----------+-----------+
                                           |                                                        |
                                           +--------------------------------------------------------+
                                                                    |
                                                                    v
                                                         +----------------------+
                                                         | FIN?                 |
                                                         +----------+-----------+
                                                                    |
                                           +------------------------+------------------------+
                                           |                                                 |
                                           | 是                                              | 否
                                           v                                                 v
                                +----------------------+                         +----------------------+
                                | 关闭输入流           |                         | 保持输入流开放       |
                                +----------+-----------+                         +----------+-----------+
                                           |                                                 |
                                           +------------------------+------------------------+
                                                                    v
                                                         +----------------------+
                                                         | 生成 ACK 与窗口      |
                                                         +----------------------+
```

### TCP 连接中的核心状态

```text
                         +----------------------+
                         | Initial              |
                         +----------+-----------+
                                    |
                                    | sender.push() / SYN
                                    v
                         +----------------------+
                         | SynSent              |
                         +----------+-----------+
                                    |
                                    | 收到 SYN 的 ACK
                                    v
                         +----------------------+
                         | Established          |
                         +-----+------------+---+
                               |            |
                 本地 close()  |            | 对端 FIN 连续到达
                               v            v
                         +----------+  +----------+
                         | FinSent  |  | Closed   |
                         +----+-----+  +----+-----+
                              |              |
                              | FIN 被 ACK   | 结束
                              v              v
                         +----------------------+
                         | Closed               |
                         +----------------------+

  Initial / SynSent / Established / FinSent
        |
        | 本地 stream error 或收到 RST
        v
  +----------------------+
  | Reset                |
  +----------------------+
```

## 目录说明

| 路径 | 内容 |
| --- | --- |
| `src/` | ByteStream、Reassembler、Wrap32、TCP sender/receiver 等核心实现 |
| `util/` | 地址、文件描述符、IPv4、TCP segment、TUN 和 socket 适配器 |
| `apps/` | `webget`、`tcp_native`、`tcp_ipv4` 和 `ip_raw` |
| `tests/` | 单元测试、TCP 测试 harness、性能测试和网络层测试源码 |
| `etc/` | CMake 构建选项、测试注册和静态扫描配置 |
| `writeups/` | 各 checkpoint 的实验记录 |
| `scripts/` | 并行构建和 TUN 辅助脚本 |
| `build/` | CMake 生成的构建目录，不应手工提交 |

## 核心模块

### ByteStream

`ByteStream` 是有固定容量的单向字节流，提供：

- 写入端 `Writer`
- 读取端 `Reader`
- available capacity 查询
- bytes written/read 查询
- EOF 和 close 状态
- stream error 状态

它同时被 sender 的输入端和 receiver 的输出端使用。

### Reassembler

`Reassembler` 接收带绝对流索引的数据片段，负责：

- 乱序片段缓存
- 重叠数据去重
- 按接收窗口裁剪
- 将连续数据推入 `ByteStream`
- 等待数据补齐后处理 FIN

### Wrap32

TCP 在线上传输的是 32 位循环序列号，而内部重组和窗口计算使用绝对序列号。`Wrap32::wrap` 和 `Wrap32::unwrap` 负责两种表示之间的转换，`checkpoint` 用于选择距离最近的绝对序列号。

### TCPSender

sender 负责：

- 首次发送 SYN
- 按对端 advertised window 分段发送
- 发送 FIN
- 跟踪 outstanding segment 和 bytes in flight
- 接收 ACK 并推进确认点
- RTO 超时重传
- 处理零窗口探测
- 本地 stream error 时发送 RST

### TCPReceiver

receiver 负责：

- 记录 SYN 和 ISN
- 将 TCP 序列号转换为流索引
- 将数据交给 Reassembler
- 生成累计 ACK
- 根据 ByteStream 剩余容量生成窗口
- 在 FIN 连续到达后关闭接收流

### TCPPeer 与适配器

`TCPPeer` 将 sender 与 receiver 组合成双向 TCP 端点。`tcp_segment.cc` 将内部消息编码为 TCP segment，`TCPOverIPv4Adapter` 再将 segment 放入 IPv4 datagram，通过 TUN 或文件描述符适配器收发。

## 当前状态

### 已具备

- ByteStream 基础功能
- Reassembler 基础功能
- Wrap32
- TCPReceiver
- TCPSender
- TCP segment 序列化、解析和校验和
- TCPPeer
- TCP over IPv4/TUN 适配器
- `webget`、`tcp_native`、`tcp_ipv4` 应用入口
- Checkpoint 1～3 对应的大部分测试源码

### 当前缺口

以下模块仍不在当前 `main` 分支中：

- `apps/tcp_eth_udp.cc`
- `apps/fun_router.cc`
- Checkpoint 7 的完整应用集成

因此，当前版本更准确地说是“TCP 核心 + 网络层实验版本”，还不是完整的 CS144 全部协议栈。

### 已知风险

- Windows 环境下 CMake/Visual Studio SDK 可能阻止编译。
- 当前构建配置包含 Unix 风格编译参数，MSVC 可能无法接受其中部分参数。
- sender 的 partial ACK 支持仍需要加强。
- TCPPeer 是面向实验的简化状态协调器，不是完整生产级 TCP 状态机。
- NetworkInterface 和 Router 已恢复源码和测试注册，但尚未在当前 Windows 工具链中完成编译验证。

## 构建环境

推荐环境：

- 支持 C++23 的编译器
- CMake 3.24.2 或更新版本
- Ninja 或 Unix Makefiles
- clang-format、clang-tidy（可选）
- Linux 下运行 `tcp_ipv4` 需要 TUN/TAP 支持和相应权限

Windows 下建议使用 Visual Studio 2022 的 x64 Native Tools 命令行，并确认已安装对应 Windows SDK。若只是验证协议核心，也可以使用 WSL 或 Linux 环境生成独立的构建目录。

## 构建与测试

### 初次配置

```bash
cmake -S . -B build
```

使用 Ninja 时：

```bash
cmake -S . -B build -G Ninja
```

### 编译

```bash
cmake --build build
```

Visual Studio 多配置生成器可以指定配置：

```powershell
cmake --build build --config Debug
```

### 运行全部已注册测试

```bash
cmake --build build --target test
```

也可以直接使用 CTest：

```bash
ctest --test-dir build --output-on-failure
```

### 按 checkpoint 测试

```bash
cmake --build build --target check1
cmake --build build --target check2
cmake --build build --target check3
```

`check5` 和 `check6` 已注册 NetworkInterface、Router 测试；在当前 Windows 工具链修复后可以直接执行。

### 代码质量工具

```bash
cmake --build build --target format
cmake --build build --target tidy
cmake --build build --target speed
```

这些 target 是否可用取决于本机是否安装了对应工具以及当前 CMake generator 的支持情况。

## 应用程序

### `webget`

通过系统 TCP socket 发送 HTTP GET 请求并输出响应：

```bash
./build/apps/webget stanford.edu /class/cs144
```

### `tcp_native`

使用系统 socket API 做双向数据拷贝。客户端：

```bash
./build/apps/tcp_native HOST PORT
```

服务端：

```bash
./build/apps/tcp_native -l HOST PORT
```

### `tcp_ipv4`

使用 Minnow TCP、IPv4 和 TUN 设备运行连接。客户端示例：

```bash
sudo ./build/apps/tcp_ipv4 HOST PORT -d tun144
```

完整参数可执行：

```bash
./build/apps/tcp_ipv4 -h
```

### `ip_raw`

当前是原始 IP 实验入口，仍需要补充具体 datagram 收发逻辑。

## 开发流程

```text
+------------------+    +------------------+    +----------------------+
| 同步 my 远端     | -> | 检查 git status  | -> | 阅读接口、测试和实现 |
+------------------+    +------------------+    +----------+-----------+
                                                           |
                                                           v
                                                +----------------------+
                                                | 小范围修改           |
                                                +----------+-----------+
                                                           |
                                                           v
                                                +----------------------+
                                                | 重新配置 / 编译      |
                                                +----------+-----------+
                                                           |
                                                           v
                                                +----------------------+
                                                | 运行 checkpoint 测试 |
                                                +----------+-----------+
                                                           |
                                                           v
                                                +----------------------+
                                                | 检查 diff 和边界情况 |
                                                +----------+-----------+
                                                           |
                                      +--------------------+--------------------+
                                      |                                         |
                                      | 失败 / 发现风险                       | 验证完成
                                      v                                         v
                              +----------------------+              +----------------------+
                              | 返回阅读与分析       |              | 提交可解释变更       |
                              +----------------------+              +----------------------+
```

每次修改前先确认工作区：

```bash
git status --short --branch
git log --oneline --decorate --graph --all -12
git remote -v
```

本项目的 fork 远端名是 `my`：

```bash
git fetch my
git log --oneline HEAD..my/main
```

## 排查构建问题

### `Repository not found`

官方 `cs144/minnow` 地址在当前环境中不可用时，不要继续使用 `origin`，确认 fork：

```bash
git remote set-url my https://github.com/talisman0112/cs144.git
git fetch my
```

### `.git/FETCH_HEAD: Permission denied`

这通常是执行环境对 `.git` 写入的限制，不代表网络故障。可以在本机终端执行 `git fetch my`，再回到工作区检查 `my/main`。

### Visual Studio SDK 访问失败

确认：

1. Visual Studio C++ Desktop Development workload 已安装。
2. Windows SDK 已安装且版本可用。
3. 使用 Developer PowerShell 或 x64 Native Tools 命令行。
4. 删除并重新生成一个新的构建目录，避免旧 generator 缓存干扰。

### `/Wpedantic` 等参数不兼容

这些参数来自 GCC/Clang。使用 MSVC 时应在 `etc/cflags.cmake` 中按编译器区分参数，而不是直接将 Unix 参数传给所有 generator。

## 优化路线

### 阶段一：构建与测试链路

- 修复 Windows SDK 或切换到可用的 GCC/Clang 环境。
- 跑通 Checkpoint 1～3 的 functionality tests。
- 记录每个测试 target 的实际结果。

### 阶段二：TCP 核心稳定性

- 保持 RST 只由 stream error 或收到 RST 触发。
- 完善 partial ACK 对 outstanding segment 和 bytes in flight 的处理。
- 加强 ACK 合法性、序列号边界和重复 SYN 处理。
- 为零窗口、FIN、重传和 wrap-around 增加边界测试。

### 阶段三：恢复网络层

- 验证 NetworkInterface 的 ARP 请求、应答、缓存和待发送队列。
- 验证 Router 的最长前缀匹配、TTL 和 checksum 处理。
- 恢复 `tcp_eth_udp`、`fun_router` 及对应 CMake target。

### 阶段四：完整验证

- 执行 Checkpoint 5/6/7 测试。
- 执行 sanitizer 和性能测试。
- 检查 TUN 实机互通。
- 更新本说明书中的状态和验证结果。

## 许可证与课程背景

本仓库用于 CS144 风格的网络协议栈学习和实验。课程原始接口、测试框架和实验目标以仓库中现有代码及 `writeups/` 为准；对外发布或重新分发前请确认原课程项目的许可证和使用要求。
