@echo off
rem Start Tyrian with the exercise bike. Options: [--flywheel PATH] [--source sim|ble] [--game PATH] [-- GAME_ARGS...]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play-with-bike.ps1" %*
if errorlevel 1 pause
