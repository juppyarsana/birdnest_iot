# ESP32 Room Controller API Documentation

## Overview

This document provides complete API documentation for the ESP32 Room Controller (Birdnest IoT v1.5) for Android app development. The device operates in regular station mode after initial WiFi setup through the ESP32's web interface.

## Base Configuration

- **Protocol**: HTTP
- **Port**: 80
- **Base URL**: `http://<device_ip>/api/`
- **Content-Type**: `application/x-www-form-urlencoded`
- **Device Name**: Birdnest Room Controller
- **Manufacturer**: Orca Technology

## Hardware Capabilities

- **220V Lights**: 3 controllable lights (relays)
- **Other Devices**: 2 controllable devices (relays)
- **Onboard LED**: ESP32 built-in LED
- **RGB LED**: Full color control (R, G, B: 0-255)
- **IR Remote**: Learn and send up to 10 IR commands
- **Display**: OLED status display

## API Endpoints

### 1. System Status

**Endpoint**: `GET /api/status`

**Description**: Retrieves current system status and all device states.

**Response**:
```json
{
  "wifi": {
    "connected": true,
    "ssid": "YourWiFiName",
    "ip": "192.168.1.100",
    "mode": "STA"
  },
  "lights": [false, true, false],
  "devices": [true, false],
  "onboard_led": false,
  "rgb": [255, 128, 0],
  "ir": {
    "learning": false,
    "commands": 3,
    "max_commands": 10
  }
}
```

**Response Fields**:
- `wifi.connected`: Boolean - WiFi connection status
- `wifi.ssid`: String - Connected network name
- `wifi.ip`: String - Device IP address
- `lights`: Array[3] - Light states (true=ON, false=OFF)
- `devices`: Array[2] - Device states (true=ON, false=OFF)
- `onboard_led`: Boolean - Onboard LED state
- `rgb`: Array[3] - RGB values [R, G, B]
- `ir.learning`: Boolean - IR learning mode status
- `ir.commands`: Integer - Number of stored IR commands

### 2. Light Control (220V Lights)

**Endpoint**: `POST /api/light`

**Description**: Controls 220V lights connected to relay outputs.

**Parameters**:
- `light`: Integer (1-3) - Light number to control
- `state`: String ("true"/"false") - Desired state

**Example Request**:
```
POST /api/light
Content-Type: application/x-www-form-urlencoded

light=1&state=true
```

**Success Response**:
```json
{"success": true}
```

**Error Response**:
```json
{"error": "Invalid light number"}
```

### 3. Device Control (Other Devices)

**Endpoint**: `POST /api/device`

**Description**: Controls other devices connected to relay outputs.

**Parameters**:
- `device`: Integer (1-2) - Device number to control
- `state`: String ("true"/"false") - Desired state

**Example Request**:
```
POST /api/device
Content-Type: application/x-www-form-urlencoded

device=2&state=false
```

**Success Response**:
```json
{"success": true}
```

### 4. Onboard LED Control

**Endpoint**: `POST /api/led`

**Description**: Controls the ESP32's built-in LED.

**Parameters**:
- `state`: String ("true"/"false") - LED state

**Example Request**:
```
POST /api/led
Content-Type: application/x-www-form-urlencoded

state=true
```

### 5. RGB LED Control

**Endpoint**: `POST /api/rgb`

**Description**: Controls RGB LED color and brightness.

**Parameters**:
- `r`: Integer (0-255) - Red component
- `g`: Integer (0-255) - Green component
- `b`: Integer (0-255) - Blue component

**Example Request**:
```
POST /api/rgb
Content-Type: application/x-www-form-urlencoded

r=255&g=0&b=128
```

**Common RGB Values**:
- White: `r=255&g=255&b=255`
- Red: `r=255&g=0&b=0`
- Green: `r=0&g=255&b=0`
- Blue: `r=0&g=0&b=255`
- Off: `r=0&g=0&b=0`

### 6. IR Remote Control

#### 6.1 Send IR Command

**Endpoint**: `POST /api/ir/send`

**Description**: Sends a previously learned IR command.

**Parameters**:
- `slot`: Integer (0-9) - Command slot number

**Example Request**:
```
POST /api/ir/send
Content-Type: application/x-www-form-urlencoded

slot=0
```

**Success Response**:
```json
{"success": true, "message": "Command sent"}
```

#### 6.2 List IR Commands

**Endpoint**: `GET /api/ir/list`

**Description**: Lists all stored IR commands.

**Response**:
```json
{
  "commands": [
    {
      "slot": 0,
      "name": "CMD_0",
      "type": "raw"
    },
    {
      "slot": 1,
      "name": "CMD_1",
      "type": "raw"
    }
  ],
  "count": 2,
  "max": 10
}
```

#### 6.3 Learn IR Command

**Endpoint**: `POST /api/ir/learn`

**Description**: Starts IR learning mode to capture a new remote signal.

**Parameters** (Optional):
- `slot`: Integer (0-9) - Specific slot to store command

**Example Request**:
```
POST /api/ir/learn
Content-Type: application/x-www-form-urlencoded

slot=3
```

**Response**:
```json
{"success": true, "message": "Learning mode started"}
```

**Usage**: After calling this endpoint, point the remote at the ESP32 and press the desired button within 10 seconds.

#### 6.4 Stop Learning Mode

**Endpoint**: `POST /api/ir/stop`

**Description**: Stops IR learning mode.

**Response**:
```json
{"success": true, "message": "Learning mode stopped"}
```

#### 6.5 Clear All IR Commands

**Endpoint**: `POST /api/ir/clear`

**Description**: Clears all stored IR commands.

**Response**:
```json
{"success": true, "message": "All commands cleared"}
```

## Android Implementation Guide

### Required Permissions

Add these permissions to your `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

### HTTP Client Setup (Java/Kotlin)

#### Using OkHttp (Recommended)

```java
OkHttpClient client = new OkHttpClient.Builder()
    .connectTimeout(10, TimeUnit.SECONDS)
    .readTimeout(10, TimeUnit.SECONDS)
    .build();

// Example: Turn on Light 1
RequestBody formBody = new FormBody.Builder()
    .add("light", "1")
    .add("state", "true")
    .build();

Request request = new Request.Builder()
    .url("http://" + deviceIP + "/api/light")
    .post(formBody)
    .build();

client.newCall(request).enqueue(new Callback() {
    @Override
    public void onResponse(Call call, Response response) {
        // Handle success
    }
    
    @Override
    public void onFailure(Call call, IOException e) {
        // Handle error
    }
});
```

#### Using Retrofit

```java
public interface ESP32API {
    @FormUrlEncoded
    @POST("light")
    Call<ResponseBody> controlLight(
        @Field("light") int lightNumber,
        @Field("state") String state
    );
    
    @GET("status")
    Call<SystemStatus> getStatus();
    
    @FormUrlEncoded
    @POST("rgb")
    Call<ResponseBody> setRGB(
        @Field("r") int red,
        @Field("g") int green,
        @Field("b") int blue
    );
}
```

### Real-time Status Updates

Implement periodic status polling:

```java
// Poll status every 3-5 seconds
Handler handler = new Handler();
Runnable statusUpdater = new Runnable() {
    @Override
    public void run() {
        updateDeviceStatus();
        handler.postDelayed(this, 3000); // 3 second interval
    }
};
```

### Error Handling Best Practices

1. **Network Timeouts**: Set appropriate timeout values
2. **Connection Failures**: Implement retry logic
3. **Invalid Responses**: Validate JSON responses
4. **Device Offline**: Show connection status to user
5. **User Feedback**: Provide loading indicators and error messages

### UI Components Needed

1. **Dashboard Screen**:
   - Connection status indicator
   - Device IP display
   - Quick status overview

2. **Light Control**:
   - 3 toggle switches for lights
   - Status indicators (ON/OFF)

3. **Device Control**:
   - 2 toggle switches for devices
   - Onboard LED toggle

4. **RGB Control**:
   - Color picker or RGB sliders
   - Brightness control
   - Color preview
   - Preset color buttons

5. **IR Remote**:
   - Grid of 10 programmable buttons
   - Learn mode indicator
   - Button labels showing stored commands
   - Clear all button

### Sample App Structure

```
app/
├── MainActivity.java          // Main dashboard
├── LightControlActivity.java  // Light controls
├── DeviceControlActivity.java // Device controls
├── RGBControlActivity.java    // RGB LED control
├── IRRemoteActivity.java      // IR remote interface
├── SettingsActivity.java      // Device IP configuration
└── models/
    ├── SystemStatus.java      // Status response model
    └── IRCommand.java         // IR command model
```

### Testing Checklist

- [ ] Device discovery and connection
- [ ] All light controls (3 lights)
- [ ] All device controls (2 devices + onboard LED)
- [ ] RGB color control and presets
- [ ] IR learning and sending
- [ ] Status updates and real-time sync
- [ ] Error handling and recovery
- [ ] Network timeout handling
- [ ] UI responsiveness

## Troubleshooting

### Common Issues

1. **Connection Refused**: Check device IP and network connectivity
2. **Timeout Errors**: Increase timeout values or check WiFi signal
3. **Invalid Responses**: Verify Content-Type header
4. **IR Learning Fails**: Ensure remote is pointed at ESP32 IR receiver
5. **Commands Not Working**: Check parameter names and values

### Debug Tips

- Use network monitoring tools to inspect HTTP requests
- Check ESP32 serial output for error messages
- Verify device is on same network as Android device
- Test API endpoints using tools like Postman first

---

**Document Version**: 1.0  
**Last Updated**: December 2024  
**Compatible with**: ESP32 Room Controller v1.5  
**Author**: Orca Technology