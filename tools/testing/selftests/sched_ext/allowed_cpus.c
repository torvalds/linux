// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Andrea Righi <arighi@nvidia.com>
 */
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <limits.h>
#include <sched.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "allowed_cpus.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct allowed_cpus *skel;

	skel = allowed_cpus__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(allowed_cpus__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static int test_select_cpu_from_user(const struct allowed_cpus *skel,
				     const char *name, int custom_cpu,
				     bool expect_busy)
{
	int fd, ret;
	__s32 cpu;
	__u64 args[] = { getpid(), (__u64)(__s64)custom_cpu };

	LIBBPF_OPTS(bpf_test_run_opts, attr,
		.ctx_in = args,
		.ctx_size_in = sizeof(args),
	);

	fd = bpf_program__fd(skel->progs.select_cpu_from_user);
	if (fd < 0)
		return fd;

	ret = bpf_prog_test_run_opts(fd, &attr);
	if (ret < 0)
		return ret;

	/* test_run returns the signed BPF result through an unsigned field. */
	cpu = (__s32)attr.retval;
	if ((expect_busy && cpu != -EBUSY) ||
	    (!expect_busy && cpu != -EBUSY && cpu != custom_cpu)) {
		SCX_ERR("%s: unexpected CPU selection result %d", name, cpu);
		return -EINVAL;
	}

	return 0;
}

/* Grow until the mask covers the kernel's CPU range, including offline CPUs. */
static int alloc_affinity(cpu_set_t **mask, size_t *size)
{
	int nr_cpus = CPU_SETSIZE;
	cpu_set_t *cpus;
	int err;

	for (;;) {
		*size = CPU_ALLOC_SIZE(nr_cpus);
		cpus = CPU_ALLOC(nr_cpus);
		if (!cpus)
			return -ENOMEM;
		CPU_ZERO_S(*size, cpus);
		if (!sched_getaffinity(0, *size, cpus)) {
			*mask = cpus;
			return nr_cpus;
		}
		err = errno;
		CPU_FREE(cpus);
		if (err != EINVAL)
			return -err;
		if (nr_cpus > INT_MAX / 2)
			return -EOVERFLOW;
		nr_cpus *= 2;
	}
}

static enum scx_test_status run(void *ctx)
{
	struct allowed_cpus *skel = ctx;
	enum scx_test_status status = SCX_TEST_FAIL;
	cpu_set_t *original = NULL, *pinned = NULL;
	bool affinity_changed = false;
	size_t size;
	int first = -1, second = -1, cpu, nr_cpus;
	struct bpf_link *link = NULL;

	nr_cpus = alloc_affinity(&original, &size);
	if (nr_cpus < 0) {
		SCX_ERR("Failed to get affinity (%d)", -nr_cpus);
		goto out;
	}
	pinned = CPU_ALLOC(nr_cpus);
	if (!pinned) {
		SCX_ERR("Failed to allocate affinity mask");
		goto out;
	}
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		if (!CPU_ISSET_S(cpu, size, original))
			continue;
		if (first < 0) {
			first = cpu;
		} else {
			second = cpu;
			break;
		}
	}
	if (first < 0) {
		SCX_ERR("No CPU in affinity mask");
		goto out;
	}

	link = bpf_map__attach_struct_ops(skel->maps.allowed_cpus_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		goto out;
	}

	if (test_select_cpu_from_user(skel, "empty mask", -1, true))
		goto out;

	/* A legal candidate may be busy; selection need not succeed. */
	if (test_select_cpu_from_user(skel, "legal candidate", first, false))
		goto out;

	if (second >= 0) {
		CPU_ZERO_S(size, pinned);
		CPU_SET_S(first, size, pinned);
		if (sched_setaffinity(0, size, pinned)) {
			SCX_ERR("Failed to pin task (%d)", errno);
			goto out;
		}
		affinity_changed = true;
		if (test_select_cpu_from_user(skel, "disjoint masks", second, true))
			goto out;
	} else {
		fprintf(stderr, "Skipping disjoint masks: need two allowed CPUs\n");
	}

	/* Just sleeping is fine, plenty of scheduling events happening. */
	sleep(1);
	if (UEI_EXITED(skel, uei)) {
		SCX_ERR("Scheduler exited unexpectedly");
		goto out;
	}
	status = SCX_TEST_PASS;

out:
	if (affinity_changed && sched_setaffinity(0, size, original)) {
		SCX_ERR("Failed to restore affinity (%d)", errno);
		status = SCX_TEST_FAIL;
	}
	bpf_link__destroy(link);
	CPU_FREE(pinned);
	CPU_FREE(original);
	return status;
}

static void cleanup(void *ctx)
{
	struct allowed_cpus *skel = ctx;

	allowed_cpus__destroy(skel);
}

struct scx_test allowed_cpus = {
	.name = "allowed_cpus",
	.description = "Verify scx_bpf_select_cpu_and()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&allowed_cpus)
