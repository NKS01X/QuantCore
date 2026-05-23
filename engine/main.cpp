#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "analytics/analytics.hpp"
#include "orderbook/orderbook.hpp"
#include "publisher/publisher.hpp"
#include "websocket/wb.hpp"

using json = nlohmann::json;

// ── helpers ───────────────────────────────────────────────────────────────────

static void printOrderBook(const json& payload, const std::string& symbol)
{
    std::cout << "\n╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  " << symbol << "  ·  HFT Microstructure" << "\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Mid Price  : " << payload["mid_price"].get<double>() << "\n";
    std::cout << "  Micro Price: " << payload["micro_price"].get<double>()
              << " (Dev: " << std::setprecision(4) << payload["micro_price_dev"].get<double>() << ")\n";
    std::cout << std::setprecision(4);
    std::cout << "  OBI (raw)  : " << payload["obi"].get<double>()            << "\n";
    std::cout << "  OBI Kalman : " << payload["obi_kalman"].get<double>()      << "\n";
    std::cout << "  Return Z   : " << payload["return_zscore"].get<double>()   << "\n";
    std::cout << "  Park Vol   : " << payload["park_vol"].get<double>()        << "\n";
    std::cout << "  OBI/Vol    : " << payload["obi_normalized"].get<double>()  << "\n";
    std::cout << "  VPIN       : " << payload["vpin"].get<double>()            << "\n";
    std::cout << "  Kyle λ     : " << payload["kyle_lambda"].get<double>()     << "\n";
    std::cout << "  Amihud Illq: " << payload["amihud_illiquidity"].get<double>() << "\n";
    std::cout << "  PDF prob↑  : " << payload["pdf_prob_up"].get<double>()     << "\n";
    std::cout << "  PDF edge   : " << payload["pdf_edge"].get<double>()        << "\n";
    std::cout << "  Composite  : " << payload["composite"].get<double>()       << "\n";
    std::cout << std::setprecision(2);
    std::cout << "  Depth 10bp : Bid=" << payload["depth_10bps_bid"].get<double>()
              << " Ask=" << payload["depth_10bps_ask"].get<double>() << "\n";
    std::cout << "  Depth 50bp : Bid=" << payload["depth_50bps_bid"].get<double>()
              << " Ask=" << payload["depth_50bps_ask"].get<double>() << "\n";
    std::cout << "  Depth 100bp: Bid=" << payload["depth_100bps_bid"].get<double>()
              << " Ask=" << payload["depth_100bps_ask"].get<double>() << "\n";

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

// gate on kyle lambda — above this, liquidity is bad, scale signal down
static constexpr double LAMBDA_MAX = 0.5;

// ── engine state reset — call this when the symbol changes ────────────────────
static void purge_signal_state(WelfordEstimator& ret_dist,
                               WelfordEstimator& innov_dist,
                               ParkinsonVol&     park,
                               VPINTracker&      vpin,
                               KalmanOBI&        kalman,
                               KyleLambda&       kyle,
                               AmihudIlliquidity& amihud,
                               OrderBook&        book,
                               double&           prev_mid,
                               double&           prev_obi)
{
    ret_dist.reset();
    innov_dist.reset();
    park.reset();
    vpin.reset();
    kalman.reset();
    kyle.reset();
    amihud.reset();
    book.bids.clear();
    book.asks.clear();
    prev_mid = 0.0;
    prev_obi = 0.0;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    ix::initNetSystem();

    std::cout << "=== HFT Engine Starting ===\n";

    // data publisher — Python dashboard subscribes here
    Publisher pub("tcp://127.0.0.1:5555");
    std::cout << "[ZMQ] pub bound to 5555\n";

    // command subscriber — Python dashboard publishes config changes here
    zmq::context_t  zmq_ctx(1);
    zmq::socket_t   cmd_sub(zmq_ctx, zmq::socket_type::sub);
    cmd_sub.connect("tcp://127.0.0.1:5556");
    cmd_sub.set(zmq::sockopt::subscribe, "");
    std::cout << "[ZMQ] cmd sub connected to 5556\n";

    // signal layer
    WelfordEstimator  ret_dist;
    WelfordEstimator  innov_dist;
    ParkinsonVol      park;
    VPINTracker       vpin;
    KalmanOBI         kalman;
    KyleLambda        kyle;
    AmihudIlliquidity amihud;

    // predictive PDF — default 500ms horizon = 5 ticks of 100ms
    pdf::PredictivePDF predictor(4.0, 5.0);

    OrderBook book;

    double prev_mid = 0.0;
    double prev_obi = 0.0;

    // active feed parameters
    std::string cur_symbol = "btcusdt";
    int         cur_dt_ms  = 500;

    const std::string base_url = "wss://stream.binance.com:9443/ws/";

    // WS callback captures references to all mutable state
    auto make_callback = [&](WebSocketClient& ws) {
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

                        // welford return z-score
                        if (prev_mid > 0.0) {
                            sig.ret = (mid - prev_mid) / prev_mid;
                            ret_dist.update(sig.ret);
                            sig.return_zscore = ret_dist.zscore(sig.ret);
                        }
                        sig.vol_estimate = ret_dist.stddev();

                        // parkinson vol
                        park.update(best_bid, best_ask);
                        sig.park_vol = park.vol();
                        sig.obi_normalized = sig.park_vol > 1e-9
                                             ? obi_raw / sig.park_vol : 0.0;

                        // vpin
                        vpin.update(mid, prev_mid, bid_qty, ask_qty);
                        sig.vpin = vpin.current();

                        // kalman smoothed obi + innovation z-score
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

                        // micro-price
                        double micro_price = mid;
                        if (bid_qty + ask_qty > 0.0) {
                            micro_price = (best_bid * ask_qty + best_ask * bid_qty) / (bid_qty + ask_qty);
                        }
                        sig.micro_price     = micro_price;
                        sig.micro_price_dev = micro_price - mid;

                        // amihud illiquidity
                        double dp = (prev_mid > 0.0) ? (mid - prev_mid) : 0.0;
                        amihud.update(dp, bid_qty + ask_qty);
                        sig.amihud_illiquidity = amihud.illiquidity();

                        // cumulative depth cushion
                        CumulativeDepth depth = book.cumulativeDepth(mid);
                        sig.depth_10bps_bid  = depth.bid_depth_10bps;
                        sig.depth_10bps_ask  = depth.ask_depth_10bps;
                        sig.depth_50bps_bid  = depth.bid_depth_50bps;
                        sig.depth_50bps_ask  = depth.ask_depth_50bps;
                        sig.depth_100bps_bid = depth.bid_depth_100bps;
                        sig.depth_100bps_ask = depth.ask_depth_100bps;

                        // predictive PDF (uses internally stored dt_)
                        pdf::PdfParams pp = predictor.params(
                            sig.obi_kalman, sig.kyle_lambda,
                            sig.park_vol,   sig.vpin);
                        sig.pdf_mu      = pp.mu;
                        sig.pdf_sigma   = pp.sigma;
                        sig.pdf_prob_up = predictor.prob_up(pp);
                        sig.pdf_edge    = predictor.edge(pp);

                        // composite signal: kalman_obi * (1 - vpin) * vol_scale * liq_gate
                        double vpin_gate = 1.0 - sig.vpin;
                        double vol_scale = sig.park_vol > 1e-9
                                           ? 1.0 / sig.park_vol : 0.0;
                        double liq_gate  = sig.kyle_lambda < LAMBDA_MAX ? 1.0 : 0.5;

                        sig.composite = sig.obi_kalman
                                        * vpin_gate
                                        * std::min(vol_scale, 5.0)
                                        * liq_gate;

                        json payload = book.toJson(5, sig);
                        // attach PDF fields not already in toJson
                        payload["pdf_prob_up"] = sig.pdf_prob_up;
                        payload["pdf_edge"]    = sig.pdf_edge;
                        payload["pdf_mu"]      = sig.pdf_mu;
                        payload["pdf_sigma"]   = sig.pdf_sigma;
                        payload["symbol"]      = cur_symbol;
                        payload["dt_ms"]       = cur_dt_ms;

                        printOrderBook(payload, cur_symbol);
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
                    std::cout << "[WS] Connected → " << cur_symbol << "\n";
                }
                else if (msg->type == ix::WebSocketMessageType::Error)
                {
                    std::cerr << "[WS] Error: " << msg->errorInfo.reason << "\n";
                }
            });
    };

    // initial connection
    std::string init_url = base_url + cur_symbol + "@depth10@100ms";
    WebSocketClient ws(init_url);
    make_callback(ws);
    ws.start();

    // ── main loop: process IPC commands, everything else is event-driven ──────
    while (true)
    {
        // non-blocking command poll
        zmq::message_t cmd_msg;
        if (cmd_sub.recv(cmd_msg, zmq::recv_flags::dontwait))
        {
            try
            {
                std::string raw(static_cast<char*>(cmd_msg.data()), cmd_msg.size());
                json cfg = json::parse(raw);

                if (cfg.value("command", "") == "update_config")
                {
                    std::string new_sym  = cfg.value("symbol", cur_symbol);
                    int         new_dt   = cfg.value("dt_ms",  cur_dt_ms);

                    // update predictor horizon regardless of symbol change
                    double new_dt_ticks = new_dt / 100.0;  // 100ms per tick
                    predictor.set_dt(new_dt_ticks);
                    cur_dt_ms = new_dt;

                    if (new_sym != cur_symbol)
                    {
                        std::cout << "[CMD] Switching " << cur_symbol
                                  << " → " << new_sym
                                  << "  |  Δt=" << new_dt << "ms\n";

                        // 1. stop the live feed
                        ws.stop();

                        // 2. purge all signal state — critical for data hygiene
                        purge_signal_state(ret_dist, innov_dist, park,
                                           vpin, kalman, kyle, amihud,
                                           book, prev_mid, prev_obi);

                        cur_symbol = new_sym;

                        // 3. reconnect on the new stream
                        std::string new_url = base_url + cur_symbol + "@depth10@100ms";
                        ws.ws.setUrl(new_url);
                        make_callback(ws);
                        ws.start();
                    }
                    else
                    {
                        std::cout << "[CMD] Updated Δt → " << new_dt << "ms"
                                  << " (" << new_dt_ticks << " ticks)\n";
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[CMD] Parse error: " << e.what() << "\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ws.stop();
    ix::uninitNetSystem();
    return 0;
}