CC      ?= cc
CFLAGS  ?= -O3 -march=native -Wall -Wextra -std=c11
LDLIBS  = -lz -lm

LIB     = python/octalgames/liboctal.so

all: octal $(LIB)

octal: octal.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# shared library used by the Python package (octalgames)
lib: $(LIB)
$(LIB): octal.c
	$(CC) $(CFLAGS) -Wno-unused-function -shared -fPIC -DOCTAL_NO_MAIN -o $@ $< $(LDLIBS)

# Sanity checks: sparse vs brute force, and known periods
test: octal
	./octal .007 -n 20000 --check
	./octal .6 -n 5000 --sparse -q -o /tmp/octal-test-sp
	./octal .6 -n 20000 --check
	./octal grundy -n 8000 --check
	./octal couples -n 8000 --check
	./octal .f6 -n 3000 --check
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

.PHONY: all lib test clean venv pytest lab
