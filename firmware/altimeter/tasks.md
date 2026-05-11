# Altimeter Workshop: Tasks

Build an altimeter on the Adafruit Feather RP2040 Adalogger, one exercise at a time. By the end you'll have a device that:

1. Reads air pressure from a barometer
2. Converts that to altitude
3. Logs each sample to a CSV file on microSD
4. Smooths the signal with a filter
5. Calibrates against local ground pressure so it reads *altitude above the launch point*
6. Lights its onboard NeoPixel green when it's ready

## Hardware

| Item | Purpose |
|---|---|
| Feather RP2040 Adalogger | MCU + built-in microSD slot |
| DPS310 barometer | pressure sensor (I²C) |
| microSD card (FAT32) | stores the CSV log |
| USB-C cable | flash + Serial Monitor |
| 4× jumper wires | DPS310 to Feather I²C header |

Wiring: `VIN→3V`, `GND→GND`, `SDA→SDA`, `SCL→SCL`. The microSD just slots into the Feather.

## Software setup

- Arduino IDE 2.x
- Board package: *Raspberry Pi Pico/RP2040* (Earle Philhower)
- Board: **Adafruit Feather RP2040 Adalogger**
- Libraries (Library Manager): [**Adafruit DPS310**](https://github.com/adafruit/Adafruit_DPS310), [**Adafruit NeoPixel**](https://github.com/adafruit/Adafruit_NeoPixel), [**SdFat (Adafruit Fork)**](https://github.com/greiman/SdFat)

Open `altimeter_template/altimeter_template.ino`. The matching `altimeter_solution/altimeter_solution.ino` is next to it. Try not to peek.

## How the sketch is organised

One file. You fill in the regions marked

```c
/* BEGIN TASK 1: ... */
// TODO
/* END TASK 1 */
```

Some tasks have one region in `setup()` and one in `loop()`.

---

## Task 1: Read the pressure sensor

**Goal.** Get one pressure value per sample onto Serial.

**Background.** The DPS310 is a digital barometer. The Adafruit library hides I²C: bring it up once, then ask for a sample whenever you want one. It returns pressure in **hPa**; Task 2 wants **Pa**.

**Hints.** [`Wire.begin()`](https://docs.arduino.cc/learn/communication/wire/), `dps.begin_I2C(addr, &Wire)`, `dps.configurePressure(rate, samples)`, `dps.getEvents(&temp_event, &pressure_event)`, `pressure_event.pressure`. [DPS310 API](https://github.com/adafruit/Adafruit_DPS310/blob/master/Adafruit_DPS310.h).

**Fill in.**

- `TASK 1 (setup)`: bring up I²C and the DPS310 in High Precision mode (`DPS310_8HZ`, `DPS310_128SAMPLES`).
- `TASK 1` (loop): read a sample and store it in `pressure_Pa` (in pascals).

**Run.** Upload, open *Tools → Serial Monitor* at 115200 baud.

**Observe.**

- First 10 s the scaffold drops every reading while the DPS310 warms up. You'll see "Warmup done", then values appear.
- `P_raw:` sits in **95 000 to 101 000 Pa**, noisy by a few Pa.
- Open *Tools → Serial Plotter*: flat but jittery.
- Lift the altimeter 1 m: pressure drops. Push it down: pressure rises.

---

## Task 2: Convert pressure to altitude

**Goal.** Turn pressure into a height in metres.

**Background.** Pressure decreases with altitude. The workshop formula (slide 22):

$$ h(p) = -H \cdot \ln(p / p_0) $$

with $H = 8500$ m and $p_0 = 101325$ Pa. C's `log()` is the natural log.

**Hints.** [`log(x)`](https://en.cppreference.com/w/c/numeric/math/log) from `<math.h>`, plus your own `pressureToAltitude()` once written.

**Fill in.**

- `TASK 2a`: implement the formula inside `pressureToAltitude()`.
- `TASK 2` (loop): call your function on `filtered_Pa`, store in `altitude_m`.

We use `filtered_Pa` so Task 4's filter feeds in automatically; until then it's a pass-through of the raw value.

**Observe.**

- `alt:` reads near your real altitude above sea level, ± a few hundred metres (St. Gallen ≈ 670 m). Task 5 fixes the offset.
- Altitude curve mirrors pressure flipped.
- Lift it ~1 m, altitude jumps ~1 m.

---

## Task 3: Log to the SD card

**Goal.** Save every sample to a CSV file.

**Background.** Serial only works while tethered. The Adalogger's microSD is on SPI1, so we use `SdFat` (the plain `SD` library can't reach SPI1). The scaffolding handles that.

**Hints.** `sd.begin(sdConfig)`, `sd.exists(name)`, `logfile.open(name, O_WRONLY | O_CREAT | O_TRUNC)`, `logfile.print(...)`, `logfile.println(...)`, `logfile.sync()`. [SdFat API](https://github.com/greiman/SdFat/blob/master/doc/usingSdFat.txt).

**Fill in.**

- `TASK 3 (setup)`: mount the card, open the next unused `ALT00.CSV`..`ALT99.CSV`, write the header `t_ms,p_Pa,p_filt_Pa,alt_m,alt_agl_m`. Fail soft if there's no card.
- `TASK 3` (loop): write one comma-separated row per sample. Sync to disk about once a second.

**Run.** Insert the SD card, upload, run ~30 s, unplug, pop the card into your laptop.

**Observe.**

- `ALT00.CSV` (or `ALT01.CSV` ...) on the card.
- Header row plus a few hundred data rows.
- Plot `alt_m` vs `t_ms`: noisy. Hang onto this; Task 4 fixes it.

---

## Task 4: Smooth with an EMA filter

**Goal.** Cut the sensor noise.

**Background.** Every reading jitters by a few Pa. The simplest useful filter (slides 24 to 27) is an **Exponential Moving Average**:

$$ y_n = \alpha\,x_n + (1 - \alpha)\,y_{n-1} $$

$\alpha \in (0, 1]$: small = smooth but laggy, $\alpha = 1$ = no filter. Initialise $y_0 = x_0$ on the very first sample.

**Hints.** Just the formula. The `ema_seeded` flag handles the first-sample case.

**Fill in.** `TASK 4` (loop): translate the formula into code that writes `filtered_Pa`. Seed it from `pressure_Pa` the first time round.

**Observe.**

- Plotter now shows `P_raw` (jittery) plus `P_filt` (smooth, through the middle).
- Tune `EMA_ALPHA` at the top of the file. Try `0.05`, `0.20`, `0.50`, `1.00`. Small α: smooth but laggy. Large α: barely filtered.
- Re-run with the SD card. Compare `p_Pa` and `p_filt_Pa` in your spreadsheet.

> **Bonus.** Slide-24 notebook ([github.com/Rechenmaschine/rosenberg-jupyter-demos](https://github.com/Rechenmaschine/rosenberg-jupyter-demos)): EMA vs SMA vs Kalman on real flight data.

---

## Task 5: Calibrate ground level

**Goal.** Report altitude *above the launch point* (AGL).

**Background.** The constant $p_0 = 101325$ Pa is wrong by tens of hPa on any given day, and we care about "how high above the pad?", not the ocean. Fix: sit still for 30 s, average the pressure, treat that as the ground reference. Then $\text{altitude}_{\text{AGL}} = \text{altitude} - \text{altitude}_{\text{ground}}$.

**Hints.** [`millis()`](https://docs.arduino.cc/language-reference/en/functions/time/millis/) for time, your own `pressureToAltitude()`, and the `cal_*` state variables already declared up top.

**Fill in.**

- `TASK 5 (setup)`: start the calibration timer.
- `TASK 5` (loop): for `CAL_DURATION_MS`, accumulate pressure samples and pin `altitude_agl_m = 0`. Once that window has elapsed, freeze the average as the ground reference and compute AGL from then on.

**Run.** Flat on a table, reset the Feather, don't touch it for 30 s.

**Observe.**

- First 30 s: `alt_agl = 0`.
- `[CAL] Done.` prints, then `alt_agl` snaps near 0 at rest.
- Lift it: `alt_agl` reads how high you lifted by. Walk upstairs and watch it climb.

---

## Task 6: Status LED

**Goal.** **Red** = calibrating, **green** = ready.

**Background.** The Adalogger has a NeoPixel on pin 17. We dim it with `setBrightness(30)` because full-brightness NeoPixels hurt to look at.

**Hints.** `pixel.begin()`, `pixel.setBrightness(...)`, `pixel.setPixelColor(0, pixel.Color(r, g, b))`, `pixel.show()`.

**Fill in.**

- `TASK 6 (setup)`: bring up the pixel, dim it, set red, show.
- `TASK 6` (loop, after Task 3): set red if not calibrated, green if calibrated, show.

**Observe.**

- Red at boot, stays red through warmup + cal (~40 s).
- Flips green the moment `[CAL] Done.` prints.

---

## What you built

A calibrated, filtered, SD-logging altimeter with a status LED. Mount it on a rocket, launch, and the CSV reveals the full flight profile: ascent rate, peak altitude, descent rate, landing time.
