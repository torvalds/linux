// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <sys/mman.h>
#include "mmap_inner_array.skel.h"

void test_mmap_inner_array(void)
{
	const long page_size = sysconf(_SC_PAGE_SIZE);
	struct mmap_inner_array *skel;
	int inner_array_fd, err;
	void *tmp;
	__u64 *val;

	skel = mmap_inner_array__open_and_load();

	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	inner_array_fd = bpf_map__fd(skel->maps.inner_array);
	tmp = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, inner_array_fd, 0);
	if (!ASSERT_OK_PTR(tmp, "inner array mmap"))
		goto out;
	val = (void *)tmp;

	err = mmap_inner_array__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out_unmap;

	skel->bss->pid = getpid();
	usleep(1);

	/* pid is set, pid_match == true and outer_map_match == false */
	ASSERT_TRUE(skel->bss->pid_match, "pid match 1");
	ASSERT_FALSE(skel->bss->outer_map_match, "outer map match 1");
	ASSERT_FALSE(skel->bss->done, "done 1");
	ASSERT_EQ(*val, 0, "value match 1");

	err = bpf_map__update_elem(skel->maps.outer_map,
				   &skel->bss->pid, sizeof(skel->bss->pid),
				   &inner_array_fd, sizeof(inner_array_fd),
				   BPF_ANY);
	if (!ASSERT_OK(err, "update elem"))
		goto out_unmap;
	usleep(1);

	/* outer map key is set, outer_map_match == true */
	ASSERT_TRUE(skel->bss->pid_match, "pid match 2");
	ASSERT_TRUE(skel->bss->outer_map_match, "outer map match 2");
	ASSERT_TRUE(skel->bss->done, "done 2");
	ASSERT_EQ(*val, skel->data->match_value, "value match 2");

out_unmap:
	munmap(tmp, page_size);
out:
	mmap_inner_array__destroy(skel);
}
