# DFPlayer Mini — библиотека для STM32 на чистом CMSIS

Драйвер MP3-модуля DFPlayer Mini (YX5300 / MH2024K) без HAL — только регистры.

## Возможности

- Работа с любым USART (USART1/2/3), пины настраиваются автоматически
- Частота шины задаётся при инициализации — не привязано к 72 МГц
- Неблокирующий приём: разбор ответов не тормозит главный цикл
- Функция задержки передаётся снаружи — библиотека не захватывает SysTick
- Проверка контрольной суммы на приём и передачу
- ~700 байт флеша

## Подключение

| STM32 (USART1) | DFPlayer Mini | Назначение |
|---|---|---|
| PA9 | пин 2 (RX) | TX → RX |
| PA10 | пин 3 (TX) | RX ← TX |
| 5V | пин 1 (VCC) | питание |
| GND | пин 7 (GND) | общая земля |

Динамик: между пином 8 (SPK_1) и пином 6 (SPK_2), 3 Вт 4–8 Ом.
Наушники — только на DAC_R/DAC_L (пины 4, 5), не на SPK.

Для USART2 используются PA2/PA3, для USART3 — PB10/PB11.

### Питание

На максимальной громкости модуль потребляет до 200 мА пиками.
Пин `5V` платы через USB это отдаёт с трудом — возможны хрипы и
перезагрузки. Для стабильной работы нужен отдельный источник 5 В
с общей землёй.

## Карта памяти

- FAT16 или FAT32
- Файлы в корне: `0001.mp3`, `0002.mp3`, …
- Или в папках: `/MP3/0001.mp3`, `/01/001.mp3`
- MP3: 8–48 кГц, 32–320 кбит/с

**Важно:** модуль не умеет пропускать теги ID3v2 с обложкой.
Файл с картинкой внутри вызовет ошибку `0x40`. Чистить так:

```bash
ffmpeg -i in.mp3 -vn -c:a copy -map_metadata -1 -map 0:a out.mp3
```

Также удаляй скрытые папки вроде `.Trash-1000` — модуль считает
файлы в них и сбивает нумерацию треков.

## Использование

```c
#include "dfplayer.h"

static void delay_ms(uint32_t ms) { /* своя реализация */ }

int main(void)
{
    DF_Init(USART1, 72000000, delay_ms);

    DF_Reset();
    DF_SetVolume(25);
    DF_PlayTrack(1);

    while (1) {
        if (DF_Poll()) {
            if (df_msg.cmd == DF_RSP_FINISHED) DF_Next();
        }
    }
}
```

Если передать `NULL` вместо функции задержки, используется
встроенный грубый цикл, рассчитанный от `pclk`.

## API

### Инициализация
```c
uint8_t DF_Init(USART_TypeDef *uart, uint32_t pclk, void (*delay_ms)(uint32_t));
```
`pclk` — частота шины этого USART: APB2 для USART1, APB1 для USART2/3.
Возвращает 0, если UART не поддерживается.

### Воспроизведение
```c
void DF_PlayTrack(uint16_t track);               // трек N по порядку записи
void DF_PlayMP3(uint16_t track);                 // /MP3/NNNN.mp3
void DF_PlayFolder(uint8_t folder, uint8_t tr);  // /NN/MMM.mp3
void DF_Play(void);
void DF_Pause(void);
void DF_Stop(void);
void DF_Next(void);
void DF_Prev(void);
void DF_LoopAll(uint8_t on);
void DF_LoopTrack(uint16_t track);
```

`DF_PlayTrack` адресует треки по **порядку записи на карту**, а не по
имени файла. Если копировал файлы вразнобой, номера не совпадут с
именами — тогда надёжнее `DF_PlayFolder`.

### Настройки
```c
void DF_Reset(void);
void DF_SetVolume(uint8_t vol);   // 0..30
void DF_VolumeUp(void);
void DF_VolumeDown(void);
void DF_SetEQ(uint8_t eq);        // DF_EQ_NORMAL … DF_EQ_BASS
void DF_Standby(uint8_t on);
```

Громкость сбрасывается после `DF_Reset()` — ставь её после сброса.

### Приём ответов
```c
uint8_t DF_Poll(void);            // 1 = кадр разобран
extern volatile df_msg_t df_msg;  // .cmd и .param
```

Модуль отвечает только на запросы (`DF_Query*`) и сам присылает
`DF_RSP_READY` после старта и `DF_RSP_FINISHED` по окончании трека.
На обычные команды ответа нет — это нормально.

### Запросы
```c
void DF_QueryStatus(void);     // ответ 0x42: младший байт 1 = играет
void DF_QueryVolume(void);     // ответ 0x43
void DF_QueryFileCount(void);  // ответ 0x48: число треков
```

### Коды ошибок (`df_msg.cmd == DF_RSP_ERROR`)

| Код | Значение |
|---|---|
| `DF_ERR_BUSY` (1) | модуль занят |
| `DF_ERR_SLEEP` (2) | спящий режим |
| `DF_ERR_FRAME` (3) | ошибка кадра |
| `DF_ERR_CHECKSUM` (4) | битая контрольная сумма |
| `DF_ERR_OUT_OF_RANGE` (5) | номер трека вне диапазона |
| `DF_ERR_NO_FILE` (6) | файл не найден |

## Протокол

Кадр 10 байт, 9600 8N1:

```
7E FF 06 CMD ACK PH PL CKH CKL EF
```

`checksum = -(FF + 06 + CMD + ACK + PH + PL)`

Между кадрами нужна пауза ~50 мс, иначе модуль теряет команды.

