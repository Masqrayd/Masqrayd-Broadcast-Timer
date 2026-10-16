# Masqrayd Broadcast Timer + Broadcast Stack

A native OBS Studio plugin for timing stories and breaks. Includes a floating, resizable timer, count-down/count-up story modes, separate break countdowns, configurable red warnings, overtime, and a smaller local date/time clock.

Designed for **OBS Studio 32.2.2 on Windows x64**. No Python or separate launcher is required. Opens/closes with OBS and is available under **Tools > Masqrayd Broadcast Timer**.

## Get the Windows download

Open [Actions](https://github.com/Masqrayd/Masqrayd-Broadcast-Timer/actions), select a successful **Build Broadcast Timer for Windows** run, and download **Masqrayd-Broadcast-Stack-1.1.2-Windows-x64** under Artifacts. Extract the ZIP, close OBS, then run **INSTALL.cmd**. The GitHub **Code > Download ZIP** option contains source code and cannot be installed directly.

See [the quick-start guide](docs/QUICK_START.md) for installation, controls, window settings, and troubleshooting.

## Optional shared broadcast window

Open **Tools > Masqrayd Broadcast Stack**, then use **Panels** to include Camera, Scene Controller, and/or Timer. Default order is camera above controls above timer, matching the broadcast layout. Only checked panels join the window. Each section has a **Detach** button that returns it to its previous standalone position and size.

Drag the stack header to move the group; drag outer edges to resize the window, and dividers to resize the sections. The Panels menu also supports side-by-side layout, reordering, borderless mode, always-on-top, opening with OBS, and detaching all panels. Choices and layout are remembered. Opening the stack is optional; a closed stack does not pull standalone panels into a hidden window.

The coordinated Windows ZIP includes compatible builds of all three plugins, preserving their existing preferences. The stack itself is included in the timer DLL. It embeds the original camera display and controls; no duplicate OBS source or recording is created. See [quick start](docs/QUICK_START.md).

## Broadcast controls

| Control | Result |
| --- | --- |
| Start / Resume | Start or continue the current segment |
| Pause / Resume | Freeze and continue the same reading |
| Stop | Freeze the final reading |
| Reset | Clear and prepare the current segment without starting |
| Next Story | Increment story number and immediately restart the story preset |
| Take a Break | Immediately start the separate break preset |

Defaults: **10-minute stories**, **5-minute breaks**, **30-second warning**. Right-click to change durations, warnings, count-up mode, pulsing, optional sound, always-on-top, borderless mode, and opening with OBS. No automatic scene changes or automatic story/break transitions occur.

Time settings apply to the next segment or Reset. Warning/display settings apply immediately. Elapsed time is measured from a monotonic clock rather than counting UI ticks. Overtime remains visible until you choose another segment, Stop, or Reset.

## Redo a story or restart the sequence

Right-click for **Restart current story**, **Previous story**, **Set story number...**, or **Restart from Story 1**. Each option clears the timer and prepares the selected story without starting it; click **Start** when ready. The normal **Reset** button preserves the story number. Story navigation during a break returns to the story timer.

## Development and validation

The Windows workflow builds the native DLL with the official OBS template's dependency setup and the pinned OBS 32.2.2 dependency versions. The Linux workflow compiles the plugin and runs deterministic timing and offscreen Qt UI tests. A successful build verifies compilation and automated behavior; the user's Windows OBS session still needs an installation check.

Core timing checks, no Qt/OBS required:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I src tests/timer-state-tests.cpp -o timer-state-tests
./timer-state-tests
```

Linux development with installed Qt6/OBS development packages and CMake 3.28+:

```sh
cmake -S . -B build-tests -DBROADCAST_TIMER_SYSTEM_BUILD=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

For UI-only tests without OBS, use `-DBROADCAST_TIMER_UI_ONLY=ON`. Build support under `cmake/` is derived from the GPL-licensed [OBS plugin template](https://github.com/obsproject/obs-plugintemplate), adapted from the existing Masqrayd Scene Vault build configuration. License: GPL-2.0-or-later.
