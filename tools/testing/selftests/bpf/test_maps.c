// SPDX-License-Identifier: GPL-2.0-only
/*
 * Testsuite for eBPF maps
 *
 * Copyright (c) 2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/bpf.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"
#include "test_maps.h"
#include "testing_helpers.h"

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif

int skips;

static struct bpf_map_create_opts map_opts = { .sz = sizeof(map_opts) };

static void test_hashmap(unsigned int task, void *data)
{
	long long key, next_key, first_key, value;
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value), 2, &map_opts);
	if (fd < 0) {
		printf("Failed to create hashmap '%s'!\n", strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);

	value = 0;
	/* BPF_NOEXIST means add new element if it doesn't exist. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       /* key=1 already exists. */
	       errno == EEXIST);

	/* -1 is an invalid flag. */
	assert(bpf_map_update_elem(fd, &key, &value, -1) < 0 &&
	       errno == EINVAL);

	/* Check that key=1 can be found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 1234);

	key = 2;
	value = 1234;
	/* Insert key=2 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);

	/* Check that key=2 matches the value and delete it */
	assert(bpf_map_lookup_and_delete_elem(fd, &key, &value) == 0 && value == 1234);

	/* Check that key=2 is not found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) < 0 && errno == ENOENT);

	/* BPF_EXIST means update existing element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) < 0 &&
	       /* key=2 is not there. */
	       errno == ENOENT);

	/* Insert key=2 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == 0);

	/* key=1 and key=2 were inserted, check that key=0 cannot be
	 * inserted due to max_entries limit.
	 */
	key = 0;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       errno == E2BIG);

	/* Update existing element, though the map is full. */
	key = 1;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) == 0);
	key = 2;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);
	key = 3;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       errno == E2BIG);

	/* Check that key = 0 doesn't exist. */
	key = 0;
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, NULL, &first_key) == 0 &&
	       (first_key == 1 || first_key == 2));
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       (next_key == first_key));
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       (next_key == 1 || next_key == 2) &&
	       (next_key != first_key));
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) < 0 &&
	       errno == ENOENT);

	/* Delete both elements. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	key = 2;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == ENOENT);

	key = 0;
	/* Check that map is empty. */
	assert(bpf_map_get_next_key(fd, NULL, &next_key) < 0 &&
	       errno == ENOENT);
	assert(bpf_map_get_next_key(fd, &key, &next_key) < 0 &&
	       errno == ENOENT);

	close(fd);
}

static void test_hashmap_sizes(unsigned int task, void *data)
{
	int fd, i, j;

	for (i = 1; i <= 512; i <<= 1)
		for (j = 1; j <= 1 << 18; j <<= 1) {
			fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, i, j, 2, &map_opts);
			if (fd < 0) {
				if (errno == ENOMEM)
					return;
				printf("Failed to create hashmap key=%d value=%d '%s'\n",
				       i, j, strerror(errno));
				exit(1);
			}
			close(fd);
			usleep(10); /* give kernel time to destroy */
		}
}

static void test_hashmap_percpu(unsigned int task, void *data)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	BPF_DECLARE_PERCPU(long, value);
	long long key, next_key, first_key;
	int expected_key_mask = 0;
	int fd, i;

	fd = bpf_map_create(BPF_MAP_TYPE_PERCPU_HASH, NULL, sizeof(key),
			    sizeof(bpf_percpu(value, 0)), 2, &map_opts);
	if (fd < 0) {
		printf("Failed to create hashmap '%s'!\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		bpf_percpu(value, i) = i + 100;

	key = 1;
	/* Insert key=1 element. */
	assert(!(expected_key_mask & key));
	assert(bpf_map_update_elem(fd, &key, value, BPF_ANY) == 0);

	/* Lookup and delete elem key=1 and check value. */
	assert(bpf_map_lookup_and_delete_elem(fd, &key, value) == 0 &&
	       bpf_percpu(value,0) == 100);

	for (i = 0; i < nr_cpus; i++)
		bpf_percpu(value,i) = i + 100;

	/* Insert key=1 element which should not exist. */
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) == 0);
	expected_key_mask |= key;

	/* BPF_NOEXIST means add new element if it doesn't exist. */
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) < 0 &&
	       /* key=1 already exists. */
	       errno == EEXIST);

	/* -1 is an invalid flag. */
	assert(bpf_map_update_elem(fd, &key, value, -1) < 0 &&
	       errno == EINVAL);

	/* Check that key=1 can be found. Value could be 0 if the lookup
	 * was run from a different CPU.
	 */
	bpf_percpu(value, 0) = 1;
	assert(bpf_map_lookup_elem(fd, &key, value) == 0 &&
	       bpf_percpu(value, 0) == 100);

	key = 2;
	/* Check that key=2 is not found. */
	assert(bpf_map_lookup_elem(fd, &key, value) < 0 && errno == ENOENT);

	/* BPF_EXIST means update existing element. */
	assert(bpf_map_update_elem(fd, &key, value, BPF_EXIST) < 0 &&
	       /* key=2 is not there. */
	       errno == ENOENT);

	/* Insert key=2 element. */
	assert(!(expected_key_mask & key));
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) == 0);
	expected_key_mask |= key;

	/* key=1 and key=2 were inserted, check that key=0 cannot be
	 * inserted due to max_entries limit.
	 */
	key = 0;
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) < 0 &&
	       errno == E2BIG);

	/* Check that key = 0 doesn't exist. */
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, NULL, &first_key) == 0 &&
	       ((expected_key_mask & first_key) == first_key));
	while (!bpf_map_get_next_key(fd, &key, &next_key)) {
		if (first_key) {
			assert(next_key == first_key);
			first_key = 0;
		}
		assert((expected_key_mask & next_key) == next_key);
		expected_key_mask &= ~next_key;

		assert(bpf_map_lookup_elem(fd, &next_key, value) == 0);

		for (i = 0; i < nr_cpus; i++)
			assert(bpf_percpu(value, i) == i + 100);

		key = next_key;
	}
	assert(errno == ENOENT);

	/* Update with BPF_EXIST. */
	key = 1;
	assert(bpf_map_update_elem(fd, &key, value, BPF_EXIST) == 0);

	/* Delete both elements. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	key = 2;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == ENOENT);

	key = 0;
	/* Check that map is empty. */
	assert(bpf_map_get_next_key(fd, NULL, &next_key) < 0 &&
	       errno == ENOENT);
	assert(bpf_map_get_next_key(fd, &key, &next_key) < 0 &&
	       errno == ENOENT);

	close(fd);
}

#define VALUE_SIZE 3
static int helper_fill_hashmap(int max_entries)
{
	int i, fd, ret;
	long long key, value[VALUE_SIZE] = {};

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value),
			    max_entries, &map_opts);
	CHECK(fd < 0,
	      "failed to create hashmap",
	      "err: %s, flags: 0x%x\n", strerror(errno), map_opts.map_flags);

	for (i = 0; i < max_entries; i++) {
		key = i; value[0] = key;
		ret = bpf_map_update_elem(fd, &key, value, BPF_NOEXIST);
		CHECK(ret != 0,
		      "can't update hashmap",
		      "err: %s\n", strerror(ret));
	}

	return fd;
}

static void test_hashmap_walk(unsigned int task, void *data)
{
	int fd, i, max_entries = 10000;
	long long key, value[VALUE_SIZE], next_key;
	bool next_key_valid = true;

	fd = helper_fill_hashmap(max_entries);

	for (i = 0; bpf_map_get_next_key(fd, !i ? NULL : &key,
					 &next_key) == 0; i++) {
		key = next_key;
		assert(bpf_map_lookup_elem(fd, &key, value) == 0);
	}

	assert(i == max_entries);

	assert(bpf_map_get_next_key(fd, NULL, &key) == 0);
	for (i = 0; next_key_valid; i++) {
		next_key_valid = bpf_map_get_next_key(fd, &key, &next_key) == 0;
		assert(bpf_map_lookup_elem(fd, &key, value) == 0);
		value[0]++;
		assert(bpf_map_update_elem(fd, &key, value, BPF_EXIST) == 0);
		key = next_key;
	}

	assert(i == max_entries);

	for (i = 0; bpf_map_get_next_key(fd, !i ? NULL : &key,
					 &next_key) == 0; i++) {
		key = next_key;
		assert(bpf_map_lookup_elem(fd, &key, value) == 0);
		assert(value[0] - 1 == key);
	}

	assert(i == max_entries);
	close(fd);
}

static void test_hashmap_zero_seed(void)
{
	int i, first, second, old_flags;
	long long key, next_first, next_second;

	old_flags = map_opts.map_flags;
	map_opts.map_flags |= BPF_F_ZERO_SEED;

	first = helper_fill_hashmap(3);
	second = helper_fill_hashmap(3);

	for (i = 0; ; i++) {
		void *key_ptr = !i ? NULL : &key;

		if (bpf_map_get_next_key(first, key_ptr, &next_first) != 0)
			break;

		CHECK(bpf_map_get_next_key(second, key_ptr, &next_second) != 0,
		      "next_key for second map must succeed",
		      "key_ptr: %p", key_ptr);
		CHECK(next_first != next_second,
		      "keys must match",
		      "i: %d first: %lld second: %lld\n", i,
		      next_first, next_second);

		key = next_first;
	}

	map_opts.map_flags = old_flags;
	close(first);
	close(second);
}

static void test_arraymap(unsigned int task, void *data)
{
	int key, next_key, fd;
	long long value;

	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, sizeof(key), sizeof(value), 2, NULL);
	if (fd < 0) {
		printf("Failed to create arraymap '%s'!\n", strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);

	value = 0;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       errno == EEXIST);

	/* Check that key=1 can be found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 1234);

	key = 0;
	/* Check that key=0 is also found and zero initialized. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 0);

	/* key=0 and key=1 were inserted, check that key=2 cannot be inserted
	 * due to max_entries limit.
	 */
	key = 2;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) < 0 &&
	       errno == E2BIG);

	/* Check that key = 2 doesn't exist. */
	assert(bpf_map_lookup_elem(fd, &key, &value) < 0 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, NULL, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       next_key == 1);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) < 0 &&
	       errno == ENOENT);

	/* Delete shouldn't succeed. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == EINVAL);

	close(fd);
}

static void test_arraymap_percpu(unsigned int task, void *data)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	BPF_DECLARE_PERCPU(long, values);
	int key, next_key, fd, i;

	fd = bpf_map_create(BPF_MAP_TYPE_PERCPU_ARRAY, NULL, sizeof(key),
			    sizeof(bpf_percpu(values, 0)), 2, NULL);
	if (fd < 0) {
		printf("Failed to create arraymap '%s'!\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		bpf_percpu(values, i) = i + 100;

	key = 1;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, values, BPF_ANY) == 0);

	bpf_percpu(values, 0) = 0;
	assert(bpf_map_update_elem(fd, &key, values, BPF_NOEXIST) < 0 &&
	       errno == EEXIST);

	/* Check that key=1 can be found. */
	assert(bpf_map_lookup_elem(fd, &key, values) == 0 &&
	       bpf_percpu(values, 0) == 100);

	key = 0;
	/* Check that key=0 is also found and zero initialized. */
	assert(bpf_map_lookup_elem(fd, &key, values) == 0 &&
	       bpf_percpu(values, 0) == 0 &&
	       bpf_percpu(values, nr_cpus - 1) == 0);

	/* Check that key=2 cannot be inserted due to max_entries limit. */
	key = 2;
	assert(bpf_map_update_elem(fd, &key, values, BPF_EXIST) < 0 &&
	       errno == E2BIG);

	/* Check that key = 2 doesn't exist. */
	assert(bpf_map_lookup_elem(fd, &key, values) < 0 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, NULL, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       next_key == 1);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) < 0 &&
	       errno == ENOENT);

	/* Delete shouldn't succeed. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) < 0 && errno == EINVAL);

	close(fd);
}

static void test_arraymap_percpu_many_keys(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	BPF_DECLARE_PERCPU(long, values);
	/* nr_keys is not too large otherwise the test stresses percpu
	 * allocator more than anything else
	 */
	unsigned int nr_keys = 2000;
	int key, fd, i;

	fd = bpf_map_create(BPF_MAP_TYPE_PERCPU_ARRAY, NULL, sizeof(key),
			    sizeof(bpf_percpu(values, 0)), nr_keys, NULL);
	if (fd < 0) {
		printf("Failed to create per-cpu arraymap '%s'!\n",
		       strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		bpf_percpu(values, i) = i + 10;

	for (key = 0; key < nr_keys; key++)
		assert(bpf_map_update_elem(fd, &key, values, BPF_ANY) == 0);

	for (key = 0; key < nr_keys; key++) {
		for (i = 0; i < nr_cpus; i++)
			bpf_percpu(values, i) = 0;

		assert(bpf_map_lookup_elem(fd, &key, values) == 0);

		for (i = 0; i < nr_cpus; i++)
			assert(bpf_percpu(values, i) == i + 10);
	}

	close(fd);
}

static void test_devmap(unsigned int task, void *data)
{
	int fd;
	__u32 key, value;

	fd = bpf_map_create(BPF_MAP_TYPE_DEVMAP, NULL, sizeof(key), sizeof(value), 2, NULL);
	if (fd < 0) {
		printf("Failed to create devmap '%s'!\n", strerror(errno));
		exit(1);
	}

	close(fd);
}

static void test_devmap_hash(unsigned int task, void *data)
{
	int fd;
	__u32 key, value;

	fd = bpf_map_create(BPF_MAP_TYPE_DEVMAP_HASH, NULL, sizeof(key), sizeof(value), 2, NULL);
	if (fd < 0) {
		printf("Failed to create devmap_hash '%s'!\n", strerror(errno));
		exit(1);
	}

	close(fd);
}

static void test_queuemap(unsigned int task, void *data)
{
	const int MAP_SIZE = 32;
	__u32 vals[MAP_SIZE + MAP_SIZE/2], val;
	int fd, i;

	/* Fill test values to be used */
	for (i = 0; i < MAP_SIZE + MAP_SIZE/2; i++)
		vals[i] = rand();

	/* Invalid key size */
	fd = bpf_map_create(BPF_MAP_TYPE_QUEUE, NULL, 4, sizeof(val), MAP_SIZE, &map_opts);
	assert(fd < 0 && errno == EINVAL);

	fd = bpf_map_create(BPF_MAP_TYPE_QUEUE, NULL, 0, sizeof(val), MAP_SIZE, &map_opts);
	/* Queue map does not support BPF_F_NO_PREALLOC */
	if (map_opts.map_flags & BPF_F_NO_PREALLOC) {
		assert(fd < 0 && errno == EINVAL);
		return;
	}
	if (fd < 0) {
		printf("Failed to create queuemap '%s'!\n", strerror(errno));
		exit(1);
	}

	/* Push MAP_SIZE elements */
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_update_elem(fd, NULL, &vals[i], 0) == 0);

	/* Check that element cannot be pushed due to max_entries limit */
	assert(bpf_map_update_elem(fd, NULL, &val, 0) < 0 &&
	       errno == E2BIG);

	/* Peek element */
	assert(bpf_map_lookup_elem(fd, NULL, &val) == 0 && val == vals[0]);

	/* Replace half elements */
	for (i = MAP_SIZE; i < MAP_SIZE + MAP_SIZE/2; i++)
		assert(bpf_map_update_elem(fd, NULL, &vals[i], BPF_EXIST) == 0);

	/* Pop all elements */
	for (i = MAP_SIZE/2; i < MAP_SIZE + MAP_SIZE/2; i++)
		assert(bpf_map_lookup_and_delete_elem(fd, NULL, &val) == 0 &&
		       val == vals[i]);

	/* Check that there are not elements left */
	assert(bpf_map_lookup_and_delete_elem(fd, NULL, &val) < 0 &&
	       errno == ENOENT);

	/* Check that non supported functions set errno to EINVAL */
	assert(bpf_map_delete_elem(fd, NULL) < 0 && errno == EINVAL);
	assert(bpf_map_get_next_key(fd, NULL, NULL) < 0 && errno == EINVAL);

	close(fd);
}

static void test_stackmap(unsigned int task, void *data)
{
	const int MAP_SIZE = 32;
	__u32 vals[MAP_SIZE + MAP_SIZE/2], val;
	int fd, i;

	/* Fill test values to be used */
	for (i = 0; i < MAP_SIZE + MAP_SIZE/2; i++)
		vals[i] = rand();

	/* Invalid key size */
	fd = bpf_map_create(BPF_MAP_TYPE_STACK, NULL, 4, sizeof(val), MAP_SIZE, &map_opts);
	assert(fd < 0 && errno == EINVAL);

	fd = bpf_map_create(BPF_MAP_TYPE_STACK, NULL, 0, sizeof(val), MAP_SIZE, &map_opts);
	/* Stack map does not support BPF_F_NO_PREALLOC */
	if (map_opts.map_flags & BPF_F_NO_PREALLOC) {
		assert(fd < 0 && errno == EINVAL);
		return;
	}
	if (fd < 0) {
		printf("Failed to create stackmap '%s'!\n", strerror(errno));
		exit(1);
	}

	/* Push MAP_SIZE elements */
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_update_elem(fd, NULL, &vals[i], 0) == 0);

	/* Check that element cannot be pushed due to max_entries limit */
	assert(bpf_map_update_elem(fd, NULL, &val, 0) < 0 &&
	       errno == E2BIG);

	/* Peek element */
	assert(bpf_map_lookup_elem(fd, NULL, &val) == 0 && val == vals[i - 1]);

	/* Replace half elements */
	for (i = MAP_SIZE; i < MAP_SIZE + MAP_SIZE/2; i++)
		assert(bpf_map_update_elem(fd, NULL, &vals[i], BPF_EXIST) == 0);

	/* Pop all elements */
	for (i = MAP_SIZE + MAP_SIZE/2 - 1; i >= MAP_SIZE/2; i--)
		assert(bpf_map_lookup_and_delete_elem(fd, NULL, &val) == 0 &&
		       val == vals[i]);

	/* Check that there are not elements left */
	assert(bpf_map_lookup_and_delete_elem(fd, NULL, &val) < 0 &&
	       errno == ENOENT);

	/* Check that non supported functions set errno to EINVAL */
	assert(bpf_map_delete_elem(fd, NULL) < 0 && errno == EINVAL);
	assert(bpf_map_get_next_key(fd, NULL, NULL) < 0 && errno == EINVAL);

	close(fd);
}

#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <linux/err.h>
#define SOCKMAP_PARSE_PROG "./sockmap_parse_prog.bpf.o"
#define SOCKMAP_VERDICT_PROG "./sockmap_verdict_prog.bpf.o"
#define SOCKMAP_TCP_MSG_PROG "./sockmap_tcp_msg_prog.bpf.o"
static void test_sockmap(unsigned int tasks, void *data)
{
	struct bpf_map *bpf_map_rx, *bpf_map_tx, *bpf_map_msg, *bpf_map_break;
	int map_fd_msg = 0, map_fd_rx = 0, map_fd_tx = 0, map_fd_break;
	struct bpf_object *parse_obj, *verdict_obj, *msg_obj;
	int ports[] = {50200, 50201, 50202, 50204};
	int err, i, fd, udp, sfd[6] = {0xdeadbeef};
	u8 buf[20] = {0x0, 0x5, 0x3, 0x2, 0x1, 0x0};
	int parse_prog, verdict_prog, msg_prog;
	struct sockaddr_in addr;
	int one = 1, s, sc, rc;
	struct timeval to;
	__u32 key, value;
	pid_t pid[tasks];
	fd_set w;

	/* Create some sockets to use with sockmap */
	for (i = 0; i < 2; i++) {
		sfd[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (sfd[i] < 0)
			goto out;
		err = setsockopt(sfd[i], SOL_SOCKET, SO_REUSEADDR,
				 (char *)&one, sizeof(one));
		if (err) {
			printf("failed to setsockopt\n");
			goto out;
		}
		err = ioctl(sfd[i], FIONBIO, (char *)&one);
		if (err < 0) {
			printf("failed to ioctl\n");
			goto out;
		}
		memset(&addr, 0, sizeof(struct sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		addr.sin_port = htons(ports[i]);
		err = bind(sfd[i], (struct sockaddr *)&addr, sizeof(addr));
		if (err < 0) {
			printf("failed to bind: err %i: %i:%i\n",
			       err, i, sfd[i]);
			goto out;
		}
		err = listen(sfd[i], 32);
		if (err < 0) {
			printf("failed to listen\n");
			goto out;
		}
	}

	for (i = 2; i < 4; i++) {
		sfd[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (sfd[i] < 0)
			goto out;
		err = setsockopt(sfd[i], SOL_SOCKET, SO_REUSEADDR,
				 (char *)&one, sizeof(one));
		if (err) {
			printf("set sock opt\n");
			goto out;
		}
		memset(&addr, 0, sizeof(struct sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		addr.sin_port = htons(ports[i - 2]);
		err = connect(sfd[i], (struct sockaddr *)&addr, sizeof(addr));
		if (err) {
			printf("failed to connect\n");
			goto out;
		}
	}


	for (i = 4; i < 6; i++) {
		sfd[i] = accept(sfd[i - 4], NULL, NULL);
		if (sfd[i] < 0) {
			printf("accept failed\n");
			goto out;
		}
	}

	/* Test sockmap with connected sockets */
	fd = bpf_map_create(BPF_MAP_TYPE_SOCKMAP, NULL,
			    sizeof(key), sizeof(value),
			    6, NULL);
	if (fd < 0) {
		if (!libbpf_probe_bpf_map_type(BPF_MAP_TYPE_SOCKMAP, NULL)) {
			printf("%s SKIP (unsupported map type BPF_MAP_TYPE_SOCKMAP)\n",
			       __func__);
			skips++;
			for (i = 0; i < 6; i++)
				close(sfd[i]);
			return;
		}

		printf("Failed to create sockmap %i\n", fd);
		goto out_sockmap;
	}

	/* Test update with unsupported UDP socket */
	udp = socket(AF_INET, SOCK_DGRAM, 0);
	i = 0;
	err = bpf_map_update_elem(fd, &i, &udp, BPF_ANY);
	if (err) {
		printf("Failed socket update SOCK_DGRAM '%i:%i'\n",
		       i, udp);
		goto out_sockmap;
	}
	close(udp);

	/* Test update without programs */
	for (i = 0; i < 6; i++) {
		err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_ANY);
		if (err) {
			printf("Failed noprog update sockmap '%i:%i'\n",
			       i, sfd[i]);
			goto out_sockmap;
		}
	}

	/* Test attaching/detaching bad fds */
	err = bpf_prog_attach(-1, fd, BPF_SK_SKB_STREAM_PARSER, 0);
	if (!err) {
		printf("Failed invalid parser prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(-1, fd, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (!err) {
		printf("Failed invalid verdict prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(-1, fd, BPF_SK_MSG_VERDICT, 0);
	if (!err) {
		printf("Failed invalid msg verdict prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(-1, fd, __MAX_BPF_ATTACH_TYPE, 0);
	if (!err) {
		printf("Failed unknown prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach(fd, BPF_SK_SKB_STREAM_PARSER);
	if (!err) {
		printf("Failed empty parser prog detach\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach(fd, BPF_SK_SKB_STREAM_VERDICT);
	if (!err) {
		printf("Failed empty verdict prog detach\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach(fd, BPF_SK_MSG_VERDICT);
	if (!err) {
		printf("Failed empty msg verdict prog detach\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach(fd, __MAX_BPF_ATTACH_TYPE);
	if (!err) {
		printf("Detach invalid prog successful\n");
		goto out_sockmap;
	}

	/* Load SK_SKB program and Attach */
	err = bpf_prog_test_load(SOCKMAP_PARSE_PROG,
			    BPF_PROG_TYPE_SK_SKB, &parse_obj, &parse_prog);
	if (err) {
		printf("Failed to load SK_SKB parse prog\n");
		goto out_sockmap;
	}

	err = bpf_prog_test_load(SOCKMAP_TCP_MSG_PROG,
			    BPF_PROG_TYPE_SK_MSG, &msg_obj, &msg_prog);
	if (err) {
		printf("Failed to load SK_SKB msg prog\n");
		goto out_sockmap;
	}

	err = bpf_prog_test_load(SOCKMAP_VERDICT_PROG,
			    BPF_PROG_TYPE_SK_SKB, &verdict_obj, &verdict_prog);
	if (err) {
		printf("Failed to load SK_SKB verdict prog\n");
		goto out_sockmap;
	}

	bpf_map_rx = bpf_object__find_map_by_name(verdict_obj, "sock_map_rx");
	if (!bpf_map_rx) {
		printf("Failed to load map rx from verdict prog\n");
		goto out_sockmap;
	}

	map_fd_rx = bpf_map__fd(bpf_map_rx);
	if (map_fd_rx < 0) {
		printf("Failed to get map rx fd\n");
		goto out_sockmap;
	}

	bpf_map_tx = bpf_object__find_map_by_name(verdict_obj, "sock_map_tx");
	if (!bpf_map_tx) {
		printf("Failed to load map tx from verdict prog\n");
		goto out_sockmap;
	}

	map_fd_tx = bpf_map__fd(bpf_map_tx);
	if (map_fd_tx < 0) {
		printf("Failed to get map tx fd\n");
		goto out_sockmap;
	}

	bpf_map_msg = bpf_object__find_map_by_name(verdict_obj, "sock_map_msg");
	if (!bpf_map_msg) {
		printf("Failed to load map msg from msg_verdict prog\n");
		goto out_sockmap;
	}

	map_fd_msg = bpf_map__fd(bpf_map_msg);
	if (map_fd_msg < 0) {
		printf("Failed to get map msg fd\n");
		goto out_sockmap;
	}

	bpf_map_break = bpf_object__find_map_by_name(verdict_obj, "sock_map_break");
	if (!bpf_map_break) {
		printf("Failed to load map tx from verdict prog\n");
		goto out_sockmap;
	}

	map_fd_break = bpf_map__fd(bpf_map_break);
	if (map_fd_break < 0) {
		printf("Failed to get map tx fd\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(parse_prog, map_fd_break,
			      BPF_SK_SKB_STREAM_PARSER, 0);
	if (!err) {
		printf("Allowed attaching SK_SKB program to invalid map\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(parse_prog, map_fd_rx,
		      BPF_SK_SKB_STREAM_PARSER, 0);
	if (err) {
		printf("Failed stream parser bpf prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(verdict_prog, map_fd_rx,
			      BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err) {
		printf("Failed stream verdict bpf prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(msg_prog, map_fd_msg, BPF_SK_MSG_VERDICT, 0);
	if (err) {
		printf("Failed msg verdict bpf prog attach\n");
		goto out_sockmap;
	}

	err = bpf_prog_attach(verdict_prog, map_fd_rx,
			      __MAX_BPF_ATTACH_TYPE, 0);
	if (!err) {
		printf("Attached unknown bpf prog\n");
		goto out_sockmap;
	}

	/* Test map update elem afterwards fd lives in fd and map_fd */
	for (i = 2; i < 6; i++) {
		err = bpf_map_update_elem(map_fd_rx, &i, &sfd[i], BPF_ANY);
		if (err) {
			printf("Failed map_fd_rx update sockmap %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
		err = bpf_map_update_elem(map_fd_tx, &i, &sfd[i], BPF_ANY);
		if (err) {
			printf("Failed map_fd_tx update sockmap %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
	}

	/* Test map delete elem and remove send/recv sockets */
	for (i = 2; i < 4; i++) {
		err = bpf_map_delete_elem(map_fd_rx, &i);
		if (err) {
			printf("Failed delete sockmap rx %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
		err = bpf_map_delete_elem(map_fd_tx, &i);
		if (err) {
			printf("Failed delete sockmap tx %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
	}

	/* Put sfd[2] (sending fd below) into msg map to test sendmsg bpf */
	i = 0;
	err = bpf_map_update_elem(map_fd_msg, &i, &sfd[2], BPF_ANY);
	if (err) {
		printf("Failed map_fd_msg update sockmap %i\n", err);
		goto out_sockmap;
	}

	/* Test map send/recv */
	for (i = 0; i < 2; i++) {
		buf[0] = i;
		buf[1] = 0x5;
		sc = send(sfd[2], buf, 20, 0);
		if (sc < 0) {
			printf("Failed sockmap send\n");
			goto out_sockmap;
		}

		FD_ZERO(&w);
		FD_SET(sfd[3], &w);
		to.tv_sec = 30;
		to.tv_usec = 0;
		s = select(sfd[3] + 1, &w, NULL, NULL, &to);
		if (s == -1) {
			perror("Failed sockmap select()");
			goto out_sockmap;
		} else if (!s) {
			printf("Failed sockmap unexpected timeout\n");
			goto out_sockmap;
		}

		if (!FD_ISSET(sfd[3], &w)) {
			printf("Failed sockmap select/recv\n");
			goto out_sockmap;
		}

		rc = recv(sfd[3], buf, sizeof(buf), 0);
		if (rc < 0) {
			printf("Failed sockmap recv\n");
			goto out_sockmap;
		}
	}

	/* Negative null entry lookup from datapath should be dropped */
	buf[0] = 1;
	buf[1] = 12;
	sc = send(sfd[2], buf, 20, 0);
	if (sc < 0) {
		printf("Failed sockmap send\n");
		goto out_sockmap;
	}

	/* Push fd into same slot */
	i = 2;
	err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_NOEXIST);
	if (!err) {
		printf("Failed allowed sockmap dup slot BPF_NOEXIST\n");
		goto out_sockmap;
	}

	err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_ANY);
	if (err) {
		printf("Failed sockmap update new slot BPF_ANY\n");
		goto out_sockmap;
	}

	err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_EXIST);
	if (err) {
		printf("Failed sockmap update new slot BPF_EXIST\n");
		goto out_sockmap;
	}

	/* Delete the elems without programs */
	for (i = 2; i < 6; i++) {
		err = bpf_map_delete_elem(fd, &i);
		if (err) {
			printf("Failed delete sockmap %i '%i:%i'\n",
			       err, i, sfd[i]);
		}
	}

	/* Test having multiple maps open and set with programs on same fds */
	err = bpf_prog_attach(parse_prog, fd,
			      BPF_SK_SKB_STREAM_PARSER, 0);
	if (err) {
		printf("Failed fd bpf parse prog attach\n");
		goto out_sockmap;
	}
	err = bpf_prog_attach(verdict_prog, fd,
			      BPF_SK_SKB_STREAM_VERDICT, 0);
	if (err) {
		printf("Failed fd bpf verdict prog attach\n");
		goto out_sockmap;
	}

	for (i = 4; i < 6; i++) {
		err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_ANY);
		if (!err) {
			printf("Failed allowed duplicate programs in update ANY sockmap %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
		err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_NOEXIST);
		if (!err) {
			printf("Failed allowed duplicate program in update NOEXIST sockmap  %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
		err = bpf_map_update_elem(fd, &i, &sfd[i], BPF_EXIST);
		if (!err) {
			printf("Failed allowed duplicate program in update EXIST sockmap  %i '%i:%i'\n",
			       err, i, sfd[i]);
			goto out_sockmap;
		}
	}

	/* Test tasks number of forked operations */
	for (i = 0; i < tasks; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			for (i = 0; i < 6; i++) {
				bpf_map_delete_elem(map_fd_tx, &i);
				bpf_map_delete_elem(map_fd_rx, &i);
				bpf_map_update_elem(map_fd_tx, &i,
						    &sfd[i], BPF_ANY);
				bpf_map_update_elem(map_fd_rx, &i,
						    &sfd[i], BPF_ANY);
			}
			exit(0);
		} else if (pid[i] == -1) {
			printf("Couldn't spawn #%d process!\n", i);
			exit(1);
		}
	}

	for (i = 0; i < tasks; i++) {
		int status;

		assert(waitpid(pid[i], &status, 0) == pid[i]);
		assert(status == 0);
	}

	err = bpf_prog_detach2(parse_prog, map_fd_rx, __MAX_BPF_ATTACH_TYPE);
	if (!err) {
		printf("Detached an invalid prog type.\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach2(parse_prog, map_fd_rx, BPF_SK_SKB_STREAM_PARSER);
	if (err) {
		printf("Failed parser prog detach\n");
		goto out_sockmap;
	}

	err = bpf_prog_detach2(verdict_prog, map_fd_rx, BPF_SK_SKB_STREAM_VERDICT);
	if (err) {
		printf("Failed parser prog detach\n");
		goto out_sockmap;
	}

	/* Test map close sockets and empty maps */
	for (i = 0; i < 6; i++) {
		bpf_map_delete_elem(map_fd_tx, &i);
		bpf_map_delete_elem(map_fd_rx, &i);
		close(sfd[i]);
	}
	close(fd);
	close(map_fd_rx);
	bpf_object__close(parse_obj);
	bpf_object__close(msg_obj);
	bpf_object__close(verdict_obj);
	return;
out:
	for (i = 0; i < 6; i++)
		close(sfd[i]);
	printf("Failed to create sockmap '%i:%s'!\n", i, strerror(errno));
	exit(1);
out_sockmap:
	for (i = 0; i < 6; i++) {
		if (map_fd_tx)
			bpf_map_delete_elem(map_fd_tx, &i);
		if (map_fd_rx)
			bpf_map_delete_elem(map_fd_rx, &i);
		close(sfd[i]);
	}
	close(fd);
	exit(1);
}

#define MAPINMAP_PROG "./test_map_in_map.bpf.o"
#define MAPINMAP_INVALID_PROG "./test_map_in_map_invalid.bpf.o"
static void test_map_in_map(void)
{
	struct bpf_object *obj;
	struct bpf_map *map;
	int mim_fd, fd, err;
	int pos = 0;
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	__u32 id = 0;
	libbpf_print_fn_t old_print_fn;

	obj = bpf_object__open(MAPINMAP_PROG);

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(int), sizeof(int), 2, NULL);
	if (fd < 0) {
		printf("Failed to create hashmap '%s'!\n", strerror(errno));
		exit(1);
	}

	map = bpf_object__find_map_by_name(obj, "mim_array");
	if (!map) {
		printf("Failed to load array of maps from test prog\n");
		goto out_map_in_map;
	}
	err = bpf_map__set_inner_map_fd(map, fd);
	if (err) {
		printf("Failed to set inner_map_fd for array of maps\n");
		goto out_map_in_map;
	}

	map = bpf_object__find_map_by_name(obj, "mim_hash");
	if (!map) {
		printf("Failed to load hash of maps from test prog\n");
		goto out_map_in_map;
	}
	err = bpf_map__set_inner_map_fd(map, fd);
	if (err) {
		printf("Failed to set inner_map_fd for hash of maps\n");
		goto out_map_in_map;
	}

	bpf_object__load(obj);

	map = bpf_object__find_map_by_name(obj, "mim_array");
	if (!map) {
		printf("Failed to load array of maps from test prog\n");
		goto out_map_in_map;
	}
	mim_fd = bpf_map__fd(map);
	if (mim_fd < 0) {
		printf("Failed to get descriptor for array of maps\n");
		goto out_map_in_map;
	}

	err = bpf_map_update_elem(mim_fd, &pos, &fd, 0);
	if (err) {
		printf("Failed to update array of maps\n");
		goto out_map_in_map;
	}

	map = bpf_object__find_map_by_name(obj, "mim_hash");
	if (!map) {
		printf("Failed to load hash of maps from test prog\n");
		goto out_map_in_map;
	}
	mim_fd = bpf_map__fd(map);
	if (mim_fd < 0) {
		printf("Failed to get descriptor for hash of maps\n");
		goto out_map_in_map;
	}

	err = bpf_map_update_elem(mim_fd, &pos, &fd, 0);
	if (err) {
		printf("Failed to update hash of maps\n");
		goto out_map_in_map;
	}

	close(fd);
	fd = -1;
	bpf_object__close(obj);

	/* Test that failing bpf_object__create_map() destroys the inner map */
	obj = bpf_object__open(MAPINMAP_INVALID_PROG);
	err = libbpf_get_error(obj);
	if (err) {
		printf("Failed to load %s program: %d %d",
		       MAPINMAP_INVALID_PROG, err, errno);
		goto out_map_in_map;
	}

	map = bpf_object__find_map_by_name(obj, "mim");
	if (!map) {
		printf("Failed to load array of maps from test prog\n");
		goto out_map_in_map;
	}

	old_print_fn = libbpf_set_print(NULL);

	err = bpf_object__load(obj);
	if (!err) {
		printf("Loading obj supposed to fail\n");
		goto out_map_in_map;
	}

	libbpf_set_print(old_print_fn);

	/* Iterate over all maps to check whether the internal map
	 * ("mim.internal") has been destroyed.
	 */
	while (true) {
		err = bpf_map_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				break;
			printf("Failed to get next map: %d", errno);
			goto out_map_in_map;
		}

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			printf("Failed to get map by id %u: %d", id, errno);
			goto out_map_in_map;
		}

		err = bpf_map_get_info_by_fd(fd, &info, &len);
		if (err) {
			printf("Failed to get map info by fd %d: %d", fd,
			       errno);
			goto out_map_in_map;
		}

		if (!strcmp(info.name, "mim.inner")) {
			printf("Inner map mim.inner was not destroyed\n");
			goto out_map_in_map;
		}

		close(fd);
	}

	bpf_object__close(obj);
	return;

out_map_in_map:
	if (fd >= 0)
		close(fd);
	exit(1);
}

#define MAP_SIZE (32 * 1024)

static void test_map_large(void)
{

	struct bigkey {
		int a;
		char b[4096];
		long long c;
	} key;
	int fd, i, value;

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value),
			    MAP_SIZE, &map_opts);
	if (fd < 0) {
		printf("Failed to create large map '%s'!\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < MAP_SIZE; i++) {
		key = (struct bigkey) { .c = i };
		value = i;

		assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == 0);
	}

	key.c = -1;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       errno == E2BIG);

	/* Iterate through all elements. */
	assert(bpf_map_get_next_key(fd, NULL, &key) == 0);
	key.c = -1;
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_get_next_key(fd, &key, &key) == 0);
	assert(bpf_map_get_next_key(fd, &key, &key) < 0 && errno == ENOENT);

	key.c = 0;
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 0);
	key.a = 1;
	assert(bpf_map_lookup_elem(fd, &key, &value) < 0 && errno == ENOENT);

	close(fd);
}

#define run_parallel(N, FN, DATA) \
	printf("Fork %u tasks to '" #FN "'\n", N); \
	__run_parallel(N, FN, DATA)

static void __run_parallel(unsigned int tasks,
			   void (*fn)(unsigned int task, void *data),
			   void *data)
{
	pid_t pid[tasks];
	int i;

	fflush(stdout);

	for (i = 0; i < tasks; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			fn(i, data);
			exit(0);
		} else if (pid[i] == -1) {
			printf("Couldn't spawn #%d process!\n", i);
			exit(1);
		}
	}

	for (i = 0; i < tasks; i++) {
		int status;

		assert(waitpid(pid[i], &status, 0) == pid[i]);
		assert(status == 0);
	}
}

static void test_map_stress(void)
{
	run_parallel(100, test_hashmap_walk, NULL);
	run_parallel(100, test_hashmap, NULL);
	run_parallel(100, test_hashmap_percpu, NULL);
	run_parallel(100, test_hashmap_sizes, NULL);

	run_parallel(100, test_arraymap, NULL);
	run_parallel(100, test_arraymap_percpu, NULL);
}

#define TASKS 100

#define DO_UPDATE 1
#define DO_DELETE 0

#define MAP_RETRIES 20
#define MAX_DELAY_US 50000
#define MIN_DELAY_RANGE_US 5000

static int map_update_retriable(int map_fd, const void *key, const void *value,
				int flags, int attempts)
{
	int delay = rand() % MIN_DELAY_RANGE_US;

	while (bpf_map_update_elem(map_fd, key, value, flags)) {
		if (!attempts || (errno != EAGAIN && errno != EBUSY))
			return -errno;

		if (delay <= MAX_DELAY_US / 2)
			delay *= 2;

		usleep(delay);
		attempts--;
	}

	return 0;
}

static int map_delete_retriable(int map_fd, const void *key, int attempts)
{
	int delay = rand() % MIN_DELAY_RANGE_US;

	while (bpf_map_delete_elem(map_fd, key)) {
		if (!attempts || (errno != EAGAIN && errno != EBUSY))
			return -errno;

		if (delay <= MAX_DELAY_US / 2)
			delay *= 2;

		usleep(delay);
		attempts--;
	}

	return 0;
}

static void test_update_delete(unsigned int fn, void *data)
{
	int do_update = ((int *)data)[1];
	int fd = ((int *)data)[0];
	int i, key, value, err;

	if (fn & 1)
		test_hashmap_walk(fn, NULL);
	for (i = fn; i < MAP_SIZE; i += TASKS) {
		key = value = i;

		if (do_update) {
			err = map_update_retriable(fd, &key, &value, BPF_NOEXIST, MAP_RETRIES);
			if (err)
				printf("error %d %d\n", err, errno);
			assert(err == 0);
			err = map_update_retriable(fd, &key, &value, BPF_EXIST, MAP_RETRIES);
			if (err)
				printf("error %d %d\n", err, errno);
			assert(err == 0);
		} else {
			err = map_delete_retriable(fd, &key, MAP_RETRIES);
			if (err)
				printf("error %d %d\n", err, errno);
			assert(err == 0);
		}
	}
}

static void test_map_parallel(void)
{
	int i, fd, key = 0, value = 0, j = 0;
	int data[2];

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value),
			    MAP_SIZE, &map_opts);
	if (fd < 0) {
		printf("Failed to create map for parallel test '%s'!\n",
		       strerror(errno));
		exit(1);
	}

again:
	/* Use the same fd in children to add elements to this map:
	 * child_0 adds key=0, key=1024, key=2048, ...
	 * child_1 adds key=1, key=1025, key=2049, ...
	 * child_1023 adds key=1023, ...
	 */
	data[0] = fd;
	data[1] = DO_UPDATE;
	run_parallel(TASKS, test_update_delete, data);

	/* Check that key=0 is already there. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) < 0 &&
	       errno == EEXIST);

	/* Check that all elements were inserted. */
	assert(bpf_map_get_next_key(fd, NULL, &key) == 0);
	key = -1;
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_get_next_key(fd, &key, &key) == 0);
	assert(bpf_map_get_next_key(fd, &key, &key) < 0 && errno == ENOENT);

	/* Another check for all elements */
	for (i = 0; i < MAP_SIZE; i++) {
		key = MAP_SIZE - i - 1;

		assert(bpf_map_lookup_elem(fd, &key, &value) == 0 &&
		       value == key);
	}

	/* Now let's delete all elemenets in parallel. */
	data[1] = DO_DELETE;
	run_parallel(TASKS, test_update_delete, data);

	/* Nothing should be left. */
	key = -1;
	assert(bpf_map_get_next_key(fd, NULL, &key) < 0 && errno == ENOENT);
	assert(bpf_map_get_next_key(fd, &key, &key) < 0 && errno == ENOENT);

	key = 0;
	bpf_map_delete_elem(fd, &key);
	if (j++ < 5)
		goto again;
	close(fd);
}

static void test_map_rdonly(void)
{
	int fd, key = 0, value = 0;
	__u32 old_flags;

	old_flags = map_opts.map_flags;
	map_opts.map_flags |= BPF_F_RDONLY;
	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value),
			    MAP_SIZE, &map_opts);
	map_opts.map_flags = old_flags;
	if (fd < 0) {
		printf("Failed to create map for read only test '%s'!\n",
		       strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* Try to insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) < 0 &&
	       errno == EPERM);

	/* Check that key=1 is not found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) < 0 && errno == ENOENT);
	assert(bpf_map_get_next_key(fd, &key, &value) < 0 && errno == ENOENT);

	close(fd);
}

static void test_map_wronly_hash(void)
{
	int fd, key = 0, value = 0;
	__u32 old_flags;

	old_flags = map_opts.map_flags;
	map_opts.map_flags |= BPF_F_WRONLY;
	fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL, sizeof(key), sizeof(value),
			    MAP_SIZE, &map_opts);
	map_opts.map_flags = old_flags;
	if (fd < 0) {
		printf("Failed to create map for write only test '%s'!\n",
		       strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);

	/* Check that reading elements and keys from the map is not allowed. */
	assert(bpf_map_lookup_elem(fd, &key, &value) < 0 && errno == EPERM);
	assert(bpf_map_get_next_key(fd, &key, &value) < 0 && errno == EPERM);

	close(fd);
}

static void test_map_wronly_stack_or_queue(enum bpf_map_type map_type)
{
	int fd, value = 0;
	__u32 old_flags;


	assert(map_type == BPF_MAP_TYPE_QUEUE ||
	       map_type == BPF_MAP_TYPE_STACK);
	old_flags = map_opts.map_flags;
	map_opts.map_flags |= BPF_F_WRONLY;
	fd = bpf_map_create(map_type, NULL, 0, sizeof(value), MAP_SIZE, &map_opts);
	map_opts.map_flags = old_flags;
	/* Stack/Queue maps do not support BPF_F_NO_PREALLOC */
	if (map_opts.map_flags & BPF_F_NO_PREALLOC) {
		assert(fd < 0 && errno == EINVAL);
		return;
	}
	if (fd < 0) {
		printf("Failed to create map '%s'!\n", strerror(errno));
		exit(1);
	}

	value = 1234;
	assert(bpf_map_update_elem(fd, NULL, &value, BPF_ANY) == 0);

	/* Peek element should fail */
	assert(bpf_map_lookup_elem(fd, NULL, &value) < 0 && errno == EPERM);

	/* Pop element should fail */
	assert(bpf_map_lookup_and_delete_elem(fd, NULL, &value) < 0 &&
	       errno == EPERM);

	close(fd);
}

static void test_map_wronly(void)
{
	test_map_wronly_hash();
	test_map_wronly_stack_or_queue(BPF_MAP_TYPE_STACK);
	test_map_wronly_stack_or_queue(BPF_MAP_TYPE_QUEUE);
}

static void prepare_reuseport_grp(int type, int map_fd, size_t map_elem_size,
				  __s64 *fds64, __u64 *sk_cookies,
				  unsigned int n)
{
	socklen_t optlen, addrlen;
	struct sockaddr_in6 s6;
	const __u32 index0 = 0;
	const int optval = 1;
	unsigned int i;
	u64 sk_cookie;
	void *value;
	__s32 fd32;
	__s64 fd64;
	int err;

	s6.sin6_family = AF_INET6;
	s6.sin6_addr = in6addr_any;
	s6.sin6_port = 0;
	addrlen = sizeof(s6);
	optlen = sizeof(sk_cookie);

	for (i = 0; i < n; i++) {
		fd64 = socket(AF_INET6, type, 0);
		CHECK(fd64 == -1, "socket()",
		      "sock_type:%d fd64:%lld errno:%d\n",
		      type, fd64, errno);

		err = setsockopt(fd64, SOL_SOCKET, SO_REUSEPORT,
				 &optval, sizeof(optval));
		CHECK(err == -1, "setsockopt(SO_REUSEPORT)",
		      "err:%d errno:%d\n", err, errno);

		/* reuseport_array does not allow unbound sk */
		if (map_elem_size == sizeof(__u64))
			value = &fd64;
		else {
			assert(map_elem_size == sizeof(__u32));
			fd32 = (__s32)fd64;
			value = &fd32;
		}
		err = bpf_map_update_elem(map_fd, &index0, value, BPF_ANY);
		CHECK(err >= 0 || errno != EINVAL,
		      "reuseport array update unbound sk",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);

		err = bind(fd64, (struct sockaddr *)&s6, sizeof(s6));
		CHECK(err == -1, "bind()",
		      "sock_type:%d err:%d errno:%d\n", type, err, errno);

		if (i == 0) {
			err = getsockname(fd64, (struct sockaddr *)&s6,
					  &addrlen);
			CHECK(err == -1, "getsockname()",
			      "sock_type:%d err:%d errno:%d\n",
			      type, err, errno);
		}

		err = getsockopt(fd64, SOL_SOCKET, SO_COOKIE, &sk_cookie,
				 &optlen);
		CHECK(err == -1, "getsockopt(SO_COOKIE)",
		      "sock_type:%d err:%d errno:%d\n", type, err, errno);

		if (type == SOCK_STREAM) {
			/*
			 * reuseport_array does not allow
			 * non-listening tcp sk.
			 */
			err = bpf_map_update_elem(map_fd, &index0, value,
						  BPF_ANY);
			CHECK(err >= 0 || errno != EINVAL,
			      "reuseport array update non-listening sk",
			      "sock_type:%d err:%d errno:%d\n",
			      type, err, errno);
			err = listen(fd64, 0);
			CHECK(err == -1, "listen()",
			      "sock_type:%d, err:%d errno:%d\n",
			      type, err, errno);
		}

		fds64[i] = fd64;
		sk_cookies[i] = sk_cookie;
	}
}

static void test_reuseport_array(void)
{
#define REUSEPORT_FD_IDX(err, last) ({ (err) ? last : !last; })

	const __u32 array_size = 4, index0 = 0, index3 = 3;
	int types[2] = { SOCK_STREAM, SOCK_DGRAM }, type;
	__u64 grpa_cookies[2], sk_cookie, map_cookie;
	__s64 grpa_fds64[2] = { -1, -1 }, fd64 = -1;
	const __u32 bad_index = array_size;
	int map_fd, err, t, f;
	__u32 fds_idx = 0;
	int fd;

	map_fd = bpf_map_create(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY, NULL,
				sizeof(__u32), sizeof(__u64), array_size, NULL);
	CHECK(map_fd < 0, "reuseport array create",
	      "map_fd:%d, errno:%d\n", map_fd, errno);

	/* Test lookup/update/delete with invalid index */
	err = bpf_map_delete_elem(map_fd, &bad_index);
	CHECK(err >= 0 || errno != E2BIG, "reuseport array del >=max_entries",
	      "err:%d errno:%d\n", err, errno);

	err = bpf_map_update_elem(map_fd, &bad_index, &fd64, BPF_ANY);
	CHECK(err >= 0 || errno != E2BIG,
	      "reuseport array update >=max_entries",
	      "err:%d errno:%d\n", err, errno);

	err = bpf_map_lookup_elem(map_fd, &bad_index, &map_cookie);
	CHECK(err >= 0 || errno != ENOENT,
	      "reuseport array update >=max_entries",
	      "err:%d errno:%d\n", err, errno);

	/* Test lookup/delete non existence elem */
	err = bpf_map_lookup_elem(map_fd, &index3, &map_cookie);
	CHECK(err >= 0 || errno != ENOENT,
	      "reuseport array lookup not-exist elem",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_delete_elem(map_fd, &index3);
	CHECK(err >= 0 || errno != ENOENT,
	      "reuseport array del not-exist elem",
	      "err:%d errno:%d\n", err, errno);

	for (t = 0; t < ARRAY_SIZE(types); t++) {
		type = types[t];

		prepare_reuseport_grp(type, map_fd, sizeof(__u64), grpa_fds64,
				      grpa_cookies, ARRAY_SIZE(grpa_fds64));

		/* Test BPF_* update flags */
		/* BPF_EXIST failure case */
		err = bpf_map_update_elem(map_fd, &index3, &grpa_fds64[fds_idx],
					  BPF_EXIST);
		CHECK(err >= 0 || errno != ENOENT,
		      "reuseport array update empty elem BPF_EXIST",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);
		fds_idx = REUSEPORT_FD_IDX(err, fds_idx);

		/* BPF_NOEXIST success case */
		err = bpf_map_update_elem(map_fd, &index3, &grpa_fds64[fds_idx],
					  BPF_NOEXIST);
		CHECK(err < 0,
		      "reuseport array update empty elem BPF_NOEXIST",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);
		fds_idx = REUSEPORT_FD_IDX(err, fds_idx);

		/* BPF_EXIST success case. */
		err = bpf_map_update_elem(map_fd, &index3, &grpa_fds64[fds_idx],
					  BPF_EXIST);
		CHECK(err < 0,
		      "reuseport array update same elem BPF_EXIST",
		      "sock_type:%d err:%d errno:%d\n", type, err, errno);
		fds_idx = REUSEPORT_FD_IDX(err, fds_idx);

		/* BPF_NOEXIST failure case */
		err = bpf_map_update_elem(map_fd, &index3, &grpa_fds64[fds_idx],
					  BPF_NOEXIST);
		CHECK(err >= 0 || errno != EEXIST,
		      "reuseport array update non-empty elem BPF_NOEXIST",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);
		fds_idx = REUSEPORT_FD_IDX(err, fds_idx);

		/* BPF_ANY case (always succeed) */
		err = bpf_map_update_elem(map_fd, &index3, &grpa_fds64[fds_idx],
					  BPF_ANY);
		CHECK(err < 0,
		      "reuseport array update same sk with BPF_ANY",
		      "sock_type:%d err:%d errno:%d\n", type, err, errno);

		fd64 = grpa_fds64[fds_idx];
		sk_cookie = grpa_cookies[fds_idx];

		/* The same sk cannot be added to reuseport_array twice */
		err = bpf_map_update_elem(map_fd, &index3, &fd64, BPF_ANY);
		CHECK(err >= 0 || errno != EBUSY,
		      "reuseport array update same sk with same index",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);

		err = bpf_map_update_elem(map_fd, &index0, &fd64, BPF_ANY);
		CHECK(err >= 0 || errno != EBUSY,
		      "reuseport array update same sk with different index",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);

		/* Test delete elem */
		err = bpf_map_delete_elem(map_fd, &index3);
		CHECK(err < 0, "reuseport array delete sk",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);

		/* Add it back with BPF_NOEXIST */
		err = bpf_map_update_elem(map_fd, &index3, &fd64, BPF_NOEXIST);
		CHECK(err < 0,
		      "reuseport array re-add with BPF_NOEXIST after del",
		      "sock_type:%d err:%d errno:%d\n", type, err, errno);

		/* Test cookie */
		err = bpf_map_lookup_elem(map_fd, &index3, &map_cookie);
		CHECK(err < 0 || sk_cookie != map_cookie,
		      "reuseport array lookup re-added sk",
		      "sock_type:%d err:%d errno:%d sk_cookie:0x%llx map_cookie:0x%llxn",
		      type, err, errno, sk_cookie, map_cookie);

		/* Test elem removed by close() */
		for (f = 0; f < ARRAY_SIZE(grpa_fds64); f++)
			close(grpa_fds64[f]);
		err = bpf_map_lookup_elem(map_fd, &index3, &map_cookie);
		CHECK(err >= 0 || errno != ENOENT,
		      "reuseport array lookup after close()",
		      "sock_type:%d err:%d errno:%d\n",
		      type, err, errno);
	}

	/* Test SOCK_RAW */
	fd64 = socket(AF_INET6, SOCK_RAW, IPPROTO_UDP);
	CHECK(fd64 == -1, "socket(SOCK_RAW)", "err:%d errno:%d\n",
	      err, errno);
	err = bpf_map_update_elem(map_fd, &index3, &fd64, BPF_NOEXIST);
	CHECK(err >= 0 || errno != ENOTSUPP, "reuseport array update SOCK_RAW",
	      "err:%d errno:%d\n", err, errno);
	close(fd64);

	/* Close the 64 bit value map */
	close(map_fd);

	/* Test 32 bit fd */
	map_fd = bpf_map_create(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY, NULL,
				sizeof(__u32), sizeof(__u32), array_size, NULL);
	CHECK(map_fd < 0, "reuseport array create",
	      "map_fd:%d, errno:%d\n", map_fd, errno);
	prepare_reuseport_grp(SOCK_STREAM, map_fd, sizeof(__u32), &fd64,
			      &sk_cookie, 1);
	fd = fd64;
	err = bpf_map_update_elem(map_fd, &index3, &fd, BPF_NOEXIST);
	CHECK(err < 0, "reuseport array update 32 bit fd",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_lookup_elem(map_fd, &index3, &map_cookie);
	CHECK(err >= 0 || errno != ENOSPC,
	      "reuseport array lookup 32 bit fd",
	      "err:%d errno:%d\n", err, errno);
	close(fd);
	close(map_fd);
}

static void run_all_tests(void)
{
	test_hashmap(0, NULL);
	test_hashmap_percpu(0, NULL);
	test_hashmap_walk(0, NULL);
	test_hashmap_zero_seed();

	test_arraymap(0, NULL);
	test_arraymap_percpu(0, NULL);

	test_arraymap_percpu_many_keys();

	test_devmap(0, NULL);
	test_devmap_hash(0, NULL);
	test_sockmap(0, NULL);

	test_map_large();
	test_map_parallel();
	test_map_stress();

	test_map_rdonly();
	test_map_wronly();

	test_reuseport_array();

	test_queuemap(0, NULL);
	test_stackmap(0, NULL);

	test_map_in_map();
}

#define DEFINE_TEST(name) extern void test_##name(void);
#include <map_tests/tests.h>
#undef DEFINE_TEST

int main(void)
{
	srand(time(NULL));

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	map_opts.map_flags = 0;
	run_all_tests();

	map_opts.map_flags = BPF_F_NO_PREALLOC;
	run_all_tests();

#define DEFINE_TEST(name) test_##name();
#include <map_tests/tests.h>
#undef DEFINE_TEST

	printf("test_maps: OK, %d SKIPPED\n", skips);
	return 0;
}
