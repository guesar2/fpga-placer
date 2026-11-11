CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -std=c++17 -Wall -Wextra
LDFLAGS  ?= -pthread

SRC := src/main.cpp src/design.cpp src/placement.cpp src/optimize.cpp src/analytic.cpp
HDR := $(wildcard src/*.hpp)

placer: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f placer

.PHONY: clean
