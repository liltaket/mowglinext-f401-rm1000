/****************************************************************************
* Title                 :   charger module
* Filename              :   charger.c
* Author                :   Nekraus
* Origin Date           :   01/04/2023
* Version               :   1.0.0

*****************************************************************************/
/** \file charger.c
 *  \brief
 *
 */
/******************************************************************************
 * Includes
 *******************************************************************************/
#include "main.h"
#include "board.h"
#include "adc.h"
#include "charger.h"
#include "charger_controller.h"
#include "fw_param_catalog.h"
/******************************************************************************
 * Module Preprocessor Constants
 *******************************************************************************/

/******************************************************************************
 * Module Preprocessor Macros
 *******************************************************************************/

/******************************************************************************
 * Module Typedefs
 *******************************************************************************/

/******************************************************************************
 * Module Variable Definitions
 *******************************************************************************/

TIM_HandleTypeDef TIM1_Handle;  // PWM Charge Controller

float SOC                           = 0;
uint16_t chargecontrol_pwm_val      = 0;
uint8_t  chargecontrol_is_charging  = 0;

static float charge_end_voltage=BAT_CHARGE_CUTOFF_VOLTAGE ;
static charger_control_t charger_control = {CHARGER_CONTROL_IDLE, 0u, 0u};

/* ADC scans cycle five inputs at a 1 kHz trigger; 50 ms tolerates ten scans.
 * ChargeController runs every 10 ms, so a total stall is shut down within
 * approximately 60 ms (ADC age threshold plus one controller interval). */
#define CHARGER_ADC_MAX_AGE_MS 50u

/* Runtime charge ceiling (fw_params, protocol v7). Seeded with the compile-time
 * board_defaults.h values; init_ROS() then applies the persisted value. Clamped
 * to the absolute envelope of fw_param_catalog.h, whose top (29.4 V, 1.2 A) is
 * the pack/charger limit: nothing on the wire can overcharge. */
static volatile float g_max_charge_voltage = (float)MAX_CHARGE_VOLTAGE;
static volatile float g_max_charge_current = (float)MAX_CHARGE_CURRENT;

/* Defence in depth: fw_params already coerced the value into the envelope.
 * Non-finite or non-positive input keeps the compiled default. */
static float charger_clamp_voltage(float v) {
  if (!(v > 0.0f)) return (float)MAX_CHARGE_VOLTAGE;
  if (v < FW_ENVELOPE_CHARGE_VOLTAGE_MIN) return FW_ENVELOPE_CHARGE_VOLTAGE_MIN;
  if (v > FW_ENVELOPE_CHARGE_VOLTAGE_MAX) return FW_ENVELOPE_CHARGE_VOLTAGE_MAX;
  return v;
}
static float charger_clamp_current(float i) {
  if (!(i > 0.0f)) return (float)MAX_CHARGE_CURRENT;
  if (i < FW_ENVELOPE_CHARGE_CURRENT_MIN) return FW_ENVELOPE_CHARGE_CURRENT_MIN;
  if (i > FW_ENVELOPE_CHARGE_CURRENT_MAX) return FW_ENVELOPE_CHARGE_CURRENT_MAX;
  return i;
}

void charger_set_charge_limits(float max_voltage, float max_current) {
  g_max_charge_voltage = charger_clamp_voltage(max_voltage);
  g_max_charge_current = charger_clamp_current(max_current);
}

/******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/******************************************************************************
 *  Public Functions
 *******************************************************************************/


/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
 void TIM1_Init(void)
{
  /* USER CODE BEGIN TIM1_Init 0 */

  __HAL_RCC_TIM1_CLK_ENABLE();
  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  TIM1_Handle.Instance = TIM1;
  TIM1_Handle.Init.Prescaler = 0;
  TIM1_Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
  TIM1_Handle.Init.Period = 1400;
  TIM1_Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  TIM1_Handle.Init.RepetitionCounter = 0;
  TIM1_Handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&TIM1_Handle) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&TIM1_Handle, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&TIM1_Handle, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 120;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&TIM1_Handle, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_ENABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_ENABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_1;
  sBreakDeadTimeConfig.DeadTime = 40;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&TIM1_Handle, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

  GPIO_InitTypeDef GPIO_InitStruct = {0};  
  CHARGE_GPIO_CLK_ENABLE();
  /** TIM1 GPIO Configuration
  PA7 or PE8     -----> TIM1_CH1N
  PA8 oe PE9    ------> TIM1_CH1
  */
  GPIO_InitStruct.Pin = CHARGE_LOWSIDE_PIN|CHARGE_HIGHSIDE_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
#if BOARD_YARDFORCE500_VARIANT_B
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
#endif
  HAL_GPIO_Init(CHARGE_GPIO_PORT, &GPIO_InitStruct);

#if BOARD_YARDFORCE500_VARIANT_ORIG
  // TODO: Is something equivalent needed for the STM32f4?
  __HAL_AFIO_REMAP_TIM1_ENABLE();        // to use PE8/9 it is a full remap
#endif


    // Charge CH1/CH1N PWM Timer
  TIM1->CCR1 = 0;  
  HAL_TIM_PWM_Start(&TIM1_Handle, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&TIM1_Handle, TIM_CHANNEL_1);
  DB_TRACE(" * Charge Controler PWM Timers initialized\r\n");
}

void charger_set_end_voltage(float v) {
    /* Limit input to reasonable values. */
    if (v>g_max_charge_voltage) {
      v=g_max_charge_voltage;
    } else if (v<LOW_BAT_THRESHOLD) {
      v=LOW_BAT_THRESHOLD;
    }
    /* Go back to constant current, if voltage is increased. */
    if (v>charge_end_voltage && charger_control.state==CHARGER_CONTROL_CHARGING_CV) {
      charger_control.state=CHARGER_CONTROL_CHARGING_CC;
    }
    charge_end_voltage=v;
 }

/*
 * manages the charge voltage, and charge, lowbat LED
 * improvementt need to be done to avoid sparks when connected charger and disconnected 
 * todo PID current measure
 * needs to be called frequently
 */
void ChargeController(void)
{                        
  const uint32_t now_ms = HAL_GetTick();
  const uint8_t adc_fresh = ADC_ChargingFeedbackIsFresh(now_ms, CHARGER_ADC_MAX_AGE_MS);
  charger_control_input_t input = {
      .now_ms = now_ms,
      .adc_fresh = adc_fresh,
      .input_voltage = chargerInputVoltage,
      .battery_voltage = battery_voltage,
      .charge_voltage = charge_voltage,
      .current = current,
      .max_voltage = g_max_charge_voltage,
      .max_current = g_max_charge_current,
      .requested_end_voltage = charge_end_voltage,
      .min_docked_voltage = MIN_DOCKED_VOLTAGE,
      .charger_detect_voltage = 30.0f,
      .connection_delay_ms = 100u,
      .pwm_max = 1350u};
  charger_control_output_t output;
  charger_control_step(&charger_control, &input, &output);

  if (output.calibrate_current_offset != 0u)
  {
    charge_current_offset.f = current_without_offset;
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, charge_current_offset.u[0]);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4, charge_current_offset.u[1]);
    HAL_PWR_DisableBkUpAccess();
  }

  if ((adc_fresh != 0u) &&
      (charger_control.state == CHARGER_CONTROL_CHARGING_CV) &&
      (current < CHARGE_END_LIMIT_CURRENT))
  {
    ampere_acc.f = 2.8f;
  }

  if (adc_fresh != 0u)
  {
    ampere_acc.f += ((current - charge_current_offset.f)/(100*60*60));
  }
  if (ampere_acc.f >= 2.8f) {
    ampere_acc.f = 2.8f;
  }
  SOC = ampere_acc.f / 2.8f;

    // Writes a data in a RTC Backup data Register 1
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, ampere_acc.u[0]);    
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, ampere_acc.u[1]);   
    HAL_PWR_DisableBkUpAccess(); 

    chargecontrol_is_charging = (uint8_t)output.state;
    chargecontrol_pwm_val = output.pwm;
    HAL_GPIO_WritePin(TF4_GPIO_PORT, TF4_PIN, output.tf4_level ? 1 : 0);
    TIM1->CCR1 = output.pwm;
    
}

/******************************************************************************
 *  Private Functions
 *******************************************************************************/
