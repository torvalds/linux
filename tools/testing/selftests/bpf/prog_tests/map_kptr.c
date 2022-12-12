// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "map_kptr.skel.h"
#include "map_kptr_fail.skel.h"

static void test_map_kptr_success(bool test_run)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct map_kptr *skel;
	int key = 0, ret;
	char buf[16];

	skel = map_kptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "map_kptr__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_map_kptr_ref), &opts);
	ASSERT_OK(ret, "test_map_kptr_ref refcount");
	ASSERT_OK(opts.retval, "test_map_kptr_ref retval");
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_map_kptr_ref2), &opts);
	ASSERT_OK(ret, "test_map_kptr_ref2 refcount");
	ASSERT_OK(opts.retval, "test_map_kptr_ref2 retval");

	if (test_run)
		goto exit;

	ret = bpf_map__update_elem(skel->maps.array_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "array_map update");
	ret = bpf_map__update_elem(skel->maps.array_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "array_map update2");

	ret = bpf_map__update_elem(skel->maps.hash_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "hash_map update");
	ret = bpf_map__delete_elem(skel->maps.hash_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "hash_map delete");

	ret = bpf_map__update_elem(skel->maps.hash_malloc_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "hash_malloc_map update");
	ret = bpf_map__delete_elem(skel->maps.hash_malloc_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "hash_malloc_map delete");

	ret = bpf_map__update_elem(skel->maps.lru_hash_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "lru_hash_map update");
	ret = bpf_map__delete_elem(skel->maps.lru_hash_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "lru_hash_map delete");

exit:
	map_kptr__destroy(skel);
}

void test_map_kptr(void)
{
	if (test__start_subtest("success")) {
		test_map_kptr_success(false);
		/* Do test_run twice, so that we see refcount going back to 1
		 * after we leave it in map from first iteration.
		 */
		test_map_kptr_success(true);
	}

	RUN_TESTS(map_kptr_fail);
}
