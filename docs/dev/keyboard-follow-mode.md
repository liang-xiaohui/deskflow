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
                                                        └─ DKBF(1,1) ──► B: m_isKeyboardFollowTarget = true
A: 用户敲键 ──► WH_KEYBOARD_LL(吞) ──► DESKFLOW_MSG_KEY ──► Server::onKeyDown
                                        └─ keyboardSink()==B ──► DKDL/DKUP ──► B: fakeKeyDown 注入
B: 用户移动自己的鼠标 ──► A 的 onMouseMovePrimary ──► setKeyboardTarget(nullptr)
                                                      └─ hook: 键盘放行回本地
                                                      └─ DKBF(0,1) ──► B: fakeAllKeysUp()
```

---

## 4. 改动清单（Windows 端，已落地）

> 约定：**改** = 修改既有文件，**新** = 新增文件。

### 4.1 协议层

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/deskflow/ProtocolTypes.h` | 改 | `kProtocolMinorVersion` 8→9；新增 `kMsgCKeyboardFollow` / `kMsgDKeyboardFollow` 声明（含 Doxygen，标记 `@since Protocol version 1.9`） |
| `src/lib/deskflow/ProtocolTypes.cpp` | 改 | `"CKBF%4i"`（Secondary→Primary，seq）、`"DKBF%1i%1i"`（Primary→Secondary：是否持有键盘 / KFM 是否开启） |

### 4.2 服务端

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/server/Server.h` | 改 | 新增成员 `m_keyboardFollow` / `m_keyboardTarget`；新增 `keyboardSink()`、`setKeyboardTarget()`、`notifyKeyboardFollow()`、`handleKeyboardFollowRequest()` |
| `src/lib/server/Server.cpp` | 改 | ① include `common/Settings.h`；② 构造函数读取 `server/keyboardFollow`；③ `onKeyDown/onKeyUp/onKeyRepeat` 的 sink 由 `m_active` 改为 `keyboardSink()`；④ `onMouseMovePrimary` 开头加 KFM 分支（清 target + return，**不碰几何/越界**）；⑤ 新增四个方法；⑥ `jumpToScreen()` 与 `switchScreen()` 开头 KFM 短路；⑦ `addClient` 注册 `ServerKeyboardFollowRequested` handler 并**立刻**下发一次 `DKBF(是否持有, KFM是否开启)`（客户端可能永远拿不到键盘，但必须知道模式已开），`removeClient` 注销并在必要时收回键盘 |
| `src/lib/server/ClientProxy.h` / `.cpp` | 改 | 新增 `virtual void keyboardFollow(bool isTarget, bool followMode)`，基类实现为空操作（协议 <1.9 的客户端收不到） |
| `src/lib/server/ClientProxy1_9.h` / `.cpp` | 新 | 解析 `CKBF` → 发 `ServerKeyboardFollowRequested` 事件；覆写 `keyboardFollow()` 发送 `DKBF`（带缓存，状态没变不重发） |
| `src/lib/server/ClientProxyUnknown.cpp` | 改 | `initProxy()` 加 `case 9:` |
| `src/lib/server/CMakeLists.txt` | 改 | 登记 `ClientProxy1_9.*` |
| `src/lib/base/EventTypes.h` | 改 | 新增 `ServerKeyboardFollowRequested` |

### 4.3 客户端

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/client/ServerProxy.h` / `.cpp` | 改 | 新增 `virtual void onLocalMouseActivity(uint32_t)`（基类空实现）+ protected `keyboardFollowChanged(bool isTarget, bool followMode)` 转发到 `Client` |
| `src/lib/client/ServerProxy1_9.h` / `.cpp` | 新 | 覆写 `onLocalMouseActivity()` 发送 `CKBF`；`parseMessage` **与 `parseHandshakeMessage`** 都要解析 `DKBF`（服务端在刚连上时就下发模式，可能早于握手完成） |
| `src/lib/client/Client.h` / `.cpp` | 改 | ① `setupScreen()` 加 `case 9:`（`ServerProxy1_9` + `m_keyboardFollowSupported=true`）；② `handshakeComplete()`：`enable()` **之后**再应用一次 `setKeyboardFollowLocalCursor()`（模式可能早于 enable 到达，而 enable 会藏光标）+ 启动跟随定时器；③ `cleanup()` 清理定时器；④ 新增 `setupFollowTimer/cleanupFollowTimer/handleFollowTimer/keyboardFollowChanged`；⑤ 常量 `kFollowPollInterval=0.04s` / `kFollowMoveThreshold=4px` / `kFollowClaimCooldownTicks=5`（=200ms 限流） |
| `src/lib/client/CMakeLists.txt` | 改 | 登记 `ServerProxy1_9.*` |

### 4.4 平台层（Windows）

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/lib/deskflow/IPlatformScreen.h` | 改 | 新增**三个非纯虚**默认实现（保证其它平台老代码零改动即可编译）：① `setKeyboardFollowDivert(bool)`（默认空实现）② `bool getLocalCursorPos(int32_t&, int32_t&) const`（默认转发 `getCursorPos`）③ `setKeyboardFollowLocalCursor(bool)`（默认空实现；只有 macOS 的客户端需要它，因为它的 `enable()` 会藏光标） |
| `src/lib/platform/MSWindowsHook.h` / `.cpp` | 改 | ① `EHookMode` 新增 `kHOOK_KEYBOARD_FOLLOW`；② 新增 `setKeyboardDivert(bool)`；③ **新增 `setIgnoreInjected(bool)`**：钩子在收起键盘状态下**同步**过滤自己合成（`LLKHF_INJECTED`）的按键，既不吞也不上报 —— KFM 的补偿键靠它才不会变成远端收到假事件 |
| `src/lib/platform/MSWindowsScreen.h` / `.cpp` | 改 | ① 新增成员 `m_keyboardFollow`（构造时读设置）与 `m_keyboardFollowDiverted`；② 实现 `setKeyboardFollowDivert`：整体包在 `m_hook.setIgnoreInjected(true/false)` 之间（**必须同步生效**——`fakeInputBegin()` 是走 desk 线程消息的，异步，会在补偿键注入之后才生效，靠它不可靠）。**接管前**把本地正按住的键用 `fakeLocalKey(..., false)` 释放（否则本地会卡住修饰键），**归还时**用 `MSWindowsHook::getPhysicalKeyState()` 找出仍被物理按住的键并重新按下（否则用户正按着的 Shift 在本地会丢失）；③ 实现 `getLocalCursorPos`（直接 `GetCursorPos`）；④ `enable()`/`enter()` 选钩子模式：KFM 用 `kHOOK_KEYBOARD_FOLLOW`，否则 `kHOOK_WATCH_JUMP_ZONE`；⑤ `leave()` 在 KFM 下直接 return（防呆：绝不 warp 光标、绝不把钩子切回 relay）。Windows 客户端本来就从不隐藏光标，所以 `setKeyboardFollowLocalCursor` 用默认空实现即可 |

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

## 6. macOS 端实施指引（可直接交给执行者）

> 本节是**交接文档**，不是提纲。共享层（协议 / Server / Client / 接口）已经全部落地并通过 MSVC 语法检查，macOS 侧的执行者**只需要动两个文件**：`src/lib/platform/OSXScreen.h` 和 `src/lib/platform/OSXScreen.mm`。不要改协议、不要改 `Server`/`Client`。

### 6.0 交接卡片

| 项 | 内容 |
|---|---|
| 目标 | 在 macOS 上实现 3 个虚函数 + 1 处 tap 回调改造 + 1 处 `leave()` 防呆 |
| 允许改的文件 | `src/lib/platform/OSXScreen.h`、`src/lib/platform/OSXScreen.mm`，**外加 `src/lib/platform/OSXKeyState.h` 的一行可见性改动**（已核实必须：`mapKeyButtonToVirtualKey` 是 `private static`，见 `OSXKeyState.h:126`） |
| 不允许改 | `ProtocolTypes.*`、`Server.*`、`Client.*`、`ServerProxy*`、`ClientProxy*`、`IPlatformScreen.h`、`Settings.h`（共享层已完成） |
| 完成定义 | ① 编译通过（Xcode/CMake，无新增警告）② 单机跑起来，日志出现 `keyboard follow mode` ③ 双机（Mac 服务端 + 任一客户端）按 §9.2 的用例 5/6 验证：光标不跳不隐藏；按住修饰键切换不粘键 |
| 必须先读 | 本节全部 + §2（架构）+ §4（共享层改了什么） |

### 6.1 macOS 输入层现状（已核实，含行号）

| 事实 | 位置 | 对 KFM 的意义 |
|---|---|---|
| tap 创建在 `OSXScreen::enable()`，primary 与 secondary **各装一个** | `OSXScreen.mm:692-749`（primary 705-708、secondary 720-723） | 两侧都有拦截能力 |
| tap location = **`kCGHIDEventTap`**，option = **`kCGEventTapOptionDefault`** | `OSXScreen.mm:705-708`、`720-723` | ✅ **能吞键，无需改 tap option、无需新权限** |
| 吞/放 = 回调返回值：`return event` 放行 / `return nullptr` 吞掉 | `OSXScreen.mm:1742-1746` | KFM 只需要改这里的判断 |
| 判断依据**只有** `m_isOnScreen` | `OSXScreen.mm:1742` | KFM 服务端该值恒为 true，**必须新增独立 divert 位** |
| 键盘分支：`kCGEventKeyDown/KeyUp/FlagsChanged` → `screen->onKey(event)` | `OSXScreen.mm:1710-1714` | 这条既有通路已把按键上报给服务端（等价 Windows 的 `DESKFLOW_MSG_KEY`），KFM 直接复用 |
| 媒体键走 `NX_SYSDEFINED` → `onMediaKey` | `OSXScreen.mm:1725-1740` | 需要**明确决定**媒体键是否一起吞（建议一起吞，否则音量键会在本机生效而其他键被转发） |
| secondary 的回调 `handleCGInputEventSecondary` 第一行就 `return event` | `OSXScreen.mm:1657-1678` | 客户端这个 tap 是死的，**不需要动** |
| tap 被系统摘掉：`kCGEventTapDisabledByTimeout` 会重新 `CGEventTapEnable`；`kCGEventTapDisabledByUserInput` **只打日志不恢复** | `OSXScreen.mm:1715-1724` | ⚠️ tap 死了 = 吞键静默失效（键盘偷偷回到本地，而服务端以为在转发）。实现时必须把 divert 与 tap 健康度关联 |
| `leave()` 会 `hideCursor()`；primary 还会 `CGAssociateMouseAndMouseCursorPosition(false)` 冻结光标 | `OSXScreen.mm:823-838` | 必须像 Windows 一样在 KFM 下直接 return |
| secondary 的 `enable()` 会 **`hideCursor()` + 把光标挪到主屏中心** | `OSXScreen.mm:709-715` | ⚠️ **这是必须额外处理的坑**：KFM 下客户端永远收不到 `enter()`，光标会一直不可见 |
| `getCursorPos()` 用 `CGEventCreate(nullptr)` + `CGEventGetLocation`，读的是**系统真实光标** | `OSXScreen.mm:245-255` | 坐标是 Quartz 全局坐标（主屏左上原点、y 向下），**与 Deskflow 一致，不需要翻转 y**（`NSEvent.mouseLocation` 才需要，本仓库没用） |
| 键盘注入：`OSXKeyState::fakeKey()` → `postHIDVirtualKey()`（IOHIDPostEvent）/ `postKeyboardKey()`（`CGEventCreateKeyboardEvent` + `CGEventPost(kCGHIDEventTap,...)`） | `OSXKeyState.cpp:643-718` | ⚠️ 见 6.5：`CGEventPost` 到 HID tap 会**再次进入我们自己 head-insert 的 tap** |
| 物理按键读取：`OSXKeyState::pollPressedKeys(KeyButtonSet&)`（`GetKeys`）/ `pollActiveModifiers()`（`GetCurrentKeyModifiers`） | `OSXKeyState.cpp:494-506`、`442-471` | 等价于 Windows 的 `getPhysicalKeyState`，用于"归还键盘时恢复仍按住的键" |
| macOS **没有** `fakeLocalKey`；`fakeInputBegin/End` 是 **`// FIXME -- not implemented`** 空实现 | `OSXScreen.mm:291-299` | 补偿键要自己实现，且"忽略自己合成的事件"这个机制 macOS 上**还不存在**（见 6.5，顺便把它补上） |
| 权限：primary 构造时要求 `AXIsProcessTrusted()`，`checkAXPermissions()` 每秒复查 | `OSXScreen.mm:131-137`、`1522-1532` | KFM 的吞键**不需要额外权限**（tap 本来就可拦截） |
| 安全输入（密码框）：`IsSecureEventInputEnabled()` 为真时系统扣住键盘 | `OSXScreen.mm:1827-1892` | KFM 转发会静默失效，属已知限制（与原模式一致） |
| tap 跑在独立线程 + 独立 `CFRunLoop` | `OSXScreen.h:301-304` | ⚠️ divert 标志**跨线程**：服务端线程写、tap 线程读 → 必须 `std::atomic<bool>` |
| `KeyState::fakeKeyUp(KeyButton)` / `fakeKeyDown(...)` **不能**用来注入补偿键 | `KeyState.cpp:899-914`（`fakeKeyUp` 要求 `m_serverKeys[button] != 0`，即只对"服务端转发下来的键"有效）、`795-826`（`fakeKeyDown` 需要 KeyID 能映射到键盘布局） | ⚠️ 陷阱：这两个 public API 看起来正好合适，但对本机物理按住的键直接返回 false/静默返回。补偿键必须走 `CGEventCreateKeyboardEvent` + `CGEventPost` |
| `mapKeyButtonToVirtualKey(b) = b - KeyButtonOffset`，`KeyButtonOffset` 保证 0 不被占用 | `OSXKeyState.cpp:1010-1013`、`1005-1009` | 映射本身是平凡的，但仍然**不要**复制粘贴到 `OSXScreen`（改可见性更安全） |

### 6.2 要实现的三个虚函数

```cpp
// OSXScreen.h
void setKeyboardFollowDivert(bool divert) override;
bool getLocalCursorPos(int32_t &x, int32_t &y) const override;
void setKeyboardFollowLocalCursor(bool keepVisible) override;

// 新增成员
bool m_keyboardFollow = false;                        // 构造时读 Settings::Server::KeyboardFollow（与 Windows 对齐）
std::atomic<bool> m_keyboardFollowDiverted{false};     // 服务端线程写、tap 线程读
bool m_keyboardFollowLocalCursor = false;              // 仅 secondary 用
std::atomic<bool> m_keyboardFollowFakeInput{false};     // 仅 macOS 用，见 6.5
```

**① `setKeyboardFollowDivert`（只扣键盘，绝不碰鼠标）**

```cpp
void OSXScreen::setKeyboardFollowDivert(bool divert)
{
  if (divert == m_keyboardFollowDiverted.load()) {
    return;
  }

  // 补偿键只给本机应用看：fakeInputBegin() 让 tap 原样放行、且不调用 onKey()
  // （等价于 Windows 的 setIgnoreInjected）。必须同步生效，所以用成员标志而不是
  // 走任何跨线程消息。
  fakeInputBegin();

  if (divert) {
    // 此刻物理按住的键已经被本机应用收到了，但它们的 key-up 之后会被转发到别的
    // 电脑，所以先在本机释放，否则会卡住修饰键
    KeyButtonSet held;
    m_keyState->pollPressedKeys(held);
    for (KeyButton b : held) {
      postLocalOnlyKey(b, false);
    }
  } else {
    // 反过来：仍被物理按住的键是刚才被吞掉并转发出去的，本机应用从没见过 key-down，
    // 需要补一次，否则用户正按着的 Shift 在本机"丢失"
    KeyButtonSet held;
    m_keyState->pollPressedKeys(held);
    for (KeyButton b : held) {
      postLocalOnlyKey(b, true);
    }
  }

  fakeInputEnd();
  m_keyboardFollowDiverted.store(divert);
  LOG_VERBOSE("keyboard follow: %s the local keyboard", divert ? "relaying" : "keeping");
}
```

**② `getLocalCursorPos`（可直接照抄 `getCursorPos`，去掉副作用）**

```cpp
bool OSXScreen::getLocalCursorPos(int32_t &x, int32_t &y) const
{
  CGEventRef event = CGEventCreate(nullptr);
  if (event == nullptr) {
    return false;
  }
  const CGPoint mouse = CGEventGetLocation(event);   // 全局坐标，左上原点，不要翻 y
  CFRelease(event);
  x = static_cast<int32_t>(mouse.x);
  y = static_cast<int32_t>(mouse.y);
  return true;
}
```
> 多显示器天然可用（全局坐标可为负）。覆写的唯一收益是不污染 `m_xCursor/m_yCursor/m_cursorPosValid`。

**③ `setKeyboardFollowLocalCursor`（只在 secondary 上有意义）**

```cpp
void OSXScreen::setKeyboardFollowLocalCursor(bool keepVisible)
{
  m_keyboardFollowLocalCursor = keepVisible;
  if (keepVisible && !m_isPrimary && m_cursorHidden) {
    showCursor();     // enable() 可能已经把光标藏了；这条消息可能早于/晚于 enable() 到达
  }
}
```
并把 `enable()` 的 secondary 分支改成有条件：
```cpp
  } else {
    // KFM: 光标属于本机鼠标，既不隐藏也不搬位置
    if (!m_keyboardFollowLocalCursor) {
      hideCursor();
      fakeMouseMove(m_xCenter, m_yCenter);
    }
    m_eventTapPort = CGEventTapCreate( ... handleCGInputEventSecondary ... );
  }
```

### 6.3 tap 回调改造（`OSXScreen::handleCGInputEvent`，`OSXScreen.mm:1681`；吞/放判断在 `1742-1746`）

> 鼠标**不需要任何改动**：KFM 下服务端 `m_isOnScreen` 恒为 `true`，回调末尾本来就会 `return event`（放行）。`onMouseMove()` 仍会照常上报给服务端，而服务端 `onMouseMovePrimary` 的 KFM 分支会立刻返回（不碰几何、不触发越界），所以光标完全由本机系统驱动。

把原来的
```cpp
  if (screen->m_isOnScreen) {
    return event;
  } else {
    return nullptr;
  }
```
换成：
```cpp
  // Keyboard follow mode: swallow ONLY the keyboard, never the mouse.  The keys
  // were already relayed by onKey() -> KeyState::sendKeyEvent.
  if (screen->m_keyboardFollowDiverted.load()) {
    switch (type) {
    case kCGEventKeyDown:
    case kCGEventKeyUp:
    case kCGEventFlagsChanged:
      return nullptr;
    case NX_SYSDEFINED:
      if (isMediaKeyEvent(event)) {
        return nullptr;      // 媒体键跟随键盘一起走（如果决定不在本机生效）
      }
      break;
    default:
      break;
    }
    return event;            // 鼠标按键/移动/滚轮一律放行
  }

  return screen->m_isOnScreen ? event : nullptr;
```
**同时在回调最前面加自我注入的放行（见 6.5）**：
```cpp
  if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kLocalOnlyKeyMarker) {
    return event;            // 我们自己注入的补偿键，永远放行
  }
```

### 6.4 `leave()` 防呆（`OSXScreen.mm:823`）

```cpp
void OSXScreen::leave()
{
  if (m_keyboardFollow) {
    // KFM 下光标永远留在本机：绝不 hideCursor、绝不冻结鼠标关联、绝不置 m_isOnScreen=false
    LOG_VERBOSE("ignoring leave request: keyboard follow mode keeps the cursor here");
    return;
  }
  ... 既有逻辑 ...
}
```
同理 `disable()` 里复位 `m_keyboardFollowDiverted`。

### 6.5 补偿键的自我捕获问题（**最容易踩的坑**）

`CGEventPost(kCGHIDEventTap, ...)` 的事件会**再次进入我们自己 `kCGHeadInsertEventTap` 的 tap**（且在链头，优先级最高）。如果不加识别：

1. divert 打开时注入的补偿 key-up 会被自己的 tap 吞掉或再次上报 → 本机应用收不到、远端收到假事件；
2. 补偿 key-down 同理。

处理办法：**不要自己发明 marker，把框架里缺的 `fakeInputBegin/End` 补上就行。**

macOS 上这两个函数是空的 FIXME 实现（`OSXScreen.mm:291-299`），而它们正是 Deskflow 为"忽略自己合成的事件"预留的 API（Windows 用 `fakeInputBegin` 设 `g_fakeServerInput`，见 `MSWindowsHook.cpp:246-249`；接口文档见 `IPrimaryScreen.h:134-147`）。实现成：

```cpp
// OSXScreen.h
std::atomic<bool> m_keyboardFollowFakeInput{false};

// OSXScreen.mm
void OSXScreen::fakeInputBegin() { m_keyboardFollowFakeInput.store(true); }
void OSXScreen::fakeInputEnd()   { m_keyboardFollowFakeInput.store(false); }

// handleCGInputEvent 的**第一行**（在 switch 之前）
if (screen->m_keyboardFollowFakeInput.load()) {
  return event;   // 自己合成的补偿键：原样放行，且不调用 onKey()（不上报给服务端）
}
```

这一下同时解决了三件事（不被吞 / 能到本机 / 不上报），而且**不依赖任何未经验证的 Apple 行为**——不用赌 `CGEventPost` 的事件会不会回到自己的 tap，也不用赌 userData 是否可靠。

要点：
- 必须在 `switch` **之前**返回，否则 `onKey(event)` 仍会把补偿键上报给服务端，又变成"远端收到假事件"。
- 标志由服务端事件线程写、tap 线程读 → 必须 `std::atomic<bool>`。
- `deskflow::Screen::fakeInputBegin()` 自带 `assert(!m_fakeInput)`（`Screen.cpp:337-351`），所以**不要**从 `Screen` 层调用、也不要嵌套；只在 `setKeyboardFollowDivert` 里调一次。
- **不要**在 tap 回调里调用 `CGEventPost`（Apple 说明 tap 回调里 post 的事件会被丢弃）。`setKeyboardFollowDivert` 由服务端事件线程调用，满足要求。
- 有了上面的标志，`postLocalOnlyKey` 本身不需要做任何标记：
  ```cpp
  void OSXScreen::postLocalOnlyKey(KeyButton button, bool down)
  {
    // 前置：按 §6.0 把 OSXKeyState::mapKeyButtonToVirtualKey 移到 public:
    CGKeyCode code = m_keyState->mapKeyButtonToVirtualKey(button);
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, code, down);
    if (event == nullptr) {
      return;
    }
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
  }
  ```
  > 为什么不用看起来更顺手的 `m_keyState->fakeKeyDown/fakeKeyUp`：`fakeKeyUp` 依赖 `m_serverKeys[]`（只对服务端转发下来的键有效，`KeyState.cpp:902-905` 对本机物理键直接 `return false`），`fakeKeyDown` 需要 KeyID 能映射进键盘布局。结论：补偿键必须走裸 `CGEvent`。
- 退路（仅当上面实测不成立时）：改用 `kCGEventSourceUserData` 标记 + 回调识别；最后退路是注入期间临时 `CGEventTapEnable(port, false)` 再恢复。

> Windows 侧的等价实现是 `MSWindowsHook::setIgnoreInjected()`（同步过滤 `LLKHF_INJECTED`）。两端语义要一致：**补偿键只到本机、不上报、不被吞**。

### 6.6 线程安全

| 数据 | 写者 | 读者 | 要求 |
|---|---|---|---|
| `m_keyboardFollowDiverted` | 服务端事件线程（`setKeyboardFollowDivert`） | tap 线程（回调） | `std::atomic<bool>` |
| `m_keyboardFollowLocalCursor` | 服务端事件线程（`Client` 收到 `DKBF` / `handshakeComplete`） | `enable()`、`showCursor()` 路径 | 只在事件线程读写即可（`enable()` 也在事件线程），无需原子 |
| `m_keyboardFollow` | 构造函数 | `leave()` | 只读 |

### 6.7 必须先在真机验证 / 可能推翻方案的点

1. **`fakeInputBegin/End` 需要你新实现**（macOS 上是空 FIXME）：这是本次唯一"新框架行为"。实测要点两条：① 补偿键确实到达本机应用；② 远端收不到假 key up/down（对话日志里没有多出来的 `DKUP`）。实现细节见 6.5，标志必须原子、必须在 `switch` 前判断。
2. **`mapKeyButtonToVirtualKey` 的可见性（已核实：必须改）**：`OSXKeyState.h:126` 把它声明在 `private:` 段（`static uint32_t mapKeyButtonToVirtualKey(KeyButton)`）。把它移到 `public:`（一行位置调整，不改实现）即可。**不要**改用 `KeyState::fakeKeyDown/fakeKeyUp` 绕过（原因见 6.1 最后两行：它们对本机物理键无效）。
3. **客户端光标**：`enable()` 的 hide+warp 改动是否影响到**原模式**（KFM 关闭时行为必须完全不变）。务必在 `server/keyboardFollow=false` 下回归一次。
4. **媒体键语义**：吞还是不吞，需与用户确认（建议吞）。
5. **tap 健康度**：`kCGEventTapDisabledByUserInput` 时是否要把 divert 关掉（建议：一旦发现 tap 不再有效，就 `setKeyboardFollowDivert(false)` + 日志告警，宁可键盘回到本机也不要静默丢键）。
6. **全屏 Space 切换 / 用户切换**（`userSwitchCallback`，`OSXScreen.mm:1379-1393`）会摘掉 tap，KFM 需容忍并自愈。
7. **`m_isOnScreen` 在 KFM 服务端恒为 true**：确认依赖它的既有代码（如 `onKey` 里的热键分支）不会因此行为异常。

### 6.8 验收标准（双机）

| # | 用例 | 期望 |
|---|---|---|
| M1 | 只开 Mac 服务端（KFM=on），不动鼠标打字 | 字在本机；日志无 divert 切换 |
| M2 | 动客户端鼠标 → 在 Mac 上打字 | 字出现在客户端；Mac 本地无字符；客户端日志 `the keyboard is here` |
| M3 | 动 Mac 鼠标 → 再打字 | 字回 Mac；客户端日志 `the keyboard went away` |
| M4 | 把 Mac 鼠标推到屏幕最右边缘 | 光标正常停在边缘，**不跳屏、不消失** |
| M5 | 按住 Shift → 动客户端鼠标 → 松开 | Mac 本地不粘 Shift；客户端不粘 Shift |
| M6 | **Mac 做客户端**（另一台开 KFM） | Mac 的**光标始终可见**、不被挪到屏幕中心；动它的鼠标后键盘跟过来 |
| M7 | KFM 关闭（回归） | 越界切换、光标隐藏、热键全部恢复原行为 |

> M6 是 macOS 独有的一条，也是 §6.1 里那个"客户端光标"坑的验收项。

### 6.9 Linux（可选，仅供参考）

`XWindowsScreen` / `EiScreen` 同样实现这三个虚函数：X11 下用 `XGrabKeyboard`/`XTestFakeKeyEvent` 的组合，libei 下用 `ei_device_start_emulating`/`ei_device_stop_emulating`。若某平台实现不了 divert，KFM 会退化成"键盘不跟随"而不是把键盘锁死（默认实现是空操作）。


---

## 7. 边界情况与已知限制

| 情况 | 行为 | 说明 |
|---|---|---|
| 对端是旧版本（协议 ≤1.8） | KFM 不可用：客户端不会发 `CKBF`，键盘永远留在服务端 | 服务端日志会有 `ignoring keyboard follow request` / 客户端 `not sent: server protocol < 1.9` |
| 客户端进程崩溃/断线 | `removeClient` 收回键盘（`m_keyboardTarget=nullptr` + 解除 divert） | 不会把键盘锁在远端 |
| 网络抖动导致 `CBYE` 丢包 | 靠 TCP 传输，不会丢；连接断开即回收 | |
| 客户端按住 Ctrl 时键盘被抢走 | 服务端发 `DKBF(0,1)`，客户端 `fakeAllKeysUp()` | 防"Ctrl 卡死" |
| 服务端按住 Shift 时键盘被抢走 | divert 打开前用 `fakeLocalKey(..., false)` 释放本地已按下的键 | 否则本地 OS 永远收不到 key up |
| 键盘归还时用户仍按着某个修饰键 | 用 `getPhysicalKeyState()` 找出来并 `fakeLocalKey(..., true)` 重新按下 | 否则这个键在本地会"丢失"直到松开再按 |
| 补偿键被当成用户输入转发出去 | Windows 下把补偿包在 `fakeInputBegin()/fakeInputEnd()` 里；macOS 下用 `kCGEventSourceUserData` 标记并在回调放行 | 否则远端会收到假的 key up/down |
| **macOS 客户端的光标** | KFM 开启时服务端下发 `DKBF(0,1)` → 客户端 `setKeyboardFollowLocalCursor(true)` → 保持光标可见、不搬到屏幕中心 | macOS 的 secondary `enable()` 默认会藏光标 + warp 到中心，不做这一步客户端光标**一直看不见**（验收用例 M6） |
| macOS tap 被系统摘掉 | `kCGEventTapDisabledByTimeout` 已会重新启用；`DisabledByUserInput` 只打日志 | divert 期间 tap 失效 = 键盘静默回到本机而服务端以为在转发，实现时应把 divert 与 tap 健康度关联 |
| macOS 媒体键（`NX_SYSDEFINED`） | 建议与键盘一起吞 | 否则音量键在本机生效、其他键被转发，行为割裂 |
| Windows 客户端的光标 | 无需处理 | Windows 的 secondary 从不隐藏光标（与 macOS 不同） |
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

- [x] **Windows 端**：实现完成，**已真编译 + 真链接**（`deskflow-core.exe` / `deskflow.exe` / `deskflow-daemon.exe`），并有 KFM 协议单测（24 passed / 0 failed）。见 §9.1.1–9.1.2
- [ ] **Windows 双机实测**：必须通过 GUI 或管理员终端启动（见 §9.1.3 的提权坑），按 §9.2 跑 10 条用例
- [ ] **macOS 端**：按 §6 交接卡片执行（3 个虚函数 + tap 回调改造 + `leave()` 防呆 + `enable()` 光标条件化）。共享层（协议 / Server / Client / 接口）已完成，执行者**只改 `OSXScreen.h/.mm` 与 `OSXKeyState.h` 的一行可见性**
- [ ] **Linux 端**：同上（可选）
- [ ] **GUI 开关**：`src/lib/gui/dialogs/ServerConfigDialog.{h,cpp,ui}` 加复选框（参照 `Win32KeepForeground`），并在 `ServerConfigDialog::init_from_config/onFormChange` 里读写
- [x] **单元测试（server 侧）**：`src/unittests/server/ClientProxyTests.cpp` 已加 `CKBF`/`DKBF` 逐字节用例 + 事件断言
- [ ] **单元测试（client 侧）**：`src/unittests/client/ServerProxyTests.cpp` 加 `CKBF` 发送 + `DKBF` 解析用例；另需覆盖 `ClientProxyUnknown::initProxy` 的 1.9 版本分发
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
> 本机（Win 开发机）已用 NMake 生成器跑通同一条路：`python deskflow-tools/build_windows.py --openssl-root <prefix> --build-dir build-real deskflow-core Deskflow deskflow-daemon`，见 §9.1.1–9.1.3。

Windows 上若没有 OpenSSL，推荐 `vcpkg install openssl:x64-windows`（或 Qt Maintenance Tool 里的 OpenSSL Toolkit），然后给 CMake 加 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`。

#### 9.1.1 本机（开发用的 Win 机）踩到的坑与现成解法

> 这几条与环境强相关，换机器可跳过；但 2026-09 在这台机器上确实都撞到了，且**都已解决**（不是"建议"，是已跑通的路径）。
>
> 本节提到的 `deskflow-tools/*.py` 与 OpenSSL 源码/产物都在仓库外：`C:\Users\liang\WorkBuddy\2026-09-26-10-24-22\deskflow-tools\`（脚本）与 `C:\Users\liang\kfm-build\`（1GB 级构建 scratch）。它们是本机的构建辅助，不随仓库分发。

| 坑 | 现象 | 解法 |
|---|---|---|
| `vcvars64.bat` 坏 | cmd 解析报错"此时不应有 \Windows" | 手拼 `INCLUDE`/`LIB`/`PATH`（MSVC 14.44.35207 + Windows SDK 10.0.26100.0）。已封装：`deskflow-tools/build_windows.py` |
| vcpkg 缺 `scripts/vcpkgTools.xml` | 每次 install 都重新"获取工具"，且解压子进程必报 `CreateFileW stdin failed with 231 (All pipe instances are busy.)` —— bash 与 PowerShell/ConPTY 下都一样，`7zr.exe` 手工运行却正常 | **放弃 vcpkg**，见下面两条 |
| 本机无原生 Windows Perl | Git 自带 perl 是 cygwin 版，**OpenSSL 3.6 明确拒绝**它做 VC-WIN64A 配置（"doesn't produce Windows like paths"） | 用 **portable Strawberry Perl 5.42.3.1**（`gh release download` 官方发布，只解压不安装） |
| 本机无 OpenSSL 3 | `find_package(OpenSSL 3.0 REQUIRED)` 让 configure 失败 | 两条路：① 只验证编译 → `deskflow-tools/stub-openssl/cmake/FindOpenSSL.cmake`；② 要可运行产物 → **从源码造 OpenSSL**（下面已跑通） |

**① 源码造 OpenSSL 3.6.4（同 vcpkg 锁定版本，已跑通）**

```bash
python C:/Users/liang/kfm-build/build_openssl.py configure   # VC-WIN64A no-asm no-tests
python C:/Users/liang/kfm-build/build_openssl.py build       # nmake，约 1 小时（!），建议后台跑
python C:/Users/liang/kfm-build/finish_openssl.py            # 等编译收敛 → install_dev → 补 applink.c
# 产物：C:/Users/liang/kfm-build/openssl-out/{include,lib,bin}
```
> 两个细节：`nmake` 全量编译耗时约 1 小时（crypto 部分 1600+ 个 obj），**必须放后台**，前台工具会在 15 分钟超时；`finish_openssl.py` 会把源码里的 `ms/applink.c` 复制到 `<prefix>/include/openssl/`，否则 CMake 内置 `FindOpenSSL` 不会创建 `OpenSSL::applink`（Deskflow 在 Windows 上链它）。

**② 真构建（已跑通，含链接）**

```bash
# 只验证改动涉及的静态库（用 stub，快）
python deskflow-tools/build_windows.py app server client platform

# 完整构建（真 OpenSSL，会链接出可执行文件）
python deskflow-tools/build_windows.py --openssl-root C:/Users/liang/kfm-build/openssl-out \
       --build-dir build-real deskflow-core Deskflow deskflow-daemon

# 单文件语法快检（不需要任何依赖，5 秒）
python deskflow-tools/check_tus.py
```

实测产物（`build-real/bin/`）：

| 文件 | 状态 |
|---|---|
| `deskflow-core.exe` | ✅ 链接成功；`--version` 输出 `v1.26.0.474 (f1c07776), protocol v1.9`（**证明协议号改动真的进了二进制**） |
| `deskflow.exe`（GUI） | ✅ 链接成功 |
| `deskflow-daemon.exe` | ✅ 链接成功 |
| `ClientProxyTests.exe` | ✅ 见 9.1.2，24 passed / 0 failed |

> GUI 目标链接后有一步 `windeployqt`，在本机会失败：`Unable to query qtpaths: Error running binary qtpaths: pipe:`（又是这台机器的管道老毛病）。它只是拷贝 Qt DLL，**失败发生在链接成功之后**，exe 已生成。运行时把 `C:/Qt/6.9.1/msvc2022_64/bin` 和 OpenSSL 的 `bin` 加进 `PATH` 即可，或手工跑一次 `windeployqt`。

#### 9.1.2 单元测试（KFM 协议 1.9 的运行时验证）

`src/unittests/server/ClientProxyTests.cpp` 里已经补上 7 条 KFM 用例：

| 用例 | 断言 |
|---|---|
| `keyboardFollow(1.9 target)` | `DKBF` + `01 01` |
| `keyboardFollow(1.9 released)` | `DKBF` + `00 01` |
| `keyboardFollow(1.9 mode off)` | `DKBF` + `01 00` |
| `keyboardFollow(1.8 unaffected)` / `(1.7 unaffected)` | **stream 为空** —— 旧客户端永远不会收到这个新消息（兼容性靠协商，不靠"忽略未知消息"） |
| `keyboardFollowRequest(request)` / `(sequence zero)` | `CKBF<seq>` 被解析并且确实抛出了 `ServerKeyboardFollowRequested` 事件 |

```bash
python deskflow-tools/build_windows.py --openssl-root C:/Users/liang/kfm-build/openssl-out \
       --build-dir build-real ClientProxyTests

cd build-real/src/lib/server
PATH="/c/Users/liang/kfm-build/openssl-out/bin:/c/Qt/6.9.1/msvc2022_64/bin:$PATH" \
  ../../unittests/server/ClientProxyTests.exe -o C:/temp/qtest.txt,txt
grep -E "FAIL|Totals" /c/temp/qtest.txt      # Totals: 24 passed, 0 failed
```
> **必须用 `-o <文件>,txt`**：这台机器上把 QTest 输出接到管道 / 交给 ctest（`--output-on-failure`）时，QTest 自己那部分输出会**整个丢掉**（只剩 Deskflow 的日志行），看起来像"测试崩了但没输出"。写文件就完整。

还没覆盖的（留给后续）：`ClientProxyUnknown::initProxy` 的版本分发（决定 1.9 客户端会不会真的被 `ClientProxy1_9` 接手）、以及 macOS/Linux 的 `ServerProxy1_9` 解析。

#### 9.1.3 想手工跑起来必须先解决"提权"这一关（**实测坑，容易误判成代码 bug**）

直接以普通权限启动 `deskflow-core.exe` 会在启动早期就死掉：

```
INFO: settings file changed: ...
FATAL: failed to create event for windows event loop
```

**原因不在 KFM**：`AppUtilWindows::eventLoop()`（`src/lib/deskflow/win32/AppUtilWindows.cpp:182`）要创建 `kCloseEventName = L"Global\\DeskflowClose"`（`common/Constants.h.in:37`），而 `Global\` 命名对象需要 **SeCreateGlobalPrivilege**（只有服务/管理员有）。实测：

```
CreateEventW(NULL, 1, 0, L"Global\\DeskflowClose") -> NULL, GetLastError=5 (ERROR_ACCESS_DENIED)
CreateEventW(NULL, 1, 0, L"DeskflowCloseTest")     -> OK
```

三条佐证它与我改动无关：① `server/keyboardFollow` 开与关**报错完全一样**；② `git diff master...feat/keyboard-follow` 里没有 `AppUtilWindows.*`（最后改动者是上游 KoljaFrahm，2026-08-29）；③ 本机 Deskflow 的 daemon 正是以**服务**身份运行并拉起 core 的（`[daemon] elevate=true`）。

所以：**要手工测试就用 GUI（它会经 daemon 提权拉起 core），或在管理员终端里直接跑 core**。

### 9.2 双机手工用例（Windows，必测）

准备：A = 服务端（插键盘），B = 客户端。两端都装同一份编译产物；`server/keyboardFollow=true` 写在 A 的配置里。

> ⚠️ **A 端必须通过 GUI 启动（走 daemon 提权），或在管理员终端里跑 core**，否则 core 会在启动早期 `FATAL: failed to create event for windows event loop` 直接退出——这是上游的提权设计，不是 KFM 的问题（详见 §9.1.3）。
> 配置方式：GUI 里改 `server/keyboardFollow`（若已加复选框，见 §8），或直接改 `%USERPROFILE%/AppData/Roaming/Deskflow/Deskflow.conf` 的 `[server]` 段；core 也支持 `-s <配置文件>` 指向别的配置（改端口避免和日常实例抢 24800）。
> 只调试一端时要注意：core 的单实例检查会跳过，但 `Global\DeskflowClose` 仍然由已在运行的 daemon 持有。

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
协议：1.9 / CKBF%4i（C→S 抢键盘）/ DKBF%1i%1i（S→C 是否持有 + KFM 是否开启）
开关：server/keyboardFollow（默认 false，需重启）
平台：只需实现 setKeyboardFollowDivert / getLocalCursorPos / setKeyboardFollowLocalCursor 三个虚函数
验证：build_windows.py --openssl-root <prefix> deskflow-core Deskflow deskflow-daemon ClientProxyTests
      单测 24 passed / 0 failed（-o <文件>,txt 否则输出会丢）；运行 core 必须提权（Global\DeskflowClose）
```
