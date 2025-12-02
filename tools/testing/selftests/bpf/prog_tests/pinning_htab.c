// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_pinning_htab.skel.h"

static void unpin_map(const char *map_name, const char *pin_path)
{
	struct test_pinning_htab *skel;
	struct bpf_map *map;
	int err;

	skel = test_pinning_htab__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;

	map = bpf_object__find_map_by_name(skel->obj, map_name);
	if (!ASSERT_OK_PTR(map, "bpf_object__find_map_by_name"))
		goto out;

	err = bpf_map__pin(map, pin_path);
	if (!ASSERT_OK(err, "bpf_map__pin"))
		goto out;

	err = bpf_map__unpin(map, pin_path);
	ASSERT_OK(err, "bpf_map__unpin");
out:
	test_pinning_htab__destroy(skel);
}

void test_pinning_htab(void)
{
	if (test__start_subtest("timer_prealloc"))
		unpin_map("timer_prealloc", "/sys/fs/bpf/timer_prealloc");
	if (test__start_subtest("timer_no_prealloc"))
		unpin_map("timer_no_prealloc", "/sys/fs/bpf/timer_no_prealloc");
}
