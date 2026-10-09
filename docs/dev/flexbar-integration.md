# Flexbar 集成

Flexbar 是 Deskflow 的可选用户会话模块。设置中的 Flexbar 标签提供启用开关、运行状态、检查连接和重启模块。新安装默认关闭；保存后才生效，取消和重置不启动后台。

## 生命周期

- 开启后随 Deskflow GUI 运行；缩到托盘不停止，真正退出时停止本模块。Flexbar 不由系统权限的 Deskflow Daemon 承载。
- 只改 Flexbar 设置不会重启键鼠 Core。关闭后保留配对、业务去重状态和上次页面，不补发旧操作。
- Mac 通过本用户私有 socket 管理 FlexDesigner 插件。插件默认休眠，不启动原生捕获、输入或网络宿主；启用后启动。关闭、父连接断开或 6 秒管理租期失效时撤权并清理。
- Windows 由 QProcess 启动独立用户进程，以 stdin/stdout 管理；停止先撤权，最多等待 20 秒排空请求并正常退出，超时仅终止自己创建的子进程，不搜索或结束其他 Node/Deskflow/FlexDesigner 进程。运行环境、入口和数据目录须为绝对路径。
- Mac 插件断开后有限频率重连。Windows 启动失败不循环重启；明确显示安装、配对或已有手动实例问题，修复后由用户点击重启。

## 安装与配置

Mac 需要 Flexbar Companion 的 `com.dot.structuredhost` 0.2.0 或更新的受管插件，以及原有私有 `mac-host.json`。启用时按需启动 `/Applications/FlexDesigner.app`，关闭时不退出它。不能把“进程启动”当作设备已进入 DirectDraw 页；状态中会提示用户进入双机工作台。冷启动和 USB 重连的全自动页面恢复仍需独立实机验收。

Windows 需要支持受管接口的 Companion agent 包及配套 native helper。默认查找 Deskflow 可执行文件旁 `flexbar/agent.mjs`，Node 从 PATH 或 Program Files 查找，私有数据沿用 `%LOCALAPPDATA%/DotFlexbar/windows-agent`。可以在高级设置选择已安装的运行环境、agent 入口和私有数据目录。不会自动下载程序、建立配对、注册服务或新增开机项。

持久配置键：

| 键 | 默认值 | 用途 |
| --- | --- | --- |
| `flexbar/enabled` | `false` | 本机显式启用 |
| `flexbar/runtime` | 空，自动查找 | Windows Node 可执行文件 |
| `flexbar/agent` | 空，使用安装包路径 | Windows agent.mjs |
| `flexbar/dataDirectory` | 空，沿用默认私有目录 | Windows 配对及业务状态 |

从实验版迁移时，先正常停止旧手动 Windows agent，保留私有数据目录；安装兼容的受管 Mac 插件，再在两端 Deskflow 中启用。不会自动接管不属于自己的旧进程。保留旧程序和布局备份，但不能同时运行新旧宿主。

## 本地管理接口

仅用于父进程管理，不传业务内容、截图、触摸坐标或凭据，不支持任意执行。

Mac：`~/.dot-flexbar/deskflow.sock`，目录 0700、socket 0600，校验当前用户所有权并拒绝符号链接、非 socket 和不安全权限。只允许一个控制者；连接后发送 `{"type":"start"}`，每秒 `{"type":"status"}`，停止发送 `{"type":"stop"}`。断开即撤权。

Windows：`node agent.mjs start --managed --data <private-directory>`。父进程每秒发送 status；stop 或 stdin EOF 正常退出。无心跳、无效命令和输入过大时拒绝继续控制。

两端每行一个 JSON，响应 `type=status, version=1`，字段为 `running, connected, phase, owner, pageActive, actionsGranted, pending, lastIssue`。错误仅用固定代码，不返回业务正文。Deskflow 限制响应行 4 KiB、缓冲 16 KiB，6 秒无响应后停止本模块。

## 检查

`FlexbarTests` 覆盖默认关闭、保存/取消/重置、实际本地 socket、协议错误、子进程停止/重启/启动中取消、缺包与中文界面。`SettingsTests` 和 `InputModeTests` 检查既有设置及键鼠模式。Companion 宿主测试另覆盖单控制者、权限、管理租期、启动中断线与真实 SDK 生命周期缓存。

测试使用隔离设置和虚构进程，不向真实电脑发送控制动作。实机部署必须另外确认两端启停、旧实例迁移、页面持久化、断线撤权和权限有效性。

## 实机验收记录（2026-10-09）

Mac 已安装 `8ed296f9f` 的配套 GUI 和 Core，搭配 Companion `ea7be29` 的 0.2.0 宿主插件。保留原签名身份、系统权限、配对文件、布局及左右侧键配置。

- 用户确认进入紫色“双机工作台”后画面恢复；同时读取到真实 SDK `pageActive=true`，不是模拟激活。
- 真正退出 Deskflow 后，宿主报告 `disabled`，远程服务停止监听；重新启动时启用开关保留，已激活页面自动恢复，无须再次点设备入口。
- 在实际偏好设置中关闭、保存，再启用、保存，键鼠 Core 始终为同一进程。关闭时远程服务和侧键 Home socket 停止，重新启用后恢复既有配对、上次导航和设备页面。
- “重启模块”同样不重启键鼠 Core；恢复后的绘制错误为 0，原生 helper 报告就绪。
- `FlexbarTests`、`SettingsTests`、`InputModeTests` 三组通过；Companion 宿主 44 项、原生模块 11 项通过。

上述页面恢复以 FlexDesigner 及其真实 SDK 页面仍然存活为前提，不代表冷启动或 USB 重连自动进入已验收。独立旧版 `com.dot.touchbarbridge` 不属于受管宿主；迁移时停止它，避免重复捕获，不应把它的进程计入受管模块启停结果。若重启 FlexDesigner 使旧插件自行启动，应单独停用旧插件，不能用 Deskflow 开关关闭其他插件。

部署时须同时更新 Deskflow GUI 和 Core：旧 Core 不认识 `flexbar` 配置组，会在清理设置时删除它。仅替换 GUI 不属于兼容部署。
