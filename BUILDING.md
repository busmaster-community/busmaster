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

GitHub Actions builds pull requests targeting `main` and pushes to `main`.
Successful builds store a ZIP of `Sources/BUSMASTER/BIN/Release` as a workflow
artifact. A push to `main` also creates a GitHub release when its commits imply
a semantic version bump; the ZIP is attached to that release.

Use Conventional Commit subjects to select the bump:

```text
fix: correct CAN timeout handling       # patch
feat: add a bus filter                  # minor
feat!: change the project file format   # major
```

`BREAKING CHANGE:` in a commit body also selects a major bump. Other commit
types do not create a release. The version is calculated from commits since
the previous `vMAJOR.MINOR.PATCH` tag. The version analyzer is
`scripts/get-semantic-version.ps1`. For squash merges, use a Conventional Commit
PR title so the merged commit communicates the intended version bump.

To require PR builds before merging, configure the repository's `main` branch
protection/ruleset to require the `Release (Win32)` check. GitHub does not allow
a workflow file to enable branch protection itself.

The CI build covers the kernel and primary BUSMASTER application. The separate
Qt-based LDF Editor and LDF Viewer solutions, format-converter suite, and
installer are not built by this workflow.
