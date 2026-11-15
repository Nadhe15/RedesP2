#pragma once

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

struct Cobro {
    std::string id;
    std::string usuario_id;
    std::string subasta_id;
    int64_t monto_centimos = 0;
    std::string estado = "pagado";
    std::time_t creado_en = 0;
};

// almaceno los cobros simulados en memoria con proteccion de concurrencia
class PagosStore {
public:
    bool crear_cobro(const std::string& usuario_id,
                     const std::string& subasta_id,
                     int64_t monto_centimos,
                     const std::string& estado,
                     Cobro& out_cobro,
                     std::string& out_error);

    bool obtener_cobro(const std::string& cobro_id, Cobro& out_cobro);

private:
    std::mutex mtx_;
    int siguiente_id_ = 1;
    std::unordered_map<std::string, Cobro> cobros_por_id_;
};
