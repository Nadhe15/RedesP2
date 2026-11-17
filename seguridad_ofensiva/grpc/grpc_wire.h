#ifndef GRPC_WIRE_H
#define GRPC_WIRE_H

#include <stddef.h>

/*
 * Cliente gRPC minimalista en C puro sobre HTTP/2 en texto plano
 * ("h2c", sin TLS) usando libnghttp2 para el framing HTTP/2 y framing
 * manual de gRPC (1 byte compresion + 4 bytes longitud BE + protobuf).
 *
 * Deliberadamente NO implementa TLS: eso es justamente lo que permite
 * capturar y decodificar el trafico con Wireshark (seccion 5.3, fila
 * "sin TLS" del enunciado). Sirve de base para grpc_reflection.c y
 * grpc_bypass_gateway.c.
 */

typedef struct grpc_wire_ctx grpc_wire_ctx_t;

/* Conecta (TCP plano) a host:puerto y hace el handshake HTTP/2 (preface). */
grpc_wire_ctx_t *grpc_wire_connect(const char *host, int puerto);

/*
 * Llamada unaria: abre un stream HTTP/2 nuevo hacia `path`
 * (ej. "/subastas.v1.Subastas/Pujar"), envia el mensaje protobuf ya
 * serializado en req/req_len como un unico frame gRPC, cierra el lado
 * de escritura (END_STREAM) y espera la respuesta.
 *
 * `metadata_auth` puede ser NULL o un valor para la cabecera
 * "authorization" (equivalente al metadata de gRPC entre servicios).
 *
 * Devuelve el payload del mensaje de respuesta (sin el prefijo de 5
 * bytes) en *resp (debes hacer free) y su longitud en *resp_len.
 * Devuelve 0 en exito, -1 en error de transporte.
 * grpc_status_out recibe el valor de la cabecera/trailer grpc-status
 * (0 = OK; distinto de 0 = rechazado, p.ej. UNAUTHENTICATED=16,
 * PERMISSION_DENIED=7, INVALID_ARGUMENT=3).
 */
int grpc_wire_unary(grpc_wire_ctx_t *ctx, const char *path,
                     const char *metadata_auth,
                     const unsigned char *req, size_t req_len,
                     unsigned char **resp, size_t *resp_len,
                     long *grpc_status_out);

void grpc_wire_close(grpc_wire_ctx_t *ctx);

/* --- utilidades minimas de protobuf wire-format (varint / length-delimited) --- */
size_t pb_encode_varint(unsigned char *buf, unsigned long long v);
size_t pb_encode_tag(unsigned char *buf, int campo, int wiretype);
size_t pb_encode_string(unsigned char *buf, int campo, const char *s);
size_t pb_encode_int64(unsigned char *buf, int campo, long long v);
size_t pb_encode_bool(unsigned char *buf, int campo, int v);

#endif
