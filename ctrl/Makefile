CC       = g++
CMPFLAGS = -O2 -Wall -Wextra
BSTFLAGS = -lboost_system -lboost_filesystem -lboost_program_options -lboost_thread -lboost_json -lboost_chrono
UHDFLAGS = -luhd

SRCS     = record_noise.cpp radar.cpp query_gps.cpp query_radio.cpp chirp.cpp recv.cpp state.cpp gps.cpp
TIDYARGS = -std=gnu++17


all: query_gps query_radio radar record_noise

record_noise: record_noise.cpp gps.cpp gps.hpp state.cpp state.hpp
	$(CC) record_noise.cpp gps.cpp state.cpp $(BSTFLAGS) $(UHDFLAGS) $(CMPFLAGS) -o record_noise

radar: chirp.cpp chirp.hpp recv.cpp recv.hpp radar.cpp state.cpp state.hpp gps.cpp gps.hpp
	$(CC) chirp.cpp recv.cpp radar.cpp state.cpp gps.cpp $(BSTFLAGS) $(UHDFLAGS) $(CMPFLAGS) -o radar

query_gps: query_gps.cpp state.cpp state.hpp gps.cpp gps.hpp
	$(CC) query_gps.cpp state.cpp gps.cpp $(BSTFLAGS) $(UHDFLAGS) $(CMPFLAGS) -o query_gps

query_radio: query_radio.cpp state.cpp state.hpp
	$(CC) query_radio.cpp state.cpp $(BSTFLAGS) $(UHDFLAGS) $(CMPFLAGS) -o query_radio

tidy:
	clang-tidy -checks='-*,misc-include-cleaner' --header-filter='.*' $(SRCS) -- $(TIDYARGS)

tidy-fix:
	clang-tidy --header-filter='.*' --fix $(SRCS) -- $(TIDYARGS)


clean:
	rm -f query_gps query_radio radar record_noise

