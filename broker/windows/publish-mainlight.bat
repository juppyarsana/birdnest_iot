@echo off
setlocal

set "STATE=%~1"
if "%STATE%"=="" set "STATE=on"

set "BROKER_HOST=%~2"
if "%BROKER_HOST%"=="" set "BROKER_HOST=192.168.1.4"

set "BROKER_PORT=%~3"
if "%BROKER_PORT%"=="" set "BROKER_PORT=1883"

set "MQTT_USER=%~4"
if "%MQTT_USER%"=="" set "MQTT_USER=birdnest"

set "MQTT_PASS=%~5"
if "%MQTT_PASS%"=="" set /p MQTT_PASS=Password: 

set "BASE_TOPIC=%~6"
if "%BASE_TOPIC%"=="" set "BASE_TOPIC=birdnest/controllers/birdnest-5C008B40C86C"

set "TOPIC=%BASE_TOPIC%/cmd/light"

set "PAYLOAD="
if /I "%STATE%"=="on" set "PAYLOAD={""light"":1,""state"":true}"
if /I "%STATE%"=="off" set "PAYLOAD={""light"":1,""state"":false}"
if "%PAYLOAD%"=="" (
  echo Invalid state. Use: on or off
  exit /b 1
)

pushd "%~dp0"

set "MOSQ_PUB=mosquitto_pub.exe"
where %MOSQ_PUB% >nul 2>nul
if not %errorlevel%==0 (
  if exist "C:\Program Files\mosquitto\mosquitto_pub.exe" (
    set "MOSQ_PUB=C:\Program Files\mosquitto\mosquitto_pub.exe"
  ) else (
    echo mosquitto_pub.exe not found.
    popd
    exit /b 1
  )
)

"%MOSQ_PUB%" -h %BROKER_HOST% -p %BROKER_PORT% -u %MQTT_USER% -P "%MQTT_PASS%" -t "%TOPIC%" -m "%PAYLOAD%"

popd
