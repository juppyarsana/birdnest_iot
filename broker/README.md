# MQTT Broker (Windows)

This folder is for running the MQTT broker on a Windows PC in your glamping site LAN.

## Recommendation

- Broker: Eclipse Mosquitto
- Port: 1883 (LAN only)

## Windows Mosquitto Config (Recommended)

Use the files under `broker/windows/`:

- `broker/windows/mosquitto.conf`
- `broker/windows/aclfile`
- `broker/windows/passwordfile` (generated locally, not committed)

### Create Password File

Run this from the `broker/windows/` folder:

```
mosquitto_passwd -c passwordfile birdnest
```

Then type the password when prompted.

### Start Broker (Console)

Run this from the `broker/windows/` folder:

```
mosquitto -c mosquitto.conf -v
```

### Quick Test

In one terminal:

```
mosquitto_sub -h 127.0.0.1 -p 1883 -u birdnest -P kintamani -t birdnest/test
```

In another terminal:

```
mosquitto_pub -h 127.0.0.1 -p 1883 -u birdnest -P kintamani -t birdnest/test -m hello
```

## Topic Convention Used by Controllers

- Controllers publish status on: `birdnest/controllers/<clientId>/status` (retained)
- Controllers listen on: `birdnest/controllers/<clientId>/cmd/#`

See `controller/ESP32_Room_Controller_API_Documentation.md` for full command payloads.
