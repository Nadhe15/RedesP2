/*
 * grpc_proto_decode.c -- evidencia para "sin TLS: interceptar y decodificar
 * el Protobuf con el .proto" (seccion 5.3 del enunciado).
 *
 * Toma un archivo binario con un mensaje Protobuf crudo -- por ejemplo,
 * los bytes de una llamada gRPC capturada con Wireshark/tcpmp (Follow ->
 * HTTP/2 stream, o exportando el objeto "DATA" de un frame gRPC) -- y lo
 * decodifica de forma GENERICA (sin necesitar el .pb-c.h generado):
 * recorre tag/wiretype/valor y, para cada campo, si es length-delimited,
 * intenta interpretarlo como texto imprimible o como submensaje anidado
 * (recursion), mostrando ademas el hex crudo.
 *
 * Esto es justamente la evidencia de que, sin TLS, el Protobuf "se lee
 * igual que el chat en claro del Proyecto 1": no hace falta ni el .proto
 * oficial para ver la estructura de campos.
 *
 * Uso:
 *   ./grpc_proto_decode <archivo.bin> [--skip-grpc-header]
 *
 * --skip-grpc-header salta los primeros 5 bytes (flag de compresion +
 * longitud BE) que anteponen los frames gRPC sobre HTTP/2, para decodificar
 * directo el mensaje Protobuf que va despues.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

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

static int parece_texto(const unsigned char *buf, size_t len) {
    if (len == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = buf[i];
        if (c < 9 || (c > 13 && c < 32) || c > 126) return 0;
    }
    return 1;
}

static void decodificar(const unsigned char *buf, size_t len, int indent) {
    size_t off = 0;
    while (off < len) {
        size_t inicio_tag = off;
        unsigned long long tag = leer_varint(buf, len, &off);
        if (off == inicio_tag) break; /* no avanzo: datos corruptos, para no ciclar */
        int campo = (int)(tag >> 3);
        int wiretype = (int)(tag & 7);

        printf("%*scampo=%-3d wiretype=%d  ", indent, "", campo, wiretype);

        if (wiretype == 0) {
            unsigned long long v = leer_varint(buf, len, &off);
            printf("varint = %llu\n", v);
        } else if (wiretype == 1) {
            if (off + 8 > len) break;
            printf("fixed64, hex:"); hexdump(buf + off, 8); off += 8;
        } else if (wiretype == 5) {
            if (off + 4 > len) break;
            printf("fixed32, hex:"); hexdump(buf + off, 4); off += 4;
        } else if (wiretype == 2) {
            unsigned long long vlen = leer_varint(buf, len, &off);
            if (off + vlen > len) { printf("(longitud invalida)\n"); break; }
            if (parece_texto(buf + off, (size_t)vlen)) {
                printf("string[%llu] = \"%.*s\"\n", vlen, (int)vlen, buf + off);
            } else {
                printf("bytes[%llu] -> intentando como submensaje anidado:\n", vlen);
                decodificar(buf + off, (size_t)vlen, indent + 4);
            }
            off += vlen;
        } else {
            printf("wiretype desconocido, abortando este nivel\n");
            break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo.bin> [--skip-grpc-header]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(len);
    size_t leidos = fread(buf, 1, (size_t)len, f);
    if (leidos != (size_t)len) {
        perror("fread");
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    size_t offset = 0;
    if (argc > 2 && strcmp(argv[2], "--skip-grpc-header") == 0 && len >= 5) {
        offset = 5;
        printf("(saltando cabecera de frame gRPC de 5 bytes)\n");
    }

    printf("Decodificando %ld bytes de %s:\n\n", len - (long)offset, argv[1]);
    decodificar(buf + offset, (size_t)(len - offset), 0);

    free(buf);
    return 0;
}
