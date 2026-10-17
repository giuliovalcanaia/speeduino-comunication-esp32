# Speeduino Logger com ESP32 e RTC DS3231

Este projeto substitui o módulo Bluetooth HC-06 da Speeduino por um **ESP32 clássico**, adicionando logging em cartão micro SD com **timestamps reais** via RTC DS3231.

## Sumário

- [Visão geral](#visao-geral)
- [Estrutura do projeto](#estrutura-do-projeto)
- [Hardware](#hardware)
  - [Ligação do ESP32 à Speeduino](#ligacao-do-esp32-a-speeduino)
  - [Ligação do RTC DS3231](#ligacao-do-rtc-ds3231)
  - [Ligação do módulo micro SD](#ligacao-do-modulo-micro-sd)
- [Sketches](#sketches)
  - [`set_rtc_time`](#set_rtc_time)
  - [`speeduino_logger`](#speeduino_logger)
- [Modos de operação](#modos-de-operacao)
- [Formato do log CSV](#formato-do-log-csv)
- [Organização dos arquivos no SD](#organizacao-dos-arquivos-no-sd)
- [Limpeza automática de logs antigos](#limpeza-automatica-de-logs-antigos)
- [Fallback quando o RTC falha](#fallback-quando-o-rtc-falha)
- [Indicação do LED](#indicacao-do-led)
- [Compilação](#compilacao)
- [Como usar](#como-usar)

## Visão geral

O ESP32 funciona como uma **ponte transparente** entre a ECU Speeduino e o TunerStudio (via Wi-Fi TCP). Ao mesmo tempo, ele escuta a comunicação e grava as respostas de dados em tempo real no cartão SD.

Quando nenhum cliente está conectado, o ESP32 passa a fazer **polling autônomo** da ECU a 30 Hz e também grava os dados no SD.

A adição do RTC DS3231 permite que cada linha do log tenha um **timestamp real** e que os arquivos sejam organizados em pastas diárias com nomes no padrão ISO 8601.

## Estrutura do projeto

```
speeduino-comunication-esp32/
├── speeduino_logger/
│   ├── speeduino_logger.ino   # Sketch principal: setup, loop e tasks
│   ├── config.h               # Constantes, pinos e USE_WIFI
│   ├── channels.h/.cpp        # Tabela de canais gravados no CSV
│   ├── rtc_time.h/.cpp        # DS3231 e formatação ISO 8601
│   ├── frame_queue.h/.cpp     # Fila FreeRTOS de frames brutos
│   ├── log_store.h/.cpp       # SD: abertura, rotação, limpeza e CSV
│   ├── transport.h/.cpp       # Wi-Fi AP + TCP 2000 ou Bluetooth SPP
│   ├── ecu_bridge.h/.cpp      # Serial2, polling e ponte/sniffer
│   ├── web_ui.h/.cpp          # Servidor HTTP de logs (USE_WIFI = 1)
│   └── led_status.h/.cpp      # Padrões do LED
└── set_rtc_time/
    └── set_rtc_time.ino       # Sketch para ajustar e conferir o RTC
```

## Hardware

### Ligação do ESP32 à Speeduino

| Sinal | Speeduino | ESP32 |
|---|---|---|
| TX da ECU (5 V) | TX | GPIO16 (RX2) via divisor 5 V → 3,3 V |
| RX da ECU | RX | GPIO17 (TX2) |
| GND | GND | GND |
| Alimentação | 5 V (pino VCC do conector HC-06) | VIN |

> **Atenção:** o pino TX da Speeduino é 5 V. Use um divisor de tensão para 3,3 V antes de ligar no GPIO16 do ESP32.

### Ligação do RTC DS3231

| DS3231 | ESP32 |
|---|---|
| VCC | 3,3 V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO19 |

### Ligação do módulo micro SD

| SD | ESP32 |
|---|---|
| VCC (5 V)* | VIN |
| GND | GND |
| CS | GPIO25 |
| MOSI | GPIO27 |
| CLK/SCK | GPIO26 |
| MISO | GPIO14 |

\* *Use 3,3 V se o seu módulo SD não tiver regulador interno 5 V → 3,3 V.*

## Sketches

### `set_rtc_time`

Sketch auxiliar para **ajustar e conferir** o RTC DS3231.

**Funções:**

1. Ajusta o RTC com a hora de compilação (`__DATE__` e `__TIME__`).
2. Aplica o fuso horário configurável (`HOURS_TO_SUBTRACT_FROM_UTC`).
3. Cria um ponto de acesso Wi-Fi aberto chamado `SPEEDUINO-RTC`.
4. Disponibiliza uma página web em `http://192.168.4.1` para conferir a hora atual do RTC.

**Configuração de fuso horário:**

```cpp
static const int HOURS_TO_SUBTRACT_FROM_UTC = 3;
```

- Se o computador de compilação está em **UTC** e você quer gravar **BRT (UTC-3)** no RTC: use `3`.
- Se o computador de compilação já está em **BRT**: use `0`.

**Página web:**

- SSID: `SPEEDUINO-RTC`
- Senha: *(aberto, sem senha)*
- IP: `192.168.4.1`
- Atualização automática a cada 1 segundo.

### `speeduino_logger`

Sketch principal. Faz a ponte com a ECU, o sniffer dos dados e o logging no SD.

**Principais constantes configuráveis** (em `speeduino_logger/config.h`):

```cpp
static const uint32_t POLL_HZ              = 30;                 // polling autonomo
static const uint32_t MAX_LOG_SIZE_BYTES   = 10UL * 1024 * 1024; // 10 MB por arquivo
static const uint32_t SD_FREE_MARGIN_MB    = 100;                // margem de espaco livre
static const int      RTC_SDA_PIN          = 21;
static const int      RTC_SCL_PIN          = 22;
```

## Modos de operação

1. **Cliente conectado** (TunerStudio/celular via Wi-Fi TCP):
   - O ESP32 atua como ponte transparente ECU ↔ cliente.
   - Escuta a conversa e grava as respostas de dados no SD.
   - Coluna `src` do CSV: `sniff`.

2. **Sem cliente conectado**:
   - O ESP32 envia o comando `n` para a ECU a 30 Hz.
   - Grava as respostas no SD.
   - Coluna `src` do CSV: `poll`.

## Formato do log CSV

Cada arquivo começa com uma linha de cabeçalho:

```csv
datetime,time_s,src,secl,status1,engine,dwell_ms,MAP_kPa,IAT_C,CLT_C,batt_V,O2_AFR,egoCorr_pct,RPM,...
```

- `datetime` — data e hora real do RTC, formato ISO 8601 (`YYYY-MM-DDTHH:MM:SS`).
- `time_s` — segundos desde que o ESP32 foi ligado.
- `src` — origem da amostra: `poll` ou `sniff`.
- Os demais campos são os canais de tempo real da Speeduino, configurados na tabela `CH[]`.

## Organização dos arquivos no SD

Os logs são organizados em pastas diárias:

```
/2026-10-14/2026-10-14T09-45-22.csv
/2026-10-14/2026-10-14T10-12-07.csv
/2026-10-15/2026-10-15T08-33-19.csv
```

- Cada pasta representa um dia (`YYYY-MM-DD`).
- Cada arquivo dentro da pasta recebe o timestamp de início da gravação.
- O nome do arquivo usa hífens no lugar dos dois-pontos, pois `:` não é permitido em nomes de arquivo no FAT32.

## Limpeza automática de logs antigos

Antes de criar qualquer novo arquivo de log, o sistema verifica o espaço livre no SD:

- Se o espaço livre for **menor que 100 MB** (`SD_FREE_MARGIN_MB`):
  1. Encontra o arquivo `.csv` mais antigo dentro das pastas diárias.
  2. Remove o arquivo.
  3. Remove a pasta se ficar vazia.
  4. Repete até liberar espaço suficiente.

Se não houver mais logs antigos para remover e o espaço ainda for insuficiente, o logger para de gravar e sinaliza erro no LED.

## Fallback quando o RTC falha

Se o DS3231 não for detectado no boot:

- O logger continua funcionando.
- Os arquivos são salvos na pasta `/NO_RTC/`.
- O nome do arquivo usa o tempo desde o boot: `BOOT_xxxxxxxxx.csv`.
- A coluna `datetime` fica vazia no CSV.
- O LED pisca a cada 200 ms para indicar falha do RTC.

## Indicação do LED

| Padrão | Significado |
|---|---|
| Piscando a cada 100 ms | Erro no SD |
| Piscando a cada 200 ms | RTC falhou (fallback ativo) |
| Piscando a cada 500 ms | Logando normalmente |
| Piscando a cada 1500 ms | Aguardando conexão/polling |

## Compilação

- Placa: **ESP32 Dev Module**
- Core: `esp32:esp32:esp32`
- Bibliotecas necessárias:
  - `RTClib` da Adafruit (para DS3231)
  - `SD` (nativa do ESP32)
  - `WiFi` / `WebServer` (nativas do ESP32)

> **Nota:** o ESP32 clássico é obrigatório. Os modelos S2, S3 e C3 **não** possuem Bluetooth clássico e não funcionam com a opção Bluetooth SPP.

## Como usar

1. **Ajuste o RTC:**
   - Configure `HOURS_TO_SUBTRACT_FROM_UTC` no `set_rtc_time.ino` conforme o fuso do seu computador.
   - Compile e grave `set_rtc_time.ino` no ESP32.
   - Conecte-se à rede `SPEEDUINO-RTC` e acesse `http://192.168.4.1` para conferir a hora.

2. **Grave o logger principal:**
   - Compile e grave `speeduino_logger.ino` no ESP32.
   - Insira um cartão micro SD formatado em FAT32.

3. **Conecte o TunerStudio:**
   - Conecte o computador/celular à rede `Speeduino` (senha: `qK95xvFC`).
   - No TunerStudio, use o driver **TCP/IP – WiFi driver** com IP `192.168.4.1` e porta `2000`.

4. **Verifique os logs:**
   - Os arquivos CSV estarão organizados em pastas diárias no cartão SD.
   - Abra diretamente no MegaLogViewer ou Excel.

## Histórico de modificações

- Adicionado suporte ao RTC DS3231 via I2C.
- Adicionada coluna `datetime` com timestamp real no CSV.
- Logs organizados em pastas diárias (`/YYYY-MM-DD/`).
- Nome dos arquivos no padrão ISO 8601.
- Rotação automática de arquivos a cada 10 MB.
- Limpeza automática de logs antigos quando o SD fica com menos de 100 MB livres.
- Fallback para `/NO_RTC/` quando o RTC falha.
- Criado sketch `set_rtc_time` com servidor web para conferência da hora.
- Refatorado `speeduino_logger.ino` em módulos `.h/.cpp` (`config`, `transport`, `ecu_bridge`, `frame_queue`, `log_store`, `web_ui`, `led_status`).
- `/NO_RTC/` passou a ser incluído na limpeza automática de logs antigos.
- `sdTask` com stack aumentado e download de logs segmentado no servidor web.

## Pontos em aberto / limitações conhecidas

> **Latência do cartão SD vs. frequência de polling**
>
> O logger grava cada resposta da ECU como uma linha de CSV diretamente no SD (`logFile.write()`) e faz `flush()` a cada 1 segundo (`FLUSH_MS = 1000`). O polling autônomo é configurado para 30 Hz, mas **cartões SD lentos ou com picos de latência podem atrasar o loop principal e derrubar a frequência efetiva abaixo de 30 Hz**.
>
> Isso não é um problema de throughput (o volume de dados é pequeno, ~12 KB/s), mas de **latência imprevisível** das operações `write()` e, principalmente, `flush()`.
>
> **Ideias futuras para mitigar:**
> - Medir no hardware real o tempo gasto em `logFrame()` e `flush()`.
> - Criar um buffer circular de frames brutos na RAM (economiza espaço em relação a buffer de linhas CSV).
> - Mover a escrita no SD para uma tarefa FreeRTOS no segundo core do ESP32, desacoplando a comunicação com a ECU da gravação no cartão.
> - Ajustar `FLUSH_MS` ou adotar flush por tamanho acumulado, balanceando risco de perda de dados em queda de energia com a latência.
