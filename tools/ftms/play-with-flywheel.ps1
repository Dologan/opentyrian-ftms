# play-with-flywheel.ps1: play Tyrian with the exercise bike through the Flywheel tray app.
#
# No arguments and no console needed: double-click "Play with Flywheel.cmd". It starts Flywheel if it is
# installed and not running, waits for it, then starts the game with --ftms-bridge=127.0.0.1:8787.
# Flywheel is left running when the game exits (it lives in the tray; quit it from there).
# Problems are shown in message boxes, never only in a console.
#
# The game is opentyrian.exe next to this script (the Windows package) or at the repository root.
# Anything passed to this script goes to the game unchanged.

param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $GameArgs = @())

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

function Show-Box([string] $Text, [string] $Buttons = 'OK') {
    return [System.Windows.Forms.MessageBox]::Show($Text, 'OpenTyrian with Flywheel', $Buttons, 'Information')
}

function Test-Flywheel {
    try {
        $r = Invoke-WebRequest -Uri 'http://127.0.0.1:8787/healthz' -UseBasicParsing -TimeoutSec 2
        return ($r.StatusCode -eq 200 -and $r.Content -match '"app"\s*:\s*"flywheel"')
    } catch { return $false }
}

function Find-FlywheelExe {
    $dirs = @()
    try { $dirs += (Get-ItemProperty -Path 'HKCU:\Software\Flywheel' -ErrorAction Stop).InstallDir } catch { }
    $dirs += (Join-Path $env:LOCALAPPDATA 'Programs\Flywheel')
    foreach ($d in $dirs) {
        if ($d) {
            $p = Join-Path $d 'flywheel.exe'
            if (Test-Path -LiteralPath $p) { return $p }
        }
    }
    return $null
}

try {
    $Game = Join-Path $PSScriptRoot 'opentyrian.exe'
    if (-not (Test-Path -LiteralPath $Game)) {
        $Game = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'opentyrian.exe'
    }
    if (-not (Test-Path -LiteralPath $Game)) {
        Show-Box "opentyrian.exe was not found next to this script. Unpack the whole Windows package and run it from there."
        exit 1
    }

    $withBike = $true
    if (-not (Test-Flywheel)) {
        $exe = Find-FlywheelExe
        if ($exe) {
            Start-Process -FilePath $exe
            for ($i = 0; $i -lt 40 -and -not (Test-Flywheel); $i++) { Start-Sleep -Milliseconds 500 }
        }
        if (-not (Test-Flywheel)) {
            $what = if ($exe) { "Flywheel was started but did not answer on port 8787." } else { "Flywheel is not installed." }
            $answer = Show-Box "$what`n`nPlay without the exercise bike?" 'YesNo'
            if ($answer -ne 'Yes') { exit 1 }
            $withBike = $false
        }
    }

    $argList = @()
    if ($withBike) { $argList += '--ftms-bridge=127.0.0.1:8787' }
    $argList += $GameArgs
    Start-Process -FilePath $Game -ArgumentList $argList -WorkingDirectory (Split-Path -Parent $Game) -Wait
} catch {
    Show-Box ("Could not start the game: " + $_.Exception.Message)
    exit 1
}
