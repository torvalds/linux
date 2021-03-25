// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "recursion.skel.h"

void test_recursion(void)
{
	struct bpf_prog_info prog_info = {};
	__u32 prog_info_len = sizeof(prog_info);
	struct recursion *skel;
	int key = 0;
	int err;

	skel = recursion__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = recursion__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	ASSERT_EQ(skel->bss->pass1, 0, "pass1 == 0");
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.hash1), &key, 0);
	ASSERT_EQ(skel->bss->pass1, 1, "pass1 == 1");
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.hash1), &key, 0);
	ASSERT_EQ(skel->bss->pass1, 2, "pass1 == 2");

	ASSERT_EQ(skel->bss->pass2, 0, "pass2 == 0");
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.hash2), &key, 0);
	ASSERT_EQ(skel->bss->pass2, 1, "pass2 == 1");
	bpf_map_lookup_elem(bpf_map__fd(skel->maps.hash2), &key, 0);
	ASSERT_EQ(skel->bss->pass2, 2, "pass2 == 2");

	err = bpf_obj_get_info_by_fd(bpf_program__fd(skel->progs.on_lookup),
				     &prog_info, &prog_info_len);
	if (!ASSERT_OK(err, "get_prog_info"))
		goto out;
	ASSERT_EQ(prog_info.recursion_misses, 2, "recursion_misses");
out:
	recursion__destroy(skel);
}
