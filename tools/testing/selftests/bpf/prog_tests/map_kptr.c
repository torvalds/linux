// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#include "map_kptr.skel.h"

void test_map_kptr(void)
{
	struct map_kptr *skel;
	int key = 0, ret;
	char buf[24];

	skel = map_kptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "map_kptr__open_and_load"))
		return;

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.array_map), &key, buf, 0);
	ASSERT_OK(ret, "array_map update");
	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.array_map), &key, buf, 0);
	ASSERT_OK(ret, "array_map update2");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.hash_map), &key, buf, 0);
	ASSERT_OK(ret, "hash_map update");
	ret = bpf_map_delete_elem(bpf_map__fd(skel->maps.hash_map), &key);
	ASSERT_OK(ret, "hash_map delete");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.hash_malloc_map), &key, buf, 0);
	ASSERT_OK(ret, "hash_malloc_map update");
	ret = bpf_map_delete_elem(bpf_map__fd(skel->maps.hash_malloc_map), &key);
	ASSERT_OK(ret, "hash_malloc_map delete");

	ret = bpf_map_update_elem(bpf_map__fd(skel->maps.lru_hash_map), &key, buf, 0);
	ASSERT_OK(ret, "lru_hash_map update");
	ret = bpf_map_delete_elem(bpf_map__fd(skel->maps.lru_hash_map), &key);
	ASSERT_OK(ret, "lru_hash_map delete");

	map_kptr__destroy(skel);
}
