// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_stacktrace_build_id(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	const char *file = "./test_stacktrace_build_id.o";
	int bytes, efd, err, pmu_fd, prog_fd, stack_trace_len;
	struct perf_event_attr attr = {};
	__u32 key, previous_key, val, duration = 0;
	struct bpf_object *obj;
	char buf[256];
	int i, j;
	struct bpf_stack_build_id id_offs[PERF_MAX_STACK_DEPTH];
	int build_id_matches = 0;
	int retry = 1;

retry:
	err = bpf_prog_load(file, BPF_PROG_TYPE_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load", "err %d errno %d\n", err, errno))
		goto out;

	/* Get the ID for the sched/sched_switch tracepoint */
	snprintf(buf, sizeof(buf),
		 "/sys/kernel/debug/tracing/events/random/urandom_read/id");
	efd = open(buf, O_RDONLY, 0);
	if (CHECK(efd < 0, "open", "err %d errno %d\n", efd, errno))
		goto close_prog;

	bytes = read(efd, buf, sizeof(buf));
	close(efd);
	if (CHECK(bytes <= 0 || bytes >= sizeof(buf),
		  "read", "bytes %d errno %d\n", bytes, errno))
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
	if (CHECK(err, "perf_event_ioc_enable", "err %d errno %d\n",
		  err, errno))
		goto close_pmu;

	err = ioctl(pmu_fd, PERF_EVENT_IOC_SET_BPF, prog_fd);
	if (CHECK(err, "perf_event_ioc_set_bpf", "err %d errno %d\n",
		  err, errno))
		goto disable_pmu;

	/* find map fds */
	control_map_fd = bpf_find_map(__func__, obj, "control_map");
	if (CHECK(control_map_fd < 0, "bpf_find_map control_map",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	stackid_hmap_fd = bpf_find_map(__func__, obj, "stackid_hmap");
	if (CHECK(stackid_hmap_fd < 0, "bpf_find_map stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	stackmap_fd = bpf_find_map(__func__, obj, "stackmap");
	if (CHECK(stackmap_fd < 0, "bpf_find_map stackmap", "err %d errno %d\n",
		  err, errno))
		goto disable_pmu;

	stack_amap_fd = bpf_find_map(__func__, obj, "stack_amap");
	if (CHECK(stack_amap_fd < 0, "bpf_find_map stack_amap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	assert(system("dd if=/dev/urandom of=/dev/zero count=4 2> /dev/null")
	       == 0);
	assert(system("./urandom_read") == 0);
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
		goto disable_pmu;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	err = extract_build_id(buf, 256);

	if (CHECK(err, "get build_id with readelf",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	err = bpf_map_get_next_key(stackmap_fd, NULL, &key);
	if (CHECK(err, "get_next_key from stackmap",
		  "err %d, errno %d\n", err, errno))
		goto disable_pmu;

	do {
		char build_id[64];

		err = bpf_map_lookup_elem(stackmap_fd, &key, id_offs);
		if (CHECK(err, "lookup_elem from stackmap",
			  "err %d, errno %d\n", err, errno))
			goto disable_pmu;
		for (i = 0; i < PERF_MAX_STACK_DEPTH; ++i)
			if (id_offs[i].status == BPF_STACK_BUILD_ID_VALID &&
			    id_offs[i].offset != 0) {
				for (j = 0; j < 20; ++j)
					sprintf(build_id + 2 * j, "%02x",
						id_offs[i].build_id[j] & 0xff);
				if (strstr(buf, build_id) != NULL)
					build_id_matches = 1;
			}
		previous_key = key;
	} while (bpf_map_get_next_key(stackmap_fd, &previous_key, &key) == 0);

	/* stack_map_get_build_id_offset() is racy and sometimes can return
	 * BPF_STACK_BUILD_ID_IP instead of BPF_STACK_BUILD_ID_VALID;
	 * try it one more time.
	 */
	if (build_id_matches < 1 && retry--) {
		ioctl(pmu_fd, PERF_EVENT_IOC_DISABLE);
		close(pmu_fd);
		bpf_object__close(obj);
		printf("%s:WARN:Didn't find expected build ID from the map, retrying\n",
		       __func__);
		goto retry;
	}

	if (CHECK(build_id_matches < 1, "build id match",
		  "Didn't find expected build ID from the map\n"))
		goto disable_pmu;

	stack_trace_len = PERF_MAX_STACK_DEPTH
		* sizeof(struct bpf_stack_build_id);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	CHECK(err, "compare_stack_ips stackmap vs. stack_amap",
	      "err %d errno %d\n", err, errno);

disable_pmu:
	ioctl(pmu_fd, PERF_EVENT_IOC_DISABLE);

close_pmu:
	close(pmu_fd);

close_prog:
	bpf_object__close(obj);

out:
	return;
}
