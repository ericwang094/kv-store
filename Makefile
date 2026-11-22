CC = gcc
CFLAGS = -Wall -Wextra -g

all: out/kvstore_array out/testcase

out/kvstore_array: src/kvstore.h src/kvstore.c src/kvs_array.c src/mem_pool/mem_pool.h src/mem_pool/mem_pool.c src/server.h src/proactor.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -luring

out/testcase: tests/testcase.c src/time_util.h src/time_util.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test: tests/test_mem_pool.c src/mem_pool/mem_pool.c
	@mkdir -p out
	$(CC) $(CFLAGS) -fsanitize=address $(filter %.c,$^) -o out/test_mem_pool
	./out/test_mem_pool

clean:
	rm -rf out

.PHONY: all test clean

