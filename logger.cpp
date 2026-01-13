#include "logger.h"
#include <iostream>
#include <filesystem>

Logger* Logger::instance_ = nullptr;

Logger& Logger::getInstance() {
    static Logger instance;
    instance_ = &instance;
    return instance;
}

Logger::Logger() {
    log_file_.open("chat.log", std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Failed to open log file" << std::endl;
    }
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

std::string Logger::getCurrentTime() const {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::writeLog(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string log_entry = "[" + getCurrentTime() + "] [" + level + "] " + message;
    
    std::cout << log_entry << std::endl;
    
    if (log_file_.is_open()) {
        log_file_ << log_entry << std::endl;
        log_file_.flush();
    }
}

void Logger::log(const std::string& message) {
    writeLog("LOG", message);
}

void Logger::logError(const std::string& message) {
    writeLog("ERROR", message);
}

void Logger::logInfo(const std::string& message) {
    writeLog("INFO", message);
}

