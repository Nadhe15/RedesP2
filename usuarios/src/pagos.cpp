#include "pagos.hpp"

bool PagosStore::crear_cobro(const std::string& usuario_id,
                             const std::string& subasta_id,
                             int64_t monto_centimos,
                             const std::string& estado,
                             const std::string& idempotency_key,
                             bool modo_vulnerable,
                             Cobro& out_cobro,
                             std::string& out_error,
                             int& out_status) {
    if (subasta_id.empty()) {
        out_error = "subasta_id es obligatorio";
        out_status = 400;
        return false;
    }

    // los montos siempre van en centimos enteros y deben ser positivos
    if (monto_centimos <= 0) {
        out_error = "monto_centimos debe ser mayor a cero";
        out_status = 400;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);

    // en modo blindado exijo Idempotency-Key y rechazo peticiones repetidas
    if (!modo_vulnerable) {
        if (idempotency_key.empty()) {
            out_error = "falta header Idempotency-Key";
            out_status = 400;
            return false;
        }

        std::string clave_compuesta = usuario_id + ":" + idempotency_key;
        if (idempotencia_.count(clave_compuesta) > 0) {
            out_error = "clave de idempotencia ya utilizada (replay bloqueado)";
            out_status = 409;
            return false;
        }
    }

    Cobro c;
    c.id = "cob-" + std::to_string(siguiente_id_++);
    c.usuario_id = usuario_id;
    c.subasta_id = subasta_id;
    c.monto_centimos = monto_centimos;
    c.estado = estado.empty() ? "pagado" : estado;
    c.creado_en = std::time(nullptr);

    cobros_por_id_[c.id] = c;
    if (!modo_vulnerable && !idempotency_key.empty()) {
        idempotencia_[usuario_id + ":" + idempotency_key] = c.id;
    }

    out_cobro = c;
    out_status = 201;
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
