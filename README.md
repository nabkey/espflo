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

> ⚠️ **Do not try to power the ESP32 from the COM cable.** Pentair's RS‑485
> cable (kit `356324Z`) is an 8‑conductor harness that *does* include a **RED
> +5 V wire**, but per Pentair it "is output from the drive only and should
> **never be wired to another power supply**" — it is not a rail for running
> accessories, and the green/yellow data pair carries no usable power. See
> [Powering the ESP32](#powering-the-esp32) below.

---

## Powering the ESP32

**Short version: give the ESP32‑S3 its own clean 5 V supply. Do not power it
from the pump's COM port.**

The SuperFlo VST COM port is an RS‑485 *signal* bus, not a power source:

- **Green / yellow (Data+ / Data−)** carry low‑current differential signaling.
  There is no current budget there to run a microcontroller.
- The full Pentair cable (kit `356324Z`) also has a **RED +5 V conductor**, but
  Pentair documents it as a drive output that is **not used** and must **never
  be connected to another power supply**. It is not rated or intended to power
  accessories, and an ESP32‑S3 with Wi‑Fi draws current spikes of several
  hundred mA that this line is not characterized for. Back‑feeding from it
  risks brownouts, resets, bus errors, or damage.

Community ESP32/Arduino Pentair projects all do the same thing: power the
RS‑485 transceiver's `VCC` from the **ESP's** supply, and power the ESP itself
separately — never off the pump cable.

### Recommended options

1. **Separate 5 V USB supply (simplest, recommended).** A standard USB phone
   charger / wall adapter into the dev board's USB port. Only the green/yellow
   pair runs to the pump.
2. **One mains‑fed AC‑DC module (single cable run, advanced).** If you want to
   power the ESP from the same feed as the pump, install a proper
   **mains‑to‑5 V converter** (e.g. a Mean Well IRM‑01/IRM‑02 or HLK‑PM01‑class
   module) — never a tap onto the data wires. It must be:
   - sized for the pump's **actual input voltage** (SuperFlo VST ships in both
     115 V and 230 V configurations — confirm yours first),
   - properly **fused** and **isolated**, and
   - mounted inside the field‑wiring enclosure or its own sealed box.

> ⚠️ This is outdoor, wet, mains‑voltage pool equipment. Power down at the
> **breaker** before opening any compartment, and if you're not comfortable
> working inside a mains junction, have an electrician do option 2.

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
$EDITOR secrets.yaml             # fill in your Wi-Fi SSID + password

esphome run pump.yaml            # build, then flash over USB the first time
```

`pump.yaml` deliberately depends on only two secrets — `wifi_ssid` and
`wifi_password` — so that inside the Home Assistant ESPHome add-on it reuses the
dashboard's existing shared `secrets.yaml` with nothing to reconcile. API
encryption and an OTA password are left off by default; add them later via the
dashboard if you want them.

Files:

| File | Purpose |
|------|---------|
| `pump.yaml` | The ESPHome device config (UART, control logic, HA entities). Self-contained — the `send_pentair()` frame builder is defined inline (no external header), so it can be pushed to the ESPHome dashboard as a single file. |
| `secrets.yaml.example` | Template for your Wi‑Fi secrets (`wifi_ssid`, `wifi_password`) |

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
  sent big‑endian. The preamble is not summed. The inline `send_pentair()`
  helper in `pump.yaml` does this for you.

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
