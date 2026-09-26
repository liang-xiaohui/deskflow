# 单点键盘跟随模式（Keyboard Follow Mode, KFM）开发指引

> 目标读者：负责 Deskflow 下游分支开发的工程师
> 分支：`feat/keyboard-follow`（基于 upstream `master` @ `427ee652c8a`）
> 状态：Windows 端代码已落地，macOS 端待实现，尚未双机联调

---

## 1. 需求与语义定义

### 1.1 用户场景

桌上两台电脑，**各自有自己的鼠标**（各自有显示器、各自光标），但**只有一套键盘**。希望：

> 哪边光标（鼠标）动了，键盘就跟随到那台电脑，直到另一边的鼠标动了再切换。

### 1.2 精确语义（KFM 模式）

| 事件 | 结果 |
|---|---|
| 客户端 B 的物理鼠标移动 | 键盘所有权 → B；A 的本地键盘按键被吞掉并转发给 B 注入 |
| 服务器 A 的物理鼠标移动 | 键盘所有权 → A（本地）；B 收到的按键停止，B 侧释放所有按下的键 |
| 两台都没动鼠标 | 所有权保持不变（**闩锁**，不会自己飘回去） |
| 光标 | **永不移动、永不隐藏**；每台电脑的光标始终由本机鼠标控制 |
| 鼠标事件 | **从不跨机转发**（这是与原模式最大的区别） |
| 剪贴板 / 文件传输 | 保持原有行为，不受影响 |

### 1.3 与原模式的关系

| | 原模式（默认） | KFM（`server/keyboardFollow=true`） |
|---|---|---|
| 鼠标 | 一套鼠标，虚拟单一光标跨屏 | 各用各的，光标互不干涉 |
| 切换触发器 | 光标撞到屏幕边缘（jump zone） | 任一台的物理鼠标移动 |
| 屏幕几何 / 屏幕布局 | 核心，必须配置 | **完全不参与** |
| 切换后光标 | warp 到目标屏 + 隐藏本机光标 | 不动 |
| 热键切换（`switchToScreen` 等） | 可用 | 被忽略（打日志） |

两种模式**互斥**：`server/keyboardFollow` 为真时，越界切换、热键切换、屏保切换一律短路，只保留键盘重定向。

---

## 2. 现有架构（改之前必须理解的五件事）

> 所有行号对应 upstream `master @ 427ee652c8a`。

### 2.1 协议不是"消息 ID 枚举"

消息是 **4 字节 ASCII 标签 + printf 风格格式串**：

- 声明：`src/lib/deskflow/ProtocolTypes.h`（每个消息一段 Doxygen）
- 定义：`src/lib/deskflow/ProtocolTypes.cpp:17-55`
- 例：`const char *const kMsgCClipboard = "CCLP%1i%4i";`
- 读写：`ProtocolUtil::writef(stream, kMsgXxx, args...)` / `readf(stream, kMsgXxx + 4, &args...)`（`+4` 跳过标签）

版本：`kProtocolMinorVersion`（`ProtocolTypes.h:48`，KFM 从 8 提到 **9**）。
兼容策略**没有 feature flag**，只有"按协商到的 minor 选代理类"：

- 服务端：`ClientProxyUnknown::initProxy()`（`ClientProxyUnknown.cpp:140-189`）的 `switch (minor)`
- 客户端：`Client::setupScreen()`（`Client.cpp:470+`）的 `switch (protocolMinor)`
- 旧对端协商到 8，永远进不了新分支，也就永远收不到新消息 —— 这就是兼容保证。

### 2.2 服务端只有一个"输入接收方" `m_active`

```cpp
// Server.h:377-391
PrimaryClient *m_primaryClient;   // 本地屏幕的"伪客户端"
BaseClientProxy *m_active;        // 唯一决定输入送给谁
```

- 键盘：`Server::onKeyDown/onKeyUp/onKeyRepeat`（`Server.cpp:1535/1558/1581`）→ `m_active->keyDown(...)`
- 切换到远端：只有 `switchScreen()`（`Server.cpp:398`）和 `jumpToScreen()`（`Server.cpp:524`）两个入口；`m_active` 全仓只有两处赋值（`Server.cpp:487`、`Server.cpp:2046`）

### 2.3 `switchScreen()` 会把光标弄乱，KFM 绝不能走它

```cpp
// Server.cpp:398 起（原逻辑）
m_active->leave();                       // → PrimaryClient::leave()
    → MSWindowsScreen::leave()           // MSWindowsScreen.cpp:278
        warpCursor(m_xCenter, m_yCenter) // 把光标拉到屏幕中心！
        m_hook.setMode(kHOOK_RELAY_EVENTS) // 钩子切 relay：鼠标+键盘一起吞
m_active = dst;
m_active->enter(x, y, seqNum, ...)       // → warpCursor(x, y) 把光标挪过去
```

结论：**"只送键盘不碰光标"必须绕开 `switchScreen()` 的全部调用**，另起一条"键盘目标"通道。

### 2.4 Windows 上真正能吞键的只有低级钩子

- `MSWindowsHook` 是文件级静态单例，`SetWindowsHookEx(WH_KEYBOARD_LL/WH_MOUSE_LL)`（`MSWindowsHook.cpp:636-668`）
- 吞/放的唯一开关是回调返回值：`return true` → 钩子返回 1 → 事件不再下发给本机应用
- 模式由 `EHookMode g_mode` 驱动（`MSWindowsHook.h:38`、`MSWindowsHook.cpp:28`）：
  - `kHOOK_DISABLE` 全放行
  - `kHOOK_WATCH_JUMP_ZONE` 只吞"屏幕边缘跳转区"内的鼠标移动
  - `kHOOK_RELAY_EVENTS` 键盘全吞、鼠标全吞（原模式的"控制远端"状态）
- **关键事实**：relay 模式下键盘事件被吞掉后，仍然会 `PostThreadMessage(DESKFLOW_MSG_KEY, ...)` 给主线程（`MSWindowsHook.cpp:415-418`），再经 `MSWindowsScreen::onKey` → `KeyState::sendKeyEvent` → 服务端。**这正是 KFM 需要的转发通道**，所以 KFM 直接复用它。

### 2.5 客户端（secondary）在 Windows 上没有任何钩子

```cpp
// MSWindowsScreen.cpp:97-99
if (m_isPrimary && m_useHooks) {   // ← 客户端不满足 m_isPrimary
  m_hook.loadLibrary();
}
```

而且 `g_isPrimary` 硬编码 `TRUE`（`MSWindowsHook.cpp:44`），"客户端忽略注入事件"的分支是死代码。

**所以不要在客户端装钩子。** KFM 下服务端不再向客户端发送任何鼠标事件，客户端的光标只会被自己那支鼠标移动 → **客户端只要轮询自己的光标位置就能判断"本地鼠标动了"**。这是本方案最省事、且天然跨平台的一点（Windows/macOS/Linux 通用，不碰任何平台钩子）。

---

## 3. 设计

### 3.1 状态机

服务端新增一个与 `m_active` 平行的状态：

```cpp
bool m_keyboardFollow = false;              // 来自 server/keyboardFollow
BaseClientProxy *m_keyboardTarget = nullptr; // nullptr = 键盘在本机
```

键盘接收方由 `keyboardSink()` 决定：

```cpp
BaseClientProxy *Server::keyboardSink() const
{
  if (m_keyboardFollow && m_keyboardTarget != nullptr) return m_keyboardTarget;
  return m_active;   // KFM 下 m_active 恒为 m_primaryClient
}
```

- `m_active` 在 KFM 下**永远是** `m_primaryClient`（因为 `switchScreen`/`jumpToScreen` 被短路）
- 因此本地光标永不 warp、永不隐藏、鼠标事件永不被吞（KFM 钩子模式下鼠标一律放行）

### 3.2 两条触发路径

| 触发 | 路径 |
|---|---|
| 服务端自己的鼠标动了 | 物理鼠标 → `MSWindowsHook`（KFM 模式，放行）→ `DESKFLOW_MSG_MOUSE_MOVE` → `MSWindowsScreen::onMouseMove`（`m_isOnScreen` 恒为 true）→ `PrimaryScreenMotionOnPrimary` → `Server::onMouseMovePrimary` → **KFM 分支：清空 `m_keyboardTarget` 后立即 return** |
| 客户端鼠标动了 | 客户端定时器轮询 `GetCursorPos` 发现移动 → `kMsgCKeyboardFollow` → `ClientProxy1_9::keyboardFollowRequested()` → 事件 `ServerKeyboardFollowRequested` → `Server::handleKeyboardFollowRequest()` → `setKeyboardTarget(client)` |

### 3.3 键盘接管的执行机制

`Server::setKeyboardTarget()` 做三件事：

1. `m_screen->getPlatformScreen()->setKeyboardFollowDivert(target != nullptr)`
   → Windows：`MSWindowsHook::setKeyboardDivert(bool)` → 钩子的键盘分支按 `isRelayingEvents()` 决定吞还是放
   → 鼠标**不受任何影响**（KFM 模式在 `mouseHookHandler` 开头直接 `return false`）
2. 通知旧持有者 `proxy->keyboardFollow(false)`（`kMsgDKeyboardFollow`）→ 客户端 `fakeAllKeysUp()` 释放可能按住的键
3. 通知新持有者 `proxy->keyboardFollow(true)` → 客户端把 `m_isKeyboardFollowTarget` 置真，停止重复请求

### 3.4 时序（客户端拿到键盘）

```
B: 轮询发现光标移动 ──┐
                     │ CKBF(seq)
                     ▼
A: ClientProxy1_9 ──► ServerKeyboardFollowRequested ──► setKeyboardTarget(B)
                                                        ├─ hook: 键盘改为吞键
                                                        └─ DKBF(1) ──► B: m_isKeyboardFollowTarget = true
A: 用户敲键 ──► WH_KEYBOARD_LL(吞) ──► DESKFLOW_MSG_KEY ──► Server::onKeyDown
                                        └─ keyboardSink()==B ──► DKDL/DKUP ──► B: fakeKeyDown 注入
B: 用户移动自己的鼠标 ──► A 的 onMouseMovePrimary ──► setKeyboardTarget(nullptr)
                                                      └─ hook: 键盘放行回本地
                                                      └─ DKBF(0) ──► B: fakeAllKeysUp()
```

---

## 4. 改动清单（Windows 端，已落地）

> 约定：**改** = 修改既有文件，**新** = 新增文件。

### 4.1 协议层

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/deskflow/ProtocolTypes.h` | 改 | `kProtocolMinorVersion` 8→9；新增 `kMsgCKeyboardFollow` / `kMsgDKeyboardFollow` 声明（含 Doxygen，标记 `@since Protocol version 1.9`） |
| `src/lib/deskflow/ProtocolTypes.cpp` | 改 | `"CKBF%4i"`（Secondary→Primary，seq）、`"DKBF%1i"`（Primary→Secondary，1/0） |

### 4.2 服务端

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/server/Server.h` | 改 | 新增成员 `m_keyboardFollow` / `m_keyboardTarget`；新增 `keyboardSink()`、`setKeyboardTarget()`、`handleKeyboardFollowRequest()` |
| `src/lib/server/Server.cpp` | 改 | ① include `common/Settings.h`；② 构造函数读取 `server/keyboardFollow`；③ `onKeyDown/onKeyUp/onKeyRepeat` 的 sink 由 `m_active` 改为 `keyboardSink()`；④ `onMouseMovePrimary` 开头加 KFM 分支（清 target + return，**不碰几何/越界**）；⑤ 新增三个方法；⑥ `jumpToScreen()` 与 `switchScreen()` 开头 KFM 短路；⑦ `addClient` 注册 `ServerKeyboardFollowRequested` handler，`removeClient` 注销并在必要时收回键盘 |
| `src/lib/server/ClientProxy.h` / `.cpp` | 改 | 新增 `virtual void keyboardFollow(bool)`，基类实现为空操作（协议 <1.9 的客户端收不到） |
| `src/lib/server/ClientProxy1_9.h` / `.cpp` | 新 | 解析 `CKBF` → 发 `ServerKeyboardFollowRequested` 事件；覆写 `keyboardFollow(bool)` 发送 `DKBF` |
| `src/lib/server/ClientProxyUnknown.cpp` | 改 | `initProxy()` 加 `case 9:` |
| `src/lib/server/CMakeLists.txt` | 改 | 登记 `ClientProxy1_9.*` |
| `src/lib/base/EventTypes.h` | 改 | 新增 `ServerKeyboardFollowRequested` |

### 4.3 客户端

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/client/ServerProxy.h` / `.cpp` | 改 | 新增 `virtual void onLocalMouseActivity(uint32_t)`（基类空实现）+ protected `keyboardFollowChanged(bool)` 转发到 `Client` |
| `src/lib/client/ServerProxy1_9.h` / `.cpp` | 新 | 覆写 `onLocalMouseActivity()` 发送 `CKBF`；`parseMessage` 解析 `DKBF` → `keyboardFollowChanged()` |
| `src/lib/client/Client.h` / `.cpp` | 改 | ① `setupScreen()` 加 `case 9:`（`ServerProxy1_9` + `m_keyboardFollowSupported=true`）；② `handshakeComplete()` 启动跟随定时器；③ `cleanup()` 清理定时器；④ 新增 `setupFollowTimer/cleanupFollowTimer/handleFollowTimer/keyboardFollowChanged`；⑤ 文件级常量 `kFollowPollInterval=0.04s` / `kFollowMoveThreshold=4px` / `kFollowClaimCooldownTicks=5`（=200ms 限流） |
| `src/lib/client/CMakeLists.txt` | 改 | 登记 `ServerProxy1_9.*` |

### 4.4 平台层（Windows）

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/deskflow/IPlatformScreen.h` | 改 | 新增两个**非纯虚**默认实现：`setKeyboardFollowDivert(bool)`（默认只打日志）、`getLocalCursorPos(int32_t&, int32_t&) const`（默认转发 `getCursorPos`，保证其它平台老代码零改动即可编译） |
| `src/lib/platform/MSWindowsHook.h` | 改 | `EHookMode` 新增 `kHOOK_KEYBOARD_FOLLOW`；新增 `static void setKeyboardDivert(bool)` |
| `src/lib/platform/MSWindowsHook.cpp` | 改 | ① 新增 `g_keyboardDivert` 与 `isRelayingEvents()`（= relay 模式 或 KFM+已接管）；② 键盘分支的两处 `g_mode == kHOOK_RELAY_EVENTS` 判断改用 `isRelayingEvents()`；③ `mouseHookHandler` 开头加 KFM 分支：只上报 `DESKFLOW_MSG_MOUSE_MOVE`，**一律 `return false`**（永不吞鼠标） |
| `src/lib/platform/MSWindowsScreen.h` / `.cpp` | 改 | ① 新增成员 `m_keyboardFollow`（构造时读设置）与 `m_keyboardFollowDiverted`；② 实现 `setKeyboardFollowDivert`：**接管前**把本地正按住的键用 `fakeLocalKey(..., false)` 释放（否则本地会卡住修饰键），**归还时**用 `MSWindowsHook::getPhysicalKeyState()` 找出仍被物理按住的键并重新按下（否则用户正按着的 Shift 在本地会丢失）；③ 实现 `getLocalCursorPos`（直接 `GetCursorPos`）；④ `enable()`/`enter()` 选钩子模式：KFM 用 `kHOOK_KEYBOARD_FOLLOW`，否则 `kHOOK_WATCH_JUMP_ZONE`；⑤ `leave()` 在 KFM 下直接 return（防呆：绝不 warp 光标、绝不把钩子切回 relay） |

### 4.5 配置

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/common/Settings.h` | 改 | `Server::KeyboardFollow = "server/keyboardFollow"`，并登记进 `m_validKeys` 与 `m_defaultFalseValues`（默认关） |

**启用方式**（core 不解析业务命令行，全部走设置文件）：

```ini
# deskflow.conf
[server]
keyboardFollow=true
```

GUI 开关是后续待办（见 §8）。

---

## 5. 为什么不做成"改越界逻辑"

被否决的几种方案，记录理由免得后人重走：

| 方案 | 否决理由 |
|---|---|
| 让客户端把自己的鼠标增量发给服务端，服务端据此驱动虚拟光标 | 等于重写整个光标模型；两台各有光标，本来就不存在"一个虚拟光标" |
| 把 `m_active` 直接指向客户端但不 warp 光标 | `switchScreen()` 里 `leave()`/`enter()`/`warpCursor` 与钩子 mode 切换是耦合的，逐处打补丁会引入"光标被拉到中心/鼠标被吞"这类难查的回归 |
| 在客户端装 Windows 低级钩子来检测本地鼠标 | 客户端 `loadLibrary()` 被 `m_isPrimary` 挡住；`g_isPrimary` 是死代码；还会牵动 desk 线程/hider window 逻辑。轮询 `GetCursorPos` 更简单且跨平台 |
| 用 `m_keyboardBroadcasting`（键盘广播）复用 | 语义不同：广播是"同一输入发给多台"，KFM 是"同一时刻只发给一台" |

---

## 6. macOS 端要补什么（下一步）

设计上刻意把平台相关收敛到 **两个虚函数**，macOS 只需：

1. `OSXScreen::setKeyboardFollowDivert(bool)`
   - macOS 的输入拦截在 `OSXScreen` 的 `CGEventTap`（`m_eventTap`）里，钩子是否吞键由事件 tap 的回调决定。
   - 需要一个"只吞键盘、不吞鼠标"的状态位，等价于 Windows 的 `g_keyboardDivert`；鼠标事件必须原样 `return event`（放行）。
   - **注意**：macOS 的 tap 可能因为超时被系统摘掉（`kCGEventTapDisabledByTimeout`），要在回调里处理重新启用，并保证 KFM 下不会把 tap 丢掉后仍在吞键。
2. `OSXScreen::getLocalCursorPos(int32_t&, int32_t&) const`
   - 直接取本机光标（`CGEventCreate` + `CGEventGetLocation`，或 `NSEvent.mouseLocation` 转成屏幕坐标）。
   - 注意 macOS 的坐标系原点在左下，而 Deskflow 的 `x,y` 是左上原点，要翻转 y。
3. `OSXScreen::leave()` / `enable()` / `enter()` 加与 Windows 同样的 KFM 防呆（不要 `hideCursor`、不要 warp）。
4. 客户端侧**无需任何改动**：`Client` 的轮询逻辑是平台无关的，`getLocalCursorPos` 默认实现已经转发 `getCursorPos`，只要 OSXScreen 覆写它即可。

Linux（X11/libei）同理：`XWindowsScreen` / `EiScreen` 各加两个实现。若某平台实现不了 divert，KFM 会退化成"键盘不跟随"而不是把键盘锁死（`setKeyboardFollowDivert` 默认空实现）。

---

## 7. 边界情况与已知限制

| 情况 | 行为 | 说明 |
|---|---|---|
| 对端是旧版本（协议 ≤1.8） | KFM 不可用：客户端不会发 `CKBF`，键盘永远留在服务端 | 服务端日志会有 `ignoring keyboard follow request` / 客户端 `not sent: server protocol < 1.9` |
| 客户端进程崩溃/断线 | `removeClient` 收回键盘（`m_keyboardTarget=nullptr` + 解除 divert） | 不会把键盘锁在远端 |
| 网络抖动导致 `CBYE` 丢包 | 靠 TCP 传输，不会丢；连接断开即回收 | |
| 客户端按住 Ctrl 时键盘被抢走 | 服务端发 `DKBF(0)`，客户端 `fakeAllKeysUp()` | 防"Ctrl 卡死" |
| 服务端按住 Shift 时键盘被抢走 | divert 打开前用 `fakeLocalKey(..., false)` 释放本地已按下的键 | 否则本地 OS 永远收不到 key up |
| 键盘归还时用户仍按着某个修饰键 | 用 `getPhysicalKeyState()` 找出来并 `fakeLocalKey(..., true)` 重新按下 | 否则这个键在本地会"丢失"直到松开再按 |
| UAC / 安全桌面 / Ctrl+Alt+Del | **注入不了**，与原模式一致的限制 | 需要服务模式（`Use Service`）才能进安全桌面 |
| 全屏游戏 / 反作弊 | 注入可能被丢弃 | 同原模式 |
| 屏保激活 | `switchScreen()` 被 KFM 短路 → 光标不再跳到 (0,0)；`screensaver()` 消息仍正常广播 | 见 §4.2 ⑤ |
| 热键切换屏幕 | 被忽略（`jumpToScreen` 短路 + 日志） | 有意为之：KFM 下切屏没意义 |
| 服务端鼠标放在屏幕边缘 | 正常：KFM 模式下钩子不吞鼠标，jump zone 不生效 | 这是 `kHOOK_KEYBOARD_FOLLOW` 存在的核心理由 |
| 多个客户端同时请求 | "最后请求者获胜"（`setKeyboardTarget` 直接覆盖） | 两台机器的场景够用；多客户端可后续加 Lamport 时钟仲裁 |
| 运行时改 `keyboardFollow` | 需重启 core 生效 | 构造时读一次 |
| 输入法（IME） | 转发的是按键而非字符，远端 IME 状态由远端决定 | 与原模式一致 |

---

## 8. 后续待办

- [ ] **macOS 端**：§6 的两个虚函数 + 防呆
- [ ] **Linux 端**：同上（可选）
- [ ] **GUI 开关**：`src/lib/gui/dialogs/ServerConfigDialog.{h,cpp,ui}` 加复选框（参照 `Win32KeepForeground`），并在 `ServerConfigDialog::init_from_config/onFormChange` 里读写
- [ ] **单元测试**：
  - `src/unittests/server/ClientProxyTests.cpp` 加 `CKBF`/`DKBF` 的逐字节用例（仿 `keyDown_data`）
  - `src/unittests/client/ServerProxyTests.cpp` 加 `CKBF` 发送 + `DKBF` 解析用例
- [ ] **状态可视化**：托盘/日志提示"键盘当前在哪台"（现在只有 INFO 日志）
- [ ] **多客户端仲裁**：若将来超过 2 台，把"最后请求者获胜"换成带序号的 Lamport 时钟（参考 `kfm.py` 的实现思路）
- [ ] **上游化评估**：这是一个与几何模型正交的模式，理论上可以作为可选特性提交上游；但需要先说服维护者接受"键盘目标 ≠ active screen"这个第二状态

---

## 9. 测试计划

### 9.1 编译期

```bash
# 依赖：Qt 6.7+（含 Network/Widgets）、OpenSSL 3.0+、MSVC 2022 或 clang、CMake 3.21+
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64" \
      -DOPENSSL_ROOT_DIR="<openssl 3 安装根目录>" \
      -DBUILD_INSTALLER=OFF
cmake --build build --config Release --target deskflow-core deskflow
```

Windows 上若没有 OpenSSL，推荐 `vcpkg install openssl:x64-windows`（或 Qt Maintenance Tool 里的 OpenSSL Toolkit），然后给 CMake 加 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`。

### 9.2 双机手工用例（Windows，必测）

准备：A = 服务端（插键盘），B = 客户端。两端都装同一份编译产物；`server/keyboardFollow=true` 写在 A 的配置里。

| # | 操作 | 期望 |
|---|---|---|
| 1 | 不动任何鼠标，在 A 上打字 | 字出现在 A；A 日志无 divert 切换 |
| 2 | 动 B 的鼠标，然后在 A 的键盘上打字 | 字出现在 **B**；A 本地不出现任何字符；B 日志 `the keyboard is here` |
| 3 | 继续在 B 上打字 5 秒不动鼠标 | 仍然在 B（闩锁，不auto切回） |
| 4 | 动 A 的鼠标，再在 A 上打字 | 字回到 A；B 日志 `the keyboard went away` |
| 5 | 把 A 的鼠标慢慢移到屏幕最右边缘 | 光标正常停在边缘，**不跳屏**、不消失 |
| 6 | 在 A 上按住 Ctrl → 立刻动 B 的鼠标 → 松开 | B 不会卡 Ctrl；A 的 Ctrl 不残留 |
| 7 | 游戏/全屏程序中在 A 上打字 | 按当前设计跟随目标，不做特殊放行 |
| 8 | 拔掉/杀掉 B 的 core | A 的键盘立即恢复本地（`removeClient`） |
| 9 | 退出 KFM（去掉设置重启） | 完全回到原模式：越界切换、热键、光标隐藏都正常 |
| 10 | 混版本：B 换成官方 release | B 不会发 `CKBF`，A 键盘只在本机；日志有明确提示 |

### 9.3 日志排查

core 日志按级别过滤（`--log-level` 或 GUI 日志窗口）：

- `LOG_INFO`：`keyboard follow mode enabled` / `keyboard follow: the keyboard is now on "X"`
- `LOG_VERBOSE`：`keyboard follow: relaying/keeping the local keyboard`、`recv keyboard follow request (sequence=N)`、`send keyboard follow state`
- 排查清单：① 两台都必须是本分支编译的产物（协议 1.9）；② A 的配置里 `server/keyboardFollow=true`；③ A 的 core 以管理员运行（否则提权窗口收不到转发按键）；④ `m_useHooks` 不能关（`core/useHooks=false` 时没有钩子，KFM 退化）。

---

## 10. 一页速查

```
触发：客户端轮询 GetCursorPos → CKBF → ServerKeyboardFollowRequested
仲裁：Server::setKeyboardTarget()  ← 唯一改键盘归属的地方
生效：Server::keyboardSink()       ← onKeyDown/Up/Repeat 只认它
吞键：MSWindowsHook kHOOK_KEYBOARD_FOLLOW + g_keyboardDivert
光标：全程不动 —— switchScreen/jumpToScreen/leave() 在 KFM 下被短路
协议：1.9 / CKBF(Client→Server) / DKBF(Server→Client)
开关：server/keyboardFollow（默认 false，需重启）
```
