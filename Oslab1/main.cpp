#include "daemon.h"
#include <syslog.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Open syslog
    openlog("daemon_lab1", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    std::string configPath = "config.txt";
    
    if (argc > 1) {
        configPath = argv[1];
    }
    
    syslog(LOG_INFO, "Starting daemon with config: %s", configPath.c_str());
    
    Daemon& daemon = Daemon::getInstance();
    
    if (!daemon.initialize(configPath)) {
        syslog(LOG_ERR, "Failed to initialize daemon");
        closelog();
        return 1;
    }
    
    daemon.run();
    
    return 0;
}
