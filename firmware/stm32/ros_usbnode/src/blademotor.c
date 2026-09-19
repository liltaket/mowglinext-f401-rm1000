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

const uint8_t blademotor_pcu8Preamble[5]  = {0x55,0xAA,0x0A,0x2,0xD0};
const uint8_t blademotor_pcu8InitMsg[BLADEMOTOR_LENGTH_INIT_MSG] =  { 0x55, 0xaa, 0x12, 0x20, 0x80, 0x00, 0xac, 0x0d, 0x00, 0x02, 0x32, 0x50, 0x1e, 0x04, 0x00, 0x15, 0x21, 0x05, 0x0a, 0x19, 0x3c, 0xaa };
/******************************************************************************
* Function Prototypes
*******************************************************************************/

/******************************************************************************
*  Public Functions
*******************************************************************************/

void blademotor_prepareMsg(uint8_t *msg)
{    
    if (blademotor_u8OnOff)
    {
        msg[5] = BLADEMOTOR_ON_COMMAND_VALUE; /* board-specific run value */
    }
    else
    {
        msg[5] = 0x00; /* change speed Motor */
    }
    msg[6] = crcCalc(msg, BLADEMOTOR_LENGTH_RQST_MSG - 1u);
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

static void blademotor_start_exchange(uint8_t *tx)
{
    if (blademotor_tx_busy || blademotor_rx_armed || blademotor_recover_rx)
    {
        return;
    }
    if (HAL_UART_Receive_DMA(&BLADEMOTOR_USART_Handler, blademotor_dma_received,
                             BLADEMOTOR_LENGTH_RECEIVED_MSG) != HAL_OK)
    {
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
        return;
    }
    blademotor_rx_armed = 1u;
    blademotor_rx_started_tick = HAL_GetTick();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (MOTORLINK_OutputInhibited())
    {
        /* Close the race between preparing ON and handing the frame to DMA. */
        tx[5] = 0x00;
        tx[6] = 0xa2;
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
    __disable_irq();
    if (blademotor_snapshot.sequence != blademotor_consumed_sequence &&
        blademotor_snapshot.valid) {
        snapshot = blademotor_snapshot;
        blademotor_consumed_sequence = snapshot.sequence;
        __enable_irq();
        BLADEMOTOR_bActivated = (snapshot.frame[5] & 0x80u) != 0u;
        BLADEMOTOR_u16RPM = snapshot.frame[7] + (snapshot.frame[8] << 8);
        BLADEMOTOR_u16Power = snapshot.frame[9] + (snapshot.frame[10] << 8);
    } else {
        __enable_irq();
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

        uint8_t *tx = blademotor_tx[blademotor_tx_slot ^ 1u];
        tx[0] = 0x55; tx[1] = 0xaa; tx[2] = 0x03; tx[3] = 0x20; tx[4] = 0x80;
        if (BLADEMOTOR_FeedbackHealthy() && !MOTORLINK_OutputInhibited())
        {
            blademotor_prepareMsg(tx);
        }
        else
        {
            /* Preserve polling and OFF delivery while feedback is untrusted. */
            tx[5] = 0x00;
            tx[6] = 0xa2;
        }
        blademotor_start_exchange(tx);
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
    (void)direction;
    blademotor_u8OnOff = on_off;
}

/// @brief drive motor receive interrupt handler
/// @param  
void BLADEMOTOR_ReceiveIT(void)
{
    /* decode the frame */
    blademotor_rx_armed = 0u;
    if(memcmp(blademotor_pcu8Preamble, blademotor_dma_received, 2) == 0){
        uint8_t l_u8crc = crcCalc(blademotor_dma_received, BLADEMOTOR_LENGTH_RECEIVED_MSG-1);

        if(blademotor_dma_received[BLADEMOTOR_LENGTH_RECEIVED_MSG-1] == l_u8crc &&
           blademotor_dma_received[6] == 0u){
            BLADEMOTOR_snapshot_t next = {0};
            memcpy(next.frame, blademotor_dma_received, BLADEMOTOR_LENGTH_RECEIVED_MSG);
            next.tick = HAL_GetTick(); next.valid = 1u;
            next.sequence = blademotor_snapshot.sequence + 1u;
            blademotor_snapshot = next;
            blademotor_last_valid_tick = next.tick;
            blademotor_seen_valid = 1u;
            blademotor_fault = 0u;
        } else {
            /* A controller-error frame has valid framing but is not valid
             * feedback: it must not refresh the software freshness age. */
            BLADEMOTOR_u32Error++;
            blademotor_fault = 1u;
            MOTORLINK_ForceInhibit();
            blademotor_recover_rx = 1u;
        }
    } else {
        BLADEMOTOR_u32Error++;
        blademotor_fault = 1u;
        MOTORLINK_ForceInhibit();
        blademotor_recover_rx = 1u;
    }
}

void BLADEMOTOR_OnUartError(void) {
    blademotor_fault = 1u;
    MOTORLINK_ForceInhibit();
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
