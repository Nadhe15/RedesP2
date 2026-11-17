
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <functional>
#include "security.hpp"
#include "grpc_client.hpp"
#include "rest_client.hpp"

class GraphQLEngine {
public:
    GraphQLEngine(
        SubastasGrpcClient& grpc,
        UsuariosRestClient& rest,
        bool isBlindado
    );

    // Recibir una consulta GraphQL y devolver el resultado
    nlohmann::json execute(
        const std::string& query,
        const std::string& token
    );

    void startSubscription(
    const std::string& query,
    const std::string& token,
    std::function<void(const nlohmann::json&)> onUpdate,
    std::function<void(const grpc::Status&)> onError
);

private:
    SubastasGrpcClient& grpc_;
    UsuariosRestClient& rest_;
    bool isBlindado_;

    // Convertir el texto GraphQL en tokens
    std::vector<std::string> tokenize(
        const std::string& query
    ) const;

    // Construir el arbol de la consulta
    ASTNode parse(
        const std::string& query
    ) const;

    // Leer los campos seleccionados
    std::vector<ASTNode> parseSelectionSet(
        const std::vector<std::string>& tokens,
        std::size_t& position
    ) const;

    // Leer argumentos, incluyendo objetos input anidados
    void parseArguments(
        const std::vector<std::string>& tokens,
        std::size_t& position,
        ASTNode& node,
        const std::string& prefix = ""
    ) const;

    // Leer un valor de un argumento
    std::string parseValue(
        const std::vector<std::string>& tokens,
        std::size_t& position
    ) const;

    // Ejecutar consultas
    nlohmann::json resolveQuery(
        const ASTNode& field,
        const std::string& token
    );

    // Ejecutar mutaciones
    nlohmann::json resolveMutation(
        const ASTNode& field,
        const std::string& token
    );

    // Consultar una subasta mediante gRPC
    nlohmann::json resolveSubasta(
        const ASTNode& field,
        const std::string& token
    );

    // Convertir una respuesta Protobuf en JSON
    nlohmann::json subastaToJson(
        const subastas::v1::Subasta& subasta,
        const std::vector<ASTNode>& selectedFields,
        const std::string& token
    );
};
