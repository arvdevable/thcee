cmake_minimum_required(VERSION 3.16)
project(thcee LANGUAGES CXX)

add_executable(thc lexer.cpp)
target_compile_features(thc PRIVATE cxx_std_17)

