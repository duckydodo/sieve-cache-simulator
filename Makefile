CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g
LDFLAGS ?= -lm
SAN_FLAGS ?= -fsanitize=address,undefined

DS_SRCS = src/dlist.c src/hashtable.c
POLICY_SRCS = src/cache.c src/fifo.c src/lru.c src/sieve.c $(DS_SRCS)
TRACE_SRCS = src/tracegen.c

.PHONY: all asan test clean

all: bin/test_ds bin/test_policies bin/test_tracegen bin/tracegen

bin/test_ds: tests/test_ds.c $(DS_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

bin/test_policies: tests/test_policies.c $(POLICY_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

bin/test_tracegen: tests/test_tracegen.c $(TRACE_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/tracegen: tools/tracegen.c $(TRACE_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

asan: CFLAGS += $(SAN_FLAGS)
asan: clean all

test: bin/test_ds bin/test_policies bin/test_tracegen bin/tracegen
	./bin/test_ds
	./bin/test_policies
	./bin/test_tracegen

clean:
	rm -rf bin obj
