#pragma once

#include <cmath>

// 1D kalman filter on raw OBI
// x_k = x_{k-1}   (assume true OBI drifts slowly)
// z_k = x_k + R   (raw snapshot is noisy)
//
// Q: how fast true imbalance changes, start ~1e-4
// R: how noisy the raw OBI is, start ~1e-2
//
// the innovation (z - predicted) tends to mean-revert, z-score of that
// is actually a decent entry trigger
class KalmanOBI
{
public:
    explicit KalmanOBI(double Q = 1e-4, double R = 1e-2)
        : Q_(Q), R_(R)
    {}

    double update(double obi_raw)
    {
        P_ += Q_;

        double K = P_ / (P_ + R_);
        innov_ = obi_raw - x_;
        x_ += K * innov_;
        P_ *= (1.0 - K);

        return x_;
    }

    double smoothed()   const { return x_; }
    double innovation() const { return innov_; }

    void reset()
    {
        x_     = 0.0;
        P_     = 1.0;
        innov_ = 0.0;
    }

private:
    double Q_;
    double R_;
    double x_     = 0.0;
    double P_     = 1.0;
    double innov_ = 0.0;
};
