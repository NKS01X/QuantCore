#pragma once

#include <array>
#include <cmath>
#include <numeric>

// fixed size ring buffer, stack alloc so no heap mess
template<typename T, int N>
class CircularBuffer
{
public:
    void push(T val)
    {
        buf_[head_] = val;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    int  size() const { return count_; }
    bool full() const { return count_ == N; }

    void reset()
    {
        buf_.fill(T{});
        head_  = 0;
        count_ = 0;
    }

    // index 0 is oldest
    T operator[](int i) const
    {
        int start = full() ? head_ : 0;
        return buf_[(start + i) % N];
    }

    double mean() const
    {
        if (count_ == 0) return 0.0;
        double s = 0.0;
        for (int i = 0; i < count_; ++i)
            s += (double)(*this)[i];
        return s / count_;
    }

private:
    std::array<T, N> buf_{};
    int head_  = 0;
    int count_ = 0;
};

// parkinson vol from best bid/ask, no candles needed
// formula: sqrt( mean(log(ask/bid)^2) / (4*ln2) )
// rolling 500 ticks, updates every tick
class ParkinsonVol
{
    static constexpr int N = 500;
    // precompute 1/(4*ln2) at compile time
    static inline constexpr double INV_4LN2 = 1.0 / (4.0 * std::log(2.0));

public:
    void update(double best_bid, double best_ask)
    {
        if (best_bid <= 0.0 || best_ask <= 0.0) return;

        double lr = std::log(best_ask / best_bid);
        buf_.push(lr * lr);
    }

    // raw vol estimate, not annualized - caller can scale
    double vol() const
    {
        if (buf_.size() < 2) return 0.0;
        return std::sqrt(buf_.mean() * INV_4LN2);
    }

    void reset() { buf_.reset(); }

private:
    CircularBuffer<double, N> buf_;
};
