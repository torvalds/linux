// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_prog_run_xattr(void)
{
	const char *file = "./test_pkt_access.o";
	struct bpf_object *obj;
	char buf[10];
	int err;
	struct bpf_prog_test_run_attr tattr = {
		.repeat = 1,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = 5,
	};

	err = bpf_prog_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj,
			    &tattr.prog_fd);
	if (CHECK_ATTR(err, "load", "err %d errno %d\n", err, errno))
		return;

	memset(buf, 0, sizeof(buf));

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err != -1 || errno != ENOSPC || tattr.retval, "run",
	      "err %d errno %d retval %d\n", err, errno, tattr.retval);

	CHECK_ATTR(tattr.data_size_out != sizeof(pkt_v4), "data_size_out",
	      "incorrect output size, want %lu have %u\n",
	      sizeof(pkt_v4), tattr.data_size_out);

	CHECK_ATTR(buf[5] != 0, "overflow",
	      "BPF_PROG_TEST_RUN ignored size hint\n");

	tattr.data_out = NULL;
	tattr.data_size_out = 0;
	errno = 0;

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err || errno || tattr.retval, "run_no_output",
	      "err %d errno %d retval %d\n", err, errno, tattr.retval);

	tattr.data_size_out = 1;
	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err != -EINVAL, "run_wrong_size_out", "err %d\n", err);

	bpf_object__close(obj);
}
