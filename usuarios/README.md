# Servicio Usuarios/Pagos (REST - C++)

Microservicio encargado del registro, inicio de sesión, emisión/validación de tokens Bearer y cobros simulados en céntimos enteros.

## Variables de entorno

| Variable | Valor por defecto | Descripción |
| :--- | :--- | :--- |
| `PUERTO` | `8080` | Puerto de escucha HTTP/HTTPS |
| `MODO_VULNERABLE` | `false` | `true` desactiva las defensas OWASP y apaga TLS |
| `USAR_TLS` | `!MODO_VULNERABLE` | Fuerza activar/desactivar HTTPS manualmente |
| `TLS_CERT` | `/app/certs/usuarios.crt` | Ruta al certificado X.509 |
| `TLS_KEY` | `/app/certs/usuarios.key` | Ruta a la llave privada TLS |
| `SUBASTAS_HOST` | `subastas:50051` | Dirección interna del servicio Subastas |

## Endpoints REST

* `GET /health`: Estado del servicio (`status`, `servicio`, `modo_vulnerable`, `tls`).
* `POST /api/v1/registro`: Body JSON `{"nombre", "correo", "password"}` -> `201 Created`.
* `POST /api/v1/login`: Body JSON `{"correo", "password"}` -> `200 OK` con `token`, `usuario_id`, `expira_en`.
* `GET /api/v1/perfil`: Requiere `Authorization: Bearer <token>`.
* `GET /api/v1/usuarios/:id`: Consulta usuario (protegido contra **BOLA / API1** en modo blindado).
* `PATCH /api/v1/usuarios/:id`: Actualiza `nombre` (protegido contra **BOLA / API1** y **Mass Assignment / API3** en modo blindado).
* `POST /api/v1/pagos/cobrar`: Body JSON `{"subasta_id", "monto_centimos"}` (entero > 0). En modo blindado exige `Idempotency-Key` y valida `X-Timestamp` opcional contra ataques de **Replay**, además de aplicar lista blanca (**API3**).
* `GET /api/v1/pagos/:id`: Consulta un cobro (protegido contra **BOLA / API1** en modo blindado).
