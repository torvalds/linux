// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>

#include "test_btf_map_in_map.skel.h"

void test_btf_map_in_map(void)
{
	int duration = 0, err, key = 0, val;
	struct test_btf_map_in_map* skel;

	skel = test_btf_map_in_map__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open&load skeleton\n"))
		return;

	err = test_btf_map_in_map__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* inner1 = input, inner2 = input + 1 */
	val = bpf_map__fd(skel->maps.inner_map1);
	bpf_map_update_elem(bpf_map__fd(skel->maps.outer_arr), &key, &val, 0);
	val = bpf_map__fd(skel->maps.inner_map2);
	bpf_map_update_elem(bpf_map__fd(skel->maps.outer_hash), &key, &val, 0);
	skel->bss->input = 1;
	usleep(1);

	bpf_map_lookup_elem(bpf_map__fd(skel->maps.inner_map1), &key, &val);
	CHECK(val != 1, "inner1", "got %d != exp %d\n", val, 1);
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.inner_map2), &key, &val);
	CHECK(val != 2, "inner2", "got %d != exp %d\n", val, 2);

	/* inner1 = input + 1, inner2 = input */
	val = bpf_map__fd(skel->maps.inner_map2);
	bpf_map_update_elem(bpf_map__fd(skel->maps.outer_arr), &key, &val, 0);
	val = bpf_map__fd(skel->maps.inner_map1);
	bpf_map_update_elem(bpf_map__fd(skel->maps.outer_hash), &key, &val, 0);
	skel->bss->input = 3;
	usleep(1);

	bpf_map_lookup_elem(bpf_map__fd(skel->maps.inner_map1), &key, &val);
	CHECK(val != 4, "inner1", "got %d != exp %d\n", val, 4);
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.inner_map2), &key, &val);
	CHECK(val != 3, "inner2", "got %d != exp %d\n", val, 3);

cleanup:
	test_btf_map_in_map__destroy(skel);
}
