// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_stacktrace_map_raw_tp(void)
{
	int control_map_fd, stackid_hmap_fd, stackmap_fd;
	const char *file = "./test_stacktrace_map.o";
	int efd, err, prog_fd;
	__u32 key, val, duration = 0;
	struct bpf_object *obj;

	err = bpf_prog_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT, &obj, &prog_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		return;

	efd = bpf_raw_tracepoint_open("sched_switch", prog_fd);
	if (CHECK(efd < 0, "raw_tp_open", "err %d errno %d\n", efd, errno))
		goto close_prog;

	/* find map fds */
	control_map_fd = bpf_find_map(__func__, obj, "control_map");
	if (control_map_fd < 0)
		goto close_prog;

	stackid_hmap_fd = bpf_find_map(__func__, obj, "stackid_hmap");
	if (stackid_hmap_fd < 0)
		goto close_prog;

	stackmap_fd = bpf_find_map(__func__, obj, "stackmap");
	if (stackmap_fd < 0)
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

	goto close_prog_noerr;
close_prog:
	error_cnt++;
close_prog_noerr:
	bpf_object__close(obj);
}
