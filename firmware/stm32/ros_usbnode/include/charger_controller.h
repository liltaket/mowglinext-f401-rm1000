#ifndef MOWGLI_CHARGER_CONTROLLER_H
#define MOWGLI_CHARGER_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CHARGER_CONTROL_IDLE = 0,
  CHARGER_CONTROL_CONNECTED,
  CHARGER_CONTROL_CHARGING_CC,
  CHARGER_CONTROL_CHARGING_CV,
  CHARGER_CONTROL_END_CHARGING
} charger_control_state_t;

typedef struct {
  charger_control_state_t state;
  uint16_t pwm;
  uint32_t connected_since_ms;
} charger_control_t;

typedef struct {
  uint32_t now_ms;
  uint8_t adc_fresh;
  float input_voltage;
  float battery_voltage;
  float charge_voltage;
  float current;
  float max_voltage;
  float max_current;
  float requested_end_voltage;
  float min_docked_voltage;
  float charger_detect_voltage;
  uint32_t connection_delay_ms;
  uint16_t pwm_max;
} charger_control_input_t;

typedef struct {
  charger_control_state_t state;
  uint16_t pwm;
  /* Logical output level only; no electrical meaning is assigned here. */
  uint8_t tf4_level;
  uint8_t calibrate_current_offset;
} charger_control_output_t;

static inline uint16_t charger_control_step(charger_control_t *control,
                                            const charger_control_input_t *input,
                                            charger_control_output_t *output)
{
  const float target_voltage =
      input->requested_end_voltage < input->max_voltage
          ? input->requested_end_voltage
          : input->max_voltage;

  output->calibrate_current_offset = 0u;

  /* A stale or incomplete scan drops charge output and exits the timed dock
   * sequence. Recovery must observe all required channels fresh and restart
   * through IDLE/CONNECTED. */
  if (!input->adc_fresh || input->input_voltage < input->min_docked_voltage) {
    control->state = CHARGER_CONTROL_IDLE;
    control->pwm = 0u;
    output->tf4_level = 1u;
    goto done;
  }

  switch (control->state) {
  case CHARGER_CONTROL_CONNECTED:
    control->pwm = 0u;
    output->tf4_level = 0u;
    if ((uint32_t)(input->now_ms - control->connected_since_ms) >
        input->connection_delay_ms) {
      control->state = CHARGER_CONTROL_CHARGING_CC;
      output->calibrate_current_offset = 1u;
      output->tf4_level = 1u;
    }
    break;

  case CHARGER_CONTROL_CHARGING_CC:
  case CHARGER_CONTROL_CHARGING_CV:
    output->tf4_level = 1u;
    /* A live ceiling reduction must stop PWM as soon as feedback reaches the
     * new cap. CC and CV use the same effective target below it. */
    if (input->charge_voltage >= input->max_voltage) {
      control->pwm = 0u;
      control->state = CHARGER_CONTROL_CHARGING_CV;
      break;
    }

    if (control->state == CHARGER_CONTROL_CHARGING_CC) {
      if (((input->battery_voltage > target_voltage) && (control->pwm > 0u)) ||
          ((input->current > input->max_current) && (control->pwm > 39u))) {
        control->pwm--;
      } else if ((input->battery_voltage < target_voltage) &&
                 (input->current < input->max_current) &&
                 (control->pwm < input->pwm_max)) {
        control->pwm++;
      }
      if (input->charge_voltage >= target_voltage) {
        control->state = CHARGER_CONTROL_CHARGING_CV;
      }
    } else {
      if ((input->battery_voltage < target_voltage) &&
          (input->charge_voltage < target_voltage) &&
          (control->pwm < input->pwm_max)) {
        control->pwm++;
      }
      if (((input->battery_voltage > target_voltage) ||
           (input->charge_voltage > target_voltage)) && (control->pwm > 0u)) {
        control->pwm--;
      }
      if ((input->current > (input->max_current / 10.0f)) && (control->pwm > 0u)) {
        control->pwm--;
      }
    }
    break;

  case CHARGER_CONTROL_END_CHARGING:
    control->pwm = 0u;
    output->tf4_level = 1u;
    break;

  case CHARGER_CONTROL_IDLE:
  default:
    control->pwm = 0u;
    output->tf4_level = 1u;
    if (input->input_voltage >= input->charger_detect_voltage) {
      control->state = CHARGER_CONTROL_CONNECTED;
      control->connected_since_ms = input->now_ms;
      output->tf4_level = 0u;
    }
    break;
  }

done:
  if (control->pwm > input->pwm_max) {
    control->pwm = input->pwm_max;
  }
  output->state = control->state;
  output->pwm = control->pwm;
  return control->pwm;
}

#ifdef __cplusplus
}
#endif

#endif
