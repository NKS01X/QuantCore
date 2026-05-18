#pragma once

#include <cmath>
#include <array>

// VPIN - volume synchronized prob of informed trading
// since we get LOB snapshots not actual trades, using tick direction (Lee-Ready):
// mid going up  -> buyer hit the ask
// mid going dn  -> seller hit the bid
// no move       -> split 50/50
// volume per tick = bid_vol + ask_vol at top of book (approximation)
// bucket vpin = |buy - sell| / total
// rolling average over last N buckets

class VPINTracker
{
    static constexpr int    BUCKET_TICKS  = 50;
    static constexpr int    BUCKET_COUNT  = 50;
    static constexpr double NEUTRAL_SPLIT = 0.5;

public:
    enum class TickSign { BUY, SELL, NEUTRAL };

    static TickSign classify(double now, double prev)
    {
        if (now > prev) return TickSign::BUY;
        if (now < prev) return TickSign::SELL;
        return TickSign::NEUTRAL;
    }

    void update(double mid_now, double mid_prev, double top_bid_qty, double top_ask_qty)
    {
        double total = top_bid_qty + top_ask_qty;
        if (total <= 0.0) return;

        TickSign s = classify(mid_now, mid_prev);
        double bv, sv;
        if (s == TickSign::BUY) {
            bv = total; sv = 0.0;
        } else if (s == TickSign::SELL) {
            bv = 0.0; sv = total;
        } else {
            bv = total * NEUTRAL_SPLIT;
            sv = total * NEUTRAL_SPLIT;
        }

        cur_buy_  += bv;
        cur_sell_ += sv;
        ++ticks_;

        if (ticks_ >= BUCKET_TICKS) {
            double tot = cur_buy_ + cur_sell_;
            double bvpin = tot > 0.0 ? std::abs(cur_buy_ - cur_sell_) / tot : 0.0;

            bucket_sum_ -= hist_[head_];
            hist_[head_] = bvpin;
            bucket_sum_ += bvpin;
            head_ = (head_ + 1) % BUCKET_COUNT;
            if (filled_ < BUCKET_COUNT) ++filled_;

            cur_buy_ = cur_sell_ = 0.0;
            ticks_ = 0;
        }
    }

    // 0 = clean, 1 = very toxic
    double current() const
    {
        if (filled_ == 0) return 0.0;
        return bucket_sum_ / (double)filled_;
    }

    void reset()
    {
        cur_buy_    = 0.0;
        cur_sell_   = 0.0;
        ticks_      = 0;
        bucket_sum_ = 0.0;
        head_       = 0;
        filled_     = 0;
        hist_.fill(0.0);
    }

private:
    double cur_buy_  = 0.0;
    double cur_sell_ = 0.0;
    int ticks_ = 0;

    std::array<double, BUCKET_COUNT> hist_{};
    double bucket_sum_ = 0.0;
    int head_   = 0;
    int filled_ = 0;
};
