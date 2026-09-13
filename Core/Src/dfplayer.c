/**
 * dfplayer.c - DFPlayer Mini driver for STM32F1, bare CMSIS.
 *
 * Протокол: 9600 8N1, кадр 10 байт
 *   7E FF 06 CMD ACK PH PL CKH CKL EF
 *   checksum = -(FF + 06 + CMD + ACK + PH + PL)
 */
#include "dfplayer.h"

#define DF_START    0x7E
#define DF_VERSION  0xFF
#define DF_LEN      0x06
#define DF_END      0xEF
#define DF_BAUD     9600UL

/* Пауза между кадрами: модуль не успевает обрабатывать быстрее */
#define DF_CMD_GAP_MS  50

volatile df_msg_t df_msg;

/* ---------------- Состояние драйвера ---------------- */
static USART_TypeDef *df_uart;
static void (*df_delay)(uint32_t);
static uint32_t df_pclk;

/* ---------------- Запасная задержка ----------------
   Используется, только если пользователь не дал свою.
   Грубая, но для пауз между кадрами достаточно.       */
static void df_delay_fallback(uint32_t ms)
{
    uint32_t loops = df_pclk / 6000;   /* ~1 мс на итерацию внешнего цикла */
    while (ms--) {
        volatile uint32_t i = loops;
        while (i--) __NOP();
    }
}

/* ---------------- Настройка пинов ----------------
   Возвращает 0 для неподдерживаемого UART.          */
static uint8_t df_gpio_setup(USART_TypeDef *uart)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    if (uart == USART1) {
        RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
        /* PA9 TX: альт. push-pull 50 МГц */
        GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
        GPIOA->CRH |=  (GPIO_CRH_MODE9_0 | GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1);
        /* PA10 RX: вход с подтяжкой вверх */
        GPIOA->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
        GPIOA->CRH |=  GPIO_CRH_CNF10_1;
        GPIOA->ODR |=  GPIO_ODR_ODR10;
        return 1;
    }

    if (uart == USART2) {
        RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
        RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
        /* PA2 TX */
        GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
        GPIOA->CRL |=  (GPIO_CRL_MODE2_0 | GPIO_CRL_MODE2_1 | GPIO_CRL_CNF2_1);
        /* PA3 RX */
        GPIOA->CRL &= ~(GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
        GPIOA->CRL |=  GPIO_CRL_CNF3_1;
        GPIOA->ODR |=  GPIO_ODR_ODR3;
        return 1;
    }

    if (uart == USART3) {
        RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
        RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
        /* PB10 TX */
        GPIOB->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
        GPIOB->CRH |=  (GPIO_CRH_MODE10_0 | GPIO_CRH_MODE10_1 | GPIO_CRH_CNF10_1);
        /* PB11 RX */
        GPIOB->CRH &= ~(GPIO_CRH_MODE11 | GPIO_CRH_CNF11);
        GPIOB->CRH |=  GPIO_CRH_CNF11_1;
        GPIOB->ODR |=  GPIO_ODR_ODR11;
        return 1;
    }

    return 0;
}

uint8_t DF_Init(USART_TypeDef *uart, uint32_t pclk, void (*delay_ms)(uint32_t))
{
    df_uart  = uart;
    df_pclk  = pclk;
    df_delay = delay_ms ? delay_ms : df_delay_fallback;

    df_msg.cmd   = 0;
    df_msg.param = 0;

    if (!df_gpio_setup(uart)) return 0;

    uart->BRR = (pclk + DF_BAUD / 2) / DF_BAUD;
    uart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    (void)uart->SR;    /* сбросить мусор в приёмнике */
    (void)uart->DR;

    df_delay(1500);    /* модулю нужно время на старт */
    return 1;
}

/* ---------------- Передача ---------------- */
static void df_write(uint8_t b)
{
    while (!(df_uart->SR & USART_SR_TXE));
    df_uart->DR = b;
}

void DF_SendCmd(uint8_t cmd, uint8_t ph, uint8_t pl, uint8_t ack)
{
    uint16_t sum = DF_VERSION + DF_LEN + cmd + ack + ph + pl;
    uint16_t chk = (uint16_t)(0 - sum);

    df_write(DF_START);
    df_write(DF_VERSION);
    df_write(DF_LEN);
    df_write(cmd);
    df_write(ack);
    df_write(ph);
    df_write(pl);
    df_write((uint8_t)(chk >> 8));
    df_write((uint8_t)(chk & 0xFF));
    df_write(DF_END);

    while (!(df_uart->SR & USART_SR_TC));
    df_delay(DF_CMD_GAP_MS);
}

/* ---------------- Приём ----------------
   Неблокирующий сборщик кадра.           */
uint8_t DF_Poll(void)
{
    static uint8_t buf[10];
    static uint8_t idx = 0;
    uint32_t sr = df_uart->SR;
    uint8_t  b, i;
    uint16_t sum = 0, chk;

    /* чтение DR снимает флаги ошибок */
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        (void)df_uart->DR;
        idx = 0;
        return 0;
    }

    if (!(sr & USART_SR_RXNE)) return 0;

    b = (uint8_t)df_uart->DR;

    if (idx == 0 && b != DF_START) return 0;   /* ждём начало кадра */

    buf[idx++] = b;
    if (idx < 10) return 0;

    idx = 0;

    if (buf[9] != DF_END) return 0;

    for (i = 1; i <= 6; i++) sum += buf[i];
    chk = ((uint16_t)buf[7] << 8) | buf[8];
    if ((uint16_t)(0 - sum) != chk) return 0;

    df_msg.cmd   = buf[3];
    df_msg.param = ((uint16_t)buf[5] << 8) | buf[6];
    return 1;
}

/* ---------------- Воспроизведение ---------------- */
void DF_PlayTrack(uint16_t t)  { DF_SendCmd(DF_CMD_TRACK, (uint8_t)(t >> 8), (uint8_t)t, 0); }
void DF_PlayMP3(uint16_t t)    { DF_SendCmd(DF_CMD_PLAY_MP3, (uint8_t)(t >> 8), (uint8_t)t, 0); }
void DF_PlayFolder(uint8_t f, uint8_t t) { DF_SendCmd(DF_CMD_FOLDER, f, t, 0); }
void DF_Play(void)             { DF_SendCmd(DF_CMD_PLAY, 0, 0, 0); }
void DF_Pause(void)            { DF_SendCmd(DF_CMD_PAUSE, 0, 0, 0); }
void DF_Stop(void)             { DF_SendCmd(DF_CMD_STOP, 0, 0, 0); }
void DF_Next(void)             { DF_SendCmd(DF_CMD_NEXT, 0, 0, 0); }
void DF_Prev(void)             { DF_SendCmd(DF_CMD_PREV, 0, 0, 0); }
void DF_LoopAll(uint8_t on)    { DF_SendCmd(DF_CMD_LOOP_ALL, 0, on ? 1 : 0, 0); }
void DF_LoopTrack(uint16_t t)  { DF_SendCmd(DF_CMD_LOOP_TRACK, (uint8_t)(t >> 8), (uint8_t)t, 0); }

/* ---------------- Настройки ---------------- */
void DF_Reset(void)
{
    DF_SendCmd(DF_CMD_RESET, 0, 0, 0);
    df_delay(1500);
}

void DF_SetVolume(uint8_t v)
{
    if (v > 30) v = 30;
    DF_SendCmd(DF_CMD_VOLUME, 0, v, 0);
}

void DF_VolumeUp(void)      { DF_SendCmd(DF_CMD_VOL_UP, 0, 0, 0); }
void DF_VolumeDown(void)    { DF_SendCmd(DF_CMD_VOL_DOWN, 0, 0, 0); }
void DF_SetEQ(uint8_t eq)   { DF_SendCmd(DF_CMD_EQ, 0, eq, 0); }
void DF_Standby(uint8_t on) { DF_SendCmd(on ? DF_CMD_STANDBY : DF_CMD_PLAY, 0, 0, 0); }

/* ---------------- Запросы ---------------- */
void DF_QueryStatus(void)    { DF_SendCmd(DF_CMD_QUERY_STATUS, 0, 0, 1); }
void DF_QueryVolume(void)    { DF_SendCmd(DF_CMD_QUERY_VOL, 0, 0, 1); }
void DF_QueryFileCount(void) { DF_SendCmd(DF_CMD_QUERY_FILES, 0, 0, 1); }