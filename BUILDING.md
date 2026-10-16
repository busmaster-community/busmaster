# Building BUSMASTER on Windows

Install Visual Studio 2022 with the C++ desktop workload, including MFC for
Win32, and a Windows SDK. From the repository root, run:

```powershell
.\scripts\build-windows.ps1
```

The script builds the kernel and primary BUSMASTER solution in `Release|Win32`
using the Visual Studio 2022 `v143` toolset. The project files still specify
the legacy `v120_xp` toolset; the script overrides that setting for the newer
compiler. A full local build also requires the IXXAT VCI SDK 3.5; use
`-SkipIxxatDriver` if that vendor SDK is not installed.

GitHub Actions runs this same build on pushes and pull requests. This initial
CI target covers the kernel and primary BUSMASTER application. The separate
Qt-based LDF Editor and LDF Viewer solutions, format-converter suite, and
installer are not included. CI also skips the `CAN_IXXAT_VCI` driver because
it requires the vendor IXXAT VCI SDK (`vcinpl.h`), which is not installed on
the hosted runner.

The local build includes all projects by default. To mirror CI's build without
the IXXAT driver, run:

```powershell
.\scripts\build-windows.ps1 -SkipIxxatDriver
```
