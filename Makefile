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

# ---- reproducing the results ---------------------------------------------------
#   make results                  every benchmark folder in BENCH_DIR (FPGA05 too, if present)
#   make FPGA01                   one benchmark (FPGA00 ... FPGA05)
#   make evaluate                 only check the .pl files already in OUT_DIR
#   make evaluate-FPGA01          same, one benchmark
#   make results BENCH_DIR=../benchmarks OUT_DIR=/tmp/out PLACER_FLAGS="--threads 2"
# Each benchmark runs with the default configuration (default time budget) and
# the seed of the delivered output (SEED_<bench>, otherwise SEED). The output
# is OUT_DIR/<bench>/new_<bench>.pl; then the course evaluator checks it.
BENCH_DIR    ?= benchmarks
OUT_DIR      ?= outputs
EVALUATOR    ?= $(firstword $(wildcard ./evaluator $(BENCH_DIR)/evaluator))
PLACER_FLAGS ?=
SEED         ?= 1
# Base seeds of the delivered outputs (best invocation of scripts/final_stats.sh).
SEED_FPGA00  ?= 1
SEED_FPGA01  ?= 17
SEED_FPGA02  ?= 9
SEED_FPGA03  ?= 1
SEED_FPGA04  ?= 5

FOUND        := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard $(BENCH_DIR)/*/*.sitemap)))))
BENCHMARKS   := $(sort FPGA00 FPGA01 FPGA02 FPGA03 FPGA04 FPGA05 $(FOUND))
EVAL_TARGETS := $(addprefix evaluate-,$(BENCHMARKS))

results: $(FOUND)

$(BENCHMARKS): placer
	@test -f $(BENCH_DIR)/$@/$@.sitemap || { echo "missing $(BENCH_DIR)/$@/$@.sitemap (set BENCH_DIR)"; exit 1; }
	@mkdir -p $(OUT_DIR)/$@
	@echo "== $@ (seed $(or $(SEED_$@),$(SEED)))"
	./placer $(BENCH_DIR)/$@/$@.sitemap $(BENCH_DIR)/$@/$@.nets $(BENCH_DIR)/$@/$@.pl \
	    $(OUT_DIR)/$@/new_$@.pl --seed $(or $(SEED_$@),$(SEED)) $(PLACER_FLAGS) | tee $(OUT_DIR)/$@/result.txt
	@$(MAKE) --no-print-directory evaluate-$@ BENCH_DIR=$(BENCH_DIR) OUT_DIR=$(OUT_DIR) EVALUATOR=$(EVALUATOR)

evaluate: $(addprefix evaluate-,$(FOUND))

$(EVAL_TARGETS): evaluate-%:
	@if [ -z "$(EVALUATOR)" ]; then echo "evaluator not found (set EVALUATOR)"; exit 1; fi
	@test -f $(OUT_DIR)/$*/new_$*.pl || { echo "missing $(OUT_DIR)/$*/new_$*.pl"; exit 1; }
	@$(EVALUATOR) $(BENCH_DIR)/$*/$*.sitemap $(BENCH_DIR)/$*/$*.nets $(BENCH_DIR)/$*/$*.pl \
	    $(OUT_DIR)/$*/new_$*.pl > $(OUT_DIR)/$*/evaluator.txt 2>&1 || true
	@pass=$$(grep -c PASS $(OUT_DIR)/$*/evaluator.txt); \
	 hp=$$(sed -n 's/.*Test Placement: *\([0-9]*\).*/\1/p' $(OUT_DIR)/$*/evaluator.txt); \
	 if [ "$$pass" -ge 2 ]; then echo "$*: legal (integrity + legality PASS), HPWL $$hp"; \
	 else echo "$*: NOT LEGAL (see $(OUT_DIR)/$*/evaluator.txt)"; exit 1; fi

.PHONY: clean results evaluate $(BENCHMARKS) $(EVAL_TARGETS)
