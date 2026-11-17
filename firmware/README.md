# Firmware

Work in progress. The target is Zephyr with the nRF Connect SDK v3.4.2, running Matter over Thread as a sleepy end device.

What is here so far:

- `src/tic/` decodes the TIC in both modes. It has no Zephyr dependency.
  - `tic_parser` checks the parity bit of every character, the checksum of every group, and drops any frame with an error, a missing ETX or an EOT.
  - `tic_meter` turns a valid frame into one reading: energy per supplier index, current index, apparent power, current, voltage. Historic labels are mapped onto the standard index numbers (HCHC = 1, HCHP = 2, Tempo 1 to 6), so the rest of the firmware does not care which mode the meter is in.
  - `tic_power_estimator` derives active power from the energy counter, since the Linky only reports apparent power.
- `tests/host/` runs the decoder on a computer, with frames built from the Enedis spec:

  ```
  make -C firmware/tests/host
  ```

Next: the board definition, then the Matter application (Electrical Sensor device type with the Electrical Energy and Power Measurement clusters).
