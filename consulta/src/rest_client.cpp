#include "rest_client.hpp"

#include <curl/curl.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct CurlGlobal {
    CurlGlobal() {
        const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) {
            throw std::runtime_error(
                std::string("No se pudo inicializar cURL: ") +
                curl_easy_strerror(result)
            );
        }
    }

    ~CurlGlobal() {
        curl_global_cleanup();
    }
};

void inicializarCurl() {
    static CurlGlobal curlGlobal;
    (void)curlGlobal;
}

std::string unirUrl(const std::string& baseUrl, const std::string& path) {
    std::string base = baseUrl;

    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    if (path.empty()) {
        return base;
    }

    if (path.front() == '/') {
        return base + path;
    }

    return base + "/" + path;
}

std::string codificarSegmento(const std::string& valor) {
    char* codificado = curl_easy_escape(
        nullptr,
        valor.c_str(),
        static_cast<int>(valor.size())
    );

    if (codificado == nullptr) {
        throw std::runtime_error("No se pudo codificar el identificador");
    }

    std::string resultado(codificado);
    curl_free(codificado);
    return resultado;
}

//agregamos el caPath a realizarPeticion  
json realizarPeticion(
    const std::string& url,
    const std::string& token,
    const std::string& caPath,
    const json* cuerpo,
    std::size_t (*writeCallback)(
        void*,
        std::size_t,
        std::size_t,
        void*
    )
) {
    inicializarCurl();

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle curl(curl_easy_init(), curl_easy_cleanup);

    if (!curl) {
        throw std::runtime_error("No se pudo crear la petición HTTP");
    }

    std::string respuesta;
    curl_slist* rawHeaders = nullptr;

    auto agregarHeader = [&rawHeaders](const std::string& valor) {
        curl_slist* siguiente = curl_slist_append(rawHeaders, valor.c_str());

        if (siguiente == nullptr) {
            curl_slist_free_all(rawHeaders);
            rawHeaders = nullptr;
            throw std::runtime_error("No se pudo preparar una cabecera HTTP");
        }

        rawHeaders = siguiente;
    };

    agregarHeader("Accept: application/json");

    if (cuerpo != nullptr) {
        agregarHeader("Content-Type: application/json");
    }

    if (!token.empty()) {
        const std::string valorToken =
            token.compare(0, 7, "Bearer ") == 0
                ? token
                : "Bearer " + token;

        agregarHeader("Authorization: " + valorToken);
    }

    using HeaderHandle =
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

    HeaderHandle headers(rawHeaders, curl_slist_free_all);

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());

    if (url.compare(0, 8, "https://") == 0) {
        if (caPath.empty()) {
            throw std::runtime_error(
                "Falta configurar la CA para la conexión HTTPS con Usuarios"
            );
        }

        curl_easy_setopt(curl.get(), CURLOPT_CAINFO, caPath.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    }

    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &respuesta);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 15L);

    std::string cuerpoSerializado;

    if (cuerpo != nullptr) {
        cuerpoSerializado = cuerpo->dump();

        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(
            curl.get(),
            CURLOPT_POSTFIELDS,
            cuerpoSerializado.c_str()
        );
        curl_easy_setopt(
            curl.get(),
            CURLOPT_POSTFIELDSIZE_LARGE,
            static_cast<curl_off_t>(cuerpoSerializado.size())
        );
    } else {
        curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
    }

    const CURLcode resultado = curl_easy_perform(curl.get());

    if (resultado != CURLE_OK) {
        throw std::runtime_error(
            std::string("Falló la petición HTTP: ") +
            curl_easy_strerror(resultado)
        );
    }

    long estadoHttp = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &estadoHttp);

    if (estadoHttp < 200 || estadoHttp >= 300) {
        throw std::runtime_error(
            "El servicio REST respondió HTTP " +
            std::to_string(estadoHttp) +
            (respuesta.empty() ? "" : ": " + respuesta)
        );
    }

    try {
        return json::parse(respuesta);
    } catch (const json::exception& e) {
        throw std::runtime_error(
            std::string("El servicio REST devolvió JSON inválido: ") +
            e.what()
        );
    }
}

} // namespace

//Ahora el constructor de la clase recibe el certificado de autenticacion 
UsuariosRestClient::UsuariosRestClient(
    const std::string& baseUrl, 
    const std::string& caPath
)
    : baseUrl_(baseUrl), caPath_(caPath) {
    if (baseUrl_.empty()) {
        throw std::invalid_argument(
            "La URL base de Usuarios no puede estar vacía :("
        );
    }
}

std::size_t UsuariosRestClient::writeCallback(
    void* contents,
    std::size_t size,
    std::size_t nmemb,
    void* userp
) {
    if (contents == nullptr || userp == nullptr) {
        return 0;
    }

    const std::size_t bytes = size * nmemb;
    auto* respuesta = static_cast<std::string*>(userp);

    try {
        respuesta->append(static_cast<char*>(contents), bytes);
    } catch (...) {
        // cURL informa el fallo de escritura al recibir 0.
        return 0;
    }

    return bytes;
}

json UsuariosRestClient::httpGet(
    const std::string& path,
    const std::string& token
) {
    return realizarPeticion(
        unirUrl(baseUrl_, path),
        token,
        caPath_,
        nullptr,
        &UsuariosRestClient::writeCallback
    );
}

json UsuariosRestClient::httpPost(
    const std::string& path,
    const json& body,
    const std::string& token
) {
    return realizarPeticion(
        unirUrl(baseUrl_, path),
        token,
        caPath_,
        &body,
        &UsuariosRestClient::writeCallback
    );
}

json UsuariosRestClient::obtenerUsuario(
    const std::string& userId,
    const std::string& token
) {
    return httpGet(
        "/api/v1/usuarios/" + codificarSegmento(userId),
        token
    );
}

json UsuariosRestClient::login(
    const std::string& username,
    const std::string& password
) {
    return httpPost(
        "/api/v1/login",
        {
            {"correo", username},
            {"password", password}
        }
    );
}

json UsuariosRestClient::me(const std::string& token) {
    return httpGet("/api/v1/perfil", token);
}