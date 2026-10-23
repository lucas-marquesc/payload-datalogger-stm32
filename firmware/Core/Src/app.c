/**
 * app.c - Aplicacao do datalogger.
 *
 * Fluxo: o INT1 do IMU (EXTI1) sinaliza amostra nova a ~208 Hz; o TIM3 (25 Hz) sinaliza a hora de ler
 * o BME280. Cada amostra do IMU vira uma linha do CSV, com o ultimo valor do BME280. O arquivo e
 * sincronizado (f_sync) a cada 1 s. O LED D1 (PA1) indica o estado por numero de piscadas.
 */
#include "app.h"

/* O label gerado no CubeMX chama-se IMU_INIT1; aceita tambem IMU_INT1 se for corrigido. */
#ifndef IMU_INT1_Pin
#define IMU_INT1_Pin IMU_INIT1_Pin
#endif
#include "main.h"
#include "tim.h"
#include "sensors.h"
#include "logger.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Estados e padroes de LED                                            */
/* ------------------------------------------------------------------ */
typedef enum {
  APP_NO_CARD,     /* sem cartao: tenta montar a cada 2 s                      (2 piscadas) */
  APP_RUNNING,     /* gravando (3 piscadas se algum sensor falhou)             (1 piscada)  */
  APP_STOPPED,     /* gravacao encerrada pelo botao: cartao pode ser removido  (LED apagado) */
  APP_ERR_FULL,    /* cartao cheio: arquivo fechado                            (4 piscadas) */
  APP_ERR_FS       /* erro de escrita ou do sistema de arquivos: remonta       (5 piscadas) */
} app_state_t;

#define LED_SOLID        0U     /* aceso fixo (inicializando) */
#define LED_OFF          255U

#define BLINK_ON_MS      150U
#define BLINK_PERIOD_MS  300U   /* 150 ms aceso + 150 ms apagado */
#define BLINK_PAUSE_MS   700U

#define RETRY_MS         2000U  /* intervalo para remontar o cartao e reiniciar sensores */
#define SYNC_MS          1000U  /* intervalo do f_sync */
#define IMU_POLL_MS      10U    /* leitura de seguranca caso o INT1 nao chegue */
#define IMU_TIMEOUT_MS   500U   /* sem amostras por este tempo: considera falha do IMU */
#define SENSOR_MAX_ERR   3U     /* falhas seguidas para declarar um sensor com defeito */

static volatile uint8_t  imu_flag = 0;   /* setado no EXTI1 */
static volatile uint8_t  bme_flag = 0;   /* setado no TIM3 (25 Hz) */
static volatile uint8_t  btn_flag = 0;   /* setado no EXTI0, com debounce */
static volatile uint32_t btn_tick = 0;

static app_state_t state = APP_NO_CARD;
static uint8_t     led_mode = LED_SOLID;

static uint8_t bme_ok = 0, imu_ok = 0;
static uint8_t bme_valid = 0;            /* ha uma amostra do BME280 para repetir nas linhas */
static uint8_t bme_err = 0, imu_err = 0;
static bme_sample_t bme_last;

static uint32_t t_retry = 0, t_sync = 0, t_sensor_retry = 0, t_imu_poll = 0, t_imu_last = 0;

/* ------------------------------------------------------------------ */
/* Callbacks do HAL (sobrescrevem as funcoes fracas)                   */
/* ------------------------------------------------------------------ */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == IMU_INT1_Pin) {
    imu_flag = 1;
  } else if (GPIO_Pin == BTN_USER_Pin) {
    uint32_t now = HAL_GetTick();
    if ((now - btn_tick) > 200U) {       /* debounce de 200 ms */
      btn_tick = now;
      btn_flag = 1;
    }
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3) {
    bme_flag = 1;
  }
}

/* ------------------------------------------------------------------ */
/* LED de status (PA1, ativo em nivel alto)                            */
/* ------------------------------------------------------------------ */
static void led_update(uint32_t now)
{
  switch (state) {
    case APP_NO_CARD:  led_mode = 2; break;
    case APP_RUNNING:  led_mode = (bme_ok && imu_ok) ? 1U : 3U; break;
    case APP_STOPPED:  led_mode = LED_OFF; break;
    case APP_ERR_FULL: led_mode = 4; break;
    case APP_ERR_FS:   led_mode = 5; break;
    default:           led_mode = LED_SOLID; break;
  }

  GPIO_PinState on;
  if (led_mode == LED_OFF) {
    on = GPIO_PIN_RESET;
  } else if (led_mode == LED_SOLID) {
    on = GPIO_PIN_SET;
  } else {
    uint32_t burst  = (uint32_t)led_mode * BLINK_PERIOD_MS;
    uint32_t t      = now % (burst + BLINK_PAUSE_MS);
    on = (t < burst && (t % BLINK_PERIOD_MS) < BLINK_ON_MS) ? GPIO_PIN_SET : GPIO_PIN_RESET;
  }
  HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, on);
}

/* ------------------------------------------------------------------ */
/* Formatacao de linhas sem printf (evita o suporte a float do newlib) */
/* ------------------------------------------------------------------ */
static char *put_u32(char *p, uint32_t v)
{
  char tmp[10];
  uint8_t n = 0;
  do { tmp[n++] = (char)('0' + v % 10U); v /= 10U; } while (v);
  while (n) { *p++ = tmp[--n]; }
  return p;
}

/* v e um inteiro escalado por 10^dec (ex.: dec = 2, v = -1234 -> "-12.34") */
static char *put_fixed(char *p, int32_t v, uint8_t dec)
{
  uint32_t mag = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < dec; i++) { scale *= 10U; }
  if (v < 0) { *p++ = '-'; }
  p = put_u32(p, mag / scale);
  if (dec) {
    *p++ = '.';
    uint32_t frac = mag % scale;
    for (uint32_t d = scale / 10U; d > 0; d /= 10U) {
      *p++ = (char)('0' + (frac / d) % 10U);
    }
  }
  return p;
}

/* Monta: timestamp_ms,temp_C,hum_pct,press_hPa,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,status */
static uint16_t build_row(char *buf, uint32_t ts, const imu_sample_t *imu)
{
  char *p = buf;
  p = put_u32(p, ts);
  *p++ = ',';
  if (bme_ok && bme_valid) {
    p = put_fixed(p, bme_last.temp_c100, 2);                                   /* C */
    *p++ = ',';
    p = put_fixed(p, (int32_t)(((uint32_t)bme_last.hum_q10 * 100U + 512U) / 1024U), 2);   /* %RH */
    *p++ = ',';
    p = put_fixed(p, (int32_t)bme_last.press_pa, 2);                          /* Pa -> hPa */
    *p++ = ',';
  } else {
    *p++ = ','; *p++ = ','; *p++ = ',';
  }
  if (imu) {
    p = put_fixed(p, imu->ax_mg, 3);   *p++ = ',';
    p = put_fixed(p, imu->ay_mg, 3);   *p++ = ',';
    p = put_fixed(p, imu->az_mg, 3);   *p++ = ',';
    p = put_fixed(p, imu->gx_mdps, 3); *p++ = ',';
    p = put_fixed(p, imu->gy_mdps, 3); *p++ = ',';
    p = put_fixed(p, imu->gz_mdps, 3); *p++ = ',';
  } else {
    for (uint8_t i = 0; i < 6; i++) { *p++ = ','; }
  }
  uint8_t status = (uint8_t)((bme_ok && bme_valid ? 1U : 0U) | (imu ? 2U : 0U));
  p = put_u32(p, status);
  *p++ = '\n';
  return (uint16_t)(p - buf);
}

/* ------------------------------------------------------------------ */
/* Gravacao e tratamento de erros                                      */
/* ------------------------------------------------------------------ */
static void handle_log_error(log_status_t st, uint32_t now)
{
  Logger_Close();
  state   = (st == LOG_ERR_FULL) ? APP_ERR_FULL : APP_ERR_FS;
  t_retry = now;
}

static void try_open(uint32_t now)
{
  t_retry = now;
  if (!Logger_CardPresent()) {
    state = APP_NO_CARD;
    return;
  }
  log_status_t st = Logger_Open();
  if (st == LOG_OK) {
    state  = APP_RUNNING;
    t_sync = now;
  } else {
    state = (st == LOG_ERR_FULL) ? APP_ERR_FULL : APP_ERR_FS;
  }
}

static void write_row(uint32_t now, const imu_sample_t *imu)
{
  if (state != APP_RUNNING) {
    return;
  }
  char row[128];
  uint16_t len = build_row(row, now, imu);
  log_status_t st = Logger_Write(row, len);
  if (st != LOG_OK) {
    handle_log_error(st, now);
  }
}

/* ------------------------------------------------------------------ */
/* Leitura dos sensores                                                */
/* ------------------------------------------------------------------ */
static void service_bme(uint32_t now)
{
  if (!bme_flag) {
    return;
  }
  bme_flag = 0;
  if (bme_ok) {
    if (Bme_Read(&bme_last) == 0) {
      bme_valid = 1;
      bme_err = 0;
    } else if (++bme_err >= SENSOR_MAX_ERR) {
      bme_ok = 0;
      bme_valid = 0;
    }
  }
  /* Sem IMU, as linhas saem na cadencia do BME280 com os campos do IMU vazios. */
  if (!imu_ok) {
    write_row(now, NULL);
  }
}

static void service_imu(uint32_t now)
{
  if (!imu_ok) {
    imu_flag = 0;
    return;
  }
  if (!imu_flag && (now - t_imu_poll) < IMU_POLL_MS) {
    return;
  }
  imu_flag = 0;
  t_imu_poll = now;

  uint8_t ready = 0;
  if (Imu_DataReady(&ready) != 0) {
    if (++imu_err >= SENSOR_MAX_ERR) { imu_ok = 0; }
    return;
  }
  if (ready) {
    imu_sample_t s;
    if (Imu_Read(&s) == 0) {
      imu_err = 0;
      t_imu_last = now;
      write_row(now, &s);
    } else if (++imu_err >= SENSOR_MAX_ERR) {
      imu_ok = 0;
    }
  } else if ((now - t_imu_last) > IMU_TIMEOUT_MS) {
    imu_ok = 0;                           /* sensor mudo: sera reinicializado */
  }
}

static void retry_sensors(uint32_t now)
{
  if ((bme_ok && imu_ok) || (now - t_sensor_retry) < RETRY_MS) {
    return;
  }
  t_sensor_retry = now;
  if (!bme_ok && Bme_Init() == 0) { bme_ok = 1; bme_err = 0; }
  if (!imu_ok && Imu_Init() == 0) { imu_ok = 1; imu_err = 0; t_imu_last = now; }
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
void App_Init(void)
{
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);   /* aceso fixo: inicializando */

  Sensors_Setup();
  bme_ok = (Bme_Init() == 0);
  imu_ok = (Imu_Init() == 0);
  t_imu_last = HAL_GetTick();

  state   = APP_NO_CARD;
  t_retry = HAL_GetTick() - RETRY_MS;     /* tenta montar o cartao ja na primeira volta */
  HAL_TIM_Base_Start_IT(&htim3);          /* 25 Hz */
}

void App_Run(void)
{
  uint32_t now = HAL_GetTick();

  led_update(now);

  /* Botao de usuario: encerra a gravacao com seguranca ou volta a gravar. */
  if (btn_flag) {
    btn_flag = 0;
    if (state == APP_RUNNING) {
      Logger_Close();
      state = APP_STOPPED;
    } else if (state != APP_NO_CARD) {
      state = APP_NO_CARD;
      t_retry = now - RETRY_MS;
    }
  }

  /* Sem cartao ou com erro de sistema de arquivos: tenta montar de novo a cada 2 s. */
  if ((state == APP_NO_CARD || state == APP_ERR_FS) && (now - t_retry) >= RETRY_MS) {
    try_open(now);
  }

  service_bme(now);
  service_imu(now);
  retry_sensors(now);

  if (state == APP_RUNNING && (now - t_sync) >= SYNC_MS) {
    t_sync = now;
    log_status_t st = Logger_Sync();
    if (st != LOG_OK) {
      handle_log_error(st, now);
    }
  }
}
