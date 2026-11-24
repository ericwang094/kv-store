#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kvstore.h"
#include "persistence/persist.h"

/*
 * kvs_malloc / kvs_free live in kvstore.c, which also has main() and needs
 * io_uring, so it can't be linked into a unit test. These copies follow the
 * same logic: use the pool first, fall back to malloc.
 */
void *kvs_malloc(mempool_t *m, size_t size) {
    void *ptr = memp_alloc(m, size);
    return ptr ? ptr : malloc(size);
}

void kvs_free(mempool_t *m, void *ptr) {
    if (memp_free(m, ptr) == -2) { // not from the pool
        free(ptr);
    }
}

// a macro (not a variable) so PATH ".tmp" is joined at compile time
#define PATH "out/test_dump.kvs"
#define TMP PATH ".tmp"

/*
 * Layout of the snapshot holding only name -> eric and age -> 18:
 *   0  magic "KVS1"      4  version      5  count
 *   9  first record: klen(4) key vlen(4) value
 *  38  crc                               42 end of file
 * Records come out in bucket order, so a test may only assume that the
 * first record starts at 9 and its key starts at 13.
 */
#define TWO_KEYS_SIZE 42
#define OFF_VERSION 4
#define OFF_COUNT 5
#define OFF_FIRST_KLEN 9
#define OFF_FIRST_KEY 13

static mempool_t pool;

static void setup(void) {
    // block size 32: strings of 32 bytes or more fall back to malloc
    assert(memp_create(&pool, 1 << 20, 32) == 0);
    assert(kvs_engine_init(&pool) == 0);
}

static void teardown(void) {
    kvs_engine_destroy();
    memp_destroy(&pool);
}

// throw away everything in memory, as if the server was restarted
static void restart(void) {
    teardown();
    setup();
}

// every test starts with no snapshot file on disk
static void clean_files(void) {
    unlink(PATH);
    unlink(TMP);
    rmdir(TMP); // test_save_failure_keeps_old_snapshot may leave a directory
}

static int count_cb(const char *key, const char *value, void *ctx) {
    (void)key;
    (void)value;
    (*(int *)ctx)++;
    return 0;
}

static int store_count(void) {
    int n = 0;
    assert(kvs_engine_traversal(count_cb, &n) == 0);
    return n;
}

static void expect(const char *key, const char *value) {
    char *got = kvs_engine_get((char *)key);
    assert(got != NULL);
    assert(strcmp(got, value) == 0);
}

static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int file_exists(const char *path) { return access(path, F_OK) == 0; }

// read the whole file into buf, return its size
static long read_file(const char *path, unsigned char *buf, size_t cap) {
    FILE *fp = fopen(path, "rb");
    assert(fp != NULL);
    long n = (long)fread(buf, 1, cap, fp);
    fclose(fp);
    return n;
}

// overwrite len bytes at offset, keeping the rest of the file
static void patch_file(long offset, const void *data, size_t len) {
    FILE *fp = fopen(PATH, "r+b");
    assert(fp != NULL);
    assert(fseek(fp, offset, SEEK_SET) == 0);
    assert(fwrite(data, 1, len, fp) == len);
    fclose(fp);
}

static void append_file(const void *data, size_t len) {
    FILE *fp = fopen(PATH, "ab");
    assert(fp != NULL);
    assert(fwrite(data, 1, len, fp) == len);
    fclose(fp);
}

// a valid snapshot with name -> eric, age -> 18, and an empty store in memory
static void make_two_key_snapshot(void) {
    clean_files();
    setup();
    assert(kvs_engine_set("name", "eric") == 0);
    assert(kvs_engine_set("age", "18") == 0);
    assert(kvs_snapshot_save(PATH) == 0);
    assert(file_size(PATH) == TWO_KEYS_SIZE);
    restart();
}

/* ---------- save ---------- */

static void test_save_byte_layout(void) {
    // one record, so the order is fixed and the whole file can be compared.
    // The crc is the standard CRC32 (python: zlib.crc32) of bytes 9..19.
    clean_files();
    setup();
    assert(kvs_engine_set("k", "v") == 0);
    assert(kvs_snapshot_save(PATH) == 0);

    const unsigned char expected[] = {
        'K', 'V', 'S', '1',     // magic
        0x01,                   // version
        0x01, 0x00, 0x00, 0x00, // count = 1 (filled in after the traversal)
        0x01, 0x00, 0x00, 0x00, // klen = 1
        'k',                    //
        0x01, 0x00, 0x00, 0x00, // vlen = 1
        'v',                    //
        0x68, 0x01, 0x7d, 0xea, // crc32 = 0xea7d0168, little-endian
    };
    unsigned char got[64];
    assert(read_file(PATH, got, sizeof(got)) == (long)sizeof(expected));
    assert(memcmp(got, expected, sizeof(expected)) == 0);
    teardown();
}

static void test_save_leaves_no_tmp(void) {
    make_two_key_snapshot();
    assert(file_exists(PATH));
    assert(!file_exists(TMP)); // renamed away
    teardown();
}

static void test_save_overwrites_previous(void) {
    make_two_key_snapshot();
    assert(kvs_snapshot_load(PATH) == 0);
    assert(kvs_engine_set("city", "sh") == 0);
    assert(kvs_engine_del("age") == 0);
    assert(kvs_snapshot_save(PATH) == 0); // second save replaces the first
    assert(!file_exists(TMP));

    restart();
    assert(kvs_snapshot_load(PATH) == 0);
    assert(store_count() == 2);
    expect("name", "eric");
    expect("city", "sh");
    assert(kvs_engine_get("age") == NULL);
    teardown();
}

static void test_save_failure_keeps_old_snapshot(void) {
    // the atomic-write guarantee: a failed save must not touch the old file
    make_two_key_snapshot();
    unsigned char before[64], after[64];
    long n_before = read_file(PATH, before, sizeof(before));

    // a directory named like the tmp file makes fopen(tmp, "wb") fail
    assert(mkdir(TMP, 0755) == 0);
    assert(kvs_engine_set("new", "data") == 0);
    assert(kvs_snapshot_save(PATH) < 0);
    rmdir(TMP);

    assert(read_file(PATH, after, sizeof(after)) == n_before);
    assert(memcmp(before, after, n_before) == 0);

    restart();
    assert(kvs_snapshot_load(PATH) == 0);
    assert(store_count() == 2);
    assert(kvs_engine_get("new") == NULL);
    teardown();
}

/* ---------- save + load round trip ---------- */

static void test_roundtrip_many_keys(void) {
    clean_files();
    setup();
    char key[16], value[16];
    for (int i = 0; i < 200; i++) {
        sprintf(key, "k%d", i);
        sprintf(value, "v%d", i);
        assert(kvs_engine_set(key, value) == 0);
    }
    assert(kvs_engine_del("k0") == 0);
    assert(kvs_engine_del("k100") == 0);
    assert(kvs_engine_mod("k7", "changed") == 0);
    assert(kvs_snapshot_save(PATH) == 0);

    restart();
    assert(store_count() == 0); // memory really is empty before load
    assert(kvs_snapshot_load(PATH) == 0);
    assert(store_count() == 198);
    for (int i = 0; i < 200; i++) {
        sprintf(key, "k%d", i);
        if (i == 0 || i == 100) {
            assert(kvs_engine_get(key) == NULL);
        } else if (i == 7) {
            expect(key, "changed");
        } else {
            sprintf(value, "v%d", i);
            expect(key, value);
        }
    }
    teardown();
}

static void test_roundtrip_empty_store(void) {
    clean_files();
    setup();
    assert(kvs_snapshot_save(PATH) == 0);
    assert(file_size(PATH) == 13); // header 9 + crc 4, no records

    restart();
    assert(kvs_snapshot_load(PATH) == 0);
    assert(store_count() == 0);
    teardown();
}

static void test_roundtrip_special_chars(void) {
    // the reason for length prefixes: spaces and newlines inside the data.
    // Set through the engine directly, the text protocol can't send these.
    clean_files();
    setup();
    assert(kvs_engine_set("hello world", "a b\nc\r\nd") == 0);
    assert(kvs_engine_set("老师", "King") == 0);
    assert(kvs_engine_set("k", "") == 0); // empty value: vlen = 0
    assert(kvs_snapshot_save(PATH) == 0);

    restart();
    assert(kvs_snapshot_load(PATH) == 0);
    assert(store_count() == 3);
    expect("hello world", "a b\nc\r\nd");
    expect("老师", "King");
    expect("k", "");
    teardown();
}

static void test_roundtrip_long_value(void) {
    // larger than the pool's block size, so the engine stores it with malloc.
    // ASan's leak check covers load + destroy freeing it.
    clean_files();
    setup();
    static char big[5000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(kvs_engine_set("big", big) == 0);
    assert(kvs_snapshot_save(PATH) == 0);

    restart();
    assert(kvs_snapshot_load(PATH) == 0);
    expect("big", big);
    teardown();
}

/* ---------- load ---------- */

static void test_load_missing_file(void) {
    clean_files();
    setup();
    assert(kvs_snapshot_load(PATH) == 1); // not an error: fresh start
    assert(store_count() == 0);
    teardown();
}

static void test_load_bad_args(void) {
    setup();
    assert(kvs_snapshot_load(NULL) < 0);
    assert(kvs_snapshot_save(NULL) < 0);
    teardown();
}

static void test_load_no_permission(void) {
    // a file that exists but can't be read is an error, not "no snapshot"
    make_two_key_snapshot();
    assert(chmod(PATH, 0) == 0);

    // root, and some mounts (e.g. CIFS/SMB shares), ignore file modes
    FILE *probe = fopen(PATH, "rb");
    if (probe != NULL) {
        fclose(probe);
        printf("  (skipped no-permission test: file modes are not enforced here)\n");
    } else {
        assert(kvs_snapshot_load(PATH) < 0);
    }

    chmod(PATH, 0644);
    teardown();
}

/*
 * Each case takes a valid two-key snapshot, damages it one way, and expects
 * load to refuse it.
 */
static void expect_load_fails(const char *what) {
    printf("  corrupt: %-28s ", what);
    fflush(stdout); // keep our line before load's message on stderr
    assert(kvs_snapshot_load(PATH) < 0);
    teardown();
}

static void test_load_corrupt_files(void) {
    printf("corrupt-file cases (the \"Snapshot load failed\" lines are expected):\n");

    make_two_key_snapshot();
    patch_file(0, "X", 1);
    expect_load_fails("bad magic");

    make_two_key_snapshot();
    patch_file(OFF_VERSION, "\x02", 1);
    expect_load_fails("unsupported version");

    make_two_key_snapshot();
    patch_file(OFF_FIRST_KEY + 1, "#", 1); // one byte inside a key
    expect_load_fails("flipped data byte (crc)");

    make_two_key_snapshot();
    patch_file(TWO_KEYS_SIZE - 1, "\x00", 1); // last byte of the crc itself
    expect_load_fails("flipped crc byte");

    make_two_key_snapshot();
    patch_file(OFF_COUNT, "\x01", 1); // count 2 -> 1
    expect_load_fails("count too small");

    make_two_key_snapshot();
    patch_file(OFF_COUNT, "\x03", 1); // count 2 -> 3
    expect_load_fails("count too large");

    make_two_key_snapshot();
    patch_file(OFF_FIRST_KLEN, "\xff\xff\xff\x7f", 4); // klen ~ 2GB
    expect_load_fails("klen over max length");

    make_two_key_snapshot();
    assert(truncate(PATH, TWO_KEYS_SIZE - 1) == 0);
    expect_load_fails("truncated crc");

    make_two_key_snapshot();
    assert(truncate(PATH, 20) == 0);
    expect_load_fails("truncated inside a record");

    make_two_key_snapshot();
    assert(truncate(PATH, 7) == 0);
    expect_load_fails("truncated header");

    make_two_key_snapshot();
    assert(truncate(PATH, 0) == 0);
    expect_load_fails("empty file");

    make_two_key_snapshot();
    append_file("x", 1);
    expect_load_fails("trailing byte after crc");
}

int main(void) {
    test_save_byte_layout();
    test_save_leaves_no_tmp();
    test_save_overwrites_previous();
    test_save_failure_keeps_old_snapshot();
    test_roundtrip_many_keys();
    test_roundtrip_empty_store();
    test_roundtrip_special_chars();
    test_roundtrip_long_value();
    test_load_missing_file();
    test_load_bad_args();
    test_load_no_permission();
    test_load_corrupt_files();
    clean_files();
    printf("all persist tests passed\n");
    return 0;
}
