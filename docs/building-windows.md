## Building on Windows (clang-cl + vcpkg)

Libasyik can be built on Windows using **clang-cl** (Clang in MSVC ABI-compatible mode) with **Ninja**, **vcpkg**, and the **Windows SDK** from VS Build Tools.

### Prerequisites

Install the following tools via `winget` (Windows Package Manager):

```powershell
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install LLVM.LLVM
winget install Microsoft.PowerShell
```

Install **Visual Studio Build Tools** (required for Windows SDK headers and MSVC runtime libraries):

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCBuildTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --passive --wait"
```

Install **vcpkg** (C++ package manager):

```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### Build

Use the provided `build.bat` script which sets up the environment and runs CMake:

```cmd
build.bat           &:: incremental build
build.bat clean     &:: clean rebuild
```

The script handles:
- Initializing the MSVC environment via `vcvarsall.bat`
- Adding LLVM, Ninja, CMake, and PowerShell 7 to `PATH`
- Setting `VCPKG_ROOT=C:\vcpkg`
- Working around vcpkg SSL certificate revocation issues on some networks
- Running `cmake --preset windows-clang-cl` + `cmake --build build`

On the first build, vcpkg automatically downloads and compiles all 88 dependency packages (Boost, OpenSSL, SOCI, etc.). This takes significant time (~20-40 minutes) but subsequent builds use cached packages.

**Build outputs:**
- `build/src/libasyik.lib` &mdash; static library
- `build/tests/libasyik_test.exe` &mdash; test executable

### Run Tests

```cmd
run_test.bat
```

**Known test behavior on Windows:**
- **SQL tests** (`test_sql.cpp`) require a PostgreSQL server on `localhost:5432`. They fail with "Connection refused" if no database is available.
- **Rate limit QPS tests** may show lower-than-expected QPS due to Windows default timer resolution (~15ms vs ~1ms on Linux).
- **WebSocket close error code test** (`test_http.cpp:1208`) checks for `bad_descriptor` / `not_connected`, but Windows returns a different Winsock error code.

### CMake Preset

The `CMakePresets.json` defines the `windows-clang-cl` preset:
- **Generator:** Ninja
- **Compiler:** clang-cl (MSVC ABI compatible)
- **Linker:** lld-link
- **Package manager:** vcpkg (`x64-windows` triplet)
- **Features enabled:** SSL server, SOCI (PostgreSQL + SQLite3)

### vcpkg Dependencies

Defined in `vcpkg.json`. Key packages:
- `boost-beast`, `boost-fiber`, `boost-context`, `boost-url`, `boost-date-time`
- `openssl`
- `soci` with `postgresql` and `sqlite3` backends
- `libpq`, `sqlite3`, `zlib`

### Platform Differences

| Feature | Linux | Windows |
|---------|-------|---------|
| `SO_REUSEPORT` | Supported | Not available (uses `SO_REUSEADDR` only) |
| PostgreSQL LISTEN/NOTIFY | Full support (`posix::stream_descriptor`) | Full support (`tcp::socket::assign` on Winsock handle) |
| Static file serving | `realpath`, `stat`, `gmtime_r`, `strptime`, `timegm` | `_fullpath`, `_stat64`, `gmtime_s`, `std::get_time`, `_mkgmtime` |
| Socket error codes | POSIX (`EBADF`, `ENOTCONN`) | Winsock (`WSAEBADF`, `WSAENOTCONN`) |

### Troubleshooting

**vcpkg SSL error 35 (`CRYPT_E_REVOCATION_OFFLINE`):**
The `build.bat` script already includes a workaround using `X_VCPKG_ASSET_SOURCES` with `curl --ssl-no-revoke`. If you still see SSL errors, ensure system curl is on your PATH.

**Ninja not found by vcpkg:**
The script sets `VCPKG_FORCE_SYSTEM_BINARIES=1` to use your system-installed Ninja instead of vcpkg downloading its own.

**`VCPKG_ROOT` not set:**
The `build.bat` sets `VCPKG_ROOT=C:\vcpkg` locally. If you installed vcpkg elsewhere, edit the script accordingly. To set it permanently:
```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```
