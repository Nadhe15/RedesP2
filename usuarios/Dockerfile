FROM debian:bookworm-slim AS builder

# instalo compilador y librerias header-only para REST, JSON y OpenSSL
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    make \
    libcpp-httplib-dev \
    nlohmann-json3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY Makefile .
COPY src/ ./src/

RUN make

FROM debian:bookworm-slim

# ca-certificates y libssl3 para ejecutar el binario compilado
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/bin/usuarios /app/usuarios

ENV PUERTO=8080
ENV MODO_VULNERABLE=false
ENV SUBASTAS_HOST=subastas:50051

EXPOSE 8080

CMD ["/app/usuarios"]
