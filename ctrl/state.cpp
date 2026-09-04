#include <boost/json.hpp>
#include <exception>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <mutex>
#include <string>
#include <iterator>
#include <system_error>
#include "state.hpp"

namespace json = boost::json;

static std::mutex stateMutex;

int update_state(std::string fileName, const json::object &updates)
{
    std::lock_guard<std::mutex> lock(stateMutex);

    json::object state = load_state(fileName);

    for (auto const &kv : updates)
    {
        state[kv.key()] = kv.value();
    }

    return save_state(fileName, state);
}

json::object load_state(std::string fileName)
{
    std::ifstream file(fileName);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << fileName << std::endl;
        boost::json::object obj;
        return obj;
    }

    try
    {
        std::string file_contents((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

        file.close();

        boost::json::value json_data = boost::json::parse(file_contents);

        json::object const json_obj = json_data.as_object();

        return json_obj;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Parsing failed: " << e.what() << std::endl;
        boost::json::object obj;
        return obj;
    }
}

int save_state(std::string fileName, json::object state)
{
    std::string tmpFileName = fileName + ".tmp";
    std::ofstream file(tmpFileName);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << fileName << std::endl;
        return 1;
    }

    file << state;

    file.close();

    std::error_code ec;

    std::filesystem::rename(tmpFileName, fileName, ec);

    if (ec)
    {
        std::cerr << "Error during rename: " << ec.message() << '\n';
    }

    return 0;
}