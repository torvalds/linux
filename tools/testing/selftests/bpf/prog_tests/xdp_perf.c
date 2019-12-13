// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_xdp_perf(void)
{
	const char *file = "./xdp_dummy.o";
	__u32 duration, retval, size;
	struct bpf_object *obj;
	char in[128], out[128];
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_prog_test_run(prog_fd, 1000000, &in[0], 128,
				out, &size, &retval, &duration);

	CHECK(err || retval != XDP_PASS || size != 128,
	      "xdp-perf",
	      "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	bpf_object__close(obj);
}
