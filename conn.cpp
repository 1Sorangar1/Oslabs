#include "conn.h"

Conn::Conn(const std::string& id, bool create) : id_(id), is_creator_(create) {
}

Conn::~Conn() {
}
