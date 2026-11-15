
#include "crow.h"
#include "graphql.hpp"
#include <iostream>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>

using json = nlohmann::json;

struct WsState {
    std::mutex mutex;
    bool open = true;
    bool subscribed = false;
};

// Limite sencillo de peticiones por IP: 60 por minuto.
bool allowRequest(const std::string& ip) {
    using Clock = std::chrono::steady_clock;

    struct Window {
        Clock::time_point start = Clock::now();
        int count = 0;
    };

    static std::mutex mutex;
    static std::unordered_map<std::string, Window> windows;

    std::lock_guard<std::mutex> lock(mutex);
    auto& window = windows[ip];
    auto now = Clock::now();

    if (now - window.start >= std::chrono::minutes(1)) {
        window.start = now;
        window.count = 0;
    }

    return ++window.count <= 60;
}

crow::response jsonResponse(int status, const json& body) {
    crow::response response(status, body.dump());
    response.add_header("Content-Type", "application/json");
    response.add_header("Access-Control-Allow-Origin", "*");
    return response;
}

int main() {
    const char* secEnv = std::getenv("SECURITY_MODE");
    const char* grpcEnv = std::getenv("SUBASTAS_GRPC_URL");
    const char* restEnv = std::getenv("USUARIOS_REST_URL");
    const char* portEnv = std::getenv("PORT");

    std::string mode = secEnv ? secEnv : "vulnerable";
    std::string grpcUrl = grpcEnv ? grpcEnv : "subastas:50051";
    std::string restUrl = restEnv ? restEnv : "http://usuarios:8080";
    int port = portEnv ? std::stoi(portEnv) : 4000;

    bool isBlindado = (mode == "blindado");

    std::cout << "Servicio Consulta GraphQL\n"
              << "Modo: " << (isBlindado ? "BLINDADO" : "VULNERABLE") << "\n"
              << "gRPC: " << grpcUrl << "\n"
              << "REST: " << restUrl << "\n"
              << "Puerto: " << port << "\n";

    SubastasGrpcClient grpcClient(grpcUrl);
    UsuariosRestClient restClient(restUrl);
    GraphQLEngine engine(grpcClient, restClient, isBlindado);

    crow::SimpleApp app;

    // Conexiones WebSocket activas.
    std::mutex clientsMutex;
    std::unordered_map<crow::websocket::connection*,
                       std::shared_ptr<WsState>> clients;

    // HTTP: Queries y Mutations.
    CROW_ROUTE(app, "/graphql").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        if (isBlindado && !allowRequest(req.remote_ip_address)) {
            return jsonResponse(429, {
                {"errors", {{{"message", "Limite de peticiones superado."}}}}
            });
        }

        json body;

        try {
            body = json::parse(req.body);
        } catch (...) {
            return jsonResponse(400, {
                {"errors", {{{"message", "JSON no valido."}}}}
            });
        }

        if (!body.is_object() || !body.contains("query") ||
            !body["query"].is_string()) {
            return jsonResponse(400, {
                {"errors", {{{"message", "Se requiere una consulta GraphQL."}}}}
            });
        }

        std::string query = body["query"];
        std::string token = req.get_header_value("Authorization");

        return jsonResponse(200, engine.execute(query, token));
    });

    // CORS.
    CROW_ROUTE(app, "/graphql").methods(crow::HTTPMethod::OPTIONS)
    ([] {
        crow::response response(204);
        response.add_header("Access-Control-Allow-Origin", "*");
        response.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        response.add_header("Access-Control-Allow-Headers",
                            "Content-Type, Authorization");
        return response;
    });

    // WebSocket: Subscriptions.
    // Mensaje esperado:
    // {"type":"subscribe","query":"subscription {...}","token":"Bearer ..."}

     CROW_ROUTE(app, "/subscriptions")
     .websocket()

    .onopen([&](crow::websocket::connection& conn) {
        auto state = std::make_shared<WsState>();

        std::lock_guard<std::mutex> lock(clientsMutex);
        clients[&conn] = state;

        std::cout << "[WS] Cliente conectado\n";
    })

    .onmessage([&](crow::websocket::connection& conn,
                   const std::string& msg, bool isBinary) {
        if (isBinary) return;

        std::shared_ptr<WsState> state;

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = clients.find(&conn);

            if (it == clients.end()) return;
            state = it->second;
        }

        auto sendMessage = [&conn, state](const json& message) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->open)
                conn.send_text(message.dump());
        };

        try {
            json data = json::parse(msg);

            if (!data.is_object() ||
                data.value("type", "") != "subscribe")
                return;

            std::string query = data.value("query", "");
            std::string token = data.value("token", "");

            if (query.empty()) {
                sendMessage({
                    {"type", "error"},
                    {"message", "Falta la consulta GraphQL."}
                });
                return;
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);

                if (!state->open || state->subscribed) return;
                state->subscribed = true;
            }

            // El motor GraphQL valida la consulta y conecta con gRPC.
            engine.startSubscription(
                query,
                token,

                // Actualizacion recibida desde Subastas.
                [sendMessage](const json& update) {
                    sendMessage({
                        {"type", "next"},
                        {"data", {{"seguirSubasta", update}}}
                    });
                },

                // Finalizacion o error del stream.
                [sendMessage, state](const grpc::Status& status) {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->subscribed = false;
                    }

                    if (status.ok()) {
                        sendMessage({{"type", "complete"}});
                    } else {
                        sendMessage({
                            {"type", "error"},
                            {"message", status.error_message()}
                        });
                    }
                }
            );
        }
        catch (const std::exception& e) {
            sendMessage({
                {"type", "error"},
                {"message", e.what()}
            });
        }
    })

    .onclose([&](crow::websocket::connection& conn,
                 const std::string&) {
        std::shared_ptr<WsState> state;

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = clients.find(&conn);

            if (it != clients.end()) {
                state = it->second;
                clients.erase(it);
            }
        }

        if (state) {
            bool cancelar = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->open = false;
        cancelar = state->subscribed;
        state->subscribed = false;
    }

    if (cancelar) {
        grpcClient.CancelarSeguir();
    }
}

        std::cout << "[WS] Cliente desconectado\n";
    });

    // Comprobacion del servicio.
    CROW_ROUTE(app, "/health")
    ([] {
        return jsonResponse(200, {
            {"status", "ok"},
            {"service", "consulta-graphql"}
        });
    });

    app.port(port).multithreaded().run();
    return 0;
}
