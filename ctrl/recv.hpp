#pragma once

#include <cstddef>
#include <string>
#include <uhd/stream.hpp>

int recv_to_file(
    uhd::rx_streamer::sptr rx_stream,
    size_t spt,
    size_t stack,
    std::string file,
    std::string stateFile);