#include "subastas_service.hpp"
#include "auth_token.hpp"
#include <iostream>
#include <utility>

SubastasServiceImpl::SubastasServiceImpl(SubastaManager& manager,
                                         bool modo_blindado,
                                         std::string auth_token_secret)
    : manager_(manager),
      modo_blindado_(modo_blindado),
      auth_token_secret_(std::move(auth_token_secret)) {}

grpc::Status SubastasServiceImpl::ValidarAutorizacion(
    grpc::ServerContext* context,
    std::string* usuario_id) {
    if (!modo_blindado_) {
        return grpc::Status::OK;
    }

    const auto& metadatos = context->client_metadata();
    auto it = metadatos.find("authorization");
    if (it == metadatos.end()) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Acceso denegado por falta de cabecera authorization");
    }

    std::string token(it->second.data(), it->second.length());
    if (token.rfind("Bearer ", 0) != 0 || token.length() <= 7) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Token de autorizacion invalido");
    }

    std::string subject;
    std::time_t expires_at = 0;
    if (!auth_token::verify(token.substr(7), auth_token_secret_,
                            subject, expires_at)) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                            "Token invalido, manipulado o expirado");
    }

    if (usuario_id) {
        *usuario_id = std::move(subject);
    }
    return grpc::Status::OK;
}

grpc::Status SubastasServiceImpl::Crear(grpc::ServerContext* context, const subastas::v1::Subasta* request, subastas::v1::Subasta* response) {
    std::string usuario_id;
    auto estado_auth = ValidarAutorizacion(context, &usuario_id);
    if (!estado_auth.ok()) {
        return estado_auth;
    }
    if (modo_blindado_ && request->vendedor_id() != usuario_id) {
        return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                            "No puedes crear una subasta en nombre de otro usuario");
    }

    std::string error;
    if (!manager_.CrearSubasta(*request, *response, error)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, error);
    }

    return grpc::Status::OK;
}

grpc::Status SubastasServiceImpl::Pujar(grpc::ServerContext* context, const subastas::v1::PujarReq* request, subastas::v1::PujarResp* response) {
    std::string usuario_id;
    auto estado_auth = ValidarAutorizacion(context, &usuario_id);
    if (!estado_auth.ok()) {
        return estado_auth;
    }
    if (modo_blindado_ && request->usuario_id() != usuario_id) {
        return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                            "No puedes pujar en nombre de otro usuario");
    }

    manager_.ProcesarPuja(request->subasta_id(), request->usuario_id(), request->monto(), *response);
    return grpc::Status::OK;
}

grpc::Status SubastasServiceImpl::Obtener(grpc::ServerContext* context, const subastas::v1::SubastaId* request, subastas::v1::Subasta* response) {
    auto estado_auth = ValidarAutorizacion(context);
    if (!estado_auth.ok()) {
        return estado_auth;
    }

    if (!manager_.ObtenerSubasta(request->id(), *response)) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Subasta no encontrada");
    }

    return grpc::Status::OK;
}

grpc::Status SubastasServiceImpl::Seguir(grpc::ServerContext* context, const subastas::v1::SubastaId* request, grpc::ServerWriter<subastas::v1::Subasta>* writer) {
    auto estado_auth = ValidarAutorizacion(context);
    if (!estado_auth.ok()) {
        return estado_auth;
    }

    subastas::v1::Subasta estado_inicial;
    if (!manager_.ObtenerSubasta(request->id(), estado_inicial)) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Subasta no encontrada");
    }

    // Envio el primer estado al conectarse
    writer->Write(estado_inicial);

    auto cola = manager_.Suscribir(request->id());

    // Mantengo el flujo de datos activo
    while (!context->IsCancelled()) {
        subastas::v1::Subasta actualizacion;
        if (cola->EsperarSiguiente(actualizacion, 500)) {
            if (!writer->Write(actualizacion)) {
                break;
            }
        }
    }

    manager_.Desuscribir(request->id(), cola);
    return grpc::Status::OK;
}
