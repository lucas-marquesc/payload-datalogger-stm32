/**
 * logger.c - CSV no microSD com FatFS (middleware do CubeMX, driver SDIO de 4 bits).
 * Nomes 8.3: LOG_0001.CSV, LOG_0002.CSV, ...
 */
#include "logger.h"
#include "main.h"
#include "fatfs.h"
#include "bsp_driver_sd.h"
#include "sdio.h"
#include <string.h>

#define LOG_BUF_SIZE   2048U

static uint8_t  log_buf[LOG_BUF_SIZE];
static uint16_t log_used = 0;
static uint8_t  log_open = 0;

static const char CSV_HEADER[] =
  "timestamp_ms,temp_C,hum_pct,press_hPa,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,status\n";

/* Sobrescreve a funcao fraca do BSP: o CubeMX nao configurou pino de deteccao, entao o
 * driver sempre achava que havia cartao. Aqui o resultado depende de SD_CD_MODE (logger.h). */
uint8_t BSP_SD_IsDetected(void)
{
  return Logger_CardPresent() ? SD_PRESENT : SD_NOT_PRESENT;
}

uint8_t Logger_CardPresent(void)
{
#if SD_CD_MODE == 0
  return 1U;   /* sem depender do pino: a montagem falha se nao houver cartao */
#else
  GPIO_PinState nivel = HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin);
  GPIO_PinState presente = (SD_CD_MODE == 1) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  return (nivel == presente) ? 1U : 0U;
#endif
}

uint8_t Logger_IsOpen(void)
{
  return log_open;
}

/* Grava o buffer pendente no arquivo. */
static log_status_t flush_buffer(void)
{
  if (log_used == 0) {
    return LOG_OK;
  }
  UINT bw = 0;
  FRESULT fr = f_write(&SDFile, log_buf, log_used, &bw);
  if (fr != FR_OK) {
    return LOG_ERR_WRITE;
  }
  if (bw != log_used) {
    return LOG_ERR_FULL;              /* escrita parcial: nao coube no cartao */
  }
  log_used = 0;
  return LOG_OK;
}

/* Setores livres, usando o FatFS (o valor fica em cache depois da primeira chamada). */
static log_status_t check_free_space(void)
{
  DWORD  free_clusters = 0;
  FATFS *pfs = NULL;
  if (f_getfree(SDPath, &free_clusters, &pfs) != FR_OK) {
    return LOG_ERR_WRITE;
  }
  if ((uint64_t)free_clusters * pfs->csize < LOGGER_MIN_FREE_SECTORS) {
    return LOG_ERR_FULL;
  }
  return LOG_OK;
}

log_status_t Logger_Open(void)
{
  if (log_open) {
    return LOG_OK;
  }
  log_used = 0;

  /* Desmonta e reinicia o controlador: permite recuperar depois de troca ou remocao do cartao. */
  f_mount(NULL, SDPath, 0);
  HAL_SD_DeInit(&hsd);
  if (disk_initialize(0) != 0) {
    return LOG_ERR_MOUNT;
  }
  if (f_mount(&SDFatFS, SDPath, 1) != FR_OK) {
    return LOG_ERR_MOUNT;
  }

  log_status_t st = check_free_space();
  if (st != LOG_OK) {
    return st;
  }

  /* Procura o primeiro nome livre: LOG_0001.CSV, LOG_0002.CSV, ... */
  char name[24];
  FRESULT fr = FR_EXIST;
  for (uint16_t n = 1; n <= 9999U && fr == FR_EXIST; n++) {
    char *p = name;
    const char *d = SDPath;
    while (*d) { *p++ = *d++; }
    memcpy(p, "LOG_", 4); p += 4;
    *p++ = (char)('0' + (n / 1000U) % 10U);
    *p++ = (char)('0' + (n / 100U)  % 10U);
    *p++ = (char)('0' + (n / 10U)   % 10U);
    *p++ = (char)('0' + n % 10U);
    memcpy(p, ".CSV", 5); /* inclui o terminador */
    fr = f_open(&SDFile, name, FA_CREATE_NEW | FA_WRITE);
  }
  if (fr != FR_OK) {
    return LOG_ERR_OPEN;
  }
  log_open = 1;

  st = Logger_Write(CSV_HEADER, (uint16_t)(sizeof(CSV_HEADER) - 1U));
  if (st == LOG_OK) {
    st = Logger_Sync();
  }
  if (st != LOG_OK) {
    Logger_Close();
  }
  return st;
}

log_status_t Logger_Write(const char *line, uint16_t len)
{
  if (!log_open) {
    return LOG_ERR_WRITE;
  }
  if ((uint32_t)log_used + len > LOG_BUF_SIZE) {
    log_status_t st = flush_buffer();
    if (st != LOG_OK) {
      return st;
    }
  }
  memcpy(&log_buf[log_used], line, len);
  log_used += len;
  return LOG_OK;
}

log_status_t Logger_Sync(void)
{
  if (!log_open) {
    return LOG_ERR_WRITE;
  }
  log_status_t st = flush_buffer();
  if (st != LOG_OK) {
    return st;
  }
  if (f_sync(&SDFile) != FR_OK) {
    return LOG_ERR_WRITE;
  }
  return check_free_space();
}

void Logger_Close(void)
{
  if (log_open) {
    flush_buffer();                   /* melhor esforco: ignora o resultado */
    f_close(&SDFile);
    log_open = 0;
  }
  log_used = 0;
  f_mount(NULL, SDPath, 0);
}
