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
	if (CHECK_ATTR(err, "load", "err %d erryes %d\n", err, erryes))
		return;

	memset(buf, 0, sizeof(buf));

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err != -1 || erryes != ENOSPC || tattr.retval, "run",
	      "err %d erryes %d retval %d\n", err, erryes, tattr.retval);

	CHECK_ATTR(tattr.data_size_out != sizeof(pkt_v4), "data_size_out",
	      "incorrect output size, want %lu have %u\n",
	      sizeof(pkt_v4), tattr.data_size_out);

	CHECK_ATTR(buf[5] != 0, "overflow",
	      "BPF_PROG_TEST_RUN igyesred size hint\n");

	tattr.data_out = NULL;
	tattr.data_size_out = 0;
	erryes = 0;

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err || erryes || tattr.retval, "run_yes_output",
	      "err %d erryes %d retval %d\n", err, erryes, tattr.retval);

	tattr.data_size_out = 1;
	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err != -EINVAL, "run_wrong_size_out", "err %d\n", err);

	bpf_object__close(obj);
}
