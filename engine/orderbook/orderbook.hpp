#pragma once

#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../analytics/analytics.hpp"

using json = nlohmann::json;

struct CumulativeDepth
{
    double bid_depth_10bps  = 0.0;
    double ask_depth_10bps  = 0.0;
    double bid_depth_50bps  = 0.0;
    double ask_depth_50bps  = 0.0;
    double bid_depth_100bps = 0.0;
    double ask_depth_100bps = 0.0;
};

class OrderBook
{
public:
    std::map<double, double, std::greater<double>> bids;  // descending
    std::map<double, double, std::less<double>>    asks;  // ascending

    void update(const json& data);

    double midPrice()  const;
    double obi()       const;
    double bestBid()   const;
    double bestAsk()   const;
    double topBidQty() const;
    double topAskQty() const;

    CumulativeDepth cumulativeDepth(double mid) const;

    json toJson(int depth, const SignalState& sig) const;
};
