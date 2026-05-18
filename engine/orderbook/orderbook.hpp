#pragma once

#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../analytics/analytics.hpp"

using json = nlohmann::json;

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

    json toJson(int depth, const SignalState& sig) const;
};
