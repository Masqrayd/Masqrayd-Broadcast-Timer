# Masqrayd Broadcast Stack + Timer — Windows quick start

1. Download **Masqrayd-Broadcast-Stack-1.1.2-Windows-x64** from the Artifacts section of a successful Windows build. GitHub requires you to be signed in to download Actions artifacts.
2. Right-click the downloaded ZIP and choose **Extract All**.
3. Close OBS completely.
4. Open the extracted folder, double-click **INSTALL.cmd**, and accept the Windows administrator prompt. No Python, CMake, or Visual Studio installation is needed.
5. Open OBS. For the group, choose **Tools > Masqrayd Broadcast Stack**. The normal timer remains available under **Tools > Masqrayd Broadcast Timer**.

The ZIP updates the camera, Scene Vault/Scene Controller, and timer together so they can attach safely. Their saved source, crop, scenes, durations, and preferences are retained. The installer backs up existing DLL directories outside OBS's plugin scan folder and rolls them back if copying fails.

The package targets OBS Studio **32.2.2, Windows x64**. It has not been tested on other OBS versions or architectures. It uses OBS's own Qt libraries; do not copy Qt DLLs into OBS.

## Put your panels together

1. Open your camera through **Tools > Camera Pop-Out Native** and choose your finished OBS camera scene/source as usual.
2. Open **Tools > Masqrayd Broadcast Stack**.
3. Click **Panels** and check **Include Camera**, **Include Scene Controller**, and **Include Timer** as desired. The controller can be opened for you when you choose it. A camera that is not open yet displays a waiting section and connects after you open it.
4. Drag the stack header to move all attached panels together. Drag the dividers to change individual section heights; drag outside edges/corners to resize the group.
5. Click **Detach** on a section to put it back into its own window. Other sections remain grouped. You can also uncheck that panel in the Panels menu.

**Camera above controls above timer** is the default order. To leave the camera separate, include only Scene Controller and Timer. Right-click the stack header or use Panels for reordering, side-by-side layout, Always on Top, Borderless Window, or Open Stack with OBS. The selected panels, order, dimensions, and divider positions are saved.

The stack is optional and does not open automatically until you enable **Open stack with OBS**. A saved but unopened stack leaves your standalone windows alone. Closing/hiding the stack hides the group while the timer and controls keep running; reopening it restores the group. Detach All returns all selected panels to separate windows.

Each plugin keeps its own functional right-click menu while attached, including story navigation and camera source/crop controls. Individual window move/resize/topmost controls are disabled while attached; use the group's controls instead. Detaching restores the original window flags and standalone position.

The stack recognizes only the native Masqrayd Camera Pop-Out plugin, the Masqrayd Scene Controller, and the Masqrayd timer. It does not attach an OBS projector or an older Python camera window.

## During a broadcast

- **Start:** starts the current timer. After Pause or Stop, it becomes Resume and continues the same timer.
- **Pause / Resume:** freezes or continues timing.
- **Stop:** freezes the final reading.
- **Reset:** clears the current reading and prepares the saved duration without starting.
- **Next Story:** switches to story mode, advances the story number, clears elapsed time, and immediately starts the saved story duration.
- **Take a Break:** switches to a separate break countdown and starts immediately. Next Story returns to a fresh story timer.

Default story length is **10 minutes**; break length is **5 minutes**. The timer pulses red with **30 seconds remaining**, and continues red into overtime. Count-up mode shows elapsed time against the same target. Breaks always count down.

## Restarting or redoing stories

Right-click the timer for these controls:

- **Restart current story:** clear its timer and keep the current story number. From a break, return to that story.
- **Previous story:** go back one story and clear its timer. Disabled at Story 1.
- **Set story number...:** choose a story directly, such as Story 2 for a retake.
- **Restart from Story 1:** reset the story sequence and timer to Story 1. Your saved durations and settings remain.

These actions prepare the chosen story in **Ready** state. Click **Start** when you are ready to record it. **Next Story** always advances to the following story and starts it immediately. The normal **Reset** button clears only the current segment timer, keeping its number.

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

Close OBS, then run **UNINSTALL.cmd**. This removes the timer and its Broadcast Stack window. The camera and Scene Vault plugins remain usable separately. Saved preferences remain in OBS's plugin configuration folder.

## If it doesn't appear

Open **Help > Log Files > View Current Log** in OBS and search for `Masqrayd Broadcast Timer`. Confirm you installed the compiled Windows artifact rather than the repository source ZIP. The installed DLL should be at:

`C:\ProgramData\obs-studio\plugins\masqrayd-broadcast-timer\bin\64bit\masqrayd-broadcast-timer.dll`

Portable OBS installations may need the DLL copied to their own `obs-plugins\64bit` folder instead. The supplied installer targets the standard Windows OBS installation.
