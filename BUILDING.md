# Building BUSMASTER on Windows

Install Visual Studio 2022 with the C++ desktop workload, including MFC for
Win32, and a Windows SDK. From the repository root, run:

```powershell
.\scripts\build-windows.ps1
```

The script builds the kernel and primary BUSMASTER solution in `Release|Win32`
using the Visual Studio 2022 `v143` toolset. The project files still specify
the legacy `v120_xp` toolset; the script overrides that setting for the newer
compiler.

GitHub Actions runs this same build on pushes and pull requests. This initial
CI target covers the kernel and primary BUSMASTER application. The separate
Qt-based LDF Editor and LDF Viewer solutions, format-converter suite, and
installer are not included.
