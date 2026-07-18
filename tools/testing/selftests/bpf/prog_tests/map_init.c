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

static void test_map_create(enum bpf_map_type map_type, const char *map_name,
			    struct bpf_map_create_opts *opts, const char *exp_msg)
{
	const int key_size = 4, value_size = 4, max_entries = 1;
	char log_buf[128];
	int fd;
	LIBBPF_OPTS(bpf_log_opts, log_opts);

	log_buf[0] = '\0';
	log_opts.buf = log_buf;
	log_opts.size = sizeof(log_buf);
	log_opts.level = 1;
	opts->log_opts = &log_opts;
	fd = bpf_map_create(map_type, map_name, key_size, value_size, max_entries, opts);
	if (!ASSERT_LT(fd, 0, "bpf_map_create")) {
		close(fd);
		return;
	}

	ASSERT_STREQ(log_buf, exp_msg, "log_buf");
	ASSERT_EQ(log_opts.true_size, strlen(exp_msg) + 1, "true_size");
}

static void test_map_create_array(struct bpf_map_create_opts *opts, const char *exp_msg)
{
	test_map_create(BPF_MAP_TYPE_ARRAY, "test_map_create", opts, exp_msg);
}

static void test_invalid_vmlinux_value_type_id_struct_ops(void)
{
	const char *msg = "btf_vmlinux_value_type_id can only be used with struct_ops maps.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .btf_vmlinux_value_type_id = 1,
	);

	test_map_create_array(&opts, msg);
}

static void test_invalid_vmlinux_value_type_id_kv_type_id(void)
{
	const char *msg = "btf_vmlinux_value_type_id is mutually exclusive with btf_key_type_id and btf_value_type_id.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .btf_vmlinux_value_type_id = 1,
		    .btf_key_type_id = 1,
	);

	test_map_create(BPF_MAP_TYPE_STRUCT_OPS, "test_map_create", &opts, msg);
}

static void test_invalid_value_type_id(void)
{
	const char *msg = "Invalid btf_value_type_id.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .btf_key_type_id = 1,
	);

	test_map_create_array(&opts, msg);
}

static void test_invalid_map_extra(void)
{
	const char *msg = "Invalid map_extra.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_extra = 1,
	);

	test_map_create_array(&opts, msg);
}

static void test_invalid_numa_node(void)
{
	const char *msg = "Invalid numa_node.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_flags = BPF_F_NUMA_NODE,
		    .numa_node = 0xFF,
	);

	test_map_create_array(&opts, msg);
}

static void test_invalid_map_type(void)
{
	const char *msg = "Invalid map_type.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts);

	test_map_create(__MAX_BPF_MAP_TYPE, "test_map_create", &opts, msg);
}

static void test_invalid_token_fd(void)
{
	const char *msg = "Invalid map_token_fd.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_flags = BPF_F_TOKEN_FD,
		    .token_fd = -1,
	);

	test_map_create_array(&opts, msg);
}

static void test_invalid_map_name(void)
{
	const char *msg = "Invalid map_name.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts);

	test_map_create(BPF_MAP_TYPE_ARRAY, "test-!@#", &opts, msg);
}

static void test_invalid_btf_fd(void)
{
	const char *msg = "Invalid btf_fd.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .btf_fd = -1,
		    .btf_key_type_id = 1,
		    .btf_value_type_id = 1,
	);

	test_map_create_array(&opts, msg);
}

static void test_excl_prog_hash_size_1(void)
{
	const char *msg = "Invalid excl_prog_hash_size.\n";
	const char *hash = "DEADCODE";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .excl_prog_hash = hash,
	);

	test_map_create_array(&opts, msg);
}

static void test_excl_prog_hash_size_2(void)
{
	const char *msg = "Invalid excl_prog_hash_size.\n";
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .excl_prog_hash_size = 1,
	);

	test_map_create_array(&opts, msg);
}

static void test_common_attr_padding(void)
{
	struct bpf_common_attr_fake {
		__u8 attrs[offsetofend(struct bpf_common_attr, log_true_size)];
		__u32 pad;
	} attr_common = {
		.pad = 1,
	};
	union bpf_attr attr = {
		.map_type    = BPF_MAP_TYPE_ARRAY,
		.key_size    = 4,
		.value_size  = 4,
		.max_entries = 1,
	};
	int fd;

	fd = syscall(__NR_bpf, BPF_MAP_CREATE | BPF_COMMON_ATTRS, &attr, sizeof(attr), &attr_common,
		     sizeof(attr_common));
	if (!ASSERT_LT(fd, 0, "syscall"))
		close(fd);
	else
		ASSERT_EQ(errno, E2BIG, "errno");
}

void test_map_create_failure(void)
{
	if (test__start_subtest("invalid_vmlinux_value_type_id_struct_ops"))
		test_invalid_vmlinux_value_type_id_struct_ops();
	if (test__start_subtest("invalid_vmlinux_value_type_id_kv_type_id"))
		test_invalid_vmlinux_value_type_id_kv_type_id();
	if (test__start_subtest("invalid_value_type_id"))
		test_invalid_value_type_id();
	if (test__start_subtest("invalid_map_extra"))
		test_invalid_map_extra();
	if (test__start_subtest("invalid_numa_node"))
		test_invalid_numa_node();
	if (test__start_subtest("invalid_map_type"))
		test_invalid_map_type();
	if (test__start_subtest("invalid_token_fd"))
		test_invalid_token_fd();
	if (test__start_subtest("invalid_map_name"))
		test_invalid_map_name();
	if (test__start_subtest("invalid_btf_fd"))
		test_invalid_btf_fd();
	if (test__start_subtest("invalid_excl_prog_hash_size_1"))
		test_excl_prog_hash_size_1();
	if (test__start_subtest("invalid_excl_prog_hash_size_2"))
		test_excl_prog_hash_size_2();
	if (test__start_subtest("common_attr_padding"))
		test_common_attr_padding();
}
