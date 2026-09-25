#include <unity.h>

#include "../include/charger_adc_freshness.h"
#include "../include/charger_controller.h"

static charger_control_input_t fresh_input()
{
  charger_control_input_t input = {};
  input.now_ms = 100u;
  input.adc_fresh = 1u;
  input.input_voltage = 32.0f;
  input.battery_voltage = 27.0f;
  input.charge_voltage = 28.0f;
  input.current = 0.5f;
  input.max_voltage = 29.4f;
  input.max_current = 1.2f;
  input.requested_end_voltage = 29.2f;
  input.min_docked_voltage = 20.0f;
  input.charger_detect_voltage = 30.0f;
  input.connection_delay_ms = 100u;
  input.pwm_max = 1350u;
  return input;
}

static void test_lower_voltage_limit_stops_pwm_immediately_in_cc()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CC, 500u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.battery_voltage = 28.0f;
  input.charge_voltage = 28.0f;
  input.max_voltage = 27.5f;

  charger_control_step(&control, &input, &output);

  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CHARGING_CV, output.state);
  TEST_ASSERT_EQUAL_UINT8(1u, output.tf4_level);
}

static void test_lower_voltage_limit_stops_pwm_immediately_in_cv()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CV, 800u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.battery_voltage = 26.0f;
  input.charge_voltage = 28.0f;
  input.max_voltage = 27.0f;

  charger_control_step(&control, &input, &output);

  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CHARGING_CV, output.state);
}

static void test_lower_limit_below_battery_never_increases_cc_pwm()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CC, 120u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.battery_voltage = 28.0f;
  input.charge_voltage = 27.0f;
  input.max_voltage = 27.5f;

  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_UINT16(119u, output.pwm);

  input.now_ms++;
  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_UINT16(118u, output.pwm);
}

static void test_cv_respects_effective_target_below_runtime_ceiling()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CV, 200u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.requested_end_voltage = 27.5f;
  input.max_voltage = 29.4f;
  input.battery_voltage = 26.0f;
  input.charge_voltage = 28.0f;
  input.current = 0.05f;

  charger_control_step(&control, &input, &output);

  TEST_ASSERT_EQUAL_UINT16(199u, output.pwm);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CHARGING_CV, output.state);
}

static void test_repeated_lower_limit_remains_a_hard_ceiling()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CV, 100u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.max_voltage = 28.0f;
  input.charge_voltage = 28.0f;

  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);

  input.now_ms++;
  input.max_voltage = 27.0f;
  input.charge_voltage = 27.0f;
  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
}

static void test_stale_feedback_fails_safe_and_restarts_connection_sequence()
{
  charger_control_t control = {CHARGER_CONTROL_CHARGING_CV, 600u, 0u};
  charger_control_output_t output;
  charger_control_input_t input = fresh_input();
  input.adc_fresh = 0u;

  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_IDLE, output.state);
  TEST_ASSERT_EQUAL_UINT8(1u, output.tf4_level);

  input.adc_fresh = 1u;
  input.now_ms = 200u;
  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CONNECTED, output.state);
  TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
  TEST_ASSERT_EQUAL_UINT8(0u, output.tf4_level);
  TEST_ASSERT_EQUAL_UINT8(0u, output.calibrate_current_offset);

  input.now_ms = 300u;
  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CONNECTED, output.state);
  TEST_ASSERT_EQUAL_UINT8(0u, output.calibrate_current_offset);

  input.now_ms = 301u;
  charger_control_step(&control, &input, &output);
  TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CHARGING_CC, output.state);
  TEST_ASSERT_EQUAL_UINT8(1u, output.calibrate_current_offset);
  TEST_ASSERT_EQUAL_UINT8(1u, output.tf4_level);
}

static void test_disconnect_always_resets_pwm_and_tf4_intent()
{
  const charger_control_state_t states[] = {
      CHARGER_CONTROL_CONNECTED,
      CHARGER_CONTROL_CHARGING_CC,
      CHARGER_CONTROL_CHARGING_CV};
  unsigned int i;

  for (i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
    charger_control_t control = {states[i], 700u, 10u};
    charger_control_output_t output;
    charger_control_input_t input = fresh_input();
    input.input_voltage = 19.0f;
    input.now_ms = 150u;
    charger_control_step(&control, &input, &output);
    TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_IDLE, output.state);
    TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
    TEST_ASSERT_EQUAL_UINT8(1u, output.tf4_level);
  }
}

static void test_disconnect_during_each_connection_delay_boundary()
{
  const uint32_t disconnect_times[] = {100u, 150u, 199u};
  unsigned int i;

  for (i = 0; i < sizeof(disconnect_times) / sizeof(disconnect_times[0]); ++i) {
    charger_control_t control = {CHARGER_CONTROL_IDLE, 0u, 0u};
    charger_control_output_t output;
    charger_control_input_t input = fresh_input();
    charger_control_step(&control, &input, &output);
    TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_CONNECTED, output.state);

    input.input_voltage = 19.0f;
    input.now_ms = disconnect_times[i];
    charger_control_step(&control, &input, &output);
    TEST_ASSERT_EQUAL_INT(CHARGER_CONTROL_IDLE, output.state);
    TEST_ASSERT_EQUAL_UINT16(0u, output.pwm);
    TEST_ASSERT_EQUAL_UINT8(1u, output.tf4_level);
  }
}

static void mark_required_channels(charger_adc_freshness_t *progress, uint32_t tick)
{
  uint8_t channel;
  for (channel = 0; channel < CHARGER_ADC_REQUIRED_COUNT; ++channel) {
    charger_adc_freshness_mark(progress, channel, tick);
  }
}

static void test_adc_freshness_requires_completion_progress_not_value_change()
{
  charger_adc_freshness_t progress;
  charger_adc_freshness_reset(&progress);
  TEST_ASSERT_EQUAL_UINT8(0u, charger_adc_freshness_is_fresh(&progress, 100u, 50u));

  mark_required_channels(&progress, 100u);
  TEST_ASSERT_EQUAL_UINT8(1u, charger_adc_freshness_is_fresh(&progress, 150u, 50u));
  TEST_ASSERT_EQUAL_UINT8(0u, charger_adc_freshness_is_fresh(&progress, 151u, 50u));
  /* Repeated identical sensor values are fresh when conversion completions advance. */
  mark_required_channels(&progress, 151u);
  TEST_ASSERT_EQUAL_UINT8(1u, charger_adc_freshness_is_fresh(&progress, 151u, 50u));
}

static void test_adc_partial_scan_stall_is_not_fresh()
{
  charger_adc_freshness_t progress;
  charger_adc_freshness_reset(&progress);
  mark_required_channels(&progress, 100u);

  charger_adc_freshness_mark(&progress, CHARGER_ADC_CURRENT, 200u);
  charger_adc_freshness_mark(&progress, CHARGER_ADC_CHARGE_VOLTAGE, 200u);
  charger_adc_freshness_mark(&progress, CHARGER_ADC_INPUT_VOLTAGE, 200u);
  TEST_ASSERT_EQUAL_UINT8(0u, charger_adc_freshness_is_fresh(&progress, 151u, 50u));
  TEST_ASSERT_EQUAL_UINT8(0u, charger_adc_freshness_is_fresh(&progress, 200u, 50u));
}

static void test_adc_freshness_tick_wrap_is_safe()
{
  charger_adc_freshness_t progress;
  charger_adc_freshness_reset(&progress);
  mark_required_channels(&progress, 0xFFFFFFF0u);
  TEST_ASSERT_EQUAL_UINT8(1u, charger_adc_freshness_is_fresh(&progress, 0x00000010u, 32u));
  TEST_ASSERT_EQUAL_UINT8(0u, charger_adc_freshness_is_fresh(&progress, 0x00000011u, 32u));
}

void run_charger_safety_tests()
{
  RUN_TEST(test_lower_voltage_limit_stops_pwm_immediately_in_cc);
  RUN_TEST(test_lower_voltage_limit_stops_pwm_immediately_in_cv);
  RUN_TEST(test_lower_limit_below_battery_never_increases_cc_pwm);
  RUN_TEST(test_cv_respects_effective_target_below_runtime_ceiling);
  RUN_TEST(test_repeated_lower_limit_remains_a_hard_ceiling);
  RUN_TEST(test_stale_feedback_fails_safe_and_restarts_connection_sequence);
  RUN_TEST(test_disconnect_always_resets_pwm_and_tf4_intent);
  RUN_TEST(test_disconnect_during_each_connection_delay_boundary);
  RUN_TEST(test_adc_freshness_requires_completion_progress_not_value_change);
  RUN_TEST(test_adc_partial_scan_stall_is_not_fresh);
  RUN_TEST(test_adc_freshness_tick_wrap_is_safe);
}
