#include "orderbook.hpp"

void OrderBook::update(const json& data)
{
    bids.clear();
    asks.clear();

    for (const auto& entry : data["bids"]) {
        double price = std::stod(entry[0].get<std::string>());
        double qty   = std::stod(entry[1].get<std::string>());
        bids[price]  = qty;
    }

    for (const auto& entry : data["asks"]) {
        double price = std::stod(entry[0].get<std::string>());
        double qty   = std::stod(entry[1].get<std::string>());
        asks[price]  = qty;
    }
}

double OrderBook::midPrice() const
{
    if (bids.empty() || asks.empty()) return 0.0;
    return (bids.begin()->first + asks.begin()->first) / 2.0;
}

// obi over top 5 levels
double OrderBook::obi() const
{
    if (bids.empty() || asks.empty()) return 0.0;

    double bv = 0.0, av = 0.0;
    int n = 0;

    for (const auto& [p, q] : bids) {
        bv += q;
        if (++n >= 5) break;
    }
    n = 0;
    for (const auto& [p, q] : asks) {
        av += q;
        if (++n >= 5) break;
    }

    double tot = bv + av;
    return tot == 0.0 ? 0.0 : (bv - av) / tot;
}

double OrderBook::bestBid() const { return bids.empty() ? 0.0 : bids.begin()->first; }
double OrderBook::bestAsk() const { return asks.empty() ? 0.0 : asks.begin()->first; }
double OrderBook::topBidQty() const { return bids.empty() ? 0.0 : bids.begin()->second; }
double OrderBook::topAskQty() const { return asks.empty() ? 0.0 : asks.begin()->second; }
CumulativeDepth OrderBook::cumulativeDepth(double mid) const
{
    CumulativeDepth depth;
    if (mid <= 0.0) return depth;

    // Basis point limits
    double limit_bid_10 = mid * (1.0 - 0.0010);
    double limit_ask_10 = mid * (1.0 + 0.0010);

    double limit_bid_50 = mid * (1.0 - 0.0050);
    double limit_ask_50 = mid * (1.0 + 0.0050);

    double limit_bid_100 = mid * (1.0 - 0.0100);
    double limit_ask_100 = mid * (1.0 + 0.0100);

    for (const auto& [p, q] : bids) {
        if (p >= limit_bid_10) {
            depth.bid_depth_10bps += q;
        }
        if (p >= limit_bid_50) {
            depth.bid_depth_50bps += q;
        }
        if (p >= limit_bid_100) {
            depth.bid_depth_100bps += q;
        }
    }

    for (const auto& [p, q] : asks) {
        if (p <= limit_ask_10) {
            depth.ask_depth_10bps += q;
        }
        if (p <= limit_ask_50) {
            depth.ask_depth_50bps += q;
        }
        if (p <= limit_ask_100) {
            depth.ask_depth_100bps += q;
        }
    }

    return depth;
}

json OrderBook::toJson(int depth, const SignalState& sig) const
{
    json out;

    out["mid_price"] = sig.mid;
    out["obi"]       = sig.obi_raw;

    out["ret"]           = sig.ret;
    out["return_zscore"] = sig.return_zscore;
    out["vol_estimate"]  = sig.vol_estimate;

    out["park_vol"]       = sig.park_vol;
    out["obi_normalized"] = sig.obi_normalized;

    out["vpin"] = sig.vpin;

    out["obi_kalman"]        = sig.obi_kalman;
    out["kalman_innovation"] = sig.kalman_innovation;
    out["innovation_zscore"] = sig.innovation_zscore;

    out["kyle_lambda"] = sig.kyle_lambda;
    
    // New fields
    out["micro_price"]        = sig.micro_price;
    out["micro_price_dev"]    = sig.micro_price_dev;
    out["amihud_illiquidity"] = sig.amihud_illiquidity;
    out["depth_10bps_bid"]    = sig.depth_10bps_bid;
    out["depth_10bps_ask"]    = sig.depth_10bps_ask;
    out["depth_50bps_bid"]    = sig.depth_50bps_bid;
    out["depth_50bps_ask"]    = sig.depth_50bps_ask;
    out["depth_100bps_bid"]   = sig.depth_100bps_bid;
    out["depth_100bps_ask"]   = sig.depth_100bps_ask;

    out["composite"]   = sig.composite;

    json topBids = json::array();
    int cnt = 0;
    for (const auto& [p, q] : bids) {
        topBids.push_back({{"price", p}, {"qty", q}});
        if (++cnt >= depth) break;
    }

    json topAsks = json::array();
    cnt = 0;
    for (const auto& [p, q] : asks) {
        topAsks.push_back({{"price", p}, {"qty", q}});
        if (++cnt >= depth) break;
    }

    out["bids"] = topBids;
    out["asks"] = topAsks;

    return out;
}
