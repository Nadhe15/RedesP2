/*
 * grpc_reflection.c -- API9:2023 Improper Inventory Management
 * (gRPC Server Reflection abierta)
 *
 * Llama a grpc.reflection.v1alpha.ServerReflection/ServerReflectionInfo
 * con un ServerReflectionRequest{ list_services = "" } y, sin conocer
 * ningun .proto del equipo, enumera los servicios gRPC que el servidor
 * expone. En modo blindado la reflexion debe estar apagada fuera de
 * desarrollo (el servidor debe cerrar el stream con UNIMPLEMENTED).
 *
 * Mensajes (grpc.reflection.v1alpha, bien conocidos y estables):
 *   ServerReflectionRequest { string host = 1; oneof { ... string
 *       list_services = 7; ... } }
 *   ServerReflectionResponse { ... oneof { ...
 *       ListServiceResponse list_services_response = 6; ... } }
 *   ListServiceResponse { repeated ServiceResponse service = 1; }
 *   ServiceResponse { string name = 1; }
 *
 * Uso: ./grpc_reflection <host> <puerto>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grpc_wire.h"
#include "common.h"

/* lee un varint desde buf[*off..], avanza *off, devuelve el valor */
static unsigned long long leer_varint(const unsigned char *buf, size_t len, size_t *off) {
    unsigned long long v = 0;
    int shift = 0;
    while (*off < len) {
        unsigned char b = buf[(*off)++];
        v |= (unsigned long long)(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return v;
}

/* recorre un mensaje protobuf plano y, cuando encuentra el campo buscado
   con wiretype length-delimited, invoca el callback con su contenido */
typedef void (*campo_cb)(int campo, const unsigned char *val, size_t vlen, void *ud);

static void recorrer_mensaje(const unsigned char *buf, size_t len, campo_cb cb, void *ud) {
    size_t off = 0;
    while (off < len) {
        unsigned long long tag = leer_varint(buf, len, &off);
        int campo = (int)(tag >> 3);
        int wiretype = (int)(tag & 7);
        if (wiretype == 0) {              /* varint */
            leer_varint(buf, len, &off);
        } else if (wiretype == 2) {       /* length-delimited */
            unsigned long long vlen = leer_varint(buf, len, &off);
            if (off + vlen > len) break;
            cb(campo, buf + off, (size_t)vlen, ud);
            off += vlen;
        } else if (wiretype == 5) {       /* 32-bit */
            off += 4;
        } else if (wiretype == 1) {       /* 64-bit */
            off += 8;
        } else {
            break; /* wiretype no soportado por este parser minimalista */
        }
    }
}

static void imprimir_servicio(int campo, const unsigned char *val, size_t vlen, void *ud) {
    (void)ud;
    if (campo != 1) return; /* ServiceResponse.name = 1 */
    printf("  - %.*s\n", (int)vlen, val);
}

static void buscar_list_services(int campo, const unsigned char *val, size_t vlen, void *ud) {
    int *encontrado = ud;
    if (campo != 6) return; /* ServerReflectionResponse.list_services_response = 6 */
    *encontrado = 1;
    /* dentro de ListServiceResponse, cada campo 1 es un ServiceResponse */
    recorrer_mensaje(val, vlen, imprimir_servicio, NULL);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <host> <puerto>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int puerto = atoi(argv[2]);

    const char *ca_path = getenv("GRPC_CA_PATH");
    grpc_wire_ctx_t *ctx = ca_path && ca_path[0]
        ? grpc_wire_connect_tls(host, puerto, ca_path)
        : grpc_wire_connect(host, puerto);
    if (!ctx) { fprintf(stderr, "no se pudo conectar a %s:%d\n", host, puerto); return 1; }

    /* ServerReflectionRequest { list_services: "" }  (host se deja vacio) */
    unsigned char req[16];
    size_t req_len = pb_encode_string(req, 7, "");

    unsigned char *resp = NULL; size_t resp_len = 0; long grpc_status = -1;
    int llamada_ok = grpc_wire_unary(
        ctx, "/grpc.reflection.v1alpha.ServerReflection/ServerReflectionInfo",
        NULL, req, req_len, &resp, &resp_len, &grpc_status);

    char objetivo[300];
    snprintf(objetivo, sizeof(objetivo), "%s:%d", host, puerto);

    if (llamada_ok != 0) {
        log_result("gRPC-Reflection/API9", objetivo, 0, "error-transporte",
                   "fallo TLS/HTTP2; no se puede concluir si reflection esta habilitada");
    } else if (grpc_status == 0 && resp_len > 0) {
        printf("Servicios expuestos via reflection en %s:\n", objetivo);
        int encontrado = 0;
        recorrer_mensaje(resp, resp_len, buscar_list_services, &encontrado);
        log_result("gRPC-Reflection/API9", objetivo, 200,
                    encontrado ? "VULNERABLE" : "respuesta-sin-servicios",
                    "reflection respondio grpc-status=0");
    } else if (grpc_status > 0) {
        log_result("gRPC-Reflection/API9", objetivo, 200, "bloqueado",
                    "reflection respondio con grpc-status distinto de cero");
    } else {
        log_result("gRPC-Reflection/API9", objetivo, 0, "respuesta-grpc-invalida",
                    "la llamada termino sin recibir grpc-status");
    }

    free(resp);
    grpc_wire_close(ctx);
    return 0;
}
