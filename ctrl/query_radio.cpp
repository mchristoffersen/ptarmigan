//
// Copyright 2012,2014-2016 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/usrp_clock/multi_usrp_clock.hpp>
#include <uhd/utils/algorithm.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/device.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include "state.hpp"

namespace po = boost::program_options;
namespace json = boost::json;

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    std::string args, stateFile;

    // Set up program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
    ("help", "help message")
    ("args", po::value<std::string>(&args)->default_value(""), "Device address arguments specifying a single USRP")
    ("state", po::value<std::string>(&stateFile)->default_value("state.json"), "Radar state file")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // Print the help message
    if (vm.count("help"))
    {
        std::cout << "Query GPSDO sensors and update device state file"
                  << std::endl
                  << std::endl
                  << desc;
        return EXIT_FAILURE;
    }

    json::object state = load_state(stateFile);

    uhd::device_addr_t hint;
    uhd::device_addrs_t device_list = uhd::device::find(hint);
    if (device_list.empty())
    {
        std::cout << "No USRP devices found" << std::endl;
        state["radioStatus"] = "no radio";
        state["refClk"] = nullptr;
        state["timeSrc"] = nullptr;
        save_state(stateFile, state);
        return EXIT_FAILURE;
    }
    else
    {
        state["radioStatus"] = "connected";
    }

    save_state(stateFile, state);

    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_list[0]);

    // Set clock source to gpsdo
    if (uhd::has(usrp->get_clock_sources(0), "gpsdo"))
    {
        usrp->set_clock_source("gpsdo");
    }

    // Set time source to gpsdo
    if (uhd::has(usrp->get_time_sources(0), "gpsdo"))
    {
        usrp->set_time_source("gpsdo");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    state["refClk"] = usrp->get_clock_source(0);
    state["timeSrc"] = usrp->get_time_source(0);

    uhd::sensor_value_t refLock = usrp->get_mboard_sensor("ref_locked");

    state["refLock"] = refLock.value;

    save_state(stateFile, state);

    return EXIT_SUCCESS;
}