# play-with-bike.ps1: start the Flywheel bridge, wait for it, play Tyrian with the bike,
# then stop the bridge when the game exits.
#
# Usage (from play-with-bike.cmd): [--flywheel PATH] [--source sim|ble] [--game PATH] [-- GAME_ARGS...]
#   --flywheel PATH  Flywheel checkout. Default: $env:FLYWHEEL_DIR, else ..\flywheel next to this repo.
#   --source SRC     sim: simulated bike, no hardware. ble: real FTMS bike over Bluetooth.
#   --game PATH      game executable. Default: opentyrian.exe at the repo root, or $env:OPENTYRIAN_BIN.
#   -- GAME_ARGS...  everything after -- is passed to the game unchanged.
#
# The bike is requested with --ftms-bridge=127.0.0.1:8787 on the command line only.
# Requires uv on PATH. Untested on Windows at the time of writing.

param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest = @()
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Port = 8787
$BridgeAddr = "127.0.0.1:$Port"
$HealthUrl = "http://127.0.0.1:$Port/healthz"
$DataRoot = Join-Path $env:LOCALAPPDATA 'OpenTyrian-FTMS'
$LogDir = Join-Path $DataRoot 'logs'
$DataDir = Join-Path $DataRoot 'bridge-data'

$Flywheel = $env:FLYWHEEL_DIR
$Source = ''
$Game = $env:OPENTYRIAN_BIN
$GameArgs = @()

function Get-Value([int] $Index) {
    if ($Index -ge $Rest.Count) { throw "$($Rest[$Index - 1]) needs a value" }
    return $Rest[$Index]
}

function Test-Health {
    try {
        $response = Invoke-WebRequest -Uri $HealthUrl -UseBasicParsing -TimeoutSec 2
        return ($response.StatusCode -eq 200)
    } catch {
        return $false
    }
}

function Quote-Arg([string] $Value) {
    if ($Value -match '\s') { return '"' + $Value + '"' }
    return $Value
}

$Bridge = $null
$Exit = 0
try {
    $i = 0
    while ($i -lt $Rest.Count) {
        switch ($Rest[$i]) {
            '--flywheel' { $Flywheel = Get-Value ($i + 1); $i += 2 }
            '--source'   { $Source = Get-Value ($i + 1); $i += 2 }
            '--game'     { $Game = Get-Value ($i + 1); $i += 2 }
            '--'         {
                if ($i + 1 -lt $Rest.Count) { $GameArgs = @($Rest[($i + 1)..($Rest.Count - 1)]) }
                $i = $Rest.Count
            }
            default      { throw "unknown option: $($Rest[$i]) (game options go after --)" }
        }
    }

    switch ($Source) {
        ''      { $BridgeSource = '' }
        'sim'   { $BridgeSource = 'sim' }
        'ble'   { $BridgeSource = 'ftms' }
        default { throw '--source must be sim or ble' }
    }

    if (-not $Flywheel) { $Flywheel = Join-Path (Split-Path -Parent $Root) 'flywheel' }
    $BridgeDir = Join-Path $Flywheel 'bridge'
    if (-not (Test-Path -LiteralPath (Join-Path $BridgeDir 'pyproject.toml'))) {
        throw "no Flywheel checkout at $Flywheel (use --flywheel PATH or set FLYWHEEL_DIR)"
    }
    $BridgeDir = (Resolve-Path -LiteralPath $BridgeDir).Path

    if (-not $Game) { $Game = Join-Path $Root 'opentyrian.exe' }
    if (-not (Test-Path -LiteralPath $Game)) {
        throw "game not found: $Game (build it, or use --game PATH)"
    }
    $Game = (Resolve-Path -LiteralPath $Game).Path

    $Uv = Get-Command uv -ErrorAction SilentlyContinue
    if (-not $Uv) { throw 'needs uv (https://docs.astral.sh/uv/)' }

    if (Test-Health) {
        throw "something already answers at $HealthUrl; quit the other bridge first"
    }

    New-Item -ItemType Directory -Force -Path $LogDir, $DataDir | Out-Null
    $Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $LogOut = Join-Path $LogDir "bridge-$Stamp.log"
    $LogErr = Join-Path $LogDir "bridge-$Stamp.err.log"

    $BridgeArgs = @('run', '--project', $BridgeDir, 'flywheel-bridge', 'run', '--data-dir', $DataDir)
    if ($BridgeSource) { $BridgeArgs += @('--source', $BridgeSource) }
    $Quoted = @($BridgeArgs | ForEach-Object { Quote-Arg $_ })

    Write-Host "play-with-bike: starting bridge from $BridgeDir (log: $LogOut)"
    $env:PYTHONUNBUFFERED = '1'
    $Bridge = Start-Process -FilePath $Uv.Source -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $LogOut -RedirectStandardError $LogErr

    $Healthy = $false
    $Deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $Deadline) {
        if (Test-Health) { $Healthy = $true; break }
        if ($Bridge.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $Healthy) {
        throw "the bridge did not answer $HealthUrl within 30 s. See $LogOut and $LogErr"
    }
    Write-Host "play-with-bike: bridge healthy at $HealthUrl (pid $($Bridge.Id))"

    $GameLine = @('--ftms-bridge=' + $BridgeAddr) + $GameArgs
    Write-Host "play-with-bike: launching $Game $($GameLine -join ' ')"
    $GameProcess = Start-Process -FilePath $Game -ArgumentList @($GameLine | ForEach-Object { Quote-Arg $_ }) `
        -WorkingDirectory $Root -PassThru
    $GameProcess.WaitForExit()
    $Exit = $GameProcess.ExitCode
} catch {
    Write-Host ('play-with-bike: ' + $_.Exception.Message) -ForegroundColor Red
    $Exit = 1
} finally {
    if ($Bridge -and -not $Bridge.HasExited) {
        # taskkill /T also removes uv's child python process.
        & taskkill.exe /T /F /PID $Bridge.Id 2>&1 | Out-Null
        Write-Host 'play-with-bike: bridge stopped'
    }
}
exit $Exit
