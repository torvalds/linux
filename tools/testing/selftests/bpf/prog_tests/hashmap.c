// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Tests for libbpf's hashmap.
 *
 * Copyright (c) 2019 Facebook
 */
#include "test_progs.h"
#include "bpf/hashmap.h"

static int duration = 0;

static size_t hash_fn(const void *k, void *ctx)
{
	return (long)k;
}

static bool equal_fn(const void *a, const void *b, void *ctx)
{
	return (long)a == (long)b;
}

static inline size_t next_pow_2(size_t n)
{
	size_t r = 1;

	while (r < n)
		r <<= 1;
	return r;
}

static inline size_t exp_cap(size_t sz)
{
	size_t r = next_pow_2(sz);

	if (sz * 4 / 3 > r)
		r <<= 1;
	return r;
}

#define ELEM_CNT 62

static void test_hashmap_generic(void)
{
	struct hashmap_entry *entry, *tmp;
	int err, bkt, found_cnt, i;
	long long found_msk;
	struct hashmap *map;

	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "hashmap__new",
		  "failed to create map: %ld\n", PTR_ERR(map)))
		return;

	for (i = 0; i < ELEM_CNT; i++) {
		const void *oldk, *k = (const void *)(long)i;
		void *oldv, *v = (void *)(long)(1024 + i);

		err = hashmap__update(map, k, v, &oldk, &oldv);
		if (CHECK(err != -ENOENT, "hashmap__update",
			  "unexpected result: %d\n", err))
			goto cleanup;

		if (i % 2) {
			err = hashmap__add(map, k, v);
		} else {
			err = hashmap__set(map, k, v, &oldk, &oldv);
			if (CHECK(oldk != NULL || oldv != NULL, "check_kv",
				  "unexpected k/v: %p=%p\n", oldk, oldv))
				goto cleanup;
		}

		if (CHECK(err, "elem_add", "failed to add k/v %ld = %ld: %d\n",
			       (long)k, (long)v, err))
			goto cleanup;

		if (CHECK(!hashmap__find(map, k, &oldv), "elem_find",
			  "failed to find key %ld\n", (long)k))
			goto cleanup;
		if (CHECK(oldv != v, "elem_val",
			  "found value is wrong: %ld\n", (long)oldv))
			goto cleanup;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT, "hashmap__size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap_cap",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		long k = (long)entry->key;
		long v = (long)entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 1024, "check_kv",
			  "invalid k/v pair: %ld = %ld\n", k, v))
			goto cleanup;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1, "elem_cnt",
		  "not all keys iterated: %llx\n", found_msk))
		goto cleanup;

	for (i = 0; i < ELEM_CNT; i++) {
		const void *oldk, *k = (const void *)(long)i;
		void *oldv, *v = (void *)(long)(256 + i);

		err = hashmap__add(map, k, v);
		if (CHECK(err != -EEXIST, "hashmap__add",
			  "unexpected add result: %d\n", err))
			goto cleanup;

		if (i % 2)
			err = hashmap__update(map, k, v, &oldk, &oldv);
		else
			err = hashmap__set(map, k, v, &oldk, &oldv);

		if (CHECK(err, "elem_upd",
			  "failed to update k/v %ld = %ld: %d\n",
			  (long)k, (long)v, err))
			goto cleanup;
		if (CHECK(!hashmap__find(map, k, &oldv), "elem_find",
			  "failed to find key %ld\n", (long)k))
			goto cleanup;
		if (CHECK(oldv != v, "elem_val",
			  "found value is wrong: %ld\n", (long)oldv))
			goto cleanup;
	}

	if (CHECK(hashmap__size(map) != ELEM_CNT, "hashmap__size",
		  "invalid updated map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap__capacity",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	found_msk = 0;
	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		long k = (long)entry->key;
		long v = (long)entry->value;

		found_msk |= 1ULL << k;
		if (CHECK(v - k != 256, "elem_check",
			  "invalid updated k/v pair: %ld = %ld\n", k, v))
			goto cleanup;
	}
	if (CHECK(found_msk != (1ULL << ELEM_CNT) - 1, "elem_cnt",
		  "not all keys iterated after update: %llx\n", found_msk))
		goto cleanup;

	found_cnt = 0;
	hashmap__for_each_key_entry(map, entry, (void *)0) {
		found_cnt++;
	}
	if (CHECK(!found_cnt, "found_cnt",
		  "didn't find any entries for key 0\n"))
		goto cleanup;

	found_msk = 0;
	found_cnt = 0;
	hashmap__for_each_key_entry_safe(map, entry, tmp, (void *)0) {
		const void *oldk, *k;
		void *oldv, *v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << (long)k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "failed to delete k/v %ld = %ld\n",
			  (long)k, (long)v))
			goto cleanup;
		if (CHECK(oldk != k || oldv != v, "check_old",
			  "invalid deleted k/v: expected %ld = %ld, got %ld = %ld\n",
			  (long)k, (long)v, (long)oldk, (long)oldv))
			goto cleanup;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "unexpectedly deleted k/v %ld = %ld\n",
			  (long)oldk, (long)oldv))
			goto cleanup;
	}

	if (CHECK(!found_cnt || !found_msk, "found_entries",
		  "didn't delete any key entries\n"))
		goto cleanup;
	if (CHECK(hashmap__size(map) != ELEM_CNT - found_cnt, "elem_cnt",
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != exp_cap(hashmap__size(map)),
		  "hashmap__capacity",
		  "unexpected map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;

	hashmap__for_each_entry_safe(map, entry, tmp, bkt) {
		const void *oldk, *k;
		void *oldv, *v;

		k = entry->key;
		v = entry->value;

		found_cnt++;
		found_msk |= 1ULL << (long)k;

		if (CHECK(!hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "failed to delete k/v %ld = %ld\n",
			  (long)k, (long)v))
			goto cleanup;
		if (CHECK(oldk != k || oldv != v, "elem_check",
			  "invalid old k/v: expect %ld = %ld, got %ld = %ld\n",
			  (long)k, (long)v, (long)oldk, (long)oldv))
			goto cleanup;
		if (CHECK(hashmap__delete(map, k, &oldk, &oldv), "elem_del",
			  "unexpectedly deleted k/v %ld = %ld\n",
			  (long)k, (long)v))
			goto cleanup;
	}

	if (CHECK(found_cnt != ELEM_CNT || found_msk != (1ULL << ELEM_CNT) - 1,
		  "found_cnt",
		  "not all keys were deleted: found_cnt:%d, found_msk:%llx\n",
		  found_cnt, found_msk))
		goto cleanup;
	if (CHECK(hashmap__size(map) != 0, "hashmap__size",
		  "invalid updated map size (already deleted: %d): %zu\n",
		  found_cnt, hashmap__size(map)))
		goto cleanup;

	found_cnt = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_exists",
		      "unexpected map entries left: %ld = %ld\n",
		      (long)entry->key, (long)entry->value);
		goto cleanup;
	}

	hashmap__clear(map);
	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_exists",
		      "unexpected map entries left: %ld = %ld\n",
		      (long)entry->key, (long)entry->value);
		goto cleanup;
	}

cleanup:
	hashmap__free(map);
}

static size_t collision_hash_fn(const void *k, void *ctx)
{
	return 0;
}

static void test_hashmap_multimap(void)
{
	void *k1 = (void *)0, *k2 = (void *)1;
	struct hashmap_entry *entry;
	struct hashmap *map;
	long found_msk;
	int err, bkt;

	/* force collisions */
	map = hashmap__new(collision_hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "hashmap__new",
		  "failed to create map: %ld\n", PTR_ERR(map)))
		return;

	/* set up multimap:
	 * [0] -> 1, 2, 4;
	 * [1] -> 8, 16, 32;
	 */
	err = hashmap__append(map, k1, (void *)1);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k1, (void *)2);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k1, (void *)4);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;

	err = hashmap__append(map, k2, (void *)8);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k2, (void *)16);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;
	err = hashmap__append(map, k2, (void *)32);
	if (CHECK(err, "elem_add", "failed to add k/v: %d\n", err))
		goto cleanup;

	if (CHECK(hashmap__size(map) != 6, "hashmap_size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;

	/* verify global iteration still works and sees all values */
	found_msk = 0;
	hashmap__for_each_entry(map, entry, bkt) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (1 << 6) - 1, "found_msk",
		  "not all keys iterated: %lx\n", found_msk))
		goto cleanup;

	/* iterate values for key 1 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k1) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (1 | 2 | 4), "found_msk",
		  "invalid k1 values: %lx\n", found_msk))
		goto cleanup;

	/* iterate values for key 2 */
	found_msk = 0;
	hashmap__for_each_key_entry(map, entry, k2) {
		found_msk |= (long)entry->value;
	}
	if (CHECK(found_msk != (8 | 16 | 32), "found_msk",
		  "invalid k2 values: %lx\n", found_msk))
		goto cleanup;

cleanup:
	hashmap__free(map);
}

static void test_hashmap_empty()
{
	struct hashmap_entry *entry;
	int bkt;
	struct hashmap *map;
	void *k = (void *)0;

	/* force collisions */
	map = hashmap__new(hash_fn, equal_fn, NULL);
	if (CHECK(IS_ERR(map), "hashmap__new",
		  "failed to create map: %ld\n", PTR_ERR(map)))
		goto cleanup;

	if (CHECK(hashmap__size(map) != 0, "hashmap__size",
		  "invalid map size: %zu\n", hashmap__size(map)))
		goto cleanup;
	if (CHECK(hashmap__capacity(map) != 0, "hashmap__capacity",
		  "invalid map capacity: %zu\n", hashmap__capacity(map)))
		goto cleanup;
	if (CHECK(hashmap__find(map, k, NULL), "elem_find",
		  "unexpected find\n"))
		goto cleanup;
	if (CHECK(hashmap__delete(map, k, NULL, NULL), "elem_del",
		  "unexpected delete\n"))
		goto cleanup;

	hashmap__for_each_entry(map, entry, bkt) {
		CHECK(false, "elem_found", "unexpected iterated entry\n");
		goto cleanup;
	}
	hashmap__for_each_key_entry(map, entry, k) {
		CHECK(false, "key_found", "unexpected key entry\n");
		goto cleanup;
	}

cleanup:
	hashmap__free(map);
}

void test_hashmap()
{
	if (test__start_subtest("generic"))
		test_hashmap_generic();
	if (test__start_subtest("multimap"))
		test_hashmap_multimap();
	if (test__start_subtest("empty"))
		test_hashmap_empty();
}
