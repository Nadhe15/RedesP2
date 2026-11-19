# IMPORTANTE LEERME 

cada que quieran alterar entre modo vulnerable y blindado existen dos varuables bien importantes dentro del .env.example:m

# Protegido: SECURITY_MODE=blindado
# Vulnerable: SECURITY_MODE=vulnerable

Cambiando esntre esos modos podemos proteger o blindar el sistema, una vez las cambiemos debemos recrear los contenedores,, por lo que deben correr: 

docker compose up -d --build --force-recreate

Asi el nuevo contenedor toma en cuenta las nuevas variuables de entorno ">u<>"

---

## Cómo está conectada la plataforma

Todos los servicios corren dentro de la red interna aislada `red_backend` en Docker Compose y se comunican por nombre: `subastas`, `usuarios` y `consulta`.

```text
Cliente / Toolkit en C
   ├── GraphQL por HTTP en puerto 4000 ───────────► consulta en C++ :4000
   │                                                   ├── gRPC / HTTP2 con TLS en blindado ──► subastas en C++ :50051
   │                                                   └── HTTPS :8080 ────────────────────────► usuarios en C++ :8080
   └── REST / JSON por HTTP o HTTPS en puerto 8080 ► usuarios en C++ :8080
                                                       └── gRPC / HTTP2 con TLS en blindado ──► subastas en C++ :50051
```

* `subastas`, gRPC en `:50051`: maneja las subastas en memoria, concurrencia y el stream en vivo `Seguir`.
* `usuarios`, REST en `:8080`: registro, login, tokens y pagos simulados con idempotencia.
* `consulta`, GraphQL en `:4000`: gateway para consultas y suscripciones.
* `proto/subastas.proto`: contrato compartido por los servicios.

## Antes de levantar

Asegúrate de tener un archivo `.env` tomando como base `.env.example`, con `AUTH_TOKEN_SECRET` de al menos 32 caracteres y el `SECURITY_MODE` que quieras usar: `vulnerable` o `blindado`.

## Crear datos de prueba y probar cada interfaz

### REST - Usuarios y Pagos en puerto 8080
En modo blindado se usa `https://` con `-k` por el certificado autofirmado; en modo vulnerable se usa `http://`.

```bash
# Ver que el servicio responde
curl -k https://localhost:8080/health

# Registrar un usuario de prueba
curl -k -X POST https://localhost:8080/api/v1/registro \
  -H "Content-Type: application/json" \
  -d '{"nombre":"Ana","correo":"ana@test.com","clave":"Password123!"}'

# Hacer login para sacar el token
curl -k -X POST https://localhost:8080/api/v1/login \
  -H "Content-Type: application/json" \
  -d '{"correo":"ana@test.com","clave":"Password123!"}'

# Consultar perfil con el token que devolvió el login
curl -k https://localhost:8080/api/v1/perfil \
  -H "Authorization: Bearer <TU_TOKEN>"

# Probar un cobro con clave de idempotencia
curl -k -X POST https://localhost:8080/api/v1/pagos/cobrar \
  -H "Authorization: Bearer <TU_TOKEN>" \
  -H "Content-Type: application/json" \
  -H "Idempotency-Key: pago-001" \
  -d '{"subasta_id":"subasta-1","monto":5000}'
```

### GraphQL - Consulta en puerto 4000

```bash
# Probar introspección, abierta en vulnerable y bloqueada en blindado
curl -s -X POST http://localhost:4000/graphql \
  -H "Content-Type: application/json" \
  -d '{"query":"{ __schema { types { name } } }"}'

# Probar bomba de profundidad o depth bomb
./seguridad_ofensiva/graphql_depth_bomb http://localhost:4000/graphql subasta subasta 15
```

### gRPC - Subastas en puerto 50051

```bash
# Llamada directa sin TLS, en blindado la rechaza por transporte
./seguridad_ofensiva/grpc_bypass_gateway 127.0.0.1 50051 subasta-demo atacante 100000

# Llamada con TLS pero sin token válido, en blindado responde grpc-status=2
GRPC_CA_PATH=subastas/certs/server.crt GRPC_TLS_SERVER_NAME=subastas \
  ./seguridad_ofensiva/grpc_bypass_gateway 127.0.0.1 50051 subasta-demo atacante 100000
```

## Correr el toolkit de ataques

Los ataques están escritos en C dentro de `seguridad_ofensiva/`. Para correrlos todos contra el modo que tengas levantado:

```bash
cd seguridad_ofensiva
make
./run_all.sh vulnerable   # cuando el sistema está en SECURITY_MODE=vulnerable
./run_all.sh blindado     # cuando el sistema está en SECURITY_MODE=blindado
```

Los resultados quedan guardados en `results.csv`, las capturas de Wireshark en `vulnerable.pcapng` para gRPC sin TLS y `blindado.pcapng` para gRPC con TLS, y el detalle completo en `InformeP2.pdf`.