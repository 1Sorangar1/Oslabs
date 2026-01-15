#pragma once

#include "conn.h"
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string>

class ConnSock : public Conn {
public:
    ConnSock(const std::string& id, bool create);
    ~ConnSock() override;
    
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
    bool Open() override;
    
private:
    int server_fd_;
    int client_fd_;
    sem_t* sem_read_;
    sem_t* sem_write_;
    std::string socket_path_;
    
    void cleanup();
    bool acceptConnection();
};

