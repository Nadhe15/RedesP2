# Rol 4 — Seguridad ofensiva (Proyecto 2, CI-4835)

Toolkit de ataque en C contra la propia plataforma de subastas (gRPC +
REST + GraphQL), mapeado al catálogo de la sección 5 del enunciado.
**Úsalo únicamente contra tu propio `docker compose`, dentro de la red de
laboratorio** (sección 11): nunca contra la red de la Universidad, otros
equipos o servicios de terceros.

## Dependencias

```
sudo apt install build-essential libcurl4-openssl-dev libnghttp2-dev libssl-dev
```

- Ataques REST y GraphQL: `libcurl` (HTTP/1.1 normal, sencillo).
- Ataques gRPC: `libnghttp2` para el framing HTTP/2; el framing gRPC
  (5 bytes de cabecera + Protobuf) y la serialización de los mensajes
  del núcleo compartido (`PujarReq`, `ServerReflectionRequest`, etc.) están
  escritos a mano en `grpc_wire.c`, sin depender de código generado por
  `protoc`, para no acoplarse a cómo cada quien genere sus stubs.
  **Deliberadamente no usa TLS** — ver sección siguiente.

## Compilar

```
make            # compila los 10 binarios
make rest       # solo los ataques REST
make graphql    # solo los ataques GraphQL
make grpc       # solo los ataques gRPC
```

## Mapeo al catálogo (sección 5 del enunciado)

| Binario                      | OWASP | Falla                                                         |
|-------------------------------|-------|----------------------------------------------------------------|
| `rest_bola`                    | API1  | BOLA: acceder a objetos ajenos cambiando el id en la ruta       |
| `rest_mass_assignment`         | API3  | Mass assignment: campos de más que el servidor acepta a ciegas  |
| `rest_auth_debil`              | API2  | Auth rota: sin token / token adivinable / token expirado        |
| `rest_replay`                  | —     | Replay: reenviar una puja capturada                             |
| `graphql_introspection`        | API8  | Introspección abierta fuera de desarrollo                       |
| `graphql_depth_bomb`           | API4  | Consulta anidada que agota CPU/memoria                          |
| `graphql_alias_batch`          | API6  | Abuso de alias/batching para fuerza bruta de login en 1 petición |
| `grpc_reflection`              | API9  | gRPC reflection abierta: enumerar servicios sin el `.proto`      |
| `grpc_bypass_gateway`          | API5  | Llamar directo a Subastas por gRPC, saltando el gateway          |
| `grpc_proto_decode`            | —     | Decodifica a mano un Protobuf capturado sin TLS (evidencia)      |

Cada binario imprime su resultado y lo anexa a `results.csv`
(`timestamp,ataque,objetivo,http_status,resultado,detalle`), pensado para
alimentar directamente las tablas del **informe de seguridad** (máx. 5
páginas, sección 8).

## Cómo demostrar el contraste vulnerable ⇄ blindado

1. Levanta el sistema del equipo en **modo vulnerable** (perfil/env del
   interruptor de la sección 6) y corre `./run_all.sh vulnerable` (ajusta
   antes las variables `REST_BASE`, `GQL_URL`, `GRPC_HOST/PUERTO` y
   `TOKEN` al inicio del script a los endpoints reales de tu equipo).
2. Levanta el mismo sistema en **modo blindado** y corre
   `./run_all.sh blindado`.
3. Compara ambas corridas en `results.csv`: cada fila dice `VULNERABLE`,
   `bloqueado` o similar — esa es la evidencia del "antes/después" que
   pide la sección 10 de evaluación.
4. Para las capturas `.pcapng` (sección 8 y 9): corre `grpc_bypass_gateway`
   o `grpc_reflection` mientras Wireshark/tcpdump captura el puerto gRPC,
   una vez sin TLS (texto plano, como aquí) y otra vez con TLS puesto por
   el equipo (ahí sólo deberían verse metadatos, no el Protobuf). Para la
   captura sin TLS, puedes exportar el payload del frame DATA desde
   Wireshark a un archivo y pasárselo a `grpc_proto_decode archivo.bin
   --skip-grpc-header` para mostrar en la defensa cómo se decodifica sin
   necesitar siquiera el `.proto` oficial.

## Notas importantes

- `rest_bola`, `rest_mass_assignment`, `rest_auth_debil` y `rest_replay`
  asumen rutas `/api/v1/...` genéricas vía argumentos de línea de comandos
  porque esas rutas las diseña el equipo (sección 4): ajusta las plantillas
  al esquema REST real que defina el Rol 2.
- `graphql_alias_batch` asume una mutation `login(usuario, clave)` —
  ajusta el nombre de campo/argumentos al esquema real que defina el Rol 3.
- `grpc_wire.c` implementa un cliente gRPC mínimo (HTTP/2 en texto plano +
  framing gRPC + Protobuf a mano) pensado para este proyecto específico
  (llamadas unarias simples). Sin `GRPC_CA_PATH` usa h2c para el modo
  vulnerable; al definir `GRPC_CA_PATH` valida el certificado TLS y negocia
  HTTP/2. Usa como host un nombre/IP incluido en el certificado del servidor;
  para separar la dirección TCP del nombre del certificado puedes definir
  `GRPC_TLS_SERVER_NAME` (por ejemplo, `subastas`).
  No es un cliente gRPC de producción.

Para levantar los servicios en modo blindado, define `AUTH_TOKEN_SECRET` con
al menos 32 bytes aleatorios en un archivo local ignorado por Git, por ejemplo
`.env.local`; Compose lo requiere tanto para emitir como para verificar tokens.
Para probar desde el host contra el certificado actual (CN `subastas`):
`GRPC_CA_PATH=subastas/certs/server.crt GRPC_TLS_SERVER_NAME=subastas`.
- Antes de la entrega, declara en el informe que parte de este código fue
  generado con asistencia de IA (sección 12, integridad académica), y
  asegúrate de poder explicar cada línea en la defensa individual.
