#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Структура сообщения чата
struct ChatMessage {
    int32_t type;        // 0 - общее сообщение, >0 - личное сообщение (id получателя)
    int32_t sender_id;   // ID отправителя (0 - хост, 1 - клиент)
    int32_t receiver_id; // ID получателя (для личных сообщений)
    int64_t timestamp;   // Время отправки
    char text[256];      // Текст сообщения
};

// Базовый класс для соединений
class Conn {
public:
    Conn(const std::string& id, bool create);
    virtual ~Conn();
    
    // Чтение данных
    virtual bool Read(void* buf, size_t count) = 0;
    
    // Запись данных
    virtual bool Write(const void* buf, size_t count) = 0;
    
    // Проверка доступности данных (для неблокирующих операций)
    virtual bool IsReady() const { return true; }
    
    // Открыть соединение (для FIFO нужно открыть после fork)
    virtual bool Open() { return true; }
    
protected:
    std::string id_;
    bool is_creator_;
};

