#include "grpc_wire.h"
#include <nghttp2/nghttp2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define GRPC_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

struct grpc_wire_ctx {
    int fd;
    nghttp2_session *session;
    char host[256];
    int puerto;
};

/* estado de UN stream/llamada en curso */
typedef struct {
    unsigned char *buf;
    size_t len, cap;
    long grpc_status;
    int status_visto;
    int cerrado;
} stream_state_t;

static void sbuf_append(stream_state_t *s, const unsigned char *d, size_t n) {
    if (s->len + n > s->cap) {
        s->cap = (s->len + n) * 2 + 64;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, d, n);
    s->len += n;
}

static ssize_t on_send(nghttp2_session *session, const uint8_t *data,
                        size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    grpc_wire_ctx_t *ctx = user_data;
    ssize_t n = send(ctx->fd, data, length, 0);
    if (n < 0) return NGHTTP2_ERR_CALLBACK_FAILURE;
    return n;
}

static int on_data_chunk(nghttp2_session *session, uint8_t flags,
                          int32_t stream_id, const uint8_t *data, size_t len,
                          void *user_data) {
    (void)flags; (void)user_data;
    stream_state_t *s = nghttp2_session_get_stream_user_data(session, stream_id);
    if (s) sbuf_append(s, data, len);
    return 0;
}

static int on_header(nghttp2_session *session, const nghttp2_frame *frame,
                      const uint8_t *name, size_t namelen,
                      const uint8_t *value, size_t valuelen,
                      uint8_t flags, void *user_data) {
    (void)user_data; (void)flags;
    if (namelen == 11 && memcmp(name, "grpc-status", 11) == 0) {
        stream_state_t *s = nghttp2_session_get_stream_user_data(
            session, frame->hd.stream_id);
        if (s) {
            char tmp[16] = {0};
            size_t n = valuelen < sizeof(tmp) - 1 ? valuelen : sizeof(tmp) - 1;
            memcpy(tmp, value, n);
            s->grpc_status = atol(tmp);
            s->status_visto = 1;
        }
    }
    return 0;
}

static int on_stream_close(nghttp2_session *session, int32_t stream_id,
                            uint32_t error_code, void *user_data) {
    (void)error_code; (void)user_data;
    stream_state_t *s = nghttp2_session_get_stream_user_data(session, stream_id);
    if (s) s->cerrado = 1;
    return 0;
}

grpc_wire_ctx_t *grpc_wire_connect(const char *host, int puerto) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char puerto_str[16];
    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto);
    if (getaddrinfo(host, puerto_str, &hints, &res) != 0) return NULL;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        if (fd >= 0) close(fd);
        return NULL;
    }
    freeaddrinfo(res);

    grpc_wire_ctx_t *ctx = calloc(1, sizeof(*ctx));
    ctx->fd = fd;
    snprintf(ctx->host, sizeof(ctx->host), "%s:%d", host, puerto);
    ctx->puerto = puerto;

    /* preface h2c en texto plano: aqui es donde, sin TLS, todo viaja claro */
    send(fd, GRPC_PREFACE, strlen(GRPC_PREFACE), 0);

    nghttp2_session_callbacks *cbs;
    nghttp2_session_callbacks_new(&cbs);
    nghttp2_session_callbacks_set_send_callback(cbs, on_send);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close);

    nghttp2_session_client_new(&ctx->session, cbs, ctx);
    nghttp2_session_callbacks_del(cbs);

    nghttp2_submit_settings(ctx->session, NGHTTP2_FLAG_NONE, NULL, 0);
    nghttp2_session_send(ctx->session);
    return ctx;
}

/* data_provider: entrega el frame gRPC (header 5 bytes + payload) ya armado */
typedef struct { const unsigned char *buf; size_t len, sent; } data_src_t;

static ssize_t on_read_data(nghttp2_session *session, int32_t stream_id,
                             uint8_t *buf, size_t length,
                             uint32_t *data_flags, nghttp2_data_source *source,
                             void *user_data) {
    (void)session; (void)stream_id; (void)user_data;
    data_src_t *src = source->ptr;
    size_t restante = src->len - src->sent;
    size_t n = restante < length ? restante : length;
    memcpy(buf, src->buf + src->sent, n);
    src->sent += n;
    if (src->sent == src->len) *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return (ssize_t)n;
}

int grpc_wire_unary(grpc_wire_ctx_t *ctx, const char *path,
                     const char *metadata_auth,
                     const unsigned char *req, size_t req_len,
                     unsigned char **resp, size_t *resp_len,
                     long *grpc_status_out) {
    /* arma el frame gRPC: 1 byte flag compresion(0) + 4 bytes BE longitud + payload */
    unsigned char *frame = malloc(5 + req_len);
    frame[0] = 0;
    frame[1] = (req_len >> 24) & 0xff;
    frame[2] = (req_len >> 16) & 0xff;
    frame[3] = (req_len >> 8) & 0xff;
    frame[4] = req_len & 0xff;
    memcpy(frame + 5, req, req_len);

    data_src_t *src = malloc(sizeof(*src));
    src->buf = frame; src->len = 5 + req_len; src->sent = 0;
    nghttp2_data_provider prd = { .source.ptr = src, .read_callback = on_read_data };

    char authority[300];
    snprintf(authority, sizeof(authority), "%s", ctx->host);

    nghttp2_nv nva[6];
    int i = 0;
    #define NV(n, v) { .name = (uint8_t *)(n), .value = (uint8_t *)(v), \
        .namelen = strlen(n), .valuelen = strlen(v), .flags = NGHTTP2_NV_FLAG_NONE }
    nva[i++] = (nghttp2_nv) NV(":method", "POST");
    nva[i++] = (nghttp2_nv) NV(":scheme", "http");
    nva[i++] = (nghttp2_nv) NV(":authority", authority);
    nva[i++] = (nghttp2_nv) NV(":path", path);
    nva[i++] = (nghttp2_nv) NV("content-type", "application/grpc+proto");
    #undef NV
    nghttp2_nv nv_auth;
    if (metadata_auth) {
        nv_auth = (nghttp2_nv){ .name = (uint8_t *)"authorization",
            .value = (uint8_t *)metadata_auth, .namelen = 13,
            .valuelen = strlen(metadata_auth), .flags = NGHTTP2_NV_FLAG_NONE };
        nva[i++] = nv_auth;
    }

    stream_state_t st = {0};
    int32_t stream_id = nghttp2_submit_request(ctx->session, NULL, nva, i, &prd, &st);
    if (stream_id < 0) { free(frame); free(src); return -1; }
    nghttp2_session_set_stream_user_data(ctx->session, stream_id, &st);

    /* bucle de E/S bloqueante hasta que el stream cierre */
    unsigned char rbuf[16384];
    while (!st.cerrado) {
        while (nghttp2_session_want_write(ctx->session))
            if (nghttp2_session_send(ctx->session) != 0) goto fin;
        if (!nghttp2_session_want_read(ctx->session)) break;
        ssize_t n = recv(ctx->fd, rbuf, sizeof(rbuf), 0);
        if (n <= 0) break;
        if (nghttp2_session_mem_recv(ctx->session, rbuf, (size_t)n) < 0) break;
    }
fin:
    free(frame);
    free(src);

    *resp = st.buf;
    *resp_len = st.len;
    if (grpc_status_out) *grpc_status_out = st.status_visto ? st.grpc_status : -1;
    return 0;
}

void grpc_wire_close(grpc_wire_ctx_t *ctx) {
    if (!ctx) return;
    nghttp2_session_del(ctx->session);
    close(ctx->fd);
    free(ctx);
}

/* --- protobuf wire-format minimo (sin depender de protoc/protobuf-c) --- */

size_t pb_encode_varint(unsigned char *buf, unsigned long long v) {
    size_t n = 0;
    do {
        unsigned char b = v & 0x7f;
        v >>= 7;
        if (v) b |= 0x80;
        buf[n++] = b;
    } while (v);
    return n;
}

size_t pb_encode_tag(unsigned char *buf, int campo, int wiretype) {
    return pb_encode_varint(buf, ((unsigned long long)campo << 3) | wiretype);
}

size_t pb_encode_string(unsigned char *buf, int campo, const char *s) {
    size_t n = pb_encode_tag(buf, campo, 2); /* wiretype 2 = length-delimited */
    size_t slen = strlen(s);
    n += pb_encode_varint(buf + n, slen);
    memcpy(buf + n, s, slen);
    return n + slen;
}

size_t pb_encode_int64(unsigned char *buf, int campo, long long v) {
    size_t n = pb_encode_tag(buf, campo, 0); /* wiretype 0 = varint */
    return n + pb_encode_varint(buf + n, (unsigned long long)v);
}

size_t pb_encode_bool(unsigned char *buf, int campo, int v) {
    return pb_encode_int64(buf, campo, v ? 1 : 0);
}