# Firmware

Zephyr with the nRF Connect SDK v3.4.2. The board joins a Thread network as a Matter sleepy end device (short idle time, polls its parent every 5 s) and shows up as an Electrical Sensor:

| Matter attribute | From the TIC |
| --- | --- |
| Cumulative energy imported (kWh) | `EAST` in standard mode, sum of the indexes in historic mode |
| Active power (W) | estimated from the energy counter, see below |
| Apparent power (VA) | `SINSTS` / `PAPP` |
| Current (A) | `IRMS1` / `IINST` |
| Voltage (V) | `URMS1`, standard mode only |

Home Assistant picks these up as energy, power, apparent power, current and voltage sensors, and the energy one works in the Energy dashboard.

It builds, but it has not run on a real board yet.

## Layout

```
boards/linky_matter_over_thread/   Zephyr board definition (pins, 32 kHz crystal, flash layout)
src/tic/                           TIC decoder, no Zephyr dependency
src/tic_reader.c                   UART reception, finds 1200 or 9600 baud on its own
src/electrical_sensor.cpp          Matter clusters on endpoint 1
src/vin_monitor.c                  input voltage on AIN6, logged once a minute
src/default_zap/                   Matter data model (linky.zap) and the code ZAP generated from it
tests/host/                        decoder tests that run on a computer
```

## How it reads the meter

- **Speed.** The reader starts at 9600 baud. If no valid frame arrives within 6 s, it switches to 1200 baud, and back again, until a frame passes all checks.
- **Format.** The TIC sends 7E1 characters, but the nRF52840 UART has no 7-bit mode. The UART reads them as 8N1, and the decoder checks the parity bit itself before dropping it.
- **Errors.** A frame is only used if every group has a good checksum and the frame ends with ETX, so a reading never mixes two frames.
- **Active power.** The Linky only reports apparent power, so active power comes from the energy counter. It moves in 1 Wh steps, and the power is taken between two steps at least a minute apart. When the counter stops moving, the value falls towards zero instead of freezing.
- **Indexes.** Both modes end up with the same index numbers (HCHC = 1, HCHP = 2, Tempo 1 to 6), so the rest of the firmware does not care which mode the meter is in. Per-index energy is decoded but not exposed in Matter yet: only the total is.

## Board choices

- **No bootloader, no DFU.** There is no external flash, so updates go through SWD. Flash is laid out as 988 KB application, 4 KB factory data, 32 KB settings.
- **No DC/DC.** The E73 does not appear to have the inductor the nRF52840 DC/DC needs, so the regulator stays in LDO mode.
- **Debug output.** The log goes to J2 pin 4 (TX, 115200 baud). There is no shell, since the header has no RX line.

The current build uses 67 % of the flash and 60 % of the RAM.

## Building

Install the SDK once:

```
nrfutil install sdk-manager
nrfutil sdk-manager install v3.4.2
```

Then build from the SDK folder, pointing at this one (`$FW` is the absolute path of this `firmware` folder):

```
cd /opt/nordic/ncs/v3.4.2
nrfutil sdk-manager toolchain launch --ncs-version v3.4.2 -- \
  west build -b linky_matter_over_thread/nrf52840 -d $FW/build $FW --sysbuild -- -DBOARD_ROOT=$FW
```

Each pristine build generates new factory data with a random setup code. The QR code and the manual pairing code are in `build/matter_factory_data/zephyr/factory_data.png` and `factory_data.txt`.

The device uses the Matter test vendor ID and test certificates. In Home Assistant, turn on the test DCL option of the Matter server before pairing it.

## Flashing

Flashing goes through J2 (3V3, GND, RST, TX, DIO, CLK). Pull JP1 and power the board from the probe.

- With a J-Link: `west flash -d $FW/build`
- With a Raspberry Pi Pico running debugprobe: `west flash -d $FW/build --runner pyocd`

Recent nRF52840 chips ship with readback protection on. A J-Link clears it with `nrfutil device recover`.

## Regenerating the data model

After editing `src/default_zap/linky.zap`, run `west zap-generate -z src/default_zap/linky.zap -o src/default_zap/zap-generated` from the SDK folder.

The `package` paths in `linky.zap` point at the SDK through a relative path. They assume this repository's location on my machine and the SDK in `/opt/nordic/ncs/v3.4.2`, so adjust them if yours differ.

## Tests

```
make -C firmware/tests/host
```

These tests build frames from the Enedis spec, then check the decoder on:
- both modes;
- timestamped groups;
- bad checksums and parity errors;
- interrupted frames and a stream picked up mid-frame;
- every chunk size;
- the Tempo index mapping;
- the power estimate.

## License

The files based on Nordic's Matter template (`CMakeLists.txt`, `src/main.cpp`, `src/app_task.*`, `src/chip_project_config.h`) keep Nordic's license, LicenseRef-Nordic-5-Clause, as their headers say.
