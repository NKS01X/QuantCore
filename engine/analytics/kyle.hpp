#pragma once

#include <cmath>
#include <array>

// Kyle's lambda: slope of price change on signed order flow
// dP = lambda * V_signed + noise
// estimated with rolling OLS over last N ticks
// lambda = Cov(dP, V) / Var(V)
// high lambda = thin book, small flow moves price a lot
// using N=100 - gives reasonable responsiveness
//
// signed vol approximation (no tape so using top of book):
//   buy tick  ->  +top_ask_qty
//   sell tick ->  -top_bid_qty

class KyleLambda
{
    static constexpr int N = 100;

public:
    void push(double dp, double dv)
    {
        dp_[head_] = dp;
        dv_[head_] = dv;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    double lambda() const
    {
        if (count_ < 2) return 0.0;

        double mv = _mean(dv_);
        double mp = _mean(dp_);

        double cov = 0.0, var = 0.0;
        for (int i = 0; i < count_; ++i) {
            int idx = (head_ - count_ + i + N) % N;
            double dv = dv_[idx] - mv;
            double dp = dp_[idx] - mp;
            cov += dv * dp;
            var += dv * dv;
        }
        return var > 1e-12 ? cov / var : 0.0;
    }

    int count() const { return count_; }

    void reset()
    {
        dp_.fill(0.0);
        dv_.fill(0.0);
        head_  = 0;
        count_ = 0;
    }

private:
    std::array<double, N> dp_{};
    std::array<double, N> dv_{};
    int head_  = 0;
    int count_ = 0;

    double _mean(const std::array<double, N>& arr) const
    {
        double s = 0.0;
        for (int i = 0; i < count_; ++i)
            s += arr[(head_ - count_ + i + N) % N];
        return s / count_;
    }
};
