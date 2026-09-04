//
// Copyright 2012,2014-2016 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <algorithm>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/usrp_clock/multi_usrp_clock.hpp>
#include <uhd/utils/algorithm.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/json.hpp>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include "state.hpp"
#include "gps.hpp"

namespace json = boost::json;
namespace po = boost::program_options;

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
        state["gpsStatus"] = "no GPSDO";
        state["gpsLat"] = nullptr;
        state["gpsLon"] = nullptr;
        state["gpsHgt"] = nullptr;
        state["gpsTime"] = nullptr;
        state["gpsSats"] = nullptr;

        save_state(stateFile, state);
        return EXIT_FAILURE;
    }

    state["radioStatus"] = "connected";

    // Create a USRP device
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_list[0]);

    // Verify GPS sensors are present
    std::vector<std::string> sensor_names = usrp->get_mboard_sensor_names(0);

    if (std::find(sensor_names.begin(), sensor_names.end(), "gps_servo") == sensor_names.end())
    {
        state["gpsStatus"] = "no GPSDO";
        save_state(stateFile, state);
        return EXIT_FAILURE;
    }

    if (uhd::has(usrp->get_clock_sources(0), "gpsdo"))
    {
        usrp->set_clock_source("gpsdo");
    }

    if (uhd::has(usrp->get_time_sources(0), "gpsdo"))
    {
        usrp->set_time_source("gpsdo");
    }

    uhd::sensor_value_t servo = usrp->get_mboard_sensor("gps_servo");
    gpsServoData info = parse_gps_servo(servo.value);
    state["gpsStatus"] = lock_state_to_string(info.lock_state);

    uhd::sensor_value_t gga = usrp->get_mboard_sensor("gps_gpgga");
    ggaData fix = parse_gga(gga.value);

    state["gpsLat"] = fix.latitude;
    state["gpsLon"] = fix.longitude;
    state["gpsHgt"] = fix.height;
    state["gpsTime"] = fix.time;
    state["gpsSats"] = fix.nsat;

    save_state(stateFile, state);

    return EXIT_SUCCESS;
}