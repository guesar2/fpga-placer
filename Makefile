CXX      ?= g++
# No -march=native: the binary must run on any x86-64 machine. Measured Nov 11:
# same speed and same FPGA01 result as -march=native on the i5-9600K.
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
LDFLAGS  ?= -pthread

SRC := src/main.cpp src/design.cpp src/placement.cpp src/optimize.cpp src/analytic.cpp src/cluster.cpp
HDR := $(wildcard src/*.hpp)

placer: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f placer

.PHONY: clean
