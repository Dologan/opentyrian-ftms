# play-with-flywheel.ps1: play Tyrian with the exercise bike through the Flywheel tray app.
#
# No arguments and no console needed: double-click "Play with Flywheel.cmd". It starts Flywheel if it is
# installed and not running, waits for it, then starts the game with --ftms-bridge=127.0.0.1:<port>.
# The port comes from Flywheel's runtime.json (confirmed by /healthz); 8585 is the fallback.
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

$DefaultPort = 8585

# Flywheel writes runtime.json in its data folder while it runs (and removes it on a clean exit).
function Get-RuntimePath {
    if ($env:FLYWHEEL_DATA_DIR) { return (Join-Path $env:FLYWHEEL_DATA_DIR 'runtime.json') }
    return (Join-Path $env:LOCALAPPDATA 'Flywheel\runtime.json')
}

# The port named by runtime.json, or $null when the file is missing, unreadable or not Flywheel's.
function Get-RuntimePort {
    try {
        $p = Get-RuntimePath
        if (-not (Test-Path -LiteralPath $p)) { return $null }
        $j = Get-Content -LiteralPath $p -Raw -ErrorAction Stop | ConvertFrom-Json
        if ($j.app -ne 'flywheel') { return $null }
        $n = [int]$j.port
        if ($n -lt 1 -or $n -gt 65535) { return $null }
        return $n
    } catch { return $null }
}

# Mode string ('tray', 'headless', 'conflict', or '' when the answer has none) if Flywheel answers
# /healthz on this port, else $null.
function Get-Health([int] $Port) {
    try {
        $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/healthz" -UseBasicParsing -TimeoutSec 2
        if ($r.StatusCode -ne 200) { return $null }
        $j = $r.Content | ConvertFrom-Json
        if ($j.app -ne 'flywheel') { return $null }
        if ($j.mode) { return [string]$j.mode }
        return ''
    } catch { return $null }
}

# A running Flywheel as @{ Port = ...; Mode = ... }, or $null. runtime.json is only a hint (a crash
# can leave it stale), so the port it names must answer /healthz; otherwise the default port is probed
# (also the case for the Python bridge, which keeps its runtime.json in its own data folder).
function Find-Flywheel {
    $port = Get-RuntimePort
    if ($port) {
        $mode = Get-Health $port
        if ($mode -ne $null) { return @{ Port = $port; Mode = $mode } }
    }
    if ($port -ne $DefaultPort) {
        $mode = Get-Health $DefaultPort
        if ($mode -ne $null) { return @{ Port = $DefaultPort; Mode = $mode } }
    }
    return $null
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
    $port = $DefaultPort
    $fw = Find-Flywheel
    if (-not $fw) {
        $exe = Find-FlywheelExe
        if ($exe) {
            Start-Process -FilePath $exe
            for ($i = 0; $i -lt 40 -and -not $fw; $i++) {
                Start-Sleep -Milliseconds 500
                $fw = Find-Flywheel
            }
        }
        if (-not $fw) {
            $what = if ($exe) { "Flywheel was started but did not answer." } else { "Flywheel is not installed." }
            $answer = Show-Box "$what`n`nPlay without the exercise bike?" 'YesNo'
            if ($answer -ne 'Yes') { exit 1 }
            $withBike = $false
        }
    }
    if ($fw -and $fw.Mode -eq 'conflict') {
        $answer = Show-Box ("Flywheel could not use its port, because another program is using it, so it cannot connect to the bike. " +
            "Open Flywheel's page at http://127.0.0.1:" + $fw.Port + "/ to fix it.`n`nPlay without the exercise bike?") 'YesNo'
        if ($answer -ne 'Yes') { exit 1 }
        $withBike = $false
    }
    if ($withBike) { $port = $fw.Port }

    $argList = @()
    if ($withBike) { $argList += ('--ftms-bridge=127.0.0.1:' + $port) }
    $argList += $GameArgs
    Start-Process -FilePath $Game -ArgumentList $argList -WorkingDirectory (Split-Path -Parent $Game) -Wait
} catch {
    Show-Box ("Could not start the game: " + $_.Exception.Message)
    exit 1
}
