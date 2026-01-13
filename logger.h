#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>

class Logger {
public:
    static Logger& getInstance();
    
    void log(const std::string& message);
    void logError(const std::string& message);
    void logInfo(const std::string& message);
    
private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::string getCurrentTime() const;
    void writeLog(const std::string& level, const std::string& message);
    
    std::ofstream log_file_;
    std::mutex mutex_;
    static Logger* instance_;
};

