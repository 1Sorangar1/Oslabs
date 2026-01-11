#!/bin/bash

rm -f daemon_lab1 *.o

echo "Compiling source files..."
g++ -Wall -Werror -std=c++17 -c config.cpp -o config.o
g++ -Wall -Werror -std=c++17 -c daemon.cpp -o daemon.o
g++ -Wall -Werror -std=c++17 -c main.cpp -o main.o

g++ -Wall -Werror -std=c++17 config.o daemon.o main.o -o daemon_lab1

rm -f *.o

echo "Executable: ./daemon_lab1 [config_file]"
