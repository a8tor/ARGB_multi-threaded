
#ifndef ARGB_H_
#define ARGB_H_

#include "libs.h"


#define USE_GAMMA_CORRECTION 1 ///< Gamma-correction should fix red&green, try for yourself


/**
 * @addtogroup Global_entities
 * @brief All driver's methods
 * @{
 * @enum ARGB_STATE
 * @brief Driver's status enum
 */
typedef enum ARGB_STATE {
    ARGB_BUSY = 0,  ///< DMA Transfer in progress
    ARGB_READY = 1, ///< DMA Ready to transfer
    ARGB_OK = 2,    ///< Function execution success
    ARGB_PARAM_ERR = 3, ///< Error in input parameters
} ARGB_STATE;


typedef struct{
    TIM_HandleTypeDef *tim_handle;
    DMA_HandleTypeDef *dma_handle;
    uint16_t buf_counter;
		u8_t *rgb_buf;
		u32_t *pwm_buf;
    uint8_t brightness;
    ARGB_STATE state;
    uint16_t num_pixels;
	  u8_t PWM_HI;
	  u8_t PWM_LO;
		u8_t apb;// #if TIM_NUM == 1 || (TIM_NUM >= 8 && TIM_NUM <= 11) apb = 1, else 2
		uint16_t tim_ch;
	
} ARGB_Strip;


void ARGB_Init(ARGB_Strip *strip);   // Initialization
void ARGB_Clear(ARGB_Strip *strip);  // Clear strip

void ARGB_SetBrightness(ARGB_Strip *strip, u8_t br); // Set global brightness

void ARGB_SetRGB(ARGB_Strip *strip, u16_t i, u8_t r, u8_t g, u8_t b);  // Set single LED by RGB


void ARGB_FillRGB(ARGB_Strip *strip, u8_t r, u8_t g, u8_t b); // Fill all strip with RGB color


ARGB_STATE ARGB_Ready(ARGB_Strip *strip); // Get DMA Ready state
ARGB_STATE ARGB_Show(ARGB_Strip *strip); // Push data to the strip


/// @} @}
#endif /* ARGB_H_ */
