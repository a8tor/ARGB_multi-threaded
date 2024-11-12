
#include "ARGB.h"  // include header file
#include "math.h"
#include "stm32f401xc.h"


#define PWM_BUF_LEN (3 * 8 * 2)    ///< Pack len * 8 bit * 2 LEDs

static inline u8_t scale8(u8_t x, u8_t scale); // Gamma correction
// Callbacks
static void ARGB_TIM_DMADelayPulseCplt(DMA_HandleTypeDef *hdma);
static void ARGB_TIM_DMADelayPulseHalfCplt(DMA_HandleTypeDef *hdma);
/// @} //Private


/**
 * @brief Init timer & prescalers
 * @param[in] strip
 */
void ARGB_Init(ARGB_Strip *strip) {
    /* Auto-calculation! */
    u32_t APBfq; // Clock freq
	if(strip->apb == 1){
		APBfq = HAL_RCC_GetPCLK1Freq();
    APBfq *= (RCC->CFGR & RCC_CFGR_PPRE1) == 0 ? 1 : 2;
	} else if(strip->apb == 2){
			APBfq = HAL_RCC_GetPCLK2Freq();
			APBfq *= (RCC->CFGR & RCC_CFGR_PPRE2) == 0 ? 1 : 2;
	}


    APBfq /= (uint32_t) (800 * 1000);  // 800 KHz - 1.25us

    strip->tim_handle->Instance->PSC = 0;                        // dummy hardcode now
    strip->tim_handle->Instance->ARR = (uint16_t) (APBfq - 1);   // set timer prescaler
    strip->tim_handle->Instance->EGR = 1;                        // update timer registers

    strip->PWM_HI = (u8_t) (APBfq * 0.56) - 1;     // Log.1 - 56% - 0.70us
    strip->PWM_LO = (u8_t) (APBfq * 0.28) - 1;     // Log.0 - 28% - 0.35us

		strip->state = ARGB_READY; // Set Ready Flag
    TIM_CCxChannelCmd(strip->tim_handle->Instance, strip->tim_ch, TIM_CCx_ENABLE); // Enable GPIO to IDLE state
    HAL_Delay(1); // Make some delay
}

/**
 * @brief Fill ALL LEDs with (0,0,0)
 * @param[in] strip
 * @note Update strip after that
 */
void ARGB_Clear(ARGB_Strip *strip) {
    ARGB_FillRGB(strip, 0, 0, 0);
}

/**
 * @brief Set GLOBAL LED brightness
 * @param[in] strip
 * @param[in] br Brightness [0..255]
 */
void ARGB_SetBrightness(ARGB_Strip *strip, u8_t br) {
    strip->brightness = br;
}

/**
 * @brief Set LED with RGB color by index
 * @param[in] i LED position
 * @param[in] r Red component   [0..255]
 * @param[in] g Green component [0..255]
 * @param[in] b Blue component  [0..255]
 */
void ARGB_SetRGB(ARGB_Strip *strip, u16_t i, u8_t r, u8_t g, u8_t b) {
    // overflow protection
    if (i >= strip->num_pixels) {
        u16_t _i = i / strip->num_pixels;
        i -= _i * strip->num_pixels;
    }
    // set brightness
    r /= 256 / ((u16_t) strip->brightness + 1);
    g /= 256 / ((u16_t) strip->brightness + 1);
    b /= 256 / ((u16_t) strip->brightness + 1);
#if USE_GAMMA_CORRECTION
    g = scale8(g, 0xB0);
    b = scale8(b, 0xF0);
#endif
    // Subpixel chain order

    const u8_t subp1 = g;
    const u8_t subp2 = r;
    const u8_t subp3 = b;
    // RGB or RGBW
    strip->rgb_buf[3 * i] = subp1;     // subpixel 1
    strip->rgb_buf[3 * i + 1] = subp2; // subpixel 2
    strip->rgb_buf[3 * i + 2] = subp3; // subpixel 3
}




/**
 * @brief Fill ALL LEDs with RGB color
 * @param[in] strip
 * @param[in] r Red component   [0..255]
 * @param[in] g Green component [0..255]
 * @param[in] b Blue component  [0..255]
 */
void ARGB_FillRGB(ARGB_Strip *strip, u8_t r, u8_t g, u8_t b) {
    for (volatile u16_t i = 0; i < strip->num_pixels; i++)
        ARGB_SetRGB(strip, i, r, g, b);
}



/**
 * @brief Get current DMA status
 * @param[in] strip
 * @return #ARGB_STATE enum
 */
ARGB_STATE ARGB_Ready(ARGB_Strip *strip) {
    return strip->state;
}

/**
 * @brief Update strip
 * @param[in] strip
 * @return #ARGB_STATE enum
 */
ARGB_STATE ARGB_Show(ARGB_Strip *strip) {
    strip->state = ARGB_BUSY;
    if (strip->buf_counter != 0 || strip->dma_handle->State != HAL_DMA_STATE_READY) {
        return ARGB_BUSY;
    } else {
        for (volatile u8_t i = 0; i < 8; i++) {
            // set first transfer from first values
            strip->pwm_buf[i] = (((strip->rgb_buf[0] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 8] = (((strip->rgb_buf[1] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 16] = (((strip->rgb_buf[2] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 24] = (((strip->rgb_buf[3] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 32] = (((strip->rgb_buf[4] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 40] = (((strip->rgb_buf[5] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
        }
        HAL_StatusTypeDef DMA_Send_Stat = HAL_ERROR;
        while (DMA_Send_Stat != HAL_OK) {
            if (TIM_CHANNEL_STATE_GET(strip->tim_handle, strip->tim_ch) == HAL_TIM_CHANNEL_STATE_BUSY) {
                DMA_Send_Stat = HAL_BUSY;
                continue;
            } else if (TIM_CHANNEL_STATE_GET(strip->tim_handle, strip->tim_ch) == HAL_TIM_CHANNEL_STATE_READY) {
                TIM_CHANNEL_STATE_SET(strip->tim_handle, strip->tim_ch, HAL_TIM_CHANNEL_STATE_BUSY);
            } else {
                DMA_Send_Stat = HAL_ERROR;
                continue;
            }
						
						uint16_t ARGB_TIM_DMA_ID;
						uint16_t ARGB_TIM_DMA_CC;
						uint32_t ARGB_TIM_CCR;
						switch(strip->tim_ch){
							case TIM_CHANNEL_1:
								ARGB_TIM_DMA_ID = TIM_DMA_ID_CC1;
								ARGB_TIM_DMA_CC = TIM_DMA_CC1;
								break;
							case TIM_CHANNEL_2:
								ARGB_TIM_DMA_ID = TIM_DMA_ID_CC2;
								ARGB_TIM_DMA_CC = TIM_DMA_CC2;
								break;
							case TIM_CHANNEL_3:
								ARGB_TIM_DMA_ID = TIM_DMA_ID_CC3;
								ARGB_TIM_DMA_CC = TIM_DMA_CC3;
								break;
							case TIM_CHANNEL_4:
								ARGB_TIM_DMA_ID = TIM_DMA_ID_CC4;
								ARGB_TIM_DMA_CC = TIM_DMA_CC4;
								break;
							default:
								break;
						}

            strip->tim_handle->hdma[ARGB_TIM_DMA_ID]->XferCpltCallback = ARGB_TIM_DMADelayPulseCplt;
            strip->tim_handle->hdma[ARGB_TIM_DMA_ID]->XferHalfCpltCallback = ARGB_TIM_DMADelayPulseHalfCplt;
            strip->tim_handle->hdma[ARGB_TIM_DMA_ID]->XferErrorCallback = TIM_DMAError;
						
						switch(strip->tim_ch){
							case TIM_CHANNEL_1:
if (HAL_DMA_Start_IT(strip->tim_handle->hdma[ARGB_TIM_DMA_ID], (u32_t) strip->pwm_buf,
                                 (u32_t) &strip->tim_handle->Instance->CCR1,
                                 (u16_t) PWM_BUF_LEN) != HAL_OK) {
                DMA_Send_Stat = HAL_ERROR;
                continue;
            }
								break;
							case TIM_CHANNEL_2:
if (HAL_DMA_Start_IT(strip->tim_handle->hdma[ARGB_TIM_DMA_ID], (u32_t) strip->pwm_buf,
                                 (u32_t) &strip->tim_handle->Instance->CCR2,
                                 (u16_t) PWM_BUF_LEN) != HAL_OK) {
                DMA_Send_Stat = HAL_ERROR;
                continue;
            }
								break;
							case TIM_CHANNEL_3:
if (HAL_DMA_Start_IT(strip->tim_handle->hdma[ARGB_TIM_DMA_ID], (u32_t) strip->pwm_buf,
                                 (u32_t) &strip->tim_handle->Instance->CCR3,
                                 (u16_t) PWM_BUF_LEN) != HAL_OK) {
                DMA_Send_Stat = HAL_ERROR;
                continue;
            }
								break;
							case TIM_CHANNEL_4:
if (HAL_DMA_Start_IT(strip->tim_handle->hdma[ARGB_TIM_DMA_ID], (u32_t) strip->pwm_buf,
                                 (u32_t) &strip->tim_handle->Instance->CCR4,
                                 (u16_t) PWM_BUF_LEN) != HAL_OK) {
                DMA_Send_Stat = HAL_ERROR;
                continue;
            }
								break;
							default:
								break;
						}

            __HAL_TIM_ENABLE_DMA(strip->tim_handle, ARGB_TIM_DMA_CC);
						
						
            if (IS_TIM_BREAK_INSTANCE(strip->tim_handle->Instance) != RESET)
                __HAL_TIM_MOE_ENABLE(strip->tim_handle);
            if (IS_TIM_SLAVE_INSTANCE(strip->tim_handle->Instance)) {
                u32_t tmpsmcr = strip->tim_handle->Instance->SMCR & TIM_SMCR_SMS;
                if (!IS_TIM_SLAVEMODE_TRIGGER_ENABLED(tmpsmcr))
                    __HAL_TIM_ENABLE(strip->tim_handle);
            } else
                __HAL_TIM_ENABLE(strip->tim_handle);
            DMA_Send_Stat = HAL_OK;
        }
        strip->buf_counter = 2;
        return ARGB_OK;
    }
}

/**
 * @brief Private method for gamma correction
 * @param[in] x Param to scale
 * @param[in] scale Scale coefficient
 * @return Scaled value
 */
static inline u8_t scale8(u8_t x, u8_t scale) {
    return ((uint16_t) x * scale) >> 8;
}


/**
  * @brief  TIM DMA Delay Pulse complete callback.
  * @param  hdma pointer to DMA handle.
  * @retval None
  */
static void ARGB_TIM_DMADelayPulseCplt(DMA_HandleTypeDef *hdma) {
		ARGB_Strip *strip = (ARGB_Strip *)hdma->Parent_strip;
	
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *) ((DMA_HandleTypeDef *) hdma)->Parent;
    // if wrong handlers
    if (hdma != strip->dma_handle || htim != strip->tim_handle) return;
    if (strip->buf_counter == 0) return; // if no data to transmit - return
    if (hdma == htim->hdma[TIM_DMA_ID_CC1]) {
        htim->Channel = HAL_TIM_ACTIVE_CHANNEL_1;
        if (hdma->Init.Mode == DMA_NORMAL) {
            TIM_CHANNEL_STATE_SET(htim, TIM_CHANNEL_1, HAL_TIM_CHANNEL_STATE_READY);
        }
    } else if (hdma == htim->hdma[TIM_DMA_ID_CC2]) {
        htim->Channel = HAL_TIM_ACTIVE_CHANNEL_2;
        if (hdma->Init.Mode == DMA_NORMAL) {
            TIM_CHANNEL_STATE_SET(htim, TIM_CHANNEL_2, HAL_TIM_CHANNEL_STATE_READY);
        }
    } else if (hdma == htim->hdma[TIM_DMA_ID_CC3]) {
        htim->Channel = HAL_TIM_ACTIVE_CHANNEL_3;
        if (hdma->Init.Mode == DMA_NORMAL) {
            TIM_CHANNEL_STATE_SET(htim, TIM_CHANNEL_3, HAL_TIM_CHANNEL_STATE_READY);
        }
    } else if (hdma == htim->hdma[TIM_DMA_ID_CC4]) {
        htim->Channel = HAL_TIM_ACTIVE_CHANNEL_4;
        if (hdma->Init.Mode == DMA_NORMAL) {
            TIM_CHANNEL_STATE_SET(htim, TIM_CHANNEL_4, HAL_TIM_CHANNEL_STATE_READY);
        }
    } else {
        /* nothing to do */
    }
// if data transfer
    if (strip->buf_counter < strip->num_pixels) {
        // fill second part of buffer
        for (volatile u8_t i = 0; i < 8; i++) {
            strip->pwm_buf[i + 24] = (((strip->rgb_buf[3 * strip->buf_counter] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 32] = (((strip->rgb_buf[3 * strip->buf_counter + 1] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 40] = (((strip->rgb_buf[3 * strip->buf_counter + 2] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
        }
        strip->buf_counter++;
    } else if (strip->buf_counter < strip->num_pixels + 2) { // if RET transfer
        memset((u32_t *) &strip->pwm_buf[PWM_BUF_LEN / 2], 0, (PWM_BUF_LEN / 2)*sizeof(u32_t)); // second part
        strip->buf_counter++;
    } else { // if END of transfer
        strip->buf_counter = 0;
        // STOP DMA:
			
			switch(strip->tim_ch){
							case TIM_CHANNEL_1:
        __HAL_TIM_DISABLE_DMA(htim, TIM_DMA_CC1);
        (void) HAL_DMA_Abort_IT(htim->hdma[TIM_DMA_ID_CC1]);
								break;
							case TIM_CHANNEL_2:
        __HAL_TIM_DISABLE_DMA(htim, TIM_DMA_CC2);
        (void) HAL_DMA_Abort_IT(htim->hdma[TIM_DMA_ID_CC2]);
								break;
							case TIM_CHANNEL_3:
        __HAL_TIM_DISABLE_DMA(htim, TIM_DMA_CC3);
        (void) HAL_DMA_Abort_IT(htim->hdma[TIM_DMA_ID_CC3]);
								break;
							case TIM_CHANNEL_4:
        __HAL_TIM_DISABLE_DMA(htim, TIM_DMA_CC4);
        (void) HAL_DMA_Abort_IT(htim->hdma[TIM_DMA_ID_CC4]);
								break;
							default:
								break;
						}

        if (IS_TIM_BREAK_INSTANCE(htim->Instance) != RESET) {
            /* Disable the Main Output */
            __HAL_TIM_MOE_DISABLE(htim);
        }
        /* Disable the Peripheral */
        __HAL_TIM_DISABLE(htim);
        /* Set the TIM channel state */
        TIM_CHANNEL_STATE_SET(htim, strip->tim_ch, HAL_TIM_CHANNEL_STATE_READY);
        strip->state = ARGB_READY;
    }
    htim->Channel = HAL_TIM_ACTIVE_CHANNEL_CLEARED;
}

/**
  * @brief  TIM DMA Delay Pulse half complete callback.
  * @param  hdma pointer to DMA handle.
  * @retval None
  */
static void ARGB_TIM_DMADelayPulseHalfCplt(DMA_HandleTypeDef *hdma) {
		ARGB_Strip *strip = (ARGB_Strip *)hdma->Parent_strip;
	
	
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *) ((DMA_HandleTypeDef *) hdma)->Parent;
    // if wrong handlers
    if (hdma != strip->dma_handle || htim != strip->tim_handle) return;
    if (strip->buf_counter == 0) return; // if no data to transmit - return
    // if data transfer
    if (strip->buf_counter < strip->num_pixels) {
        // fill first part of buffer
        for (volatile u8_t i = 0; i < 8; i++) {

            strip->pwm_buf[i] = (((strip->rgb_buf[3 * strip->buf_counter] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 8] = (((strip->rgb_buf[3 * strip->buf_counter + 1] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
            strip->pwm_buf[i + 16] = (((strip->rgb_buf[3 * strip->buf_counter + 2] << i) & 0x80) > 0) ? strip->PWM_HI : strip->PWM_LO;
        }
        strip->buf_counter++;
    } else if (strip->buf_counter < strip->num_pixels + 2) { // if RET transfer
        memset((u32_t *) &strip->pwm_buf[0], 0, (PWM_BUF_LEN / 2)*sizeof(u32_t)); // first part
        strip->buf_counter++;
    }
}

