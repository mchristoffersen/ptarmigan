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
#include "chirp.hpp"
#include "recv.hpp"
#include "state.hpp"
#include "gps.hpp"

// Length of command queue
#define NCMDQ 16

// GPIO pin masks
#define AMP_GPIO_MASK (1 << 3) // Pin 4 for power amplifier gate

#define MAN_GPIO_MASK (0)                             // Manual control GPIO pins
#define ATR_GPIO_MASK (AMP_GPIO_MASK)                 // ATR GPIO pins
#define ALL_GPIO_MASK (MAN_GPIO_MASK | ATR_GPIO_MASK) // All used GPIO pins

// GPIO control mask (1 = ATR : 0 = manual)
#define CTL_GPIO_MASK (ATR_GPIO_MASK)

// GPIO direction mask (1 = out : 0 = in)
#define DDR_GPIO_MASK (ALL_GPIO_MASK)

// Output file version
#define OUT_VERSION_MAJOR 0
#define OUT_VERSION_MINOR 1

namespace po = boost::program_options;
namespace json = boost::json;
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

    if (std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S.dat", &tm) == 0)
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
    if (not vm.count("spt"))
    {
        std::cerr << "Please specify the number of samples in each recorded trace with --spt" << std::endl;
        return ~0;
    }
    if (not vm.count("chirp-cf"))
    {
        std::cerr << "Please specify the chirp center frequency with --chirp-cf" << std::endl;
        return ~0;
    }
    if (not vm.count("chirp-bw"))
    {
        std::cerr << "Please specify the chirp bandwidth with --chirp-bw" << std::endl;
        return ~0;
    }
    if (not vm.count("chirp-len"))
    {
        std::cerr << "Please specify the chirp length with --chirp-len" << std::endl;
        return ~0;
    }
    if (not vm.count("chirp-prf"))
    {
        std::cerr << "Please specify the chirp repetition frequency with --chirp-prf" << std::endl;
        return ~0;
    }
    if (not vm.count("stack"))
    {
        std::cerr << "Please specify stacking with --stack" << std::endl;
        return ~0;
    }
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
    std::string dir, file, gpsFile, stateFile;
    size_t spt, stack;
    double chirp_cf, chirp_bw, chirp_len, chirp_prf;
    float chirp_amp;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("spt", po::value<size_t>(&spt), "samples per trace")
        ("chirp-cf", po::value<double>(&chirp_cf), "Chirp center frequency in Hz")
        ("chirp-bw", po::value<double>(&chirp_bw), "Chirp bandwidth in Hz")
        ("chirp-len", po::value<double>(&chirp_len), "Chirp length in s")
        ("chirp-amp", po::value<float>(&chirp_amp)->default_value(float(1)), "amplitude of the chirp [0 to 1]")
        ("chirp-prf", po::value<double>(&chirp_prf), "Chirp repetition frequency in Hz")
        ("stack", po::value<size_t>(&stack), "Number of traces to stack before writing to disk")
        ("dir", po::value<std::string>(&dir), "Output directory")
        ("state", po::value<std::string>(&stateFile)->default_value("state.json"), "State file for GUI communication")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help"))
    {
        std::cout << boost::format("PERSEUS Radar %s") % desc << std::endl;
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
    std::string tx_cpu = "fc32";
    std::string rx_cpu = "sc16";

    // Device args
    std::string args = "type=x300";

    // Set antennas based on mode
    std::string tx_ant = "A";
    std::string rx_ant = "A";

    // Only ever using channel 0
    std::size_t channel = 0;

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Set TX and RX subdev
    std::string tx_subdev = "A:0";
    std::string rx_subdev = "A:0";
    // std::string rx_subdev = "B:0";
    usrp->set_tx_subdev_spec(tx_subdev);
    usrp->set_rx_subdev_spec(rx_subdev);

    // RX sample rate and center frequency
    double rx_rate = 100e6;
    double rx_freq = 0e6;

    // TX rate
    double tx_rate = 50e6;

    // Set clock and time source to gpsdo
    usrp->set_clock_source("gpsdo");
    usrp->set_time_source("gpsdo");

    // Set up GPIO
    usrp->set_gpio_attr("FP0", "CTRL", CTL_GPIO_MASK, ALL_GPIO_MASK);
    usrp->set_gpio_attr("FP0", "DDR", DDR_GPIO_MASK, ALL_GPIO_MASK);
    usrp->set_gpio_attr("FP0", "ATR_XX", ATR_GPIO_MASK, ATR_GPIO_MASK);
    // usrp->set_gpio_attr("FP0", "ATR_TX", ATR_GPIO_MASK, ATR_GPIO_MASK); // if using rx in B

    // set the TX sample rate
    std::cout << boost::format("Setting TX Rate: %f Msps...") % (tx_rate / 1e6) << std::endl;

    usrp->set_tx_rate(tx_rate, channel);
    tx_rate = usrp->get_tx_rate(channel);
    std::cout << boost::format("Actual TX Rate: %f Msps...") % (usrp->get_tx_rate(channel) / 1e6)
              << std::endl
              << std::endl;

    // set the RX sample rate
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rx_rate / 1e6) << std::endl;

    usrp->set_rx_rate(rx_rate, channel);
    std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate(channel) / 1e6)
              << std::endl
              << std::endl;

    // set the antennas
    usrp->set_tx_antenna(tx_ant, channel);
    usrp->set_rx_antenna(rx_ant, channel);

    // Tune TX
    std::cout << boost::format("Setting TX Freq: %f MHz...") % (chirp_cf / 1e6) << std::endl;

    uhd::tune_request_t tx_tune_request(chirp_cf);
    usrp->set_tx_freq(tx_tune_request, channel);

    double freq_tune = usrp->get_tx_freq(channel);
    std::cout << boost::format("Actual TX Freq: %f MHz...") % (freq_tune / 1e6)
              << std::endl
              << std::endl;

    // Tune RX
    std::cout << boost::format("Setting RX Freq: %f MHz...") % (rx_freq)
              << std::endl;

    uhd::tune_request_t rx_tune_request(rx_freq);
    usrp->set_rx_freq(rx_tune_request, channel);

    std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
              << std::endl
              << std::endl;

    // create a transmit streamer
    uhd::stream_args_t tx_stream_args(tx_cpu, otw);
    tx_stream_args.channels = {channel};
    tx_stream_args.args = "fullscale=1.0";
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(tx_stream_args);

    // create a receive streamer
    uhd::stream_args_t rx_stream_args(rx_cpu, otw);
    rx_stream_args.channels = {channel};
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(rx_stream_args);

    // Buffer with chirp
    std::vector<std::complex<float>> buff = chirp_complex(chirp_cf - freq_tune, chirp_bw, chirp_len, chirp_amp, tx_rate, 0.1);
    std::vector<std::complex<float> *>
        buffs(1, &buff.front());

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

    // Set up rx stream command
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = spt;
    stream_cmd.stream_now = false;

    // Set up tx metadata.
    uhd::tx_metadata_t tx_md;
    tx_md.start_of_burst = true;
    tx_md.end_of_burst = true;
    tx_md.has_time_spec = true;

    // Write header to output file
    double chirp_amp_double = static_cast<double>(chirp_amp);
    uint64_t filesig = 0xF0CACC1ADEADFA11;
    uint32_t version_major = OUT_VERSION_MAJOR;
    uint32_t version_minor = OUT_VERSION_MINOR;

    std::ofstream out(file, std::ios::binary | std::ios::trunc);

    // File signature and version number
    out.write(reinterpret_cast<const char *>(&filesig), sizeof(uint64_t));
    out.write(reinterpret_cast<const char *>(&version_major), sizeof(uint32_t));
    out.write(reinterpret_cast<const char *>(&version_minor), sizeof(uint32_t));

    // Metadata
    out.write(reinterpret_cast<const char *>(&chirp_cf), sizeof(double));
    out.write(reinterpret_cast<const char *>(&chirp_bw), sizeof(double));
    out.write(reinterpret_cast<const char *>(&chirp_len), sizeof(double));
    out.write(reinterpret_cast<const char *>(&chirp_amp_double), sizeof(double));
    out.write(reinterpret_cast<const char *>(&chirp_prf), sizeof(double));
    out.write(reinterpret_cast<const char *>(&stack), sizeof(size_t));
    out.write(reinterpret_cast<const char *>(&spt), sizeof(size_t));
    out.write(reinterpret_cast<const char *>(&rx_rate), sizeof(double));
    out.write("XXXXXX", 6);

    out.close();

    // Update status
    uhd::sensor_value_t refLock = usrp->get_mboard_sensor("ref_locked");
    update_state(stateFile, {{"radioStatus", "connected"},
                             {"refClk", usrp->get_clock_source(0)},
                             {"timeSrc", usrp->get_time_source(0)},
                             {"refLock", refLock.value}});

    // Spin up receive worker thread
    boost::thread_group workers;
    workers.create_thread(boost::bind(&recv_to_file, rx_stream, spt, stack, file, stateFile));
    workers.create_thread(boost::bind(&log_gps, usrp, gpsFile, stateFile, 1.0));

    // Set up initial time for tx and rx
    uhd::time_spec_t t0 = usrp->get_time_now();
    tx_md.time_spec = t0 + uhd::time_spec_t(0.1);
    stream_cmd.time_spec = t0 + uhd::time_spec_t(0.1);

    // Estimate device time on host
    // uhd::time_spec_t t_dev0 = usrp->get_time_now();
    // auto t_host0 = std::chrono::steady_clock::now();
    // device time estimate:
    // auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_host0).count();
    // uhd::time_spec_t now_est = t_dev0 + uhd::time_spec_t(dt);

    // send data until the signal handler gets called
    while (true)
    {
        // Break on CTRL-C
        if (stop_signal_called)
        {
            break;
        }

        // Send next command if not too far ahead in time
        if ((tx_md.time_spec - usrp->get_time_now()).get_real_secs() * chirp_prf < NCMDQ)
        {
            // Send 8 commands
            for (int i = 0; i < 8; i++)
            {
                // Transmit command
                tx_stream->send(buffs, buff.size(), tx_md);

                // Receive command
                rx_stream->issue_stream_cmd(stream_cmd);

                tx_md.time_spec += 1.0 / chirp_prf;
                stream_cmd.time_spec = tx_md.time_spec;
            }
        }

        // Sleep for 2 PRFs
        std::this_thread::sleep_for(std::chrono::microseconds((int)(2 * 1e6 / (chirp_prf))));
    }

    workers.interrupt_all();
    workers.join_all();

    update_state(stateFile, {{"acquiring", false}});

    // finished
    std::cout
        << std::endl
        << "Done!" << std::endl
        << std::endl;
    return EXIT_SUCCESS;
}
