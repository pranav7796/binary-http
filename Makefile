CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

COMMON = protocol.o net.o

all: bserve bcurl

bserve: server.o files.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $^

bcurl: client.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $^

test: all
	python3 tests/test_protocol.py
	python3 tests/independent_wire_audit.py -v

clean:
	rm -f bserve bcurl *.o

.PHONY: all test clean
