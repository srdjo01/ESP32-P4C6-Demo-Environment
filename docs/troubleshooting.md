# Troubleshooting

Real problems seen on this board and how to fix them. Skim the headings and jump to
your symptom.

---

## Connection & ports

### The GUI can't find / open the port
- You must select the **P4 USB-CDC** port (`VID_303A&PID_4001`, shown as
  `USB Serial Device`), **not** the USB-Serial/JTAG (`1001`) and **not** the CH340.
- Only one program can hold a serial port. Close any serial monitor, a second copy
  of the GUI, or an `idf.py monitor` that still has the port open.
- After flashing, the CDC port re-enumerates — click **Refresh** and reconnect.

### "Access is denied" / "could not open port"
Another process holds it. Common culprits: a previous GUI instance, a serial
monitor, or a script that didn't close the port. Close them, or unplug/replug.

---

## Wi-Fi / Bluetooth

### Scan shows nothing / "No response — is the ESP32-C6 powered?"
Work through these in order:

1. **Power-cycle the board** (unplug/replug USB-C). This is the most common fix.
   The C6's reset line is driven by the CH340's DTR/RTS; if a program has opened
   and closed the C6's CH340 port (COM4), it can leave the C6 held in reset. A
   clean power-up releases it. **Tip:** don't open the C6 CH340 port while using
   the GUI — the GUI never needs it.
2. **Confirm the C6 firmware is flashed.** Flash it explicitly:
   `.\build_firmware.ps1 -Target c6 -Flash -C6Port COM4`.
3. **Check the C6 is booting:** open its CH340 port (COM4) at 115200 in a serial
   monitor — you should see Wi-Fi calibration logs (`wifi: ...`) shortly after reset.
   No output at all = the C6 is held in reset (see step 1) or not flashed.
4. **Verify the inter-chip wiring** if this is a bare board: P4 GPIO15↔C6 GPIO21 and
   P4 GPIO14↔C6 GPIO20 must be connected. If you ever swap TX/RX, the symptom is
   exactly this (no response).

### Wi-Fi connect fails
- Wrong password, or 5 GHz-only network (the C6 is 2.4 GHz only).
- A `wifi_ping` before connecting returns `not connected` — connect first.

### BLE scan finds fewer devices during heavy Wi-Fi traffic
Expected: Wi-Fi and BLE share the 2.4 GHz front-end (software coexistence). Scan
again when Wi-Fi is idle.

---

## GPIO

### GPIO 14 and 15 "don't work" / return "invalid pin"
**By design.** GPIO14/15 carry the UART link to the ESP32-C6 (Wi-Fi/BLE). The
firmware reserves them, and the GUI's GPIO tab only offers **16–19**. If you send
`gpio_set`/`gpio_get` for 14 or 15 over the protocol you'll get
`{"status":"error","message":"invalid pin"}`.

To use 14/15 as test pins you'd have to move the C6 link to another wire pair (which
then becomes unavailable instead) — see
[`hardware_overview.md`](hardware_overview.md) §7. There are only six physical wires
between the chips, so Wi-Fi/BLE always costs two of GPIO 14–19.

---

## Display

### Display stays black / nothing happens, but the board keeps working
The display is initialised **on demand**, the first time you send a `display_*`
command, not at boot. If the panel or its FPC cable isn't attached correctly, that
first command returns an error (`display init failed (panel/FPC?)`) instead of
hanging the whole board.

### The board used to freeze at boot with a display attached
Older firmware initialised the display during boot; with no panel (or a mis-seated
FPC) the MIPI-DSI driver busy-waits forever and starves the CPU (task watchdog
warnings on the P4 log). This is fixed — display init is deferred to the first
display command. If you see watchdog spam, make sure you're on the current firmware.

### FPC cable orientation
The display adapter has an **inverted FPC connector**. Use an "opposite-side
contacts" cable; the wrong cable has damaged boards. See
[`modules/display.md`](modules/display.md) before plugging the panel in.

---

## eMMC

### eMMC test fails / times out
eMMC is hardware/board-dependent on this design. Check:
- The test enables LDO channel 4 at 1.8 V around each run; a board with a faulty
  LDO ch4 won't respond.
- Start at a low clock (`400 kHz`, then `4 MHz`) to separate signal-integrity issues
  from device problems.

---

## Building

### `setup.ps1` errors on `ensurepip`
Older copies called `python -m ensurepip --quiet`; `ensurepip` has no `--quiet`
flag. This is fixed in the current script. If you hit it, it's harmless — venvs ship
with pip already; just re-run, or create the venv manually:
`python -m venv host_tool\.venv` then
`host_tool\.venv\Scripts\python -m pip install -r host_tool\requirements.txt`.

### `build_gui.ps1` aborts immediately or "PyInstaller failed"
- **Aborts at the PyInstaller step with red text but PyInstaller seemed fine:**
  PowerShell's `$ErrorActionPreference="Stop"` treats PyInstaller's normal
  stderr logging as fatal. The current script relaxes this around the call — make
  sure you're on the updated `build_gui.ps1`.
- **"PyInstaller failed" at the very end:** the output `.exe` is **locked by a
  running copy**. Close any open `ESP32-P4C6-Tool.exe` (there can be two processes
  for a one-file build) and rebuild.

### `build_firmware` can't find ESP-IDF
The scripts expect **ESP-IDF v5.4.1**. Windows looks in `C:\Espressif\v5.4.1`;
macOS/Linux in `~/.espressif/v5.4.1`, `~/esp/esp-idf`, or `/opt/esp-idf`. If yours
is installed elsewhere or is a different version, edit the path at the top of
`build_firmware.ps1` / `build_firmware.sh`, or run `.\setup.ps1` / `./setup.sh`.

### First firmware build is slow / downloads things
Normal. On first build IDF fetches the managed components (LVGL, the CO5300 display
driver, TinyUSB). Subsequent builds are incremental and fast.

---

## Flashing

### Flashing the P4 vs the C6 — wrong port
They are flashed over **different** ports:
- P4 → its USB-Serial/JTAG port (`303A:1001`), e.g. COM5.
- C6 → its CH340 port (`1A86:7523`), e.g. COM4.

Use `-Port` for the P4 and `-C6Port` for the C6 (or `--port` / `--c6-port`).

### "Could not open port … the port is busy"
Close anything using that port (serial monitor, GUI, another flash). On the C6,
opening/closing its CH340 quickly in succession can briefly hold it — wait a second
and retry.

### Board #5 / a board that won't enter download mode
Some boards have eFuse quirks. Flash over an external USB-UART adapter in manual
bootloader mode — see [`flashing_guide.md`](flashing_guide.md) "External adapter".

### `idf.py` fails with `No module named 'click'`
You're running `idf.py` with the wrong Python. ESP-IDF v5.4 keeps `idf.py`'s CLI
deps (click, pyserial, …) in a venv at `~/.espressif/python_env/idf5.4_pyX.Y_env/`,
not in the system / Homebrew Python. Two fixes:

- **From the GUI:** open the **Setup** tab and confirm the **IDF Python venv**
  field points at that directory. Click **Test** — it should report a version
  string, not the click error.
- **From the shell:** source `export.sh` first
  (`. ~/esp/v5.4/esp-idf/export.sh`), or run
  `~/.espressif/python_env/idf5.4_py3.13_env/bin/python ~/esp/v5.4/esp-idf/tools/idf.py …`
  directly.

The `build_firmware.sh` script handles this for you, including pinning Python
3.13 on systems where the default `python3` is a newer version IDF doesn't yet
ship a venv for.

---

## CAN

### `Self-test (loopback)` reports FAIL
The TWAI controller couldn't loop a frame back to itself. This is a controller
or build problem, not a wiring problem (the self-test runs in `NO_ACK` self-mode
and needs no external hardware).

- Re-flash the firmware and try again — the controller can be left in a weird
  state by an aborted previous run.
- Verify GPIO 1/2/3 aren't claimed by another component on a fork of the firmware.

### Frames sent but never received on the other side
Walk through these in order:

1. **Same bitrate on both ends?** This is the most common cause.
2. **120 Ω termination at each end of the bus?** Without it, error counters
   climb fast and the controller will trip into bus-off.
3. **Common GND between the two nodes?** A floating ground silently breaks the
   bus.
4. **Silent mode off?** Click the **Silent mode (STB high)** checkbox to clear it.
   Silent mode keeps the receiver active but the transceiver won't drive the bus.
5. **CAN_H / CAN_L not swapped?** The TJA1051 doesn't tolerate inversion.

### Status shows `bus_off=true`
The controller has shut down its transmitter after exceeding 256 TX errors —
usually missing termination, no other node ack'ing, or the wrong bitrate. To
recover: **Stop**, fix the underlying wiring/bitrate issue, then **Start** again.

### `tx_errors` and `arb_lost` keep climbing
On a busy bus `arb_lost` is normal — your node lost arbitration to a
higher-priority frame. `tx_errors` climbing while idle indicates a wiring
problem (open bus, missing ground, mis-terminated bus).

---

## When all else fails

1. Power-cycle the board (unplug/replug).
2. Re-flash both chips: `.\build_firmware.ps1 -Target both -Flash -Port COM5 -C6Port COM4`.
3. Watch the P4 log on its USB-Serial/JTAG port and the C6 log on its CH340 port at
   115200 — boot banners and module-ready lines tell you which stage is failing.
