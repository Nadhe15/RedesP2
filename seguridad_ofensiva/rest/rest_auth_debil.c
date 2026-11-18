/*
 * rest_auth_debil.c -- API2:2023 Broken Authentication
 *
 * Tres pruebas en un solo binario:
 *   1. ping-sin-token: llama un endpoint protegido sin Authorization.
 *   2. adivinar-token: si los tokens son secuenciales/predecibles (ej.
 *      enteros o timestamps codificados), se prueba un rango pequeño.
 *   3. token-expirado: reutiliza un token ya vencido para ver si el
 *      servidor igual lo acepta (falta de validacion de expiracion).
 *
 * En modo blindado: (1) y (3) deben devolver 401, y (2) no debe tener
 * ningun acierto porque los tokens son aleatorios de verdad.
 *
 * Uso:
 *   ./rest_auth_debil sin-token   <url_protegida>
 *   ./rest_auth_debil adivinar    <url_protegida> <prefijo_token> <desde> <hasta>
 *   ./rest_auth_debil expirado    <url_protegida> <token_viejo>
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static long hacer_get(const char *url, const char *token, buffer_t *buf) {
    CURL *curl = curl_easy_init();
if (curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
}
    struct curl_slist *headers = NULL;
    char authhdr[512];
    if (token) {
        snprintf(authhdr, sizeof(authhdr), "Authorization: %s", token);
        headers = curl_slist_append(headers, authhdr);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso:\n"
            "  %s sin-token <url>\n"
            "  %s adivinar  <url> <prefijo_token> <desde> <hasta>\n"
            "  %s expirado  <url> <token_viejo>\n", argv[0], argv[0], argv[0]);
        return 1;
    }
    const char *modo = argv[1];
    const char *url = argv[2];
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (strcmp(modo, "sin-token") == 0) {
        buffer_t buf; buffer_init(&buf);
        long status = hacer_get(url, NULL, &buf);
        const char *r = (status >= 200 && status < 300) ? "VULNERABLE" :
                         (status == 401) ? "bloqueado" : "?";
        log_result("AuthDebil/API2:sin-token", url, status, r, buf.data);
        buffer_free(&buf);

    } else if (strcmp(modo, "adivinar") == 0) {
        if (argc != 6) { fprintf(stderr, "faltan argumentos para 'adivinar'\n"); return 1; }
        const char *prefijo = argv[3];
        long desde = atol(argv[4]), hasta = atol(argv[5]);
        int aciertos = 0;
        for (long i = desde; i <= hasta; i++) {
            char token[256];
            snprintf(token, sizeof(token), "Bearer %s%ld", prefijo, i);
            buffer_t buf; buffer_init(&buf);
            long status = hacer_get(url, token, &buf);
            if (status >= 200 && status < 300) {
                log_result("AuthDebil/API2:adivinar", url, status, "VULNERABLE", token);
                aciertos++;
            }
            buffer_free(&buf);
        }
        printf("\nTokens adivinados con exito: %d / %ld intentados.\n",
               aciertos, hasta - desde + 1);
        if (aciertos == 0)
            printf("(esperado en modo blindado: tokens aleatorios de verdad)\n");

    } else if (strcmp(modo, "expirado") == 0) {
        if (argc != 4) { fprintf(stderr, "falta el token viejo\n"); return 1; }
        char token[512];
        snprintf(token, sizeof(token), "Bearer %s", argv[3]);
        buffer_t buf; buffer_init(&buf);
        long status = hacer_get(url, token, &buf);
        const char *r = (status >= 200 && status < 300) ? "VULNERABLE" :
                         (status == 401) ? "bloqueado" : "?";
        log_result("AuthDebil/API2:expirado", url, status, r, buf.data);
        buffer_free(&buf);
    } else {
        fprintf(stderr, "modo desconocido: %s\n", modo);
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
