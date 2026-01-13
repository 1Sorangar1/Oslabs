#include "conn_mq.h"
#include "logger.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>

ConnMQ::ConnMQ(const std::string& id, bool create) : Conn(id, create) {
    mq_descriptor_ = -1;
    sem_read_ = nullptr;
    sem_write_ = nullptr;
    
    std::string queue_name = "/chat_mq_" + id;
    std::string sem_read_name = "/chat_sem_read_" + id;
    std::string sem_write_name = "/chat_sem_write_" + id;
    
    Logger::getInstance().logInfo("ConnMQ: Initializing with id=" + id + ", create=" + (create ? "true" : "false"));
    
    if (create) {
        struct mq_attr attr;
        attr.mq_flags = 0;
        attr.mq_maxmsg = 10;
        attr.mq_msgsize = 512;  
        attr.mq_curmsgs = 0;
        
        mq_unlink(queue_name.c_str());
        sem_unlink(sem_read_name.c_str());
        sem_unlink(sem_write_name.c_str());
        
        mq_descriptor_ = mq_open(queue_name.c_str(), O_CREAT | O_RDWR, 0666, &attr);
        if (mq_descriptor_ == -1) {
            Logger::getInstance().logError("ConnMQ: Failed to create message queue: " + std::string(strerror(errno)));
            return;
        }
        
        sem_read_ = sem_open(sem_read_name.c_str(), O_CREAT, 0666, 0);
        sem_write_ = sem_open(sem_write_name.c_str(), O_CREAT, 0666, 1);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnMQ: Failed to create semaphores: " + std::string(strerror(errno)));
            cleanup();
            return;
        }
        
        Logger::getInstance().logInfo("ConnMQ: Created message queue and semaphores");
    } else {
        mq_descriptor_ = mq_open(queue_name.c_str(), O_RDWR);
        if (mq_descriptor_ == -1) {
            Logger::getInstance().logError("ConnMQ: Failed to open message queue: " + std::string(strerror(errno)));
            return;
        }
        
        sem_read_ = sem_open(sem_read_name.c_str(), 0);
        sem_write_ = sem_open(sem_write_name.c_str(), 0);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnMQ: Failed to open semaphores: " + std::string(strerror(errno)));
            cleanup();
            return;
        }
        
        Logger::getInstance().logInfo("ConnMQ: Opened message queue and semaphores");
    }
}

ConnMQ::~ConnMQ() {
    cleanup();
}

void ConnMQ::cleanup() {
    if (mq_descriptor_ != -1) {
        mq_close(mq_descriptor_);
        mq_descriptor_ = -1;
    }
    
    if (is_creator_) {
        std::string queue_name = "/chat_mq_" + id_;
        mq_unlink(queue_name.c_str());
        
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
            sem_unlink(("/chat_sem_read_" + id_).c_str());
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
            sem_unlink(("/chat_sem_write_" + id_).c_str());
        }
        
        Logger::getInstance().logInfo("ConnMQ: Cleaned up resources");
    } else {
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
        }
    }
}

bool ConnMQ::Read(void* buf, size_t count) {
    if (mq_descriptor_ == -1) {
        Logger::getInstance().logError("ConnMQ: Invalid descriptor for read (mq_descriptor_ == -1)");
        return false;
    }
    if (sem_read_ == nullptr || sem_read_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnMQ: Invalid semaphore for read");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;
    
    if (sem_timedwait(sem_read_, &timeout) != 0) {
        if (errno == ETIMEDOUT) {
            return false;  
        }
        Logger::getInstance().logError("ConnMQ: Read error: " + std::string(strerror(errno)));
        return false;
    }
    
    struct mq_attr attr;
    size_t buffer_size = 512;  
    if (mq_getattr(mq_descriptor_, &attr) == 0) {
        buffer_size = attr.mq_msgsize;
    }
    
    char temp_buf[512];
    ssize_t bytes_read = mq_receive(mq_descriptor_, temp_buf, buffer_size, nullptr);
    if (bytes_read == -1) {
        Logger::getInstance().logError("ConnMQ: Failed to receive message: " + std::string(strerror(errno)));
        return false;
    }
    
    size_t copy_size = (bytes_read < static_cast<ssize_t>(count)) ? bytes_read : count;
    memcpy(buf, temp_buf, copy_size);
    
    Logger::getInstance().logInfo("ConnMQ: Read " + std::to_string(bytes_read) + " bytes");
    return true;
}

bool ConnMQ::Write(const void* buf, size_t count) {
    if (mq_descriptor_ == -1) {
        Logger::getInstance().logError("ConnMQ: Invalid descriptor for write (mq_descriptor_ == -1)");
        return false;
    }
    if (sem_write_ == nullptr || sem_write_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnMQ: Invalid semaphore for write");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write_, &timeout) != 0) {
        Logger::getInstance().logError("ConnMQ: Write timeout or error: " + std::string(strerror(errno)));
        return false;
    }
    
    int result = mq_send(mq_descriptor_, static_cast<const char*>(buf), count, 0);
    if (result == -1) {
        Logger::getInstance().logError("ConnMQ: Failed to send message: " + std::string(strerror(errno)));
        sem_post(sem_write_);
        return false;
    }
    
    sem_post(sem_read_);
    sem_post(sem_write_); 
    
    Logger::getInstance().logInfo("ConnMQ: Wrote " + std::to_string(count) + " bytes");
    return true;
}
