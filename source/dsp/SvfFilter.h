#pragma once

#include "Parameters.h"

#include <cmath>
#include <complex>
#include <numbers>

namespace harmeq
{
// Coefficients for a TPT (topology-preserving transform) state-variable filter, after Andrew Simper /
// Cytomic, "Solving the continuous SVF equations using trapezoidal integration and equivalent currents".
// All curve types share the same core; only the output mix (m0, m1, m2) and g/k differ. The structure
// stays well-behaved when coefficients change every few samples, which is what pitch tracking needs.
struct SvfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    double g = 0.0, k = 1.0;

    static SvfCoefficients design (FilterType type, double freqHz, double q, double gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (std::numbers::pi * freqHz / sampleRate);
        double k = 1.0 / q;
        double m0 = 0.0, m1 = 0.0, m2 = 0.0;

        switch (type)
        {
            case FilterType::bell:      k = 1.0 / (q * A); m0 = 1.0; m1 = k * (A * A - 1.0); break;
            case FilterType::lowShelf:  g /= std::sqrt (A); m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case FilterType::highShelf: g *= std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
            case FilterType::highPass:  m0 = 1.0; m1 = -k; m2 = -1.0; break;
            case FilterType::lowPass:   m2 = 1.0; break;
            case FilterType::notch:     m0 = 1.0; m1 = -k; break;
        }

        SvfCoefficients c;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        c.a1 = (float) a1;
        c.a2 = (float) (g * a1);
        c.a3 = (float) (g * g * a1);
        c.m0 = (float) m0;
        c.m1 = (float) m1;
        c.m2 = (float) m2;
        c.g = g;
        c.k = k;
        return c;
    }

    // Exact frequency response at freqHz. The TPT SVF equals the bilinear transform of the analogue
    // prototype, so evaluate H(s) = m0 + (m1 s + m2) / (s^2 + k s + 1) at s = j tan(pi f / fs) / g.
    std::complex<double> response (double freqHz, double sampleRate) const
    {
        return responseFromWarped (std::tan (std::numbers::pi * freqHz / sampleRate));
    }

    // The same, given the frequency's warped value tan(pi f / fs): lets a caller drawing many curves on
    // the same frequency grid compute the tan() once per point.
    std::complex<double> responseFromWarped (double warped) const
    {
        const std::complex<double> s { 0.0, warped / g };
        return (double) m0 + ((double) m1 * s + (double) m2) / (s * s + k * s + 1.0);
    }

    double magnitude (double freqHz, double sampleRate) const { return std::abs (response (freqHz, sampleRate)); }
};

struct SvfState
{
    float ic1eq = 0.0f, ic2eq = 0.0f;

    void reset() noexcept { ic1eq = ic2eq = 0.0f; }

    float process (float v0, const SvfCoefficients& c) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = c.a1 * ic1eq + c.a2 * v3;
        const float v2 = ic2eq + c.a2 * ic1eq + c.a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};
} // namespace harmeq
