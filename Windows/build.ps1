param(
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "..\\dist\\windows"
)

$ErrorActionPreference = "Stop"
$projectDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $projectDirectory $OutputDirectory))
$packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\\WinGet\\Packages\\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe"
$toolchain = Get-ChildItem $packageRoot -Directory -Filter "llvm-mingw-*-ucrt-x86_64" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $toolchain) {
    throw "LLVM-MinGW was not found. Install MartinStorsjo.LLVM-MinGW.UCRT with winget."
}
$bin = Join-Path $toolchain.FullName "bin"
$compiler = Join-Path $bin "x86_64-w64-mingw32-clang++.exe"
$resourceCompiler = Join-Path $bin "llvm-windres.exe"
$strip = Join-Path $bin "llvm-strip.exe"

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$object = Join-Path $projectDirectory "app-resources.o"
$output = Join-Path $outputPath "YilaiCodexSwitcher.exe"

Push-Location $projectDirectory
try {
    & $resourceCompiler --target=pe-x86-64 "app.rc" -O coff -o $object
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE." }

    $optimization = if ($Configuration -eq "Debug") { "-O0" } else { "-O2" }
    & $compiler -std=c++20 $optimization -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 `
        -finput-charset=UTF-8 -municode -mwindows -static -static-libgcc -static-libstdc++ `
        "main.cpp" "config.cpp" $object -o $output `
        -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lcomctl32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -ladvapi32
    if ($LASTEXITCODE -ne 0) { throw "Native C++ build failed with exit code $LASTEXITCODE." }

    if ($Configuration -ne "Debug") { & $strip --strip-all $output }
}
finally {
    Pop-Location
}

Get-Item $output | Select-Object FullName, Length, LastWriteTime
