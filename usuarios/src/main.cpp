#include <cstdlib>
#include <iostream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "auth.hpp"

using json = nlohmann::json;

struct Config {
    int puerto = 8080;
    bool modo_vulnerable = false;
    std::string subastas_host = "subastas:50051";
};

// cargo la configuracion desde variables de entorno para cambiar de modo en docker compose
static Config leer_config() {
    Config cfg;

    if (const char* p = std::getenv("PUERTO")) {
        try {
            int val = std::stoi(p);
            if (val > 0 && val <= 65535) {
                cfg.puerto = val;
            }
        } catch (...) {
            // si el puerto no es valido me quedo con el 8080 por defecto
        }
    }

    if (const char* mv = std::getenv("MODO_VULNERABLE")) {
        std::string v(mv);
        cfg.modo_vulnerable = (v == "true" || v == "1" || v == "TRUE");
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

int main() {
    Config cfg = leer_config();
    AuthStore auth;
    httplib::Server svr;

    // endpoint basico para verificar que el contenedor levanto bien
    svr.Get("/health", [&cfg](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"status", "ok"},
            {"servicio", "usuarios"},
            {"modo_vulnerable", cfg.modo_vulnerable}
        };
        res.set_content(body.dump(), "application/json");
    });

    svr.Post("/api/v1/registro", [&auth](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            responder_error(res, 400, "json invalido");
            return;
        }

        std::string nombre = body.value("nombre", "");
        std::string correo = body.value("correo", "");
        std::string password = body.value("password", "");

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
        try {
            body = json::parse(req.body);
        } catch (...) {
            responder_error(res, 400, "json invalido");
            return;
        }

        std::string correo = body.value("correo", "");
        std::string password = body.value("password", "");

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

    std::cout << "[usuarios] escuchando en 0.0.0.0:" << cfg.puerto
              << " (modo_vulnerable=" << (cfg.modo_vulnerable ? "true" : "false")
              << ", subastas=" << cfg.subastas_host << ")" << std::endl;

    if (!svr.listen("0.0.0.0", cfg.puerto)) {
        std::cerr << "[usuarios] error al iniciar el servidor en el puerto " << cfg.puerto << std::endl;
        return 1;
    }

    return 0;
}
