#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <csignal>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include "auth_token.hpp"
#include "subastas_service.hpp"

namespace {
    std::atomic<bool> ejecutando{true};
    std::unique_ptr<grpc::Server> servidor_grpc;

    // Detengo el servidor de forma limpia
    void ManejadorSenal(int) {
        ejecutando = false;
        if (servidor_grpc) {
            servidor_grpc->Shutdown();
        }
    }

    // Cargo el texto de un archivo
    std::string LeerArchivo(const std::string& ruta) {
        std::ifstream archivo(ruta);
        if (!archivo.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << archivo.rdbuf();
        return buffer.str();
    }
}

int main() {
    std::signal(SIGINT, ManejadorSenal);
    std::signal(SIGTERM, ManejadorSenal);

    // Obtengo las variables del entorno
    const char* env_puerto = std::getenv("PORT");
    std::string puerto = env_puerto ? env_puerto : "50051";

    const char* env_modo = std::getenv("SECURITY_MODE");
    std::string modo = env_modo ? env_modo : "blindado";
    if (modo != "blindado" && modo != "hardened" && modo != "vulnerable") {
        std::cerr << "SECURITY_MODE invalido: " << modo << std::endl;
        return 1;
    }
    bool modo_blindado = (modo == "hardened" || modo == "blindado");

    const char* env_token_secret = std::getenv("AUTH_TOKEN_SECRET");
    std::string auth_token_secret =
        env_token_secret ? env_token_secret : "";
    if (modo_blindado && !auth_token::secret_is_valid(auth_token_secret)) {
        std::cerr << "AUTH_TOKEN_SECRET debe tener al menos 32 caracteres"
                  << std::endl;
        return 1;
    }

    const char* env_cert = std::getenv("TLS_CERT_PATH");
    std::string ruta_cert = env_cert ? env_cert : "certs/server.crt";

    const char* env_key = std::getenv("TLS_KEY_PATH");
    std::string ruta_key = env_key ? env_key : "certs/server.key";

    std::string direccion_servidor = "0.0.0.0:" + puerto;

    SubastaManager manager;
    SubastasServiceImpl servicio(manager, modo_blindado,
                                 std::move(auth_token_secret));

    grpc::ServerBuilder builder;

    // Configuro la seguridad elegida
    if (modo_blindado) {
        std::string contenido_cert = LeerArchivo(ruta_cert);
        std::string contenido_key = LeerArchivo(ruta_key);

        if (contenido_cert.empty() || contenido_key.empty()) {
            std::cerr << "Error al cargar los certificados TLS para modo blindado" << std::endl;
            return 1;
        }

        grpc::SslServerCredentialsOptions opciones_ssl;
        opciones_ssl.pem_key_cert_pairs.push_back({contenido_key, contenido_cert});
        auto credenciales = grpc::SslServerCredentials(opciones_ssl);

        builder.AddListeningPort(direccion_servidor, credenciales);
        std::cout << "Iniciando servicio de Subastas en MODO BLINDADO" << std::endl;
    } else {
        auto credenciales = grpc::InsecureServerCredentials();
        builder.AddListeningPort(direccion_servidor, credenciales);

        // Habilito reflection para pruebas
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();

        std::cout << "Iniciando servicio de Subastas en MODO VULNERABLE" << std::endl;
    }

    builder.RegisterService(&servicio);
    servidor_grpc = builder.BuildAndStart();

    if (!servidor_grpc) {
        std::cerr << "Fallo al iniciar el servidor en el puerto " << puerto << std::endl;
        return 1;
    }

    std::cout << "Servicio escuchando peticiones gRPC en " << direccion_servidor << std::endl;
    servidor_grpc->Wait();

    std::cout << "Servicio de Subastas detenido correctamente" << std::endl;
    return 0;
}
