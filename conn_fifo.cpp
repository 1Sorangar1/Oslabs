#include "conn_fifo.h"
#include "logger.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <sys/types.h>
#include <chrono>
#include <thread>

extern std::atomic<bool> running;


ConnFIFO::ConnFIFO(const std::string& id, bool create) : Conn(id, create), fd_read_(-1), fd_write_(-1) {
	sem_read_ = nullptr;
	sem_write_ = nullptr;
	if (id == "host_to_client") {
		fifo_read_path_ = "/tmp/chat_fifo_host_to_client";
		fifo_write_path_ = fifo_read_path_;
	}
	else if (id == "client_to_host") {
		fifo_read_path_ = "/tmp/chat_fifo_client_to_host";
		fifo_write_path_ = fifo_read_path_;
	}
	else {
		fifo_read_path_ = "/tmp/chat_fifo_" + id;
		fifo_write_path_ = fifo_read_path_;
	}
	std::string sem_read_name = "/chat_fifo_sem_read_" + id;
	std::string sem_write_name = "/chat_fifo_sem_write_" + id;
	Logger::getInstance().logInfo("ConnFIFO: Initializing with id=" + id + ", create=" + (create ? "true" : "false"));
	if (create) {
		unlink(fifo_read_path_.c_str());
		sem_unlink(sem_read_name.c_str());
		sem_unlink(sem_write_name.c_str());

		if (mkfifo(fifo_read_path_.c_str(), 0666) != 0) {
			if (errno != EEXIST) {
				Logger::getInstance().logError("ConnFIFO: Failed to create FIFO: " + std::string(strerror(errno)));
				return;
			}
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
	}
	else {
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
		if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
			sem_close(sem_read_);
			sem_unlink(("/chat_fifo_sem_read_" + id_).c_str());
		}
		if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
			sem_close(sem_write_);
			sem_unlink(("/chat_fifo_sem_write_" + id_).c_str());
		}
		Logger::getInstance().logInfo("ConnFIFO: Cleaned up resources");
	}
	else {
		if (sem_read_ != nullptr && sem_read_ != SEM_FAILED) {
			sem_close(sem_read_);
		}
		if (sem_write_ != nullptr && sem_write_ != SEM_FAILED) {
			sem_close(sem_write_);
		}
	}
}
bool ConnFIFO::Open() {
	bool is_host_to_client = (id_ == "host_to_client");
	if (is_creator_) {
		if (is_host_to_client) {
			if (fd_write_ == -1) {
				fd_write_ = open(fifo_write_path_.c_str(), O_WRONLY);
				if (fd_write_ == -1) {
					Logger::getInstance().logError("ConnFIFO: Failed to open write FIFO (host_to_client): " + std::string(strerror(errno)));
					return false;
				}
				Logger::getInstance().logInfo("ConnFIFO: Host opened write FIFO (host_to_client)");
			}
		}
		else {
			if (fd_read_ == -1) {
				fd_read_ = open(fifo_read_path_.c_str(), O_RDONLY | O_NONBLOCK);
				if (fd_read_ == -1) {
					Logger::getInstance().logError("ConnFIFO: Failed to open read FIFO (client_to_host): " + std::string(strerror(errno)));
					return false;
				}
				int flags = fcntl(fd_read_, F_GETFL);
				if (flags == -1 || fcntl(fd_read_, F_SETFL, flags & ~O_NONBLOCK) == -1) {
					Logger::getInstance().logError("ConnFIFO: Failed to set blocking mode for read FIFO: " + std::string(strerror(errno)));
					close(fd_read_);
					fd_read_ = -1;
					return false;
				}
				Logger::getInstance().logInfo("ConnFIFO: Host opened read FIFO (client_to_host)");
			}
		}
	}
	else {
		if (is_host_to_client) {
			if (fd_read_ == -1) {
				fd_read_ = open(fifo_read_path_.c_str(), O_RDONLY | O_NONBLOCK);
				if (fd_read_ == -1) {
					Logger::getInstance().logError("ConnFIFO: Client failed to open read FIFO (host_to_client): " + std::string(strerror(errno)));
					return false;
				}
				int flags = fcntl(fd_read_, F_GETFL);
				if (flags == -1 || fcntl(fd_read_, F_SETFL, flags & ~O_NONBLOCK) == -1) {
					Logger::getInstance().logError("ConnFIFO: Failed to set blocking mode for read FIFO: " + std::string(strerror(errno)));
					close(fd_read_);
					fd_read_ = -1;
					return false;
				}
				Logger::getInstance().logInfo("ConnFIFO: Client opened read FIFO (host_to_client)");
			}
		}
		else {
			if (fd_write_ == -1) {
				fd_write_ = open(fifo_write_path_.c_str(), O_WRONLY);
				if (fd_write_ == -1) {
					Logger::getInstance().logError("ConnFIFO: Client failed to open write FIFO (client_to_host): " + std::string(strerror(errno)));
					return false;
				}
				Logger::getInstance().logInfo("ConnFIFO: Client opened write FIFO (client_to_host)");
			}
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
	timeout.tv_sec += 0;  
	timeout.tv_nsec += 100000000; 
	if (timeout.tv_nsec >= 1000000000) { 
		timeout.tv_nsec -= 1000000000;
		timeout.tv_sec += 1;
	}
	if (sem_timedwait(sem_read_, &timeout) != 0) {

		if (errno == ETIMEDOUT) {
			return false;  // Не логируем таймаут как ошибку
		}
		Logger::getInstance().logError("ConnFIFO: Read error: " + std::string(strerror(errno)));
		return false;
	}
	ssize_t bytes_read = read(fd_read_, buf, count);
	if (bytes_read == 0) { 
		Logger::getInstance().logInfo("ConnFIFO: EOF on read, connection closed");
		return false;
	}
	if (bytes_read == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return false;  // Нет данных, выйти без ошибки
		}
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