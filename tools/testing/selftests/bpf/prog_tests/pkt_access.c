// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

void test_pkt_access(void)
{
	const char *file = "./test_pkt_access.bpf.o";
	struct bpf_object *obj;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 100000,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv4 test_run_opts err");
	ASSERT_OK(topts.retval, "ipv4 test_run_opts retval");

	topts.data_in = &pkt_v6;
	topts.data_size_in = sizeof(pkt_v6);
	topts.data_size_out = 0; /* reset from last call */
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv6 test_run_opts err");
	ASSERT_OK(topts.retval, "ipv6 test_run_opts retval");

	bpf_object__close(obj);
}
