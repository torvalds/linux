// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"
#include "bpf_rlimit.h"

#define CHECK(condition, tag, format...) ({		\
	int __ret = !!(condition);			\
	if (__ret) {					\
		printf("%s:FAIL:%s ", __func__, tag);	\
		printf(format);				\
	} else {					\
		printf("%s:PASS:%s\n", __func__, tag);	\
	}						\
	__ret;						\
})

static int bpf_find_map(const char *test, struct bpf_object *obj,
			const char *name)
{
	struct bpf_map *map;

	map = bpf_object__find_map_by_name(obj, name);
	if (!map)
		return -1;
	return bpf_map__fd(map);
}

#define TEST_CGROUP "/test-bpf-get-cgroup-id/"

int main(int argc, char **argv)
{
	const char *probe_name = "syscalls/sys_enter_nanosleep";
	const char *file = "get_cgroup_id_kern.o";
	int err, bytes, efd, prog_fd, pmu_fd;
	int cgroup_fd, cgidmap_fd, pidmap_fd;
	struct perf_event_attr attr = {};
	struct bpf_object *obj;
	__u64 kcgid = 0, ucgid;
	__u32 key = 0, pid;
	int exit_code = 1;
	char buf[256];
	const struct timespec req = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	cgroup_fd = cgroup_setup_and_join(TEST_CGROUP);
	if (CHECK(cgroup_fd < 0, "cgroup_setup_and_join", "err %d errno %d\n", cgroup_fd, errno))
		return 1;

	err = bpf_prog_load(file, BPF_PROG_TYPE_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "bpf_prog_load", "err %d errno %d\n", err, errno))
		goto cleanup_cgroup_env;

	cgidmap_fd = bpf_find_map(__func__, obj, "cg_ids");
	if (CHECK(cgidmap_fd < 0, "bpf_find_map", "err %d errno %d\n",
		  cgidmap_fd, errno))
		goto close_prog;

	pidmap_fd = bpf_find_map(__func__, obj, "pidmap");
	if (CHECK(pidmap_fd < 0, "bpf_find_map", "err %d errno %d\n",
		  pidmap_fd, errno))
		goto close_prog;

	pid = getpid();
	bpf_map_update_elem(pidmap_fd, &key, &pid, 0);

	snprintf(buf, sizeof(buf),
		 "/sys/kernel/debug/tracing/events/%s/id", probe_name);
	efd = open(buf, O_RDONLY, 0);
	if (CHECK(efd < 0, "open", "err %d errno %d\n", efd, errno))
		goto close_prog;
	bytes = read(efd, buf, sizeof(buf));
	close(efd);
	if (CHECK(bytes <= 0 || bytes >= sizeof(buf), "read",
		  "bytes %d errno %d\n", bytes, errno))
		goto close_prog;

	attr.config = strtol(buf, NULL, 0);
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.sample_type = PERF_SAMPLE_RAW;
	attr.sample_period = 1;
	attr.wakeup_events = 1;

	/* attach to this pid so the all bpf invocations will be in the
	 * cgroup associated with this pid.
	 */
	pmu_fd = syscall(__NR_perf_event_open, &attr, getpid(), -1, -1, 0);
	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n", pmu_fd,
		  errno))
		goto close_prog;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
	if (CHECK(err, "perf_event_ioc_enable", "err %d errno %d\n", err,
		  errno))
		goto close_pmu;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_SET_BPF, prog_fd);
	if (CHECK(err, "perf_event_ioc_set_bpf", "err %d errno %d\n", err,
		  errno))
		goto close_pmu;

	/* trigger some syscalls */
	syscall(__NR_nanosleep, &req, NULL);

	err = bpf_map_lookup_elem(cgidmap_fd, &key, &kcgid);
	if (CHECK(err, "bpf_map_lookup_elem", "err %d errno %d\n", err, errno))
		goto close_pmu;

	ucgid = get_cgroup_id(TEST_CGROUP);
	if (CHECK(kcgid != ucgid, "compare_cgroup_id",
		  "kern cgid %llx user cgid %llx", kcgid, ucgid))
		goto close_pmu;

	exit_code = 0;
	printf("%s:PASS\n", argv[0]);

close_pmu:
	close(pmu_fd);
close_prog:
	bpf_object__close(obj);
cleanup_cgroup_env:
	cleanup_cgroup_environment();
	return exit_code;
}
