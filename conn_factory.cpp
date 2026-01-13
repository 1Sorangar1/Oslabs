#include "conn_factory.h"
#include "conn_mq.h"
#include "conn_fifo.h"
#include "conn_sock.h"

std::unique_ptr<Conn> ConnFactory::createToClient(const std::string& conn_type, int client_id, bool create) {
    std::string id = "host_to_client_" + std::to_string(client_id);
    return createToClient(conn_type, id, create);
}

std::unique_ptr<Conn> ConnFactory::createFromClient(const std::string& conn_type, int client_id, bool create) {
    std::string id = "client_" + std::to_string(client_id) + "_to_host";
    return createFromClient(conn_type, id, create);
}

std::unique_ptr<Conn> ConnFactory::createToClient(const std::string& conn_type, const std::string& id, bool create) {
    if (conn_type == "mq") {
        return std::make_unique<ConnMQ>(id, create);
    } else if (conn_type == "fifo") {
        return std::make_unique<ConnFIFO>(id, create);
    } else if (conn_type == "sock") {
        return std::make_unique<ConnSock>(id, create);
    }
    return nullptr;
}

std::unique_ptr<Conn> ConnFactory::createFromClient(const std::string& conn_type, const std::string& id, bool create) {
    if (conn_type == "mq") {
        return std::make_unique<ConnMQ>(id, create);
    } else if (conn_type == "fifo") {
        return std::make_unique<ConnFIFO>(id, create);
    } else if (conn_type == "sock") {
        return std::make_unique<ConnSock>(id, create);
    }
    return nullptr;
}

