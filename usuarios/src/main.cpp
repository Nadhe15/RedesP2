#include <cstdlib>
#include <iostream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

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

int main() {
    Config cfg = leer_config();
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

    std::cout << "[usuarios] escuchando en 0.0.0.0:" << cfg.puerto
              << " (modo_vulnerable=" << (cfg.modo_vulnerable ? "true" : "false")
              << ", subastas=" << cfg.subastas_host << ")" << std::endl;

    if (!svr.listen("0.0.0.0", cfg.puerto)) {
        std::cerr << "[usuarios] error al iniciar el servidor en el puerto " << cfg.puerto << std::endl;
        return 1;
    }

    return 0;
}
