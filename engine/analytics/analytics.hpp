#pragma once

// pull in all the signal stuff from one place
#include "welford.hpp"
#include "parkinson.hpp"
#include "vpin.hpp"
#include "kalman.hpp"
#include "kyle.hpp"

// everything the engine computes per tick, gets serialized into the zmq payload
struct SignalState
{
    double mid     = 0.0;
    double obi_raw = 0.0;

    // welford return stuff
    double ret           = 0.0;
    double return_zscore = 0.0;
    double vol_estimate  = 0.0;

    // parkinson
    double park_vol      = 0.0;
    double obi_normalized = 0.0;  // obi / park_vol

    // flow toxicity
    double vpin = 0.0;

    // kalman
    double obi_kalman        = 0.0;
    double kalman_innovation = 0.0;
    double innovation_zscore = 0.0;

    // kyle lambda
    double kyle_lambda = 0.0;

    // final output
    double composite = 0.0;
};
