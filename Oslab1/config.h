#pragma once

#include <string>
#include <fstream>
#include <filesystem>

class Config {
public:
    static Config& getInstance();
    
    bool load(const std::string& configPath);
    bool reload();
    
    std::string getFolder1() const { return folder1_; }
    std::string getFolder2() const { return folder2_; }
    int getInterval() const { return interval_; }
    std::string getConfigPath() const { return configPath_; }
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::string configPath_;
    std::string folder1_;
    std::string folder2_;
    int interval_ = 60;
    
    bool parseConfigFile(const std::string& path);
};
