// ARIS Rocketry Workshop - Altimeter (SOLUTION)
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
//
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
    const float H  = 8500.0f;
    const float p0 = 101325.0f;
    return -H * log(p_Pa / p0);
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

    /* BEGIN TASK 1 (setup): initialise the DPS310 over I2C */
    Wire.begin();
    if (!dps.begin_I2C(DPS310_ADDR, &Wire)) {
        Serial.println(F("[ERR] DPS310 not found at 0x77 -- check wiring"));
        while (true) { delay(100); }
    }
    // High Precision mode: 128x oversampling at 8 Hz, ~0.3 Pa RMS noise.
    dps.configurePressure(DPS310_8HZ, DPS310_128SAMPLES);
    dps.configureTemperature(DPS310_8HZ, DPS310_128SAMPLES);
    Serial.println(F("[OK]  DPS310 ready"));
    /* END TASK 1 (setup) */

    /* BEGIN TASK 3 (setup): mount the SD card and open a fresh CSV file */
    if (!sd.begin(sdConfig)) {
        Serial.println(F("[WARN] SD init failed -- continuing without logging"));
    } else {
        // Pick the lowest unused ALT00.CSV .. ALT99.CSV.
        char fname[16];
        for (int i = 0; i < 100; ++i) {
            snprintf(fname, sizeof(fname), "ALT%02d.CSV", i);
            if (!sd.exists(fname)) break;
        }
        if (logfile.open(fname, O_WRONLY | O_CREAT | O_TRUNC)) {
            logfile.println(F("t_ms,p_Pa,p_filt_Pa,alt_m,alt_agl_m"));
            logfile.sync();
            Serial.print(F("[OK]  Logging to ")); Serial.println(fname);
        } else {
            Serial.println(F("[WARN] Could not open log file"));
        }
    }
    /* END TASK 3 (setup) */

    /* BEGIN TASK 5 (setup): start the 30 s ground-pressure calibration */
    cal_start_ms = millis();
    Serial.println(F("[CAL] Sensor warmup (10 s) + ground average (30 s) -- hold still..."));
    /* END TASK 5 (setup) */

    /* BEGIN TASK 6 (setup): NeoPixel red until calibrated */
    pixel.begin();
    pixel.setBrightness(30);   // 30/255 ~ 12%; NeoPixels are blindingly bright
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
    pixel.show();
    /* END TASK 6 (setup) */

    last_sample_ms = millis();
}


// --- loop() -------------------------------------------------------------

void loop() {
    // Run the sample step every SAMPLE_PERIOD_MS.
    uint32_t now = millis();
    if (now - last_sample_ms < SAMPLE_PERIOD_MS) return;
    last_sample_ms = now;

    /* BEGIN TASK 1: read pressure from the DPS310 (sets pressure_Pa) */
    sensors_event_t temp_event, pressure_event;
    if (!dps.getEvents(&temp_event, &pressure_event)) return;
    pressure_Pa = pressure_event.pressure * 100.0f;   // hPa -> Pa
    /* END TASK 1 */

    // Warmup: drop early samples; let the first kept sample seed the
    // EMA and start the calibration window.
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

    /* BEGIN TASK 4: exponential moving average (sets filtered_Pa) */
    //   y_n = alpha * x_n + (1 - alpha) * y_{n-1},   y_0 = x_0
    if (!ema_seeded) {
        filtered_Pa = pressure_Pa;
        ema_seeded  = true;
    } else {
        filtered_Pa = EMA_ALPHA * pressure_Pa + (1.0f - EMA_ALPHA) * filtered_Pa;
    }
    /* END TASK 4 */

    /* BEGIN TASK 2: convert filtered pressure to altitude (sets altitude_m) */
    altitude_m = pressureToAltitude(filtered_Pa);
    /* END TASK 2 */

    /* BEGIN TASK 5: accumulate during calibration, then subtract ground */
    if (!calibrated) {
        cal_sum_Pa += filtered_Pa;
        cal_count++;
        if (now - cal_start_ms >= CAL_DURATION_MS) {
            float ground_p_Pa = cal_sum_Pa / (float)cal_count;
            ground_alt_m      = pressureToAltitude(ground_p_Pa);
            calibrated        = true;
            Serial.print(F("[CAL] Done. Ground pressure = "));
            Serial.print(ground_p_Pa, 1);
            Serial.print(F(" Pa,  ground altitude = "));
            Serial.print(ground_alt_m, 2);
            Serial.println(F(" m"));
        }
        altitude_agl_m = 0.0f;   // pin to 0 while calibrating
    } else {
        altitude_agl_m = altitude_m - ground_alt_m;
    }
    /* END TASK 5 */

    /* BEGIN TASK 3: append one row to the CSV log */
    if (logfile) {
        logfile.print(now);              logfile.print(',');
        logfile.print(pressure_Pa, 2);   logfile.print(',');
        logfile.print(filtered_Pa, 2);   logfile.print(',');
        logfile.print(altitude_m, 3);    logfile.print(',');
        logfile.println(altitude_agl_m, 3);
        // Sync once per second so a power cut loses at most ~1 s.
        static uint32_t last_sync_ms = 0;
        if (now - last_sync_ms >= 1000) {
            logfile.sync();
            last_sync_ms = now;
        }
    }
    /* END TASK 3 */

    /* BEGIN TASK 6: flip the NeoPixel green when calibration is done */
    if (calibrated) {
        pixel.setPixelColor(0,pixel.Color(0, 255, 0));
    }else{
        pixel.setPixelColor(0, pixel.Color(255, 0, 0));
    }
    
    pixel.show();
    /* END TASK 6 */

    // Serial Plotter: "label:value" pairs, comma-separated.
    Serial.print(F("P_raw:"));    Serial.print(pressure_Pa, 1);
    Serial.print(F(",P_filt:")); Serial.print(filtered_Pa, 1);
    Serial.print(F(",alt:"));     Serial.print(altitude_m, 2);
    Serial.print(F(",alt_agl:")); Serial.println(altitude_agl_m, 2);
}
