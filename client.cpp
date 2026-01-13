#include <iostream>
#include <string>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <thread>

#include "conn.h"
#include "conn_factory.h"
#include "logger.h"

std::atomic<bool> running(true);
std::atomic<std::chrono::steady_clock::time_point> last_message_time(std::chrono::steady_clock::now());

void signalHandler(int sig) {
    (void)sig;
    running = false;
}

void runClient(const std::string& conn_type, int client_id) {
    std::unique_ptr<Conn> conn_to_host = ConnFactory::createFromClient(conn_type, client_id, false);
    std::unique_ptr<Conn> conn_from_host = ConnFactory::createToClient(conn_type, client_id, false);
    
    if (!conn_to_host || !conn_from_host) {
        std::cerr << "Client " << client_id << ": Failed to create connections" << std::endl;
        return;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (conn_type == "fifo" || conn_type == "sock") {
        if (conn_type == "fifo") {
            if (!conn_to_host->Open()) {
                std::cerr << "Client " << client_id << ": Failed to open connection to host" << std::endl;
                return;
            }

            if (!conn_from_host->Open()) {
                std::cerr << "Client " << client_id << ": Failed to open connection from host" << std::endl;
                return;
            }
        } else {

            if (!conn_to_host->Open()) {
                std::cerr << "Client " << client_id << ": Failed to open connection to host" << std::endl;
                return;
            }
            if (!conn_from_host->Open()) {
                std::cerr << "Client " << client_id << ": Failed to open connection from host" << std::endl;
                return;
            }
        }
    }
    
    ChatMessage join_msg;
    join_msg.type = -1; // Сигнал присоединения
    join_msg.sender_id = client_id;
    join_msg.receiver_id = 0;
    join_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    strncpy(join_msg.text, "joined", sizeof(join_msg.text) - 1);
    conn_to_host->Write(&join_msg, sizeof(join_msg));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ChatMessage ready_msg;
    ready_msg.type = -1; 
    ready_msg.sender_id = client_id;
    ready_msg.receiver_id = 0;
    ready_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    strncpy(ready_msg.text, "ready", sizeof(ready_msg.text) - 1);
    conn_to_host->Write(&ready_msg, sizeof(ready_msg));
    std::cout << "Client " << client_id << " ready" << std::endl;
    
    std::thread read_thread([&]() {
        ChatMessage msg;
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (conn_from_host->Read(&msg, sizeof(msg))) {
                if (msg.type == 0) {
                    std::cout << "Client " << client_id << " received broadcast: " << msg.text << std::endl;
                } else if (msg.type == 1) {
                    std::cout << "Client " << client_id << " received host private to " << client_id << ": " << msg.text << std::endl;
                }
            }
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ChatMessage listen_msg;
    listen_msg.type = -2;
    listen_msg.sender_id = client_id;
    listen_msg.receiver_id = 0;
    listen_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    strncpy(listen_msg.text, "listening", sizeof(listen_msg.text) - 1);
    conn_to_host->Write(&listen_msg, sizeof(listen_msg));
    
    std::thread timeout_thread([&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto now = std::chrono::steady_clock::now();
            auto last_msg = last_message_time.load();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_msg).count();
            
            if (elapsed >= 60) {
                Logger::getInstance().logError("Client: No messages sent for 60 seconds, terminating");
                kill(getpid(), SIGKILL);
            }
        }
    });
    
    // Основной цикл
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    read_thread.join();
    timeout_thread.join();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <mq|fifo|sock> <client_id>" << std::endl;
        return 1;
    }
    
    std::string conn_type = argv[1];
    int client_id = 0;
    
    if (argc >= 3) {
        try {
            client_id = std::stoi(argv[2]);
        } catch (...) {
            std::cerr << "Invalid client ID" << std::endl;
            return 1;
        }
    }
    
    if (conn_type != "mq" && conn_type != "fifo" && conn_type != "sock") {
        std::cerr << "Invalid connection type. Use: mq, fifo, or sock" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    runClient(conn_type, client_id);
    
    return 0;
}
