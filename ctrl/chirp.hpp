#pragma once

#include <vector>
#include <complex>

std::vector<std::complex<float>> chirp_complex(
    float center_frequency,
    float bandwidth,
    float chirp_length,
    float amplitude,
    float sampling_frequency,
    float atuk);