
#pragma once

// CLIENTE gRPC -> Servicio Subastas 


#include <grpcpp/grpcpp.h>
#include "subastas.grpc.pb.h"
#include <google/protobuf/timestamp.pb.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <mutex>
#include <thread> 

using subastas::v1::Subastas;
using subastas::v1::Subasta;
using subastas::v1::SubastaId;
using subastas::v1::PujarReq;
using subastas::v1::PujarResp;

class SubastasGrpcClient {
public:
    explicit SubastasGrpcClient(
        const std::string& target,
        const std::string& caPath,
        bool usarTls
    );

    ~SubastasGrpcClient();

    // Obtener una subasta por ID
    bool Obtener(
        const std::string& id,
        const std::string& token,
        Subasta& out,
        std::string& error
    );

    void CancelarSeguir();

    // Crear una nueva subasta
    bool Crear(
        const std::string& titulo,
        std::int64_t precio_salida,
        std::int64_t incremento_minimo,
        const std::string& vendedor_id,
        const google::protobuf::Timestamp& cierre,
        const std::string& token,
        Subasta& out,
        std::string& error
    );

    // Enviar una puja
    bool Pujar(
        const std::string& subasta_id,
        const std::string& usuario_id,
        std::int64_t monto,
        const std::string& token,
        PujarResp& out,
        std::string& error
    );

    // Consumir el stream de actualizaciones de una subasta
    void Seguir(
        const std::string& id,
        const std::string& token,
        std::function<void(const Subasta&)> onUpdate,
        std::function<void(const grpc::Status&)> onError
    );

private:
    std::unique_ptr<Subastas::Stub> stub_;

    //certificado de autenticacion 
    std::string caPath_;

    // Control de la subscripcion activa
    std::thread streamThread_;
    std::unique_ptr<grpc::ClientContext> streamContext_;
    std::mutex streamMutex_;

    // Propagar el token mediante metadata gRPC
    void setToken(
        grpc::ClientContext& ctx,
        const std::string& token
    );
};
