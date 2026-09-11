param([Parameter(Mandatory)][string]$Codex)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    & "$root/Windows/build.ps1"
    $compiler = (Get-Command x86_64-w64-mingw32-clang++.exe).Source
    $compileArgs = @('-std=c++17','-O2','-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-municode','-static','-Wno-deprecated-literal-operator',
        '-I','Sources/ConfigRewrite/include','-I','Sources/HistorySync/include','-I','Sources/Diagnostics/include','Tests/driver.cpp','Windows/Platform.cpp','Sources/ConfigRewrite/config_rewrite.cpp','Sources/HistorySync/history_sync.cpp','Sources/Diagnostics/diagnostics.cpp','dist/windows/sqlite3.o',
        '-o','dist/test-driver.exe','-lcomctl32','-lshell32','-lole32','-luuid','-lgdi32','-luser32','-ladvapi32','-lwindowscodecs')
    & $compiler @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Integration driver build failed' }
    node Tests/runtime-integration.mjs dist/test-driver.exe $Codex Tests/fixtures/ccs-native-responses-template.json
    if ($LASTEXITCODE -ne 0) { throw 'Runtime integration failed' }
} finally { Pop-Location }
