[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheDir = Join-Path $projectRoot 'top\build\environment'
$distro = 'TOC-ESL-Debian12'
$installDir = Join-Path $env:LOCALAPPDATA 'TOC_ESL\WSL\Debian12'
$rootfsGz = Join-Path $cacheDir 'debian12-rootfs.tar.gz'
$rootfsTar = Join-Path $cacheDir 'debian12-rootfs.tar'
$expectedHash = 'a4aa977c0d0eb2336dad68941e458308ebf32a717c19ea72e475b9eda8035506'
$rootfsUrl = 'https://raw.githubusercontent.com/debuerreotype/docker-debian-artifacts/69faaf492e77aff83e4c7fc401017c9d25ecde24/bookworm/oci/blobs/rootfs.tar.gz'
$wslExe = Join-Path $env:WINDIR 'System32\wsl.exe'
$stateFile = Join-Path $cacheDir 'environmentState.json'
$state = [ordered]@{ status = 'starting'; distro = $distro; installDir = $installDir; updatedAt = (Get-Date).ToString('o') }

function saveState {
    $state.updatedAt = (Get-Date).ToString('o')
    $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
}

function invokeWsl {
    param([string[]]$wslArgs)
    & $wslExe @wslArgs
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed with exit code $LASTEXITCODE."
    }
}

New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
Start-Transcript -Path (Join-Path $cacheDir 'environmentInstall.log') -Append | Out-Null
try {
    saveState
    $hostStateFile = Join-Path $cacheDir 'hostState.json'
    if (Test-Path -LiteralPath $hostStateFile) {
        $hostState = Get-Content -LiteralPath $hostStateFile -Raw | ConvertFrom-Json
        if ($hostState.restartRequired -or $hostState.restart_required) {
            $bootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
            $hostUpdatedAt = if ($hostState.updatedAt) { $hostState.updatedAt } else { $hostState.updated_at }
            if ($bootTime -le [datetime]$hostUpdatedAt) {
                $state.status = 'restartRequired'
                saveState
                throw 'Restart Windows, then run this script again. The script does not restart Windows automatically.'
            }
        }
    }
    invokeWsl -wslArgs @('--version')
    $names = & $wslExe --list --quiet
    if ($LASTEXITCODE -ne 0) { throw 'WSL is not ready. Complete setupWslHost.ps1 and any required restart first.' }
    $registered = @($names | ForEach-Object { ($_ -replace "`0", '').Trim() })
    if ($registered -notcontains $distro) {
        $state.status = 'preparingDebianRootfs'
        saveState
        if (-not (Test-Path -LiteralPath $rootfsGz)) {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $rootfsUrl -OutFile ($rootfsGz + '.download') -TimeoutSec 600 -UseBasicParsing
            $downloadHash = (Get-FileHash -LiteralPath ($rootfsGz + '.download') -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($downloadHash -ne $expectedHash) { throw 'Downloaded Debian rootfs checksum mismatch.' }
            Move-Item -LiteralPath ($rootfsGz + '.download') -Destination $rootfsGz -Force
        }
        if ((Get-FileHash -LiteralPath $rootfsGz -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
            throw 'Cached Debian rootfs checksum mismatch.'
        }
        $inputStream = [IO.File]::OpenRead($rootfsGz)
        try {
            $gzip = New-Object IO.Compression.GZipStream($inputStream, [IO.Compression.CompressionMode]::Decompress)
            try {
                $outputStream = [IO.File]::Create($rootfsTar)
                try { $gzip.CopyTo($outputStream) } finally { $outputStream.Dispose() }
            } finally { $gzip.Dispose() }
        } finally { $inputStream.Dispose() }
        if ((Test-Path -LiteralPath $installDir) -and (Get-ChildItem -LiteralPath $installDir -Force | Select-Object -First 1)) {
            throw "The unregistered install directory is not empty: $installDir. Inspect it before retrying."
        }
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
        $state.status = 'importingDebian12'
        saveState
        invokeWsl -wslArgs @('--import', $distro, $installDir, $rootfsTar, '--version', '2')
    }

    $linuxRootOutput = & $wslExe -d $distro -u root --exec wslpath -a -u $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not resolve the project path inside WSL.' }
    $linuxRoot = ($linuxRootOutput -join "`n").Trim()
    $userBeforeSetup = & $wslExe -d $distro --exec id -un
    if ($LASTEXITCODE -ne 0) { throw 'Could not determine the current default Linux user.' }
    $state.status = 'installingToolchain'
    saveState
    invokeWsl -wslArgs @('-d', $distro, '-u', 'root', '--exec', 'bash', "$linuxRoot/scrpits/setupDebian12.sh")
    # Apply the initial default-user change before CMake uses the Windows drive.
    # A drive first mounted as root can reject chmod from the new esl user.
    if (($userBeforeSetup -join "`n").Trim() -ne 'esl') {
        invokeWsl -wslArgs @('--terminate', $distro)
    }
    $state.status = 'checkingToolchain'
    saveState
    invokeWsl -wslArgs @('-d', $distro, '-u', 'esl', '--exec', 'bash', "$linuxRoot/scrpits/checkToolchain.sh")
    $state.status = 'ready'
    $state.compiler = 'GCC/G++ 12.2.x'
    $state.test = 'gcc12Cpp17Runtime passed'
    saveState
    Write-Output 'Environment ready. Enter it with: wsl -d TOC-ESL-Debian12 -u esl'
} catch {
    if ($state.status -ne 'restartRequired') { $state.status = 'failed' }
    $state.error = $_.Exception.Message
    saveState
    Write-Error $_
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
