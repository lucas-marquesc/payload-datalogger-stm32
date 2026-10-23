/**
 * app.h - Aplicacao do datalogger: aquisicao, gravacao em CSV, LED de status e tratamento de erros.
 */
#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

void App_Init(void);   /* chamar uma vez, depois dos MX_..._Init (USER CODE 2 do main.c) */
void App_Run(void);    /* chamar dentro do while(1) (USER CODE 3 do main.c) */

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
