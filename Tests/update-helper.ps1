$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixtureRoot = Join-Path $repo ('dist/update-helper-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$compiler = (Get-Command x86_64-w64-mingw32-clang++.exe).Source
$windres = Join-Path (Split-Path $compiler) 'llvm-windres.exe'
& $windres --target=pe-x86-64 (Join-Path $PSScriptRoot 'update-helper-fixture.rc') -O coff -o "$fixtureRoot/fixture.o"
if ($LASTEXITCODE) { throw 'Fixture resources failed' }
foreach ($kind in 'old','new','fails') {
    $arguments = @('-std=c++17','-O1','-municode','-mwindows','-static',"$PSScriptRoot/update-helper-fixture.cpp","$fixtureRoot/fixture.o",'-lshell32','-o',"$fixtureRoot/$kind.exe")
    if ($kind -eq 'old') { $arguments += '-DFIXTURE_OLD' }
    if ($kind -eq 'fails') { $arguments += '-DFIXTURE_FAIL_START' }
    & $compiler @arguments
    if ($LASTEXITCODE) { throw 'Fixture compilation failed' }
}
$previousLocal = $env:LOCALAPPDATA
$env:LOCALAPPDATA = Join-Path $fixtureRoot 'local'
New-Item -ItemType Directory -Path $env:LOCALAPPDATA | Out-Null
New-Item -ItemType Directory -Path "$env:LOCALAPPDATA/YilaiCodexSwitcher" | Out-Null
try {
    foreach ($scenario in 'success','wrong-hash','startup-failure') {
        $directory = Join-Path $fixtureRoot $scenario
        New-Item -ItemType Directory -Path $directory | Out-Null
        $target = Join-Path $directory 'YilaiCodexSwitcher.exe'
        Copy-Item -LiteralPath "$fixtureRoot/old.exe" -Destination $target
        $originalHash = (Get-FileHash $target).Hash
        $token = [Guid]::NewGuid().ToString('N')
        $stage = Join-Path $directory ('.yilai-update-' + $token)
        New-Item -ItemType Directory -Path $stage | Out-Null
        $source = Join-Path $stage 'YilaiCodexSwitcher.exe'
        $candidate = if ($scenario -eq 'startup-failure') { "$fixtureRoot/fails.exe" } else { "$fixtureRoot/new.exe" }
        Copy-Item -LiteralPath $candidate -Destination $source
        $backup = $target + '.old-' + $token
        $digest = (Get-FileHash $source).Hash.ToLowerInvariant()
        if ($scenario -eq 'wrong-hash') { $digest = '0' * 64 }
        $oldProcess = Start-Process -FilePath $target -ArgumentList '--hold' -WindowStyle Hidden -PassThru
        $eventName = 'Local\YilaiCodexSwitcher-update-' + $token
        $ready = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$eventName)
        $helper = $null
        try {
            $started = $oldProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
            $params = @{Source=$source;Target=$target;Backup=$backup;Stage=$stage;Sha256=$digest;Size=(Get-Item $source).Length;Version='v3.4.0';ProcessId=$oldProcess.Id;ProcessStarted=$started;Token=$token;ReadyEvent=$eventName}
            $json = $params | ConvertTo-Json -Compress
            $encodedParams = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))
            $helperPath = Join-Path $stage 'update.ps1'
            Copy-Item -LiteralPath (Join-Path $repo 'Windows/update.ps1') -Destination $helperPath
            $escapedHelper = $helperPath.Replace("'", "''")
            $script = "`$p=ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$encodedParams'))); `$a=@{}; `$p.PSObject.Properties | ForEach-Object {`$a[`$_.Name]=`$_.Value}; & '$escapedHelper' @a"
            $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
            $helper = Start-Process -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded -WindowStyle Hidden -PassThru -RedirectStandardOutput "$directory/helper.out" -RedirectStandardError "$directory/helper.err"
            if ($scenario -ne 'wrong-hash') {
                if (-not $ready.WaitOne(20000)) { throw "Helper was not ready: $scenario" }
                Stop-Process -Id $oldProcess.Id
            }
            if (-not $helper.WaitForExit(30000)) { throw 'Helper timed out' }
            $receipt = Get-Content "$env:LOCALAPPDATA/YilaiCodexSwitcher/update-result.json" -Raw | ConvertFrom-Json
            $expected = switch ($scenario) { 'success' {'success'} 'wrong-hash' {'failed'} 'startup-failure' {'rolled_back'} }
            if ($receipt.status -ne $expected) { throw "Unexpected result $($receipt.status) for $scenario" }
            if ($scenario -eq 'success') {
                if ((Get-FileHash $target).Hash -ne (Get-FileHash $candidate).Hash) { throw 'New executable not installed' }
                if ((Get-FileHash $backup).Hash -ne $originalHash) { throw 'Backup mismatch' }
            } elseif ((Get-FileHash $target).Hash -ne $originalHash) { throw 'Old executable not preserved/restored' }
            Write-Output "PASS: $scenario"
        } finally {
            $ready.Dispose()
            if ($helper -and -not $helper.HasExited) { Stop-Process -Id $helper.Id }
            if (-not $oldProcess.HasExited) { Stop-Process -Id $oldProcess.Id }
            # Only fixture executables at this exact target can be launched by the helper.
            Get-Process | Where-Object { try { $_.Path -eq $target } catch { $false } } | Stop-Process
        }
    }
    Write-Output "Helper test evidence: $fixtureRoot"
} finally { $env:LOCALAPPDATA = $previousLocal }
