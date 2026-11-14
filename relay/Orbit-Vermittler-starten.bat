@echo off
title Orbit Vermittler - Fenster offen lassen
cd /d "%~dp0"
echo.
echo   Orbit Vermittler fuer PS3
echo   -------------------------
echo   Dieses Fenster offen lassen, solange ihr Orbit uebers Internet nutzt.
echo   Zusaetzlich muss playit.gg laufen (Tunnel auf 127.0.0.1:31400).
echo.
where python >nul 2>nul
if not errorlevel 1 goto python
where py >nul 2>nul
if not errorlevel 1 goto launcher
echo   Python wurde nicht gefunden.
echo   Bitte von https://www.python.org/downloads/ installieren und dabei
echo   "Add python.exe to PATH" anhaken. Danach diese Datei erneut starten.
goto end
:python
python orbit_relay.py --host 127.0.0.1 --port 31400 --state "%~dp0owners.json"
goto end
:launcher
py -3 orbit_relay.py --host 127.0.0.1 --port 31400 --state "%~dp0owners.json"
:end
echo.
echo   Der Vermittler wurde beendet.
pause
