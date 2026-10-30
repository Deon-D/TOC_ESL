[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheDir = Join-Path $projectRoot 'top\build\environment'
$stateFile = Join-Path $cacheDir 'hostState.json'
$msiFile = Join-Path $cacheDir 'wsl.3.0.3.0.x64.msi'
$manifestFile = Join-Path $cacheDir 'downloadManifest.json'
$state = [ordered]@{ status = 'starting'; restartRequired = $false; updatedAt = (Get-Date).ToString('o') }

function saveState {
    $state.updatedAt = (Get-Date).ToString('o')
    $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell. It does not restart Windows.'
}

New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
Start-Transcript -Path (Join-Path $cacheDir 'hostInstall.log') -Append | Out-Null
try {
    saveState
    $manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
    $actualHash = (Get-FileHash -LiteralPath $msiFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($manifest.wslDigest -ne ('sha256:' + $actualHash)) {
        throw 'The WSL installer checksum does not match the downloaded release manifest.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $msiFile
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
        throw 'The WSL installer must have a valid Microsoft Corporation signature.'
    }

    $state.status = 'installingWslPackage'
    saveState
    $msiLog = Join-Path $cacheDir 'wslMsi.log'
    $installArgs = '/i "{0}" /qn /norestart /L*v "{1}"' -f $msiFile, $msiLog
    $installer = Start-Process -FilePath 'msiexec.exe' -ArgumentList $installArgs -WindowStyle Hidden -Wait -PassThru
    $state.msiExitCode = $installer.ExitCode
    if ($installer.ExitCode -notin @(0, 3010)) {
        throw "WSL MSI installation failed with exit code $($installer.ExitCode). See $msiLog"
    }
    if ($installer.ExitCode -eq 3010) { $state.restartRequired = $true }

    $state.status = 'enablingVirtualMachinePlatform'
    saveState
    $feature = Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform
    if ($feature.State -ne 'Enabled') {
        if ($feature.State -eq 'EnablePending') {
            $state.restartRequired = $true
        } else {
            $result = Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -All -NoRestart
            if ($result.RestartNeeded) { $state.restartRequired = $true }
        }
    }
    $state.virtualMachinePlatform = (Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform).State.ToString()
    $state.status = if ($state.restartRequired) { 'restartRequired' } else { 'hostReady' }
    saveState
    $state | ConvertTo-Json
} catch {
    $state.status = 'failed'
    $state.error = $_.Exception.Message
    saveState
    Write-Error $_
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
