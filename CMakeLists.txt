cmake_minimum_required(VERSION 3.10)
project(ChatLab2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

set(COMMON_SOURCES
    conn.cpp
    logger.cpp
    conn_factory.cpp
)

set(CONN_SOURCES
    conn_mq.cpp
    conn_fifo.cpp
    conn_sock.cpp
)

# Хост
add_executable(host_mq host.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(host_mq rt pthread)

add_executable(host_fifo host.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(host_fifo rt pthread)

add_executable(host_sock host.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(host_sock rt pthread)

# Клиент
add_executable(client_mq client.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(client_mq rt pthread)

add_executable(client_fifo client.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(client_fifo rt pthread)

add_executable(client_sock client.cpp ${CONN_SOURCES} ${COMMON_SOURCES})
target_link_libraries(client_sock rt pthread)
