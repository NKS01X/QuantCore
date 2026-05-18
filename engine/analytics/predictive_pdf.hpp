#pragma once

#include <cmath>

// predictive_pdf.hpp — short-term price move distribution engine
//
// models the next-tick price change as a Location-Scale Student-t:
//   x ~ t(mu, sigma, nu)
//
// mu    = kyle_lambda * obi_kalman * dt          (deterministic drift)
// sigma = park_vol * sqrt(dt) * (1 + vpin)       (uncertainty, vpin-penalised)
// nu    = empirically 3..5 for HFT returns        (tail thickness)
//
// we expose:
//   params()   -> fills a PdfParams struct — zero-cost, just arithmetic
//   prob_up()  -> P(x > 0), computed via the regularised incomplete beta function
//   prob_dn()  -> 1 - prob_up()
//
// the CDF of a Student-t with nu d.o.f. at standardised t0 = -mu/sigma:
//   P(x > 0) = 1 - I_{nu/(nu+t0^2)}(nu/2, 1/2) / 2     (t0 < 0 side)
//
// we use the continued fraction / series expansion for the regularised
// incomplete beta (Abramowitz & Stegun §26.5). no external deps, runs in O(1).

namespace pdf {

// ── parameters of the fitted distribution ────────────────────────────────────
struct PdfParams
{
    double mu;     // location  — expected drift
    double sigma;  // scale     — uncertainty
    double nu;     // degrees of freedom — tail fatness
};

// ── regularised incomplete beta via Lentz continued fraction ─────────────────
// I_x(a, b) for 0 < x < 1, a, b > 0
// stopping when the relative change drops below 1e-12
namespace detail {

inline double ibeta_cf(double x, double a, double b)
{
    // uses the modified Lentz algorithm
    // follows Numerical Recipes §6.4
    constexpr double EPS   = 1e-12;
    constexpr double FPMIN = 1e-300;
    constexpr int    MAXIT = 200;

    double qab = a + b;
    double qap = a + 1.0;
    double qam = a - 1.0;

    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::abs(d) < FPMIN) d = FPMIN;
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m <= MAXIT; ++m) {
        int    m2  = 2 * m;
        double aa;

        // even step
        aa = (double)m * (b - (double)m) * x / ((qam + (double)m2) * (a + (double)m2));
        d  = 1.0 + aa * d;
        if (std::abs(d) < FPMIN) d = FPMIN;
        c  = 1.0 + aa / c;
        if (std::abs(c) < FPMIN) c = FPMIN;
        d  = 1.0 / d;
        h *= d * c;

        // odd step
        aa = -(a + (double)m) * (qab + (double)m) * x
             / ((a + (double)m2) * (qap + (double)m2));
        d  = 1.0 + aa * d;
        if (std::abs(d) < FPMIN) d = FPMIN;
        c  = 1.0 + aa / c;
        if (std::abs(c) < FPMIN) c = FPMIN;
        d  = 1.0 / d;

        double delta = d * c;
        h *= delta;

        if (std::abs(delta - 1.0) < EPS) break;
    }
    return h;
}

// log of the beta function via lgamma — needed to front-factor the CF
inline double lbeta(double a, double b)
{
    return std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
}

// regularised incomplete beta I_x(a,b)
inline double ibeta(double x, double a, double b)
{
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;

    // use the symmetry relation when x > (a+1)/(a+b+2) for better convergence
    double lbeta_ab = lbeta(a, b);
    double front    = std::exp(std::log(x) * a + std::log(1.0 - x) * b - lbeta_ab);

    if (x < (a + 1.0) / (a + b + 2.0))
        return front * ibeta_cf(x, a, b) / a;
    else
        return 1.0 - front * ibeta_cf(1.0 - x, b, a) / b;
}

}  // namespace detail

// ── Student-t CDF: P(X <= t0) for X ~ t(0,1,nu) ─────────────────────────────
// using the regularised incomplete beta identity:
//   F(t0; nu) = 1 - I_{nu/(nu+t0^2)}(nu/2, 0.5) / 2    for t0 >= 0
//   F(t0; nu) =     I_{nu/(nu+t0^2)}(nu/2, 0.5) / 2    for t0 <  0
inline double student_cdf(double t0, double nu)
{
    double x  = nu / (nu + t0 * t0);
    double ib = detail::ibeta(x, nu * 0.5, 0.5);
    return t0 >= 0.0 ? 1.0 - 0.5 * ib : 0.5 * ib;
}

// ── main engine ───────────────────────────────────────────────────────────────
class PredictivePDF
{
public:
    // nu is fixed at construction — tune between 3 and 5 empirically
    // default 4 splits the difference for BTC-style microstructure
    // dt_ticks: prediction horizon in tick units (500ms default = 5 ticks @ 100ms)
    explicit PredictivePDF(double nu = 4.0, double dt_ticks = 5.0)
        : nu_(nu), dt_(dt_ticks) {}

    // live reconfiguration — called by the IPC command handler
    void set_dt(double new_dt) { dt_ = new_dt; }
    double dt()  const { return dt_; }
    double nu()  const { return nu_; }

    // build distribution params for this tick.
    // uses stored dt_ unless caller explicitly overrides
    PdfParams params(double obi_kalman,
                     double kyle_lambda,
                     double park_vol,
                     double vpin,
                     double dt_override = -1.0) const
    {
        double dt  = dt_override >= 0.0 ? dt_override : dt_;
        PdfParams p;
        p.nu    = nu_;
        p.mu    = kyle_lambda * obi_kalman * dt;
        // vpin widens uncertainty — toxic flow = we really don't know where
        // price is going, so inflate the scale
        double raw_sigma = park_vol * std::sqrt(dt);
        p.sigma = raw_sigma * (1.0 + vpin);
        return p;
    }

    // P(delta_price > 0) — probability of an up-tick next period
    // returns value in (0, 1); 0.5 = no edge, >0.5 = bullish lean
    double prob_up(const PdfParams& p) const
    {
        if (p.sigma < 1e-14) return p.mu >= 0.0 ? 1.0 : 0.0;
        // standardise: P(x > 0) = P(z > -mu/sigma)
        double t0 = -p.mu / p.sigma;
        return 1.0 - student_cdf(t0, p.nu);
    }

    double prob_dn(const PdfParams& p) const { return 1.0 - prob_up(p); }

    // directional edge: prob_up - prob_dn, range (-1, 1)
    // positive = bullish, negative = bearish
    double edge(const PdfParams& p) const { return 2.0 * prob_up(p) - 1.0; }

private:
    double nu_;
    double dt_;
};

}  // namespace pdf
