// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "for_each_hash_map_elem.skel.h"
#include "for_each_array_map_elem.skel.h"
#include "for_each_map_elem_write_key.skel.h"

static unsigned int duration;

static void test_hash_map(void)
{
	int i, err, max_entries;
	struct for_each_hash_map_elem *skel;
	__u64 *percpu_valbuf = NULL;
	size_t percpu_val_sz;
	__u32 key, num_cpus;
	__u64 val;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = for_each_hash_map_elem__open_and_load();
	if (!ASSERT_OK_PTR(skel, "for_each_hash_map_elem__open_and_load"))
		return;

	max_entries = bpf_map__max_entries(skel->maps.hashmap);
	for (i = 0; i < max_entries; i++) {
		key = i;
		val = i + 1;
		err = bpf_map__update_elem(skel->maps.hashmap, &key, sizeof(key),
					   &val, sizeof(val), BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	num_cpus = bpf_num_possible_cpus();
	percpu_val_sz = sizeof(__u64) * num_cpus;
	percpu_valbuf = malloc(percpu_val_sz);
	if (!ASSERT_OK_PTR(percpu_valbuf, "percpu_valbuf"))
		goto out;

	key = 1;
	for (i = 0; i < num_cpus; i++)
		percpu_valbuf[i] = i + 1;
	err = bpf_map__update_elem(skel->maps.percpu_map, &key, sizeof(key),
				   percpu_valbuf, percpu_val_sz, BPF_ANY);
	if (!ASSERT_OK(err, "percpu_map_update"))
		goto out;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_pkt_access), &topts);
	duration = topts.duration;
	if (CHECK(err || topts.retval, "ipv4", "err %d errno %d retval %d\n",
		  err, errno, topts.retval))
		goto out;

	ASSERT_EQ(skel->bss->hashmap_output, 4, "hashmap_output");
	ASSERT_EQ(skel->bss->hashmap_elems, max_entries, "hashmap_elems");

	key = 1;
	err = bpf_map__lookup_elem(skel->maps.hashmap, &key, sizeof(key), &val, sizeof(val), 0);
	ASSERT_ERR(err, "hashmap_lookup");

	ASSERT_EQ(skel->bss->percpu_called, 1, "percpu_called");
	ASSERT_LT(skel->bss->cpu, num_cpus, "num_cpus");
	ASSERT_EQ(skel->bss->percpu_map_elems, 1, "percpu_map_elems");
	ASSERT_EQ(skel->bss->percpu_key, 1, "percpu_key");
	ASSERT_EQ(skel->bss->percpu_val, skel->bss->cpu + 1, "percpu_val");
	ASSERT_EQ(skel->bss->percpu_output, 100, "percpu_output");
out:
	free(percpu_valbuf);
	for_each_hash_map_elem__destroy(skel);
}

static void test_array_map(void)
{
	__u32 key, num_cpus, max_entries;
	int i, err;
	struct for_each_array_map_elem *skel;
	__u64 *percpu_valbuf = NULL;
	size_t percpu_val_sz;
	__u64 val, expected_total;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = for_each_array_map_elem__open_and_load();
	if (!ASSERT_OK_PTR(skel, "for_each_array_map_elem__open_and_load"))
		return;

	expected_total = 0;
	max_entries = bpf_map__max_entries(skel->maps.arraymap);
	for (i = 0; i < max_entries; i++) {
		key = i;
		val = i + 1;
		/* skip the last iteration for expected total */
		if (i != max_entries - 1)
			expected_total += val;
		err = bpf_map__update_elem(skel->maps.arraymap, &key, sizeof(key),
					   &val, sizeof(val), BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	num_cpus = bpf_num_possible_cpus();
	percpu_val_sz = sizeof(__u64) * num_cpus;
	percpu_valbuf = malloc(percpu_val_sz);
	if (!ASSERT_OK_PTR(percpu_valbuf, "percpu_valbuf"))
		goto out;

	key = 0;
	for (i = 0; i < num_cpus; i++)
		percpu_valbuf[i] = i + 1;
	err = bpf_map__update_elem(skel->maps.percpu_map, &key, sizeof(key),
				   percpu_valbuf, percpu_val_sz, BPF_ANY);
	if (!ASSERT_OK(err, "percpu_map_update"))
		goto out;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_pkt_access), &topts);
	duration = topts.duration;
	if (CHECK(err || topts.retval, "ipv4", "err %d errno %d retval %d\n",
		  err, errno, topts.retval))
		goto out;

	ASSERT_EQ(skel->bss->arraymap_output, expected_total, "array_output");
	ASSERT_EQ(skel->bss->cpu + 1, skel->bss->percpu_val, "percpu_val");

out:
	free(percpu_valbuf);
	for_each_array_map_elem__destroy(skel);
}

static void test_write_map_key(void)
{
	struct for_each_map_elem_write_key *skel;

	skel = for_each_map_elem_write_key__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "for_each_map_elem_write_key__open_and_load"))
		for_each_map_elem_write_key__destroy(skel);
}

void test_for_each(void)
{
	if (test__start_subtest("hash_map"))
		test_hash_map();
	if (test__start_subtest("array_map"))
		test_array_map();
	if (test__start_subtest("write_map_key"))
		test_write_map_key();
}
