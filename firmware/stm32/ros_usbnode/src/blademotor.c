/****************************************************************************
* Title                 :   
* Filename              :   blademotor.c
* Author                :   Nekraus
* Origin Date           :   17/08/2022
* Version               :   1.0.0

*****************************************************************************/
/** \file blademotor.c
*  \brief 
*
*/
/******************************************************************************
* Includes
*******************************************************************************/
#include <string.h>
#include <stdbool.h>

#include "stm32f_board_hal.h"

#include "main.h"
#include "board.h"

#include "blademotor.h" 

/******************************************************************************
* Module Preprocessor Constants
*******************************************************************************/
#define BLADEMOTOR_LENGTH_INIT_MSG 22
#define BLADEMOTOR_LENGTH_RQST_MSG 7
/* Reversal requires an accepted OFF transmission and qualifying ESC reports.
 * The tested 500 holds its speed word after OFF, then clears it; it reports no
 * progressive coast-down curve. These guards are not a mechanical stop model. */
#define BLADEMOTOR_REVERSE_OFF_MS 1000u
#define BLADEMOTOR_ZERO_CONFIRM_MS 300u
#define BLADEMOTOR_FEEDBACK_MAX_AGE_MS 300u
#define BLADEMOTOR_REVERSE_REPORT_MS 5000u
#ifndef BLADEMOTOR_COASTDOWN_VALIDATION
#define BLADEMOTOR_COASTDOWN_VALIDATION 0
#endif
#if BLADEMOTOR_COASTDOWN_VALIDATION && !BOARD_YARDFORCE500_VARIANT_ORIG
#error "Coast-down validation uses the Yardforce500 UART debug output"
#endif
#define BLADEMOTOR_FEEDBACK_TIMEOUT_MS 350u
/******************************************************************************
* Module Preprocessor Macros
*******************************************************************************/

/******************************************************************************
* Module Typedefs
*******************************************************************************/
typedef enum {
    BLADEMOTOR_INIT_1,
    BLADEMOTOR_INIT_2,
    BLADEMOTOR_RUN
}BLADEMOTOR_STATE_e;

/******************************************************************************
* Module Variable Definitions
*******************************************************************************/
UART_HandleTypeDef BLADEMOTOR_USART_Handler; // UART  Handle

DMA_HandleTypeDef hdma_uart_blade_rx;
DMA_HandleTypeDef hdma_uart_blade_tx;

static BLADEMOTOR_STATE_e blademotor_eState = BLADEMOTOR_INIT_1;

bool BLADEMOTOR_bActivated = false;
uint16_t BLADEMOTOR_u16RPM = 0;
uint16_t BLADEMOTOR_u16Power = 0;
uint32_t BLADEMOTOR_u32Error = 0;

/* DMA storage is ISR-private.  Foreground consumes a validated copy once. */
static uint8_t blademotor_dma_received[BLADEMOTOR_LENGTH_RECEIVED_MSG] = {0};
typedef struct {
    uint8_t frame[BLADEMOTOR_LENGTH_RECEIVED_MSG];
    uint32_t tick;
    uint32_t sequence;
    uint8_t valid;
} BLADEMOTOR_snapshot_t;
static volatile BLADEMOTOR_snapshot_t blademotor_snapshot = {0};
static uint32_t blademotor_consumed_sequence = 0;
static volatile uint32_t blademotor_last_valid_tick = 0;
static volatile uint8_t blademotor_seen_valid = 0;
static volatile uint8_t blademotor_fault = 0;
static volatile uint8_t blademotor_recover_rx = 0;
static volatile uint8_t blademotor_rx_armed = 0;
static volatile uint32_t blademotor_rx_started_tick = 0;
static uint8_t blademotor_tx[2][BLADEMOTOR_LENGTH_RQST_MSG];
static uint8_t blademotor_tx_slot = 0;
static volatile uint8_t blademotor_tx_busy = 0;
static volatile uint32_t blademotor_tx_started_tick = 0;
static uint8_t blademotor_u8OnOff = 0;
static uint8_t blademotor_u8Direction = 0;
static uint8_t blademotor_u8RunDirection = 0;
static bool blademotor_reverse_pending = false;
static bool blademotor_off_sent = false;
static bool blademotor_zero_seen = false;
static uint32_t blademotor_stop_since, blademotor_zero_since;
static uint32_t blademotor_last_feedback_seq;
static uint32_t blademotor_zero_epoch;
static uint32_t blademotor_pending_since, blademotor_pending_report_tick;
#if BLADEMOTOR_COASTDOWN_VALIDATION
static uint32_t blademotor_trace_seq, blademotor_trace_tx_tick;
static uint8_t blademotor_trace_command;
#endif

typedef struct {
    uint32_t seq, tick, zero_epoch;
    uint16_t reported_speed;
    uint8_t valid, activated, error;
} blademotor_feedback_t;
typedef struct {
    uint32_t seq, zero_epoch;
    uint8_t required;
} blademotor_reverse_release_t;
/* Updated only after RX completion; snapshot with IRQs masked in the foreground.
 * seq/tick identify UART replies, not new physical speed measurements inside the
 * ESC. Never infer deceleration from a held word or use public cached RPM alone.
 * Invalid replies break qualification. Hardware evidence is in BLADE-REVERSE.md. */
static volatile blademotor_feedback_t blademotor_feedback;

const uint8_t blademotor_pcu8Preamble[5]  = {0x55,0xAA,0x0A,0x2,0xD0};
const uint8_t blademotor_pcu8InitMsg[BLADEMOTOR_LENGTH_INIT_MSG] =  { 0x55, 0xaa, 0x12, 0x20, 0x80, 0x00, 0xac, 0x0d, 0x00, 0x02, 0x32, 0x50, 0x1e, 0x04, 0x00, 0x15, 0x21, 0x05, 0x0a, 0x19, 0x3c, 0xaa };
/******************************************************************************
* Function Prototypes
*******************************************************************************/

/******************************************************************************
*  Public Functions
*******************************************************************************/

static bool blademotor_feedback_qualified_for_reverse(
    uint32_t now, blademotor_reverse_release_t *release)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    blademotor_feedback_t feedback = blademotor_feedback;
    __set_PRIMASK(primask);

    /* Do not miss a bad/nonzero reply overwritten by a later good reply before
     * this foreground cycle. RX also breaks the epoch across a feedback gap. */
    if (feedback.zero_epoch != blademotor_zero_epoch)
    {
        blademotor_zero_seen = false;
        blademotor_zero_epoch = feedback.zero_epoch;
    }
    if (!blademotor_off_sent || !feedback.valid || feedback.activated ||
        feedback.error || feedback.reported_speed != 0 ||
        (uint32_t)(now - feedback.tick) > BLADEMOTOR_FEEDBACK_MAX_AGE_MS)
    {
        blademotor_zero_seen = false;
        blademotor_last_feedback_seq = feedback.seq;
        return false;
    }
    /* Release only while consuming a new reply. The minimum OFF dwell must
     * not expire into an enable using the same cached zero as the last pass. */
    if (feedback.seq == blademotor_last_feedback_seq)
        return false;
    blademotor_last_feedback_seq = feedback.seq;
    if (!blademotor_zero_seen)
    {
        blademotor_zero_since = feedback.tick;
        blademotor_zero_seen = true;
    }
    /* A latched nonzero word never contributes to this zero-report interval.
     * A direct nonzero-to-zero transition needs the full confirmation window. */
    /* The bench image must never reverse, even when the ESC reports zero.
     * Its purpose is to compare that report with independent rotor observation. */
    const bool qualified = !BLADEMOTOR_COASTDOWN_VALIDATION && blademotor_zero_seen &&
        (uint32_t)(feedback.tick - blademotor_zero_since) >= BLADEMOTOR_ZERO_CONFIRM_MS &&
        (uint32_t)(now - blademotor_stop_since) >= BLADEMOTOR_REVERSE_OFF_MS;
    if (qualified)
    {
        release->seq = feedback.seq;
        release->zero_epoch = feedback.zero_epoch;
        release->required = 1u;
    }
    return qualified;
}

static void blademotor_prepareMsg(uint8_t *msg,
                                  blademotor_reverse_release_t *release)
{
    *release = (blademotor_reverse_release_t){0};
    uint8_t command = 0;
    if (!blademotor_u8OnOff)
    {
        /* OFF from the existing emergency/idle/heartbeat gates cancels a
         * pending start. Never retain a queued enable across that request. */
        blademotor_reverse_pending = false;
        blademotor_off_sent = blademotor_zero_seen = false;
    }
    else
    {
        if (!blademotor_reverse_pending &&
            blademotor_u8Direction != blademotor_u8RunDirection)
        {
            blademotor_reverse_pending = true;
            blademotor_off_sent = blademotor_zero_seen = false;
            blademotor_pending_since = blademotor_pending_report_tick = HAL_GetTick();
        }
        if (!blademotor_reverse_pending ||
            blademotor_feedback_qualified_for_reverse(HAL_GetTick(), release))
            command = blademotor_u8Direction ? 0xC0 : 0x80;
    }
    /* The additive checksum differs for forward, reverse and OFF. */
    msg[5] = command;
    msg[6] = crcCalc(msg, BLADEMOTOR_LENGTH_RQST_MSG - 1);
}

static void blademotor_recover_rx_if_needed(void)
{
    const uint32_t now = HAL_GetTick();
    if (blademotor_tx_busy &&
        (uint32_t)(now - blademotor_tx_started_tick) > BLADEMOTOR_FEEDBACK_TIMEOUT_MS)
    {
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        if (HAL_UART_AbortTransmit(&BLADEMOTOR_USART_Handler) == HAL_OK)
        {
            blademotor_tx_busy = 0u;
        }
    }
    if (blademotor_rx_armed &&
        (uint32_t)(now - blademotor_rx_started_tick) > BLADEMOTOR_FEEDBACK_TIMEOUT_MS)
    {
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
    }
    if (blademotor_recover_rx &&
        HAL_UART_AbortReceive(&BLADEMOTOR_USART_Handler) == HAL_OK)
    {
        blademotor_rx_armed = 0u;
        blademotor_recover_rx = 0u;
    }
}

static bool blademotor_start_exchange(
    uint8_t *tx, const blademotor_reverse_release_t *release)
{
    if (blademotor_tx_busy || blademotor_rx_armed || blademotor_recover_rx)
    {
        return false;
    }
    if (HAL_UART_Receive_DMA(&BLADEMOTOR_USART_Handler, blademotor_dma_received,
                             BLADEMOTOR_LENGTH_RECEIVED_MSG) != HAL_OK)
    {
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
        return false;
    }
    blademotor_rx_armed = 1u;
    blademotor_rx_started_tick = HAL_GetTick();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (MOTORLINK_OutputInhibited() ||
        (release->required &&
         (blademotor_feedback.seq != release->seq ||
          blademotor_feedback.zero_epoch != release->zero_epoch ||
          !blademotor_feedback.valid || blademotor_feedback.activated ||
          blademotor_feedback.error ||
          blademotor_feedback.reported_speed != 0u ||
          (uint32_t)(HAL_GetTick() - blademotor_feedback.tick) >
              BLADEMOTOR_FEEDBACK_MAX_AGE_MS ||
          !BLADEMOTOR_FeedbackHealthy())))
    {
        /* A reversal releases against one exact qualifying reply. A newer
         * reply or UART error before DMA handoff cannot reuse that decision. */
        tx[5] = 0x00;
        tx[6] = crcCalc(tx, BLADEMOTOR_LENGTH_RQST_MSG - 1);
    }
    const HAL_StatusTypeDef tx_status =
        HAL_UART_Transmit_DMA(&BLADEMOTOR_USART_Handler, tx,
                              BLADEMOTOR_LENGTH_RQST_MSG);
    if (tx_status == HAL_OK)
    {
        blademotor_tx_slot ^= 1u;
        blademotor_tx_busy = 1u;
        blademotor_tx_started_tick = HAL_GetTick();
    }
    else
    {
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
    return tx_status == HAL_OK;
}

/**
 * @brief Init the Blade Motor Serial Port (PAC5223)
 * @retval None
 */
void BLADEMOTOR_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PAC 5223 Reset Line (Blade Motor) */
    PAC5223RESET_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = PAC5223RESET_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(PAC5223RESET_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(PAC5223RESET_GPIO_PORT, PAC5223RESET_PIN, 1);     /* take Blade PAC out of reset if HIGH */

    // enable port and usart clocks
    BLADEMOTOR_USART_GPIO_CLK_ENABLE();

	// Initiale USART3 for STM32f1 and USART6 for STM32f4
#if BOARD_YARDFORCE500_VARIANT_ORIG
	__HAL_RCC_USART3_CLK_ENABLE();
#elif BOARD_YARDFORCE500_VARIANT_B
	__HAL_RCC_USART6_CLK_ENABLE();
#endif
    
#if BOARD_YARDFORCE500_VARIANT_ORIG
    // RX
    GPIO_InitStruct.Pin = BLADEMOTOR_USART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(BLADEMOTOR_USART_RX_PORT, &GPIO_InitStruct);

    // TX
    GPIO_InitStruct.Pin = BLADEMOTOR_USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(BLADEMOTOR_USART_TX_PORT, &GPIO_InitStruct);

    // Alternate Pin Set ?
    __HAL_AFIO_REMAP_USART2_ENABLE();
#elif BOARD_YARDFORCE500_VARIANT_B
    // RX TX
    GPIO_InitStruct.Pin = BLADEMOTOR_USART_TX_PIN | BLADEMOTOR_USART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(BLADEMOTOR_USART_TX_PORT, &GPIO_InitStruct);
#endif

    BLADEMOTOR_USART_Handler.Instance = BLADEMOTOR_USART_INSTANCE;
    BLADEMOTOR_USART_Handler.Init.BaudRate = 115200;               // Baud rate
    BLADEMOTOR_USART_Handler.Init.WordLength = UART_WORDLENGTH_8B; // The word is  8  Bit format
    BLADEMOTOR_USART_Handler.Init.StopBits = USART_STOPBITS_1;     // A stop bit
    BLADEMOTOR_USART_Handler.Init.Parity = UART_PARITY_NONE;       // No parity bit
    BLADEMOTOR_USART_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE; // No hardware flow control
    BLADEMOTOR_USART_Handler.Init.Mode = USART_MODE_TX_RX;         // Transceiver mode
    
    HAL_UART_Init(&BLADEMOTOR_USART_Handler); 

    DB_TRACE(" * Blade Motor UART initialized\r\n");

    /* UART4 DMA Init */
    /* UART4_RX Init */
#if BOARD_YARDFORCE500_VARIANT_ORIG
	hdma_uart_blade_rx.Instance = DMA1_Channel3;
#elif BOARD_YARDFORCE500_VARIANT_B
	hdma_uart_blade_rx.Instance = DMA2_Stream1;
	hdma_uart_blade_rx.Init.Channel = DMA_CHANNEL_5;
	hdma_uart_blade_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
#endif
	hdma_uart_blade_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
	hdma_uart_blade_rx.Init.PeriphInc = DMA_PINC_DISABLE;
	hdma_uart_blade_rx.Init.MemInc = DMA_MINC_ENABLE;
	hdma_uart_blade_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	hdma_uart_blade_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	hdma_uart_blade_rx.Init.Mode = DMA_NORMAL;
	hdma_uart_blade_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_uart_blade_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(&BLADEMOTOR_USART_Handler, hdmarx, hdma_uart_blade_rx);
    
    /* UART4 DMA Init */
    /* UART4_TX Init */
#if BOARD_YARDFORCE500_VARIANT_ORIG
	hdma_uart_blade_tx.Instance = DMA1_Channel2;
#elif BOARD_YARDFORCE500_VARIANT_B
	hdma_uart_blade_tx.Instance = DMA2_Stream6;
	hdma_uart_blade_tx.Init.Channel = DMA_CHANNEL_5;
	hdma_uart_blade_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
#endif
	hdma_uart_blade_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
	hdma_uart_blade_tx.Init.PeriphInc = DMA_PINC_DISABLE;
	hdma_uart_blade_tx.Init.MemInc = DMA_MINC_ENABLE;
	hdma_uart_blade_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	hdma_uart_blade_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	hdma_uart_blade_tx.Init.Mode = DMA_NORMAL;
	hdma_uart_blade_tx.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_uart_blade_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(&BLADEMOTOR_USART_Handler, hdmatx, hdma_uart_blade_tx);
    
    // enable IRQ
#if BOARD_YARDFORCE500_VARIANT_ORIG
	IRQn_Type usart_irq = USART3_IRQn;
#elif BOARD_YARDFORCE500_VARIANT_B
	IRQn_Type usart_irq = USART6_IRQn;
#endif

    HAL_NVIC_SetPriority(usart_irq, 0, 0);
	HAL_NVIC_EnableIRQ(usart_irq);
    __HAL_UART_ENABLE_IT(&BLADEMOTOR_USART_Handler, UART_IT_TC);

    blademotor_eState = BLADEMOTOR_INIT_1;    
}

/// @brief handle drive motor messages
/// @param  
void  BLADEMOTOR_App(void){
    BLADEMOTOR_snapshot_t snapshot;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (blademotor_snapshot.sequence != blademotor_consumed_sequence &&
        blademotor_snapshot.valid) {
        snapshot = blademotor_snapshot;
        blademotor_consumed_sequence = snapshot.sequence;
        __set_PRIMASK(primask);
        BLADEMOTOR_bActivated = (snapshot.frame[5] & 0x80u) != 0u;
        BLADEMOTOR_u16RPM = snapshot.frame[7] + (snapshot.frame[8] << 8);
        BLADEMOTOR_u16Power = snapshot.frame[9] + (snapshot.frame[10] << 8);
    } else {
        __set_PRIMASK(primask);
    }
    blademotor_recover_rx_if_needed();
    switch (blademotor_eState)
    {
    case BLADEMOTOR_INIT_1:

        if (HAL_UART_Transmit_DMA(&BLADEMOTOR_USART_Handler, (uint8_t*)blademotor_pcu8InitMsg, BLADEMOTOR_LENGTH_INIT_MSG) == HAL_OK) {
            blademotor_tx_busy = 1u;
            blademotor_tx_started_tick = HAL_GetTick();
            blademotor_eState = BLADEMOTOR_RUN;
            debug_printf(" * Blade Motor Controller initialization command queued\r\n");
        }
        break;
    
    case BLADEMOTOR_RUN: {

        if (blademotor_reverse_pending &&
            (uint32_t)(HAL_GetTick() - blademotor_pending_report_tick) >= BLADEMOTOR_REVERSE_REPORT_MS)
        {
            blademotor_pending_report_tick = HAL_GetTick();
            BLADEMOTOR_u32Error++;
            debug_printf("Blade reversal waiting: OFF retained (%lu ms)\r\n",
                (unsigned long)(HAL_GetTick() - blademotor_pending_since));
        }
#if BLADEMOTOR_COASTDOWN_VALIDATION
        /* Foreground only; never print in the RX interrupt. Sequence gaps
         * reveal overwritten samples or dropped best-effort UART debug lines. */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        blademotor_feedback_t trace = blademotor_feedback;
        __set_PRIMASK(primask);
        if (trace.seq != blademotor_trace_seq)
        {
            blademotor_trace_seq = trace.seq;
            debug_printf("blade coast t=%lu tx=%02x tx_t=%lu seq=%lu rx_t=%lu valid=%u active=%u speed_word=%u err=%u\r\n",
                (unsigned long)HAL_GetTick(), (unsigned)blademotor_trace_command,
                (unsigned long)blademotor_trace_tx_tick, (unsigned long)trace.seq,
                (unsigned long)trace.tick, (unsigned)trace.valid, (unsigned)trace.activated,
                (unsigned)trace.reported_speed, (unsigned)trace.error);
        }
#endif
        /* Build only in the slot not owned by TX DMA. Keep polling while OFF. */
        uint8_t *tx = blademotor_tx[blademotor_tx_slot ^ 1u];
        blademotor_reverse_release_t release = {0};
        tx[0] = 0x55; tx[1] = 0xaa; tx[2] = 0x03; tx[3] = 0x20; tx[4] = 0x80;
        if (BLADEMOTOR_FeedbackHealthy() && !MOTORLINK_OutputInhibited())
            blademotor_prepareMsg(tx, &release);
        else
        {
            tx[5] = 0x00;
            tx[6] = crcCalc(tx, BLADEMOTOR_LENGTH_RQST_MSG - 1);
        }

        if (blademotor_start_exchange(tx, &release))
        {
#if BLADEMOTOR_COASTDOWN_VALIDATION
            if (blademotor_trace_command != tx[5])
            {
                blademotor_trace_command = tx[5];
                blademotor_trace_tx_tick = HAL_GetTick();
            }
#endif
            if (tx[5] & 0x80u)
            {
                blademotor_u8RunDirection = blademotor_u8Direction;
                blademotor_reverse_pending = false;
            }
            else if (blademotor_reverse_pending && !blademotor_off_sent)
            {
                /* A failed/busy transmission must not start the stop timer.
                 * Only subsequent feedback can qualify this direction change. */
                blademotor_stop_since = HAL_GetTick();
                const uint32_t primask = __get_PRIMASK();
                __disable_irq();
                blademotor_last_feedback_seq = blademotor_feedback.seq;
                __set_PRIMASK(primask);
                blademotor_off_sent = true;
            }
        }
        break;
    }
    
    default:
        break;
    }
}

/// @brief control blade motor (there is no speed control for this motor)
/// @param on_off 1 to turn on, 0 to turn off
void BLADEMOTOR_Set(uint8_t on_off, uint8_t direction)
{
    /* Latch the gated request; never modify the DMA-owned message here. */
    blademotor_u8OnOff = on_off != 0;
    blademotor_u8Direction = direction != 0;
    if (!blademotor_u8OnOff)
    {
        blademotor_reverse_pending = false;
        blademotor_off_sent = blademotor_zero_seen = false;
    }
}

/// @brief drive motor receive interrupt handler
/// @param  
void BLADEMOTOR_ReceiveIT(void)
{
    blademotor_rx_armed = 0u;
    const uint32_t now = HAL_GetTick();
    const bool framed = memcmp(blademotor_pcu8Preamble,
                               blademotor_dma_received, 2) == 0;
    const bool checksummed = framed &&
        blademotor_dma_received[BLADEMOTOR_LENGTH_RECEIVED_MSG - 1] ==
        crcCalc(blademotor_dma_received, BLADEMOTOR_LENGTH_RECEIVED_MSG - 1);
    const uint8_t error = checksummed ? blademotor_dma_received[6] : 0xffu;
    const uint8_t activated = checksummed &&
        (blademotor_dma_received[5] & 0x80u) != 0u;
    const uint16_t reported_speed = checksummed ?
        (uint16_t)(blademotor_dma_received[7] |
                   (blademotor_dma_received[8] << 8)) : 0u;

    /* Every completed reply has an identity. An invalid, active, nonzero or
     * gapped reply breaks the contiguous zero-report epoch for reversal. */
    if (!checksummed || activated || error != 0u || reported_speed != 0u ||
        (uint32_t)(now - blademotor_feedback.tick) > BLADEMOTOR_FEEDBACK_MAX_AGE_MS)
        blademotor_feedback.zero_epoch++;
    blademotor_feedback.tick = now;
    blademotor_feedback.seq++;
    blademotor_feedback.valid = checksummed;
    blademotor_feedback.activated = activated;
    blademotor_feedback.error = error;
    blademotor_feedback.reported_speed = reported_speed;

    if (checksummed && error == 0u) {
            BLADEMOTOR_snapshot_t next = {0};
            memcpy(next.frame, blademotor_dma_received, BLADEMOTOR_LENGTH_RECEIVED_MSG);
            next.tick = now; next.valid = 1u;
            next.sequence = blademotor_snapshot.sequence + 1u;
            blademotor_snapshot = next;
            blademotor_last_valid_tick = next.tick;
            blademotor_seen_valid = 1u;
            blademotor_fault = 0u;
    } else {
        /* Controller-error or invalid framing never refreshes link health. */
        BLADEMOTOR_u32Error++;
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
    }
}

void BLADEMOTOR_OnUartError(void) {
    blademotor_fault = 1u;
    MOTORLINK_ForceInhibit();
    blademotor_feedback.valid = 0u;
    blademotor_feedback.zero_epoch++;
    blademotor_rx_armed = 0u;
    blademotor_recover_rx = 1u;
    BLADEMOTOR_u32Error++;
}
void BLADEMOTOR_OnTxComplete(void) { blademotor_tx_busy = 0u; }
bool BLADEMOTOR_FeedbackHealthy(void) {
    return blademotor_seen_valid != 0u && blademotor_fault == 0u &&
        (uint32_t)(HAL_GetTick() - blademotor_last_valid_tick) <= BLADEMOTOR_FEEDBACK_TIMEOUT_MS;
}

/******************************************************************************
*  Private Functions
*******************************************************************************/
