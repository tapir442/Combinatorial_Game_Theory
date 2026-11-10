CC      ?= cc
CFLAGS  ?= -O3 -march=native -Wall -Wextra -std=c11
LDLIBS  = -lz -lm
# OpenMP for --blocked / octal_extend_blocked(); set OMPFLAGS= to build without threads
OMPFLAGS ?= -fopenmp
# GPU support (--gpu / octal_extend_gpu()): on when the Vulkan headers and the generated
# SPIR-V header octal_gpu_spv.h exist; `make VULKAN=0` builds without it
VULKAN  ?= $(shell pkg-config --exists vulkan 2>/dev/null && test -f octal_gpu_spv.h && echo 1)
ifeq ($(VULKAN),1)
GPUFLAGS = -DOCTAL_VULKAN
GPULIBS  = -lvulkan
endif
GLSLANG ?= glslangValidator

LIB     = python/octalgames/liboctal.so

all: octal $(LIB)

octal: octal.c octal.h $(wildcard octal_gpu_spv.h)
	$(CC) $(CFLAGS) $(OMPFLAGS) $(GPUFLAGS) -o $@ $< $(LDLIBS) $(GPULIBS)

# shared library used by the Python package (octalgames)
lib: $(LIB)
$(LIB): octal.c octal.h $(wildcard octal_gpu_spv.h)
	$(CC) $(CFLAGS) $(OMPFLAGS) $(GPUFLAGS) -Wno-unused-function -shared -fPIC -DOCTAL_NO_MAIN -o $@ $< $(LDLIBS) $(GPULIBS)

# compile the GPU compute shader to SPIR-V, embedded as a C array (the result is committed,
# so only needed after changing octal_gpu.comp; apt install glslang-tools)
spv: octal_gpu.comp
	$(GLSLANG) -V --target-env vulkan1.1 --vn octal_gpu_spv -o octal_gpu_spv.h octal_gpu.comp

# GPU vs brute force (needs a Vulkan GPU)
test-gpu: octal
	./octal .007 -n 100000 --gpu --check
	./octal .6 -n 100000 --gpu --check
	./octal grundy -n 60000 --gpu --check
	./octal couples -n 60000 --gpu --check
	./octal 4.07 -n 60000 --gpu --check

# Thread-safety: 8 threads at once under ThreadSanitizer, compared with a sequential run
test-threads: tests/thread_test.c octal.c octal.h
	$(CC) -O1 -g -fsanitize=thread -DOCTAL_NO_MAIN -Wno-unused-function -o /tmp/octal-thread-test tests/thread_test.c octal.c -lz -lm -lpthread
	TSAN_OPTIONS=halt_on_error=1 /tmp/octal-thread-test

# Sanity checks: sparse and blocked vs brute force, known periods, thread safety
test: octal test-threads
	./octal .007 -n 20000 --check
	./octal .6 -n 5000 --sparse -q -o /tmp/octal-test-sp
	./octal .6 -n 20000 --check
	./octal grundy -n 8000 --check
	./octal couples -n 8000 --check
	./octal .f6 -n 3000 --check
	./octal .007 -n 100000 --blocked --check
	./octal .6 -n 100000 --blocked -j 3 --check
	./octal grundy -n 50000 --blocked --check
	./octal couples -n 50000 --blocked --check
	./octal 4.07 -n 50000 --blocked --check
	./octal .f6 -n 5000 --blocked --check
	./octal .77 -n 500 -q -o /tmp/octal-test-kayles && grep -q "period          12, preperiod 71 (PROVED" /tmp/octal-test-kayles/summary.txt && echo "Kayles period 12 OK"
	./octal .07 -n 500 -q -o /tmp/octal-test-dawson && grep -q "period          34, preperiod 53 (PROVED" /tmp/octal-test-dawson/summary.txt && echo "Dawson's Kayles period 34 OK"

clean:
	rm -f octal $(LIB)

# Python environment (uv)
venv: lib
	uv sync --extra jupyter

pytest: venv
	uv run pytest -q

lab: venv
	uv run jupyter lab notebooks/explore_octal_games.ipynb

# documentation (MkDocs): serve on http://127.0.0.1:8000, or build into site/
docs: lib
	uv sync --extra jupyter --group docs
	uv run mkdocs serve

docs-build: lib
	uv sync --extra jupyter --group docs
	uv run mkdocs build --strict

# benchmark against CGSuite (CGSUITE=path to the installation, default ~/CGSuite)
CGSUITE ?= $(HOME)/CGSuite
bench: lib
	uv run python benchmarks/bench_cgsuite.py --cgsuite $(CGSUITE)

bench-quick: lib
	uv run python benchmarks/bench_cgsuite.py --cgsuite $(CGSUITE) --quick --out /tmp/octal-bench-quick

# C reference documentation with Doxygen (output: doxygen/html/index.html)
doxygen:
	@command -v doxygen >/dev/null || { echo "doxygen not found (e.g. apt install doxygen)"; exit 1; }
	mkdir -p doxygen
	doxygen Doxyfile
	@test -s doxygen/warnings.log && cat doxygen/warnings.log || echo "doxygen: no warnings"

.PHONY: all lib spv test test-gpu test-threads clean venv pytest lab docs docs-build bench bench-quick doxygen
