param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Target,
    [Parameter(Mandatory = $true)][string]$Backup,
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Sha256,
    [Parameter(Mandatory = $true)][long]$Size,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][long]$ProcessStarted,
    [Parameter(Mandatory = $true)][string]$Token,
    [string]$ReadyEvent = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Same-Path([string]$Left, [string]$Right) {
    return [string]::Equals($Left, $Right, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-Path([string]$Path, [bool]$Directory, [bool]$AllowMissing = $false) {
    if ($Path.Length -ge 260 -or $Path -notmatch '^[A-Za-z]:\\' -or
        $Path.IndexOf(':', 2) -ge 0 -or $Path.IndexOf([char]0) -ge 0) {
        throw 'Unsafe update path.'
    }
    $full = [IO.Path]::GetFullPath($Path)
    if (-not (Same-Path $Path $full)) { throw 'Update paths must be exact absolute paths.' }
    $current = [IO.Path]::GetPathRoot($full)
    $parts = $full.Substring($current.Length).Split([char]'\')
    $rootAttributes = [IO.File]::GetAttributes($current)
    if (($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0) {
        throw 'Unsafe update volume.'
    }
    for ($index = 0; $index -lt $parts.Length; ++$index) {
        $part = $parts[$index]
        if ($part.Length -eq 0 -or $part -eq '.' -or $part -eq '..' -or
            $part.EndsWith('.') -or $part.EndsWith(' ')) { throw 'Unsafe update path component.' }
        $current = [IO.Path]::Combine($current, $part)
        $last = $index -eq ($parts.Length - 1)
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch {
            $cause = $_.Exception.GetBaseException()
            if ($last -and $AllowMissing -and
                ($cause -is [IO.FileNotFoundException] -or $cause -is [IO.DirectoryNotFoundException])) {
                return $full
            }
            throw 'Update path is inaccessible.'
        }
        $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
        $expectedDirectory = if ($last) { $Directory } else { $true }
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $isDirectory -ne $expectedDirectory -or
            ($attributes -band [IO.FileAttributes]::Device) -ne 0) {
            throw 'Update paths must be regular files and directories without reparse points.'
        }
    }
    return $full
}

function Get-Digest([string]$Path) {
    $null = Assert-Path $Path $false
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        if ($stream.Length -le 0 -or $stream.Length -gt 104857600) { throw 'Invalid executable size.' }
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function Copy-NewFile([string]$From, [string]$To) {
    $null = Assert-Path $From $false
    $null = Assert-Path $To $false $true
    $inputStream = [IO.File]::Open($From, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $outputStream = $null
    try {
        $outputStream = [IO.FileStream]::new($To, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
            [IO.FileShare]::None, 65536, [IO.FileOptions]::WriteThrough)
        $inputStream.CopyTo($outputStream)
        $outputStream.Flush($true)
    } finally {
        if ($null -ne $outputStream) { $outputStream.Dispose() }
        $inputStream.Dispose()
    }
}

function Assert-Candidate([string]$Path) {
    $null = Assert-Path $Path $false
    if ([IO.FileInfo]::new($Path).Length -ne $Size -or (Get-Digest $Path) -cne $Sha256) {
        throw 'Downloaded executable size or SHA-256 mismatch.'
    }
    $fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    $match = [regex]::Match($Version, '^v?([0-9]{1,4})\.([0-9]{1,4})\.([0-9]{1,4})$')
    if (-not $match.Success -or $fileVersion.FileMajorPart -ne [int]$match.Groups[1].Value -or
        $fileVersion.FileMinorPart -ne [int]$match.Groups[2].Value -or
        $fileVersion.FileBuildPart -ne [int]$match.Groups[3].Value -or $fileVersion.FilePrivatePart -ne 0 -or
        $fileVersion.ProductMajorPart -ne $fileVersion.FileMajorPart -or
        $fileVersion.ProductMinorPart -ne $fileVersion.FileMinorPart -or
        $fileVersion.ProductBuildPart -ne $fileVersion.FileBuildPart -or $fileVersion.ProductPrivatePart -ne 0) {
        throw 'Executable version does not match the release.'
    }
}

function Replace-Executable([string]$From, [bool]$Restore = $false) {
    $useRename = $Restore
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $expectedDigest = if ($Restore) { $originalDigest } else { $Sha256 }
    while ($timer.ElapsedMilliseconds -lt 5000) {
        $null = Assert-Path $From $false
        $null = Assert-Path $Target $false $Restore
        $null = Assert-Path $Backup $false
        if ((Get-Digest $From) -cne $expectedDigest -or
            (-not $Restore -and (Get-Digest $Target) -cne $originalDigest)) {
            throw 'Executable changed before replacement.'
        }
        $code = [YilaiUpdateNative]::ReplaceOnce($Target, $From, $useRename)
        if ($code -eq 0) { return }
        if (-not $useRename -and $code -in @(1, 50, 120, 1175)) {
            # Recheck both hashes before a same-volume rename that bypasses ACL merging.
            $useRename = $true
            continue
        }
        if ($code -notin @(5, 32, 33)) { throw 'Executable replacement failed.' }
        $remaining = 5000 - $timer.ElapsedMilliseconds
        if ($remaining -le 0) { break }
        Start-Sleep -Milliseconds ([int][Math]::Min(200, $remaining))
    }
    throw 'Executable replacement remained locked past the update deadline.'
}

function Save-Result([string]$Status, [string]$FailureStage) {
    # Keep the script ASCII so Windows PowerShell 5.1 does not require a UTF-8 BOM.
    $messages = ConvertFrom-Json @'
{"success":"\u8f6f\u4ef6\u66f4\u65b0\u5b8c\u6210\u3002","rolled_back":"\u8f6f\u4ef6\u66f4\u65b0\u5931\u8d25\uff0c\u5df2\u6062\u590d\u5e76\u91cd\u65b0\u542f\u52a8\u65e7\u7248\u672c\u3002","failed":"\u8f6f\u4ef6\u66f4\u65b0\u672a\u5b8c\u6210\uff0c\u539f\u7a0b\u5e8f\u5df2\u4fdd\u7559\u3002","rollback_failed":"\u8f6f\u4ef6\u66f4\u65b0\u5931\u8d25\uff0c\u81ea\u52a8\u6062\u590d\u672a\u5b8c\u6210\u3002\u65e7\u7248\u5907\u4efd\u4fdd\u7559\u5728\u7a0b\u5e8f\u65c1\uff0c\u8bf7\u624b\u52a8\u6062\u590d\u3002"}
'@
    $receipt = [ordered]@{
        schema_version = 1
        product = 'YilaiCodexSwitcher'
        token = $Token
        target = $Target
        backup = $Backup
        version = $Version
        status = $Status
        stage = $FailureStage
        message = $messages.$Status
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
    }
    $text = $receipt | ConvertTo-Json -Compress
    $data = [Text.UTF8Encoding]::new($false).GetBytes($text)
    $temporary = $script:ResultPath + '.write-' + $Token
    $null = Assert-Path $temporary $false $true
    $null = Assert-Path $script:ResultPath $false $true
    $stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
        [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough)
    try {
        $stream.Write($data, 0, $data.Length)
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    try {
        $null = Assert-Path $script:ResultPath $false $true
        if (-not [YilaiUpdateNative]::MoveFileExW($temporary, $script:ResultPath, 9)) {
            throw 'Cannot persist update result.'
        }
    } finally {
        if ([IO.File]::Exists($temporary)) {
            $null = Assert-Path $temporary $false
            [IO.File]::Delete($temporary)
        }
    }
}

function Remove-Stage {
    # The exact stage is derived from the target and token; never follow a link while cleaning it.
    $null = Assert-Path $Stage $true
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Stage)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        $null = Assert-Path $directory $true
        foreach ($item in [IO.Directory]::GetFileSystemEntries($directory)) {
            $attributes = [IO.File]::GetAttributes($item)
            $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
            $null = Assert-Path $item $isDirectory
            if ($isDirectory) { $pending.Push($item) }
        }
    }
    Remove-Item -LiteralPath $Stage -Recurse -Force
}

$pathsReady = $false
$oldExited = $false
$backupReady = $false
$replacementAttempted = $false
$originalDigest = ''
$phase = 'validation'
$oldProcess = $null
$newProcess = $null
$script:ResultPath = ''
$exitCode = 1

try {
    if ($Token -cnotmatch '^[a-f0-9]{32}$' -or $Sha256 -cnotmatch '^[a-f0-9]{64}$' -or
        $Size -le 0 -or $Size -gt 104857600 -or $ProcessId -le 0 -or $ProcessStarted -le 0 -or
        $Version -cnotmatch '^v?[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}$') {
        throw 'Invalid update parameters.'
    }
    $Target = Assert-Path $Target $false
    if ([IO.Path]::GetExtension($Target) -ine '.exe') { throw 'Target must be an executable.' }
    $targetDirectory = [IO.Path]::GetDirectoryName($Target)
    $expectedStage = [IO.Path]::Combine($targetDirectory, '.yilai-update-' + $Token)
    $expectedSource = [IO.Path]::Combine($expectedStage, 'YilaiCodexSwitcher.exe')
    $expectedBackup = $Target + '.old-' + $Token
    if (-not (Same-Path $Stage $expectedStage) -or -not (Same-Path $Source $expectedSource) -or
        -not (Same-Path $Backup $expectedBackup) -or
        -not (Same-Path $PSCommandPath ([IO.Path]::Combine($expectedStage, 'update.ps1')))) {
        throw 'Update source, helper, target, or backup does not match the exact staging plan.'
    }
    $Stage = Assert-Path $Stage $true
    $Source = Assert-Path $Source $false
    $Backup = Assert-Path $Backup $false $true
    $null = Assert-Path $PSCommandPath $false
    if ([IO.File]::Exists($Backup)) { throw 'Backup path already exists.' }
    Set-Location -LiteralPath $targetDirectory
    [Environment]::CurrentDirectory = $targetDirectory
    $local = Assert-Path $env:LOCALAPPDATA $true
    $resultDirectory = Assert-Path ([IO.Path]::Combine($local, 'YilaiCodexSwitcher')) $true
    $script:ResultPath = Assert-Path ([IO.Path]::Combine($resultDirectory, 'update-result.json')) $false $true

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class YilaiUpdateNative {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ReplaceFileW(string target, string source, string backup, uint flags, IntPtr exclude, IntPtr reserved);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool MoveFileExW(string source, string target, uint flags);
    public static int ReplaceOnce(string target, string source, bool rename) {
        // PowerShell converts a null string argument to an empty path; keep native nulls in C#.
        bool replaced = rename ? MoveFileExW(source, target, 9) :
            ReplaceFileW(target, source, null, 0, IntPtr.Zero, IntPtr.Zero);
        return replaced ? 0 : Marshal.GetLastWin32Error();
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct StartupInfo {
        public uint size; public string reserved, desktop, title;
        public uint x, y, width, height, charsX, charsY, fill, flags;
        public ushort show, reservedSize;
        public IntPtr reservedData, input, output, error;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct Started { public IntPtr process, thread; public uint processId, threadId; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateProcessW(string application, StringBuilder command, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out Started process);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool CloseHandle(IntPtr handle);
    public static Started StartNormal(string application) {
        var startup = new StartupInfo();
        startup.size = (uint)Marshal.SizeOf(typeof(StartupInfo));
        startup.flags = 1; startup.show = 1;
        Started process;
        // Windows filenames cannot contain a quotation mark; this command has no additional arguments.
        if (!CreateProcessW(application, new StringBuilder("\"" + application + "\""), IntPtr.Zero,
            IntPtr.Zero, false, 4, IntPtr.Zero, System.IO.Path.GetDirectoryName(application), ref startup, out process))
            throw new InvalidOperationException("Cannot start the application.");
        return process;
    }
}
'@
    $pathsReady = $true
    Assert-Candidate $Source
    $originalDigest = Get-Digest $Target
    $probe = [IO.Path]::Combine($resultDirectory, '.update-probe-' + $Token)
    $null = Assert-Path $probe $false $true
    $probeStream = [IO.File]::Open($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $probeStream.Dispose()
    [IO.File]::Delete($probe)
    try { $oldProcess = [Diagnostics.Process]::GetProcessById($ProcessId) }
    catch {
        if ($_.Exception.GetBaseException() -isnot [ArgumentException]) { throw 'Cannot identify original process.' }
    }
    if ($null -ne $oldProcess -and -not $oldProcess.HasExited) {
        if ($oldProcess.StartTime.ToFileTimeUtc() -ne $ProcessStarted -or
            -not (Same-Path $oldProcess.MainModule.FileName $Target)) {
            throw 'Original process identity does not match the application.'
        }
    }
    if ($ReadyEvent.Length -gt 0) {
        if ($ReadyEvent -cne ('Local\YilaiCodexSwitcher-update-' + $Token)) { throw 'Invalid readiness event.' }
        $ready = [Threading.EventWaitHandle]::OpenExisting($ReadyEvent)
        try { $null = $ready.Set() } finally { $ready.Dispose() }
    }
    $phase = 'wait'
    if ($null -ne $oldProcess -and -not $oldProcess.WaitForExit(60000)) {
        throw 'Original application did not exit within the update deadline.'
    }
    $oldExited = $true
    if ($null -ne $oldProcess) {
        $oldProcess.Dispose()
        $oldProcess = $null
    }
    Assert-Candidate $Source
    $null = Assert-Path $Target $false
    if ((Get-Digest $Target) -cne $originalDigest) { throw 'Original executable changed before replacement.' }
    $phase = 'backup'
    Copy-NewFile $Target $Backup
    if ((Get-Digest $Backup) -cne $originalDigest) { throw 'Executable backup verification failed.' }
    $backupReady = $true
    $phase = 'replace'
    $null = Assert-Path $Target $false
    $null = Assert-Path $Source $false
    $null = Assert-Path $Backup $false
    $replacementAttempted = $true
    Replace-Executable $Source
    Assert-Candidate $Target
    $phase = 'launch'
    $newProcess = [YilaiUpdateNative]::StartNormal($Target)
    # Publish before resuming so the new application's first startup can consume the receipt.
    Save-Result 'success' ''
    if ([YilaiUpdateNative]::ResumeThread($newProcess.thread) -eq [uint32]::MaxValue -or
        [YilaiUpdateNative]::WaitForSingleObject($newProcess.process, 2000) -ne 258) {
        throw 'Updated application could not stay running.'
    }
    $exitCode = 0
} catch {
    # Persist fixed status text, never exception details, environment variables, or credentials.
    $failedPhase = $phase
    $status = 'failed'
    if ($null -ne $newProcess) {
        $null = [YilaiUpdateNative]::TerminateProcess($newProcess.process, 1)
        $null = [YilaiUpdateNative]::WaitForSingleObject($newProcess.process, 3000)
        $null = [YilaiUpdateNative]::CloseHandle($newProcess.thread)
        $null = [YilaiUpdateNative]::CloseHandle($newProcess.process)
        $newProcess = $null
    }
    if ($replacementAttempted) {
        $status = 'rollback_failed'
        try {
            if (-not $backupReady -or (Get-Digest $Backup) -cne $originalDigest) {
                throw 'A verified rollback backup is unavailable.'
            }
            $null = Assert-Path $Target $false $true
            if (-not [IO.File]::Exists($Target) -or (Get-Digest $Target) -cne $originalDigest) {
                $restore = [IO.Path]::Combine($Stage, 'rollback-' + $Token + '.exe')
                Copy-NewFile $Backup $restore
                if ((Get-Digest $restore) -cne $originalDigest) { throw 'Rollback copy verification failed.' }
                $null = Assert-Path $Target $false $true
                Replace-Executable $restore $true
                if ((Get-Digest $Target) -cne $originalDigest) { throw 'Rollback replacement failed.' }
            }
            $status = 'rolled_back'
        } catch { $status = 'rollback_failed' }
    }
    if ($pathsReady) {
        try { Save-Result $status $failedPhase } catch {}
    }
    if ($pathsReady -and $oldExited -and $status -ne 'rollback_failed') {
        try {
            if ((Get-Digest $Target) -cne $originalDigest) { throw 'Original application is not safe to restart.' }
            $newProcess = [YilaiUpdateNative]::StartNormal($Target)
            if ([YilaiUpdateNative]::ResumeThread($newProcess.thread) -eq [uint32]::MaxValue) {
                throw 'Could not restart the previous application.'
            }
        } catch {
            if ($null -ne $newProcess) { $null = [YilaiUpdateNative]::TerminateProcess($newProcess.process, 1) }
            $status = 'rollback_failed'
            try { Save-Result $status 'restart' } catch {}
        }
    }
    if ($status -eq 'rollback_failed') { $exitCode = 2 }
} finally {
    if ($null -ne $oldProcess) { $oldProcess.Dispose() }
    if ($null -ne $newProcess) {
        $null = [YilaiUpdateNative]::CloseHandle($newProcess.thread)
        $null = [YilaiUpdateNative]::CloseHandle($newProcess.process)
    }
    if ($pathsReady) {
        try { Remove-Stage } catch {}
    }
}
exit $exitCode
