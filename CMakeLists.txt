cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(TdExample VERSION 1.0 LANGUAGES CXX)

find_package(Td REQUIRED)
find_package(CURL REQUIRED)

add_executable(fetcher helpers.h json/jsoncpp.cpp json-forwards.h json/json.h main.cpp fetcher.cpp fetcher.h requests.cpp requests.h)
target_link_libraries(fetcher PRIVATE Td::TdStatic CURL::libcurl Td::TdJson)
set_property(TARGET fetcher PROPERTY CXX_STANDARD 14)

