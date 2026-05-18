#include "wb.hpp"

WebSocketClient::WebSocketClient(const std::string& url)
{
    ws.setUrl(url);
}

void WebSocketClient::start()
{
    ws.start();
}

void WebSocketClient::stop()
{
    ws.stop();
}

void WebSocketClient::send(const std::string& message)
{
    ws.send(message);
}