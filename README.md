# TMD-56 Temperature Logger

Dual-input temperature logger for simulation and replay. This is not an official Amprobe product. The physical meter connection is still experimental and does not decode temperatures.

The window title is **TMD-56 Temperature Logger**. The executable name is `tmd56-monitor`. Version 0.1.1 is shown in About.

## For users

You do not need a compiler, CMake, GTK, MSYS2, Python, or a terminal.

### Windows 10 or 11 (64-bit)

Download `TMD56Monitor-Setup-x64.exe` from the GitHub release and run it. The installer adds a Start menu shortcut named **TMD-56 Temperature Logger** and an uninstall entry. It installs under Program Files. After that, normal use does not need administrator rights.

A portable zip is also published as `TMD56Monitor-Windows-x64.zip`. Unzip it and double-click `tmd56-monitor.exe`.

Recordings are stored in `%LOCALAPPDATA%\TMD56Monitor\logs`. Uninstall does not delete that folder.

### Linux

Download `TMD56Monitor-x86_64.AppImage` from the GitHub release.

```bash
chmod +x TMD56Monitor-x86_64.AppImage
./TMD56Monitor-x86_64.AppImage
```

You can start it from any directory. The stylesheet is inside the application. Recordings go to `$XDG_DATA_HOME/TMD56Monitor/logs`, which is `~/.local/share/TMD56Monitor/logs` when `XDG_DATA_HOME` is unset.

`TMD56Monitor_0.1.1_amd64.deb` is optional. That package uses the distribution's GTK and libserialport instead of bundling them, so it is not the standalone release. The AppImage is.

## For developers

One C codebase and one CMake project. Linux uses GCC. Windows uses MSYS2 UCRT64 and MinGW-w64 GCC. Do not keep a separate Windows copy of the program.

### Linux development dependencies

These package names match Linux Mint 21 and 22 (Ubuntu 22.04 / 24.04). `libserialport-dev` is in the Ubuntu universe component.

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libgtk-4-dev libcairo2-dev libserialport-dev libglib2.0-dev-bin
```

`libglib2.0-dev-bin` provides `glib-compile-resources`, which embeds `resources/style.css` into the binary.

### Windows development dependencies (MSYS2 UCRT64)

From an UCRT64 shell:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-cairo \
  mingw-w64-ucrt-x86_64-libserialport \
  mingw-w64-ucrt-x86_64-ntldd \
  mingw-w64-ucrt-x86_64-nsis
```

Release builds are GUI programs and do not open a command prompt. Debug builds keep a console.

### Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/tmd56-stage
```

On Windows use `-G Ninja` from the UCRT64 shell. The same `ctest --output-on-failure` command runs the suite.

`TMD56_DATA_DIR`, when set, is the data root used instead of the normal per-user directory. Tests set it so they do not write into your real log folder.

Headless checks, from any directory:

```bash
tmd56-monitor --version
tmd56-monitor --self-test
```

`--version` prints `TMD-56 Temperature Logger 0.1.1`. `--self-test` checks the embedded stylesheet and icon, the log directory, the simulator, and the history buffer. It does not open a window and does not need the meter. Exit code 0 means every check passed.

### Packaging

Linux AppImage, from the repository root:

```bash
packaging/linux/build-appimage.sh
```

The script installs into an AppDir and runs linuxdeploy with the GTK 4 plugin. The file is `dist/TMD56Monitor-x86_64.AppImage`.

Debian package (depends on distro GTK, not standalone):

```bash
cpack --config build/CPackConfig.cmake -G DEB -B dist
```

Windows installer and portable zip are produced on a Windows runner by installing the CMake tree and bundling the DLLs that `ntldd` reports, plus the GTK schemas, pixbuf loaders, and icon theme the toolkit needs. See `packaging/windows/bundle-runtime.sh`. Expected release names:

- `TMD56Monitor-Setup-x64.exe`
- `TMD56Monitor-Windows-x64.zip`

Pushing a tag such as `v0.1.1` runs `.github/workflows/release.yml` and attaches those files to the GitHub Release. There is no automatic updater.

There is no automatic updater in this version. GitHub Releases are the distribution channel.

## What you can do without the meter

**Simulator.** Start with T1 and T2 near 25 °C. Samples arrive every 500 ms. Scenarios:

| Scenario | Behaviour |
| --- | --- |
| Stable | Both channels stay near 25 °C, with a slow drift |
| Heating | T1 rises toward about 85 °C. T2 follows more slowly |
| Cooling | T1 falls toward about −3 °C. T2 follows more slowly |
| Thermal gradient | T1 and T2 move apart |
| Noise | Same neighbourhood as Stable, with larger measurement noise |
| Occasional spike | A single-sample jump of about +45 °C or −38 °C every 20 s |

The spike is applied only to the emitted sample. The underlying curve does not stay at the bad value. Nothing here is a recording from a TMD-56.

**Replay.** Choose Replay File, then OPEN FILE. The chooser accepts `.txt`, `.tsv` and `.csv`, and the parser decides from the contents rather than the extension. Tab-separated Amprobe exports and CSV logs from this program both work. Metadata before the samples is ignored. A missing or non-numeric channel, including `OL`, is kept as an invalid sample and is never stored as 0 °C. Negative temperatures are valid. The first sample is elapsed 0 s. Playback follows the original spacing: 1× waits the recorded interval, 2× waits half of it. PLAY, PAUSE and RESTART use the same display, statistics and graph as live acquisition. The graph fills only as samples are released. Restart clears that session and begins again. Logging stays off in replay. Change the source only after STOP.

`examples/sample_tmd_export.txt` is a short synthetic file in the export layout, including one `OL` token and one obvious spike.

**Graph.** Cairo draws T1 and T2 against elapsed time, with grid lines and a temperature axis that scales to the visible samples. The history window is 1, 5, 10, 30 or 60 minutes, or all samples kept so far. The acquisition buffer is separate from the drawing code and holds up to 24 hours at 10 Hz; older samples are dropped after that. The plot is asked to redraw at most about 10 times a second.

**Logging.** A session name is required. The file looks like:

```text
timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid
2026-09-24T18:30:00.000,0.000,24.500,24.600,-0.100,1
2026-09-24T18:30:01.000,1.000,,,0
```

Invalid temperatures are left blank. `valid` is 1 only when both channels are real numbers. The suspicious-sample flag is not a column: the log always stores the raw reading. The file is flushed on every row and synced about every 20 rows. Logs are written under the per-user data directory described above, not beside the executable.

**Statistics.** T1 and T2 each show the current value, minimum, maximum and average for the acquisition that is running. Start clears them. Delta T is T1 − T2.

**Spikes.** Optional. A sample is marked when the absolute change on either channel from the last accepted temperature is larger than the threshold (default 20 °C). The raw numbers stay in the history and in the CSV. Three samples in a row that agree with each other are treated as a real step, not a spike. "Exclude spikes from graph and statistics" hides marked points and leaves them out of min / max / average. This is not applied to the TMD-56 source, so a future protocol dump is not filtered.

## Layout

```text
src/sources/measurement_source.h   open, close, start, stop, get_measurement
src/sources/simulator_source.c     synthetic channels
src/sources/replay_parser.c        export / CSV parser
src/sources/replay_source.c        replay playback
src/sources/tmd56_source.c         unverified serial stub
src/platform/user_paths.c          per-user log directory
src/history_buffer.c               bounded acquisition history
src/logger.c                       CSV session log
src/ui/main_window.c               GTK 4 window
src/ui/live_plot.c                 Cairo graph
resources/style.css                instrument theme, compiled in with GResource
packaging/                         installers, AppImage, Windows runtime bundle
```

## TMD-56 hardware

Not implemented, and not validated.

`src/sources/tmd56_source.c` is the only place that knows about the serial port. The notes recorded there, from other implementations and **not checked on this instrument**, are:

- 19200 baud, 8 data bits, even parity, 1 stop bit, no flow control
- a reported query `#0A0000NA2` followed by CR LF
- a reported 16-byte response beginning `0x3E 0x0F`
- temperatures reportedly near bytes 5–6 (T1) and 10–11 (T2)

The program does not send that query and does not turn those bytes into temperatures. Status-byte meanings are not invented. Connect opens a port with the settings above when you type a device path such as `/dev/ttyUSB0`, counts any bytes that arrive, and still produces no readings. The window labels this mode **Experimental / unverified**.

Your user account is expected to be in the `dialout` group before a later milestone can open the real port. That is not a claim that the protocol works.

## Requirements

- C11, GCC, CMake
- GTK 4
- Cairo
- libserialport (linked now, used only by the unverified stub)
