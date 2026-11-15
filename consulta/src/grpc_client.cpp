
#include "grpc_client.hpp"

#include <thread>
#include <utility>

// Se tiene que utilizar TLS/mTLS.
// Puse InsecureChannelCredentials() para las
// pruebas de comunicación sin cifrado.
//
// Probablemente cuando se configure TLS, coordinar certificados y configuración
// con Rol 1 y sustituir las credenciales correspondientes.


SubastasGrpcClient::SubastasGrpcClient(const std::string& target) {
    auto channel = grpc::CreateChannel(
        target,
        grpc::InsecureChannelCredentials() // cambiaaaaar.
    );

    stub_ = Subastas::NewStub(channel);
}

// Destructor: cancelar y esperar cualquier suscripción activa
SubastasGrpcClient::~SubastasGrpcClient() {
    CancelarSeguir();
}

// Propagar el token Bearer mediante metadata gRPC
void SubastasGrpcClient::setToken(
    grpc::ClientContext& ctx,
    const std::string& token
) {
    if (!token.empty()) {
        std::string value =
            (token.rfind("Bearer ", 0) == 0)
                ? token
                : "Bearer " + token;

        ctx.AddMetadata("authorization", value);
    }
}

// Obtener una subasta por ID
bool SubastasGrpcClient::Obtener(
    const std::string& id,
    const std::string& token,
    Subasta& out,
    std::string& error
) {
    grpc::ClientContext ctx;
    setToken(ctx, token);

    SubastaId req;
    req.set_id(id);

    grpc::Status status = stub_->Obtener(&ctx, req, &out);

    if (!status.ok()) {
        error = status.error_message();
        return false;
    }

    error.clear();
    return true;
}

// Crear una nueva subasta
bool SubastasGrpcClient::Crear(
    const std::string& titulo,
    std::int64_t precio_salida,
    std::int64_t incremento_minimo,
    const std::string& vendedor_id,
    const google::protobuf::Timestamp& cierre,
    const std::string& token,
    Subasta& out,
    std::string& error
) {
    grpc::ClientContext ctx;
    setToken(ctx, token);

    Subasta req;

    req.set_titulo(titulo);
    req.set_precio_salida(precio_salida);
    req.set_incremento_minimo(incremento_minimo);
    req.set_vendedor_id(vendedor_id);

    *req.mutable_cierre() = cierre;

    grpc::Status status = stub_->Crear(&ctx, req, &out);

    if (!status.ok()) {
        error = status.error_message();
        return false;
    }

    error.clear();
    return true;
}

// Enviar una puja
bool SubastasGrpcClient::Pujar(
    const std::string& subasta_id,
    const std::string& usuario_id,
    std::int64_t monto,
    const std::string& token,
    PujarResp& out,
    std::string& error
) {
    grpc::ClientContext ctx;
    setToken(ctx, token);

    PujarReq req;

    req.set_subasta_id(subasta_id);
    req.set_usuario_id(usuario_id);
    req.set_monto(monto);

    grpc::Status status = stub_->Pujar(&ctx, req, &out);

    if (!status.ok()) {
        error = status.error_message();
        return false;
    }

    error.clear();
    return true;
}

// Consumir las actualizaciones de una subasta
void SubastasGrpcClient::Seguir(
    const std::string& id,
    const std::string& token,
    std::function<void(const Subasta&)> onUpdate,
    std::function<void(const grpc::Status&)> onError
) {
    // Cancelar cualquier suscripción anterior antes de iniciar otra
    CancelarSeguir();

    std::lock_guard<std::mutex> lock(streamMutex_);

    streamContext_ = std::make_unique<grpc::ClientContext>();
    setToken(*streamContext_, token);

    grpc::ClientContext* ctx = streamContext_.get();

    streamThread_ = std::thread(
        [this, id, ctx, onUpdate, onError]() {

            SubastaId req;
            req.set_id(id);

            auto reader = stub_->Seguir(ctx, req);

            Subasta update;

            while (reader->Read(&update)) {
                if (onUpdate) {
                    onUpdate(update);
                }
            }

            grpc::Status status = reader->Finish();

            if (!status.ok() && onError) {
                onError(status);
            }
        }
    );
}

// Cancelar la suscripción y esperar a que termine el hilo
void SubastasGrpcClient::CancelarSeguir() {
    std::thread threadToJoin;

    {
        std::lock_guard<std::mutex> lock(streamMutex_);

        if (streamContext_) {
            streamContext_->TryCancel();
        }

        if (streamThread_.joinable()) {
            threadToJoin = std::move(streamThread_);
        }
    }

    // Esperar fuera del mutex para evitar bloqueos
    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }

    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streamContext_.reset();
    }
}
