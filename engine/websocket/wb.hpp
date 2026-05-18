#pragma once

#include <ixwebsocket/IXWebSocket.h>
#include <string>

class WebSocketClient
{
public:
    explicit WebSocketClient(const std::string& url);

    void start();
    void stop();
    void send(const std::string& message);

    ix::WebSocket ws;
};