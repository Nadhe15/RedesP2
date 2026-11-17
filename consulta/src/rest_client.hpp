
#pragma once

#include <string>
#include <cstddef>
#include <nlohmann/json.hpp>

// Interfaz provisional, seguro habra que hacer cambios con otros roles . 

using json = nlohmann::json;

// Cliente REST para el servicio Usuarios/Pagos
class UsuariosRestClient {
public:
    explicit UsuariosRestClient(const std::string& baseUrl);

    // Obtener un usuario por ID
    json obtenerUsuario(
        const std::string& userId,
        const std::string& token
    );

    // Iniciar sesión
    json login(
        const std::string& username,
        const std::string& password
    );

    // Obtener el usuario autenticado
    json me(const std::string& token);

private:
    std::string baseUrl_;

    static std::size_t writeCallback(
        void* contents,
        std::size_t size,
        std::size_t nmemb,
        void* userp
    );

    json httpGet(
        const std::string& path,
        const std::string& token
    );

    json httpPost(
        const std::string& path,
        const json& body,
        const std::string& token = ""
    );
};
