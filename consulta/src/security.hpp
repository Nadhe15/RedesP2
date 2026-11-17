
#pragma once

#include <string>
#include <vector>
#include <map>

// Tengo que volver a revisarlo 

// Nodo utilizado para representar una consulta GraphQL
struct ASTNode {
    std::string alias;
    std::string name;
    std::map<std::string, std::string> arguments;
    std::vector<ASTNode> children;
};

class Security {
public:
    // API8: Detectar solicitudes de introspección
    static bool hasIntrospection(const ASTNode& root);

    // API4: Calcular la profundidad de la consulta
    static int calculateDepth(const ASTNode& node, int current = 0);

        // API4: Calcular complejidad de la consulta
    static int calculateComplexity(const ASTNode& node);

    // API6: Contar alias utilizados
    static int countAliases(const ASTNode& node);
};
