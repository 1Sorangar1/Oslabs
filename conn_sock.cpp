#include "conn_sock.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>

ConnSock::ConnSock(const std::string& id, bool create) : Conn(id, create), server_fd_(-1), client_fd_(-1) {
    sem_read_ = nullptr;
    sem_write_ = nullptr;
    
    socket_path_ = "/tmp/chat_sock_" + id;
    
    std::string sem_read_name = "/chat_sock_sem_read_" + id;
    std::string sem_write_name = "/chat_sock_sem_write_" + id;
    
    Logger::getInstance().logInfo("ConnSock: Initializing with id=" + id + ", create=" + (create ? "true" : "false"));
    
    if (create) {
        // сокет
        unlink(socket_path_.c_str());
        
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ == -1) {
            Logger::getInstance().logError("ConnSock: Failed to create socket: " + std::string(strerror(errno)));
            return;
        }
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            Logger::getInstance().logError("ConnSock: Failed to bind socket: " + std::string(strerror(errno)));
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
        
        if (listen(server_fd_, 1) == -1) {
            Logger::getInstance().logError("ConnSock: Failed to listen: " + std::string(strerror(errno)));
            close(server_fd_);
            server_fd_ = -1;
            return;
        }
        
        sem_unlink(sem_read_name.c_str());
        sem_unlink(sem_write_name.c_str());
        
        sem_read_ = sem_open(sem_read_name.c_str(), O_CREAT, 0666, 0);
        sem_write_ = sem_open(sem_write_name.c_str(), O_CREAT, 0666, 1);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnSock: Failed to create semaphores: " + std::string(strerror(errno)));
            cleanup();
            return;
        }
        
        Logger::getInstance().logInfo("ConnSock: Created server socket and semaphores");
    } else {
        client_fd_ = -1;
        server_fd_ = -1;
        
        sem_read_ = sem_open(sem_read_name.c_str(), 0);
        sem_write_ = sem_open(sem_write_name.c_str(), 0);
        
        if (sem_read_ == SEM_FAILED || sem_write_ == SEM_FAILED) {
            Logger::getInstance().logError("ConnSock: Failed to open semaphores: " + std::string(strerror(errno)));
            cleanup();
            return;
        }
        
        Logger::getInstance().logInfo("ConnSock: Client socket initialized, will connect in Open()");
    }
}

ConnSock::~ConnSock() {
    cleanup();
}

void ConnSock::cleanup() {
    if (client_fd_ != -1) {
        close(client_fd_);
        client_fd_ = -1;
    }
    
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    if (is_creator_) {
        unlink(socket_path_.c_str());
        
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
            sem_unlink(("/chat_sock_sem_read_" + id_).c_str());
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
            sem_unlink(("/chat_sock_sem_write_" + id_).c_str());
        }
        
        Logger::getInstance().logInfo("ConnSock: Cleaned up resources");
    } else {
        if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
            sem_close(sem_read_);
        }
        if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
            sem_close(sem_write_);
        }
    }
}

bool ConnSock::Open() {
    if (is_creator_) {
        return acceptConnection();
    } else {
        if (client_fd_ == -1) {
            client_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
            if (client_fd_ == -1) {
                Logger::getInstance().logError("ConnSock: Failed to create client socket: " + std::string(strerror(errno)));
                return false;
            }
            
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
            
            Logger::getInstance().logInfo("ConnSock: Connecting to server socket...");
            if (connect(client_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                Logger::getInstance().logError("ConnSock: Failed to connect: " + std::string(strerror(errno)));
                close(client_fd_);
                client_fd_ = -1;
                return false;
            }
            
            Logger::getInstance().logInfo("ConnSock: Connected to server socket");
        }
        return true;
    }
}

bool ConnSock::acceptConnection() {
    if (server_fd_ == -1) {
        Logger::getInstance().logError("ConnSock: Server socket not created");
        return false;
    }
    
    if (client_fd_ == -1) {
        Logger::getInstance().logInfo("ConnSock: Waiting for client connection...");
                
        struct sockaddr_un addr;
        socklen_t len = sizeof(addr);
        client_fd_ = accept(server_fd_, (struct sockaddr*)&addr, &len);
        if (client_fd_ == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                Logger::getInstance().logError("ConnSock: Accept timeout - client did not connect in time");
            } else {
                Logger::getInstance().logError("ConnSock: Failed to accept connection: " + std::string(strerror(errno)));
            }
            return false;
        }
        Logger::getInstance().logInfo("ConnSock: Accepted connection from client");
    }
    
    return true;
}

bool ConnSock::Read(void* buf, size_t count) {
    int fd = -1;
    if (is_creator_) {
        if (client_fd_ == -1) {
            Logger::getInstance().logError("ConnSock: Connection not accepted, call Open() first");
            return false;
        }
        fd = client_fd_;
    } else {
        fd = client_fd_;
    }
    
    if (fd == -1 || sem_read_ == nullptr || sem_read_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnSock: Invalid descriptor for read (fd=" + std::to_string(fd) + ")");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;
    
    if (sem_timedwait(sem_read_, &timeout) != 0) {
        if (errno == ETIMEDOUT) {
            return false;  
        }
        Logger::getInstance().logError("ConnSock: Read error: " + std::string(strerror(errno)));
        return false;
    }
    
    ssize_t bytes_read = recv(fd, buf, count, 0);
    if (bytes_read == -1) {
        Logger::getInstance().logError("ConnSock: Failed to receive: " + std::string(strerror(errno)));
        return false;
    }
    
    Logger::getInstance().logInfo("ConnSock: Read " + std::to_string(bytes_read) + " bytes");
    return true;
}

bool ConnSock::Write(const void* buf, size_t count) {
    int fd = -1;
    if (is_creator_) {
        if (client_fd_ == -1) {
            Logger::getInstance().logError("ConnSock: Connection not accepted, call Open() first");
            return false;
        }
        fd = client_fd_;
    } else {
        fd = client_fd_;
    }
    
    if (fd == -1 || sem_write_ == nullptr || sem_write_ == SEM_FAILED) {
        Logger::getInstance().logError("ConnSock: Invalid descriptor for write (fd=" + std::to_string(fd) + ")");
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write_, &timeout) != 0) {
        Logger::getInstance().logError("ConnSock: Write timeout or error: " + std::string(strerror(errno)));
        return false;
    }
    
    ssize_t bytes_written = send(fd, buf, count, 0);
    if (bytes_written == -1) {
        Logger::getInstance().logError("ConnSock: Failed to send: " + std::string(strerror(errno)));
        sem_post(sem_write_);
        return false;
    }
    
    sem_post(sem_read_); 
    sem_post(sem_write_); 
    
    Logger::getInstance().logInfo("ConnSock: Wrote " + std::to_string(bytes_written) + " bytes");
    return true;
}

