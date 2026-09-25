#ifndef MOWGLI_CHARGER_ADC_FRESHNESS_H
#define MOWGLI_CHARGER_ADC_FRESHNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADC channels used to decide whether charging may run. NTC is intentionally
 * excluded: it does not feed the charge controller's voltage/current loops. */
enum {
  CHARGER_ADC_CURRENT = 0,
  CHARGER_ADC_CHARGE_VOLTAGE,
  CHARGER_ADC_BATTERY_VOLTAGE,
  CHARGER_ADC_INPUT_VOLTAGE,
  CHARGER_ADC_REQUIRED_COUNT
};

#define CHARGER_ADC_REQUIRED_MASK ((uint8_t)((1u << CHARGER_ADC_REQUIRED_COUNT) - 1u))

typedef struct {
  uint32_t completed_at_ms[CHARGER_ADC_REQUIRED_COUNT];
  uint8_t valid_mask;
} charger_adc_freshness_t;

static inline void charger_adc_freshness_reset(volatile charger_adc_freshness_t *progress)
{
  uint8_t i;
  for (i = 0; i < CHARGER_ADC_REQUIRED_COUNT; ++i) {
    progress->completed_at_ms[i] = 0u;
  }
  progress->valid_mask = 0u;
}

static inline void charger_adc_freshness_mark(volatile charger_adc_freshness_t *progress,
                                               uint8_t channel, uint32_t now_ms)
{
  if (channel < CHARGER_ADC_REQUIRED_COUNT) {
    progress->completed_at_ms[channel] = now_ms;
    progress->valid_mask |= (uint8_t)(1u << channel);
  }
}

static inline uint8_t charger_adc_freshness_is_fresh(
    const volatile charger_adc_freshness_t *progress, uint32_t now_ms, uint32_t max_age_ms)
{
  uint8_t i;
  if ((progress->valid_mask & CHARGER_ADC_REQUIRED_MASK) != CHARGER_ADC_REQUIRED_MASK) {
    return 0u;
  }
  for (i = 0; i < CHARGER_ADC_REQUIRED_COUNT; ++i) {
    if ((uint32_t)(now_ms - progress->completed_at_ms[i]) > max_age_ms) {
      return 0u;
    }
  }
  return 1u;
}

#ifdef __cplusplus
}
#endif

#endif
