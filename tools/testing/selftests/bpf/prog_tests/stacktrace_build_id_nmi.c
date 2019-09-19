// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

static __u64 read_perf_max_sample_freq(void)
{
	__u64 sample_freq = 5000; /* fallback to 5000 on error */
	FILE *f;

	f = fopen("/proc/sys/kernel/perf_event_max_sample_rate", "r");
	if (f == NULL)
		return sample_freq;
	fscanf(f, "%llu", &sample_freq);
	fclose(f);
	return sample_freq;
}

void test_stacktrace_build_id_nmi(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	const char *prog_name = "tracepoint/random/urandom_read";
	const char *file = "./test_stacktrace_build_id.o";
	int err, pmu_fd, prog_fd;
	struct perf_event_attr attr = {
		.freq = 1,
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	__u32 key, previous_key, val, duration = 0;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link;
	char buf[256];
	int i, j;
	struct bpf_stack_build_id id_offs[PERF_MAX_STACK_DEPTH];
	int build_id_matches = 0;
	int retry = 1;

	attr.sample_freq = read_perf_max_sample_freq();

retry:
	err = bpf_prog_load(file, BPF_PROG_TYPE_PERF_EVENT, &obj, &prog_fd);
	if (CHECK(err, "prog_load", "err %d errno %d\n", err, errno))
		return;

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (CHECK(!prog, "find_prog", "prog '%s' not found\n", prog_name))
		goto close_prog;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1 /* pid */,
			 0 /* cpu 0 */, -1 /* group id */,
			 0 /* flags */);
	if (CHECK(pmu_fd < 0, "perf_event_open",
		  "err %d errno %d. Does the test host support PERF_COUNT_HW_CPU_CYCLES?\n",
		  pmu_fd, errno))
		goto close_prog;

	link = bpf_program__attach_perf_event(prog, pmu_fd);
	if (CHECK(IS_ERR(link), "attach_perf_event",
		  "err %ld\n", PTR_ERR(link))) {
		close(pmu_fd);
		goto close_prog;
	}

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

	if (CHECK_FAIL(system("dd if=/dev/urandom of=/dev/zero count=4 2> /dev/null")))
		goto disable_pmu;
	if (CHECK_FAIL(system("taskset 0x1 ./urandom_read 100000")))
		goto disable_pmu;
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
		bpf_link__destroy(link);
		bpf_object__close(obj);
		printf("%s:WARN:Didn't find expected build ID from the map, retrying\n",
		       __func__);
		goto retry;
	}

	if (CHECK(build_id_matches < 1, "build id match",
		  "Didn't find expected build ID from the map\n"))
		goto disable_pmu;

	/*
	 * We intentionally skip compare_stack_ips(). This is because we
	 * only support one in_nmi() ips-to-build_id translation per cpu
	 * at any time, thus stack_amap here will always fallback to
	 * BPF_STACK_BUILD_ID_IP;
	 */

disable_pmu:
	bpf_link__destroy(link);
close_prog:
	bpf_object__close(obj);
}
