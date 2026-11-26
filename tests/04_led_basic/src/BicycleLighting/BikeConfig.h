#pragma once
#include <stdint.h>

// Test defaults, NOT confirmed hardware specifications. See firmware/README.md.
namespace BikeConfig {
constexpr uint8_t SENSOR_PIN_A = 2;
constexpr uint8_t SENSOR_PIN_B = 3;
constexpr uint8_t LED_PIN_A = 6;
constexpr uint8_t MODE_SWITCH_PIN = 4; // Sketch 21: LOW=two riders, HIGH=one rider
constexpr uint8_t RESET_BUTTON_PIN = 5; // LOW=reset both sessions
constexpr bool SENSOR_ACTIVE_LOW_A = true;
constexpr bool SENSOR_ACTIVE_LOW_B = true;
constexpr bool SENSOR_PULLUP_A = true;
constexpr bool SENSOR_PULLUP_B = true;
// 0 disables interval filtering. Measure valid pulse widths before increasing.
constexpr uint32_t MIN_PULSE_GAP_US_A = 0;
constexpr uint32_t MIN_PULSE_GAP_US_B = 0;

// ADDRESSABLE GROUPS, not physical LEDs. 100 assumes 3 LEDs/group on uncut 5m.
constexpr uint16_t LED_UNITS = 100; // Confirm the actual IC/cut grouping.
constexpr uint16_t LED_LENGTH_CM = 500; // Physical length of the uncut strip, used by sketch 21.
constexpr uint8_t BRIGHTNESS = 48; // Not a current limiter or fuse.
constexpr uint8_t STORM_BRIGHTNESS = 128;
constexpr uint32_t STOP_TIMEOUT_MS = 2500;
constexpr uint32_t PHOTO_HOLD_MS = 10000;
constexpr uint32_t FADE_MS = 3000;
constexpr uint32_t CELEBRATION_MS = 6000;
constexpr uint32_t SWITCH_DEBOUNCE_MS = 30;
constexpr uint32_t LED_MIN_INTERVAL_MS = 40;
constexpr uint32_t LOG_INTERVAL_MS = 500;
// Uncalibrated defaults: include actual detection count and gearing as needed.
constexpr uint16_t PULSES_PER_REV_A = 1;
constexpr uint16_t PULSES_PER_REV_B = 1;
constexpr uint16_t TARGET_REVOLUTIONS = 30;
constexpr uint16_t FULL_EFFECT_RPM = 90;
constexpr uint8_t SPEED_STYLE = 0; // Single bicycle: 0=outward flow, 1=outward comet, 2=storm
constexpr uint8_t SPEED_STYLE_STORM = 2;
constexpr uint8_t SPEED_STYLE_BURST = 3;
constexpr uint8_t SPEED_STYLE_LIGHTNING = 4;
constexpr uint8_t SPEED_STYLE_FIREWORKS = 5;
constexpr uint8_t SPEED_STYLE_FINALE = 6;
constexpr uint8_t SPEED_STYLE_AUTO = 7;
constexpr uint8_t GOAL_STYLE = 0; // 0=gold fill, 1=rainbow fill
constexpr uint8_t GOAL_STYLE_FESTIVAL = 2;

// Festival difficulty. A (D2) is recumbent, B (D3) is upright by default.
constexpr bool FLIP_WINGS = false; // Initial value before the first EEPROM save in sketch 21.
// A: 20 slots per disc revolution. Use disc turns provisionally until the tire ratio is measured.
// B: 12 slots per disc revolution, with disc and tire reported to turn 1:1.
// Enter accepted pulses (p), not the total number of signal transitions (e).
constexpr uint16_t TIRE_CAL_PULSES_A = 20;
constexpr uint8_t TIRE_CAL_TURNS_A = 1;
constexpr uint16_t TIRE_CAL_PULSES_B = 12;
constexpr uint8_t TIRE_CAL_TURNS_B = 1;
constexpr uint16_t SHOT_REVOLUTIONS_A = 2;
constexpr uint16_t SHOT_REVOLUTIONS_B = 3;
constexpr uint16_t FESTIVAL_TARGET_A = 16;
constexpr uint16_t FESTIVAL_TARGET_B = 24;
constexpr uint16_t GOAL_FLIGHT_MS = 420;
constexpr uint16_t FESTIVAL_RESUME_MS = 15000;
constexpr uint16_t FESTIVAL_FINISH_MS = 30000;
constexpr uint8_t FESTIVAL_BRIGHTNESS = 255;
constexpr uint8_t GOAL_PATTERN_COUNT = 8;
constexpr uint8_t GOAL_RAREST_PATTERN = 7;
// IDs 1 through GOAL_PATTERN_COUNT. Probability is the weight divided by the sum.
constexpr uint8_t GOAL_WEIGHTS[GOAL_PATTERN_COUNT] = {30, 23, 18, 3, 12, 8, 1, 5};
constexpr uint8_t TBT_PATTERN_COUNT = 5;
constexpr uint8_t TBT_RAREST_PATTERN = 5;
constexpr uint8_t TBT_WEIGHTS[TBT_PATTERN_COUNT] = {30, 25, 20, 20, 5};
constexpr uint16_t goalWeightSum(const uint8_t *weights, uint8_t count) {
  return count ? weights[count - 1] + goalWeightSum(weights, count - 1) : 0;
}
constexpr uint32_t SERIAL_BAUD = 115200;
static_assert(LED_UNITS >= 2 && LED_UNITS <= 200 && LED_UNITS % 2 == 0,
              "Use an even count, 2..200; review SRAM for larger strips");
static_assert(LED_LENGTH_CM > 0, "Enter the physical strip length in cm");
static_assert(PULSES_PER_REV_A > 0 && PULSES_PER_REV_B > 0 && TARGET_REVOLUTIONS > 0,
              "Calibration and target must be positive");
static_assert(uint32_t(PULSES_PER_REV_A) * TARGET_REVOLUTIONS <= 65535UL &&
              uint32_t(PULSES_PER_REV_B) * TARGET_REVOLUTIONS <= 65535UL,
              "Target pulse count must fit 16 bits");
static_assert(TIRE_CAL_PULSES_A > 0 && TIRE_CAL_PULSES_B > 0 &&
              TIRE_CAL_TURNS_A > 0 && TIRE_CAL_TURNS_A <= 60 &&
              TIRE_CAL_TURNS_B > 0 && TIRE_CAL_TURNS_B <= 60 &&
              SHOT_REVOLUTIONS_A > 0 && SHOT_REVOLUTIONS_A <= FESTIVAL_TARGET_A &&
              SHOT_REVOLUTIONS_B > 0 && SHOT_REVOLUTIONS_B <= FESTIVAL_TARGET_B &&
              uint32_t(TIRE_CAL_PULSES_A) * FESTIVAL_TARGET_A <= 65535UL * TIRE_CAL_TURNS_A &&
              uint32_t(TIRE_CAL_PULSES_B) * FESTIVAL_TARGET_B <= 65535UL * TIRE_CAL_TURNS_B,
              "Review the festival goals and rotation calibration");
static_assert(goalWeightSum(GOAL_WEIGHTS, GOAL_PATTERN_COUNT) > 0 &&
              goalWeightSum(TBT_WEIGHTS, TBT_PATTERN_COUNT) > 0 &&
              CELEBRATION_MS < 65535UL - GOAL_FLIGHT_MS,
              "Enable a goal pattern and keep the celebration within the display timer");
static_assert(STOP_TIMEOUT_MS > 0 && STOP_TIMEOUT_MS < 0x80000000UL,
              "Invalid timeout");
static_assert(STOP_TIMEOUT_MS < FESTIVAL_RESUME_MS &&
              FESTIVAL_RESUME_MS < FESTIVAL_FINISH_MS &&
              FADE_MS < FESTIVAL_FINISH_MS - FESTIVAL_RESUME_MS &&
              CELEBRATION_MS + GOAL_FLIGHT_MS < FESTIVAL_FINISH_MS - FADE_MS,
              "Keep the festival celebration, resume window and fade inside the end timer");
static_assert(FADE_MS > 0 && PHOTO_HOLD_MS > 0 && CELEBRATION_MS > 0 && FULL_EFFECT_RPM > 0,
              "Durations and full-effect cadence must be positive");
static_assert(LED_PIN_A == 6 && SENSOR_PIN_A == 2 && SENSOR_PIN_B == 3 &&
              MODE_SWITCH_PIN == 4 && RESET_BUTTON_PIN == 5,
              "Review wiring before changing these assignments");
}

// NeoPixel macros are expanded only after Adafruit_NeoPixel.h is included.
// BRG swaps green and blue relative to GRB. Verify on the strip using sketch 04.
#define BIKE_LED_TYPE (NEO_BRG + NEO_KHZ800)
