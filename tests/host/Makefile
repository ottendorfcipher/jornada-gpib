# Host-side C unit tests: build the portable runtime pieces with the native compiler and run
# them. `make -C tests/host` from the repository root, or `make test` at the top level.
CC      ?= cc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g -DRT_HOST_TEST -I../../include
BUILD    = ../../build/host
SRC_RT   = ../../src/rt/crt.c ../../src/rt/fmt.c

TESTS = $(BUILD)/test_fmt

.PHONY: all clean run
all: run

$(BUILD)/test_fmt: test_fmt.c $(SRC_RT) ../../include/rt/rt.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_fmt.c $(SRC_RT)

run: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; $$t || exit 1; done

clean:
	rm -rf $(BUILD)
