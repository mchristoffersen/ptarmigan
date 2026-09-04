#pragma once

#include <string>
#include <vector>
#include <uhd/usrp/multi_usrp.hpp>

struct gpsServoData
{
    std::string date;
    int pps_count;
    int fine_dac;
    double utc_offset_ns;
    double freq_error_estimate;
    int sats_visible;
    int sats_tracked;
    int lock_state;
    int health_status;
};

struct ggaData
{
    std::string time;
    double latitude;
    double longitude;
    int lock_state;
    int nsat;
    double height;
};

int log_gps(
    uhd::usrp::multi_usrp::sptr usrp,
    std::string file,
    std::string stateFile,
    double interval);

double ddmm_to_dd(const std::string &raw, char direction);

std::vector<std::string> split_csv(const std::string &line);

ggaData parse_gga(const std::string &gga_str);

gpsServoData parse_gps_servo(const std::string &servo_str);

std::string lock_state_to_string(int lock_state);