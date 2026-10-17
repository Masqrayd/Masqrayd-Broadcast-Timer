# Masqrayd Broadcast Stack + Timer — Windows quick start

1. Download **Masqrayd-Broadcast-Stack-1.2.1-Windows-x64** from the Artifacts section of a successful Windows build. GitHub requires you to be signed in to download Actions artifacts.
2. Right-click the downloaded ZIP and choose **Extract All**.
3. Close OBS completely.
4. Open the extracted folder, double-click **INSTALL.cmd**, and accept the Windows administrator prompt. No Python, CMake, or Visual Studio installation is needed.
5. Open OBS. For the group, choose **Tools > Masqrayd Broadcast Stack**. The normal timer remains available under **Tools > Masqrayd Broadcast Timer**.

The ZIP updates the camera, Scene Vault/Scene Controller, timer, and Scene Collections together so they can attach safely. Their saved source, crop, scenes, durations, and preferences are retained. The installer backs up existing DLL directories outside OBS's plugin scan folder and rolls them back if copying fails.

The package targets OBS Studio **32.2.2, Windows x64**. It has not been tested on other OBS versions or architectures. It uses OBS's own Qt libraries; do not copy Qt DLLs into OBS.

## Put your panels together

1. Open your camera through **Tools > Camera Pop-Out Native** and choose your finished OBS camera scene/source as usual.
2. Open **Tools > Masqrayd Broadcast Stack**.
3. Click **Panels** and check **Include Camera**, **Include Scene Controller**, **Include Timer**, and **Include Scene Collections** as desired. The controller can be opened for you when you choose it. A camera that is not open yet displays a waiting section and connects after you open it.
4. Drag the stack header to move all attached panels together. Drag the dividers to change individual section heights; drag outside edges/corners to resize the group.
5. Click **Detach** on a section to put it back into its own window. Other sections remain grouped. You can also uncheck that panel in the Panels menu.

**Camera above controls above timer** is the default order. To leave the camera separate, include only Scene Controller and Timer. Right-click the stack header or use Panels for reordering, side-by-side layout, Always on Top, Borderless Window, or Open Stack with OBS. The selected panels, order, dimensions, and divider positions are saved.

The stack is optional and does not open automatically until you enable **Open stack with OBS**. A saved but unopened stack leaves your standalone windows alone. Closing/hiding the stack hides the group while the timer and controls keep running; reopening it restores the group. Detach All returns all selected panels to separate windows.

Each plugin keeps its own functional right-click menu while attached, including story navigation and camera source/crop controls. Individual window move/resize/topmost controls are disabled while attached; use the group's controls instead. Detaching restores the original window flags and standalone position.

The stack recognizes only the native Masqrayd Camera Pop-Out plugin, the Masqrayd Scene Controller, the Masqrayd timer, and Masqrayd Scene Collections. It does not attach an OBS projector or an older Python camera window.

## Switch your channel collection

1. Open **Tools > Masqrayd Scene Collections**, or choose **Panels > Include Scene Collections** in Broadcast Stack.
2. Click **Celebrity Masqrayd**, **Criminal Masqrayd**, **Political Masqrayd**, or **Masqrayd-Mayhem**. Gold shows the collection actually active in OBS.
3. Right-click the collection panel > **Collections shown** to choose different collections. Its labels use your exact OBS names. Uncheck old unavailable names and check their replacements after a rename.
4. Choose **Button columns > 1, 2, or 4** to set the arrangement. Text shrinks/wraps inside the buttons when you resize.
5. For a standalone window, right-click for **Always on top**, **Borderless window**, and **Open with OBS**. Drag the heading or Ctrl+drag a button to move it; drag edges to resize. In the stack, use its header/dividers and **Detach**.

The panel loads an entire existing collection through OBS's normal collection-switch operation. The OBS profile remains selected. It neither starts nor stops a recording and does not reset your timer; loading a collection can briefly interrupt the picture while its sources load. Its highlight also updates when you change collections from OBS's menu. An unsuccessful change displays a message and retains the real active highlight.

For the camera to reconnect across channels, keep the selected camera source/scene name available in each collection and enable the camera plugin's existing automatic-open option. Missing assets and damaged collection files still need to be repaired in OBS.

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

Close OBS, then run **UNINSTALL.cmd**. This removes the timer and its Broadcast Stack window. The camera, Scene Vault, and Scene Collections plugins remain usable separately. To remove Scene Collections too, delete its `masqrayd-scene-collections` plugin folder after closing OBS. Saved preferences remain in OBS's plugin configuration folder.

## If it doesn't appear

Open **Help > Log Files > View Current Log** in OBS and search for `Masqrayd Broadcast Timer`. Confirm you installed the compiled Windows artifact rather than the repository source ZIP. The installed DLL should be at:

`C:\ProgramData\obs-studio\plugins\masqrayd-broadcast-timer\bin\64bit\masqrayd-broadcast-timer.dll`

Portable OBS installations may need the DLL copied to their own `obs-plugins\64bit` folder instead. The supplied installer targets the standard Windows OBS installation.

## Compact panel heights (1.2.1)

Drag the horizontal divider above or below Timer to make that section shorter.
Timer digits, labels, button text, spacing and margins scale with the available
height. The timer body can shrink to 110 pixels (plus its stack header).
Scene Collections can shrink to an 80-pixel body. Scene Controller rows and
Studio/Send controls also scale with height. Use the freed room for the scene
buttons. The stack remembers the section sizes; widening the window is optional.
