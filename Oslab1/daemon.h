#pragma once

#include <string>
#include <csignal>
#include <atomic>
#include <memory>
#include "config.h"

class Daemon {
public:
    static Daemon& getInstance();
    
    bool initialize(const std::string& configPath);
    void run();
    void stop();
    
    // Signal handlers
    static void handleSIGHUP(int sig);
    static void handleSIGTERM(int sig);
    
private:
    Daemon() = default;
    ~Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    
    bool daemonize();
    bool createPidFile();
    bool checkExistingProcess();
    void removePidFile();
    bool processExists(pid_t pid);
    
    void performTask();
    void clearFolder(const std::string& folder);
    void copyFiles();
    
    std::atomic<bool> running_;
    std::string pidFilePath_;
    static Daemon* instance_;
};
