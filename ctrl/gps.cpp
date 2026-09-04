#include <string>
#include <sstream>
#include <stdexcept>
#include <uhd/usrp/multi_usrp.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include "state.hpp"
#include "gps.hpp"

namespace json = boost::json;

double ddmm_to_dd(const std::string &raw, char direction)
{
    // raw is ddmm.mmmm (lat) or dddmm.mmmm (lon)
    int deg_length = (direction == 'N' || direction == 'S') ? 2 : 3;
    double degrees = std::stod(raw.substr(0, deg_length));
    double minutes = std::stod(raw.substr(deg_length));
    double decimal = degrees + minutes / 60.0;
    if (direction == 'S' || direction == 'W')
    {
        decimal = -decimal;
    }
    return decimal;
}

std::vector<std::string> split_csv(const std::string &line)
{
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
    {
        fields.push_back(field);
    }
    return fields;
}

ggaData parse_gga(const std::string &gga_str)
{
    std::vector<std::string> fields = split_csv(gga_str);

    if (fields.size() < 10)
    {
        throw std::runtime_error("Malformed GGA string: " + gga_str);
    }

    ggaData data;

    const std::string &raw_time = fields[1]; // hhmmss.ss
    data.time = raw_time.substr(0, 2) + ":" + raw_time.substr(2, 2) + ":" + raw_time.substr(4, 2);

    data.latitude = ddmm_to_dd(fields[2], fields[3][0]);
    data.longitude = ddmm_to_dd(fields[4], fields[5][0]);
    data.lock_state = std::stoi(fields[6]);
    data.nsat = std::stoi(fields[7]);
    data.height = std::stod(fields[9]);

    return data;
}

gpsServoData parse_gps_servo(const std::string &servo_str)
{
    std::istringstream iss(servo_str);
    gpsServoData data;
    std::string health_hex;

    if (!(iss >> data.date >> data.pps_count >> data.fine_dac >> data.utc_offset_ns >> data.freq_error_estimate >> data.sats_visible >> data.sats_tracked >> data.lock_state >> health_hex))
    {
        throw std::runtime_error("Failed to parse GPS servo string: " + servo_str);
    }

    data.health_status = std::stoi(health_hex, nullptr, 16);

    return data;
}

std::string lock_state_to_string(int lock_state)
{
    switch (lock_state)
    {
    case 0:
        return "warmup";
    case 1:
        return "holdover";
    case 2:
        return "locking";
    case 5:
        return "holdover-locked";
    case 6:
        return "locked";
    default:
        return "unknown";
    }
}

int log_gps(uhd::usrp::multi_usrp::sptr usrp,
            std::string file,
            std::string stateFile,
            double interval)
{
    std::ofstream out(file, std::ios::app);
    if (!out.is_open())
    {
        std::cerr << "Failed to open GPS log: " << file << std::endl;
        return 1;
    }

    boost::chrono::steady_clock::time_point next = boost::chrono::steady_clock::now();
    boost::chrono::milliseconds period((int)(interval * 1000));

    try
    {
        while (true)
        {
            // Schedule the next wakeup before doing any work
            next += period;
            if (next < boost::chrono::steady_clock::now())
            {
                next = boost::chrono::steady_clock::now() + period;
            }

            try
            {
                std::string gga = usrp->get_mboard_sensor("gps_gpgga", 0).value;
                std::string gpstime = usrp->get_mboard_sensor("gps_time", 0).value;
                std::string servo = usrp->get_mboard_sensor("gps_servo").value;

                out << gpstime << "," << gga << "\n";
                out.flush();

                if (!stateFile.empty())
                {
                    ggaData fix = parse_gga(gga);
                    gpsServoData info = parse_gps_servo(servo);

                    update_state(stateFile, {{"gpsStatus", lock_state_to_string(info.lock_state)},
                                             {"gpsLat", fix.latitude},
                                             {"gpsLon", fix.longitude},
                                             {"gpsHgt", fix.height},
                                             {"gpsTime", fix.time},
                                             {"gpsSats", fix.nsat}});
                }
            }
            catch (const std::exception &e)
            {
                out << "ERROR," << e.what() << "\n";
                out.flush();
            }

            boost::this_thread::sleep_until(next);
        }
    }
    catch (const boost::thread_interrupted &)
    {
        std::cout << std::endl
                  << "GPS worker received interruption" << std::endl;
        return 0;
    }

    return 0;
}