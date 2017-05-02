/*
 * Testsuite for eBPF maps
 *
 * Copyright (c) 2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <sys/resource.h>

#include <linux/bpf.h>

#include <bpf/bpf.h>
#include "bpf_util.h"

static int map_flags;

static void test_hashmap(int task, void *data)
{
	long long key, next_key, value;
	int fd;

	fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value),
			    2, map_flags);
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
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
	       /* key=1 already exists. */
	       errno == EEXIST);

	/* -1 is an invalid flag. */
	assert(bpf_map_update_elem(fd, &key, &value, -1) == -1 &&
	       errno == EINVAL);

	/* Check that key=1 can be found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 1234);

	key = 2;
	/* Check that key=2 is not found. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == -1 && errno == ENOENT);

	/* BPF_EXIST means update existing element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) == -1 &&
	       /* key=2 is not there. */
	       errno == ENOENT);

	/* Insert key=2 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == 0);

	/* key=1 and key=2 were inserted, check that key=0 cannot be
	 * inserted due to max_entries limit.
	 */
	key = 0;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* Update existing element, though the map is full. */
	key = 1;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) == 0);
	key = 2;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);
	key = 3;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* Check that key = 0 doesn't exist. */
	key = 0;
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       (next_key == 1 || next_key == 2));
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       (next_key == 1 || next_key == 2));
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == -1 &&
	       errno == ENOENT);

	/* Delete both elements. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	key = 2;
	assert(bpf_map_delete_elem(fd, &key) == 0);
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == ENOENT);

	key = 0;
	/* Check that map is empty. */
	assert(bpf_map_get_next_key(fd, &key, &next_key) == -1 &&
	       errno == ENOENT);

	close(fd);
}

static void test_hashmap_sizes(int task, void *data)
{
	int fd, i, j;

	for (i = 1; i <= 512; i <<= 1)
		for (j = 1; j <= 1 << 18; j <<= 1) {
			fd = bpf_create_map(BPF_MAP_TYPE_HASH, i, j,
					    2, map_flags);
			if (fd < 0) {
				printf("Failed to create hashmap key=%d value=%d '%s'\n",
				       i, j, strerror(errno));
				exit(1);
			}
			close(fd);
			usleep(10); /* give kernel time to destroy */
		}
}

static void test_hashmap_percpu(int task, void *data)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	long long value[nr_cpus];
	long long key, next_key;
	int expected_key_mask = 0;
	int fd, i;

	fd = bpf_create_map(BPF_MAP_TYPE_PERCPU_HASH, sizeof(key),
			    sizeof(value[0]), 2, map_flags);
	if (fd < 0) {
		printf("Failed to create hashmap '%s'!\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		value[i] = i + 100;

	key = 1;
	/* Insert key=1 element. */
	assert(!(expected_key_mask & key));
	assert(bpf_map_update_elem(fd, &key, value, BPF_ANY) == 0);
	expected_key_mask |= key;

	/* BPF_NOEXIST means add new element if it doesn't exist. */
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) == -1 &&
	       /* key=1 already exists. */
	       errno == EEXIST);

	/* -1 is an invalid flag. */
	assert(bpf_map_update_elem(fd, &key, value, -1) == -1 &&
	       errno == EINVAL);

	/* Check that key=1 can be found. Value could be 0 if the lookup
	 * was run from a different CPU.
	 */
	value[0] = 1;
	assert(bpf_map_lookup_elem(fd, &key, value) == 0 && value[0] == 100);

	key = 2;
	/* Check that key=2 is not found. */
	assert(bpf_map_lookup_elem(fd, &key, value) == -1 && errno == ENOENT);

	/* BPF_EXIST means update existing element. */
	assert(bpf_map_update_elem(fd, &key, value, BPF_EXIST) == -1 &&
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
	assert(bpf_map_update_elem(fd, &key, value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* Check that key = 0 doesn't exist. */
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == ENOENT);

	/* Iterate over two elements. */
	while (!bpf_map_get_next_key(fd, &key, &next_key)) {
		assert((expected_key_mask & next_key) == next_key);
		expected_key_mask &= ~next_key;

		assert(bpf_map_lookup_elem(fd, &next_key, value) == 0);

		for (i = 0; i < nr_cpus; i++)
			assert(value[i] == i + 100);

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
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == ENOENT);

	key = 0;
	/* Check that map is empty. */
	assert(bpf_map_get_next_key(fd, &key, &next_key) == -1 &&
	       errno == ENOENT);

	close(fd);
}

static void test_arraymap(int task, void *data)
{
	int key, next_key, fd;
	long long value;

	fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(key), sizeof(value),
			    2, 0);
	if (fd < 0) {
		printf("Failed to create arraymap '%s'!\n", strerror(errno));
		exit(1);
	}

	key = 1;
	value = 1234;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_ANY) == 0);

	value = 0;
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
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
	assert(bpf_map_update_elem(fd, &key, &value, BPF_EXIST) == -1 &&
	       errno == E2BIG);

	/* Check that key = 2 doesn't exist. */
	assert(bpf_map_lookup_elem(fd, &key, &value) == -1 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       next_key == 1);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == -1 &&
	       errno == ENOENT);

	/* Delete shouldn't succeed. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == EINVAL);

	close(fd);
}

static void test_arraymap_percpu(int task, void *data)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	int key, next_key, fd, i;
	long long values[nr_cpus];

	fd = bpf_create_map(BPF_MAP_TYPE_PERCPU_ARRAY, sizeof(key),
			    sizeof(values[0]), 2, 0);
	if (fd < 0) {
		printf("Failed to create arraymap '%s'!\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		values[i] = i + 100;

	key = 1;
	/* Insert key=1 element. */
	assert(bpf_map_update_elem(fd, &key, values, BPF_ANY) == 0);

	values[0] = 0;
	assert(bpf_map_update_elem(fd, &key, values, BPF_NOEXIST) == -1 &&
	       errno == EEXIST);

	/* Check that key=1 can be found. */
	assert(bpf_map_lookup_elem(fd, &key, values) == 0 && values[0] == 100);

	key = 0;
	/* Check that key=0 is also found and zero initialized. */
	assert(bpf_map_lookup_elem(fd, &key, values) == 0 &&
	       values[0] == 0 && values[nr_cpus - 1] == 0);

	/* Check that key=2 cannot be inserted due to max_entries limit. */
	key = 2;
	assert(bpf_map_update_elem(fd, &key, values, BPF_EXIST) == -1 &&
	       errno == E2BIG);

	/* Check that key = 2 doesn't exist. */
	assert(bpf_map_lookup_elem(fd, &key, values) == -1 && errno == ENOENT);

	/* Iterate over two elements. */
	assert(bpf_map_get_next_key(fd, &key, &next_key) == 0 &&
	       next_key == 0);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == 0 &&
	       next_key == 1);
	assert(bpf_map_get_next_key(fd, &next_key, &next_key) == -1 &&
	       errno == ENOENT);

	/* Delete shouldn't succeed. */
	key = 1;
	assert(bpf_map_delete_elem(fd, &key) == -1 && errno == EINVAL);

	close(fd);
}

static void test_arraymap_percpu_many_keys(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	/* nr_keys is not too large otherwise the test stresses percpu
	 * allocator more than anything else
	 */
	unsigned int nr_keys = 2000;
	long long values[nr_cpus];
	int key, fd, i;

	fd = bpf_create_map(BPF_MAP_TYPE_PERCPU_ARRAY, sizeof(key),
			    sizeof(values[0]), nr_keys, 0);
	if (fd < 0) {
		printf("Failed to create per-cpu arraymap '%s'!\n",
		       strerror(errno));
		exit(1);
	}

	for (i = 0; i < nr_cpus; i++)
		values[i] = i + 10;

	for (key = 0; key < nr_keys; key++)
		assert(bpf_map_update_elem(fd, &key, values, BPF_ANY) == 0);

	for (key = 0; key < nr_keys; key++) {
		for (i = 0; i < nr_cpus; i++)
			values[i] = 0;

		assert(bpf_map_lookup_elem(fd, &key, values) == 0);

		for (i = 0; i < nr_cpus; i++)
			assert(values[i] == i + 10);
	}

	close(fd);
}

#define MAP_SIZE (32 * 1024)

static void test_map_large(void)
{
	struct bigkey {
		int a;
		char b[116];
		long long c;
	} key;
	int fd, i, value;

	fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value),
			    MAP_SIZE, map_flags);
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
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == E2BIG);

	/* Iterate through all elements. */
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_get_next_key(fd, &key, &key) == 0);
	assert(bpf_map_get_next_key(fd, &key, &key) == -1 && errno == ENOENT);

	key.c = 0;
	assert(bpf_map_lookup_elem(fd, &key, &value) == 0 && value == 0);
	key.a = 1;
	assert(bpf_map_lookup_elem(fd, &key, &value) == -1 && errno == ENOENT);

	close(fd);
}

static void run_parallel(int tasks, void (*fn)(int task, void *data),
			 void *data)
{
	pid_t pid[tasks];
	int i;

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
	run_parallel(100, test_hashmap, NULL);
	run_parallel(100, test_hashmap_percpu, NULL);
	run_parallel(100, test_hashmap_sizes, NULL);

	run_parallel(100, test_arraymap, NULL);
	run_parallel(100, test_arraymap_percpu, NULL);
}

#define TASKS 1024

#define DO_UPDATE 1
#define DO_DELETE 0

static void do_work(int fn, void *data)
{
	int do_update = ((int *)data)[1];
	int fd = ((int *)data)[0];
	int i, key, value;

	for (i = fn; i < MAP_SIZE; i += TASKS) {
		key = value = i;

		if (do_update) {
			assert(bpf_map_update_elem(fd, &key, &value,
						   BPF_NOEXIST) == 0);
			assert(bpf_map_update_elem(fd, &key, &value,
						   BPF_EXIST) == 0);
		} else {
			assert(bpf_map_delete_elem(fd, &key) == 0);
		}
	}
}

static void test_map_parallel(void)
{
	int i, fd, key = 0, value = 0;
	int data[2];

	fd = bpf_create_map(BPF_MAP_TYPE_HASH, sizeof(key), sizeof(value),
			    MAP_SIZE, map_flags);
	if (fd < 0) {
		printf("Failed to create map for parallel test '%s'!\n",
		       strerror(errno));
		exit(1);
	}

	/* Use the same fd in children to add elements to this map:
	 * child_0 adds key=0, key=1024, key=2048, ...
	 * child_1 adds key=1, key=1025, key=2049, ...
	 * child_1023 adds key=1023, ...
	 */
	data[0] = fd;
	data[1] = DO_UPDATE;
	run_parallel(TASKS, do_work, data);

	/* Check that key=0 is already there. */
	assert(bpf_map_update_elem(fd, &key, &value, BPF_NOEXIST) == -1 &&
	       errno == EEXIST);

	/* Check that all elements were inserted. */
	key = -1;
	for (i = 0; i < MAP_SIZE; i++)
		assert(bpf_map_get_next_key(fd, &key, &key) == 0);
	assert(bpf_map_get_next_key(fd, &key, &key) == -1 && errno == ENOENT);

	/* Another check for all elements */
	for (i = 0; i < MAP_SIZE; i++) {
		key = MAP_SIZE - i - 1;

		assert(bpf_map_lookup_elem(fd, &key, &value) == 0 &&
		       value == key);
	}

	/* Now let's delete all elemenets in parallel. */
	data[1] = DO_DELETE;
	run_parallel(TASKS, do_work, data);

	/* Nothing should be left. */
	key = -1;
	assert(bpf_map_get_next_key(fd, &key, &key) == -1 && errno == ENOENT);
}

static void run_all_tests(void)
{
	test_hashmap(0, NULL);
	test_hashmap_percpu(0, NULL);

	test_arraymap(0, NULL);
	test_arraymap_percpu(0, NULL);

	test_arraymap_percpu_many_keys();

	test_map_large();
	test_map_parallel();
	test_map_stress();
}

int main(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);

	map_flags = 0;
	run_all_tests();

	map_flags = BPF_F_NO_PREALLOC;
	run_all_tests();

	printf("test_maps: OK\n");
	return 0;
}
