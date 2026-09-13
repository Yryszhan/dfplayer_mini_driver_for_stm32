#include "mic.h"

/* Сколько выборок в одном окне измерения.
   200 выборок при ~20 мкс на выборку = окно ~4 мс. */
#define MIC_SAMPLES   200

static uint8_t  mic_ch;
static uint16_t mic_baseline = 0;
static uint32_t rng_state    = 0;

/* ---------------- ADC1 ---------------- */
void MIC_Init(uint8_t channel)
{
    mic_ch = channel;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;

    /* ADC максимум 14 МГц: 72 / 6 = 12 МГц */
    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |=  RCC_CFGR_ADCPRE_DIV6;

    /* Пин в аналоговый режим: MODE=00, CNF=00 */
    GPIOA->CRL &= ~((GPIO_CRL_MODE0 | GPIO_CRL_CNF0) << (channel * 4));

    /* Время выборки 55.5 тактов - источник с высоким импедансом */
    if (channel < 10) {
        ADC1->SMPR2 &= ~(7U << (channel * 3));
        ADC1->SMPR2 |=  (5U << (channel * 3));
    }

    ADC1->SQR1 = 0;                /* одна конверсия в последовательности */
    ADC1->SQR3 = channel;          /* канал первый в очереди */

    ADC1->CR2 |= ADC_CR2_ADON;     /* включить */
    for (volatile uint32_t d = 0; d < 10000; d++) __NOP();   /* стабилизация */

    /* Калибровка АЦП */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while (ADC1->CR2 & ADC_CR2_RSTCAL);
    ADC1->CR2 |= ADC_CR2_CAL;
    while (ADC1->CR2 & ADC_CR2_CAL);

    MIC_Calibrate();
}

static uint16_t adc_read(void)
{
    ADC1->CR2 |= ADC_CR2_ADON;          /* запуск конверсии */
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)ADC1->DR;
}

uint16_t MIC_ReadLevel(void)
{
    uint16_t v, mn = 0xFFFF, mx = 0;
    uint16_t i;

    for (i = 0; i < MIC_SAMPLES; i++) {
        v = adc_read();
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        rng_state = rng_state * 1664525U + 1013904223U + v;  /* копим энтропию */
    }

    return mx - mn;
}

void MIC_Calibrate(void)
{
    uint32_t sum = 0;
    uint8_t  i;

    /* Усредняем 16 окон, чтобы случайный звук не испортил замер */
    for (i = 0; i < 16; i++) sum += MIC_ReadLevel();

    mic_baseline = (uint16_t)(sum / 16);
}

uint16_t MIC_GetBaseline(void)
{
    return mic_baseline;
}

uint16_t MIC_Random(uint16_t max)
{
    if (max == 0) return 0;
    rng_state = rng_state * 1664525U + 1013904223U;
    return (uint16_t)((rng_state >> 16) % max) + 1;
}