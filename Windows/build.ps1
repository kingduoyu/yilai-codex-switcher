param([string]$OutputDirectory = "../dist/windows")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputDirectory))
$packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe"
$toolchain = Get-ChildItem $packageRoot -Directory -Filter "llvm-mingw-*-ucrt-x86_64" | Select-Object -First 1
if(-not $toolchain) { throw "LLVM-MinGW UCRT is required" }
$bin = Join-Path $toolchain.FullName "bin"
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $PSScriptRoot
try {
    & "$bin/llvm-windres.exe" --target=pe-x86-64 app.rc -O coff -o "$output/resources.o"
    if($LASTEXITCODE -ne 0) { throw "Resource compilation failed" }
    $compileArgs = @('-std=c++17','-O2','-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-municode','-mwindows','-static','-static-libgcc','-static-libstdc++','-Wno-deprecated-literal-operator',
        '-I',"$root/Sources/ConfigRewrite/include",'-I',"$root/Sources/Diagnostics/include",'-I',"$root/Sources/OperationGuard/include",'-I',"$root/Sources/ConfigSources/include",'App.cpp','Platform.cpp',"$root/Sources/ConfigRewrite/config_rewrite.cpp", "$root/Sources/Diagnostics/diagnostics.cpp","$root/Sources/OperationGuard/operation_guard.cpp","$root/Sources/ConfigSources/config_sources.cpp","$root/Sources/ConfigSources/runtime_probe.cpp","$output/resources.o",'-o',"$output/YilaiCodexSwitcher.exe",'-lcomctl32','-lshell32','-lole32','-luuid','-lgdi32','-luser32','-ladvapi32','-lwindowscodecs')
    & "$bin/x86_64-w64-mingw32-clang++.exe" @compileArgs
    if($LASTEXITCODE -ne 0) { throw "Windows compilation failed" }
    & "$bin/llvm-strip.exe" --strip-all "$output/YilaiCodexSwitcher.exe"
} finally { Pop-Location }
Get-Item "$output/YilaiCodexSwitcher.exe" | Select-Object FullName,Length
