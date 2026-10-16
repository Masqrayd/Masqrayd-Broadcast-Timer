# Scene Collections validation — 1.2.0

Local Linux development compilation passed for the timer/stack and the new standalone scene collections module using Qt 6.4.2 and installed OBS 30 development headers. The Windows workflow remains pinned to OBS 32.2.2 and its Qt dependencies.

All four CTest suites pass: scene-collections, timer-state, timer-ui, broadcast-stack.

The collection suite covers channel defaults, exact-name selection, current-collection no-op, deferred activation, rapid-click suppression, external-menu synchronization, rename/delete handling (including deletion before a queued request), refused-switch reporting, narrow two-column layout, fourth-panel attachment and switching, detachment geometry/ownership, saved panel inclusion, saved empty selection, and migration of the previous three-panel order. These use an injected OBS backend and real Qt widgets; they do not substitute for a Windows OBS session check.

Still required: run the Windows GitHub Actions build/tests/package and install in OBS 32.2.2. Verify switching each existing collection while idle and during a short test recording, camera reconnection with the selected source name, refreshed Scene Controller buttons, and saved stack layout after OBS restart. The switcher itself does not start/stop outputs or hold scene/source references.

The new work is on the local feature/scene-collection-switcher branch, based on feature/broadcast-stack. No remote update was completed; automatic approval review blocked the GitHub push pending explicit user authorization.
