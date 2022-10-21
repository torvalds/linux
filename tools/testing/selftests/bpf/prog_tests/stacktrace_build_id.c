// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_stacktrace_build_id.skel.h"

void test_stacktrace_build_id(void)
{

	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	struct test_stacktrace_build_id *skel;
	int err, stack_trace_len;
	__u32 key, prev_key, val, duration = 0;
	char buf[256];
	int i, j;
	struct bpf_stack_build_id id_offs[PERF_MAX_STACK_DEPTH];
	int build_id_matches = 0;
	int retry = 1;

retry:
	skel = test_stacktrace_build_id__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open/load failed\n"))
		return;

	err = test_stacktrace_build_id__attach(skel);
	if (CHECK(err, "attach_tp", "err %d\n", err))
		goto cleanup;

	/* find map fds */
	control_map_fd = bpf_map__fd(skel->maps.control_map);
	stackid_hmap_fd = bpf_map__fd(skel->maps.stackid_hmap);
	stackmap_fd = bpf_map__fd(skel->maps.stackmap);
	stack_amap_fd = bpf_map__fd(skel->maps.stack_amap);

	if (CHECK_FAIL(system("dd if=/dev/urandom of=/dev/zero count=4 2> /dev/null")))
		goto cleanup;
	if (CHECK_FAIL(system("./urandom_read")))
		goto cleanup;
	/* disable stack trace collection */
	key = 0;
	val = 1;
	bpf_map_update_elem(control_map_fd, &key, &val, 0);

	/* for every element in stackid_hmap, we can find a corresponding one
	 * in stackmap, and vise versa.
	 */
	err = compare_map_keys(stackid_hmap_fd, stackmap_fd);
	if (CHECK(err, "compare_map_keys stackid_hmap vs. stackmap",
		  "err %d errno %d\n", err, errno))
		goto cleanup;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto cleanup;

	err = extract_build_id(buf, 256);

	if (CHECK(err, "get build_id with readelf",
		  "err %d errno %d\n", err, errno))
		goto cleanup;

	err = bpf_map__get_next_key(skel->maps.stackmap, NULL, &key, sizeof(key));
	if (CHECK(err, "get_next_key from stackmap",
		  "err %d, errno %d\n", err, errno))
		goto cleanup;

	do {
		char build_id[64];

		err = bpf_map_lookup_elem(stackmap_fd, &key, id_offs);
		if (CHECK(err, "lookup_elem from stackmap",
			  "err %d, errno %d\n", err, errno))
			goto cleanup;
		for (i = 0; i < PERF_MAX_STACK_DEPTH; ++i)
			if (id_offs[i].status == BPF_STACK_BUILD_ID_VALID &&
			    id_offs[i].offset != 0) {
				for (j = 0; j < 20; ++j)
					sprintf(build_id + 2 * j, "%02x",
						id_offs[i].build_id[j] & 0xff);
				if (strstr(buf, build_id) != NULL)
					build_id_matches = 1;
			}
		prev_key = key;
	} while (bpf_map__get_next_key(skel->maps.stackmap, &prev_key, &key, sizeof(key)) == 0);

	/* stack_map_get_build_id_offset() is racy and sometimes can return
	 * BPF_STACK_BUILD_ID_IP instead of BPF_STACK_BUILD_ID_VALID;
	 * try it one more time.
	 */
	if (build_id_matches < 1 && retry--) {
		test_stacktrace_build_id__destroy(skel);
		printf("%s:WARN:Didn't find expected build ID from the map, retrying\n",
		       __func__);
		goto retry;
	}

	if (CHECK(build_id_matches < 1, "build id match",
		  "Didn't find expected build ID from the map\n"))
		goto cleanup;

	stack_trace_len = PERF_MAX_STACK_DEPTH *
			  sizeof(struct bpf_stack_build_id);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	CHECK(err, "compare_stack_ips stackmap vs. stack_amap",
	      "err %d errno %d\n", err, errno);

cleanup:
	test_stacktrace_build_id__destroy(skel);
}
