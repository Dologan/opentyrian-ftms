@echo off
rem Double-click to play Tyrian with the exercise bike. Starts Flywheel if needed; see play-with-flywheel.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0play-with-flywheel.ps1" %*
