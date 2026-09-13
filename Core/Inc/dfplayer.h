/**
 * dfplayer.h - DFPlayer Mini (YX5300/MH2024K) driver for STM32, bare CMSIS.
 *
 * No HAL, no SPL. Works with any USART and any clock frequency.
 *
 * Usage:
 *     DF_Init(USART1, 72000000, df_delay);
 *     DF_SetVolume(25);
 *     DF_PlayTrack(1);
 *     while (1) { if (DF_Poll()) { ... } }
 */
#ifndef DFPLAYER_H
#define DFPLAYER_H

#include "stm32f1xx.h"
#include <stdint.h>

/* ---------------- Команды протокола ---------------- */
#define DF_CMD_NEXT         0x01
#define DF_CMD_PREV         0x02
#define DF_CMD_TRACK        0x03  /* играть трек N по порядку записи */
#define DF_CMD_VOL_UP       0x04
#define DF_CMD_VOL_DOWN     0x05
#define DF_CMD_VOLUME       0x06  /* 0..30 */
#define DF_CMD_EQ           0x07  /* 0..5 */
#define DF_CMD_LOOP_TRACK   0x08
#define DF_CMD_SOURCE       0x09  /* 2 = SD */
#define DF_CMD_STANDBY      0x0A
#define DF_CMD_RESET        0x0C
#define DF_CMD_PLAY         0x0D
#define DF_CMD_PAUSE        0x0E
#define DF_CMD_FOLDER       0x0F  /* /NN/MMM.mp3 */
#define DF_CMD_LOOP_ALL     0x11
#define DF_CMD_PLAY_MP3     0x12  /* /MP3/NNNN.mp3 */
#define DF_CMD_STOP         0x16
#define DF_CMD_QUERY_STATUS 0x42
#define DF_CMD_QUERY_VOL    0x43
#define DF_CMD_QUERY_FILES  0x48

/* ---------------- Ответы модуля ---------------- */
#define DF_RSP_FINISHED     0x3D  /* трек доиграл, param = номер */
#define DF_RSP_READY        0x3F  /* носитель готов, param бит 1 = SD */
#define DF_RSP_ERROR        0x40  /* ошибка, param = код */
#define DF_RSP_ACK          0x41  /* команда принята */
#define DF_RSP_STATUS       0x42
#define DF_RSP_VOLUME       0x43
#define DF_RSP_FILES        0x48

/* Коды ошибок (в param кадра 0x40) */
#define DF_ERR_BUSY         0x01
#define DF_ERR_SLEEP        0x02
#define DF_ERR_FRAME        0x03
#define DF_ERR_CHECKSUM     0x04
#define DF_ERR_OUT_OF_RANGE 0x05
#define DF_ERR_NO_FILE      0x06

/* Эквалайзер */
#define DF_EQ_NORMAL  0
#define DF_EQ_POP     1
#define DF_EQ_ROCK    2
#define DF_EQ_JAZZ    3
#define DF_EQ_CLASSIC 4
#define DF_EQ_BASS    5

/* ---------------- Принятый кадр ---------------- */
typedef struct {
    uint8_t  cmd;      /* код ответа, см. DF_RSP_* */
    uint16_t param;    /* параметр из байтов 5-6 */
} df_msg_t;

/* Последний корректно разобранный кадр. */
extern volatile df_msg_t df_msg;

/* ================== API ================== */

/**
 * Инициализация.
 * @param uart     USART1, USART2 или USART3
 * @param pclk     частота шины этого USART в Гц
 *                 (APB2 для USART1, APB1 для USART2/3)
 * @param delay_ms функция задержки; если 0, используется
 *                 встроенный грубый цикл
 *
 * Пины настраиваются автоматически:
 *   USART1 -> PA9 / PA10
 *   USART2 -> PA2 / PA3
 *   USART3 -> PB10 / PB11
 *
 * @return 1 если UART поддерживается, иначе 0
 */
uint8_t DF_Init(USART_TypeDef *uart, uint32_t pclk, void (*delay_ms)(uint32_t));

/**
 * Опрос приёмника. Вызывать часто из главного цикла.
 * @return 1 когда собран корректный кадр (лежит в df_msg)
 */
uint8_t DF_Poll(void);

/* Низкоуровневая отправка. ack=1 - требовать подтверждение. */
void DF_SendCmd(uint8_t cmd, uint8_t ph, uint8_t pl, uint8_t ack);

/* ---------------- Воспроизведение ---------------- */
void DF_PlayTrack(uint16_t track);              /* трек N с карты */
void DF_PlayMP3(uint16_t track);                /* /MP3/NNNN.mp3 */
void DF_PlayFolder(uint8_t folder, uint8_t tr); /* /NN/MMM.mp3 */
void DF_Play(void);
void DF_Pause(void);
void DF_Stop(void);
void DF_Next(void);
void DF_Prev(void);
void DF_LoopAll(uint8_t on);
void DF_LoopTrack(uint16_t track);

/* ---------------- Настройки ---------------- */
void DF_Reset(void);
void DF_SetVolume(uint8_t vol);   /* 0..30 */
void DF_VolumeUp(void);
void DF_VolumeDown(void);
void DF_SetEQ(uint8_t eq);
void DF_Standby(uint8_t on);

/* ---------------- Запросы ----------------
   Ответ придёт асинхронно в df_msg, ловить через DF_Poll. */
void DF_QueryStatus(void);
void DF_QueryVolume(void);
void DF_QueryFileCount(void);

#endif /* DFPLAYER_H */