#pragma once

#include "subastas.grpc.pb.h"
#include "subasta_manager.hpp"

// Implemento el servicio gRPC de subastas
class SubastasServiceImpl final : public subastas::v1::Subastas::Service {
public:
    SubastasServiceImpl(SubastaManager& manager, bool modo_blindado);

    // Recibo la peticion para crear una subasta
    grpc::Status Crear(grpc::ServerContext* context, const subastas::v1::Subasta* request, subastas::v1::Subasta* response) override;

    // Recibo y proceso una puja enviada
    grpc::Status Pujar(grpc::ServerContext* context, const subastas::v1::PujarReq* request, subastas::v1::PujarResp* response) override;

    // Consulto la subasta solicitada
    grpc::Status Obtener(grpc::ServerContext* context, const subastas::v1::SubastaId* request, subastas::v1::Subasta* response) override;

    // Emito las actualizaciones en vivo por streaming
    grpc::Status Seguir(grpc::ServerContext* context, const subastas::v1::SubastaId* request, grpc::ServerWriter<subastas::v1::Subasta>* writer) override;

private:
    // Compruebo los permisos del cliente
    grpc::Status ValidarAutorizacion(grpc::ServerContext* context);

    SubastaManager& manager_;
    bool modo_blindado_{false};
};
