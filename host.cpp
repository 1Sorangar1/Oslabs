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
//                           ХОСТ 
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
	// Порождаем родственного клиента
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
	if (conn_type == "fifo" || conn_type == "sock") {
		Logger::getInstance().logInfo("Host: Opening connections...");
		if (!conn_from_client->Open()) {
			Logger::getInstance().logError("Host: Failed to open connection from client");
			return;
		}
		if (!conn_to_client->Open()) {
			Logger::getInstance().logError("Host: Failed to open connection to client");
			return;
		}
	}
	// Ожидани
	ChatMessage join_msg;
	if (conn_from_client->Read(&join_msg, sizeof(join_msg))) {
		if (join_msg.type == -1 && std::string(join_msg.text) == "joined") {
			Logger::getInstance().logInfo("Host: Client joined");
		}
	}
	// Поток для чтения сообщений от клиента
	std::thread read_thread([&]() {
		ChatMessage msg;
		while (running) {
			if (conn_from_client->Read(&msg, sizeof(msg))) {
				auto time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point(std::chrono::milliseconds(msg.timestamp)));
				std::string time_str = std::ctime(&time_t);
				time_str.pop_back();
				if (msg.type == -3 && std::string(msg.text) == "quit") {
					Logger::getInstance().logInfo("Host: Received quit from client");
					running = false;
					return; 
				}
				if (msg.type == 0) {
					//std::cout << "[" << time_str << "] Client: " << msg.text << std::endl;
					Logger::getInstance().logInfo("Host received broadcast from client: " + std::string(msg.text));
				}
				else if (msg.type == 1 && msg.receiver_id == 0) {
					std::cout << "[" << time_str << "] Private from Client: " << msg.text << std::endl;
					Logger::getInstance().logInfo("Host received private from client: " + std::string(msg.text));
				}

		  }
		  std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	});
	// Основной цикл для ввода и отправки сообщений
	while (running) {
		std::string input;
		std::cout << "Host: ";
		if (std::getline(std::cin, input)) {
			if (input == "/quit") {
				ChatMessage quit_msg;
				quit_msg.type = -3;  // Специальный тип для quit
				quit_msg.sender_id = 0;
				quit_msg.receiver_id = 1;
				quit_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();
				strncpy(quit_msg.text, "quit", sizeof(quit_msg.text) - 1);
				conn_to_client->Write(&quit_msg, sizeof(quit_msg));

				running = false;
				continue;
			}

			if (input.empty()) continue;
			ChatMessage msg;
			msg.sender_id = 0;  // Host ID
			msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			size_t space_pos = input.find(' ');
			if (input.rfind("@ ", 0) == 0 && space_pos != std::string::npos) {
				// Private message to client
				msg.type = 1;
				msg.receiver_id = 1;  // Client ID
				strncpy(msg.text, input.substr(space_pos + 1).c_str(), sizeof(msg.text) - 1);
				msg.text[sizeof(msg.text) - 1] = '\0';
			}
			else {
				// Broadcast
				msg.type = 0;
				msg.receiver_id = 0;
				strncpy(msg.text, input.c_str(), sizeof(msg.text) - 1);
				msg.text[sizeof(msg.text) - 1] = '\0';
			}
			if (conn_to_client->Write(&msg, sizeof(msg))) {
				Logger::getInstance().logInfo("Host sent message: " + input);
			}
			else {
				Logger::getInstance().logError("Host: Failed to send message");
			}
		}
	}
	read_thread.join();
	waitpid(client_pid, nullptr, 0);
	Logger::getInstance().logInfo("Host: Shutting down");
}
//                            КЛИЕНТ
void runClient(const std::string& conn_type) {
	int client_id = 1;  // т.к. 1 к 1
	Logger::getInstance().logInfo("=== Client: Starting chat with connection type: " + conn_type + " ===");
	std::unique_ptr<Conn> conn_to_host;
	std::unique_ptr<Conn> conn_from_host;
	if (conn_type == "mq") {
		conn_to_host = std::make_unique<ConnMQ>("client_to_host", false);
		conn_from_host = std::make_unique<ConnMQ>("host_to_client", false);
	}
	else if (conn_type == "fifo") {
		conn_to_host = std::make_unique<ConnFIFO>("client_to_host", false);
		conn_from_host = std::make_unique<ConnFIFO>("host_to_client", false);
	}
	else if (conn_type == "sock") {
		conn_to_host = std::make_unique<ConnSock>("client_to_host", false);
		conn_from_host = std::make_unique<ConnSock>("host_to_client", false);
	}
	else {
		Logger::getInstance().logError("Client: Unknown connection type: " + conn_type);
		return;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	if (conn_type == "fifo" || conn_type == "sock") {
		Logger::getInstance().logInfo("Client: Opening connections...");
		if (!conn_from_host->Open()) {
			Logger::getInstance().logError("Client: Failed to open connection from host");
			return;
		}
		if (!conn_to_host->Open()) {
			Logger::getInstance().logError("Client: Failed to open connection to host");
			return;
		}
	}
	// Отправка сигнала присоединения
	ChatMessage join_msg;
	join_msg.type = -1;
	join_msg.sender_id = client_id;
	join_msg.receiver_id = 0;
	join_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	strncpy(join_msg.text, "joined", sizeof(join_msg.text) - 1);
	conn_to_host->Write(&join_msg, sizeof(join_msg));
	// Поток для чтения сообщений от хоста
	std::thread read_thread([&]() {
		ChatMessage msg;
		while (running) {
			if (conn_from_host->Read(&msg, sizeof(msg))) {
				auto time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point(std::chrono::milliseconds(msg.timestamp)));
				std::string time_str = std::ctime(&time_t);
				time_str.pop_back();
				if (msg.type == -3 && std::string(msg.text) == "quit") {
					Logger::getInstance().logInfo("Host: Received quit from host");
					running = false;
					return; 
				}
				if (msg.type == 0) {
					//std::cout << "[" << time_str << "] Host (broadcast): " << msg.text << std::endl;
					Logger::getInstance().logInfo("Client received broadcast: " + std::string(msg.text));
				}
				else if (msg.type == 1 && msg.receiver_id == client_id) {
					std::cout << "[" << time_str << "] Private from Host: " << msg.text << std::endl;
					Logger::getInstance().logInfo("Client received private from host: " + std::string(msg.text));
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	});
	// Таймаут поток
	std::atomic<std::chrono::steady_clock::time_point> last_message_time(std::chrono::steady_clock::now());
	std::thread timeout_thread([&]() {
		while (running) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_message_time.load()).count();
			if (elapsed >= 60) {
				Logger::getInstance().logError("Client: No messages sent for 60 seconds, terminating");
				kill(getpid(), SIGKILL);
			}
		}
	});
	// Основной цикл для ввода и отправки сообщений
	while (running) {
		std::string input;
		std::cout << "Client: ";
		if (std::getline(std::cin, input)) {
			if (input == "/quit") {
				running = false;
				continue;
			}
			if (input.empty()) continue;
			ChatMessage msg;
			msg.sender_id = client_id;
			msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			size_t space_pos = input.find(' ');
			if (input.rfind("@ ", 0) == 0 && space_pos != std::string::npos) {
				// Private to host
				msg.type = 1;
				msg.receiver_id = 0;  // Host ID
				strncpy(msg.text, input.substr(space_pos + 1).c_str(), sizeof(msg.text) - 1);
				msg.text[sizeof(msg.text) - 1] = '\0';
			}
			else {
				// Broadcast
				msg.type = 0;
				msg.receiver_id = 0;
				strncpy(msg.text, input.c_str(), sizeof(msg.text) - 1);
				msg.text[sizeof(msg.text) - 1] = '\0';
			}
			if (conn_to_host->Write(&msg, sizeof(msg))) {
				last_message_time = std::chrono::steady_clock::now();
				Logger::getInstance().logInfo("Client sent message: " + input);
			}
			else {
				Logger::getInstance().logError("Client: Failed to send message");
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