#include <cstddef>
#include <math.h>
#include <vector>
#include <complex>
#include <cmath>
#include "chirp.hpp"

// Tukey window
static float tukey(size_t i, size_t N, float a)
{
    const float pi = M_PI;

    if (i >= N / 2)
    {
        i = N - i - 1;
    }

    if (i < (a * N / 2))
    {
        return 0.5 * (1 - std::cos(2 * pi * i / (a * N)));
    }
    else if (i >= (a * N / 2) && i <= (N / 2))
    {
        return 1;
    }
    return 1;
}

// Generate a complex-valued linear chirp
std::vector<std::complex<float>> chirp_complex(float center_frequency, float bandwidth, float chirp_length, float amplitude, float sampling_frequency, float atuk)
{
    const float pi = M_PI;
    float rate = bandwidth / chirp_length;
    size_t nsamp = static_cast<size_t>(std::floor(chirp_length * sampling_frequency));
    float f0 = center_frequency - (bandwidth / 2.0f);
    const std::complex<float> j(0.0f, 1.0f);
    size_t npad = size_t(750e-9 * sampling_frequency); // add 500ns of zeros to extend ATR gate
    std::vector<std::complex<float>> v(nsamp + npad, std::complex<float>(0.0, 0.0));

    for (size_t i = 0; i < nsamp; ++i)
    {
        float t = static_cast<float>(i) / sampling_frequency;
        v[i] = amplitude * tukey(i, nsamp, atuk) * std::exp(j * 2.0f * pi * (f0 + (rate / 2.0f) * t) * t);
    }

    return v;
}