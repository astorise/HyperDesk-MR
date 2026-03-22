# HyperDesk MR

A mixed-reality desktop streaming application for Meta Quest 3 that connects to a Windows host via RDP and renders up to 16 virtual monitors as OpenXR quad layers.

## Installation (Sideloading)

### Prerequisite: Enable Developer Mode on Meta Quest 3

1. Open the **Meta Horizon** mobile app and go to **Menu → Devices**.
2. Select your headset, tap **Developer Mode**, and toggle it on.
3. Put on your headset and accept the developer mode prompt if one appears.

### Method 1: ADB (Command Line)

Install [Android Platform Tools](https://developer.android.com/tools/releases/platform-tools) for your OS, then connect your Quest 3 via USB and run:

```sh
adb install -r <path_to_hyperdesk-mr.apk>
```

Accept the **Allow USB Debugging** dialog inside the headset when prompted.

### Method 2: Meta Quest Developer Hub (GUI)

1. Download and install [Meta Quest Developer Hub (MQDH)](https://developer.oculus.com/meta-quest-developer-hub/).
2. Connect your Quest 3 via USB and pair it in MQDH.
3. Open the **Device Manager** tab and drag-and-drop the `.apk` file onto the device panel to sideload it.
