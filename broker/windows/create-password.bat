@echo off
setlocal

pushd "%~dp0"

where mosquitto_passwd.exe >nul 2>nul
if %errorlevel%==0 (
  mosquitto_passwd.exe -c passwordfile birdnest
  popd
  exit /b %errorlevel%
)

if exist "C:\Program Files\mosquitto\mosquitto_passwd.exe" (
  "C:\Program Files\mosquitto\mosquitto_passwd.exe" -c passwordfile birdnest
  popd
  exit /b %errorlevel%
)

echo mosquitto_passwd.exe not found.
echo Install Mosquitto or add it to PATH.
popd
exit /b 1
