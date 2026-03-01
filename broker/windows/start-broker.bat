@echo off
setlocal

pushd "%~dp0"

if not exist "mosquitto.conf" (
  echo mosquitto.conf not found in %CD%
  popd
  exit /b 1
)

if not exist "passwordfile" (
  echo passwordfile not found in %CD%
  echo Run: mosquitto_passwd -c passwordfile birdnest
  popd
  exit /b 1
)

where mosquitto.exe >nul 2>nul
if %errorlevel%==0 (
  mosquitto.exe -c "mosquitto.conf" -v
  popd
  exit /b %errorlevel%
)

if exist "C:\Program Files\mosquitto\mosquitto.exe" (
  "C:\Program Files\mosquitto\mosquitto.exe" -c "mosquitto.conf" -v
  popd
  exit /b %errorlevel%
)

echo mosquitto.exe not found.
echo Install Mosquitto or add it to PATH.
popd
exit /b 1
