#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>

#include "orderbook/orderbook.hpp"
#include "publisher/publisher.hpp"
#include "websocket/wb.hpp"

using json = nlohmann::json;

// Pretty-print the top N bid/ask levels to stdout
static void printOrderBook(const json& payload)
{
    std::cout << "\n╔══════════════════════════════════╗\n";
    std::cout << "║      BTC/USDT  Order Book        ║\n";
    std::cout << "╠══════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Mid Price : " << payload["mid_price"].get<double>() << "\n";
    std::cout << "  OBI       : " << std::setprecision(4)
              << payload["obi"].get<double>() << "\n";

    std::cout << "  ── Bids ──────────────────────── \n";
    for (const auto& b : payload["bids"])
        std::cout << "    " << std::setprecision(2) << b["price"].get<double>()
                  << "  qty=" << b["qty"].get<double>() << "\n";

    std::cout << "  ── Asks ──────────────────────── \n";
    for (const auto& a : payload["asks"])
        std::cout << "    " << std::setprecision(2) << a["price"].get<double>()
                  << "  qty=" << a["qty"].get<double>() << "\n";

    std::cout << "╚══════════════════════════════════╝\n";
}

int main()
{
    ix::initNetSystem();

    std::cout << "=== HFT Engine Starting ===\n";

    // ZeroMQ publisher
    Publisher pub("tcp://127.0.0.1:5555");
    std::cout << "[ZMQ] Publisher bound to tcp://127.0.0.1:5555\n";

    // Order book (shared with the callback via capture)
    OrderBook book;

    // Binance partial depth stream — top 10 levels, 100 ms updates
    const std::string url =
        "wss://stream.binance.com:9443/ws/btcusdt@depth10@100ms";

    WebSocketClient ws(url);

    ws.ws.setOnMessageCallback(
        [&pub, &book](const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Message)
            {
                try
                {
                    // 1. Parse raw Binance snapshot
                    json data = json::parse(msg->str);

                    // 2. Update the order book
                    book.update(data);

                    // 3. Build the enriched payload
                    json payload = book.toJson(5);

                    // 4. Print to terminal
                    printOrderBook(payload);

                    // 5. Publish over ZeroMQ → Python dashboard
                    pub.publish(payload.dump());
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[ERROR] " << e.what() << "\n";
                }
            }
            else if (msg->type == ix::WebSocketMessageType::Open)
            {
                std::cout << "[WS] Connected to Binance\n";
            }
            else if (msg->type == ix::WebSocketMessageType::Error)
            {
                std::cerr << "[WS] Error: " << msg->errorInfo.reason << "\n";
            }
        });

    ws.start();

    // Keep alive
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    ix::uninitNetSystem();
    return 0;
}