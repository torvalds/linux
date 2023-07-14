// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_stacktrace_build_id.skel.h"

void test_stacktrace_build_id_nmi(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd;
	struct test_stacktrace_build_id *skel;
	int err, pmu_fd;
	struct perf_event_attr attr = {
		.freq = 1,
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	__u32 key, prev_key, val, duration = 0;
	char buf[BPF_BUILD_ID_SIZE];
	struct bpf_stack_build_id id_offs[PERF_MAX_STACK_DEPTH];
	int build_id_matches = 0, build_id_size;
	int i, retry = 1;

	attr.sample_freq = read_perf_max_sample_freq();

retry:
	skel = test_stacktrace_build_id__open();
	if (CHECK(!skel, "skel_open", "skeleton open failed\n"))
		return;

	/* override program type */
	bpf_program__set_type(skel->progs.oncpu, BPF_PROG_TYPE_PERF_EVENT);

	err = test_stacktrace_build_id__load(skel);
	if (CHECK(err, "skel_load", "skeleton load failed: %d\n", err))
		goto cleanup;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);
	if (pmu_fd < 0 && errno == ENOENT) {
		printf("%s:SKIP:no PERF_COUNT_HW_CPU_CYCLES\n", __func__);
		test__skip();
		goto cleanup;
	}
	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n",
		  pmu_fd, errno))
		goto cleanup;

	skel->links.oncpu = bpf_program__attach_perf_event(skel->progs.oncpu,
							   pmu_fd);
	if (!ASSERT_OK_PTR(skel->links.oncpu, "attach_perf_event")) {
		close(pmu_fd);
		goto cleanup;
	}

	/* find map fds */
	control_map_fd = bpf_map__fd(skel->maps.control_map);
	stackid_hmap_fd = bpf_map__fd(skel->maps.stackid_hmap);
	stackmap_fd = bpf_map__fd(skel->maps.stackmap);

	if (CHECK_FAIL(system("dd if=/dev/urandom of=/dev/zero count=4 2> /dev/null")))
		goto cleanup;
	if (CHECK_FAIL(system("taskset 0x1 ./urandom_read 100000")))
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

	build_id_size = read_build_id("urandom_read", buf, sizeof(buf));
	err = build_id_size < 0 ? build_id_size : 0;

	if (CHECK(err, "get build_id with readelf",
		  "err %d errno %d\n", err, errno))
		goto cleanup;

	err = bpf_map__get_next_key(skel->maps.stackmap, NULL, &key, sizeof(key));
	if (CHECK(err, "get_next_key from stackmap",
		  "err %d, errno %d\n", err, errno))
		goto cleanup;

	do {
		err = bpf_map__lookup_elem(skel->maps.stackmap, &key, sizeof(key),
					   id_offs, sizeof(id_offs), 0);
		if (CHECK(err, "lookup_elem from stackmap",
			  "err %d, errno %d\n", err, errno))
			goto cleanup;
		for (i = 0; i < PERF_MAX_STACK_DEPTH; ++i)
			if (id_offs[i].status == BPF_STACK_BUILD_ID_VALID &&
			    id_offs[i].offset != 0) {
				if (memcmp(buf, id_offs[i].build_id, build_id_size) == 0)
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

	/*
	 * We intentionally skip compare_stack_ips(). This is because we
	 * only support one in_nmi() ips-to-build_id translation per cpu
	 * at any time, thus stack_amap here will always fallback to
	 * BPF_STACK_BUILD_ID_IP;
	 */

cleanup:
	test_stacktrace_build_id__destroy(skel);
}
