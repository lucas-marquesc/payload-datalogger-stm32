/**
 * sensors.h - Camada de acesso aos sensores (BME280 por I2C1 e LSM6DSO por SPI1).
 * Todas as funcoes de inicializacao e leitura retornam 0 em caso de sucesso.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

/* Amostra do BME280 ja compensada, em unidades inteiras (sem ponto flutuante). */
typedef struct {
  int32_t  temp_c100;   /* temperatura em centesimos de grau Celsius (2412 = 24,12 C) */
  uint32_t press_pa;    /* pressao em pascal (91184 = 911,84 hPa) */
  uint32_t hum_q10;     /* umidade em %RH no formato Q22.10 (dividir por 1024) */
} bme_sample_t;

/* Amostra do LSM6DSO em unidades inteiras. */
typedef struct {
  int32_t ax_mg, ay_mg, az_mg;        /* aceleracao em milesimos de g */
  int32_t gx_mdps, gy_mdps, gz_mdps;  /* velocidade angular em milidegraus por segundo */
} imu_sample_t;

/* Habilita o contador de ciclos usado nos atrasos curtos. Chamar uma vez. */
void Sensors_Setup(void);

/* BME280 */
int Bme_Init(void);                 /* confere o ID (0x60), configura e inicia o modo normal */
int Bme_Read(bme_sample_t *out);

/* LSM6DSO */
int Imu_Init(void);                 /* confere o WHO_AM_I (0x6C), 208 Hz, +-16 g, +-2000 dps, INT1 data-ready */
int Imu_DataReady(uint8_t *ready);  /* 1 se ha amostra nova de acelerometro ou giroscopio */
int Imu_Read(imu_sample_t *out);

#endif /* SENSORS_H */
