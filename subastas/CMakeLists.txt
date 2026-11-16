# Indico la version minima y el estandar de C++
cmake_minimum_required(VERSION 3.15)
project(ServicioSubastas CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Busco las librerias necesarias
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Genero los archivos de protobuf y grpc
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/subastas.proto)

get_filename_component(PROTO_PATH "${PROTO_FILE}" PATH)
get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)

set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")

find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

add_custom_command(
      OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
      COMMAND protobuf::protoc
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${PROTO_PATH}"
        --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${PROTO_FILE}"
      DEPENDS "${PROTO_FILE}")

include_directories("${CMAKE_CURRENT_BINARY_DIR}" src)

# Defino el ejecutable del servidor
add_executable(servidor_subastas
    src/main.cpp
    src/subasta_manager.cpp
    src/subastas_service.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# Enlazo las dependencias del proyecto
target_link_libraries(servidor_subastas
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
    pthread
)
