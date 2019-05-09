// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_stacktrace_map(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	const char *file = "./test_stacktrace_map.o";
	int bytes, efd, err, pmu_fd, prog_fd, stack_trace_len;
	struct perf_event_attr attr = {};
	__u32 key, val, duration = 0;
	struct bpf_object *obj;
	char buf[256];

	err = bpf_prog_load(file, BPF_PROG_TYPE_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load", "err %d errno %d\n", err, errno))
		return;

	/* Get the ID for the sched/sched_switch tracepoint */
	snprintf(buf, sizeof(buf),
		 "/sys/kernel/debug/tracing/events/sched/sched_switch/id");
	efd = open(buf, O_RDONLY, 0);
	if (CHECK(efd < 0, "open", "err %d errno %d\n", efd, errno))
		goto close_prog;

	bytes = read(efd, buf, sizeof(buf));
	close(efd);
	if (bytes <= 0 || bytes >= sizeof(buf))
		goto close_prog;

	/* Open the perf event and attach bpf progrram */
	attr.config = strtol(buf, NULL, 0);
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_CALLCHAIN;
	attr.sample_period = 1;
	attr.wakeup_events = 1;
	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);
	if (CHECK(pmu_fd < 0, "perf_event_open", "err %d errno %d\n",
		  pmu_fd, errno))
		goto close_prog;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
	if (err)
		goto disable_pmu;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_SET_BPF, prog_fd);
	if (err)
		goto disable_pmu;

	/* find map fds */
	control_map_fd = bpf_find_map(__func__, obj, "control_map");
	if (control_map_fd < 0)
		goto disable_pmu;

	stackid_hmap_fd = bpf_find_map(__func__, obj, "stackid_hmap");
	if (stackid_hmap_fd < 0)
		goto disable_pmu;

	stackmap_fd = bpf_find_map(__func__, obj, "stackmap");
	if (stackmap_fd < 0)
		goto disable_pmu;

	stack_amap_fd = bpf_find_map(__func__, obj, "stack_amap");
	if (stack_amap_fd < 0)
		goto disable_pmu;

	/* give some time for bpf program run */
	sleep(1);

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
		goto disable_pmu_noerr;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu_noerr;

	stack_trace_len = PERF_MAX_STACK_DEPTH * sizeof(__u64);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	if (CHECK(err, "compare_stack_ips stackmap vs. stack_amap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu_noerr;

	goto disable_pmu_noerr;
disable_pmu:
	error_cnt++;
disable_pmu_noerr:
	ioctl(pmu_fd, PERF_EVENT_IOC_DISABLE);
	close(pmu_fd);
close_prog:
	bpf_object__close(obj);
}
