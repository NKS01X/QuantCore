#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>

#include "analytics/analytics.hpp"
#include "orderbook/orderbook.hpp"
#include "publisher/publisher.hpp"
#include "websocket/wb.hpp"

using json = nlohmann::json;

static void printOrderBook(const json& payload)
{
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║        BTC/USDT  ·  HFT Microstructure          ║\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Mid Price  : " << payload["mid_price"].get<double>() << "\n";
    std::cout << std::setprecision(4);
    std::cout << "  OBI (raw)  : " << payload["obi"].get<double>()            << "\n";
    std::cout << "  OBI Kalman : " << payload["obi_kalman"].get<double>()      << "\n";
    std::cout << "  Return Z   : " << payload["return_zscore"].get<double>()   << "\n";
    std::cout << "  Park Vol   : " << payload["park_vol"].get<double>()        << "\n";
    std::cout << "  OBI/Vol    : " << payload["obi_normalized"].get<double>()  << "\n";
    std::cout << "  VPIN       : " << payload["vpin"].get<double>()            << "\n";
    std::cout << "  Kyle λ     : " << payload["kyle_lambda"].get<double>()     << "\n";
    std::cout << "  Composite  : " << payload["composite"].get<double>()       << "\n";

    std::cout << "  ── Bids ───────────────────────────────────────── \n";
    for (const auto& b : payload["bids"])
        std::cout << "    " << std::setprecision(2) << b["price"].get<double>()
                  << "  qty=" << b["qty"].get<double>() << "\n";

    std::cout << "  ── Asks ───────────────────────────────────────── \n";
    for (const auto& a : payload["asks"])
        std::cout << "    " << std::setprecision(2) << a["price"].get<double>()
                  << "  qty=" << a["qty"].get<double>() << "\n";

    std::cout << "╚══════════════════════════════════════════════════╝\n";
}

// gate on kyle lambda - above this liquidity is bad, scale signal down
static constexpr double LAMBDA_MAX = 0.5;

int main()
{
    ix::initNetSystem();

    std::cout << "=== HFT Engine Starting ===\n";

    Publisher pub("tcp://127.0.0.1:5555");
    std::cout << "[ZMQ] bound to 5555\n";

    WelfordEstimator ret_dist;
    WelfordEstimator innov_dist;
    ParkinsonVol     park;
    VPINTracker      vpin;
    KalmanOBI        kalman;
    KyleLambda       kyle;

    OrderBook book;

    double prev_mid = 0.0;
    double prev_obi = 0.0;  // not used rn but might need later

    const std::string url = "wss://stream.binance.com:9443/ws/btcusdt@depth10@100ms";
    WebSocketClient ws(url);

    ws.ws.setOnMessageCallback(
        [&](const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Message)
            {
                try
                {
                    json data = json::parse(msg->str);
                    book.update(data);

                    double mid      = book.midPrice();
                    double obi_raw  = book.obi();
                    double best_bid = book.bestBid();
                    double best_ask = book.bestAsk();
                    double bid_qty  = book.topBidQty();
                    double ask_qty  = book.topAskQty();

                    SignalState sig;
                    sig.mid     = mid;
                    sig.obi_raw = obi_raw;

                    // return z-score
                    if (prev_mid > 0.0) {
                        sig.ret = (mid - prev_mid) / prev_mid;
                        ret_dist.update(sig.ret);
                        sig.return_zscore = ret_dist.zscore(sig.ret);
                    }
                    sig.vol_estimate = ret_dist.stddev();

                    // parkinson vol
                    park.update(best_bid, best_ask);
                    sig.park_vol = park.vol();
                    sig.obi_normalized = sig.park_vol > 1e-9 ? obi_raw / sig.park_vol : 0.0;

                    // vpin
                    vpin.update(mid, prev_mid, bid_qty, ask_qty);
                    sig.vpin = vpin.current();

                    // kalman smoothed obi + innovation
                    sig.obi_kalman        = kalman.update(obi_raw);
                    sig.kalman_innovation = kalman.innovation();
                    innov_dist.update(sig.kalman_innovation);
                    sig.innovation_zscore = innov_dist.zscore(sig.kalman_innovation);

                    // kyle lambda
                    if (prev_mid > 0.0) {
                        double dp = mid - prev_mid;
                        double sv = (mid > prev_mid) ?  ask_qty
                                  : (mid < prev_mid) ? -bid_qty
                                  : 0.0;
                        kyle.push(dp, sv);
                    }
                    sig.kyle_lambda = kyle.lambda();

                    // composite: kalman_obi * (1 - vpin) * vol_scale * liq_gate
                    double vpin_gate = 1.0 - sig.vpin;
                    double vol_scale = sig.park_vol > 1e-9 ? 1.0 / sig.park_vol : 0.0;
                    double liq_gate  = sig.kyle_lambda < LAMBDA_MAX ? 1.0 : 0.5;

                    sig.composite = sig.obi_kalman
                                    * vpin_gate
                                    * std::min(vol_scale, 5.0)
                                    * liq_gate;

                    json payload = book.toJson(5, sig);
                    printOrderBook(payload);
                    pub.publish(payload.dump());

                    prev_mid = mid;
                    prev_obi = obi_raw;
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

    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    ix::uninitNetSystem();
    return 0;
}