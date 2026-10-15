# Masqrayd Broadcast Timer — Windows quick start

1. Download **Masqrayd-Broadcast-Timer-1.0.0-Windows-x64** from the Artifacts section of a successful Windows build. GitHub requires you to be signed in to download Actions artifacts.
2. Right-click the downloaded ZIP and choose **Extract All**.
3. Close OBS completely.
4. Open the extracted folder, double-click **INSTALL.cmd**, and accept the Windows administrator prompt. No Python, CMake, or Visual Studio installation is needed.
5. Open OBS. The timer opens automatically. You can also use **Tools > Masqrayd Broadcast Timer**.

The package targets OBS Studio **32.2.2, Windows x64**. It has not been tested on other OBS versions or architectures. It uses OBS's own Qt libraries; do not copy Qt DLLs into OBS.

## During a broadcast

- **Start:** starts the current timer. After Pause or Stop, it becomes Resume and continues the same timer.
- **Pause / Resume:** freezes or continues timing.
- **Stop:** freezes the final reading.
- **Reset:** clears the current reading and prepares the saved duration without starting.
- **Next Story:** switches to story mode, advances the story number, clears elapsed time, and immediately starts the saved story duration.
- **Take a Break:** switches to a separate break countdown and starts immediately. Next Story returns to a fresh story timer.

Default story length is **10 minutes**; break length is **5 minutes**. The timer pulses red with **30 seconds remaining**, and continues red into overtime. Count-up mode shows elapsed time against the same target. Breaks always count down.

## Settings and window controls

**Right-click anywhere** to open timer settings or change Always on Top, Borderless Window, Open with OBS, or Count Up for Stories. Set durations in hours:minutes:seconds and warnings in seconds. A warning value of zero disables the advance warning; expiry still turns red. Disable pulsing for a steady red warning.

- Drag the timer numbers or date to move the window. Ctrl+drag also works over a button.
- Drag any edge or corner to resize. Buttons stay in their rows and text shrinks to fit.
- Right-click > Hide timer hides the window while timing continues. Reopen through OBS's Tools menu.
- To close the timer permanently for the current OBS session, hide it; to prevent automatic opening next time, turn off Open with OBS.
- Window position, dimensions, presets, and appearance save automatically. A disconnected monitor's position is moved back onto an available screen when you reopen the timer.
- Each OBS launch starts with a fresh, stopped timer. Presets are retained; an earlier timer is not silently resumed.

The small clock displays your computer's **local date and time**. Timer durations use a monotonic clock, so changes to your computer's date/time do not alter a running segment. Windows sleep/hibernation is outside the supported broadcast workflow; check/reset the timer after waking the PC.

The timer does not switch OBS scenes, pause recording, or put itself into a video source. Display Capture of the monitor containing the timer can include it. Optional alerts play through system audio and may be picked up by Desktop Audio; sound is off by default.

## Uninstall

Close OBS, then run **UNINSTALL.cmd**. This removes only this plugin. Saved preferences remain in OBS's plugin configuration folder.

## If it doesn't appear

Open **Help > Log Files > View Current Log** in OBS and search for `Masqrayd Broadcast Timer`. Confirm you installed the compiled Windows artifact rather than the repository source ZIP. The installed DLL should be at:

`C:\ProgramData\obs-studio\plugins\masqrayd-broadcast-timer\bin\64bit\masqrayd-broadcast-timer.dll`

Portable OBS installations may need the DLL copied to their own `obs-plugins\64bit` folder instead. The supplied installer targets the standard Windows OBS installation.
