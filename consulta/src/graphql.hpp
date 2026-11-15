
#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>
#include "rest_client.hpp"
#include "security.hpp"
#include "grpc_client.hpp"

using json = nlohmann::json;

class GraphQLEngine {
public:
    GraphQLEngine(
        SubastasGrpcClient& grpc,
        UsuariosRestClient& rest,
        bool isBlindado
    );

    // Recibe una consulta GraphQL y devuelve su resultado JSON
    json execute(
        const std::string& query,
        const std::string& token
    );

private:
    SubastasGrpcClient& grpc_;
    UsuariosRestClient& rest_;
    bool isBlindado_;

    // Parser de consultas GraphQL
    ASTNode parse(const std::string& query);

    std::vector<ASTNode> parseSelections(
        std::istringstream& ss
    );

    std::map<std::string, std::string> parseArguments(
        const std::string& raw
    );

    // Resolvers
    json resolveQuery(
        const ASTNode& node,
        const std::string& token
    );

    json resolveMutation(
        const ASTNode& node,
        const std::string& token
    );

    json resolveSubasta(
        const ASTNode& field,
        const std::string& token
    );

    // Convierte una subasta de Protobuf a JSON
    json subastaToJson(
        const subastas::v1::Subasta& s,
        const std::vector<ASTNode>& selectedFields,
        const std::string& token
    );
};
