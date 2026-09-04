#include <string>
#include <cstddef>
#include <ios>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <uhd/exception.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/thread.hpp>
#include <boost/json.hpp>
#include <fstream>
#include <complex>
#include <vector>
#include "recv.hpp"
#include "state.hpp"

namespace json = boost::json;

static void writeTrace(std::ofstream &out, const std::vector<int> &data, const std::string &stateFile, size_t ntrace, uhd::time_spec_t time)
{
    // Write time of last raw trace
    int64_t full_secs = time.get_full_secs();
    double frac_secs = time.get_frac_secs();
    out.write(reinterpret_cast<const char *>(&full_secs), sizeof(int64_t));
    out.write(reinterpret_cast<const char *>(&frac_secs), sizeof(double));

    // Write out trace
    out.write(reinterpret_cast<const char *>(data.data()), data.size() * sizeof(int));
    out.flush();

    if (ntrace % 10 == 0)
    {
        update_state(stateFile, {{"rxTraces", ntrace},
                                 {"lastTrace", json::value_from(data)}});
    }
}

int recv_to_file(
    uhd::rx_streamer::sptr rx_stream,
    size_t spt,
    size_t stack,
    std::string file,
    std::string stateFile)
{
    uhd::set_thread_priority_safe(1, true);

    size_t ntrace = 0;
    size_t nstack = 0;

    // RX receive buffer and metadata
    std::vector<std::vector<std::complex<short>>> rx_buffs(1, std::vector<std::complex<short>>(spt));
    uhd::rx_metadata_t rx_md;

    std::vector<int> trace_buffer(spt, 0);

    update_state(stateFile, {{"acquiring", true},
                             {"fileName", file},
                             {"rxTraces", 0}});

    // output file
    std::ofstream out(file, std::ios::binary | std::ios::app);

    try
    {
        while (true)
        {
            boost::this_thread::interruption_point();

            // Receive data
            size_t n = rx_stream->recv(rx_buffs, spt, rx_md, 1.0);

            if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                std::cerr << "RX error: " << rx_md.strerror() << std::endl;
                continue; // drop this pulse
            }
            if (n != spt)
            {
                std::cerr << "Short read: " << n << " of " << spt << std::endl;
                continue; // drop this pulse
            }

            // Add to trace buffer
            for (size_t i = 0; i < spt; i++)
            {
                trace_buffer[i] += rx_buffs[0][i].real();
            }

            // Save and reset trace if stacking complete
            nstack++;
            if (nstack == stack)
            {
                ntrace++;
                writeTrace(out, trace_buffer, stateFile, ntrace, rx_md.time_spec);

                for (size_t i = 0; i < spt; i++)
                {
                    trace_buffer[i] = 0;
                }
                nstack = 0;

                // std::cout << "Writing trace " << ntrace << "\r" << std::flush;
            }
        }
    }
    catch (const boost::thread_interrupted &)
    {
        std::cout << std::endl
                  << std::endl
                  << "RX worker received interruption" << std::endl;
        return 0;
    }

    std::cout << std::endl
              << "You shouldn't be here..." << std::endl;
    return 0;
}