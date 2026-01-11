#include "daemon.h"
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <signal.h>

namespace fs = std::filesystem;

Daemon* Daemon::instance_ = nullptr;

Daemon& Daemon::getInstance() {
    static Daemon instance;
    instance_ = &instance;
    return instance;
}

void Daemon::handleSIGHUP(int sig) {
    (void)sig;
    if (instance_) {
        syslog(LOG_INFO, "Received SIGHUP, reloading configuration");
        Config::getInstance().reload();
    }
}

void Daemon::handleSIGTERM(int sig) {
    (void)sig;
    if (instance_) {
        syslog(LOG_INFO, "Received SIGTERM, shutting down");
        instance_->stop();
    }
}

bool Daemon::initialize(const std::string& configPath) {
    running_ = true;
    
    // Setup signal handlers
    signal(SIGHUP, handleSIGHUP);
    signal(SIGTERM, handleSIGTERM);
    
    // Load configuration
    if (!Config::getInstance().load(configPath)) {
        syslog(LOG_ERR, "Failed to load configuration");
        return false;
    }
    
    // Check for existing process
    if (!checkExistingProcess()) {
        syslog(LOG_ERR, "Failed to check existing process");
        return false;
    }
    
    // Daemonize
    if (!daemonize()) {
        syslog(LOG_ERR, "Failed to daemonize");
        return false;
    }
    
    // Create PID file
    if (!createPidFile()) {
        syslog(LOG_ERR, "Failed to create PID file");
        return false;
    }
    
    syslog(LOG_INFO, "Daemon initialized successfully");
    return true;
}

bool Daemon::daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed");
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }
    
    
    // Create new session
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed");
        return false;
    }
    
    // Second fork
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Second fork failed");
        return false;
    }
    
    if (pid > 0) {
        exit(0);
    }
    
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir to / failed");
        return false;
    }
    
    for (int i = 0; i < 3; ++i) {
        close(i);
    }
    
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }
    
    syslog(LOG_INFO, "Daemonized successfully, PID: %d", getpid());
    return true;
}

bool Daemon::checkExistingProcess() {
    pidFilePath_ = "/tmp/daemon_lab1.pid";
    
    std::ifstream pidFile(pidFilePath_);
    if (!pidFile.is_open()) {
        // No existing PID file, that's fine
        return true;
    }
    
    pid_t oldPid;
    if (!(pidFile >> oldPid)) {
        pidFile.close();
        return true; // Invalid PID file, continue
    }
    pidFile.close();
    
    if (processExists(oldPid)) {
        syslog(LOG_INFO, "Found existing process with PID %d, sending SIGTERM", oldPid);
        if (kill(oldPid, SIGTERM) < 0) {
            syslog(LOG_WARNING, "Failed to send SIGTERM to process %d", oldPid);
        } else {
            // Wait for termination
            sleep(2);
        }
    }
    
    return true;
}

bool Daemon::processExists(pid_t pid) {
    std::string procPath = "/proc/" + std::to_string(pid);
    return fs::exists(procPath);
}

bool Daemon::createPidFile() {
    std::ofstream pidFile(pidFilePath_);
    if (!pidFile.is_open()) {
        syslog(LOG_ERR, "Failed to create PID file: %s", pidFilePath_.c_str());
        return false;
    }
    
    pidFile << getpid() << std::endl;
    pidFile.close();
    
    syslog(LOG_INFO, "PID file created: %s", pidFilePath_.c_str());
    return true;
}

void Daemon::removePidFile() {
    if (!pidFilePath_.empty() && fs::exists(pidFilePath_)) {
        fs::remove(pidFilePath_);
        syslog(LOG_INFO, "PID file removed");
    }
}

void Daemon::stop() {
    running_ = false;
}

void Daemon::run() {
    syslog(LOG_INFO, "Daemon started running");
    
    while (running_) {
        performTask();
        
        int interval = Config::getInstance().getInterval();
        for (int i = 0; i < interval && running_; ++i) {
            sleep(1);
        }
    }
    
    syslog(LOG_INFO, "Daemon stopping");
    removePidFile();
    closelog();
}

void Daemon::performTask() {
    try {
        syslog(LOG_DEBUG, "Performing task");
        copyFiles();
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Error performing task: %s", e.what());
    }
}

void Daemon::clearFolder(const std::string& folder) {
    if (!fs::exists(folder)) {
        syslog(LOG_WARNING, "Folder does not exist: %s", folder.c_str());
        return;
    }
    
    try {
        for (const auto& entry : fs::directory_iterator(folder)) {
            if (fs::is_directory(entry)) {
                fs::remove_all(entry.path());
            } else {
                fs::remove(entry.path());
            }
        }
        syslog(LOG_INFO, "Cleared folder: %s", folder.c_str());
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Error clearing folder %s: %s", folder.c_str(), e.what());
    }
}

void Daemon::copyFiles() {
    std::string folder1 = Config::getInstance().getFolder1();
    std::string folder2 = Config::getInstance().getFolder2();
    
    if (!fs::exists(folder1)) {
        syslog(LOG_WARNING, "Folder1 does not exist: %s", folder1.c_str());
        return;
    }
    
    // Clear folder2
    clearFolder(folder2);
    
    // Create subdirectories in folder2
    std::string imgDir = folder2 + "/IMG";
    std::string othersDir = folder2 + "/OTHERS";
    
    try {
        fs::create_directories(imgDir);
        fs::create_directories(othersDir);
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Error creating subdirectories: %s", e.what());
        return;
    }
    
    // Copy files from folder1 to folder2
    try {
        for (const auto& entry : fs::directory_iterator(folder1)) {
            if (fs::is_regular_file(entry)) {
                std::string filename = entry.path().filename().string();
                std::string targetDir;
                
                // Check if file has .png extension
                if (entry.path().extension() == ".png") {
                    targetDir = imgDir;
                } else {
                    targetDir = othersDir;
                }
                
                std::string targetPath = targetDir + "/" + filename;
                fs::copy_file(entry.path(), targetPath, fs::copy_options::overwrite_existing);
                
                syslog(LOG_DEBUG, "Copied %s to %s", filename.c_str(), targetDir.c_str());
            }
        }
        syslog(LOG_INFO, "Task completed: files copied from %s to %s", folder1.c_str(), folder2.c_str());
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Error copying files: %s", e.what());
    }
}
