#include "dfplayer.h"
#include "mic.h"

/* ---- Настройки детектора ---- */
#define TRACK_COUNT     4      /* сколько треков на карте */
#define TRIGGER_MARGIN  400    /* насколько выше тишины считать шумом */
#define COOLDOWN_MS     500    /* пауза после окончания трека */

/* ---- Для наблюдения в отладчике ---- */
volatile uint16_t dbg_level    = 0;   /* текущий уровень */
volatile uint16_t dbg_baseline = 0;   /* уровень тишины */
volatile uint16_t dbg_peak     = 0;   /* максимум за всё время */
volatile uint32_t dbg_triggers = 0;   /* сколько раз сработало */
volatile uint16_t dbg_track    = 0;   /* последний запущенный трек */

static void delay_ms(uint32_t ms)
{
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
}

static void systick_init(void)
{
    SysTick->LOAD = 72000 - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    (void)SysTick->CTRL;
}

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
    uint16_t level, threshold;
    uint8_t  playing = 0;

    clock_init();
    systick_init();

    /* Микрофон на PA0. Калибровка тишины идёт внутри - молчи 2 секунды. */
    MIC_Init(0);
    dbg_baseline = MIC_GetBaseline();
    threshold    = dbg_baseline + TRIGGER_MARGIN;

    /* Плеер на USART1 (PA9/PA10) */
    DF_Init(USART1, 72000000, delay_ms);
    DF_Reset();
    DF_SetVolume(30);
    DF_SetEQ(DF_EQ_NORMAL);

    while (1) {
        /* Ловим ответы модуля */
        if (DF_Poll()) {
            if (df_msg.cmd == DF_RSP_FINISHED) {
                playing = 0;
                delay_ms(COOLDOWN_MS);
                /* перекалибровка: AGC мог сдвинуть уровень за время трека */
                MIC_Calibrate();
                dbg_baseline = MIC_GetBaseline();
                threshold    = dbg_baseline + TRIGGER_MARGIN;
            }
        }

        /* Пока играет - микрофон не слушаем, иначе сработает от динамика */
        if (playing) continue;

        level = MIC_ReadLevel();
        dbg_level = level;
        if (level > dbg_peak) dbg_peak = level;

        if (level > threshold) {
            dbg_track = MIC_Random(TRACK_COUNT);
            DF_PlayTrack(dbg_track);
            dbg_triggers++;
            playing = 1;
        }
    }
}