#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <atomic>
#include <mutex>

const char * portstr = getenv("PORT");
int port = std::stoi(portstr);

const char * masterPortstr = getenv("MPORT");
int mport = std::stoi(masterPortstr);

#define PORT port
#define BUFFER_SIZE 1024
#define MASTER_PORT mport

struct time_pack {
    int64_t counter;  // Contador de segundos
    int clock_id;     // Identificador do relógio
};

// Variáveis globais para drift e ajuste manual do tempo
std::atomic<double> drift_sec(1.0);  // Drift inicial em segundos
std::atomic<int64_t> counter(0);
std::atomic<int> master_id(-1); // ID do relógio mestre
std::mutex adjustment_mutex;
std::string master_ip;

void adjust_drift(double new_drift_sec) {
    drift_sec.store(new_drift_sec);
}

void adjust_time_manually(int64_t new_counter_value) {
    std::lock_guard<std::mutex> lock(adjustment_mutex);
    counter.store(new_counter_value);
}

void send_time_packets(const std::vector<std::string>& ip_addresses, int clock_id) {
    int sockfd;
    struct sockaddr_in server_addr;
    time_pack packet;
    packet.clock_id = clock_id; // Definir o identificador do relógio

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Error: Unable to create socket" << std::endl;
        return;
    }

    while (true) {
        packet.counter = counter.load();

        for (const auto& ip_address : ip_addresses) {
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT); // Usar a porta constante

            if (inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr) <= 0) {
                std::cerr << "Invalid address: " << ip_address << std::endl;
                continue;
            }

            if (sendto(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                std::cerr << "Error: Failed to send packet to " << ip_address << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(drift_sec.load()));
        counter.fetch_add(1);
    }

    close(sockfd);
}

void receive_time(const std::vector<std::string>& ip_addresses, int clock_id) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    time_pack packet;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Error: Unable to create socket" << std::endl;
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT); // Usar a porta constante
    server_addr.sin_addr.s_addr = inet_addr(ip_addresses[0].c_str());

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Unable to bind socket" << std::endl;
        close(sockfd);
        return;
    }

    while (true) {
        ssize_t n = recvfrom(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&client_addr, &addr_len);
        if (n < 0) {
            std::cerr << "Error: Failed to receive packet" << std::endl;
            continue;
        }

        // Atualizar o contador do relógio mestre se necessário
        if (packet.counter > counter.load() + 1) {
            {
                std::lock_guard<std::mutex> lock(adjustment_mutex);
                master_id.store(packet.clock_id);
                // Atualizar o IP do master
                master_ip = inet_ntoa(client_addr.sin_addr);
            }
        }

        // Exibir o contador apenas se for do próprio relógio
        if (packet.clock_id == clock_id) {
            std::cout << "Own counter: " << counter.load() << std::endl;
        }
    }

    close(sockfd);
}

void sync_time_with_master(const std::vector<std::string>& ip_addresses, int clock_id) {
    int sockfd;
    struct sockaddr_in server_addr;
    time_pack packet;
    packet.clock_id = clock_id;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Error: Unable to create socket" << std::endl;
        return;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Checar o mestre a cada 5 segundos

        int current_master_id = master_id.load();
        if (current_master_id == -1) {
            continue; // Mestre não definido ainda
        }

        std::cout << "Synchronizing with master Clock ID: " << current_master_id << std::endl;

        // Configurar o endereço do mestre
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(MASTER_PORT); // Porta do mestre

        if (inet_pton(AF_INET, master_ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address: " << master_ip << std::endl;
            continue;
        }

        packet.counter = -1; // Indica uma solicitação de sincronização
        if (sendto(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Error: Failed to send sync request to " << master_ip << std::endl;
        }

        // Esperar resposta do mestre e ajustar o tempo local
        ssize_t n = recvfrom(sockfd, &packet, sizeof(time_pack), 0, NULL, NULL);
        if (n > 0 && packet.counter != -1) {
            std::lock_guard<std::mutex> lock(adjustment_mutex);
            if (packet.counter > counter.load()) {
                counter.store(packet.counter);
            } else {
                master_id.store(-1);
            }
            std::cout << "Time synchronized with master. New counter: " << counter.load() << std::endl;
        }
    }

    close(sockfd);
}

void handle_sync_request() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    time_pack packet;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Error: Unable to create socket" << std::endl;
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MASTER_PORT); // Porta para escutar solicitações de sincronização
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Unable to bind socket" << std::endl;
        close(sockfd);
        return;
    }

    while (true) {
        ssize_t n = recvfrom(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&client_addr, &addr_len);
        if (n < 0) {
            std::cerr << "Error: Failed to receive packet" << std::endl;
            continue;
        }

        if (packet.counter == -1) {
            // Responder à solicitação de sincronização com o contador atual
            packet.counter = counter.load();
            std::cout << "Received sync request from " << inet_ntoa(client_addr.sin_addr)
                      << " (Clock ID: " << packet.clock_id << ") at counter: " << counter.load() << std::endl;
            if (sendto(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
                std::cerr << "Error: Failed to send sync response" << std::endl;
            }
        }
    }

    close(sockfd);
}

void manual_adjustment_thread() {
    while (true) {
        std::cout << "Enter command (drift <sec> | adjust <value_sec>): ";
        std::string command;
        std::cin >> command;
        if (command == "drift") {
            double new_drift;
            std::cin >> new_drift;
            adjust_drift(new_drift);
            std::cout << "Drift adjusted to: " << drift_sec.load() << " sec" << std::endl;
        } else if (command == "adjust") {
            int64_t new_value;
            std::cin >> new_value;
            adjust_time_manually(new_value);
            std::cout << "Counter adjusted to: " << new_value << " sec" << std::endl;
        } else {
            std::cout << "Unknown command!" << std::endl;
        }
    }
}

int main() {
    const char* clock_id_env = std::getenv("CLOCK_ID");
    const char* ip_addresses_env = std::getenv("IP_ADDRESSES");

    if (!clock_id_env || !ip_addresses_env) {
        std::cerr << "Environment variables CLOCK_ID and IP_ADDRESSES must be set." << std::endl;
        return 1;
    }

    int clock_id = std::stoi(clock_id_env); // Identificador do relógio

    std::vector<std::string> ip_addresses;
    std::string ip_addresses_str(ip_addresses_env);
    size_t pos = 0;

    // Processa a lista de IPs separados por vírgulas
    while ((pos = ip_addresses_str.find(',')) != std::string::npos) {
        ip_addresses.push_back(ip_addresses_str.substr(0, pos));
        ip_addresses_str.erase(0, pos + 1);
    }

    // Adicione o último IP, se houver
    if (!ip_addresses_str.empty()) {
        ip_addresses.push_back(ip_addresses_str);
    }

    // Criar e iniciar as threads
    std::thread sender(send_time_packets, ip_addresses, clock_id);
    std::thread receiver(receive_time, ip_addresses, clock_id);
    std::thread adjustment(manual_adjustment_thread);
    std::thread sync(sync_time_with_master, ip_addresses, clock_id);
    std::thread sync_handler(handle_sync_request);

    sender.join();
    receiver.join();
    adjustment.join();
    sync.join();
    sync_handler.join();

    return 0;
}
