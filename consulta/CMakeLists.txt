
cmake_minimum_required(VERSION 3.16)
project(ConsultaGraphQL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Crow: servidor HTTP y WebSocket
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.0+5
)
FetchContent_MakeAvailable(Crow)

# JSON
find_package(nlohmann_json 3.11 QUIET)

if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Dependencias del sistema
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Servicio Consulta
add_executable(consulta
    src/main.cpp
    src/graphql.cpp
    src/grpc_client.cpp
    src/rest_client.cpp
    src/security.cpp
    generated/subastas.pb.cc
    generated/subastas.grpc.pb.cc
)

# Carpetas de cabeceras
target_include_directories(consulta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

# Bibliotecas
target_link_libraries(consulta PRIVATE
    Crow::Crow
    nlohmann_json::nlohmann_json
    gRPC::grpc++
    protobuf::libprotobuf
    CURL::libcurl
    Threads::Threads
)