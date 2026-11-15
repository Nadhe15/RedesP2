#pragma once

#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

struct Usuario {
    std::string id;
    std::string nombre;
    std::string correo;
    std::string salt;
    std::string password_hash;
    std::string rol = "usuario";
};

struct SesionToken {
    std::string token;
    std::string usuario_id;
    std::string correo;
    std::string rol;
    std::time_t expira_en = 0; // 0 significa sin expiracion (solo en modo vulnerable)
};

// manejo usuarios y tokens en memoria de forma segura entre hilos
class AuthStore {
public:
    bool registrar(const std::string& nombre,
                   const std::string& correo,
                   const std::string& password,
                   Usuario& out_usuario,
                   std::string& out_error,
                   int& out_status);

    bool login(const std::string& correo,
               const std::string& password,
               bool modo_vulnerable,
               SesionToken& out_sesion,
               std::string& out_error);

    bool validar_token(const std::string& auth_header,
                       bool modo_vulnerable,
                       SesionToken& out_sesion,
                       std::string& out_error);

    bool obtener_usuario(const std::string& usuario_id, Usuario& out_usuario);

    bool actualizar_usuario(const std::string& usuario_id,
                            const std::string& nuevo_nombre,
                            const std::string& nuevo_rol,
                            bool modo_vulnerable,
                            Usuario& out_usuario,
                            std::string& out_error);

private:
    std::mutex mtx_;
    int siguiente_id_ = 1;
    int contador_tokens_ = 1;
    std::unordered_map<std::string, Usuario> usuarios_por_id_;
    std::unordered_map<std::string, std::string> id_por_correo_;
    std::unordered_map<std::string, SesionToken> sesiones_;
};
