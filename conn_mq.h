#pragma once

#include "conn.h"
#include <mqueue.h>
#include <semaphore.h>

class ConnMQ : public Conn {
public:
    ConnMQ(const std::string& id, bool create);
    ~ConnMQ() override;
    
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
    
private:
    mqd_t mq_descriptor_;
    sem_t* sem_read_;
    sem_t* sem_write_;
    
    void cleanup();
};

