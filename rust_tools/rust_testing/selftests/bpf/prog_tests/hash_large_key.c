// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_hash_large_key.skel.h"

void test_hash_large_key(void)
{
	int err, value = 21, duration = 0, hash_map_fd;
	struct test_hash_large_key *skel;

	struct bigelement {
		int a;
		char b[4096];
		long long c;
	} key;
	bzero(&key, sizeof(key));

	skel = test_hash_large_key__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open/load failed\n"))
		return;

	hash_map_fd = bpf_map__fd(skel->maps.hash_map);
	if (CHECK(hash_map_fd < 0, "bpf_map__fd", "failed\n"))
		goto cleanup;

	err = test_hash_large_key__attach(skel);
	if (CHECK(err, "attach_raw_tp", "err %d\n", err))
		goto cleanup;

	err = bpf_map_update_elem(hash_map_fd, &key, &value, BPF_ANY);
	if (CHECK(err, "bpf_map_update_elem", "errno=%d\n", errno))
		goto cleanup;

	key.c = 1;
	err = bpf_map_lookup_elem(hash_map_fd, &key, &value);
	if (CHECK(err, "bpf_map_lookup_elem", "errno=%d\n", errno))
		goto cleanup;

	CHECK_FAIL(value != 42);

cleanup:
	test_hash_large_key__destroy(skel);
}
