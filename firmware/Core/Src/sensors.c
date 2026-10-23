/**
 * sensors.c - Cola entre o HAL do STM32 e os drivers oficiais:
 *   - BME280 Sensor API (Bosch Sensortec), em bme280.c
 *   - lsm6dso-pid (STMicroelectronics), em lsm6dso_reg.c
 */
#include "sensors.h"
#include "main.h"
#include "i2c.h"
#include "spi.h"
#include "bme280.h"
#include "lsm6dso_reg.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Atraso curto em microssegundos (contador de ciclos DWT)             */
/* ------------------------------------------------------------------ */
void Sensors_Setup(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
  if (us >= 1000U) {
    HAL_Delay((us + 999U) / 1000U);
    return;
  }
  uint32_t start  = DWT->CYCCNT;
  uint32_t cycles = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - start) < cycles) { }
}

/* ------------------------------------------------------------------ */
/* BME280 (I2C1, endereco 0x76: SDO no GND)                            */
/* ------------------------------------------------------------------ */
static struct bme280_dev bme_dev;
static uint8_t           bme_addr = BME280_I2C_ADDR_PRIM;

static BME280_INTF_RET_TYPE bme_read(uint8_t reg, uint8_t *data, uint32_t len, void *intf_ptr)
{
  uint8_t addr = *(uint8_t *)intf_ptr;
  return (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                           data, (uint16_t)len, 20) == HAL_OK) ? 0 : -1;
}

static BME280_INTF_RET_TYPE bme_write(uint8_t reg, const uint8_t *data, uint32_t len, void *intf_ptr)
{
  uint8_t addr = *(uint8_t *)intf_ptr;
  return (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                            (uint8_t *)data, (uint16_t)len, 20) == HAL_OK) ? 0 : -1;
}

static void bme_delay_us(uint32_t period, void *intf_ptr)
{
  (void)intf_ptr;
  delay_us(period);
}

int Bme_Init(void)
{
  memset(&bme_dev, 0, sizeof(bme_dev));
  bme_dev.intf     = BME280_I2C_INTF;
  bme_dev.read     = bme_read;
  bme_dev.write    = bme_write;
  bme_dev.delay_us = bme_delay_us;
  bme_dev.intf_ptr = &bme_addr;

  /* bme280_init faz soft reset, confere o ID (0x60) e le os coeficientes de calibracao */
  if (bme280_init(&bme_dev) != BME280_OK) {
    return -1;
  }

  struct bme280_settings st;
  st.osr_h        = BME280_OVERSAMPLING_1X;
  st.osr_p        = BME280_OVERSAMPLING_1X;
  st.osr_t        = BME280_OVERSAMPLING_1X;
  st.filter       = BME280_FILTER_COEFF_OFF;
  st.standby_time = BME280_STANDBY_TIME_0_5_MS;
  if (bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &st, &bme_dev) != BME280_OK) {
    return -2;
  }
  if (bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &bme_dev) != BME280_OK) {
    return -3;
  }
  return 0;
}

int Bme_Read(bme_sample_t *out)
{
  struct bme280_data d;
  if (bme280_get_sensor_data(BME280_ALL, &d, &bme_dev) != BME280_OK) {
    return -1;
  }
  out->temp_c100 = d.temperature;
  out->press_pa  = d.pressure;
  out->hum_q10   = d.humidity;
  return 0;
}

/* ------------------------------------------------------------------ */
/* LSM6DSO (SPI1 modo 3, CS em GPIO)                                   */
/* ------------------------------------------------------------------ */
static stmdev_ctx_t imu_ctx;

static inline void imu_cs(GPIO_PinState s)
{
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, s);
}

static int32_t imu_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef s;
  uint8_t cmd = reg & 0x7FU;              /* bit 7 = 0: escrita */
  imu_cs(GPIO_PIN_RESET);
  s = HAL_SPI_Transmit((SPI_HandleTypeDef *)handle, &cmd, 1, 10);
  if (s == HAL_OK) {
    s = HAL_SPI_Transmit((SPI_HandleTypeDef *)handle, (uint8_t *)buf, len, 10);
  }
  imu_cs(GPIO_PIN_SET);
  return (s == HAL_OK) ? 0 : -1;
}

static int32_t imu_read(void *handle, uint8_t reg, uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef s;
  uint8_t cmd = reg | 0x80U;              /* bit 7 = 1: leitura */
  imu_cs(GPIO_PIN_RESET);
  s = HAL_SPI_Transmit((SPI_HandleTypeDef *)handle, &cmd, 1, 10);
  if (s == HAL_OK) {
    s = HAL_SPI_Receive((SPI_HandleTypeDef *)handle, buf, len, 10);
  }
  imu_cs(GPIO_PIN_SET);
  return (s == HAL_OK) ? 0 : -1;
}

int Imu_Init(void)
{
  int32_t ret = 0;
  uint8_t id = 0, rst = 1;

  imu_ctx.write_reg = imu_write;
  imu_ctx.read_reg  = imu_read;
  imu_ctx.mdelay    = HAL_Delay;
  imu_ctx.handle    = &hspi1;

  imu_cs(GPIO_PIN_SET);
  HAL_Delay(15);                          /* tempo de boot do sensor: 10 ms */

  if (lsm6dso_device_id_get(&imu_ctx, &id) != 0 || id != LSM6DSO_ID) {
    return -1;                            /* WHO_AM_I diferente de 0x6C */
  }

  /* reset por software e espera o bit SW_RESET voltar a zero */
  ret |= lsm6dso_reset_set(&imu_ctx, PROPERTY_ENABLE);
  uint32_t t0 = HAL_GetTick();
  while (rst && (HAL_GetTick() - t0) < 100U) {
    ret |= lsm6dso_reset_get(&imu_ctx, &rst);
  }
  if (rst || ret) {
    return -2;
  }

  ret |= lsm6dso_i3c_disable_set(&imu_ctx, LSM6DSO_I3C_DISABLE);  /* garante a interface SPI */
  ret |= lsm6dso_block_data_update_set(&imu_ctx, PROPERTY_ENABLE);
  ret |= lsm6dso_xl_full_scale_set(&imu_ctx, LSM6DSO_16g);
  ret |= lsm6dso_gy_full_scale_set(&imu_ctx, LSM6DSO_2000dps);
  ret |= lsm6dso_data_ready_mode_set(&imu_ctx, LSM6DSO_DRDY_PULSED);

  lsm6dso_pin_int1_route_t route;
  memset(&route, 0, sizeof(route));
  route.drdy_xl = PROPERTY_ENABLE;
  route.drdy_g  = PROPERTY_ENABLE;
  ret |= lsm6dso_pin_int1_route_set(&imu_ctx, route);

  ret |= lsm6dso_xl_data_rate_set(&imu_ctx, LSM6DSO_XL_ODR_208Hz);
  ret |= lsm6dso_gy_data_rate_set(&imu_ctx, LSM6DSO_GY_ODR_208Hz);
  return ret ? -3 : 0;
}

int Imu_DataReady(uint8_t *ready)
{
  uint8_t xl = 0, gy = 0;
  if (lsm6dso_xl_flag_data_ready_get(&imu_ctx, &xl) != 0) return -1;
  if (lsm6dso_gy_flag_data_ready_get(&imu_ctx, &gy) != 0) return -1;
  *ready = (xl || gy) ? 1U : 0U;
  return 0;
}

int Imu_Read(imu_sample_t *out)
{
  uint8_t b[12];
  /* Leitura em rajada a partir de OUTX_L_G (0x22): giroscopio X,Y,Z e depois acelerometro X,Y,Z */
  if (lsm6dso_read_reg(&imu_ctx, LSM6DSO_OUTX_L_G, b, sizeof(b)) != 0) {
    return -1;
  }
  int16_t gx = (int16_t)((b[1]  << 8) | b[0]);
  int16_t gy = (int16_t)((b[3]  << 8) | b[2]);
  int16_t gz = (int16_t)((b[5]  << 8) | b[4]);
  int16_t ax = (int16_t)((b[7]  << 8) | b[6]);
  int16_t ay = (int16_t)((b[9]  << 8) | b[8]);
  int16_t az = (int16_t)((b[11] << 8) | b[10]);

  /* Sensibilidades do datasheet: +-2000 dps = 70 mdps/LSB; +-16 g = 0,488 mg/LSB */
  out->gx_mdps = (int32_t)gx * 70;
  out->gy_mdps = (int32_t)gy * 70;
  out->gz_mdps = (int32_t)gz * 70;
  out->ax_mg   = ((int32_t)ax * 488) / 1000;
  out->ay_mg   = ((int32_t)ay * 488) / 1000;
  out->az_mg   = ((int32_t)az * 488) / 1000;
  return 0;
}
