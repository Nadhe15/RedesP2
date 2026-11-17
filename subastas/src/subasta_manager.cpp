#include "subasta_manager.hpp"
#include <iostream>

void ColaSuscriptor::Enviar(const subastas::v1::Subasta& subasta) {
    std::lock_guard<std::mutex> lock(mutex_);
    cola_.push(subasta);
    cv_.notify_one();
}

bool ColaSuscriptor::EsperarSiguiente(subastas::v1::Subasta& subasta, int tiempo_espera_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool llego = cv_.wait_for(lock, std::chrono::milliseconds(tiempo_espera_ms), [this] {
        return !cola_.empty() || cancelado_;
    });

    if (cancelado_ || !llego || cola_.empty()) {
        return false;
    }

    subasta = cola_.front();
    cola_.pop();
    return true;
}

void ColaSuscriptor::Cancelar() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelado_ = true;
    cv_.notify_all();
}

SubastaManager::SubastaManager() {
    // Inicio la revision automatica por tiempo
    hilo_tiempo_ = std::thread(&SubastaManager::HiloRevisionCierres, this);
}

SubastaManager::~SubastaManager() {
    activo_ = false;
    if (hilo_tiempo_.joinable()) {
        hilo_tiempo_.join();
    }
}

void SubastaManager::VerificarCierre(subastas::v1::Subasta& subasta) {
    if (subasta.estado() != subastas::v1::ABIERTA) {
        return;
    }

    auto ahora = std::chrono::system_clock::now();
    int64_t seg_ahora = std::chrono::duration_cast<std::chrono::seconds>(ahora.time_since_epoch()).count();

    if (subasta.has_cierre() && seg_ahora >= subasta.cierre().seconds()) {
        subasta.set_estado(subastas::v1::CERRADA);
    }
}

bool SubastaManager::CrearSubasta(subastas::v1::Subasta subasta, subastas::v1::Subasta& subasta_creada, std::string& motivo_error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (subasta.titulo().empty()) {
        motivo_error = "El titulo de la subasta no puede estar vacio";
        return false;
    }
    if (subasta.vendedor_id().empty()) {
        motivo_error = "El identificador del vendedor es obligatorio";
        return false;
    }
    if (subasta.precio_salida() <= 0) {
        motivo_error = "El precio de salida debe ser mayor a cero";
        return false;
    }
    if (subasta.incremento_minimo() <= 0) {
        motivo_error = "El incremento minimo debe ser mayor a cero";
        return false;
    }

    // Asigno un identificador unico si viene vacio
    if (subasta.id().empty()) {
        subasta.set_id("subasta-" + std::to_string(contador_id_++));
    }

    if (subastas_.find(subasta.id()) != subastas_.end()) {
        motivo_error = "Ya existe una subasta con ese identificador";
        return false;
    }

    // Asigno cierre por defecto si no lo enviaron
    if (!subasta.has_cierre()) {
        auto ahora = std::chrono::system_clock::now() + std::chrono::hours(1);
        int64_t seg = std::chrono::duration_cast<std::chrono::seconds>(ahora.time_since_epoch()).count();
        subasta.mutable_cierre()->set_seconds(seg);
        subasta.mutable_cierre()->set_nanos(0);
    }

    subasta.set_estado(subastas::v1::ABIERTA);
    subasta.set_precio_actual(subasta.precio_salida());
    subasta.clear_lider_id();

    subastas_[subasta.id()] = subasta;
    subasta_creada = subasta;
    return true;
}

bool SubastaManager::ProcesarPuja(const std::string& subasta_id, const std::string& usuario_id, int64_t monto, subastas::v1::PujarResp& resp) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = subastas_.find(subasta_id);
    if (it == subastas_.end()) {
        resp.set_aceptada(false);
        resp.set_motivo("Subasta no encontrada");
        return false;
    }

    subastas::v1::Subasta& subasta = it->second;
    VerificarCierre(subasta);

    if (subasta.estado() == subastas::v1::CERRADA) {
        resp.set_aceptada(false);
        resp.set_motivo("La subasta ya cerro");
        *resp.mutable_subasta() = subasta;
        return false;
    }

    // Evito que el vendedor puje en su propia publicacion
    if (usuario_id == subasta.vendedor_id()) {
        resp.set_aceptada(false);
        resp.set_motivo("El vendedor no puede pujar en su propia subasta");
        *resp.mutable_subasta() = subasta;
        return false;
    }

    // Calculo el monto minimo necesario
    int64_t minimo_valido = 0;
    if (subasta.lider_id().empty()) {
        minimo_valido = subasta.precio_salida();
    } else {
        minimo_valido = subasta.precio_actual() + subasta.incremento_minimo();
    }

    if (monto < minimo_valido) {
        resp.set_aceptada(false);
        resp.set_motivo("El monto no supera el precio actual y el incremento minimo");
        *resp.mutable_subasta() = subasta;
        return false;
    }

    // Registro el nuevo lider y precio
    subasta.set_precio_actual(monto);
    subasta.set_lider_id(usuario_id);

    resp.set_aceptada(true);
    resp.set_motivo("Puja aceptada");
    *resp.mutable_subasta() = subasta;

    // Emito la actualizacion a todos los que escuchan
    NotificarSuscriptores(subasta);
    return true;
}

bool SubastaManager::ObtenerSubasta(const std::string& id, subastas::v1::Subasta& subasta) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = subastas_.find(id);
    if (it == subastas_.end()) {
        return false;
    }

    auto estado_anterior = it->second.estado();
    VerificarCierre(it->second);

    if (estado_anterior != it->second.estado()) {
        NotificarSuscriptores(it->second);
    }

    subasta = it->second;
    return true;
}

std::shared_ptr<ColaSuscriptor> SubastaManager::Suscribir(const std::string& subasta_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto cola = std::make_shared<ColaSuscriptor>();
    suscriptores_[subasta_id].push_back(cola);
    return cola;
}

void SubastaManager::Desuscribir(const std::string& subasta_id, std::shared_ptr<ColaSuscriptor> cola) {
    std::lock_guard<std::mutex> lock(mutex_);
    cola->Cancelar();

    auto it = suscriptores_.find(subasta_id);
    if (it != suscriptores_.end()) {
        auto& lista = it->second;
        for (auto iter = lista.begin(); iter != lista.end(); ++iter) {
            if (*iter == cola) {
                lista.erase(iter);
                break;
            }
        }
        if (lista.empty()) {
            suscriptores_.erase(it);
        }
    }
}

void SubastaManager::NotificarSuscriptores(const subastas::v1::Subasta& subasta) {
    auto it = suscriptores_.find(subasta.id());
    if (it != suscriptores_.end()) {
        for (const auto& cola : it->second) {
            cola->Enviar(subasta);
        }
    }
}

void SubastaManager::HiloRevisionCierres() {
    while (activo_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!activo_) break;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& par : subastas_) {
            auto estado_anterior = par.second.estado();
            VerificarCierre(par.second);
            if (estado_anterior != par.second.estado()) {
                NotificarSuscriptores(par.second);
            }
        }
    }
}
