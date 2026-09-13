#include "dfplayer.h"

/* ---- Задержка на SysTick, отдаём её библиотеке ---- */
static void delay_ms(uint32_t ms)
{
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
}

static void systick_init(void)
{
    SysTick->LOAD = 72000 - 1;   /* 1 мс при 72 МГц */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    (void)SysTick->CTRL;
}

/* ---- Blue Pill: HSE 8 МГц -> PLL x9 -> 72 МГц ---- */
static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
    RCC->CFGR |= RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClock = 72000000;
}

int main(void)
{
    clock_init();
    systick_init();

    /* USART1 на PA9/PA10, шина APB2 = 72 МГц */
    DF_Init(USART1, 72000000, delay_ms);

    DF_Reset();
    DF_SetVolume(30);
    DF_SetEQ(DF_EQ_NORMAL);

    DF_PlayTrack(4);

    while (1) {
     
        DF_PlayTrack(4);

        delay_ms(8000);
        DF_PlayTrack(3);
        delay_ms(8000);
        DF_PlayTrack(2);
         delay_ms(8000);
        DF_PlayTrack(1);
           delay_ms(8000);
        
        
    }
}