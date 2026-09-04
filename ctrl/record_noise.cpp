//
// Copyright 2010-2012,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <stdexcept>
#include <complex>
#include <cstdlib>
#include <uhd/exception.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/thread.hpp>
#include <stdint.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/json.hpp>
#include <boost/bind.hpp>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "gps.hpp"

namespace po = boost::program_options;
namespace fs = std::filesystem;

// Format a Unix epoch time as YYYY-MM-DD_HH-MM-SS.dat (UTC)
std::string gps_filename(std::time_t t)
{
    std::tm tm{};

    if (gmtime_r(&t, &tm) == nullptr)
    {
        throw std::runtime_error("Failed to convert GPS time");
    }

    char buf[32];

    if (std::strftime(buf, sizeof(buf), "noise-%Y%m%d-%H%M%S.dat", &tm) == 0)
    {
        throw std::runtime_error("Failed to format GPS time");
    }

    return std::string(buf);
}

/***********************************************************************
 * Signal handlers
 **********************************************************************/
static bool stop_signal_called = false;
static void sig_int_handler(int)
{
    stop_signal_called = true;
}

int validate_inputs(po::variables_map vm)
{
    // Validate command line inputs
    if (not vm.count("dir"))
    {
        std::cerr << "Please specify output directory with --dir" << std::endl;
        return ~0;
    }
    // Check that the output directory exists
    if (not fs::is_directory(vm["dir"].as<std::string>()))
    {
        std::cerr << "Output directory " << vm["dir"].as<std::string>() << " does not exist" << std::endl;
        return ~0;
    }
    return 0;
}

/***********************************************************************
 * Main function
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe(1, true);

    // variables to be set by po
    std::string dir, file, gpsFile;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("dir", po::value<std::string>(&dir), "Output directory")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help"))
    {
        std::cout << boost::format("X310 noise recording %s") % desc << std::endl;
        return ~0;
    }

    // Validate inputs
    if (validate_inputs(vm))
    {
        std::cerr << "Invalid input. Exiting program" << std::endl;
        return ~0;
    }

    // Exchange formats
    std::string otw = "sc16";
    std::string rx_cpu = "sc16";

    // Device args
    std::string args = "type=x300";

    // Set antennas based on mode
    std::string rx_ant = "A";

    // Only ever using channel 0
    std::size_t channel = 0;

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Set RX subdev
    std::string rx_subdev = "A:0";
    // std::string rx_subdev = "B:0";
    usrp->set_rx_subdev_spec(rx_subdev);

    // RX sample rate and center frequency
    double rx_rate = 100e6;
    double rx_freq = 0e6;

    // Set clock and time source to gpsdo
    usrp->set_clock_source("gpsdo");
    usrp->set_time_source("gpsdo");

    // set the RX sample rate
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rx_rate / 1e6) << std::endl;

    usrp->set_rx_rate(rx_rate, channel);
    std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate(channel) / 1e6)
              << std::endl
              << std::endl;

    // set the antennas
    usrp->set_rx_antenna(rx_ant, channel);

    // Tune RX
    std::cout << boost::format("Setting RX Freq: %f MHz...") % (rx_freq)
              << std::endl;

    uhd::tune_request_t rx_tune_request(rx_freq);
    usrp->set_rx_freq(rx_tune_request, channel);

    std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
              << std::endl
              << std::endl;

    // create a receive streamer
    uhd::stream_args_t rx_stream_args(rx_cpu, otw);
    rx_stream_args.channels = {channel};
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(rx_stream_args);

    // Set device time to gps time
    std::cout
        << boost::format("Setting device time to GPS time...") << std::endl;
    uhd::time_spec_t last = usrp->get_time_last_pps();
    uhd::time_spec_t next = usrp->get_time_last_pps();
    while (next == last)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        last = next;
        next = usrp->get_time_last_pps();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Device time starts at the GPS second following the next PPS edge
    int64_t gpsTime = usrp->get_mboard_sensor("gps_time").to_int() + 1;
    usrp->set_time_next_pps(uhd::time_spec_t(gpsTime));

    // Name output files for that second
    file = (fs::path(dir) / gps_filename(gpsTime)).string();
    gpsFile = fs::path(file).replace_extension(".gps").string();

    if (fs::exists(file))
    {
        std::cerr << "Output file " << file << " already exists" << std::endl;
        return ~0;
    }

    std::cout << boost::format("Output file: %s") % file << std::endl;

    // Let things settle
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Set up interrupt handler
    std::signal(SIGINT, &sig_int_handler);
    std::signal(SIGTERM, &sig_int_handler);

    // std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // Start gps logging thread
    // Spin up receive worker thread
    std::string noState;
    boost::thread_group workers;
    workers.create_thread(boost::bind(&log_gps, usrp, gpsFile, noState, 1.0));

    // Set up rx stream command
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.stream_now = true;
    uhd::rx_metadata_t rx_md;
    size_t samps_per_buff = 262144;
    std::vector<std::vector<std::complex<short>>> rx_buffs(1, std::vector<std::complex<short>>(samps_per_buff));
    std::vector<short> trace_buffer(samps_per_buff, 0);
    bool first_buff = true;
    size_t total_samps = 0;

    // Output file
    std::ofstream out(file, std::ios::binary | std::ios::trunc);

    // Issue command
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    rx_stream->issue_stream_cmd(stream_cmd);

    // send data until the signal handler gets called
    while (not stop_signal_called)
    {
        size_t num_rx_samps =
            rx_stream->recv(rx_buffs, samps_per_buff, rx_md, 3.0);

        if (rx_md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            std::cout << std::endl
                      << "Timeout while streaming" << std::endl;
            break;
        }
        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            std::string error = "Receiver error: " + rx_md.strerror();
            throw std::runtime_error(error);
        }

        // Add to trace buffer
        for (size_t i = 0; i < samps_per_buff; i++)
        {
            trace_buffer[i] = rx_buffs[0][i].real();
        }

        if (first_buff)
        {
            first_buff = false;
            uhd::time_spec_t time = rx_md.time_spec;
            int64_t full_secs = time.get_full_secs();
            double frac_secs = time.get_frac_secs();
            out.write(reinterpret_cast<const char *>(&full_secs), sizeof(int64_t));
            out.write(reinterpret_cast<const char *>(&frac_secs), sizeof(double));
        }

        out.write(reinterpret_cast<const char *>(trace_buffer.data()), trace_buffer.size() * sizeof(short));

        total_samps = total_samps + num_rx_samps;
        std::cout << "\rMegasamples recorded: " << int(total_samps / 1e6) << std::flush;
    }

    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);

    out.close();

    std::cout << std::endl;

    workers.interrupt_all();
    workers.join_all();

    // finished
    std::cout
        << std::endl
        << "Done!" << std::endl
        << std::endl;
    return EXIT_SUCCESS;
}
