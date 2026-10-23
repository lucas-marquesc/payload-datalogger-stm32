/**
 * logger.h - Gravacao dos dados em arquivos CSV no cartao microSD (FatFS sobre SDIO).
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

/* Uso da chave de detecção do slot (SD_CD, PB12, com pull-up externo R14):
 *   0 = ignora o pino: o firmware sempre tenta montar o cartao (a ausencia aparece como falha
 *       de montagem). Nao depende da polaridade da chave. PADRAO, enquanto a polaridade do
 *       TF-01A nao foi confirmada no datasheet.
 *   1 = pino em nivel BAIXO significa cartao inserido (chave liga o pino ao GND).
 *   2 = pino em nivel ALTO significa cartao inserido. */
#define SD_CD_MODE   0

/* Espaco livre minimo para continuar gravando (em setores de 512 bytes: 2048 = 1 MiB). */
#define LOGGER_MIN_FREE_SECTORS   2048UL

typedef enum {
  LOG_OK = 0,
  LOG_ERR_MOUNT,     /* falha ao inicializar o cartao ou montar o sistema de arquivos */
  LOG_ERR_OPEN,      /* falha ao criar o arquivo */
  LOG_ERR_WRITE,     /* erro de escrita ou de sincronizacao */
  LOG_ERR_FULL       /* cartao cheio */
} log_status_t;

/* 1 se a chave de detecção do slot indica cartao inserido. */
uint8_t Logger_CardPresent(void);

/* Monta o cartao, cria o proximo LOG_NNNN.CSV e escreve o cabecalho. */
log_status_t Logger_Open(void);

/* Acrescenta uma linha (com '\n') ao buffer; grava no cartao quando o buffer enche. */
log_status_t Logger_Write(const char *line, uint16_t len);

/* Grava o buffer pendente, faz f_sync e confere o espaco livre. Chamar a cada 1 s. */
log_status_t Logger_Sync(void);

/* Grava o que falta, fecha o arquivo e desmonta o cartao. Seguro de chamar com o arquivo fechado. */
void Logger_Close(void);

uint8_t Logger_IsOpen(void);

#endif /* LOGGER_H */
