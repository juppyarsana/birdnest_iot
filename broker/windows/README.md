# Mosquitto Broker (Windows)

This folder contains a ready-to-use Mosquitto configuration for the Birdnest LAN broker.

## Prerequisites

- Install Eclipse Mosquitto for Windows (includes `mosquitto.exe`, `mosquitto_pub.exe`, `mosquitto_sub.exe`, `mosquitto_passwd.exe`)
- Recommended: add the Mosquitto install folder to your PATH, or run commands using the full path (example below)

Common install location:

```
C:\Program Files\mosquitto\
```

## 1) Create the password file

Easiest: double-click:

- `create-password.bat`

Or run from this folder (`broker/windows/`):

```powershell
cd "C:\Users\dj03p\Documents\Glamping Kintamani\birdnest_iot\broker\windows"
mosquitto_passwd -c passwordfile birdnest
```

Enter a password when prompted. The `passwordfile` is not committed to git.

## 2) Start the broker

Easiest: double-click:

- `start-broker.bat`

Or run from this folder (`broker/windows/`):

```powershell
cd "C:\Users\dj03p\Documents\Glamping Kintamani\birdnest_iot\broker\windows"
mosquitto -c mosquitto.conf -v
```

If Mosquitto is not in your PATH, use the full path:

```powershell
cd "C:\Users\dj03p\Documents\Glamping Kintamani\birdnest_iot\broker\windows"
& "C:\Program Files\mosquitto\mosquitto.exe" -c mosquitto.conf -v
```

Notes:

- This config listens on `0.0.0.0:1883` (LAN).
- It uses relative paths (`passwordfile`, `aclfile`, `data/`, `log/`), so start Mosquitto with the working directory set to this folder.
- If Windows Firewall prompts you, allow Mosquitto on your Private network.

## 3) Quick test (publish/subscribe)

Terminal 1:

```powershell
mosquitto_sub -h 127.0.0.1 -p 1883 -u birdnest -P "<YOUR_PASSWORD>" -t birdnest/test
```

Terminal 2:

```powershell
mosquitto_pub -h 127.0.0.1 -p 1883 -u birdnest -P "<YOUR_PASSWORD>" -t birdnest/test -m "hello"
```

## 4) LAN test from another device

Replace `<BROKER_IP>` with your Windows PC IP on the LAN:

```powershell
mosquitto_sub -h <BROKER_IP> -p 1883 -u birdnest -P "<YOUR_PASSWORD>" -t birdnest/#
```

## 5) One-click test scripts (PowerShell not required)

These scripts default to:

- Broker: `192.168.1.4:1883`
- User: `birdnest`
- Controller base topic: `birdnest/controllers/birdnest-5C008B40C86C`

Subscribe to everything:

- Double-click `subscribe-all.bat`

Subscribe to this controller only:

- Double-click `subscribe-controller.bat`

Publish Main Light:

- Double-click `publish-mainlight.bat` and type `on` or `off` when prompted

Examples from a terminal (optional arguments):

```powershell
.\subscribe-controller.bat 192.168.1.4 1883 birdnest "<PASS>" "birdnest/controllers/birdnest-5C008B40C86C"
.\publish-mainlight.bat on 192.168.1.4 1883 birdnest "<PASS>" "birdnest/controllers/birdnest-5C008B40C86C"
.\publish-mainlight.bat off 192.168.1.4 1883 birdnest "<PASS>" "birdnest/controllers/birdnest-5C008B40C86C"
```

## Topic conventions used by controllers

- Controllers publish status (retained): `birdnest/controllers/<clientId>/status`
- Controllers subscribe to commands: `birdnest/controllers/<clientId>/cmd/#`
- Broadcast commands: `birdnest/cmd/all/#`

See `controller/ESP32_Room_Controller_API_Documentation.md` for payload formats.
