// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_stacktrace_map_raw_tp(void)
{
	const char *prog_name = "tracepoint/sched/sched_switch";
	int control_map_fd, stackid_hmap_fd, stackmap_fd;
	const char *file = "./test_stacktrace_map.o";
	__u32 key, val, duration = 0;
	int err, prog_fd;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link = NULL;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		return;

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (CHECK(!prog, "find_prog", "prog '%s' not found\n", prog_name))
		goto close_prog;

	link = bpf_program__attach_raw_tracepoint(prog, "sched_switch");
	if (!ASSERT_OK_PTR(link, "attach_raw_tp"))
		goto close_prog;

	/* find map fds */
	control_map_fd = bpf_find_map(__func__, obj, "control_map");
	if (CHECK_FAIL(control_map_fd < 0))
		goto close_prog;

	stackid_hmap_fd = bpf_find_map(__func__, obj, "stackid_hmap");
	if (CHECK_FAIL(stackid_hmap_fd < 0))
		goto close_prog;

	stackmap_fd = bpf_find_map(__func__, obj, "stackmap");
	if (CHECK_FAIL(stackmap_fd < 0))
		goto close_prog;

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
		goto close_prog;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto close_prog;

close_prog:
	bpf_link__destroy(link);
	bpf_object__close(obj);
}
