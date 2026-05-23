#pragma once

#include "parkinson.hpp" // Reuses CircularBuffer template

class AmihudIlliquidity
{
    static constexpr int N = 100; // Rolling window of 100 ticks

public:
    void update(double dp, double volume)
    {
        if (volume <= 0.0) return;
        double ratio = std::abs(dp) / volume;
        buf_.push(ratio);
    }

    double illiquidity() const
    {
        return buf_.mean();
    }

    void reset()
    {
        buf_.reset();
    }

private:
    CircularBuffer<double, N> buf_;
};
