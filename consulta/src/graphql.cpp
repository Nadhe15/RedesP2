
#include "graphql.hpp"

#include <cctype>
#include <stdexcept>
#include <cstdint>
#include <google/protobuf/util/time_util.h>

using json = nlohmann::json;

GraphQLEngine::GraphQLEngine(SubastasGrpcClient& grpc,
                             UsuariosRestClient& rest,
                             bool isBlindado)
    : grpc_(grpc), rest_(rest), isBlindado_(isBlindado) {}

// PARSER

std::vector<std::string> GraphQLEngine::tokenize(
    const std::string& query) const {
    std::vector<std::string> tokens;

    for (std::size_t i = 0; i < query.size();) {
        char c = query[i];

        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
        } else if (c == '#') {
            while (i < query.size() && query[i] != '\n') ++i;
        } else if (c == '"') {
            std::string value;
            ++i;
            while (i < query.size() && query[i] != '"')
                value += query[i++];
            if (i < query.size()) ++i;
            tokens.push_back("\"" + value + "\"");
        } else if (std::isalnum(static_cast<unsigned char>(c)) ||
                   c == '_' || c == '-' || c == '.') {
            std::string word;
            while (i < query.size()) {
                char ch = query[i];
                if (!std::isalnum(static_cast<unsigned char>(ch)) &&
                    ch != '_' && ch != '-' && ch != '.') break;
                word += query[i++];
            }
            tokens.push_back(word);
        } else if (std::string("{}():!$[]@").find(c) != std::string::npos) {
            tokens.emplace_back(1, c);
            ++i;
        } else {
            throw std::runtime_error("Caracter no valido.");
        }
    }
    return tokens;
}

std::string GraphQLEngine::parseValue(
    const std::vector<std::string>& tokens,
    std::size_t& pos) const {
    if (pos >= tokens.size() || tokens[pos] == "$" || tokens[pos] == "[")
        throw std::runtime_error("Valor no soportado.");
    std::string value = tokens[pos++];
    if (value.size() >= 2 && value.front() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}

void GraphQLEngine::parseArguments(
    const std::vector<std::string>& tokens,
    std::size_t& pos,
    ASTNode& node,
    const std::string& prefix) const {
    std::string end = tokens[pos++] == "(" ? ")" : "}";

    while (pos < tokens.size() && tokens[pos] != end) {
        std::string key = tokens[pos++];
        if (pos >= tokens.size() || tokens[pos++] != ":")
            throw std::runtime_error("Argumento no valido.");

        if (!prefix.empty()) key = prefix + "." + key;

        if (pos < tokens.size() && tokens[pos] == "{")
            parseArguments(tokens, pos, node, key);
        else
            node.arguments[key] = parseValue(tokens, pos);
    }

    if (pos >= tokens.size())
        throw std::runtime_error("Argumentos sin cerrar.");
    ++pos;
}

std::vector<ASTNode> GraphQLEngine::parseSelectionSet(
    const std::vector<std::string>& tokens,
    std::size_t& pos) const {
    if (pos >= tokens.size() || tokens[pos++] != "{")
        throw std::runtime_error("Falta '{'.");

    std::vector<ASTNode> nodes;

    while (pos < tokens.size() && tokens[pos] != "}") {
        ASTNode node;
        node.name = tokens[pos++];

        if (node.name == "...")
            throw std::runtime_error("Fragmentos no soportados.");

        if (pos < tokens.size() && tokens[pos] == ":") {
            node.alias = node.name;
            ++pos;
            node.name = tokens.at(pos++);
        }

        if (pos < tokens.size() && tokens[pos] == "(")
            parseArguments(tokens, pos, node);

        if (pos < tokens.size() && tokens[pos] == "{")
            node.children = parseSelectionSet(tokens, pos);

        nodes.push_back(node);
    }

    if (pos >= tokens.size())
        throw std::runtime_error("Seleccion sin cerrar.");
    ++pos;
    return nodes;
}

ASTNode GraphQLEngine::parse(const std::string& query) const {
    auto tokens = tokenize(query);
    if (tokens.empty()) throw std::runtime_error("Consulta vacia.");

    ASTNode root;
    std::size_t pos = 0;
    root.name = "query";

    if (tokens[pos] == "query" || tokens[pos] == "mutation" ||
        tokens[pos] == "subscription") {
        root.name = tokens[pos++];

        if (pos < tokens.size() && tokens[pos] != "{") {
            if (tokens[pos] == "(")
                throw std::runtime_error("Variables no soportadas.");
            ++pos;
        }
    }

    root.children = parseSelectionSet(tokens, pos);
    if (pos != tokens.size())
        throw std::runtime_error("Consulta no valida.");

    return root;
}

// EJECUCION Y SEGURIDAD

json GraphQLEngine::execute(const std::string& query,
                            const std::string& token) {
    try {
        ASTNode root = parse(query);

        if (root.name == "subscription")
            throw std::runtime_error("Use WebSocket para suscripciones.");

        if (isBlindado_) {
            if (Security::hasIntrospection(root))
                throw std::runtime_error("[API8] Introspeccion bloqueada.");

            if (Security::calculateDepth(root) > 4 ||
                Security::calculateComplexity(root) > 20)
                throw std::runtime_error("[API4] Consulta demasiado compleja.");

            if (Security::countAliases(root) > 5)
                throw std::runtime_error("[API6] Demasiados alias.");
        }

        json data = json::object();

        for (const auto& field : root.children) {
            std::string key = field.alias.empty() ? field.name : field.alias;

            if (field.name == "__schema" || field.name == "__type") {
                data[key] = {{"types", {
                    {{"name", "Subasta"}}, {{"name", "Usuario"}},
                    {{"name", "Query"}}, {{"name", "Mutation"}},
                    {{"name", "Subscription"}}
                }}};
            } else if (root.name == "query") {
                data[key] = resolveQuery(field, token);
            } else if (root.name == "mutation") {
                data[key] = resolveMutation(field, token);
            } else {
                throw std::runtime_error("Operacion no reconocida.");
            }
        }

        return {{"data", data}};
    } catch (const std::exception& e) {
        return {{"errors", {{{"message", e.what()}}}}};
    }
}

// RESOLVERS

json GraphQLEngine::resolveQuery(const ASTNode& field,
                                 const std::string& token) {
    if (field.name == "subasta") return resolveSubasta(field, token);
    if (field.name == "me") return rest_.me(token);
    return nullptr;
}

json GraphQLEngine::resolveMutation(const ASTNode& field,
                                    const std::string& token) {
    if (field.name == "login")
        return rest_.login(field.arguments.at("username"),
                           field.arguments.at("pass"));

    json user = rest_.me(token);
    std::string userId;

    if (user.contains("id") && user["id"].is_string())
        userId = user["id"];
    else if (user.contains("usuario_id") && user["usuario_id"].is_string())
        userId = user["usuario_id"];

    if (userId.empty())
        throw std::runtime_error("No se pudo identificar al usuario.");

    if (field.name == "crearSubasta") {
        google::protobuf::Timestamp close;
        if (!google::protobuf::util::TimeUtil::FromString(
                field.arguments.at("input.cierre"), &close))
            throw std::runtime_error("Fecha de cierre no valida.");

        subastas::v1::Subasta result;
        std::string error;

        if (!grpc_.Crear(
                field.arguments.at("input.titulo"),
                std::stoll(field.arguments.at("input.precio_salida")),
                std::stoll(field.arguments.at("input.incremento_minimo")),
                userId, close, token, result, error))
            throw std::runtime_error(error);

        return subastaToJson(result, field.children, token);
    }

    if (field.name == "pujar") {
        subastas::v1::PujarResp result;
        std::string error;

        if (!grpc_.Pujar(
                field.arguments.at("input.subasta_id"), userId,
                std::stoll(field.arguments.at("input.monto")),
                token, result, error))
            throw std::runtime_error(error);

        json auction = nullptr;
        for (const auto& child : field.children) {
            if (child.name == "subasta" && result.has_subasta())
                auction = subastaToJson(
                    result.subasta(), child.children, token);
        }

        return {
            {"aceptada", result.aceptada()},
            {"motivo", result.motivo()},
            {"subasta", auction}
        };
    }

    return nullptr;
}

json GraphQLEngine::resolveSubasta(const ASTNode& field,
                                   const std::string& token) {
    subastas::v1::Subasta result;
    std::string error;

    if (!grpc_.Obtener(field.arguments.at("id"), token, result, error))
        throw std::runtime_error(error);

    return subastaToJson(result, field.children, token);
}

json GraphQLEngine::subastaToJson(
    const subastas::v1::Subasta& s,
    const std::vector<ASTNode>& fields,
    const std::string& token) {
    std::string state = "ESTADO_DESCONOCIDO";
    if (s.estado() == subastas::v1::ABIERTA) state = "ABIERTA";
    if (s.estado() == subastas::v1::CERRADA) state = "CERRADA";

    json result = {
        {"id", s.id()}, {"vendedor_id", s.vendedor_id()},
        {"titulo", s.titulo()}, {"precio_salida", s.precio_salida()},
        {"incremento_minimo", s.incremento_minimo()},
        {"precio_actual", s.precio_actual()},
        {"lider_id", s.lider_id()},
        {"cierre", google::protobuf::util::TimeUtil::ToString(s.cierre())},
        {"estado", state}
    };

    for (const auto& field : fields) {
        if (field.name == "vendedor")
            result["vendedor"] = rest_.obtenerUsuario(s.vendedor_id(), token);
    }
    return result;
}

// SUSCRIPCION gRPC

void GraphQLEngine::startSubscription(
    const std::string& query,
    const std::string& token,
    std::function<void(const nlohmann::json&)> onUpdate,
    std::function<void(const grpc::Status&)> onError) {
    try {
        ASTNode root = parse(query);

        if (root.name != "subscription" || root.children.size() != 1 ||
            root.children[0].name != "seguirSubasta")
            throw std::runtime_error("Suscripcion no valida.");

        if (isBlindado_ &&
            (Security::hasIntrospection(root) ||
             Security::calculateDepth(root) > 4 ||
             Security::calculateComplexity(root) > 20))
            throw std::runtime_error("Suscripcion bloqueada por seguridad.");

        const auto& field = root.children[0];

        grpc_.Seguir(field.arguments.at("id"), token,
            [this, fields = field.children, token, onUpdate]
            (const subastas::v1::Subasta& update) {
                if (onUpdate)
                    onUpdate(subastaToJson(update, fields, token));
            }, onError);
    } catch (const std::exception& e) {
        if (onError)
            onError(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what()));
    }
}
