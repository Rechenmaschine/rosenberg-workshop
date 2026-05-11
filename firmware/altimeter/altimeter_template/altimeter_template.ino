// ARIS Rocketry Workshop -- Altimeter (TEMPLATE)
//
// This is your working sketch. You will fill in six blocks of code,
// one per workshop exercise, marked like this:
//
//     /* BEGIN TASK N: ... */
//     // TODO: your code here
//     /* END TASK N */
//
// The rest of the file (globals, Serial setup, the loop timer, the
// Serial Plotter print) is already wired up so you can focus on the
// interesting part of each task. Read `tasks.md` next to this file --
// it walks through each task in order with what to do, where to put
// it, and what to look for when you run it.
//
// Hardware:
//   - Feather RP2040 Adalogger              (built-in microSD on SPI1,
//                                            built-in NeoPixel on pin 17)
//   - DPS310 barometer on the I2C header    (Wire, address 0x77)
//
// Libraries (Arduino IDE -> Library Manager):
//   - Adafruit DPS310
//   - Adafruit NeoPixel
//   - SdFat - Adafruit Fork
// Board package: "Raspberry Pi Pico/RP2040" by Earle Philhower.
// Pick board: "Adafruit Feather RP2040 Adalogger".

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <Adafruit_DPS310.h>
#include <Adafruit_NeoPixel.h>
#include "SdFat.h"


// --- hardware constants -------------------------------------------------

#define DPS310_ADDR   0x77    // I2C address of the DPS310
#define SD_CS_PIN     23      // chip-select for the built-in microSD slot
#define NEOPIXEL_PIN  17      // built-in NeoPixel on the Adalogger

Adafruit_DPS310   dps;
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SdFat             sd;
FsFile            logfile;

// The Adalogger's microSD is wired to SPI1 (not the default SPI), so we
// pass SdFat its own SdSpiConfig. 16 MHz is a safe clock speed.
SdSpiConfig     sdConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &SPI1);


// --- sampling ------------------------------------------------------------

// 125 ms = 8 Hz, matches the DPS310's High Precision measurement rate.
const uint32_t SAMPLE_PERIOD_MS = 125;
uint32_t       last_sample_ms   = 0;

// Discard early readings while the DPS310 warms up. Bump to 15-20s if
// alt_agl still snaps to a non-zero value when calibration finishes.
const uint32_t WARMUP_MS = 10000;


// --- shared state, updated each loop iteration --------------------------
// Defaults are sane standins so the pipeline still produces readable
// numbers before earlier tasks are filled in.

float pressure_Pa    = 101325.0f;   // raw pressure from sensor, in pascals
float filtered_Pa    = 101325.0f;   // pressure after TASK 4's EMA filter
float altitude_m     = 0.0f;        // absolute altitude (MSL), from TASK 2
float altitude_agl_m = 0.0f;        // altitude above ground, from TASK 5


// --- TASK 4: filter state -----------------------------------------------

const float EMA_ALPHA = 0.50f;      // tune this! 1.0 = no filter, 0.0 = frozen
bool        ema_seeded = false;


// --- TASK 5: calibration state ------------------------------------------

const uint32_t CAL_DURATION_MS = 30000;   // 30 seconds (~240 samples at 8 Hz)
uint32_t       cal_start_ms    = 0;
float          cal_sum_Pa      = 0.0f;
uint32_t       cal_count       = 0;
bool           calibrated      = false;
float          ground_alt_m    = 0.0f;


// Workshop barometric formula:  h(p) = -H * ln(p / p0),
// with H = 8500 m (scale height) and p0 = 101325 Pa (sea level).
float pressureToAltitude(float p_Pa) {
    /* BEGIN TASK 2a: return the altitude in metres for the given pressure */
    // TODO: replace this stub with the real formula.
    (void)p_Pa;
    return 0.0f;
    /* END TASK 2a */
}


// --- setup() ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    // Wait up to 3 s for Serial, but don't block forever.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) { delay(10); }

    Serial.println();
    Serial.println(F("ARIS Altimeter"));

    /* BEGIN TASK 1 (setup): initialise the DPS310 over I2C
     *
     * Bring up I2C and the DPS310. Halt with an error if it doesn't
     * answer. Configure it in High Precision mode.
     */
    // TODO

    /* END TASK 1 (setup) */

    /* BEGIN TASK 3 (setup): mount the SD card and open a fresh CSV file
     *
     * Mount the card, find the next unused name ALT00.CSV..ALT99.CSV,
     * open it, and write the header row
     * "t_ms,p_Pa,p_filt_Pa,alt_m,alt_agl_m". Fail soft if no card.
     */
    // TODO

    /* END TASK 3 (setup) */

    /* BEGIN TASK 5 (setup): start the 30 s ground-pressure calibration
     *
     * Remember the start time and print a "hold still" message.
     */
    // TODO

    /* END TASK 5 (setup) */

    /* BEGIN TASK 6 (setup): light the NeoPixel red (not calibrated yet)
     *
     * Bring up the pixel, dim it, and show red. Full-brightness NeoPixels
     * hurt to look at -- use setBrightness around 30 (~12%).
     */
    // TODO

    /* END TASK 6 (setup) */

    last_sample_ms = millis();
}


// --- loop() -------------------------------------------------------------

void loop() {
    // Run the sample step every SAMPLE_PERIOD_MS.
    uint32_t now = millis();
    if (now - last_sample_ms < SAMPLE_PERIOD_MS) return;
    last_sample_ms = now;

    /* BEGIN TASK 1: read pressure from the DPS310 (sets pressure_Pa)
     *
     * Grab a fresh sample, return early on failure, and store the
     * pressure in pressure_Pa. The library returns hPa; we want Pa.
     */
    // TODO

    /* END TASK 1 */

    // Warmup: drop early samples; let the first kept sample seed the
    // EMA and start the calibration window. Scaffolding -- not a task.
    static bool warmup_announced = false;
    if (now < WARMUP_MS) {
        ema_seeded   = false;
        cal_start_ms = now;
        return;
    }
    if (!warmup_announced) {
        Serial.println(F("[CAL] Warmup done -- averaging ground pressure"));
        warmup_announced = true;
    }

    /* BEGIN TASK 4: exponential moving average (sets filtered_Pa)
     *
     *   y_n = alpha * x_n + (1 - alpha) * y_{n-1},   y_0 = x_0
     *
     * Use EMA_ALPHA as alpha and ema_seeded for the first-sample case.
     * The default behaviour below is a pass-through (no filtering) so
     * the rest of the sketch keeps working before this task is done.
     * Replace it with the real filter when you get to TASK 4.
     */
    filtered_Pa = pressure_Pa;   // <-- replace me in TASK 4

    /* END TASK 4 */

    /* BEGIN TASK 2: convert filtered pressure to altitude (sets altitude_m)
     *
     * Apply pressureToAltitude() to filtered_Pa and store in altitude_m.
     */
    // TODO

    /* END TASK 2 */

    /* BEGIN TASK 5: accumulate during calibration, then subtract ground
     *
     * For CAL_DURATION_MS, accumulate filtered_Pa into the cal_* state
     * and pin altitude_agl_m to 0 (we don't want the noisy absolute
     * altitude on the plotter during this window). Once the window
     * elapses, freeze the average as ground_alt_m and compute AGL from
     * then on.
     *
     * Default below: no calibration, AGL == absolute altitude. Replace
     * when you get to TASK 5.
     */
    altitude_agl_m = altitude_m;   // <-- replace me in TASK 5

    /* END TASK 5 */

    /* BEGIN TASK 3: append one row to the CSV log
     *
     * If the file is open, write one comma-separated row of
     * (now, pressure_Pa, filtered_Pa, altitude_m, altitude_agl_m) and
     * terminate with println. Sync about once a second so a power cut
     * loses at most ~1 s of data.
     */
    // TODO

    /* END TASK 3 */

    /* BEGIN TASK 6: flip the NeoPixel green once calibration is done
     *
     * Set the pixel red if not calibrated, green if calibrated. Don't
     * forget to call show().
     */
    // TODO

    /* END TASK 6 */

    // Serial Plotter: "label:value" pairs, comma-separated.
    Serial.print(F("P_raw:"));    Serial.print(pressure_Pa, 1);
    Serial.print(F(",P_filt:")); Serial.print(filtered_Pa, 1);
    Serial.print(F(",alt:"));     Serial.print(altitude_m, 2);
    Serial.print(F(",alt_agl:")); Serial.println(altitude_agl_m, 2);
}
