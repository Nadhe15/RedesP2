/*
 * rest_replay.c -- Replay attack sobre una puja REST/JSON capturada
 *
 * Idea: toma una peticion POST ya capturada (headers + body, por ejemplo
 * con Wireshark/mitmproxy o copiada del devtools del navegador) y la
 * reenvia N veces seguidas. Si el servidor no tiene idempotencia
 * (clave de idempotencia, nonce o ventana de tiempo), la misma puja
 * puede "entrar" mas de una vez.
 *
 * En modo blindado: solo la primera debe devolver 2xx; las repeticiones
 * deben devolver 409/422 (o el mismo recurso ya creado, sin duplicar).
 *
 * Uso:
 *   ./rest_replay <url> <token> <archivo_body.json> <repeticiones>
 *
 * El archivo_body.json es exactamente el body capturado (puede incluir
 * ya un idempotency-key si el equipo lo usa, para probar si de verdad
 * se respeta al reenviarlo identico).
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static char *leer_archivo(const char *path, long *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t leidos = fread(buf, 1, (size_t)len, f);
    if (leidos != (size_t)len) {
        perror("fread");
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    if (len_out) *len_out = len;
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <url> <token> <archivo_body.json> <repeticiones>\n", argv[0]);
        return 1;
    }
    const char *url = argv[1];
    const char *token = argv[2];
    char *body = leer_archivo(argv[3], NULL);
    int repeticiones = atoi(argv[4]);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    int exitosas = 0;
    for (int i = 1; i <= repeticiones; i++) {
        CURL *curl = curl_easy_init();
if (curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
}
        buffer_t buf; buffer_init(&buf);

        struct curl_slist *headers = NULL;
        char authhdr[512];
        snprintf(authhdr, sizeof(authhdr), "Authorization: %s", token);
        headers = curl_slist_append(headers, authhdr);
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        CURLcode res = curl_easy_perform(curl);
        long status = 0;
        if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        int ok = (status >= 200 && status < 300);
        if (ok) exitosas++;
        char etiqueta[64];
        snprintf(etiqueta, sizeof(etiqueta), "Replay intento %d/%d", i, repeticiones);
        log_result("Replay", url, status, ok ? "aceptada" : "rechazada", etiqueta);

        curl_slist_free_all(headers);
        buffer_free(&buf);
        curl_easy_cleanup(curl);
    }

    printf("\nResumen replay: %d/%d repeticiones aceptadas.\n", exitosas, repeticiones);
    if (exitosas > 1)
        printf("VULNERABLE: la misma peticion entro mas de una vez (sin idempotencia).\n");
    else
        printf("Comportamiento esperado en modo blindado: solo 1 aceptada.\n");

    free(body);
    curl_global_cleanup();
    return 0;
}
