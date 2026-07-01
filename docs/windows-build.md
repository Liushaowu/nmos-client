# Windows 编译说明

本文档说明如何在 Windows 环境下编译 `nmos-sync-daemon`。

当前 Windows 构建目标使用：

- Visual Studio 2022
- Conan 2.x
- CMake Presets
- 静态链接运行库
- 保留 mDNS
- 开启 LLDP


## 1. 手动安装的软件

### 1.1 Visual Studio 2022

建议安装：

- Visual Studio 2022 Community / Professional / Enterprise
- 版本：Visual Studio 17 2022
- 工具集：MSVC v143

安装 Visual Studio 时需要勾选：

- `Desktop development with C++`
- `MSVC v143 - VS 2022 C++ x64/x86 build tools`
- `Windows 10 SDK` 或 `Windows 11 SDK`
- `C++ CMake tools for Windows`

同时建议额外安装：

- `MSVC v142 - VS 2019 C++ x64/x86 build tools`

说明：部分 Conan 依赖（尤其 mDNSResponder）在 Windows 下可能会请求
`v142` 工具集。安装该组件可以避免依赖构建阶段缺少工具集。


### 1.2 CMake

要求：

- CMake `>= 3.23`

建议安装较新的 CMake 版本，并勾选：

- `Add CMake to the system PATH`

安装后确认：

```powershell
cmake --version
```


### 1.3 Python

要求：

- Python 3

建议版本：

- Python 3.11 或更新版本

安装时建议勾选：

- `Add python.exe to PATH`

安装后确认：

```powershell
python --version
```


### 1.4 Conan

要求：

- Conan 2.x

安装命令：

```powershell
python -m pip install conan
```

安装后确认：

```powershell
conan --version
```

如果 `conan` 不在 PATH 中，构建脚本会尝试从 Python 用户目录中查找
`conan.exe`。


### 1.5 Npcap 和 Npcap SDK

本项目 Windows 构建默认开启 LLDP：

```text
NMOS_CPP_BUILD_LLDP=ON
```

因此需要安装：

- Npcap Runtime
- Npcap SDK

下载地址：

```text
https://npcap.com/#download
```

运行时需要安装 Npcap Runtime。

编译时如果依赖需要 pcap 头文件和库，则需要 Npcap SDK。


## 2. 编译方式

所有命令建议在项目根目录执行：

```powershell
cd D:\WorkSpace\cpp\nmos-client-opencode
```


### 2.1 Release 编译

执行：

```powershell
.\scripts\windows_build.ps1
```

Release 构建目录：

```text
build\win-release
```

Visual Studio 解决方案：

```text
build\win-release\nmos-client.sln
```

可执行文件：

```text
build\win-release\src\Release\nmos-sync-daemon.exe
```

打包文件：

```text
build\win-release\nmos-sync-daemon-1.0.0-win64.zip
```


### 2.2 Debug 编译

执行：

```powershell
.\scripts\windows_build.ps1 -Debug
```

Debug 构建目录：

```text
build\win-debug
```

Visual Studio 解决方案：

```text
build\win-debug\nmos-client.sln
```

可执行文件：

```text
build\win-debug\src\Debug\nmos-sync-daemon.exe
```

打包文件：

```text
build\win-debug\nmos-sync-daemon-1.0.0-win64.zip
```

Debug 首次编译会重新构建 Debug 版本依赖，耗时会明显长于 Release。


## 3. 手动分步编译

如果不使用一键脚本，也可以手动执行 Conan 和 CMake。


### 3.1 Release

```powershell
$conan = "$env:APPDATA\Python\Python314\Scripts\conan.exe"

& $conan install . `
  --output-folder="build/win-release" `
  --build=missing `
  --profile:all="scripts/conan_profile_vs2022" `
  -s "build_type=Release" `
  -s "compiler.runtime_type=Release" `
  -o "nmos-cpp/*:shared=False" `
  -c "tools.cmake.cmaketoolchain:extra_variables={'NMOS_CPP_BUILD_LLDP':'ON'}"

cmake --preset win-release
cmake --build --preset win-release --config Release
```


### 3.2 Debug

```powershell
$conan = "$env:APPDATA\Python\Python314\Scripts\conan.exe"

& $conan install . `
  --output-folder="build/win-debug" `
  --build=missing `
  --profile:all="scripts/conan_profile_vs2022" `
  -s "build_type=Debug" `
  -s "compiler.runtime_type=Debug" `
  -o "nmos-cpp/*:shared=False" `
  -c "tools.cmake.cmaketoolchain:extra_variables={'NMOS_CPP_BUILD_LLDP':'ON'}"

cmake --preset win-debug
cmake --build --preset win-debug --config Debug
```


## 4. 运行配置文件

Windows 打包配置文件位于：

```text
config\windows\daemon_config.json
config\windows\node_config.json
config\windows\start.bat
```

绿色版 ZIP 中会包含：

- `nmos-sync-daemon.exe`
- `daemon_config.json`
- `node_config.json`
- `start.bat`

`daemon_config.json` 中的：

```json
"node_config_path": "node_config.json"
```

表示运行时会从当前工作目录查找 `node_config.json`。


## 5. Visual Studio 调试

Debug 编译完成后，打开：

```text
build\win-debug\nmos-client.sln
```

在 Visual Studio 中：

1. 将 `nmos-sync-daemon` 设置为启动项目
2. 打开项目属性
3. 进入 `Configuration Properties -> Debugging`
4. 设置启动参数和工作目录

示例：

```text
Command Arguments:
D:\WorkSpace\cpp\nmos-client-opencode\config\windows\daemon_config.json

Working Directory:
D:\WorkSpace\cpp\nmos-client-opencode\config\windows
```

如果使用绝对路径传入 `daemon_config.json`，建议 `Working Directory` 仍然指向
`config\windows`，这样相对路径 `node_config.json` 可以正常解析。


## 6. 注意事项

### 6.1 不要混用 Release 和 Debug 依赖

Release 使用：

```text
compiler.runtime_type=Release
CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
```

Debug 使用：

```text
compiler.runtime_type=Debug
CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug
```

不要将 Release 依赖链接到 Debug 工程，也不要将 Debug 依赖链接到 Release 工程。


### 6.2 从项目根目录执行 preset 命令

以下命令应在项目根目录执行：

```powershell
cmake --preset win-release
cmake --preset win-debug
```

不要在 `build\win-release` 或 `build\win-debug` 目录中执行项目级 preset。


### 6.3 Conan 会生成 CMakeUserPresets.json

执行 Conan 后，根目录可能会生成：

```text
CMakeUserPresets.json
```

这是 Conan 自动生成的用户 preset 文件，不是项目源码的一部分。

如果同时生成了 Release 和 Debug 的 Conan presets，建议优先使用项目自带的：

```text
CMakePresets.json
```


### 6.4 mDNS 和 LLDP

当前 Windows 构建保留 mDNS，并开启 LLDP。

构建脚本会传入：

```text
NMOS_CPP_BUILD_LLDP=ON
```

如果要在运行时使用 LLDP，请确保系统已安装 Npcap Runtime。


### 6.5 Windows HTTP 监听地址

Windows 下 cpprestsdk 使用 `http.sys` 作为 HTTP listener 后端。

如果配置 HTTP 监听地址，建议：

本机调试：

```json
"debug_http_url": "http://127.0.0.1:18088/"
```

监听所有网卡：

```json
"debug_http_url": "http://+:18088/"
```

不建议在 Windows 下使用：

```json
"debug_http_url": "http://0.0.0.0:18088"
```

因为 Windows `http.sys` 与 Linux socket 对 `0.0.0.0` 的语义不同。
