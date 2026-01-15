#pragma once

#include "conn.h"
#include <memory>
#include <string>

class ConnFactory {
public:
    static std::unique_ptr<Conn> createToClient(const std::string& conn_type, int client_id, bool create);
    
    static std::unique_ptr<Conn> createFromClient(const std::string& conn_type, int client_id, bool create);
    
    static std::unique_ptr<Conn> createToClient(const std::string& conn_type, const std::string& id, bool create);
    
    static std::unique_ptr<Conn> createFromClient(const std::string& conn_type, const std::string& id, bool create);
};

