// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Bytedance

#include <test_progs.h>

#include "test_map_lookup_percpu_elem.skel.h"

#define TEST_VALUE  1

void test_map_lookup_percpu_elem(void)
{
	struct test_map_lookup_percpu_elem *skel;
	int key = 0, ret;
	int nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	int *buf;

	buf = (int *)malloc(nr_cpus*sizeof(int));
	if (!ASSERT_OK_PTR(buf, "malloc"))
		return;
	memset(buf, 0, nr_cpus*sizeof(int));
	buf[0] = TEST_VALUE;

	skel = test_map_lookup_percpu_elem__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_map_lookup_percpu_elem__open_and_load"))
		return;
	ret = test_map_lookup_percpu_elem__attach(skel);
	ASSERT_OK(ret, "test_map_lookup_percpu_elem__attach");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.percpu_array_map), &key, buf, 0);
	ASSERT_OK(ret, "percpu_array_map update");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.percpu_hash_map), &key, buf, 0);
	ASSERT_OK(ret, "percpu_hash_map update");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.percpu_lru_hash_map), &key, buf, 0);
	ASSERT_OK(ret, "percpu_lru_hash_map update");

	syscall(__NR_getuid);

	ret = skel->bss->percpu_array_elem_val == TEST_VALUE &&
	      skel->bss->percpu_hash_elem_val == TEST_VALUE &&
	      skel->bss->percpu_lru_hash_elem_val == TEST_VALUE;
	ASSERT_OK(!ret, "bpf_map_lookup_percpu_elem success");

	test_map_lookup_percpu_elem__destroy(skel);
}
