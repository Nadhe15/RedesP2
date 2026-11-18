#include "grpc_wire.h"
#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

struct grpc_wire_ctx {
    int fd;
    nghttp2_session *session;
    SSL_CTX *ssl_ctx;
    SSL *ssl;
    int usar_tls;
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
    uint32_t error_cierre;
} stream_state_t;

static void sbuf_append(stream_state_t *s, const unsigned char *d, size_t n) {
    if (s->len + n > s->cap) {
        s->cap = (s->len + n) * 2 + 64;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, d, n);
    s->len += n;
}

static ssize_t transport_write(grpc_wire_ctx_t *ctx,
                               const uint8_t *data,
                               size_t length) {
    if (!ctx->ssl) {
        return send(ctx->fd, data, length, MSG_NOSIGNAL);
    }

    int amount = length > INT_MAX ? INT_MAX : (int)length;
    for (;;) {
        int n = SSL_write(ctx->ssl, data, amount);
        if (n > 0) {
            return n;
        }
        int error = SSL_get_error(ctx->ssl, n);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        return -1;
    }
}

static ssize_t transport_read(grpc_wire_ctx_t *ctx,
                              unsigned char *data,
                              size_t length) {
    if (!ctx->ssl) {
        return recv(ctx->fd, data, length, 0);
    }

    int amount = length > INT_MAX ? INT_MAX : (int)length;
    for (;;) {
        int n = SSL_read(ctx->ssl, data, amount);
        if (n > 0) {
            return n;
        }
        int error = SSL_get_error(ctx->ssl, n);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (error == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        return -1;
    }
}

static ssize_t on_send(nghttp2_session *session, const uint8_t *data,
                        size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    grpc_wire_ctx_t *ctx = user_data;
    ssize_t n = transport_write(ctx, data, length);
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
    (void)user_data;
    stream_state_t *s = nghttp2_session_get_stream_user_data(session, stream_id);
    if (s) {
        s->cerrado = 1;
        s->error_cierre = error_code;
    }
    return 0;
}

static grpc_wire_ctx_t *grpc_wire_connect_impl(const char *host, int puerto,
                                                const char *ca_file) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char puerto_str[16];
    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto);
    if (getaddrinfo(host, puerto_str, &hints, &res) != 0) return NULL;

    int fd = -1;
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return NULL;
    }

    grpc_wire_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        close(fd);
        return NULL;
    }
    ctx->fd = fd;
    ctx->usar_tls = ca_file != NULL;
    snprintf(ctx->host, sizeof(ctx->host), "%s:%d", host, puerto);
    ctx->puerto = puerto;

    if (ctx->usar_tls) {
        static const unsigned char alpn_h2[] = {2, 'h', '2'};
        const char *verify_host = getenv("GRPC_TLS_SERVER_NAME");
        if (!verify_host || !*verify_host) verify_host = host;
        ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx->ssl_ctx ||
            SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_2_VERSION) != 1 ||
            SSL_CTX_load_verify_locations(ctx->ssl_ctx, ca_file, NULL) != 1) {
            grpc_wire_close(ctx);
            return NULL;
        }
        SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER, NULL);
        ctx->ssl = SSL_new(ctx->ssl_ctx);
        struct in_addr ipv4;
        struct in6_addr ipv6;
        const int is_ip = inet_pton(AF_INET, verify_host, &ipv4) == 1 ||
                          inet_pton(AF_INET6, verify_host, &ipv6) == 1;
        const int host_check_ok = ctx->ssl && (is_ip
            ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ctx->ssl), verify_host)
            : (SSL_set_tlsext_host_name(ctx->ssl, verify_host) == 1 &&
               SSL_set1_host(ctx->ssl, verify_host) == 1));
        if (!ctx->ssl ||
            SSL_set_fd(ctx->ssl, fd) != 1 ||
            !host_check_ok ||
            SSL_set_alpn_protos(ctx->ssl, alpn_h2, sizeof(alpn_h2)) != 0 ||
            SSL_connect(ctx->ssl) != 1) {
            ERR_print_errors_fp(stderr);
            grpc_wire_close(ctx);
            return NULL;
        }

        const unsigned char *selected_alpn = NULL;
        unsigned int selected_alpn_len = 0;
        SSL_get0_alpn_selected(ctx->ssl, &selected_alpn, &selected_alpn_len);
        if (selected_alpn_len != 2 ||
            memcmp(selected_alpn, "h2", 2) != 0) {
            grpc_wire_close(ctx);
            return NULL;
        }
    }

    nghttp2_session_callbacks *cbs;
    if (nghttp2_session_callbacks_new(&cbs) != 0) {
        grpc_wire_close(ctx);
        return NULL;
    }
    nghttp2_session_callbacks_set_send_callback(cbs, on_send);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk);
    nghttp2_session_callbacks_set_on_header_callback(cbs, on_header);
    nghttp2_session_callbacks_set_on_stream_close_callback(cbs, on_stream_close);

    int result = nghttp2_session_client_new(&ctx->session, cbs, ctx);
    nghttp2_session_callbacks_del(cbs);
    if (result != 0) {
        grpc_wire_close(ctx);
        return NULL;
    }

    nghttp2_submit_settings(ctx->session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (nghttp2_session_send(ctx->session) != 0) {
        grpc_wire_close(ctx);
        return NULL;
    }
    return ctx;
}

grpc_wire_ctx_t *grpc_wire_connect(const char *host, int puerto) {
    return grpc_wire_connect_impl(host, puerto, NULL);
}

grpc_wire_ctx_t *grpc_wire_connect_tls(const char *host, int puerto,
                                       const char *ca_file) {
    if (!ca_file || !*ca_file) return NULL;
    return grpc_wire_connect_impl(host, puerto, ca_file);
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
    if (!ctx || !path || !req || !resp || !resp_len) return -1;
    *resp = NULL;
    *resp_len = 0;
    if (grpc_status_out) *grpc_status_out = -1;
    if (req_len > UINT32_MAX) return -1;
    int transporte_ok = 0;

    unsigned char *frame = malloc(5 + req_len);
    if (!frame) return -1;
    frame[0] = 0;
    frame[1] = (req_len >> 24) & 0xff;
    frame[2] = (req_len >> 16) & 0xff;
    frame[3] = (req_len >> 8) & 0xff;
    frame[4] = req_len & 0xff;
    memcpy(frame + 5, req, req_len);

    data_src_t *src = malloc(sizeof(*src));
    if (!src) {
        free(frame);
        return -1;
    }
    src->buf = frame; src->len = 5 + req_len; src->sent = 0;
    nghttp2_data_provider prd = { .source.ptr = src, .read_callback = on_read_data };

    char authority[300];
    snprintf(authority, sizeof(authority), "%s", ctx->host);

    nghttp2_nv nva[6];
    int i = 0;
    #define NV(n, v) { .name = (uint8_t *)(n), .value = (uint8_t *)(v), \
        .namelen = strlen(n), .valuelen = strlen(v), .flags = NGHTTP2_NV_FLAG_NONE }
    nva[i++] = (nghttp2_nv) NV(":method", "POST");
    nva[i++] = (nghttp2_nv) NV(":scheme", ctx->usar_tls ? "https" : "http");
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
        ssize_t n = transport_read(ctx, rbuf, sizeof(rbuf));
        if (n <= 0) break;
        if (nghttp2_session_mem_recv(ctx->session, rbuf, (size_t)n) < 0) break;
    }
    transporte_ok = st.cerrado && st.error_cierre == NGHTTP2_NO_ERROR;
fin:
    free(frame);
    free(src);

    if (!transporte_ok) {
        free(st.buf);
        if (grpc_status_out) {
            *grpc_status_out = -1;
        }
        return -1;
    }

    if (st.len > 0) {
        if (st.len < 5 || st.buf[0] != 0) {
            free(st.buf);
            if (grpc_status_out) {
                *grpc_status_out = -1;
            }
            return -1;
        }

        uint32_t msg_len =
            ((uint32_t)st.buf[1] << 24) |
            ((uint32_t)st.buf[2] << 16) |
            ((uint32_t)st.buf[3] << 8)  |
            (uint32_t)st.buf[4];

        if ((size_t)msg_len != st.len - 5) {
            free(st.buf);
            if (grpc_status_out) {
                *grpc_status_out = -1;
            }
            return -1;
        }

        *resp = malloc(msg_len ? msg_len : 1);
        if (!*resp) {
            free(st.buf);
            return -1;
        }

        memcpy(*resp, st.buf + 5, msg_len);
        *resp_len = msg_len;
    }

    free(st.buf);

    if (grpc_status_out) {
        *grpc_status_out = st.status_visto ? st.grpc_status : -1;
    }
    return 0;

}

void grpc_wire_close(grpc_wire_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->session) nghttp2_session_del(ctx->session);
    if (ctx->ssl) {
        SSL_shutdown(ctx->ssl);
        SSL_free(ctx->ssl);
    }
    if (ctx->ssl_ctx) SSL_CTX_free(ctx->ssl_ctx);
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