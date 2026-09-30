[CmdletBinding(SupportsShouldProcess)]
param([switch]$Publish)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    function GitChecked([string[]]$Arguments) {
        $result = & git -c "safe.directory=$repo" @Arguments
        if ($LASTEXITCODE -ne 0) { throw "Git failed: $($Arguments[0])" }
        return $result
    }
    if ((Get-Item model-catalog.json).Length -gt 4MB) { throw 'Model catalog exceeds the client limit' }
    $catalog = Get-Content model-catalog.json -Raw | ConvertFrom-Json -Depth 100
    if (-not $catalog.models -or $catalog.models.Count -gt 100) { throw 'Invalid model catalog' }
    $seen = [Collections.Generic.HashSet[string]]::new()
    foreach ($model in $catalog.models) {
        if ($model.slug -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$' -or
            -not $seen.Add($model.slug) -or -not $model.display_name -or
            $model.display_name -isnot [string] -or
            $model.context_window -isnot [long] -or $model.context_window -le 0 -or
            $model.supported_reasoning_levels -isnot [array] -or
            $model.model_messages -isnot [pscustomobject]) { throw 'Invalid/duplicate model metadata' }
    }
    $before = (GitChecked @('show', 'HEAD:model-catalog.json') | Out-String) | ConvertFrom-Json -Depth 100
    foreach ($model in $before.models) {
        if (-not $seen.Contains($model.slug)) { throw "Catalog removes model: $($model.slug)" }
    }
    $current = Get-Content model-channel.json -Raw | ConvertFrom-Json
    $revision = [long]$current.revision + 1
    if (-not $Publish) {
        Write-Output "Validated $($catalog.models.Count) models. Next revision: $revision. Use -Publish to publish catalog only."
        return
    }
    if ((GitChecked @('branch','--show-current')) -ne 'main') { throw 'Catalog publishing requires main' }
    $changes = @(GitChecked @('status','--porcelain'))
    if (@($changes | Where-Object { $_.Substring(3) -ne 'model-catalog.json' }).Count) {
        throw 'Commit unrelated changes before catalog publishing; nothing was modified'
    }
    if (-not $PSCmdlet.ShouldProcess('origin/main', "Publish model catalog revision $revision")) { return }
    GitChecked @('fetch','origin','main') | Out-Null
    if ((GitChecked @('rev-parse','HEAD')) -ne (GitChecked @('rev-parse','origin/main'))) {
        throw 'Local main differs from origin/main; review first'
    }
    if ($changes.Count) {
        GitChecked @('add','model-catalog.json') | Out-Null
        GitChecked @('commit','-m',"models: publish catalog revision $revision") | Out-Null
    }
    $commit = GitChecked @('rev-parse','HEAD')
    # Hash Git's LF-normalized blob, not the Windows checkout bytes.
    $normalized = (GitChecked @('show',"${commit}:model-catalog.json")) -join "`n"
    $normalized += "`n"
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($normalized))).ToLowerInvariant()
    GitChecked @('push','origin','main') | Out-Null
    $catalogURL = "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/$commit/model-catalog.json"
    $download = Invoke-WebRequest $catalogURL -Headers @{'Cache-Control'='no-cache'}
    $bytes = $download.RawContentStream.ToArray()
    $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($actual -ne $digest) { throw 'Immutable catalog hash does not match; channel was not advanced' }
    $manifest = [ordered]@{schema_version=1;revision=$revision;sha256=$digest;url=$catalogURL}
    $json = ($manifest | ConvertTo-Json) + "`n"
    [IO.File]::WriteAllText((Join-Path $repo 'model-channel.json'),$json,[Text.UTF8Encoding]::new($false))
    GitChecked @('add','model-channel.json') | Out-Null
    GitChecked @('commit','-m',"models: advance channel to revision $revision") | Out-Null
    GitChecked @('push','origin','main') | Out-Null
    $remote = Invoke-RestMethod 'https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main/model-channel.json' -Headers @{'Cache-Control'='no-cache'}
    if ($remote.revision -ne $revision -or $remote.sha256 -ne $digest) { throw 'Published channel has not been verified; retry verification, not a new revision' }
    Write-Output "Published model catalog revision $revision. No software release was created."
} finally { Pop-Location }
