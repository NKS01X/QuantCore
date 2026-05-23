#pragma once

// pull in all the signal stuff from one place
#include "welford.hpp"
#include "parkinson.hpp"
#include "vpin.hpp"
#include "kalman.hpp"
#include "kyle.hpp"
#include "predictive_pdf.hpp"
#include "amihud.hpp"

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

    // predictive pdf (Location-Scale Student-t)
    double pdf_mu      = 0.0;   // expected drift  (mu = lambda * obi_kalman * dt)
    double pdf_sigma   = 0.0;   // uncertainty scale (vpin-penalised parkinson vol)
    double pdf_prob_up = 0.0;   // P(delta_price > 0)  [0, 1]
    double pdf_edge    = 0.0;   // directional edge = 2*prob_up - 1  [-1, 1]

    // LOB Micro-Price
    double micro_price     = 0.0;
    double micro_price_dev = 0.0; // micro_price - mid

    // Amihud Illiquidity
    double amihud_illiquidity = 0.0;

    // Cumulative Depth Cushion (Bid & Ask for 10, 50, 100 bps)
    double depth_10bps_bid  = 0.0;
    double depth_10bps_ask  = 0.0;
    double depth_50bps_bid  = 0.0;
    double depth_50bps_ask  = 0.0;
    double depth_100bps_bid = 0.0;
    double depth_100bps_ask = 0.0;

    // final output
    double composite = 0.0;
};
