#include "pagos.hpp"

bool PagosStore::crear_cobro(const std::string& usuario_id,
                             const std::string& subasta_id,
                             int64_t monto_centimos,
                             const std::string& estado,
                             Cobro& out_cobro,
                             std::string& out_error) {
    if (subasta_id.empty()) {
        out_error = "subasta_id es obligatorio";
        return false;
    }

    // los montos siempre van en centimos enteros y deben ser positivos
    if (monto_centimos <= 0) {
        out_error = "monto_centimos debe ser mayor a cero";
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    Cobro c;
    c.id = "cob-" + std::to_string(siguiente_id_++);
    c.usuario_id = usuario_id;
    c.subasta_id = subasta_id;
    c.monto_centimos = monto_centimos;
    c.estado = estado.empty() ? "pagado" : estado;
    c.creado_en = std::time(nullptr);

    cobros_por_id_[c.id] = c;
    out_cobro = c;
    return true;
}

bool PagosStore::obtener_cobro(const std::string& cobro_id, Cobro& out_cobro) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = cobros_por_id_.find(cobro_id);
    if (it == cobros_por_id_.end()) {
        return false;
    }
    out_cobro = it->second;
    return true;
}
