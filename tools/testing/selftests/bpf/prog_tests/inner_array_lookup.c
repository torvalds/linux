// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>

#include "inner_array_lookup.skel.h"

void test_inner_array_lookup(void)
{
	int map1_fd, err;
	int key = 3;
	int val = 1;
	struct inner_array_lookup *skel;

	skel = inner_array_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_load_skeleton"))
		return;

	err = inner_array_lookup__attach(skel);
	if (!ASSERT_OK(err, "skeleton_attach"))
		goto cleanup;

	map1_fd = bpf_map__fd(skel->maps.inner_map1);
	bpf_map_update_elem(map1_fd, &key, &val, 0);

	/* Probe should have set the element at index 3 to 2 */
	bpf_map_lookup_elem(map1_fd, &key, &val);
	ASSERT_EQ(val, 2, "value_is_2");

cleanup:
	inner_array_lookup__destroy(skel);
}
