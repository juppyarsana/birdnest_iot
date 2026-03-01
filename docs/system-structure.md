# System Structure (5 Rooms)

## Components

- Controller (ESP32): one per room, drives lights/devices/IR/RGB and publishes status via MQTT.
- Android tablet app (planned): subscribes to status and publishes commands via MQTT.
- MQTT broker (Windows): central message hub for the site LAN.

## Room Model

- 5 rooms: `room01`..`room05`
- Each room has exactly one ESP32 controller.
- Controllers identify themselves by MQTT client id: `birdnest-<deviceId>`.

## Topic Model

The controller firmware uses a per-device base topic:

- Base topic: `birdnest/controllers/<clientId>`
- Status (retained): `<base>/status`
- Availability (retained): `<base>/availability`
- Commands: `<base>/cmd/<command>`

For “room-first” UI, keep a mapping file (see `config/rooms.example.json`) to map:

- `room_id` → `controller_base_topic`

That way the Android app can be written against `room_id` while still targeting the correct controller topics.

## Recommended Deployment Notes

- Put the broker and Android tablets on the same LAN/VLAN as the ESP32 controllers.
- Use broker ACLs so each tablet can publish only to expected command topics.
- Use retained status so tablets can show the last known state immediately after reconnect.
