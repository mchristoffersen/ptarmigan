#pragma once

#include <string>
#include <boost/json.hpp>

int update_state(std::string fileName, const boost::json::object &updates);

boost::json::object load_state(std::string fileName);

int save_state(std::string fileName, boost::json::object state);