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

#define BUFFER_SIZE 1024

struct time_pack {
    int64_t counter;  // Contador de segundos
    int clock_id; // Identificador do relógio
};

struct IPPort {
    std::string address;
    int port;
};

// Variáveis globais para drift e ajuste manual do tempo
std::atomic<double> drift_sec(1.0);  // Drift inicial em segundos
std::atomic<int64_t> counter(0);
std::mutex adjustment_mutex;

void adjust_drift(double new_drift_sec) {
    drift_sec.store(new_drift_sec);
}

void adjust_time_manually(int64_t new_counter_value) {
    std::lock_guard<std::mutex> lock(adjustment_mutex);
    counter.store(new_counter_value);
}

void udp_client(const std::vector<IPPort>& ip_ports, int clock_id) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Error: Unable to create socket" << std::endl;
        return;
    }

    time_pack packet;
    packet.clock_id = clock_id; // Definir o identificador do relógio

    while (true) {
        packet.counter = counter.load();

        for (const auto& ip_port : ip_ports) {
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(ip_port.port);

            if (inet_pton(AF_INET, ip_port.address.c_str(), &server_addr.sin_addr) <= 0) {
                std::cerr << "Invalid address: " << ip_port.address << std::endl;
                continue;
            }

            if (sendto(sockfd, &packet, sizeof(time_pack), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                std::cerr << "Error: Failed to send packet to " << ip_port.address << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(drift_sec.load()));
        counter.fetch_add(1);
    }

    close(sockfd);
}

void receive_time(std::string ip, int port) {
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
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

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

        std::cout << "Received counter from " << inet_ntoa(client_addr.sin_addr)
                  << " (Clock ID: " << packet.clock_id << ") at counter: " << packet.counter << std::endl;
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

int main(int argc, char* argv[]) {
    if (argc < 4 || (argc - 2) % 2 != 0) {
        std::cerr << "Usage: " << argv[0] << " <clock_id> <ip_address1> <port1> <ip_address2> <port2> ... <ip_addressN> <portN>" << std::endl;
        return 1;
    }

    int clock_id = std::stoi(argv[1]); // Identificador do relógio

    std::vector<IPPort> ip_ports;
    for (int i = 2; i < argc; i += 2) {
        IPPort ip_port;
        ip_port.address = argv[i];
        ip_port.port = std::stoi(argv[i + 1]);
        ip_ports.push_back(ip_port);
    }

    std::thread sender(udp_client, ip_ports, clock_id);
    std::thread receiver(receive_time, ip_ports[0].address, ip_ports[0].port); // Usar a porta do primeiro endereço IP para o receptor
    std::thread adjustment(manual_adjustment_thread);

    sender.join();
    receiver.join();
    adjustment.join();

    return 0;
}
