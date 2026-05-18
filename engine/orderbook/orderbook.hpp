#pragma once

#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

class OrderBook
{
public:
    // Bids: descending (best bid first)
    std::map<double, double, std::greater<double>> bids;
    // Asks: ascending (best ask first)
    std::map<double, double, std::less<double>> asks;

    void update(const json& data);

    double midPrice() const;
    double obi() const;

    json toJson(int depth = 5) const;
};
