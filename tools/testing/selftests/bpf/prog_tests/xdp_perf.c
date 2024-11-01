// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_xdp_perf(void)
{
	const char *file = "./xdp_dummy.bpf.o";
	struct bpf_object *obj;
	char in[128], out[128];
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = in,
		.data_size_in = sizeof(in),
		.data_out = out,
		.data_size_out = sizeof(out),
		.repeat = 1000000,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, XDP_PASS, "test_run retval");
	ASSERT_EQ(topts.data_size_out, 128, "test_run data_size_out");

	bpf_object__close(obj);
}
