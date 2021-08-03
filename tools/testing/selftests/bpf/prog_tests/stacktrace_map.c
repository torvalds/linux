// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_stacktrace_map(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	const char *prog_name = "tracepoint/sched/sched_switch";
	int err, prog_fd, stack_trace_len;
	const char *file = "./test_stacktrace_map.o";
	__u32 key, val, duration = 0;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link;

	err = bpf_prog_load(file, BPF_PROG_TYPE_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load", "err %d errno %d\n", err, errno))
		return;

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (CHECK(!prog, "find_prog", "prog '%s' not found\n", prog_name))
		goto close_prog;

	link = bpf_program__attach_tracepoint(prog, "sched", "sched_switch");
	if (!ASSERT_OK_PTR(link, "attach_tp"))
		goto close_prog;

	/* find map fds */
	control_map_fd = bpf_find_map(__func__, obj, "control_map");
	if (CHECK_FAIL(control_map_fd < 0))
		goto disable_pmu;

	stackid_hmap_fd = bpf_find_map(__func__, obj, "stackid_hmap");
	if (CHECK_FAIL(stackid_hmap_fd < 0))
		goto disable_pmu;

	stackmap_fd = bpf_find_map(__func__, obj, "stackmap");
	if (CHECK_FAIL(stackmap_fd < 0))
		goto disable_pmu;

	stack_amap_fd = bpf_find_map(__func__, obj, "stack_amap");
	if (CHECK_FAIL(stack_amap_fd < 0))
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
		goto disable_pmu;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

	stack_trace_len = PERF_MAX_STACK_DEPTH * sizeof(__u64);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	if (CHECK(err, "compare_stack_ips stackmap vs. stack_amap",
		  "err %d errno %d\n", err, errno))
		goto disable_pmu;

disable_pmu:
	bpf_link__destroy(link);
close_prog:
	bpf_object__close(obj);
}
