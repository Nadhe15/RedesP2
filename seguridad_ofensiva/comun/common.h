#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

/* Buffer dinamico simple para acumular respuestas de libcurl */
typedef struct {
    char *data;
    size_t size;
} buffer_t;

void buffer_init(buffer_t *b);
void buffer_free(buffer_t *b);
size_t buffer_write_cb(void *contents, size_t size, size_t nmemb, void *userp);

/* Registra un resultado de ataque en stdout y en results.csv (para el informe) */
void log_result(const char *ataque, const char *objetivo, long http_status,
                 const char *resultado, const char *detalle);

/* Vuelca `len` bytes en hexadecimal + ascii, como un hexdump -C simplificado */
void hexdump(const unsigned char *buf, size_t len);

/* Timestamp ISO-8601 en buffer de al menos 32 bytes */
void now_iso8601(char *out, size_t outlen);

#endif
