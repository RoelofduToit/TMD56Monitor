# Windows release validation

Do not mark `v0.1.0` packaging as validated until this list has been completed on a clean machine. `v0.1.0-rc1` is only a candidate: the application still reports version 0.1.0.

The automated Windows job runs `tmd56-monitor.exe --self-test` from the portable directory and from a silent install, with `PATH` limited to `C:\Windows\System32`. That does not replace this manual pass.

1. Use a clean Windows 10 or Windows 11 x64 machine or VM.
2. Confirm MSYS2 is not installed.
3. Confirm GTK is not installed separately.
4. Download `TMD56Monitor-Windows-x64.zip`.
5. Extract it. The archive must contain one top-level folder, `TMD56Monitor\`, with `tmd56-monitor.exe` directly inside it.
6. Double-click `tmd56-monitor.exe`.
7. Confirm no console window appears.
8. Confirm the instrument styling loads (red rail, graphite face, light readout). A missing stylesheet looks like a plain GTK window.
9. Start the simulator.
10. Verify T1, T2, and T1−T2 are finite, one decimal place, and agree with each other.
11. Verify the graph updates.
12. Stop acquisition, then start it again. Confirm the run resets and does not duplicate timers.
13. Open a replay file (`.txt`, `.tsv`, or `.csv`).
14. Verify Play, Pause, Restart, and the speed control.
15. Start a simulated logging session.
16. Confirm the CSV appears under `%LOCALAPPDATA%\TMD56Monitor\logs`.
17. Close the application while it is idle.
18. Start acquisition again, then close the application while it is acquiring. It must exit without a crash and close the log if one was open.
19. Install `TMD56Monitor-Setup-x64.exe`.
20. Launch **TMD-56 Temperature Logger** from the Start menu.
21. Repeat the simulator, replay, and logging smoke tests.
22. Uninstall from Windows Settings or the uninstaller.
23. Confirm the Program Files directory for the application is removed.
24. Confirm the CSV files under `%LOCALAPPDATA%\TMD56Monitor\logs` are still there.
25. Confirm none of the steps showed a missing-DLL dialog or a GTK/MSYS2 error.

Publisher on the installer must be **TMD56 Monitor Project**, not Amprobe.
