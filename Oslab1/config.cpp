#include "config.h"
#include <syslog.h>
#include <filesystem>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& configPath) {
    std::string absPath;
    
    if (fs::path(configPath).is_absolute()) {
        absPath = configPath;
    } else {
        absPath = fs::absolute(configPath).string();
    }
    
    configPath_ = absPath;
    return parseConfigFile(absPath);
}

bool Config::reload() {
    if (configPath_.empty()) {
        syslog(LOG_ERR, "Cannot reload: config path not set");
        return false;
    }
    return parseConfigFile(configPath_);
}

bool Config::parseConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        syslog(LOG_ERR, "Failed to open config file: %s", path.c_str());
        return false;
    }
    
    std::string line;
    if (std::getline(file, line)) {
        std::istringstream iss(line);
        if (!(iss >> folder1_ >> folder2_)) {
            syslog(LOG_ERR, "Invalid config format. Expected: folder1 folder2 [interval]");
            file.close();
            return false;
        }
        
        // Try to read interval (optional)
        std::string intervalStr;
        if (iss >> intervalStr) {
            try {
                interval_ = std::stoi(intervalStr);
                if (interval_ <= 0) {
                    interval_ = 60; // default
                }
            } catch (...) {
                interval_ = 60; // default
            }
        }
    } else {
        syslog(LOG_ERR, "Config file is empty");
        file.close();
        return false;
    }
    
    file.close();
    
    // Validate paths
    if (!fs::exists(folder1_)) {
        syslog(LOG_WARNING, "Folder1 does not exist: %s", folder1_.c_str());
    }
    
    if (!fs::exists(folder2_)) {
        syslog(LOG_WARNING, "Folder2 does not exist: %s", folder2_.c_str());
    }
    
    syslog(LOG_INFO, "Config loaded: folder1=%s, folder2=%s, interval=%d", 
           folder1_.c_str(), folder2_.c_str(), interval_);
    
    return true;
}
