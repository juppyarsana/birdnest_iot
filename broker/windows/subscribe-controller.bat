@echo off
setlocal

set "BROKER_HOST=%~1"
if "%BROKER_HOST%"=="" set "BROKER_HOST=192.168.1.4"

set "BROKER_PORT=%~2"
if "%BROKER_PORT%"=="" set "BROKER_PORT=1883"

set "MQTT_USER=%~3"
if "%MQTT_USER%"=="" set "MQTT_USER=birdnest"

set "MQTT_PASS=%~4"
if "%MQTT_PASS%"=="" set /p MQTT_PASS=Password: 

set "BASE_TOPIC=%~5"
if "%BASE_TOPIC%"=="" set "BASE_TOPIC=birdnest/controllers/birdnest-5C008B40C86C"

pushd "%~dp0"

set "MOSQ_SUB=mosquitto_sub.exe"
where %MOSQ_SUB% >nul 2>nul
if not %errorlevel%==0 (
  if exist "C:\Program Files\mosquitto\mosquitto_sub.exe" (
    set "MOSQ_SUB=C:\Program Files\mosquitto\mosquitto_sub.exe"
  ) else (
    echo mosquitto_sub.exe not found.
    popd
    exit /b 1
  )
)

"%MOSQ_SUB%" -h %BROKER_HOST% -p %BROKER_PORT% -u %MQTT_USER% -P "%MQTT_PASS%" -t "%BASE_TOPIC%/#" -v

popd
