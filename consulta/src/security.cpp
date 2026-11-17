
#include "security.hpp"
#include <algorithm>

// API8: Detectar solicitudes de introspeccion
bool Security::hasIntrospection(const ASTNode& node) {
    if (node.name == "__schema" || node.name == "__type") {
        return true;
    }

    for (const auto& child : node.children) {
        if (hasIntrospection(child)) {
            return true;
        }
    }

    return false;
}

// API4: Calcular la profundidad de la consulta
int Security::calculateDepth(const ASTNode& node, int current) {
    if (node.children.empty()) {
        return current;
    }

    int maxDepth = current;

    for (const auto& child : node.children) {
        maxDepth = std::max(
            maxDepth,
            calculateDepth(child, current + 1)
        );
    }

    return maxDepth;
}

// API4: Calcular la complejidad de la consulta
int Security::calculateComplexity(const ASTNode& node) {
    int complexity = 1;

    if (node.name == "query" ||
        node.name == "mutation" ||
        node.name == "subscription") {
        complexity = 0;
    }

    for (const auto& child : node.children) {
        complexity += calculateComplexity(child);
    }

    return complexity;
}

// API6: Contar los alias utilizados
int Security::countAliases(const ASTNode& node) {
    int count = node.alias.empty() ? 0 : 1;

    for (const auto& child : node.children) {
        count += countAliases(child);
    }

    return count;
}
