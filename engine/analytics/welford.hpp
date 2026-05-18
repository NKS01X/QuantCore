#pragma once

#include <cmath>

// online mean/variance - welford's algo
// no buffer needed, O(1) memory which is nice
class WelfordEstimator
{
public:
    void update(double x)
    {
        ++n_;
        double delta = x - mean_;
        mean_ += delta / (double)n_;
        double delta2 = x - mean_;
        M2_ += delta * delta2;
    }

    double mean()     const { return mean_; }
    double variance() const { return n_ > 1 ? M2_ / (double)(n_ - 1) : 0.0; }
    double stddev()   const { return std::sqrt(variance()); }

    // z-score - for crypto use |z| > 2.5 roughly (fat tails so dont be too strict)
    double zscore(double x) const
    {
        double sd = stddev();
        return sd > 1e-12 ? (x - mean_) / sd : 0.0;
    }

    long count() const { return n_; }

private:
    long   n_    = 0;
    double mean_ = 0.0;
    double M2_   = 0.0;
};
