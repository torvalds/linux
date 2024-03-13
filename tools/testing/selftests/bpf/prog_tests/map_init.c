// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Tessares SA <http://www.tessares.net> */

#include <test_progs.h>
#include "test_map_init.skel.h"

#define TEST_VALUE 0x1234
#define FILL_VALUE 0xdeadbeef

static int nr_cpus;
static int duration;

typedef unsigned long long map_key_t;
typedef unsigned long long map_value_t;
typedef struct {
	map_value_t v; /* padding */
} __bpf_percpu_val_align pcpu_map_value_t;


static int map_populate(int map_fd, int num)
{
	pcpu_map_value_t value[nr_cpus];
	int i, err;
	map_key_t key;

	for (i = 0; i < nr_cpus; i++)
		bpf_percpu(value, i) = FILL_VALUE;

	for (key = 1; key <= num; key++) {
		err = bpf_map_update_elem(map_fd, &key, value, BPF_NOEXIST);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			return -1;
	}

	return 0;
}

static struct test_map_init *setup(enum bpf_map_type map_type, int map_sz,
			    int *map_fd, int populate)
{
	struct test_map_init *skel;
	int err;

	skel = test_map_init__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return NULL;

	err = bpf_map__set_type(skel->maps.hashmap1, map_type);
	if (!ASSERT_OK(err, "bpf_map__set_type"))
		goto error;

	err = bpf_map__set_max_entries(skel->maps.hashmap1, map_sz);
	if (!ASSERT_OK(err, "bpf_map__set_max_entries"))
		goto error;

	err = test_map_init__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto error;

	*map_fd = bpf_map__fd(skel->maps.hashmap1);
	if (CHECK(*map_fd < 0, "bpf_map__fd", "failed\n"))
		goto error;

	err = map_populate(*map_fd, populate);
	if (!ASSERT_OK(err, "map_populate"))
		goto error_map;

	return skel;

error_map:
	close(*map_fd);
error:
	test_map_init__destroy(skel);
	return NULL;
}

/* executes bpf program that updates map with key, value */
static int prog_run_insert_elem(struct test_map_init *skel, map_key_t key,
				map_value_t value)
{
	struct test_map_init__bss *bss;

	bss = skel->bss;

	bss->inKey = key;
	bss->inValue = value;
	bss->inPid = getpid();

	if (!ASSERT_OK(test_map_init__attach(skel), "skel_attach"))
		return -1;

	/* Let tracepoint trigger */
	syscall(__NR_getpgid);

	test_map_init__detach(skel);

	return 0;
}

static int check_values_one_cpu(pcpu_map_value_t *value, map_value_t expected)
{
	int i, nzCnt = 0;
	map_value_t val;

	for (i = 0; i < nr_cpus; i++) {
		val = bpf_percpu(value, i);
		if (val) {
			if (CHECK(val != expected, "map value",
				  "unexpected for cpu %d: 0x%llx\n", i, val))
				return -1;
			nzCnt++;
		}
	}

	if (CHECK(nzCnt != 1, "map value", "set for %d CPUs instead of 1!\n",
		  nzCnt))
		return -1;

	return 0;
}

/* Add key=1 elem with values set for all CPUs
 * Delete elem key=1
 * Run bpf prog that inserts new key=1 elem with value=0x1234
 *   (bpf prog can only set value for current CPU)
 * Lookup Key=1 and check value is as expected for all CPUs:
 *   value set by bpf prog for one CPU, 0 for all others
 */
static void test_pcpu_map_init(void)
{
	pcpu_map_value_t value[nr_cpus];
	struct test_map_init *skel;
	int map_fd, err;
	map_key_t key;

	/* max 1 elem in map so insertion is forced to reuse freed entry */
	skel = setup(BPF_MAP_TYPE_PERCPU_HASH, 1, &map_fd, 1);
	if (!ASSERT_OK_PTR(skel, "prog_setup"))
		return;

	/* delete element so the entry can be re-used*/
	key = 1;
	err = bpf_map_delete_elem(map_fd, &key);
	if (!ASSERT_OK(err, "bpf_map_delete_elem"))
		goto cleanup;

	/* run bpf prog that inserts new elem, re-using the slot just freed */
	err = prog_run_insert_elem(skel, key, TEST_VALUE);
	if (!ASSERT_OK(err, "prog_run_insert_elem"))
		goto cleanup;

	/* check that key=1 was re-created by bpf prog */
	err = bpf_map_lookup_elem(map_fd, &key, value);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		goto cleanup;

	/* and has expected values */
	check_values_one_cpu(value, TEST_VALUE);

cleanup:
	test_map_init__destroy(skel);
}

/* Add key=1 and key=2 elems with values set for all CPUs
 * Run bpf prog that inserts new key=3 elem
 *   (only for current cpu; other cpus should have initial value = 0)
 * Lookup Key=1 and check value is as expected for all CPUs
 */
static void test_pcpu_lru_map_init(void)
{
	pcpu_map_value_t value[nr_cpus];
	struct test_map_init *skel;
	int map_fd, err;
	map_key_t key;

	/* Set up LRU map with 2 elements, values filled for all CPUs.
	 * With these 2 elements, the LRU map is full
	 */
	skel = setup(BPF_MAP_TYPE_LRU_PERCPU_HASH, 2, &map_fd, 2);
	if (!ASSERT_OK_PTR(skel, "prog_setup"))
		return;

	/* run bpf prog that inserts new key=3 element, re-using LRU slot */
	key = 3;
	err = prog_run_insert_elem(skel, key, TEST_VALUE);
	if (!ASSERT_OK(err, "prog_run_insert_elem"))
		goto cleanup;

	/* check that key=3 replaced one of earlier elements */
	err = bpf_map_lookup_elem(map_fd, &key, value);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		goto cleanup;

	/* and has expected values */
	check_values_one_cpu(value, TEST_VALUE);

cleanup:
	test_map_init__destroy(skel);
}

void test_map_init(void)
{
	nr_cpus = bpf_num_possible_cpus();
	if (nr_cpus <= 1) {
		printf("%s:SKIP: >1 cpu needed for this test\n", __func__);
		test__skip();
		return;
	}

	if (test__start_subtest("pcpu_map_init"))
		test_pcpu_map_init();
	if (test__start_subtest("pcpu_lru_map_init"))
		test_pcpu_lru_map_init();
}
