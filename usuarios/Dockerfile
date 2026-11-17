FROM debian:trixie-slim AS builder

# instalo compilador y librerias para REST, JSON y OpenSSL
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    make \
    libcpp-httplib-dev \
    nlohmann-json3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY usuarios/Makefile ./Makefile
COPY usuarios/src/ ./src/

RUN make

FROM debian:trixie-slim

# ca-certificates, openssl y librerias de ejecucion para el binario compilado
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    openssl \
    libcpp-httplib0.18 \
    libssl3t64 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/bin/usuarios /app/usuarios

# genero certificados autofirmados por defecto si el compose no monta unos propios
RUN mkdir -p /usuarios/certs && \
    openssl req -x509 -newkey rsa:2048 -nodes \
      -keyout /app/certs/usuarios.key \
      -out /app/certs/usuarios.crt \
      -days 30 \
      -subj "/CN=usuarios" \
      -addext "subjectAltName=DNS:usuarios,DNS:localhost,IP:127.0.0.1"

ENV PUERTO=8080
ENV MODO_VULNERABLE=false
ENV TLS_CERT_PATH=/run/tls/usuarios.crt
ENV TLS_KEY_PATH=/run/tls/usuarios.key
ENV SUBASTAS_HOST=subastas:50051

EXPOSE 8080

CMD ["/app/usuarios"]
