#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "pagos.hpp"
#include "auth_token.hpp"

using json = nlohmann::json;

struct Config {
    int puerto = 8080;
    bool modo_vulnerable = false;
    bool usar_tls = true;
    std::string tls_cert = "/app/certs/usuarios.crt";
    std::string tls_key = "/app/certs/usuarios.key";
    std::string subastas_host = "subastas:50051";
};

static bool archivo_existe(const std::string& ruta) {
    std::ifstream f(ruta);
    return f.good();
}

// cargo la configuracion desde variables de entorno para cambiar de modo en docker compose
static Config leer_config() {
    Config cfg;

    // acepto PORT (usado en docker-compose) o PUERTO
    const char* p = std::getenv("PORT");
    if (!p || std::string(p).empty()) {
        p = std::getenv("PUERTO");
    }
    if (p) {
        try {
            int val = std::stoi(p);
            if (val > 0 && val <= 65535) {
                cfg.puerto = val;
            }
        } catch (...) {
            // si el puerto no es valido me quedo con el 8080 por defecto
        }
    }

    // acepto SECURITY_MODE (vulnerable/blindado) o MODO_VULNERABLE (true/false)
    if (const char* sm = std::getenv("SECURITY_MODE")) {
        std::string v(sm);
        cfg.modo_vulnerable = (v == "vulnerable" || v == "VULNERABLE");
    } else if (const char* mv = std::getenv("MODO_VULNERABLE")) {
        std::string v(mv);
        cfg.modo_vulnerable = (v == "true" || v == "1" || v == "TRUE");
    }

    // en modo vulnerable apago TLS para permitir la intercepcion de trafico en claro
    cfg.usar_tls = !cfg.modo_vulnerable;
    if (const char* ut = std::getenv("USAR_TLS")) {
        std::string v(ut);
        cfg.usar_tls = (v == "true" || v == "1" || v == "TRUE");
    }

    if (const char* tc = std::getenv("TLS_CERT_PATH")) {
        if (std::string(tc).size() > 0) cfg.tls_cert = tc;
    } else if (const char* tc2 = std::getenv("TLS_CERT")) {
        if (std::string(tc2).size() > 0) cfg.tls_cert = tc2;
    }

    if (const char* tk = std::getenv("TLS_KEY_PATH")) {
        if (std::string(tk).size() > 0) cfg.tls_key = tk;
    } else if (const char* tk2 = std::getenv("TLS_KEY")) {
        if (std::string(tk2).size() > 0) cfg.tls_key = tk2;
    }

    if (const char* sh = std::getenv("SUBASTAS_HOST")) {
        if (std::string(sh).size() > 0) {
            cfg.subastas_host = sh;
        }
    }

    return cfg;
}

static void responder_error(httplib::Response& res, int status, const std::string& mensaje) {
    res.status = status;
    json body = {{"error", mensaje}};
    res.set_content(body.dump(), "application/json");
}

static bool parsear_objeto_json(const std::string& raw, json& out) {
    try {
        out = json::parse(raw);
        return out.is_object();
    } catch (...) {
        return false;
    }
}

// evito que nlohmann::json lance excepcion si un campo viene como numero o arreglo
static bool leer_campo_string(const json& body, const std::string& clave, std::string& out) {
    if (!body.contains(clave)) {
        out.clear();
        return true;
    }
    if (!body[clave].is_string()) {
        return false;
    }
    out = body[clave].get<std::string>();
    return true;
}

// registro todas las rutas sobre httplib::Server para compartirlas entre HTTP y HTTPS
static void registrar_rutas(httplib::Server& svr, AuthStore& auth, PagosStore& pagos, const Config& cfg) {
    // endpoint basico para verificar que el contenedor levanto bien
    svr.Get("/health", [&cfg](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"status", "ok"},
            {"servicio", "usuarios"},
            {"modo_vulnerable", cfg.modo_vulnerable},
            {"tls", cfg.usar_tls}
        };
        res.set_content(body.dump(), "application/json");
    });

    svr.Post("/api/v1/registro", [&auth](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parsear_objeto_json(req.body, body)) {
            responder_error(res, 400, "json invalido");
            return;
        }

        std::string nombre, correo, password;
        if (!leer_campo_string(body, "nombre", nombre) ||
            !leer_campo_string(body, "correo", correo) ||
            !leer_campo_string(body, "password", password)) {
            responder_error(res, 400, "nombre, correo y password deben ser texto");
            return;
        }

        Usuario u;
        std::string err;
        int status = 400;
        if (!auth.registrar(nombre, correo, password, u, err, status)) {
            responder_error(res, status, err);
            return;
        }

        res.status = 201;
        json resp = {
            {"id", u.id},
            {"nombre", u.nombre},
            {"correo", u.correo},
            {"rol", u.rol}
        };
        res.set_content(resp.dump(), "application/json");
    });

    svr.Post("/api/v1/login", [&auth, &cfg](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parsear_objeto_json(req.body, body)) {
            responder_error(res, 400, "json invalido");
            return;
        }

        std::string correo, password;
        if (!leer_campo_string(body, "correo", correo) ||
            !leer_campo_string(body, "password", password)) {
            responder_error(res, 400, "correo y password deben ser texto");
            return;
        }

        SesionToken ses;
        std::string err;
        if (!auth.login(correo, password, cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        res.status = 200;
        json resp = {
            {"token", ses.token},
            {"usuario_id", ses.usuario_id},
            {"expira_en", ses.expira_en}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // devuelvo la sesion del token actual para validar autenticacion desde clientes o scripts
    svr.Get("/api/v1/perfil", [&auth, &cfg](const httplib::Request& req, httplib::Response& res) {
        std::string auth_header = req.get_header_value("Authorization");
        SesionToken ses;
        std::string err;
        if (!auth.validar_token(auth_header, cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        res.status = 200;
        json resp = {
            {"usuario_id", ses.usuario_id},
            {"correo", ses.correo},
            {"rol", ses.rol}
        };
        res.set_content(resp.dump(), "application/json");
    });

    svr.Get(R"(/api/v1/usuarios/([^/]+))", [&auth, &cfg](const httplib::Request& req, httplib::Response& res) {
        SesionToken ses;
        std::string err;
        if (!auth.validar_token(req.get_header_value("Authorization"), cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        std::string target_id = req.matches[1];

        // defensa API1 (BOLA): solo dejo consultar si es el dueño del recurso o admin
        if (!cfg.modo_vulnerable && ses.usuario_id != target_id && ses.rol != "admin") {
            responder_error(res, 403, "no tienes permiso para consultar este usuario");
            return;
        }

        Usuario u;
        if (!auth.obtener_usuario(target_id, u)) {
            responder_error(res, 404, "usuario no encontrado");
            return;
        }

        res.status = 200;
        json resp = {
            {"id", u.id},
            {"nombre", u.nombre},
            {"correo", u.correo},
            {"rol", u.rol}
        };
        res.set_content(resp.dump(), "application/json");
    });

    svr.Patch(R"(/api/v1/usuarios/([^/]+))", [&auth, &cfg](const httplib::Request& req, httplib::Response& res) {
        SesionToken ses;
        std::string err;
        if (!auth.validar_token(req.get_header_value("Authorization"), cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        std::string target_id = req.matches[1];
        if (!cfg.modo_vulnerable && ses.usuario_id != target_id && ses.rol != "admin") {
            responder_error(res, 403, "no tienes permiso para modificar este usuario");
            return;
        }

        json body;
        if (!parsear_objeto_json(req.body, body)) {
            responder_error(res, 400, "json invalido");
            return;
        }

        // en modo blindado aplico lista blanca y rechazo campos no permitidos como rol
        if (!cfg.modo_vulnerable) {
            for (auto it = body.begin(); it != body.end(); ++it) {
                if (it.key() != "nombre") {
                    responder_error(res, 400, "campo no permitido: " + it.key());
                    return;
                }
            }
        }

        std::string nuevo_nombre, nuevo_rol;
        if (!leer_campo_string(body, "nombre", nuevo_nombre) ||
            !leer_campo_string(body, "rol", nuevo_rol)) {
            responder_error(res, 400, "los campos deben ser texto");
            return;
        }

        Usuario u;
        if (!auth.actualizar_usuario(target_id, nuevo_nombre, nuevo_rol, cfg.modo_vulnerable, u, err)) {
            responder_error(res, 404, err);
            return;
        }

        res.status = 200;
        json resp = {
            {"id", u.id},
            {"nombre", u.nombre},
            {"correo", u.correo},
            {"rol", u.rol}
        };
        res.set_content(resp.dump(), "application/json");
    });

    svr.Post("/api/v1/pagos/cobrar", [&auth, &pagos, &cfg](const httplib::Request& req, httplib::Response& res) {
        SesionToken ses;
        std::string err;
        if (!auth.validar_token(req.get_header_value("Authorization"), cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        json body;
        if (!parsear_objeto_json(req.body, body)) {
            responder_error(res, 400, "json invalido");
            return;
        }

        // en modo blindado solo acepto subasta_id y monto_centimos
        if (!cfg.modo_vulnerable) {
            for (auto it = body.begin(); it != body.end(); ++it) {
                if (it.key() != "subasta_id" && it.key() != "monto_centimos") {
                    responder_error(res, 400, "campo no permitido: " + it.key());
                    return;
                }
            }
        }

        if (!body.contains("monto_centimos") || !body["monto_centimos"].is_number_integer()) {
            responder_error(res, 400, "monto_centimos debe ser un entero");
            return;
        }

        std::string subasta_id;
        if (!leer_campo_string(body, "subasta_id", subasta_id)) {
            responder_error(res, 400, "subasta_id debe ser texto");
            return;
        }

        int64_t monto = body["monto_centimos"].get<int64_t>();
        std::string idem_key = req.get_header_value("Idempotency-Key");

        // si viene marca temporal en modo blindado verifico que no sea una peticion vieja
        if (!cfg.modo_vulnerable && req.has_header("X-Timestamp")) {
            try {
                std::string ts_str = req.get_header_value("X-Timestamp");
                size_t idx = 0;
                std::time_t ts_req = std::stoll(ts_str, &idx);
                if (idx != ts_str.size()) {
                    responder_error(res, 400, "X-Timestamp invalido");
                    return;
                }
                std::time_t ahora = std::time(nullptr);
                if (ahora - ts_req > 60 || ts_req - ahora > 60) {
                    responder_error(res, 409, "marca temporal expirada (replay bloqueado)");
                    return;
                }
            } catch (...) {
                responder_error(res, 400, "X-Timestamp invalido");
                return;
            }
        }

        // falla API3: en modo vulnerable acepto usuario_id y estado enviados por el cliente
        std::string usuario_cobro = ses.usuario_id;
        std::string estado_cobro = "pagado";
        if (cfg.modo_vulnerable) {
            if (body.contains("usuario_id") && body["usuario_id"].is_string()) {
                usuario_cobro = body["usuario_id"].get<std::string>();
            }
            if (body.contains("estado") && body["estado"].is_string()) {
                estado_cobro = body["estado"].get<std::string>();
            }
        }

        Cobro c;
        int status_cobro = 400;
        if (!pagos.crear_cobro(usuario_cobro, subasta_id, monto, estado_cobro, idem_key, cfg.modo_vulnerable, c, err, status_cobro)) {
            responder_error(res, status_cobro, err);
            return;
        }

        res.status = 201;
        json resp = {
            {"id", c.id},
            {"usuario_id", c.usuario_id},
            {"subasta_id", c.subasta_id},
            {"monto_centimos", c.monto_centimos},
            {"estado", c.estado},
            {"creado_en", c.creado_en}
        };
        res.set_content(resp.dump(), "application/json");
    });

    svr.Get(R"(/api/v1/pagos/([^/]+))", [&auth, &pagos, &cfg](const httplib::Request& req, httplib::Response& res) {
        SesionToken ses;
        std::string err;
        if (!auth.validar_token(req.get_header_value("Authorization"), cfg.modo_vulnerable, ses, err)) {
            responder_error(res, 401, err);
            return;
        }

        std::string cobro_id = req.matches[1];
        Cobro c;
        if (!pagos.obtener_cobro(cobro_id, c)) {
            responder_error(res, 404, "cobro no encontrado");
            return;
        }

        // defensa API1 (BOLA): evito que un usuario vea recibos de cobro ajenos
        if (!cfg.modo_vulnerable && c.usuario_id != ses.usuario_id && ses.rol != "admin") {
            responder_error(res, 403, "no tienes permiso para ver este cobro");
            return;
        }

        res.status = 200;
        json resp = {
            {"id", c.id},
            {"usuario_id", c.usuario_id},
            {"subasta_id", c.subasta_id},
            {"monto_centimos", c.monto_centimos},
            {"estado", c.estado},
            {"creado_en", c.creado_en}
        };
        res.set_content(resp.dump(), "application/json");
    });
}

int main() {
    Config cfg = leer_config();
    if (!cfg.modo_vulnerable) {
        const char* secret = std::getenv("AUTH_TOKEN_SECRET");
        if (!secret || !auth_token::secret_is_valid(secret)) {
            std::cerr << "[usuarios] AUTH_TOKEN_SECRET debe tener al menos 32 caracteres"
                      << std::endl;
            return 1;
        }
    }

    AuthStore auth;
    PagosStore pagos;

    if (cfg.usar_tls) {
        if (!archivo_existe(cfg.tls_cert) || !archivo_existe(cfg.tls_key)) {
            std::cerr << "[usuarios] faltan certificados TLS en " << cfg.tls_cert
                      << " y " << cfg.tls_key << std::endl;
            return 1;
        }

        httplib::SSLServer svr(cfg.tls_cert.c_str(), cfg.tls_key.c_str());
        if (!svr.is_valid()) {
            std::cerr << "[usuarios] no se pudo inicializar el contexto SSL/TLS" << std::endl;
            return 1;
        }

        registrar_rutas(svr, auth, pagos, cfg);
        std::cout << "[usuarios] escuchando en HTTPS 0.0.0.0:" << cfg.puerto
                  << " (modo_vulnerable=" << (cfg.modo_vulnerable ? "true" : "false")
                  << ", subastas=" << cfg.subastas_host << ")" << std::endl;

        if (!svr.listen("0.0.0.0", cfg.puerto)) {
            std::cerr << "[usuarios] error al iniciar el servidor HTTPS en el puerto " << cfg.puerto << std::endl;
            return 1;
        }
    } else {
        httplib::Server svr;
        registrar_rutas(svr, auth, pagos, cfg);
        std::cout << "[usuarios] escuchando en HTTP 0.0.0.0:" << cfg.puerto
                  << " (modo_vulnerable=" << (cfg.modo_vulnerable ? "true" : "false")
                  << ", subastas=" << cfg.subastas_host << ")" << std::endl;

        if (!svr.listen("0.0.0.0", cfg.puerto)) {
            std::cerr << "[usuarios] error al iniciar el servidor HTTP en el puerto " << cfg.puerto << std::endl;
            return 1;
        }
    }

    return 0;
}
