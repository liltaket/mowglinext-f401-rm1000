/****************************************************************************
* Title                 :   Blade motor driver
* Filename              :   blademotor.h
* Author                :   Nekraus
* Origin Date           :   17/08/2022
* Version               :   1.0.0

*****************************************************************************/
/** \file blademotor.h
*  \brief 
*
*/
#ifndef __BLADEMOTOR_H
#define __BLADEMOTOR_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
* Includes
*******************************************************************************/
#include <stdbool.h>

/******************************************************************************
* Preprocessor Constants
*******************************************************************************/

/******************************************************************************
* Constants
*******************************************************************************/

/******************************************************************************
* Macros
*******************************************************************************/

/******************************************************************************
* Typedefs
*******************************************************************************/

/******************************************************************************
* Variables
*******************************************************************************/
// global variables used by ROS
extern bool BLADEMOTOR_bActivated;
extern uint16_t BLADEMOTOR_u16RPM;
extern uint16_t BLADEMOTOR_u16Power;
extern uint32_t BLADEMOTOR_u32Error;
/******************************************************************************
* PUBLIC Function Prototypes
*******************************************************************************/

void BLADEMOTOR_Init(void);
void BLADEMOTOR_App(void);
void BLADEMOTOR_ReceiveIT(void);
void BLADEMOTOR_OnUartError(void);
void BLADEMOTOR_OnTxComplete(void);
/* Blade status is polled at 100 ms; 350 ms permits three missed polls plus
 * cooperative-loop jitter, but never treats a stale status as healthy. */
bool BLADEMOTOR_FeedbackHealthy(void);

void BLADEMOTOR_Set(uint8_t on_off, uint8_t direction);


#ifdef __cplusplus
}
#endif

#endif /*BLADEMOTOR_H*/ 

/*** End of File **************************************************************/
