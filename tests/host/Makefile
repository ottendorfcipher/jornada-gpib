# Host-side C unit tests: build the portable runtime pieces with the native compiler and run
# them. `make -C tests/host` from the repository root, or `make test` at the top level.
CC      ?= cc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g -DRT_HOST_TEST -I../../include
BUILD    = ../../build/host
SRC_RT   = ../../src/rt/crt.c ../../src/rt/fmt.c

SRC_GPIB = ../../src/gpib/tnt4882.c ../../src/gpib/gpib488.c

TESTS = $(BUILD)/test_fmt $(BUILD)/test_tnt4882 $(BUILD)/test_pnpid

.PHONY: all clean run
all: run

$(BUILD)/test_fmt: test_fmt.c $(SRC_RT) ../../include/rt/rt.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_fmt.c $(SRC_RT)

$(BUILD)/test_tnt4882: test_tnt4882.c sim_tnt4882.c sim_tnt4882.h $(SRC_GPIB) ../../include/gpib/tnt4882.h ../../include/gpib/gpib488.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_tnt4882.c sim_tnt4882.c $(SRC_GPIB)

$(BUILD)/test_pnpid: test_pnpid.c ../../src/gpib/pnpid.c ../../include/gpib/pnpid.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_pnpid.c ../../src/gpib/pnpid.c

run: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; $$t || exit 1; done

clean:
	rm -rf $(BUILD)
