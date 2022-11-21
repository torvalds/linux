// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_tcp_estats(void)
{
	const char *file = "./test_tcp_estats.bpf.o";
	int err, prog_fd;
	struct bpf_object *obj;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_TRACEPOINT, &obj, &prog_fd);
	if (!ASSERT_OK(err, ""))
		return;

	bpf_object__close(obj);
}
