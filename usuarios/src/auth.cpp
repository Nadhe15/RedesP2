#include "auth.hpp"
#include "auth_token.hpp"

#include <cstdlib>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>

static std::string bytes_a_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

// genero bytes aleatorios criptograficos con OpenSSL para salts y tokens seguros
static std::string generar_aleatorio_hex(size_t num_bytes) {
    unsigned char buf[64];
    if (num_bytes > sizeof(buf)) {
        num_bytes = sizeof(buf);
    }
    RAND_bytes(buf, static_cast<int>(num_bytes));
    return bytes_a_hex(buf, num_bytes);
}

static std::string sha256_hex(const std::string& entrada) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, entrada.data(), entrada.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    return bytes_a_hex(hash, hash_len);
}

bool AuthStore::registrar(const std::string& nombre,
                          const std::string& correo,
                          const std::string& password,
                          Usuario& out_usuario,
                          std::string& out_error,
                          int& out_status) {
    if (nombre.empty() || correo.empty() || password.empty()) {
        out_error = "nombre, correo y password no pueden estar vacios";
        out_status = 400;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (id_por_correo_.count(correo) > 0) {
        out_error = "el correo ya esta registrado";
        out_status = 409;
        return false;
    }

    Usuario u;
    u.id = "usr-" + std::to_string(siguiente_id_++);
    u.nombre = nombre;
    u.correo = correo;
    u.salt = generar_aleatorio_hex(16);
    u.password_hash = sha256_hex(u.salt + ":" + password);
    u.rol = "usuario";

    usuarios_por_id_[u.id] = u;
    id_por_correo_[u.correo] = u.id;
    out_usuario = u;
    out_status = 201;
    return true;
}

bool AuthStore::login(const std::string& correo,
                      const std::string& password,
                      bool modo_vulnerable,
                      SesionToken& out_sesion,
                      std::string& out_error) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it_id = id_por_correo_.find(correo);
    if (it_id == id_por_correo_.end()) {
        out_error = "credenciales invalidas";
        return false;
    }

    const Usuario& u = usuarios_por_id_.at(it_id->second);
    if (sha256_hex(u.salt + ":" + password) != u.password_hash) {
        out_error = "credenciales invalidas";
        return false;
    }

    SesionToken ses;
    ses.usuario_id = u.id;
    ses.correo = u.correo;
    ses.rol = u.rol;

    if (modo_vulnerable) {
        // falla API2: token predecible y sin expiracion
        ses.token = "tok-" + u.id + "-" + std::to_string(contador_tokens_++);
        ses.expira_en = 0;
    } else {
        ses.expira_en = std::time(nullptr) + 3600;
        const char* secret = std::getenv("AUTH_TOKEN_SECRET");
        if (!secret || !auth_token::secret_is_valid(secret)) {
            out_error = "AUTH_TOKEN_SECRET no esta configurado o es demasiado corto";
            return false;
        }
        ses.token = auth_token::issue(u.id, ses.expira_en, secret);
        if (ses.token.empty()) {
            out_error = "no se pudo generar el token firmado";
            return false;
        }
    }

    sesiones_[ses.token] = ses;
    out_sesion = ses;
    return true;
}

bool AuthStore::validar_token(const std::string& auth_header,
                              bool modo_vulnerable,
                              SesionToken& out_sesion,
                              std::string& out_error) {
    const std::string prefijo = "Bearer ";
    if (auth_header.size() <= prefijo.size() || auth_header.compare(0, prefijo.size(), prefijo) != 0) {
        out_error = "falta header Authorization Bearer";
        return false;
    }

    std::string token = auth_header.substr(prefijo.size());

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sesiones_.find(token);
    if (it != sesiones_.end()) {
        // en modo blindado reviso que el token no haya expirado
        if (!modo_vulnerable && it->second.expira_en > 0 && std::time(nullptr) >= it->second.expira_en) {
            sesiones_.erase(it);
            out_error = "token expirado";
            return false;
        }
        out_sesion = it->second;
        return true;
    }

    // falla API2: en modo vulnerable acepto tokens forjados con el formato predecible tok-usr-<n>-...
    if (modo_vulnerable && token.rfind("tok-usr-", 0) == 0) {
        size_t pos_guion = token.find('-', 8);
        std::string uid = (pos_guion != std::string::npos) ? token.substr(4, pos_guion - 4) : token.substr(4);
        auto it_u = usuarios_por_id_.find(uid);
        if (it_u != usuarios_por_id_.end()) {
            out_sesion.token = token;
            out_sesion.usuario_id = it_u->second.id;
            out_sesion.correo = it_u->second.correo;
            out_sesion.rol = it_u->second.rol;
            out_sesion.expira_en = 0;
            return true;
        }
    }

    out_error = "token invalido";
    return false;
}

bool AuthStore::obtener_usuario(const std::string& usuario_id, Usuario& out_usuario) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = usuarios_por_id_.find(usuario_id);
    if (it == usuarios_por_id_.end()) {
        return false;
    }
    out_usuario = it->second;
    return true;
}

bool AuthStore::actualizar_usuario(const std::string& usuario_id,
                                   const std::string& nuevo_nombre,
                                   const std::string& nuevo_rol,
                                   bool modo_vulnerable,
                                   Usuario& out_usuario,
                                   std::string& out_error) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = usuarios_por_id_.find(usuario_id);
    if (it == usuarios_por_id_.end()) {
        out_error = "usuario no encontrado";
        return false;
    }

    if (!nuevo_nombre.empty()) {
        it->second.nombre = nuevo_nombre;
    }

    // falla API3 (mass assignment): en modo vulnerable dejo cambiar el rol sin lista blanca
    if (modo_vulnerable && !nuevo_rol.empty()) {
        it->second.rol = nuevo_rol;
    }

    out_usuario = it->second;
    return true;
}
