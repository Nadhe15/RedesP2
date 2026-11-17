#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <thread>
#include "subastas.grpc.pb.h"

// Manejo la cola de actualizaciones para cada suscriptor
class ColaSuscriptor {
public:
    void Enviar(const subastas::v1::Subasta& subasta);
    bool EsperarSiguiente(subastas::v1::Subasta& subasta, int tiempo_espera_ms);
    void Cancelar();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<subastas::v1::Subasta> cola_;
    bool cancelado_{false};
};

// Administro el estado y las operaciones de las subastas
class SubastaManager {
public:
    SubastaManager();
    ~SubastaManager();

    // Registro una nueva subasta
    bool CrearSubasta(subastas::v1::Subasta subasta, subastas::v1::Subasta& subasta_creada, std::string& motivo_error);

    // Valido y proceso una puja entrante
    bool ProcesarPuja(const std::string& subasta_id, const std::string& usuario_id, int64_t monto, subastas::v1::PujarResp& resp);

    // Obtengo los datos actuales de una subasta
    bool ObtenerSubasta(const std::string& id, subastas::v1::Subasta& subasta);

    // Gestiono las suscripciones al stream
    std::shared_ptr<ColaSuscriptor> Suscribir(const std::string& subasta_id);
    void Desuscribir(const std::string& subasta_id, std::shared_ptr<ColaSuscriptor> cola);

private:
    // Compruebo si el tiempo de la subasta expiro
    void VerificarCierre(subastas::v1::Subasta& subasta);

    // Reviso periodicamente los cierres en segundo plano
    void HiloRevisionCierres();

    // Notifico a los clientes que siguen la subasta
    void NotificarSuscriptores(const subastas::v1::Subasta& subasta);

    std::mutex mutex_;
    std::unordered_map<std::string, subastas::v1::Subasta> subastas_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<ColaSuscriptor>>> suscriptores_;

    int contador_id_{1};
    std::atomic<bool> activo_{true};
    std::thread hilo_tiempo_;
};
