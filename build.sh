#!/bin/bash

# Создаем директорию build если её нет
mkdir -p build

# Переходим в директорию build
cd build

# Очистка промежуточных файлов cmake
rm -rf CMakeFiles
rm -f CMakeCache.txt
rm -f cmake_install.cmake
rm -f Makefile
rm -f *.cmake

# Запуск cmake из директории build
cmake ..

# Сборка проекта
make
