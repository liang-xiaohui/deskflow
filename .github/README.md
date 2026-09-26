<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/deskflow/deskflow-artwork/blob/main/logo/deskflow-logo-dark-200px.png?raw=true">
  <source media="(prefers-color-scheme: light)" srcset="https://github.com/deskflow/deskflow-artwork/blob/main/logo/deskflow-logo-light-200px.png?raw=true">
  <img alt="Deskflow" src="https://github.com/user-attachments/assets/f005b958-24df-4f4a-9bfd-4f834dae59d6">
</picture>

# Keyboard follows local mouse activity

**One shared keyboard. A local mouse or trackpad for each computer.**

This experimental Deskflow branch adds an optional **Mouse follow mode**:
the shared keyboard follows the computer whose local pointing device was most
recently moved. Each computer keeps its own cursor at its own working position.
The existing **Extended screen mode** remains available.

> [!NOTE]
> This is a community prototype for upstream discussion and review.
> Windows and macOS backends are implemented. Full cross-platform input acceptance
> is still in progress; the validation status is described below.

## Why this workflow?

A keyboard takes up more desk space than an additional mouse or trackpad.
An intended setup is one keyboard connected to a Mac, a trackpad for the Mac,
and a separate mouse connected to a Windows PC, placed side by side.

When changing tasks, I usually use the pointing device to locate or select something
before typing. That first movement selects the keyboard destination automatically.
By the time I start typing, the keyboard has followed the computer I am operating.
Each cursor stays where I last used it, reducing the travel needed to cross screen
boundaries and reach the next target.

Users have already asked for a shared keyboard with independent mice in
[Keyboard Only mode? (#10071)](https://github.com/deskflow/deskflow/discussions/10071)
and [Is it possible to share the keyboard only? (#8982)](https://github.com/deskflow/deskflow/discussions/8982).
This branch explores local mouse activity as the way to select the keyboard destination.

## Two sharing modes

| Mode | Pointing devices | Keyboard destination |
| --- | --- | --- |
| **Extended screen mode** | The server's mouse or trackpad moves across configured screen edges. | Follows the shared cursor to the selected computer. |
| **Mouse follow mode** | Each computer uses its own mouse or trackpad; movement, clicks and scrolling stay local. | Follows the most recently moved local pointing device and stays there while devices are idle. |

In Mouse follow mode, the server supplies the shared keyboard. Clients report
local cursor activity, and the server selects one keyboard destination.
There is no need to cross a screen boundary or press a switching hotkey.

The implementation includes movement filtering, release of synthetic keys when
the target changes, held-modifier transfer, and a return to the server when the
target disconnects. Clipboard sharing remains available. The main window and tray
tooltip expose the running state and current keyboard destination.

## Try this branch

1. Build [`feat/keyboard-follow`](https://github.com/liang-xiaohui/deskflow/tree/feat/keyboard-follow)
   using the [build instructions](../docs/dev/build.md), and use matching builds on both computers.
2. Open the normal Deskflow GUI on the computer connected to the shared keyboard.
   Select **Share this computer's input**, then choose **Mouse follow mode** under **Sharing mode**
   or in **Configure Server**.
3. Register the client computer names and connect each client to the server as usual.
   Clients enter the selected sharing mode automatically. In Mouse follow mode,
   their positions in the screen grid do not determine keyboard ownership.
4. Start sharing, move either computer's mouse or trackpad, and check the
   **Keyboard → computer name** indicator before typing.

Use the GUI and Core from the same build. On Windows, a background service must
also match that build; the version-mismatch dialog offers **Use this version** to
stop the old service's Core and save desktop mode for the bundled Core.
Windows servers require input hooks. macOS requires the appropriate system input
permissions for the current build; stale permissions after rebuilding may need
to be renewed. The Mac backend checks that keyboard capture is actually available.

## Implementation and validation status

| Platform | Mouse follow implementation | Recorded validation |
| --- | --- | --- |
| Windows | Server and client backends implemented; integrated into the existing GUI. | Focused QtTest checks, deployed GUI/Core startup, mode selection, keyboard-target display and normal shutdown. |
| macOS | Server and client backends implemented; keyboard-capture permission checks included. | Native Apple Silicon build, focused QtTest checks, GUI/Core startup and initial Windows ↔ Mac usage feedback. |
| Linux / BSD | Mouse follow backend not implemented. | This feature has not been validated on these platforms. |

The focused tests cover protocol messages, keyboard routing, activity sampling,
disconnect cleanup, modifier state, GUI settings and service recovery. These checks
and initial usage feedback do not establish complete two-computer input acceptance.
Held-key transitions, modifier-plus-click actions, input methods, clipboard transfer,
reconnection and returning to Extended screen mode still need systematic testing.

See the [implementation and validation record](../docs/dev/keyboard-follow-mode.md)
(Chinese) for architecture, platform details and the remaining acceptance checklist.

For code review, start with [keyboard routing](../src/lib/server/Server.cpp),
[client activity sampling](../src/lib/client/Client.cpp), and the
[Windows](../src/lib/platform/MSWindowsScreen.cpp) and
[macOS](../src/lib/platform/OSXScreen.mm) input backends. The prototype extends the
protocol with activity and keyboard-ownership messages; compatibility of this mode
with unmodified peers remains a topic for upstream review.

## Upstream discussion

Feedback is welcome on the interaction, activity detection, keyboard ownership,
protocol compatibility and platform behavior before preparing an upstream PR.
This prototype was developed with AI assistance. Contributions will follow the
[Deskflow contribution guide](https://github.com/deskflow/deskflow/wiki/Contributing),
including disclosure of the tools used.

---

## About upstream Deskflow

**Deskflow** is a free and open source keyboard and mouse sharing app.
Use the keyboard, mouse, or trackpad of one computer to control nearby computers,
and work seamlessly between them.
It's like a software KVM (but without the video).
TLS encryption is enabled by default. Wayland is supported. Clipboard sharing is supported.

> [!TIP]
>
> **Chat with us**
>
> - Main discussion on Matrix: [`#deskflow:matrix.org`](https://matrix.to/#/#deskflow:matrix.org) ([Matrix clients](https://matrix.org/ecosystem/clients/))
> - Discussion also happens on IRC: `#deskflow` or `#deskflow-dev` on [Libera Chat](https://libera.chat/)
> - Start a [new discussion](https://github.com/deskflow/deskflow/discussions) on our GitHub project.

## Upstream downloads

The links below are for upstream Deskflow distributions. To review the implementation
described above, build this feature branch.

[![Downloads: Stable Release](https://img.shields.io/github/downloads/deskflow/deskflow/latest/total?style=for-the-badge&logo=github&label=Download%20Stable)](https://github.com/deskflow/deskflow/releases/latest)&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;[![Downloads: Continuous Build](https://img.shields.io/github/downloads/deskflow/deskflow/continuous/total?style=for-the-badge&logo=github&label=Download%20Continuous)](https://github.com/deskflow/deskflow/releases/continuous)&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;[![Download From Flathub](https://img.shields.io/flathub/downloads/org.deskflow.deskflow?style=for-the-badge&logo=flathub&label=Download%20from%20flathub)](https://flathub.org/apps/org.deskflow.deskflow)

> [!NOTE]
> On Windows, you will need to install the
> [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-microsoft-visual-c-redistributable-version).  
> Download latest: [`vc_redist.x64.exe`](https://aka.ms/vc14/vc_redist.x64.exe) [`vc_redist.arm64.exe`](https://aka.ms/vc14/vc_redist.arm64.exe)

> [!TIP]
> For macOS users, the easiest way to install and stay up to date is to use [Homebrew](https://brew.sh) with our [homebrew-tap](https://github.com/deskflow/homebrew-tap).
> macOS reports unsigned apps as damaged. This occurs because we do not use an Apple certificate for notarization. Clear the quarantine attribute to run the app: `xattr -c Deskflow.app`

To use Deskflow, download one of our [packages](https://github.com/deskflow/deskflow/releases), install `deskflow` (from your package repository), or [build it](https://github.com/deskflow/deskflow/wiki/Building) from source.

## Upstream project stats

[![GitHub commit activity](https://img.shields.io/github/commit-activity/m/deskflow/deskflow?logo=github)](https://github.com/deskflow/deskflow/commits/master/)
[![GitHub top language](https://img.shields.io/github/languages/top/deskflow/deskflow?logo=github)](https://github.com/deskflow/deskflow/commits/master/)
[![GitHub License](https://img.shields.io/github/license/deskflow/deskflow?logo=github)](../LICENSE)
[![REUSE status](https://api.reuse.software/badge/github.com/deskflow/deskflow)](https://api.reuse.software/info/github.com/deskflow/deskflow)

[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=deskflow_deskflow&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=deskflow_deskflow)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=deskflow_deskflow&metric=coverage)](https://sonarcloud.io/summary/new_code?id=deskflow_deskflow)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=deskflow_deskflow&metric=code_smells)](https://sonarcloud.io/summary/new_code?id=deskflow_deskflow)
[![Vulnerabilities](https://sonarcloud.io/api/project_badges/measure?project=deskflow_deskflow&metric=vulnerabilities)](https://sonarcloud.io/summary/new_code?id=deskflow_deskflow)

[![CI](https://github.com/deskflow/deskflow/actions/workflows/continuous-integration.yml/badge.svg)](https://github.com/deskflow/deskflow/actions/workflows/continuous-integration.yml)
[![CodeQL Analysis](https://github.com/deskflow/deskflow/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/deskflow/deskflow/actions/workflows/codeql-analysis.yml)
[![SonarCloud Analysis](https://github.com/deskflow/deskflow/actions/workflows/sonarcloud-analysis.yml/badge.svg)](https://github.com/deskflow/deskflow/actions/workflows/sonarcloud-analysis.yml)

## Contribute

[![Good first issues](https://img.shields.io/github/issues/deskflow/deskflow/good%20first%20issue?label=good%20first%20issues&color=%2344cc11)](https://github.com/deskflow/deskflow/labels/good%20first%20issue)

There are many ways to contribute to the Deskflow project.

We're a friendly, active, and welcoming community focused on building a great app.

Read our [Contributing](https://github.com/deskflow/deskflow/wiki/Contributing) page to get started.

For instructions on building Deskflow, use the wiki page: [Building](https://github.com/deskflow/deskflow/wiki/Building)

## Operating Systems

We support all major operating systems, including Windows, macOS, Linux, and Unix-like BSD-derived.

Windows 10 v1809 or higher is required.

macOS 13 or higher is required to use our CI builds for Apple Silicon machines. macOS 12 or higher is required for Intel macs or local builds.

Linux requires libei 1.3+ and libportal 0.8+ for the server/client. Additionally, Qt 6.7+ is required for the GUI.
Linux users with systems not meeting these requirements should use flatpak in place of a native package.

We officially support FreeBSD, and would also like to support: OpenBSD, NetBSD, DragonFly, Solaris.

## Repology

Repology monitors a huge number of package repositories and other sources comparing package
versions across them and gathering other information.

[![Repology](https://repology.org/badge/vertical-allrepos/deskflow.svg?columns=2&exclude_unsupported)](https://repology.org/project/deskflow/versions)

## Installing on macOS

When you install Deskflow on macOS, you need to allow accessibility access (Privacy & Security) to both the `Deskflow` app and the `deskflow` process.

If using Sequoia, you may also need to allow `Deskflow` under Local Network‍ settings (Privacy & Security).
When prompted by the OS, go to the settings and enable the access.

If you are upgrading and you already have `Deskflow` or `deskflow`
on the allowed list you will need to manually remove them before accessibility access can be granted to the new version.

macOS users who download directly from releases may need to run `xattr -c /Applications/Deskflow.app` after copying the app to the `Applications` dir.

It is recommended to install Deskflow using [Homebrew](https://brew.sh) from our [homebrew-tap](https://github.com/deskflow/homebrew-tap)

To add our tap, run:

```
brew tap deskflow/tap
```

Then install either:

- Stable: `brew install deskflow`
- Continuous: `brew install deskflow-dev`

## Similar Projects

In the open source developer community, similar projects collaborate for the improvement of all
mouse and keyboard sharing tools. We aim for idea sharing and interoperability.

- [**Lan Mouse**](https://github.com/feschber/lan-mouse) -
  Rust implementation with the goal of having native front-ends and interoperability with
  Deskflow/Synergy.
- [**Synergy**](https://symless.com/synergy) -
  Downstream commercial fork. Synergy sponsors Deskflow with financial support and contributes code ([learn more](https://github.com/deskflow/deskflow/wiki/Relationship-with-Synergy)).
- [**Input Leap**](https://github.com/input-leap/input-leap) -
  Inactive Deskflow/Synergy-derivative with the goal continuing Barrier development (now a dead fork).

## FAQ

### Is Deskflow compatible with Synergy, Input Leap, or Barrier?

Yes, Deskflow has network compatibility with all forks:

- Requires Deskflow >= v1.17.0.96
- Deskflow will _just work_ with Input Leap and Barrier (server or client).
- Connecting a Deskflow client to a Synergy 1 server will also _just work_.
- To connect a Synergy 1 client, you need to select the Synergy protocol in the Deskflow server settings.

_Note:_ Only Synergy 1 is compatible with Deskflow (Synergy 3 is not yet compatible).

### Is Deskflow compatible with Lan Mouse?

We would love to see compatibility with Lan Mouse. This may be quite an effort as currently the way they handle the generated input is very different.

### If I want to solve issues in Deskflow do I need to contribute to a fork?

We welcome PRs (pull requests) from the community. If you'd like to make a change, please feel
free to [start a discussion](https://github.com/deskflow/deskflow/discussions) or
[open a PR](https://github.com/deskflow/deskflow/wiki/Contributing).

### Is clipboard sharing supported?

Absolutely. The clipboard-sharing feature is a cornerstone feature of the product and we are
committed to maintaining and improving that feature.

### Is Wayland for Linux supported?

Yes! Wayland (the Linux display server protocol aimed to become the successor of the X Window
System) is an important platform for us.
The [`libei`](https://gitlab.freedesktop.org/libinput/libei) and
[`libportal`](https://github.com/flatpak/libportal) libraries enable
Wayland support for Deskflow. We would like to give special thanks to Peter Hutterer,
who is the author of `libei`, a major contributor to `libportal`, and the author of the Wayland
implementation in Deskflow. Others such as Olivier Fourdan and Povilas Kanapickas helped with the
Wayland implementation.

Some features _may_ be unavailable or broken on Wayland. Please see the [known Wayland issues](https://github.com/deskflow/deskflow/discussions/7499).

### Where did it all start?

Deskflow was first created as Synergy in 2001 by Chris Schoeneman.
Read about the [history of the project](https://github.com/deskflow/deskflow/wiki/History) on our
wiki.

## Meow'Dib (our mascot)

![Meow'Dib](https://github.com/user-attachments/assets/726f695c-3dfb-4abd-875d-ed658f6c610f)

## Deskflow Contributors

[![Sponsored by Synergy](https://raw.githubusercontent.com/deskflow/deskflow-artwork/b2c72a3e60a42dee793bd47efc275b5ee0bdaa5f/misc/synergy-sponsor.svg)](https://symless.com/synergy)

[Synergy](https://symless.com/synergy) sponsors the Deskflow project by contributing code and providing financial support ([learn more](https://github.com/deskflow/deskflow/wiki/Relationship-with-Synergy)).

Deskflow is made by possible by these contributors.

 <a href = "https://github.com/deskflow/deskflow/graphs/contributors">
   <img src = "https://contrib.rocks/image?repo=deskflow/deskflow"/>
 </a>

## License

This project is licensed under [GPL-2.0](../LICENSE) with an [OpenSSL exception](../LICENSES/LicenseRef-OpenSSL-Exception.txt).
