#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void buffer_init(buffer_t *b) {
    b->data = malloc(1);
    b->data[0] = '\0';
    b->size = 0;
}

void buffer_free(buffer_t *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

size_t buffer_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    buffer_t *b = (buffer_t *)userp;
    char *ptr = realloc(b->data, b->size + realsize + 1);
    if (!ptr) return 0; /* sin memoria: aborta la transferencia */
    b->data = ptr;
    memcpy(&(b->data[b->size]), contents, realsize);
    b->size += realsize;
    b->data[b->size] = '\0';
    return realsize;
}

void now_iso8601(char *out, size_t outlen) {
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, outlen, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

void log_result(const char *ataque, const char *objetivo, long http_status,
                 const char *resultado, const char *detalle) {
    char ts[32];
    now_iso8601(ts, sizeof(ts));

    fprintf(stdout, "[%s] %-28s objetivo=%-40s status=%-5ld %-9s %s\n",
            ts, ataque, objetivo, http_status, resultado, detalle ? detalle : "");

    /* Evidencia acumulada para el informe de seguridad (seccion 8 del enunciado) */
    FILE *f = fopen("results.csv", "a");
    if (!f) return;
    long pos = ftell(f);
    if (pos == 0) {
        fprintf(f, "timestamp,ataque,objetivo,http_status,resultado,detalle\n");
    }
    fprintf(f, "%s,%s,%s,%ld,%s,\"%s\"\n", ts, ataque, objetivo, http_status,
            resultado, detalle ? detalle : "");
    fclose(f);
}

void hexdump(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i += 16) {
        printf("%08zx  ", i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) printf("%02x ", buf[i + j]);
            else printf("   ");
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            unsigned char c = buf[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}
