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

#include "conn_mq.h"
#include "conn_fifo.h"
#include "conn_sock.h"
#include "logger.h"

std::atomic<bool> running(true);
pid_t client_pid = 0;

void signalHandler(int sig) {
    (void)sig;
    running = false;
    if (client_pid > 0) {
        kill(client_pid, SIGTERM);
    }
}

std::string extractConnType(const std::string& program_name) {
    std::filesystem::path p(program_name);
    std::string filename = p.filename().string();

    if (filename.find("_mq") != std::string::npos) {
        return "mq";
    }
    else if (filename.find("_fifo") != std::string::npos) {
        return "fifo";
    }
    else if (filename.find("_sock") != std::string::npos) {
        return "sock";
    }

    return "";
}

void runHost(const std::string& conn_type) {
    Logger::getInstance().logInfo("=== Host: Starting chat with connection type: " + conn_type + " ===");

    std::unique_ptr<Conn> conn_to_client;
    std::unique_ptr<Conn> conn_from_client;

    if (conn_type == "mq") {
        Logger::getInstance().logInfo("Host: Creating message queue connections...");
        conn_to_client = std::make_unique<ConnMQ>("host_to_client", true);
        conn_from_client = std::make_unique<ConnMQ>("client_to_host", true);
    }
    else if (conn_type == "fifo") {
        Logger::getInstance().logInfo("Host: Creating FIFO connections...");
        conn_to_client = std::make_unique<ConnFIFO>("host_to_client", true);
        conn_from_client = std::make_unique<ConnFIFO>("client_to_host", true);
    }
    else if (conn_type == "sock") {
        Logger::getInstance().logInfo("Host: Creating socket connections...");
        conn_to_client = std::make_unique<ConnSock>("host_to_client", true);
        conn_from_client = std::make_unique<ConnSock>("client_to_host", true);
    }
    else {
        Logger::getInstance().logError("Host: Unknown connection type: " + conn_type);
        return;
    }

    Logger::getInstance().logInfo("Host: Connections created successfully");

    pid_t pid = fork();
    if (pid == -1) {
        Logger::getInstance().logError("Host: Failed to fork: " + std::string(strerror(errno)));
        return;
    }

    if (pid == 0) {
        char path[1024];
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len != -1) {
            path[len] = '\0';
            execl(path, path, "client", conn_type.c_str(), nullptr);
        }

        std::string exec_path = "./build/host_" + conn_type;
        if (access(exec_path.c_str(), X_OK) != 0) {
            exec_path = "./host_" + conn_type;
        }
        execl(exec_path.c_str(), exec_path.c_str(), "client", conn_type.c_str(), nullptr);
        Logger::getInstance().logError("Host: Failed to exec client: " + std::string(strerror(errno)));
        exit(1);
    }

    client_pid = pid;
    Logger::getInstance().logInfo("Host: Client started with PID: " + std::to_string(pid));

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    if (conn_type == "fifo") {
        Logger::getInstance().logInfo("Host: Opening FIFO connections...");
        if (!static_cast<ConnFIFO*>(conn_to_client.get())->Open()) {
            Logger::getInstance().logError("Host: Failed to open FIFO to client");
            return;
        }
        if (!static_cast<ConnFIFO*>(conn_from_client.get())->Open()) {
            Logger::getInstance().logError("Host: Failed to open FIFO from client");
            return;
        }
        Logger::getInstance().logInfo("Host: FIFO connections opened");
    }
    else if (conn_type == "sock") {
        Logger::getInstance().logInfo("Host: Accepting socket connections...");
        if (!static_cast<ConnSock*>(conn_to_client.get())->Open()) {
            Logger::getInstance().logError("Host: Failed to accept connection to client");
            return;
        }
        if (!static_cast<ConnSock*>(conn_from_client.get())->Open()) {
            Logger::getInstance().logError("Host: Failed to accept connection from client");
            return;
        }
        Logger::getInstance().logInfo("Host: Socket connections accepted");
    }

    std::thread read_thread([&]() {
        ChatMessage msg;
        while (running) {
            if (conn_from_client->Read(&msg, sizeof(msg))) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::string time_str = std::ctime(&time_t);
                time_str.pop_back();

                if (msg.type == 0) {
                    // Общее сообщение
                    Logger::getInstance().log("[" + time_str + "] Client: " + std::string(msg.text));
                    Logger::getInstance().logInfo("Host: Received general message from client");
                }
                else {
                    // Личное сообщение
                    Logger::getInstance().log("[" + time_str + "] Client -> Host (private): " + std::string(msg.text));
                    Logger::getInstance().logInfo("Host: Received private message from client");
                }
            }
        }
        });

    std::string input;
    while (running) {
        std::cout << "Host> ";
        std::getline(std::cin, input);

        if (!running) break;

        if (input.empty()) continue;

        if (input == "/quit") {
            running = false;
            break;
        }

        ChatMessage msg;
        msg.sender_id = 0;
        msg.receiver_id = 0;
        msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (input[0] == '@') {
            size_t space_pos = input.find(' ');
            if (space_pos != std::string::npos) {
                std::string target = input.substr(1, space_pos - 1);
                msg.type = 1;

                if (target == "client" || target == "1") {
                    msg.receiver_id = 1;
                }
                else {
                    try {
                        msg.receiver_id = std::stoi(target);
                    }
                    catch (...) {
                        msg.receiver_id = 1;
                    }
                }

                strncpy(msg.text, input.substr(space_pos + 1).c_str(), sizeof(msg.text) - 1);
                msg.text[sizeof(msg.text) - 1] = '\0';

                Logger::getInstance().logInfo("Host: Sending private message to client " + std::to_string(msg.receiver_id) + ": " + std::string(msg.text));

                if (conn_to_client->Write(&msg, sizeof(msg))) {
                    auto now = std::chrono::system_clock::now();
                    auto time_t = std::chrono::system_clock::to_time_t(now);
                    std::string time_str = std::ctime(&time_t);
                    time_str.pop_back();
                    Logger::getInstance().log("[" + time_str + "] Host -> Client " + std::to_string(msg.receiver_id) + " (private): " + std::string(msg.text));
                    Logger::getInstance().logInfo("Host: Private message sent successfully to client " + std::to_string(msg.receiver_id));
                }
                else {
                    Logger::getInstance().logError("Host: Failed to send private message to client " + std::to_string(msg.receiver_id));
                }
            }
        }
        else {
            msg.type = 0;
            strncpy(msg.text, input.c_str(), sizeof(msg.text) - 1);
            msg.text[sizeof(msg.text) - 1] = '\0';

            Logger::getInstance().logInfo("Host: Sending general message to all clients: " + std::string(msg.text));

            if (conn_to_client->Write(&msg, sizeof(msg))) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::string time_str = std::ctime(&time_t);
                time_str.pop_back();
                Logger::getInstance().log("[" + time_str + "] Host: " + std::string(msg.text));
                Logger::getInstance().logInfo("Host: General message sent successfully to all clients");
            }
            else {
                Logger::getInstance().logError("Host: Failed to send general message to clients");
            }
        }
    }

    read_thread.join();

    int status;
    waitpid(pid, &status, 0);
    Logger::getInstance().logInfo("Host: Client finished");
}

std::atomic<std::chrono::steady_clock::time_point> last_message_time(std::chrono::steady_clock::now());

void runClient(const std::string& conn_type) {
    Logger::getInstance().logInfo("=== Client: Starting chat with connection type: " + conn_type + " ===");

    std::unique_ptr<Conn> conn_to_host;
    std::unique_ptr<Conn> conn_from_host;

    if (conn_type == "mq") {
        Logger::getInstance().logInfo("Client: Opening message queue connections...");
        conn_to_host = std::make_unique<ConnMQ>("client_to_host", false);
        conn_from_host = std::make_unique<ConnMQ>("host_to_client", false);
    }
    else if (conn_type == "fifo") {
        Logger::getInstance().logInfo("Client: Opening FIFO connections...");
        conn_to_host = std::make_unique<ConnFIFO>("client_to_host", false);
        conn_from_host = std::make_unique<ConnFIFO>("host_to_client", false);
    }
    else if (conn_type == "sock") {
        Logger::getInstance().logInfo("Client: Opening socket connections...");
        conn_to_host = std::make_unique<ConnSock>("client_to_host", false);
        conn_from_host = std::make_unique<ConnSock>("host_to_client", false);
    }
    else {
        Logger::getInstance().logError("Client: Unknown connection type: " + conn_type);
        return;
    }

    if (conn_to_host == nullptr || conn_from_host == nullptr) {
        Logger::getInstance().logError("Client: Failed to create connections");
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (conn_type == "fifo") {
        Logger::getInstance().logInfo("Client: Opening FIFO connections...");
        if (!static_cast<ConnFIFO*>(conn_to_host.get())->Open() ||
            !static_cast<ConnFIFO*>(conn_from_host.get())->Open()) {
            Logger::getInstance().logError("Client: Failed to open FIFO connections");
            return;
        }
        Logger::getInstance().logInfo("Client: FIFO connections opened");
    }
    else if (conn_type == "sock") {
        Logger::getInstance().logInfo("Client: Connecting to host sockets...");
        if (!static_cast<ConnSock*>(conn_to_host.get())->Open() ||
            !static_cast<ConnSock*>(conn_from_host.get())->Open()) {
            Logger::getInstance().logError("Client: Failed to connect to host");
            return;
        }
        Logger::getInstance().logInfo("Client: Socket connections established");
    }

    Logger::getInstance().logInfo("Client: Connections opened successfully");
    Logger::getInstance().logInfo("Client: Connected to host");

    std::thread read_thread([&]() {
        ChatMessage msg;
        while (running) {
            if (conn_from_host->Read(&msg, sizeof(msg))) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::string time_str = std::ctime(&time_t);
                time_str.pop_back();

                if (msg.type == 0) {
                    // Общее сообщение
                    Logger::getInstance().log("[" + time_str + "] Host: " + std::string(msg.text));
                    Logger::getInstance().logInfo("Client: Received general message from host");
                }
                else {
                    // Личное сообщение
                    Logger::getInstance().log("[" + time_str + "] Host -> Client (private): " + std::string(msg.text));
                    Logger::getInstance().logInfo("Client: Received private message from host");
                }
            }
        }
        });

    std::thread timeout_thread([&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto now = std::chrono::steady_clock::now();
            auto last_msg = last_message_time.load();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_msg).count();

            if (elapsed >= 60) {
                Logger::getInstance().logError("Client: No messages sent for 60 seconds, terminating");
                running = false;
                // Отправляем сообщение о выходе хосту перед завершением
                ChatMessage exit_msg;
                exit_msg.type = -1;
                exit_msg.sender_id = 1;
                strncpy(exit_msg.text, "timeout_exit", sizeof(exit_msg.text) - 1);
                conn_to_host->Write(&exit_msg, sizeof(exit_msg));
                exit(0);
            }
        }
        });

    std::string input;
    while (running) {
        std::cout << "Client> ";
        std::getline(std::cin, input);

        if (!running) break;

        if (input.empty()) continue;

        if (input == "/quit") {
            running = false;
            break;
        }

        ChatMessage msg;
        msg.sender_id = 1;
        msg.receiver_id = 0;
        msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (input[0] == '@') {
            size_t space_pos = input.find(' ');
            if (space_pos != std::string::npos) {
                msg.type = 1; // Личное сообщение
                msg.receiver_id = 0; // Хост
                strncpy(msg.text, input.substr(space_pos + 1).c_str(), sizeof(msg.text) - 1);
                msg.text[sizeof(msg.text) - 1] = '\0';

                Logger::getInstance().logInfo("Client: Sending private message to host: " + std::string(msg.text));

                if (conn_to_host->Write(&msg, sizeof(msg))) {
                    last_message_time = std::chrono::steady_clock::now();
                    auto now = std::chrono::system_clock::now();
                    auto time_t = std::chrono::system_clock::to_time_t(now);
                    std::string time_str = std::ctime(&time_t);
                    time_str.pop_back();
                    Logger::getInstance().log("[" + time_str + "] Client -> Host (private): " + std::string(msg.text));
                    Logger::getInstance().logInfo("Client: Private message sent successfully to host");
                }
                else {
                    Logger::getInstance().logError("Client: Failed to send private message to host");
                }
            }
        }
        else {
            // Общее сообщение
            msg.type = 0;
            strncpy(msg.text, input.c_str(), sizeof(msg.text) - 1);
            msg.text[sizeof(msg.text) - 1] = '\0';

            Logger::getInstance().logInfo("Client: Sending general message to host: " + std::string(msg.text));

            if (conn_to_host->Write(&msg, sizeof(msg))) {
                last_message_time = std::chrono::steady_clock::now();
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::string time_str = std::ctime(&time_t);
                time_str.pop_back();
                Logger::getInstance().log("[" + time_str + "] Client: " + std::string(msg.text));
                Logger::getInstance().logInfo("Client: General message sent successfully to host");
            }
            else {
                Logger::getInstance().logError("Client: Failed to send general message to host");
            }
        }
    }

    read_thread.join();
    timeout_thread.join();

    Logger::getInstance().logInfo("Client: Shutting down");
}

int main(int argc, char* argv[]) {
    std::string mode = "host";
    std::string conn_type;

    if (argc > 0 && argv[0] != nullptr) {
        conn_type = extractConnType(argv[0]);
    }

    if (conn_type.empty()) {
        if (argc >= 2) {
            conn_type = argv[1];
        }
        else {
            std::cerr << "Error: Cannot determine connection type from program name" << std::endl;
            std::cerr << "Usage: " << argv[0] << " [client] [mq|fifo|sock]" << std::endl;
            return 1;
        }
    }

    if (argc >= 2 && std::string(argv[1]) == "client") {
        mode = "client";
        if (conn_type.empty() && argc >= 3) {
            conn_type = argv[2];
        }
    }

    if (conn_type != "mq" && conn_type != "fifo" && conn_type != "sock") {
        std::cerr << "Invalid connection type: " << conn_type << std::endl;
        std::cerr << "Usage: " << argv[0] << " [client]" << std::endl;
        return 1;
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if (mode == "client") {
        Logger::getInstance().logInfo("Client: Starting with connection type: " + conn_type);
        runClient(conn_type);
    }
    else {
        Logger::getInstance().logInfo("Host: Starting with connection type: " + conn_type);
        runHost(conn_type);
    }

    return 0;
}
[file content end]