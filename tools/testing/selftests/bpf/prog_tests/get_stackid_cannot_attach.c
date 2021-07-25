// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#include <test_progs.h>
#include "test_stacktrace_build_id.skel.h"

void test_get_stackid_cannot_attach(void)
{
	struct perf_event_attr attr = {
		/* .type = PERF_TYPE_SOFTWARE, */
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.precise_ip = 1,
		.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_BRANCH_STACK,
		.branch_sample_type = PERF_SAMPLE_BRANCH_USER |
			PERF_SAMPLE_BRANCH_NO_FLAGS |
			PERF_SAMPLE_BRANCH_NO_CYCLES |
			PERF_SAMPLE_BRANCH_CALL_STACK,
		.sample_period = 5000,
		.size = sizeof(struct perf_event_attr),
	};
	struct test_stacktrace_build_id *skel;
	__u32 duration = 0;
	int pmu_fd, err;

	skel = test_stacktrace_build_id__open();
	if (CHECK(!skel, "skel_open", "skeleton open failed\n"))
		return;

	/* override program type */
	bpf_program__set_perf_event(skel->progs.oncpu);

	err = test_stacktrace_build_id__load(skel);
	if (CHECK(err, "skel_load", "skeleton load failed: %d\n", err))
		goto cleanup;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);
	if (pmu_fd < 0 && (errno == ENOENT || errno == EOPNOTSUPP)) {
		printf("%s:SKIP:cannot open PERF_COUNT_HW_CPU_CYCLES with precise_ip > 0\n",
		       __func__);
		test__skip();
		goto cleanup;
	}
	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n",
		  pmu_fd, errno))
		goto cleanup;

	skel->links.oncpu = bpf_program__attach_perf_event(skel->progs.oncpu,
							   pmu_fd);
	ASSERT_ERR_PTR(skel->links.oncpu, "attach_perf_event_no_callchain");
	close(pmu_fd);

	/* add PERF_SAMPLE_CALLCHAIN, attach should succeed */
	attr.sample_type |= PERF_SAMPLE_CALLCHAIN;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);

	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n",
		  pmu_fd, errno))
		goto cleanup;

	skel->links.oncpu = bpf_program__attach_perf_event(skel->progs.oncpu,
							   pmu_fd);
	ASSERT_OK_PTR(skel->links.oncpu, "attach_perf_event_callchain");
	close(pmu_fd);

	/* add exclude_callchain_kernel, attach should fail */
	attr.exclude_callchain_kernel = 1;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);

	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n",
		  pmu_fd, errno))
		goto cleanup;

	skel->links.oncpu = bpf_program__attach_perf_event(skel->progs.oncpu,
							   pmu_fd);
	ASSERT_ERR_PTR(skel->links.oncpu, "attach_perf_event_exclude_callchain_kernel");
	close(pmu_fd);

cleanup:
	test_stacktrace_build_id__destroy(skel);
}
