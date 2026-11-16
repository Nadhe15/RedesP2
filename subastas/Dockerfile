# Preparo la imagen de construccion
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Instalo las herramientas requeridas
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copio el codigo fuente
COPY proto /app/proto
COPY subastas /app/subastas

WORKDIR /app/subastas/build

# Compilo el binario del servidor
RUN cmake .. && make -j$(nproc)

# Preparo la imagen final de ejecucion
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libgrpc++1.51t64 \
    libprotobuf32t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copio los archivos compilados
COPY --from=builder /app/subastas/build/servidor_subastas /app/servidor_subastas
COPY subastas/certs /app/certs

EXPOSE 50051

ENV PORT=50051
ENV SECURITY_MODE=vulnerable
ENV TLS_CERT_PATH=/app/certs/server.crt
ENV TLS_KEY_PATH=/app/certs/server.key

# Inicio el servicio al arrancar
CMD ["/app/servidor_subastas"]
