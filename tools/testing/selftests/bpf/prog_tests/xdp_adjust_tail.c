// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_xdp_adjust_tail(void)
{
	const char *file = "./test_adjust_tail.o";
	struct bpf_object *obj;
	char buf[128];
	__u32 duration, retval, size;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (err) {
		error_cnt++;
		return;
	}

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);

	CHECK(err || retval != XDP_DROP,
	      "ipv4", "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v6, sizeof(pkt_v6),
				buf, &size, &retval, &duration);
	CHECK(err || retval != XDP_TX || size != 54,
	      "ipv6", "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);
	bpf_object__close(obj);
}
