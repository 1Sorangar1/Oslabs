#pragma once

#include "conn.h"
#include <semaphore.h>
#include <string>

class ConnFIFO : public Conn {
public:
    ConnFIFO(const std::string& id, bool create);
    ~ConnFIFO() override;
    
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
    bool Open() override;
    
private:
    int fd_read_;
    int fd_write_;
    sem_t* sem_read_;
    sem_t* sem_write_;
    std::string fifo_read_path_;
    std::string fifo_write_path_;
    
    void cleanup();
};

