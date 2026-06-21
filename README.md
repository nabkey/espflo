# espflo

ESPHome control for a **Pentair SuperFlo VST** variable‑speed pool pump using
an **ESP32‑S3** and a **TTL485 V2.0** (TTL ↔ RS‑485) adapter.

The SuperFlo VST exposes an RS‑485 "COM port" for external control. It does
**not** speak Modbus — it uses Pentair's proprietary RS‑485 protocol (the same
family as IntelliFlo). This project makes the ESP32‑S3 act as an external
controller on that bus, so you can set the pump speed from Home Assistant.

> ⚠️ This is unofficial. The protocol is community‑reverse‑engineered, not
> documented by Pentair. Wire and operate at your own risk; pool equipment
> runs on mains voltage. Power the pump down at the breaker before opening the
> field‑wiring compartment.

---

## What you need

| Part | Notes |
|------|-------|
| ESP32‑S3 dev board | Any S3 board with exposed GPIOs (e.g. ESP32‑S3‑DevKitC‑1) |
| TTL485 V2.0 module | TTL ↔ RS‑485 with **automatic flow control** (no DE/RE pin) |
| Custom pump cable | Plugs into the SuperFlo VST COM port; breaks out the green/yellow data pair |
| Jumper wires | ESP ↔ TTL485 |

The "automatic flow control" / "auto‑direction" feature matters: the module
switches between transmit and receive on its own, so there is **no driver‑enable
pin to wire to a GPIO**. You only connect power, ground, TX and RX.

---

## How the pieces connect

```
  ESP32-S3                 TTL485 V2.0                 SuperFlo VST COM port
 +---------+              +-----------+               +--------------------+
 |     3V3 |------------->| VCC       |               |                    |
 |     GND |------------->| GND       |               |                    |
 | GPIO17  |--- TX ------>| TXD (DI)  |   A (D+) ===> | GREEN  (Data +)    |
 | GPIO18  |<-- RX -------| RXD (RO)  |   B (D-) ===> | YELLOW (Data -)    |
 +---------+              +-----------+               +--------------------+
                                          twisted pair
```

### ESP32‑S3 ↔ TTL485 (TTL side)

| ESP32‑S3 | TTL485 V2.0 | Purpose |
|----------|-------------|---------|
| `3V3`    | `VCC`       | Power the module at 3.3 V so its logic levels are safe for the ESP's RX pin |
| `GND`    | `GND`       | Common ground |
| `GPIO17` | `TXD` (data‑in) | ESP transmit → module → RS‑485 bus |
| `GPIO18` | `RXD` (data‑out) | RS‑485 bus → module → ESP receive |

Notes:
- **Power from 3V3, not 5V.** Auto‑flow modules (MAX13487‑class) work at 3.3 V,
  and this keeps the module's TX output from driving 5 V into the ESP32‑S3 RX
  pin, which is **not** 5 V tolerant. If your specific module only works at 5 V,
  put a level shifter (or divider) on the module‑TXD → ESP‑RX line.
- **TXD/RXD labels vary between modules.** The functional rule is: *ESP TX goes
  to the module's serial input; ESP RX comes from the module's serial output.*
  If you see no traffic, swap these two wires first.
- `GPIO17`/`GPIO18` are just sensible defaults (not strapping, USB, or flash
  pins). Change them via the `tx_pin` / `rx_pin` substitutions in `pump.yaml`.

### TTL485 ↔ pump (RS‑485 side)

On the SuperFlo VST COM port **only the green and yellow conductors are used**;
all other conductors in the cable should be cut back and insulated.

| Pump wire | RS‑485 signal | TTL485 terminal |
|-----------|---------------|-----------------|
| **GREEN** | Data + (A)    | `A` / `D+`      |
| **YELLOW**| Data − (B)    | `B` / `D−`      |

If the pump never responds and swapping TXD/RXD didn't help, **swap A/B
(green/yellow)** — reversed RS‑485 polarity is the second most common mistake.
Keep this pair short and ideally twisted.

---

## Pump‑side setup (do this on the pump keypad)

1. Set the pump's **Pump Address** to **1** (this maps to bus address `0x60`,
   which is what `pump.yaml` targets via the `pump_address` substitution; use
   `0x61` if you set address 2).
2. So the external controller fully owns the speed, set the pump's built‑in
   **Speed 1 to 0 RPM with a 24‑hour duration** (per the SuperFlo VST manual's
   "External Control via RS‑485" section). The pump then runs only what we tell
   it to.

---

## Software setup

This repo is a standalone ESPHome project (it does not require Home Assistant to
build, but pairs naturally with it).

```bash
pip install esphome              # or use the ESPHome add-on / Docker

cp secrets.yaml.example secrets.yaml
$EDITOR secrets.yaml             # fill in Wi-Fi + keys

esphome run pump.yaml            # build, then flash over USB the first time
```

Files:

| File | Purpose |
|------|---------|
| `pump.yaml` | The ESPHome device config (UART, control logic, HA entities) |
| `pentair.h` | C++ helper that frames + checksums Pentair packets |
| `secrets.yaml.example` | Template for your Wi‑Fi / API / OTA secrets |

### Entities you get in Home Assistant

- **Pump Speed** (number, 0–3450 RPM, 50‑RPM steps) — the master control. `0`
  turns the pump off; any value > 0 takes remote control and runs that speed.
- **Pump** (switch) — convenience on/off; "on" resumes `default_rpm`.
- **Pump Low / Medium / High** (buttons) — 1200 / 2400 / 3450 RPM presets.

---

## How the control works (the protocol)

Communication is **9600 baud, 8 data bits, no parity, 1 stop bit**, half‑duplex.

Every packet looks like:

```
FF 00 FF | A5 00 <dst> <src> <cmd> <len> <data...> | <ckHi> <ckLo>
\_preamble_/ \____________ checksummed body ___________/ \_checksum_/
```

- `dst` = `0x60` (pump address 1), `src` = `0x21` (this controller).
- Checksum = 16‑bit sum of every byte from `A5` through the last data byte,
  sent big‑endian. The preamble is not summed. `pentair.h` does this for you.

The commands this project sends (verified against community packet captures):

| Action | cmd | data | Example full frame |
|--------|-----|------|--------------------|
| Take remote control | `0x04` | `FF` | `FF 00 FF A5 00 60 21 04 01 FF 02 2A` |
| Release remote control | `0x04` | `00` | `… 04 01 00 …` |
| Power on | `0x06` | `0A` | `FF 00 FF A5 00 60 21 06 01 0A 01 37` |
| Power off | `0x06` | `04` | `… 06 01 04 …` |
| Set speed (RPM) | `0x01` | `02 C4 <hi> <lo>` | 800 RPM → `… 01 04 02 C4 03 20 02 14` |

To run the pump, `pump.yaml` sends *take remote control → set speed → power on*.
Because the pump reverts to its keypad/programmed behavior if it stops hearing a
controller for ~30 seconds, an **`interval` re‑sends the current state every 15
seconds** as a keep‑alive. Setting speed to `0` powers the pump off and releases
remote control back to the keypad.

---

## Troubleshooting

| Symptom | Things to check |
|---------|-----------------|
| Pump never reacts | Swap module **TXD/RXD**; then swap **A/B (green/yellow)**; confirm pump address = 1; confirm 9600 8N1 |
| Reacts then stops after ~30 s | Keep‑alive not firing — confirm the `interval` block and that Wi‑Fi/log isn't blocking the loop |
| Garbage / no frames in log | Enable the `uart: debug:` block in `pump.yaml` and watch raw bytes; check 3.3 V power and common ground |
| ESP resets / brownout | Don't power the ESP from the pump; give the ESP its own clean 5 V/USB supply |

To see raw traffic, uncomment the `debug:` section under `uart:` in `pump.yaml`
and run `esphome logs pump.yaml`.

---

## Credits

Protocol details are thanks to the long‑running community reverse‑engineering
efforts around Pentair RS‑485 (CocoonTech IntelliFlo threads, nodejs‑poolController,
OPNpool, and various ESPHome/Arduino pump projects). The SuperFlo VST wiring
("green/yellow only", external control via RS‑485) is from Pentair's own
SuperFlo VST installation manual.
