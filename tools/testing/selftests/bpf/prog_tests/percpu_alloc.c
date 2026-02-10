// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "percpu_alloc_array.skel.h"
#include "percpu_alloc_cgrp_local_storage.skel.h"
#include "percpu_alloc_fail.skel.h"

static void test_array(void)
{
	struct percpu_alloc_array *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = percpu_alloc_array__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_array__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_array_map_1, true);
	bpf_program__set_autoload(skel->progs.test_array_map_2, true);
	bpf_program__set_autoload(skel->progs.test_array_map_3, true);
	bpf_program__set_autoload(skel->progs.test_array_map_4, true);

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_array__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__load"))
		goto out;

	err = percpu_alloc_array__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__attach"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.test_array_map_1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run array_map 1-4");
	ASSERT_EQ(topts.retval, 0, "test_run array_map 1-4");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");
out:
	percpu_alloc_array__destroy(skel);
}

static void test_array_sleepable(void)
{
	struct percpu_alloc_array *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = percpu_alloc_array__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_array_map_10, true);

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_array__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__load"))
		goto out;

	err = percpu_alloc_array__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__attach"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.test_array_map_10);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run array_map_10");
	ASSERT_EQ(topts.retval, 0, "test_run array_map_10");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");
out:
	percpu_alloc_array__destroy(skel);
}

static void test_cgrp_local_storage(void)
{
	struct percpu_alloc_cgrp_local_storage *skel;
	int err, cgroup_fd, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	cgroup_fd = test__join_cgroup("/percpu_alloc");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup /percpu_alloc"))
		return;

	skel = percpu_alloc_cgrp_local_storage__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_cgrp_local_storage__open"))
		goto close_fd;

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_cgrp_local_storage__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_cgrp_local_storage__load"))
		goto destroy_skel;

	err = percpu_alloc_cgrp_local_storage__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_cgrp_local_storage__attach"))
		goto destroy_skel;

	prog_fd = bpf_program__fd(skel->progs.test_cgrp_local_storage_1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run cgrp_local_storage 1-3");
	ASSERT_EQ(topts.retval, 0, "test_run cgrp_local_storage 1-3");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");

destroy_skel:
	percpu_alloc_cgrp_local_storage__destroy(skel);
close_fd:
	close(cgroup_fd);
}

static void test_failure(void) {
	RUN_TESTS(percpu_alloc_fail);
}

static void test_percpu_map_op_cpu_flag(struct bpf_map *map, void *keys, size_t key_sz, u32 entries,
					int nr_cpus, bool test_batch)
{
	size_t value_sz = sizeof(u32), value_sz_cpus, value_sz_total;
	u32 *values = NULL, *values_percpu = NULL;
	const u32 value = 0xDEADC0DE;
	int i, j, cpu, map_fd, err;
	u64 batch = 0, flags;
	void *values_row;
	u32 count, v;
	LIBBPF_OPTS(bpf_map_batch_opts, batch_opts);

	value_sz_cpus = value_sz * nr_cpus;
	values = calloc(entries, value_sz_cpus);
	if (!ASSERT_OK_PTR(values, "calloc values"))
		return;

	values_percpu = calloc(entries, roundup(value_sz, 8) * nr_cpus);
	if (!ASSERT_OK_PTR(values_percpu, "calloc values_percpu")) {
		free(values);
		return;
	}

	value_sz_total = value_sz_cpus * entries;
	memset(values, 0, value_sz_total);

	map_fd = bpf_map__fd(map);
	flags = BPF_F_CPU | BPF_F_ALL_CPUS;
	err = bpf_map_lookup_elem_flags(map_fd, keys, values, flags);
	if (!ASSERT_ERR(err, "bpf_map_lookup_elem_flags cpu|all_cpus"))
		goto out;

	err = bpf_map_update_elem(map_fd, keys, values, flags);
	if (!ASSERT_ERR(err, "bpf_map_update_elem cpu|all_cpus"))
		goto out;

	flags = BPF_F_ALL_CPUS;
	err = bpf_map_lookup_elem_flags(map_fd, keys, values, flags);
	if (!ASSERT_ERR(err, "bpf_map_lookup_elem_flags all_cpus"))
		goto out;

	flags = BPF_F_LOCK | BPF_F_CPU;
	err = bpf_map_lookup_elem_flags(map_fd, keys, values, flags);
	if (!ASSERT_ERR(err, "bpf_map_lookup_elem_flags BPF_F_LOCK"))
		goto out;

	flags = BPF_F_LOCK | BPF_F_ALL_CPUS;
	err = bpf_map_update_elem(map_fd, keys, values, flags);
	if (!ASSERT_ERR(err, "bpf_map_update_elem BPF_F_LOCK"))
		goto out;

	flags = (u64)nr_cpus << 32 | BPF_F_CPU;
	err = bpf_map_update_elem(map_fd, keys, values, flags);
	if (!ASSERT_EQ(err, -ERANGE, "bpf_map_update_elem -ERANGE"))
		goto out;

	err = bpf_map__update_elem(map, keys, key_sz, values, value_sz, flags);
	if (!ASSERT_EQ(err, -ERANGE, "bpf_map__update_elem -ERANGE"))
		goto out;

	err = bpf_map_lookup_elem_flags(map_fd, keys, values, flags);
	if (!ASSERT_EQ(err, -ERANGE, "bpf_map_lookup_elem_flags -ERANGE"))
		goto out;

	err = bpf_map__lookup_elem(map, keys, key_sz, values, value_sz, flags);
	if (!ASSERT_EQ(err, -ERANGE, "bpf_map__lookup_elem -ERANGE"))
		goto out;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		/* clear value on all cpus */
		values[0] = 0;
		flags = BPF_F_ALL_CPUS;
		for (i = 0; i < entries; i++) {
			err = bpf_map__update_elem(map, keys + i * key_sz, key_sz, values,
						   value_sz, flags);
			if (!ASSERT_OK(err, "bpf_map__update_elem all_cpus"))
				goto out;
		}

		/* update value on specified cpu */
		for (i = 0; i < entries; i++) {
			values[0] = value;
			flags = (u64)cpu << 32 | BPF_F_CPU;
			err = bpf_map__update_elem(map, keys + i * key_sz, key_sz, values,
						   value_sz, flags);
			if (!ASSERT_OK(err, "bpf_map__update_elem specified cpu"))
				goto out;

			/* lookup then check value on CPUs */
			for (j = 0; j < nr_cpus; j++) {
				flags = (u64)j << 32 | BPF_F_CPU;
				err = bpf_map__lookup_elem(map, keys + i * key_sz, key_sz, values,
							   value_sz, flags);
				if (!ASSERT_OK(err, "bpf_map__lookup_elem specified cpu"))
					goto out;
				if (!ASSERT_EQ(values[0], j != cpu ? 0 : value,
					       "bpf_map__lookup_elem value on specified cpu"))
					goto out;
			}
		}
	}

	if (!test_batch)
		goto out;

	count = entries;
	batch_opts.elem_flags = (u64)nr_cpus << 32 | BPF_F_CPU;
	err = bpf_map_update_batch(map_fd, keys, values, &count, &batch_opts);
	if (!ASSERT_EQ(err, -ERANGE, "bpf_map_update_batch -ERANGE"))
		goto out;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		memset(values, 0, value_sz_total);

		/* clear values across all CPUs */
		count = entries;
		batch_opts.elem_flags = BPF_F_ALL_CPUS;
		err = bpf_map_update_batch(map_fd, keys, values, &count, &batch_opts);
		if (!ASSERT_OK(err, "bpf_map_update_batch all_cpus"))
			goto out;
		if (!ASSERT_EQ(count, entries, "bpf_map_update_batch count"))
			goto out;

		/* update values on specified CPU */
		for (i = 0; i < entries; i++)
			values[i] = value;

		count = entries;
		batch_opts.elem_flags = (u64)cpu << 32 | BPF_F_CPU;
		err = bpf_map_update_batch(map_fd, keys, values, &count, &batch_opts);
		if (!ASSERT_OK(err, "bpf_map_update_batch specified cpu"))
			goto out;
		if (!ASSERT_EQ(count, entries, "bpf_map_update_batch count"))
			goto out;

		/* lookup values on specified CPU */
		batch = 0;
		count = entries;
		memset(values, 0, entries * value_sz);
		err = bpf_map_lookup_batch(map_fd, NULL, &batch, keys, values, &count, &batch_opts);
		if (!ASSERT_TRUE(!err || err == -ENOENT, "bpf_map_lookup_batch specified cpu"))
			goto out;
		if (!ASSERT_EQ(count, entries, "bpf_map_lookup_batch count"))
			goto out;

		for (i = 0; i < entries; i++)
			if (!ASSERT_EQ(values[i], value,
				       "bpf_map_lookup_batch value on specified cpu"))
				goto out;

		/* lookup values from all CPUs */
		batch = 0;
		count = entries;
		batch_opts.elem_flags = 0;
		memset(values_percpu, 0, roundup(value_sz, 8) * nr_cpus * entries);
		err = bpf_map_lookup_batch(map_fd, NULL, &batch, keys, values_percpu, &count,
					   &batch_opts);
		if (!ASSERT_TRUE(!err || err == -ENOENT, "bpf_map_lookup_batch all_cpus"))
			goto out;
		if (!ASSERT_EQ(count, entries, "bpf_map_lookup_batch count"))
			goto out;

		for (i = 0; i < entries; i++) {
			values_row = (void *) values_percpu +
				     roundup(value_sz, 8) * i * nr_cpus;
			for (j = 0; j < nr_cpus; j++) {
				v = *(u32 *) (values_row + roundup(value_sz, 8) * j);
				if (!ASSERT_EQ(v, j != cpu ? 0 : value,
					       "bpf_map_lookup_batch value all_cpus"))
					goto out;
			}
		}
	}

out:
	free(values_percpu);
	free(values);
}

static void test_percpu_map_cpu_flag(enum bpf_map_type map_type)
{
	struct percpu_alloc_array *skel;
	size_t key_sz = sizeof(int);
	int *keys, nr_cpus, i, err;
	struct bpf_map *map;
	u32 max_entries;

	nr_cpus = libbpf_num_possible_cpus();
	if (!ASSERT_GT(nr_cpus, 0, "libbpf_num_possible_cpus"))
		return;

	max_entries = nr_cpus * 2;
	keys = calloc(max_entries, key_sz);
	if (!ASSERT_OK_PTR(keys, "calloc keys"))
		return;

	for (i = 0; i < max_entries; i++)
		keys[i] = i;

	skel = percpu_alloc_array__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_array__open")) {
		free(keys);
		return;
	}

	map = skel->maps.percpu;
	bpf_map__set_type(map, map_type);
	bpf_map__set_max_entries(map, max_entries);

	err = percpu_alloc_array__load(skel);
	if (!ASSERT_OK(err, "test_percpu_alloc__load"))
		goto out;

	test_percpu_map_op_cpu_flag(map, keys, key_sz, nr_cpus, nr_cpus, true);
out:
	percpu_alloc_array__destroy(skel);
	free(keys);
}

static void test_percpu_array_cpu_flag(void)
{
	test_percpu_map_cpu_flag(BPF_MAP_TYPE_PERCPU_ARRAY);
}

static void test_percpu_hash_cpu_flag(void)
{
	test_percpu_map_cpu_flag(BPF_MAP_TYPE_PERCPU_HASH);
}

static void test_lru_percpu_hash_cpu_flag(void)
{
	test_percpu_map_cpu_flag(BPF_MAP_TYPE_LRU_PERCPU_HASH);
}

static void test_percpu_cgroup_storage_cpu_flag(void)
{
	struct percpu_alloc_array *skel = NULL;
	struct bpf_cgroup_storage_key key;
	int cgroup, prog_fd, nr_cpus, err;
	struct bpf_map *map;

	nr_cpus = libbpf_num_possible_cpus();
	if (!ASSERT_GT(nr_cpus, 0, "libbpf_num_possible_cpus"))
		return;

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "setup_cgroup_environment"))
		return;

	cgroup = create_and_get_cgroup("/cg_percpu");
	if (!ASSERT_GE(cgroup, 0, "create_and_get_cgroup")) {
		cleanup_cgroup_environment();
		return;
	}

	err = join_cgroup("/cg_percpu");
	if (!ASSERT_OK(err, "join_cgroup"))
		goto out;

	skel = percpu_alloc_array__open_and_load();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_array__open_and_load"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.cgroup_egress);
	err = bpf_prog_attach(prog_fd, cgroup, BPF_CGROUP_INET_EGRESS, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out;

	map = skel->maps.percpu_cgroup_storage;
	err = bpf_map_get_next_key(bpf_map__fd(map), NULL, &key);
	if (!ASSERT_OK(err, "bpf_map_get_next_key"))
		goto out;

	test_percpu_map_op_cpu_flag(map, &key, sizeof(key), 1, nr_cpus, false);
out:
	bpf_prog_detach2(-1, cgroup, BPF_CGROUP_INET_EGRESS);
	close(cgroup);
	cleanup_cgroup_environment();
	percpu_alloc_array__destroy(skel);
}

static void test_map_op_cpu_flag(enum bpf_map_type map_type)
{
	u32 max_entries = 1, count = max_entries;
	u64 flags, batch = 0, val = 0;
	int err, map_fd, key = 0;
	LIBBPF_OPTS(bpf_map_batch_opts, batch_opts);

	map_fd = bpf_map_create(map_type, "test_cpu_flag", sizeof(int), sizeof(u64), max_entries,
				NULL);
	if (!ASSERT_GE(map_fd, 0, "bpf_map_create"))
		return;

	flags = BPF_F_ALL_CPUS;
	err = bpf_map_update_elem(map_fd, &key, &val, flags);
	ASSERT_ERR(err, "bpf_map_update_elem all_cpus");

	batch_opts.elem_flags = BPF_F_ALL_CPUS;
	err = bpf_map_update_batch(map_fd, &key, &val, &count, &batch_opts);
	ASSERT_ERR(err, "bpf_map_update_batch all_cpus");

	flags = BPF_F_CPU;
	err = bpf_map_lookup_elem_flags(map_fd, &key, &val, flags);
	ASSERT_ERR(err, "bpf_map_lookup_elem_flags cpu");

	batch_opts.elem_flags = BPF_F_CPU;
	err = bpf_map_lookup_batch(map_fd, NULL, &batch, &key, &val, &count, &batch_opts);
	ASSERT_ERR(err, "bpf_map_lookup_batch cpu");

	close(map_fd);
}

static void test_array_cpu_flag(void)
{
	test_map_op_cpu_flag(BPF_MAP_TYPE_ARRAY);
}

static void test_hash_cpu_flag(void)
{
	test_map_op_cpu_flag(BPF_MAP_TYPE_HASH);
}

void test_percpu_alloc(void)
{
	if (test__start_subtest("array"))
		test_array();
	if (test__start_subtest("array_sleepable"))
		test_array_sleepable();
	if (test__start_subtest("cgrp_local_storage"))
		test_cgrp_local_storage();
	if (test__start_subtest("failure_tests"))
		test_failure();
	if (test__start_subtest("cpu_flag_percpu_array"))
		test_percpu_array_cpu_flag();
	if (test__start_subtest("cpu_flag_percpu_hash"))
		test_percpu_hash_cpu_flag();
	if (test__start_subtest("cpu_flag_lru_percpu_hash"))
		test_lru_percpu_hash_cpu_flag();
	if (test__start_subtest("cpu_flag_percpu_cgroup_storage"))
		test_percpu_cgroup_storage_cpu_flag();
	if (test__start_subtest("cpu_flag_array"))
		test_array_cpu_flag();
	if (test__start_subtest("cpu_flag_hash"))
		test_hash_cpu_flag();
}
