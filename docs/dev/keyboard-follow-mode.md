# 鼠标跟随模式开发与验证

## 使用方式

只使用原有 Deskflow GUI：在服务器主窗口选择“共享模式”，或打开“配置服务器”。

- **扩展屏模式**（默认）：使用服务器的一套键盘和鼠标，按电脑布局跨屏切换。
- **鼠标跟随模式**：每台电脑使用自己的鼠标，共享服务器的键盘。最后移动鼠标的电脑接收键盘输入，静止时归属保持不变。

两种模式互斥。选择模式会保存 `server/keyboardFollow`，正在共享时通过原有 Core 生命周期重新启动；配置对话框的取消和重置沿用原有行为。

服务器仍需登记客户端电脑名。跟随模式保留网格用于管理这些电脑，位置不决定键盘归属。原来的边缘切换、相对鼠标移动、屏幕锁定和快捷键规则暂停使用；切回扩展屏时保留原有设置。剪贴板仍可共享。

主窗口状态栏与托盘提示显示运行状态；跟随模式启动后显示“键盘 → 电脑名”。断开目标客户端后回到服务器，停止后清除归属。

跟随模式的输入后端支持 Windows 和 macOS；Windows 服务器必须启用输入 hooks，Mac 需要授予辅助功能权限，作为服务器还需确认输入监控对当前签名的应用有效。客户端随协议通知自动进入此模式，不需要另选模式。两端应使用配套版本。

## 运行入口和部署

GUI、Core、Daemon 是 Deskflow 原有的进程结构：GUI 配置和控制，Core 处理输入与网络，Daemon 是可选后台服务。新增模式不增加应用、启动器或独立配置体系。

- 桌面运行：原 GUI 的“设置”中关闭后台服务，GUI 启动同目录的 Core。
- 服务运行：安装与 GUI 配套的后台服务，GUI 将同一份设置交给服务。
- 不应把新 GUI 与已安装的旧 Daemon 混用；版本不匹配时明确报错并回到停止状态。点击“使用此版本启动”会通过旧服务停止其 Core、保存桌面运行设置，再启动同目录配套 Core；待执行的恢复启动可以取消。
- Windows 构建依赖 Qt Svg；标准部署步骤复制 Qt 运行库、插件以及应用翻译。修改翻译时也会重新生成并部署 QM 文件，无需 GUI 重新链接。GUI 和托盘资源路径包含 `.svg`，窗口另有内嵌 ICO 备用图标。
- Windows 退出事件按 Core 的 PID 命名，Daemon 只向其管理的进程发退出信号，避免不同权限或独立实例共享同一个事件。
- Windows 线程取消先释放检查锁，再执行取消异常路径，避免停止时递归获取同一把互斥锁；屏幕停用可重复调用。

WorkBuddy 的本机日志显示，先前版本手动部署漏了 `Qt6Svg.dll`，测试只检查 GUI 进程存在；随后创建了独立测试配置、不同端口和管理员 Core 快捷方式。正常 GUI 仍连接旧服务，所以这些测试入口没有验证原 GUI 的共享流程。它们不属于新模式的使用方式。

## 实现

协议 1.9 沿用 Deskflow 的四字节标签：

| 消息 | 方向 | 含义 |
|---|---|---|
| `CKBF%4i` | 客户端 → 服务器 | 本地鼠标活动申请键盘，附序号 |
| `DKBF%1i%1i` | 服务器 → 客户端 | 当前是否拥有键盘、是否启用跟随模式 |

服务器保持 `m_active` 为本地屏幕，只用 `m_keyboardTarget` 路由键盘；广播设置不覆盖这个目标。鼠标不跨机转发，不调用切屏来更改键盘归属。

客户端仅在协议支持、模式启用且握手完成后创建 40 ms 采样定时器。移动门槛为曼哈顿距离 4 像素；申请冷却约 200 ms，包括静止时间。拥有键盘时也更新坐标，收到归属变化时重新采样，避免旧坐标导致误抢回。所有断线、发送失败、销毁路径清理定时器、模式状态和仍按住的合成键。

服务器记录按住的修饰键，切换时在新目标重放，以支持 Shift/Ctrl 加客户端鼠标点击。目标变化也刷新服务器剪贴板并送到新目标。Windows 补偿按键带专用 `dwExtraInfo` 标记，只送入本地应用、不重新转发；物理按键快照与被补偿修改的系统键状态分开保存。

macOS 实现同一组平台接口。主机保持鼠标事件本地放行，仅在键盘转给客户端时拦截普通键、修饰键和媒体键；客户端通过 Quartz 全局坐标参与既有活动采样，跟随模式下不隐藏或移动本机光标。补偿键用 `kCGEventSourceUserData` 标记，即使延迟进入 tap 也不被吞掉或重新转发。单独保存物理按键及左右修饰键状态，补偿改变系统状态后仍按物理状态转发和恢复键盘；Caps Lock 不重放。停止时归还本地键盘，tap 无法恢复时通过事件队列停止共享，避免在 tap 线程上等待自身退出。

IPC 保留最新连接状态、客户端列表和键盘归属，GUI 重连后能恢复当前状态，不依赖旧日志猜测。

## 验证范围

- `ClientTests`：模式与握手开关、归属变化、静止冷却、断线/发送失败/销毁清理、旧协议重连。
- `ServerTests`：原有结构测试，以及目标路由、广播隔离、修饰键迁移、鼠标不切屏、目标断开、保留原快捷键设置、IPC 状态重放和真实工作线程取消。
- `InputModeTests`：真实 Qt 控件的保存/取消/重置/重开、两模式控件状态、主窗口选择同步、图标渲染、归属标签、服务版本不匹配从 Starting 回到 Stopped，以及恢复按钮通过 IPC 停止旧服务、保存桌面设置和取消待执行启动。
- `ClientProxyTests`：协议输出与请求解析。
- `OSXKeyboardFollowTests`：补偿事件及其副本不污染物理快照、系统补偿后仍保留按住的修饰键、左右修饰键独立释放、Caps Lock 保留、无效键码处理，以及拒绝系统已移除键盘事件的监听范围。测试不向系统注入按键。

2026-09-26 本机验证：上述四个测试程序分别通过 10、8、8、24 个 QtTest 检查，共 50 个（含初始化和清理），无失败。干净构建后的两种模式均启动真实 Windows Core，在隔离的回环端口 24921 接受 TCP 连接，并通过各自 PID 的退出事件正常停止，退出码为 0。将 `build-real/bin` 复制到独立目录、移除开发环境的 Qt PATH 后，GUI/Core 的 `--version` 均成功；SVG 插件、Windows 平台插件以及应用和 Qt 中文翻译均已部署。控件与图标通过 Qt 离屏渲染检查。实际 Windows GUI 也已启动同目录 Core，显示“正在等待客户端连接”和“键盘 → Legion”，并可正常停止；旧服务版本冲突的恢复操作已将运行设置切换为桌面模式，并启动配套 Core。实际通知区域的显示仍需人工确认。

2026-09-26 macOS 验证：以 `6a986b03` 的 Windows 实现为基础，arm64 Release 构建完成，GUI 与 Core 均报告协议 1.9 配套版本。`ClientTests`、`ServerTests`、`InputModeTests`、`ClientProxyTests` 和 `OSXKeyboardFollowTests` 五组通过，分别有 10、7、8、24、8 个 QtTest 检查，共 57 个（含初始化和清理），无失败或跳过。Qt 运行库及其传递依赖已随应用部署，GUI/Core 的 `--version` 和完整签名检查通过。应用安装于 `/Applications/Deskflow.app`；首次启动需要为新构建重新授予系统输入控制权限，旧签名的授权不能作为新签名已获授权的证据。

授权更新后，实际 GUI 启动同目录 Core。服务器模式从扩展屏切为跟随模式时，旧 Core 正常退出，新 Core 显示 `Listening` 和本机键盘归属，日志明确确认本机鼠标保持本地。随后用户将 Mac 切为客户端，GUI 和 TCP 连接均确认已连接 Windows 服务器 `192.168.3.77:24800`，用户反馈基本功能可用。保留用户选择的客户端角色和连接，尚未逐项执行下列双机验收；日志另提示 Windows 的 `zh` 键盘布局未安装于 Mac，需要在中文输入验收时确认。

Mac 作为服务器的双机测试发现旧“输入监控”授权仍显示开启，但 Quartz 实际监听范围为 `0xfffbf3ff`，缺少按键按下和抬起事件；因此键盘归属能切换，按键仍只进入本机。用户移除旧授权并重新添加当前 `/Applications/Deskflow.app` 后，重启 Core，范围恢复为 `0xfffbffff`，用户反馈可用。新增启动检测使用 `CGGetEventTapList` 验证实际范围，缺少任一键盘事件时停止启动并给出重新授权的提示，避免静默失效。新增回归检查通过，Mac 测试组现为 9 个 QtTest 检查。检测补丁部署后，实际 Core 记录 `verified quartz keyboard capture permission` 并再次连接 `Legion`。

增量链接会重新带入 Homebrew Qt 的绝对库路径和 `LC_RPATH`。部署必须将 Qt 依赖改为应用内 Frameworks，并移除开发环境的绝对搜索路径；否则 GUI 可能同时加载开发目录和应用内两套 Qt，导致 Cocoa 插件启动崩溃。本次修正后已实际打开 GUI，并确认运行进程仅加载应用内 QtCore/QtGui。

本机 Xcode 尚未接受许可，构建使用独立 Command Line Tools，无需修改 Xcode：

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  -DBUILD_INSTALLER=OFF -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build-macos \
  --target Deskflow deskflow-core ClientTests ServerTests InputModeTests ClientProxyTests OSXKeyboardFollowTests
QT_QPA_PLATFORM=offscreen ctest --test-dir build-macos/src/unittests \
  -R '^(OSXKeyboardFollowTests|ClientTests|ServerTests|InputModeTests|ClientProxyTests)$' --output-on-failure
```

单机测试不能替代一台 Mac 与一台 Windows 的输入验证。双机验收应覆盖：两边鼠标轮流移动、静止不抢回、服务器本地不重复输入、Shift/Ctrl 加客户端鼠标点击、长按键切换、目标断线与重连、剪贴板粘贴、停止恢复本地键盘、切回扩展屏模式。

本机构建曾出现 MSVC 中文 include 输出没有被依赖扫描正确识别，修改类头文件后旧对象未重编译，导致实际 Core 堆损坏。本地 `build-real` 缓存已设置 `-DCMAKE_DEPENDS_USE_COMPILER=OFF`，使用 CMake 依赖扫描，并确认 `ServerApp.cpp` 对 `Server.h` 的依赖生效。涉及类结构变化必须确认头文件依赖有效或执行干净构建；进程存在不能作为启动成功证据。
