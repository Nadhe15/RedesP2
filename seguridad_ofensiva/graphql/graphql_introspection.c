/*
 * graphql_introspection.c -- API8:2023 Security Misconfiguration
 * (introspeccion abierta)
 *
 * Envia la query de introspeccion estandar de GraphQL. Si el servidor
 * responde con el esquema completo (tipos, campos, mutaciones), un
 * atacante puede mapear toda la API de Consulta sin ninguna
 * documentacion previa. En modo blindado, introspeccion debe estar
 * apagada fuera de desarrollo (o restringida a campos autorizados).
 *
 * Uso: ./graphql_introspection <url_graphql> [token]
 */
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

static const char *INTROSPECCION_QUERY =
"{\"query\":\"query IntrospectionQuery { __schema { queryType { name } "
"mutationType { name } subscriptionType { name } types { kind name "
"fields { name args { name type { name kind } } type { name kind } } } } }\"}";

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <url_graphql> [token]\n", argv[0]);
        return 1;
    }
    const char *url = argv[1];
    const char *token = argc > 2 ? argv[2] : NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
if (curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
}
    buffer_t buf; buffer_init(&buf);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char authhdr[512];
    if (token) {
        snprintf(authhdr, sizeof(authhdr), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, authhdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, INTROSPECCION_QUERY);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    int expone_schema = buf.data && strstr(buf.data, "__schema") != NULL
                         && strstr(buf.data, "\"errors\"") == NULL;
    const char *resultado = expone_schema ? "VULNERABLE" : "bloqueado";

    log_result("Introspeccion/API8", url, status, resultado, "ver salida completa abajo");
    printf("\n--- respuesta completa ---\n%s\n", buf.data ? buf.data : "(vacio)");

    curl_slist_free_all(headers);
    buffer_free(&buf);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
