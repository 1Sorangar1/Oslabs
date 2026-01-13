#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <cstdlib>

#include "conn.h"
#include "conn_factory.h"
#include "logger.h"

const int NUM_CLIENTS = 3;

std::atomic<bool> running(true);
std::vector<pid_t> client_pids;

struct ClientConnection {
    int id;
    std::unique_ptr<Conn> conn_to_client;
    std::unique_ptr<Conn> conn_from_client;
    bool ready;
    bool listening;
    
    ClientConnection(int client_id) : id(client_id), ready(false), listening(false) {}
};

void signalHandler(int sig) {
    (void)sig;
    running = false;
    for (pid_t pid : client_pids) {
        if (pid > 0) {
            kill(pid, SIGTERM);
        }
    }
}

std::string extractConnType(const std::string& program_name) {
    std::filesystem::path p(program_name);
    std::string filename = p.filename().string();
    
    if (filename.find("_mq") != std::string::npos) {
        return "mq";
    } else if (filename.find("_fifo") != std::string::npos) {
        return "fifo";
    } else if (filename.find("_sock") != std::string::npos) {
        return "sock";
    }
    
    return "";
}

void runHost(const std::string& conn_type) {
    std::cout << "Chat server starting" << std::endl;
    
    std::vector<ClientConnection> clients;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.emplace_back(i);
        clients[i].conn_to_client = ConnFactory::createToClient(conn_type, i, true);
        clients[i].conn_from_client = ConnFactory::createFromClient(conn_type, i, true);
        
        if (!clients[i].conn_to_client || !clients[i].conn_from_client) {
            std::cerr << "Failed to create connections for client " << i << std::endl;
            return;
        }
    }
    
    std::filesystem::path host_exe_path;
    char host_path[1024];
    ssize_t len = readlink("/proc/self/exe", host_path, sizeof(host_path) - 1);
    if (len != -1) {
        host_path[len] = '\0';
        host_exe_path = std::filesystem::path(host_path);
    } else {
        std::cerr << "Failed to get executable path" << std::endl;
        return;
    }
    
    std::filesystem::path build_dir = host_exe_path.parent_path();
    std::string client_exe_name = "client_" + conn_type;
    std::filesystem::path client_exe_path = build_dir / client_exe_name;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "Failed to fork client " << i << std::endl;
            continue;
        }
        
        if (pid == 0) {
            std::string client_id_str = std::to_string(i);
            std::string client_path = client_exe_path.string();
            
            execl(client_path.c_str(), client_exe_name.c_str(), conn_type.c_str(), client_id_str.c_str(), nullptr);
            
            std::string alt_path = "./build/" + client_exe_name;
            execl(alt_path.c_str(), client_exe_name.c_str(), conn_type.c_str(), client_id_str.c_str(), nullptr);
            
            alt_path = "./" + client_exe_name;
            execl(alt_path.c_str(), client_exe_name.c_str(), conn_type.c_str(), client_id_str.c_str(), nullptr);
            
            std::cerr << "Failed to exec client: " << strerror(errno) << std::endl;
            exit(1);
        }
        
        client_pids.push_back(pid);
        std::cout << "Client " << i << " waiting for messages" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    if (conn_type == "fifo" || conn_type == "sock") {
        std::vector<std::thread> open_threads;
        for (auto& client : clients) {
            open_threads.emplace_back([&client, conn_type]() {
                if (conn_type == "fifo") {
                    if (client.conn_from_client) {
                        client.conn_from_client->Open();
                    }
                    if (client.conn_to_client) {
                        client.conn_to_client->Open();
                    }
                } else {
                    if (client.conn_to_client) {
                        client.conn_to_client->Open();
                    }
                    if (client.conn_from_client) {
                        client.conn_from_client->Open();
                    }
                }
            });
        }
        for (auto& thread : open_threads) {
            thread.join();
        }
    }
    
    std::vector<std::thread> read_threads;
    std::atomic<int> ready_count(0);
    std::atomic<int> listening_count(0);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        read_threads.emplace_back([&, i]() {
            ChatMessage msg;
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (clients[i].conn_from_client && clients[i].conn_from_client->Read(&msg, sizeof(msg))) {
                    if (msg.type == -1) { // Сигнал присоединения/готовности
                        std::string text = std::string(msg.text);
                        if (text == "joined" && !clients[i].ready) {
                            clients[i].ready = true;
                            std::cout << "Client " << i << " joined" << std::endl;
                            ready_count++;
                        }
                        // "ready" обрабатывается клиентом
                    } else if (msg.type == -2) { // Сигнал начала прослушивания
                        if (!clients[i].listening) {
                            clients[i].listening = true;
                            std::cout << "Client " << i << " started listening" << std::endl;
                            listening_count++;
                        }
                    } else {
                    }
                }
            }
        });
    }
    
    while (ready_count < NUM_CLIENTS && running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "All clients confirmed readiness" << std::endl;
    
    while (listening_count < NUM_CLIENTS && running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "All clients started listening" << std::endl;
    std::cout << "Chat server ready. Commands:" << std::endl;
    std::cout << "  all:<message>       - broadcast to all" << std::endl;
    std::cout << "  to <id>:<message>   - send to specific client (0-" << (NUM_CLIENTS - 1) << ")" << std::endl;
    std::cout << "  quit                - shutdown server" << std::endl;
    
    std::string input;
    while (running) {
        std::getline(std::cin, input);
        
        if (!running) break;
        
        if (input.empty()) continue;
        
        if (input == "quit") {
            running = false;
            break;
        }
        
        ChatMessage msg;
        msg.sender_id = 0; // Хост
        msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (input.substr(0, 4) == "all:") {
            std::string message = input.substr(4);
            if (!message.empty() && message[0] == ' ') {
                message = message.substr(1);
            }
            
            msg.type = 0; 
            msg.receiver_id = 0;
            strncpy(msg.text, message.c_str(), sizeof(msg.text) - 1);
            msg.text[sizeof(msg.text) - 1] = '\0';
            
            std::cout << "Host broadcast: " << message << std::endl;
            
            for (auto& client : clients) {
                if (client.conn_to_client) {
                    client.conn_to_client->Write(&msg, sizeof(msg));
                }
            }
        } else if (input.substr(0, 3) == "to ") {
            size_t colon_pos = input.find(':', 3);
            if (colon_pos != std::string::npos) {
                std::string id_str = input.substr(3, colon_pos - 3);
                id_str.erase(0, id_str.find_first_not_of(" \t"));
                id_str.erase(id_str.find_last_not_of(" \t") + 1);
                
                try {
                    int client_id = std::stoi(id_str);
                    if (client_id >= 0 && client_id < NUM_CLIENTS) {
                        std::string message = input.substr(colon_pos + 1);
                        if (!message.empty() && message[0] == ' ') {
                            message = message.substr(1);
                        }
                        
                        msg.type = 1;
                        msg.receiver_id = client_id;
                        strncpy(msg.text, message.c_str(), sizeof(msg.text) - 1);
                        msg.text[sizeof(msg.text) - 1] = '\0';
                        
                        std::cout << "Host private to " << client_id << ": " << message << std::endl;
                        
                        if (clients[client_id].conn_to_client) {
                            clients[client_id].conn_to_client->Write(&msg, sizeof(msg));
                        }
                    }
                } catch (...) {
                }
            }
        }
    }
    
    for (auto& thread : read_threads) {
        thread.join();
    }
    
    for (pid_t pid : client_pids) {
        if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

int main(int argc, char* argv[]) {
    std::string conn_type;
    
    if (argc > 0 && argv[0] != nullptr) {
        conn_type = extractConnType(argv[0]);
    }
    
    if (conn_type.empty()) {
        std::cerr << "Error: Cannot determine connection type from program name" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    runHost(conn_type);
    
    return 0;
}
