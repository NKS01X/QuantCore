#include "orderbook.hpp"

// Parse a full Binance depth snapshot into bid/ask maps
void OrderBook::update(const json& data)
{
    bids.clear();
    asks.clear();

    for (const auto& entry : data["bids"])
    {
        double price = std::stod(entry[0].get<std::string>());
        double qty   = std::stod(entry[1].get<std::string>());
        bids[price]  = qty;
    }

    for (const auto& entry : data["asks"])
    {
        double price = std::stod(entry[0].get<std::string>());
        double qty   = std::stod(entry[1].get<std::string>());
        asks[price]  = qty;
    }
}

// (best_bid + best_ask) / 2
double OrderBook::midPrice() const
{
    if (bids.empty() || asks.empty())
        return 0.0;

    return (bids.begin()->first + asks.begin()->first) / 2.0;
}

// (bidVol - askVol) / (bidVol + askVol) over top N levels
double OrderBook::obi() const
{
    if (bids.empty() || asks.empty())
        return 0.0;

    double bidVol = 0.0;
    double askVol = 0.0;
    int    count  = 0;

    for (const auto& [price, qty] : bids)
    {
        bidVol += qty;
        if (++count >= 5)
            break;
    }

    count = 0;
    for (const auto& [price, qty] : asks)
    {
        askVol += qty;
        if (++count >= 5)
            break;
    }

    double total = bidVol + askVol;
    return (total == 0.0) ? 0.0 : (bidVol - askVol) / total;
}

// Package mid-price, OBI, and top N bid/ask levels into a JSON object
json OrderBook::toJson(int depth) const
{
    json out;
    out["mid_price"] = midPrice();
    out["obi"]       = obi();

    json topBids = json::array();
    int  count   = 0;
    for (const auto& [price, qty] : bids)
    {
        topBids.push_back({{"price", price}, {"qty", qty}});
        if (++count >= depth)
            break;
    }

    json topAsks = json::array();
    count        = 0;
    for (const auto& [price, qty] : asks)
    {
        topAsks.push_back({{"price", price}, {"qty", qty}});
        if (++count >= depth)
            break;
    }

    out["bids"] = topBids;
    out["asks"] = topAsks;

    return out;
}
