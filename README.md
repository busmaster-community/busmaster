<h1 align="center">BUSMASTER</h1>

<p align="center">
  <img src="Documents/Logo/BUSMASTER_Logo_622x127.png" alt="BUSMASTER logo" width="320">
</p>

<p align="center">
  <a href="https://github.com/busmaster-community/busmaster/actions/workflows/windows-build.yml"><img src="https://github.com/busmaster-community/busmaster/actions/workflows/windows-build.yml/badge.svg" alt="Windows build"></a>
  <a href="COPYING.LESSER.txt"><img src="https://img.shields.io/badge/license-LGPL--3.0-blue.svg" alt="License: LGPL v3"></a>
  <a href="https://github.com/busmaster-community/busmaster/releases"><img src="https://img.shields.io/github/v/release/busmaster-community/busmaster" alt="Latest release"></a>
</p>

**BUSMASTER** is a monitoring, analysis and simulation tool for **CAN** and **LIN** bus systems on Windows. It is developed in C++ with MFC/ATL, released under the GNU Lesser General Public License v3, and maintained by the community — originally open-sourced by Bosch (RBEI-ETAS).

Think of it as a free, open-source alternative to commercial bus tools such as Vector CANoe / CANalyzer.

## ✨ Features

- **CAN & LIN monitoring** — trace messages, signal-level views, statistics, filters, and logging
- **Node simulation** — write simulation nodes in BusMaster's CAPL-like "C" scripting and simulate full ECUs without hardware
- **Database editors** — edit and view CAN databases (DBC) and LIN description files (LDF) with the built-in editors
- **Diagnostics (UDS / ISO-TP)** — request tester, diagnostic trouble codes and protocol-level tooling
- **Test automation** — create test setups and execute automated test suites against bus traffic
- **Replay & record** — capture bus traffic and replay it for regression testing
- **Multi-vendor hardware support** — drivers for Vector XL, PEAK PCAN-USB, Kvaser, IXXAT VCI, ETAS (ES581/BOA), ICS neoVI, and more
- **Simulation mode** — run the complete toolchain with no hardware attached (built-in stub driver)

## 🚀 Getting started

### Build from source

**Prerequisites**

- Windows
- Visual Studio 2022 with the **Desktop development with C++** workload (MSVC **v143** toolset, ATL and MFC) — the build script locates MSBuild automatically via `vswhere`, or you can add MSBuild to `PATH`
- .NET Framework (the legacy `build.bat` scripts use MSBuild; the BUSMASTER solution also supports CMake)

**Build**

1. Recommended: run `powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1`. It builds `Sources\Kernel\BusmasterKernel.sln` and `Sources\BUSMASTER\BUSMASTER.sln` as `Release|Win32` with the v143 toolset.
   - Add `-SkipIxxatDriver` if you don't have the IXXAT VCI SDK installed — the script then removes the IXXAT driver from the build (this is what CI does, since the IXXAT SDK is not publicly distributable).
2. Alternatively, open `Sources\Kernel\BusmasterKernel.sln` and `Sources\BUSMASTER\BUSMASTER.sln` in Visual Studio and build them yourself.
3. Legacy alternative: `Sources\build.bat` builds the Kernel, the BUSMASTER application (plus Language DLLs, LDF Editor/Viewer and Format Converters, using flex and bison from the `Tools` folder) in Release configuration.

Every push and pull request is built by GitHub Actions on `windows-2022` (see [`.github/workflows/windows-build.yml`](.github/workflows/windows-build.yml)), so the instructions above are continuously verified.

For the full development environment setup, tool downloads and step-by-step instructions, refer to the document *"1. BUSMASTER OSS - Development Environment"* in [`Documents/1 Development Environment`](Documents/1%20Development%20Environment/).

### Simulation mode without hardware

BUSMASTER can be run entirely in simulation mode — no CAN/LIN hardware is needed to explore the application, simulate nodes and replay logs. Follow the instructions in the development environment document to get started.

### Using real hardware

To use BUSMASTER with a real CAN bus, procure the interface hardware from the vendor and install its driver:

- **PCAN-USB** (PEAK-System) or other PEAK devices
- **ES 581** (ETAS GmbH) and further ETAS devices — install the ETAS HSP (Hardware Service Pack) driver

## 📚 Documentation

- [Documents/](Documents/) contains the full original documentation: user help, requirement and design specifications, and source code documentation
- [Examples/](Examples/) contains example configurations, including COM automation and test automation examples
- [ReleaseNotes.txt](ReleaseNotes.txt) describes the latest release (v3.2.x)

## 🛠️ Building an installer

NSIS installer scripts live in [`Installation Setup/`](Installation%20Setup/). See the development environment document for the complete instructions.

## 🤝 Contributing

Contributions are welcome — bug reports, fixes, documentation improvements and new ideas are all appreciated. Open an issue or submit a pull request.

## ⚖️ License

Licensed under the **GNU Lesser General Public License v3.0** — see [`COPYING.LESSER.txt`](COPYING.LESSER.txt) for details.

## 🙏 Credits

This project was originally developed by Bosch (RBEI-ETAS) and released as open source; the original upstream repository is [`rbei-etas/busmaster`](https://github.com/rbei-etas/busmaster). The open-source components used by BUSMASTER (MinGW/GCC, bison, flex, libxml2, gettext, Qt, Antlr, FreeType, the Oxygen icon set and more) are listed in [`Readme.txt`](Readme.txt).