// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include "../src/hash.h"
#include "../src/wsdb_priv.h"
#include "wsdb_test.h"

static uint32_t s_hash_size;

static uint32_t hash_fun_cache(uint32_t max_size, uint16_t len, char *key) {
    uint32_t ret = *(uint32_t *)key % max_size;
    return ret;
}

static int hash_cmp_cache(uint16_t len1, char key1[], uint16_t len2, char key2[]) {
    if ((uint32_t)(*key1) < (uint32_t)(*key2)) {
        return -1;
    } else if((uint32_t)(*key1) > (uint32_t)(*key2)) {
        return 1;
    } else {
        return 0;
    }
}

static uint32_t hash_fun(uint32_t max_size, uint16_t len, char *str) {
    uint32_t hash = 5381;
    int c;
    while (len-- > 0) {
        c = *str++;
        hash = ((hash << 5) + hash) + c; // hash*33 + c
    }
    return hash % s_hash_size;
}

static int hash_cmp(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    uint16_t i = 0;
    while (i<len1 && i<len2) {
        if (key1[i] < key2[i]) {
            return -1;
        } else if (key1[i] > key2[i]) {
            return 1;
        }
        i++;
    }
    if (len1 < len2) {
        return -1;
    } else if (len1 > len2) {
        return 1;
    }
    return 0;
}

START_TEST (test_hash_cache)
{
    unsigned long i = 0;
    unsigned long k;
    fail_if((i==1), "dummy");
	
    /* unit test code */
    s_hash_size = 5000;
    struct wsdb_hash *hash = wsdb_hash_open(
        s_hash_size, hash_fun_cache, hash_cmp_cache
    );
    fail_if((!hash), "no hash");
    mark_point();
    i = 1;
    fail_if (
        wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i)), 
        "hash push at %ull", i
    );
    i = 256;
    fail_if (
        wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i)), 
        "hash push at %d", i
    );
    i = 257;
    fail_if (
        wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i)), 
        "hash push at %d", i
    );
    mark_point();

    i = 1;
    k = (unsigned long) wsdb_hash_search(hash, 4, (char *)&i);
    fail_unless((k == 10000+i), "hash search: %ul", i);

    i = 256;
    k = (unsigned long)wsdb_hash_search(hash, 4, (char *)&i);
    fail_unless((k == 10000+i), "hash search: %i", i);

    i = 257;
    k = (unsigned long)wsdb_hash_search(hash, 4, (char *)&i);
    fail_unless((k == 10000+i), "hash search: %i", i);

    fail_if(wsdb_hash_close(hash), "hash close");
}
END_TEST
START_TEST (test_hash_trivial)
{
    unsigned long i = 0;
    fail_if((i==1), "dummy");
	
    /* unit test code */
    s_hash_size = 100;
    struct wsdb_hash *hash = wsdb_hash_open(s_hash_size, hash_fun, hash_cmp);
    fail_if((!hash), "no hash");
    for (i=1; i<100; i++) {
        mark_point();
        fail_if (
            wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i)), 
            "hash push at %d", i
        );
        mark_point();
    }
    mark_point();
    for (i=1; i<100; i++) {
        int k = (unsigned long)wsdb_hash_search(hash, 4, (char *)&i);
        fail_unless((k == 10000+i), "hash search: %i", i);
    }
    for (i=1; i<100; i += 2) {
        int k = (unsigned long)wsdb_hash_delete(hash, 4, (char *)&i);
        fail_unless((k == 10000+i), "hash delete: %i", i);
    }
    mark_point();
    for (i=2; i<100; i += 2) {
        int k = (unsigned long)wsdb_hash_search(hash, 4, (char *)&i);
        fail_unless((k == 10000+i), "later delete, hash search: %i", i);
    }
    fail_if(wsdb_hash_close(hash), "hash close");
}
END_TEST

START_TEST (test_hash_overflow)
{
    unsigned long i;
    /* unit test code */
    s_hash_size = 10;
    struct wsdb_hash *hash = wsdb_hash_open(s_hash_size, hash_fun, hash_cmp);
    for (i=1; i<50; i++) {
        if (wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i))) {
            fail("hash push: %d", i);
        }
    }
    for (i=1; i<50; i++) {
        unsigned long k = (unsigned long)wsdb_hash_search(hash, 4, (char *)&i);
        fail_unless((k == 10000+i), "hash search: %i %i", i, k);
    }
    fail_if(wsdb_hash_close(hash), "hash close");
}
END_TEST

START_TEST (test_hash_big)
{
    unsigned long i;
    /* unit test code */
    s_hash_size = 65000;
    struct wsdb_hash *hash = wsdb_hash_open(s_hash_size, hash_fun, hash_cmp);
    for (i=1; i<100000; i++) {
        if (wsdb_hash_push(hash, 4, (char *)&i, (void *)(10000 + i))) {
            fail("hash push: %d", i);
        }
    }
    for (i=1; i<100000; i++) {
        unsigned long k = (unsigned long)wsdb_hash_search(hash, 4, (char*)&i);
        fail_unless((k == 10000+i), "hash search: %i %i", i, k);
    }
    fail_if(wsdb_hash_close(hash), "hash close");
}
END_TEST

START_TEST (test_hash_big_64_key)
{
    uint64_t i;
    /* unit test code */
    s_hash_size = 32000;
    struct wsdb_hash *hash = wsdb_hash_open(s_hash_size, hash_fun, hash_cmp);
    for (i=1; i<100000; i++) {
        int *val = (int *)malloc(sizeof(int));
        *val = 10000+i;
        if (wsdb_hash_push(hash, sizeof(uint64_t), (char *)&i, (void *)val)) {
            fail("hash push: %d", i);
        }
    }
    for (i=1; i<100000; i++) {
        int *val = (int *)wsdb_hash_search(hash, sizeof(uint64_t), (char *)&i);
        fail_unless((*val == 10000+i), "hash search: %i %i", i, *val);
        free(val);
    }
    fail_if(wsdb_hash_close(hash), "hash close");
}
END_TEST

Suite *make_hash_suite(void)
{
    Suite *s = suite_create("hash");
    TCase *tc_core = tcase_create("Core");
    
    suite_add_tcase (s, tc_core);
    tcase_add_test(tc_core, test_hash_cache);
    tcase_add_test(tc_core, test_hash_overflow);
    tcase_add_test(tc_core, test_hash_trivial);
    tcase_add_test(tc_core, test_hash_big);
    tcase_add_test(tc_core, test_hash_big_64_key);
    
    return s;
}

