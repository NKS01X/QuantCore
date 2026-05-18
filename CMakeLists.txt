cmake_minimum_required(VERSION 3.10)
project(hft_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find packages provided by vcpkg
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)

# Executable
add_executable(trading_engine
    engine/main.cpp
    engine/orderbook/orderbook.cpp
    engine/publisher/publisher.cpp
    engine/websocket/wb.cpp
)

# Link libraries
target_link_libraries(trading_engine PRIVATE
    nlohmann_json::nlohmann_json
    cppzmq
    ixwebsocket::ixwebsocket
    Threads::Threads
)
