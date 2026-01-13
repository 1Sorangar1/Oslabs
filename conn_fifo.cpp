#include "conn_fifo.h"
#include "logger.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <sys/types.h>

ConnFIFO::ConnFIFO(const std::string& id, bool create) : Conn(id, create), fd_read_(-1), fd_write_(-1) {
    sem_read_ = nullptr;
    sem_write_ = nullptr;
    
    fifo_read_path_ = "/tmp/chat_fifo_read_" + id;
    fifo_write_path_ = "/tmp/chat_fifo_write_" + id;
    
    std::string sem_read_name = "/chat_fifo_sem_read_" + id;
    std::string sem_write_name = "/chat_fifo_sem_write_" + id;
    
    Logger::getInstance().logInfo("ConnFIFO: Initializing with id=" + id + ", create=" + (create ? "true" : "false"));
    
    if (create) {
        unlink(fifo_read_path_.c_str());
        unlink(fifo_write_path_.c_str());
        
        sem_unlink(sem_read_name.c_str());
        sem_unlink(sem_write_name.c_str());
        
        // именованные каналы
        if (mkfifo(fifo_read_path_.c_str(), 0666) != 0) {
            Logger::getInstance().logError("ConnFIFO: Failed to create read FIFO: " + std::string(strerror(errno)));
            return;
        }
        
        if (mkfifo(fifo_write_path_.c_str(), 0666) != 0) {
            Logger::getInstance().logError("ConnFIFO: Failed to create write FIFO: " + std::string(strerror(errno)));
            unlink(fifo_read_path_.c_str());
            return;
        }
        
        sem_read_ = sem_open(sem_read_name.c_str(), O_CREAT, 0666, 0);
        sem_write_ = sem_open(sem_write_name.c_str(), O_CREAT, 0666, 1);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnFIFO: Failed to create semaphores: " + std::string(strerror(errno)));
            unlink(fifo_read_path_.c_str());
            unlink(fifo_write_path_.c_str());
            return;
        }
        
        Logger::getInstance().logInfo("ConnFIFO: Created FIFOs and semaphores");
    } else {
        fd_read_ = -1;
        fd_write_ = -1;
        
        sem_read_ = sem_open(sem_read_name.c_str(), 0);
        sem_write_ = sem_open(sem_write_name.c_str(), 0);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnFIFO: Failed to open semaphores: " + std::string(strerror(errno)));
            return;
        }
        
        Logger::getInstance().logInfo("ConnFIFO: Client initialized, will open FIFOs in Open()");
    }
}

ConnFIFO::~ConnFIFO() {
    bool was_read_open = (fd_read_ != -1);
    bool was_write_open = (fd_write_ != -1);
    
    if (fd_read_ != -1) {
        close(fd_read_);
        fd_read_ = -1;
    }
    
    if (fd_write_ != -1) {
        close(fd_write_);
        fd_write_ = -1;
    }
    
    if (is_creator_ && (was_read_open || was_write_open)) {
        unlink(fifo_read_path_.c_str());
        unlink(fifo_write_path_.c_str());
        
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
            sem_unlink(("/chat_fifo_sem_read_" + id_).c_str());
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
            sem_unlink(("/chat_fifo_sem_write_" + id_).c_str());
        }
        
        Logger::getInstance().logInfo("ConnFIFO: Removed FIFO files and semaphores");
    } else {
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
        }
    }
}

void ConnFIFO::cleanup() {
    if (fd_read_ != -1) {
        close(fd_read_);
        fd_read_ = -1;
    }
    
    if (fd_write_ != -1) {
        close(fd_write_);
        fd_write_ = -1;
    }
}

bool ConnFIFO::Open() {
    if (is_creator_) {
        if (fd_read_ == -1) {
            fd_read_ = open(fifo_write_path_.c_str(), O_RDONLY);
            if (fd_read_ == -1) {
                Logger::getInstance().logError("ConnFIFO: Failed to open read FIFO: " + std::string(strerror(errno)));
                return false;
            }
            Logger::getInstance().logInfo("ConnFIFO: Opened read FIFO for host");
        }
        if (fd_write_ == -1) {
            fd_write_ = open(fifo_read_path_.c_str(), O_WRONLY);
            if (fd_write_ == -1) {
                Logger::getInstance().logError("ConnFIFO: Failed to open write FIFO: " + std::string(strerror(errno)));
                if (fd_read_ != -1) {
                    close(fd_read_);
                    fd_read_ = -1;
                }
                return false;
            }
            Logger::getInstance().logInfo("ConnFIFO: Opened write FIFO for host");
        }
    } else {
        if (fd_write_ == -1) {
            fd_write_ = open(fifo_write_path_.c_str(), O_WRONLY);
            if (fd_write_ == -1) {
                Logger::getInstance().logError("ConnFIFO: Failed to open write FIFO: " + std::string(strerror(errno)));
                return false;
            }
            Logger::getInstance().logInfo("ConnFIFO: Opened write FIFO for client");
        }
        if (fd_read_ == -1) {
            fd_read_ = open(fifo_read_path_.c_str(), O_RDONLY);
            if (fd_read_ == -1) {
                Logger::getInstance().logError("ConnFIFO: Failed to open read FIFO: " + std::string(strerror(errno)));
                if (fd_write_ != -1) {
                    close(fd_write_);
                    fd_write_ = -1;
                }
                return false;
            }
            Logger::getInstance().logInfo("ConnFIFO: Opened read FIFO for client");
        }
    }
    return true;
}

bool ConnFIFO::Read(void* buf, size_t count) {
    if (fd_read_ == -1) {
        Logger::getInstance().logError("ConnFIFO: Read FIFO not opened, call Open() first");
        return false;
    }
    
    if (sem_read_ == nullptr || sem_read_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnFIFO: Invalid semaphore for read");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;
    
    if (sem_timedwait(sem_read_, &timeout) != 0) {
        if (errno == ETIMEDOUT) {
            return false;  // Не логируем таймаут как ошибку
        }
        Logger::getInstance().logError("ConnFIFO: Read error: " + std::string(strerror(errno)));
        return false;
    }
    
    ssize_t bytes_read = read(fd_read_, buf, count);
    if (bytes_read == -1) {
        Logger::getInstance().logError("ConnFIFO: Failed to read: " + std::string(strerror(errno)));
        return false;
    }
    
    Logger::getInstance().logInfo("ConnFIFO: Read " + std::to_string(bytes_read) + " bytes");
    return true;
}

bool ConnFIFO::Write(const void* buf, size_t count) {
    if (fd_write_ == -1) {
        Logger::getInstance().logError("ConnFIFO: Write FIFO not opened, call Open() first");
        return false;
    }
    
    if (sem_write_ == nullptr || sem_write_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnFIFO: Invalid semaphore for write");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write_, &timeout) != 0) {
        Logger::getInstance().logError("ConnFIFO: Write timeout or error: " + std::string(strerror(errno)));
        return false;
    }
    
    ssize_t bytes_written = write(fd_write_, buf, count);
    if (bytes_written == -1) {
        Logger::getInstance().logError("ConnFIFO: Failed to write: " + std::string(strerror(errno)));
        sem_post(sem_write_);
        return false;
    }
    
    sem_post(sem_read_); 
    sem_post(sem_write_); 
    
    Logger::getInstance().logInfo("ConnFIFO: Wrote " + std::to_string(bytes_written) + " bytes");
    return true;
}
