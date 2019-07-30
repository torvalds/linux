// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_pkt_md_access(void)
{
	const char *file = "./test_pkt_md_access.o";
	struct bpf_object *obj;
	__u32 duration, retval;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (err) {
		error_cnt++;
		return;
	}

	err = bpf_prog_test_run(prog_fd, 10, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	bpf_object__close(obj);
}
