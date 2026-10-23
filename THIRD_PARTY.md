# Código de terceiros

| Componente | Origem | Licença | Onde |
|---|---|---|---|
| BME280 Sensor API | Bosch Sensortec, https://github.com/boschsensortec/BME280_SensorAPI | BSD-3-Clause | `firmware/Core/Src/bme280.c`, `firmware/Core/Inc/bme280*.h`. Alteração: `#define BME280_32BIT_ENABLE` em `bme280_defs.h` |
| lsm6dso-pid | STMicroelectronics, https://github.com/STMicroelectronics/lsm6dso-pid | BSD-3-Clause | `firmware/Core/Src/lsm6dso_reg.c`, `firmware/Core/Inc/lsm6dso_reg.h` |
| STM32CubeF4 HAL, CMSIS e FatFS | STMicroelectronics (gerados pelo STM32CubeMX) | BSD-3-Clause (HAL, CMSIS Device) e licença do FatFS (ChaN) | `firmware/Drivers`, `firmware/Middlewares`, `firmware/FATFS` |
