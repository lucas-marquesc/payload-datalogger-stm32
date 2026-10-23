/* Teste da compensação do BME280 no PC (sem hardware).
 * Inclui o próprio bme280.c (rotinas static) e alimenta com coeficientes de calibração
 * fictícios (os do exemplo do datasheet para T e P; H típico) e leituras brutas fixas.
 * Saída: T em 0,01 °C, P em Pa, H em Q22.10 (%RH * 1024). Compare com test_bme280_ref.py. */
#include <stdio.h>
#include "bme280.c"   /* compilar com -I ../firmware/Core/Src -I ../firmware/Core/Inc */

int main(void)
{
    struct bme280_calib_data c = {0};
    c.dig_t1 = 27504; c.dig_t2 = 26435; c.dig_t3 = -1000;
    c.dig_p1 = 36477; c.dig_p2 = -10685; c.dig_p3 = 3024; c.dig_p4 = 2855; c.dig_p5 = 140;
    c.dig_p6 = -7;    c.dig_p7 = 15500;  c.dig_p8 = -14600; c.dig_p9 = 6000;
    c.dig_h1 = 75; c.dig_h2 = 366; c.dig_h3 = 0; c.dig_h4 = 317; c.dig_h5 = 50; c.dig_h6 = 30;

    const int32_t adc_t[] = {519888, 500000, 540000};
    const int32_t adc_p[] = {415148, 400000, 430000};
    const int32_t adc_h[] = {30000, 26000, 36000};
    for (int i = 0; i < 3; i++) {
        struct bme280_uncomp_data u = {0};
        u.temperature = (uint32_t)adc_t[i]; u.pressure = (uint32_t)adc_p[i]; u.humidity = (uint32_t)adc_h[i];
        int32_t t = compensate_temperature(&u, &c);     /* preenche c.t_fine */
        uint32_t p = compensate_pressure(&u, &c);
        uint32_t h = compensate_humidity(&u, &c);
        printf("%d %ld %lu %lu\n", i, (long)t, (unsigned long)p, (unsigned long)h);
    }
    return 0;
}
