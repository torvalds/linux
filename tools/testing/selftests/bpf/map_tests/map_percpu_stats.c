// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */

#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <bpf_util.h>
#include <test_maps.h>

#include "map_percpu_stats.skel.h"

#define MAX_ENTRIES			16384
#define MAX_ENTRIES_HASH_OF_MAPS	64
#define N_THREADS			8
#define MAX_MAP_KEY_SIZE		4

static void map_info(int map_fd, struct bpf_map_info *info)
{
	__u32 len = sizeof(*info);
	int ret;

	memset(info, 0, sizeof(*info));

	ret = bpf_obj_get_info_by_fd(map_fd, info, &len);
	CHECK(ret < 0, "bpf_obj_get_info_by_fd", "error: %s\n", strerror(errno));
}

static const char *map_type_to_s(__u32 type)
{
	switch (type) {
	case BPF_MAP_TYPE_HASH:
		return "HASH";
	case BPF_MAP_TYPE_PERCPU_HASH:
		return "PERCPU_HASH";
	case BPF_MAP_TYPE_LRU_HASH:
		return "LRU_HASH";
	case BPF_MAP_TYPE_LRU_PERCPU_HASH:
		return "LRU_PERCPU_HASH";
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		return "BPF_MAP_TYPE_HASH_OF_MAPS";
	default:
		return "<define-me>";
	}
}

static __u32 map_count_elements(__u32 type, int map_fd)
{
	__u32 key = -1;
	int n = 0;

	while (!bpf_map_get_next_key(map_fd, &key, &key))
		n++;
	return n;
}

#define BATCH	true

static void delete_and_lookup_batch(int map_fd, void *keys, __u32 count)
{
	static __u8 values[(8 << 10) * MAX_ENTRIES];
	void *in_batch = NULL, *out_batch;
	__u32 save_count = count;
	int ret;

	ret = bpf_map_lookup_and_delete_batch(map_fd,
					      &in_batch, &out_batch,
					      keys, values, &count,
					      NULL);

	/*
	 * Despite what uapi header says, lookup_and_delete_batch will return
	 * -ENOENT in case we successfully have deleted all elements, so check
	 * this separately
	 */
	CHECK(ret < 0 && (errno != ENOENT || !count), "bpf_map_lookup_and_delete_batch",
		       "error: %s\n", strerror(errno));

	CHECK(count != save_count,
			"bpf_map_lookup_and_delete_batch",
			"deleted not all elements: removed=%u expected=%u\n",
			count, save_count);
}

static void delete_all_elements(__u32 type, int map_fd, bool batch)
{
	static __u8 val[8 << 10]; /* enough for 1024 CPUs */
	__u32 key = -1;
	void *keys;
	__u32 i, n;
	int ret;

	keys = calloc(MAX_MAP_KEY_SIZE, MAX_ENTRIES);
	CHECK(!keys, "calloc", "error: %s\n", strerror(errno));

	for (n = 0; !bpf_map_get_next_key(map_fd, &key, &key); n++)
		memcpy(keys + n*MAX_MAP_KEY_SIZE, &key, MAX_MAP_KEY_SIZE);

	if (batch) {
		/* Can't mix delete_batch and delete_and_lookup_batch because
		 * they have different semantics in relation to the keys
		 * argument. However, delete_batch utilize map_delete_elem,
		 * so we actually test it in non-batch scenario */
		delete_and_lookup_batch(map_fd, keys, n);
	} else {
		/* Intentionally mix delete and lookup_and_delete so we can test both */
		for (i = 0; i < n; i++) {
			void *keyp = keys + i*MAX_MAP_KEY_SIZE;

			if (i % 2 || type == BPF_MAP_TYPE_HASH_OF_MAPS) {
				ret = bpf_map_delete_elem(map_fd, keyp);
				CHECK(ret < 0, "bpf_map_delete_elem",
					       "error: key %u: %s\n", i, strerror(errno));
			} else {
				ret = bpf_map_lookup_and_delete_elem(map_fd, keyp, val);
				CHECK(ret < 0, "bpf_map_lookup_and_delete_elem",
					       "error: key %u: %s\n", i, strerror(errno));
			}
		}
	}

	free(keys);
}

static bool is_lru(__u32 map_type)
{
	return map_type == BPF_MAP_TYPE_LRU_HASH ||
	       map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH;
}

struct upsert_opts {
	__u32 map_type;
	int map_fd;
	__u32 n;
};

static int create_small_hash(void)
{
	int map_fd;

	map_fd = bpf_map_create(BPF_MAP_TYPE_HASH, "small", 4, 4, 4, NULL);
	CHECK(map_fd < 0, "bpf_map_create()", "error:%s (name=%s)\n",
			strerror(errno), "small");

	return map_fd;
}

static void *patch_map_thread(void *arg)
{
	struct upsert_opts *opts = arg;
	int val;
	int ret;
	int i;

	for (i = 0; i < opts->n; i++) {
		if (opts->map_type == BPF_MAP_TYPE_HASH_OF_MAPS)
			val = create_small_hash();
		else
			val = rand();
		ret = bpf_map_update_elem(opts->map_fd, &i, &val, 0);
		CHECK(ret < 0, "bpf_map_update_elem", "key=%d error: %s\n", i, strerror(errno));

		if (opts->map_type == BPF_MAP_TYPE_HASH_OF_MAPS)
			close(val);
	}
	return NULL;
}

static void upsert_elements(struct upsert_opts *opts)
{
	pthread_t threads[N_THREADS];
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(threads); i++) {
		ret = pthread_create(&i[threads], NULL, patch_map_thread, opts);
		CHECK(ret != 0, "pthread_create", "error: %s\n", strerror(ret));
	}

	for (i = 0; i < ARRAY_SIZE(threads); i++) {
		ret = pthread_join(i[threads], NULL);
		CHECK(ret != 0, "pthread_join", "error: %s\n", strerror(ret));
	}
}

static __u32 read_cur_elements(int iter_fd)
{
	char buf[64];
	ssize_t n;
	__u32 ret;

	n = read(iter_fd, buf, sizeof(buf)-1);
	CHECK(n <= 0, "read", "error: %s\n", strerror(errno));
	buf[n] = '\0';

	errno = 0;
	ret = (__u32)strtol(buf, NULL, 10);
	CHECK(errno != 0, "strtol", "error: %s\n", strerror(errno));

	return ret;
}

static __u32 get_cur_elements(int map_id)
{
	struct map_percpu_stats *skel;
	struct bpf_link *link;
	__u32 n_elements;
	int iter_fd;
	int ret;

	skel = map_percpu_stats__open();
	CHECK(skel == NULL, "map_percpu_stats__open", "error: %s", strerror(errno));

	skel->bss->target_id = map_id;

	ret = map_percpu_stats__load(skel);
	CHECK(ret != 0, "map_percpu_stats__load", "error: %s", strerror(errno));

	link = bpf_program__attach_iter(skel->progs.dump_bpf_map, NULL);
	CHECK(!link, "bpf_program__attach_iter", "error: %s\n", strerror(errno));

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	CHECK(iter_fd < 0, "bpf_iter_create", "error: %s\n", strerror(errno));

	n_elements = read_cur_elements(iter_fd);

	close(iter_fd);
	bpf_link__destroy(link);
	map_percpu_stats__destroy(skel);

	return n_elements;
}

static void check_expected_number_elements(__u32 n_inserted, int map_fd,
					   struct bpf_map_info *info)
{
	__u32 n_real;
	__u32 n_iter;

	/* Count the current number of elements in the map by iterating through
	 * all the map keys via bpf_get_next_key
	 */
	n_real = map_count_elements(info->type, map_fd);

	/* The "real" number of elements should be the same as the inserted
	 * number of elements in all cases except LRU maps, where some elements
	 * may have been evicted
	 */
	if (n_inserted == 0 || !is_lru(info->type))
		CHECK(n_inserted != n_real, "map_count_elements",
		      "n_real(%u) != n_inserted(%u)\n", n_real, n_inserted);

	/* Count the current number of elements in the map using an iterator */
	n_iter = get_cur_elements(info->id);

	/* Both counts should be the same, as all updates are over */
	CHECK(n_iter != n_real, "get_cur_elements",
	      "n_iter=%u, expected %u (map_type=%s,map_flags=%08x)\n",
	      n_iter, n_real, map_type_to_s(info->type), info->map_flags);
}

static void __test(int map_fd)
{
	struct upsert_opts opts = {
		.map_fd = map_fd,
	};
	struct bpf_map_info info;

	map_info(map_fd, &info);
	opts.map_type = info.type;
	opts.n = info.max_entries;

	/* Reduce the number of elements we are updating such that we don't
	 * bump into -E2BIG from non-preallocated hash maps, but still will
	 * have some evictions for LRU maps  */
	if (opts.map_type != BPF_MAP_TYPE_HASH_OF_MAPS)
		opts.n -= 512;
	else
		opts.n /= 2;

	/*
	 * Upsert keys [0, n) under some competition: with random values from
	 * N_THREADS threads. Check values, then delete all elements and check
	 * values again.
	 */
	upsert_elements(&opts);
	check_expected_number_elements(opts.n, map_fd, &info);
	delete_all_elements(info.type, map_fd, !BATCH);
	check_expected_number_elements(0, map_fd, &info);

	/* Now do the same, but using batch delete operations */
	upsert_elements(&opts);
	check_expected_number_elements(opts.n, map_fd, &info);
	delete_all_elements(info.type, map_fd, BATCH);
	check_expected_number_elements(0, map_fd, &info);

	close(map_fd);
}

static int map_create_opts(__u32 type, const char *name,
			   struct bpf_map_create_opts *map_opts,
			   __u32 key_size, __u32 val_size)
{
	int max_entries;
	int map_fd;

	if (type == BPF_MAP_TYPE_HASH_OF_MAPS)
		max_entries = MAX_ENTRIES_HASH_OF_MAPS;
	else
		max_entries = MAX_ENTRIES;

	map_fd = bpf_map_create(type, name, key_size, val_size, max_entries, map_opts);
	CHECK(map_fd < 0, "bpf_map_create()", "error:%s (name=%s)\n",
			strerror(errno), name);

	return map_fd;
}

static int map_create(__u32 type, const char *name, struct bpf_map_create_opts *map_opts)
{
	return map_create_opts(type, name, map_opts, sizeof(int), sizeof(int));
}

static int create_hash(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts, .map_flags = BPF_F_NO_PREALLOC);

	return map_create(BPF_MAP_TYPE_HASH, "hash", &map_opts);
}

static int create_percpu_hash(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts, .map_flags = BPF_F_NO_PREALLOC);

	return map_create(BPF_MAP_TYPE_PERCPU_HASH, "percpu_hash", &map_opts);
}

static int create_hash_prealloc(void)
{
	return map_create(BPF_MAP_TYPE_HASH, "hash", NULL);
}

static int create_percpu_hash_prealloc(void)
{
	return map_create(BPF_MAP_TYPE_PERCPU_HASH, "percpu_hash_prealloc", NULL);
}

static int create_lru_hash(__u32 type, __u32 map_flags)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts, .map_flags = map_flags);

	return map_create(type, "lru_hash", &map_opts);
}

static int create_hash_of_maps(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts,
		.map_flags = BPF_F_NO_PREALLOC,
		.inner_map_fd = create_small_hash(),
	);
	int ret;

	ret = map_create_opts(BPF_MAP_TYPE_HASH_OF_MAPS, "hash_of_maps",
			      &map_opts, sizeof(int), sizeof(int));
	close(map_opts.inner_map_fd);
	return ret;
}

static void map_percpu_stats_hash(void)
{
	__test(create_hash());
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_percpu_hash(void)
{
	__test(create_percpu_hash());
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_hash_prealloc(void)
{
	__test(create_hash_prealloc());
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_percpu_hash_prealloc(void)
{
	__test(create_percpu_hash_prealloc());
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_lru_hash(void)
{
	__test(create_lru_hash(BPF_MAP_TYPE_LRU_HASH, 0));
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_lru_hash_no_common(void)
{
	__test(create_lru_hash(BPF_MAP_TYPE_LRU_HASH, BPF_F_NO_COMMON_LRU));
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_percpu_lru_hash(void)
{
	__test(create_lru_hash(BPF_MAP_TYPE_LRU_PERCPU_HASH, 0));
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_percpu_lru_hash_no_common(void)
{
	__test(create_lru_hash(BPF_MAP_TYPE_LRU_PERCPU_HASH, BPF_F_NO_COMMON_LRU));
	printf("test_%s:PASS\n", __func__);
}

static void map_percpu_stats_hash_of_maps(void)
{
	__test(create_hash_of_maps());
	printf("test_%s:PASS\n", __func__);
}

void test_map_percpu_stats(void)
{
	map_percpu_stats_hash();
	map_percpu_stats_percpu_hash();
	map_percpu_stats_hash_prealloc();
	map_percpu_stats_percpu_hash_prealloc();
	map_percpu_stats_lru_hash();
	map_percpu_stats_lru_hash_no_common();
	map_percpu_stats_percpu_lru_hash();
	map_percpu_stats_percpu_lru_hash_no_common();
	map_percpu_stats_hash_of_maps();
}
