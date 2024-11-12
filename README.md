# ARGB_multi-threaded
library library for controlling addressable WS2812 LED using several timer channels

config:

1. add ``` void                       *Parent_strip; ```

on DMA_HandleTypeDef struct

![Снимок экрана 2024-11-12 180710](https://github.com/user-attachments/assets/f9614c60-f9c4-4125-b0a3-87a6b14e7904)


2. create and fill one or few ARGB_Strip structure

```
ARGB_Strip my_strip;

my_strip.tim_handle = &htim2;
my_strip.dma_handle = &hdma_tim2_ch2_ch4;
my_strip.buf_counter = 0;
my_strip.brightness = 255;
my_strip.state = ARGB_READY;
my_strip.num_pixels = 16;
my_strip.apb = 2;
my_strip.tim_ch = TIM_CHANNEL_2;
my_strip.rgb_buf = (uint8_t *)malloc(3 * my_strip.num_pixels * sizeof(uint8_t));
my_strip.pwm_buf = (uint32_t *)malloc(3*8*2 * sizeof(uint32_t));
```

3. set your struct as parent
```hdma_tim2_ch2_ch4.Parent_strip = &my_strip;```
