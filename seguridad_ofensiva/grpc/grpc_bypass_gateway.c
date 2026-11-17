/*
 * grpc_bypass_gateway.c -- API5:2023 Broken Function Level Authorization
 * (metodo interno gRPC llamado sin pasar por el gateway)
 *
 * El enunciado dice que Consulta (GraphQL) y Usuarios (REST) llaman a
 * Subastas por gRPC puertas adentro, y que el token debe validarse "en
 * cada RPC" via metadata. Este programa se conecta DIRECTO al servicio
 * Subastas (saltandose GraphQL/REST) e invoca subastas.v1.Subastas/Pujar
 * sin metadata de autorizacion (o con un token invalido/forjado), para
 * comprobar si el servicio interno confia ciegamente en quien lo llama
 * solo por estar en la red interna.
 *
 * En modo blindado: debe responder UNAUTHENTICATED/PERMISSION_DENIED
 * (grpc-status != 0) aunque la llamada llegue por la red interna.
 *
 * Mensajes del nucleo compartido (seccion 4 del enunciado):
 *   PujarReq { string subasta_id = 1; string usuario_id = 2; int64 monto = 3; }
 *   PujarResp { bool aceptada = 1; string motivo = 2; Subasta subasta = 3; }
 *
 * Uso:
 *   ./grpc_bypass_gateway <host_subastas> <puerto> <subasta_id> <usuario_id> \
 *       <monto_centimos> [token_falso_o_vacio]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grpc_wire.h"
#include "common.h"

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
            "Uso: %s <host_subastas> <puerto> <subasta_id> <usuario_id> "
            "<monto_centimos> [token_falso_o_vacio]\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int puerto = atoi(argv[2]);
    const char *subasta_id = argv[3];
    const char *usuario_id = argv[4];
    long long monto = atoll(argv[5]);
    const char *token = (argc > 6 && argv[6][0]) ? argv[6] : NULL;

    grpc_wire_ctx_t *ctx = grpc_wire_connect(host, puerto);
    if (!ctx) { fprintf(stderr, "no se pudo conectar a %s:%d\n", host, puerto); return 1; }

    /* PujarReq serializado a mano */
    unsigned char req[512];
    unsigned char *p = req;
    p += pb_encode_string(p, 1, subasta_id);
    p += pb_encode_string(p, 2, usuario_id);
    p += pb_encode_int64(p, 3, monto);
    size_t req_len = p - req;

    char authhdr[300];
    const char *auth_a_usar = NULL;
    if (token) {
        snprintf(authhdr, sizeof(authhdr), "Bearer %s", token);
        auth_a_usar = authhdr;
    }

    unsigned char *resp = NULL; size_t resp_len = 0; long grpc_status = -1;
    grpc_wire_unary(ctx, "/subastas.v1.Subastas/Pujar", auth_a_usar,
                     req, req_len, &resp, &resp_len, &grpc_status);

    char objetivo[300];
    snprintf(objetivo, sizeof(objetivo), "%s:%d/subastas.v1.Subastas/Pujar", host, puerto);

    /* PujarResp: campo 1 = aceptada (varint/bool). Basta mirar el primer
       byte del mensaje (tag 0x08 = campo1,wiretype0) para esta prueba. */
    int aceptada = (resp_len >= 2 && resp[0] == 0x08 && resp[1] != 0);

    const char *resultado;
    char detalle[160];
    if (grpc_status != 0) {
        resultado = "bloqueado";
        snprintf(detalle, sizeof(detalle),
                 "grpc-status=%ld (token=%s) -> rechazado, esperado en modo blindado",
                 grpc_status, token ? token : "(ninguno)");
    } else if (aceptada) {
        resultado = "VULNERABLE";
        snprintf(detalle, sizeof(detalle),
                 "Pujar() aceptada llamando DIRECTO a Subastas, sin pasar por el "
                 "gateway, con token=%s", token ? token : "(ninguno)");
    } else {
        resultado = "rechazada-por-reglas-de-negocio";
        snprintf(detalle, sizeof(detalle),
                 "grpc-status=0 pero aceptada=false (ver 'motivo' en la respuesta cruda)");
    }

    log_result("gRPC-BypassGateway/API5", objetivo, 200, resultado, detalle);

    printf("\n--- payload crudo de PujarResp (%zu bytes) ---\n", resp_len);
    hexdump(resp, resp_len);

    free(resp);
    grpc_wire_close(ctx);
    return 0;
}
