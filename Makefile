CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc

SERVER_SRC = src/kvstore.c src/mem_pool/mem_pool.c src/proactor.c src/persistence/snapshot.c
SERVER_HDR = src/kvstore.h src/mem_pool/mem_pool.h src/persistence/persist.h

all: out/kvstore_array out/kvstore_hash out/test_kv_store

# each engine only adds its own files
out/kvstore_array: src/kv_engine/engine_array.c src/kvs_array.c
out/kvstore_hash: src/kv_engine/engine_hash.c src/kvs_hash.c

# one recipe for all servers: out/kvstore_<engine>, this will combine with the above command
out/kvstore_%: $(SERVER_SRC) $(SERVER_HDR)
	@mkdir -p out
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -luring

out/test_kv_store: tests/test_kv_store.c src/time_util.h src/time_util.c
	@mkdir -p out
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test: out/test_mem_pool out/test_hash
	./out/test_mem_pool
	./out/test_hash

out/test_mem_pool: tests/test_mem_pool.c src/mem_pool/mem_pool.h src/mem_pool/mem_pool.c
	@mkdir -p out
	$(CC) $(CFLAGS) -fsanitize=address $(filter %.c,$^) -o $@

out/test_hash: tests/test_hash.c src/kvs_hash.c src/kvstore.h src/mem_pool/mem_pool.h src/mem_pool/mem_pool.c
	@mkdir -p out
	$(CC) $(CFLAGS) -fsanitize=address $(filter %.c,$^) -o $@

clean:
	rm -rf out

.PHONY: all test clean

