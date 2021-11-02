// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void test_xdp_adjust_tail_shrink(void)
{
	const char *file = "./test_xdp_adjust_tail_shrink.o";
	__u32 duration, retval, size, expect_sz;
	struct bpf_object *obj;
	int err, prog_fd;
	char buf[128];

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);

	CHECK(err || retval != XDP_DROP,
	      "ipv4", "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	expect_sz = sizeof(pkt_v6) - 20;  /* Test shrink with 20 bytes */
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v6, sizeof(pkt_v6),
				buf, &size, &retval, &duration);
	CHECK(err || retval != XDP_TX || size != expect_sz,
	      "ipv6", "err %d errno %d retval %d size %d expect-size %d\n",
	      err, errno, retval, size, expect_sz);
	bpf_object__close(obj);
}

static void test_xdp_adjust_tail_grow(void)
{
	const char *file = "./test_xdp_adjust_tail_grow.o";
	struct bpf_object *obj;
	char buf[4096]; /* avoid segfault: large buf to hold grow results */
	__u32 duration, retval, size, expect_sz;
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);
	CHECK(err || retval != XDP_DROP,
	      "ipv4", "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	expect_sz = sizeof(pkt_v6) + 40; /* Test grow with 40 bytes */
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v6, sizeof(pkt_v6) /* 74 */,
				buf, &size, &retval, &duration);
	CHECK(err || retval != XDP_TX || size != expect_sz,
	      "ipv6", "err %d errno %d retval %d size %d expect-size %d\n",
	      err, errno, retval, size, expect_sz);

	bpf_object__close(obj);
}

static void test_xdp_adjust_tail_grow2(void)
{
	const char *file = "./test_xdp_adjust_tail_grow.o";
	char buf[4096]; /* avoid segfault: large buf to hold grow results */
	int tailroom = 320; /* SKB_DATA_ALIGN(sizeof(struct skb_shared_info))*/;
	struct bpf_object *obj;
	int err, cnt, i;
	int max_grow;

	struct bpf_prog_test_run_attr tattr = {
		.repeat		= 1,
		.data_in	= &buf,
		.data_out	= &buf,
		.data_size_in	= 0, /* Per test */
		.data_size_out	= 0, /* Per test */
	};

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &tattr.prog_fd);
	if (CHECK_ATTR(err, "load", "err %d errno %d\n", err, errno))
		return;

	/* Test case-64 */
	memset(buf, 1, sizeof(buf));
	tattr.data_size_in  =  64; /* Determine test case via pkt size */
	tattr.data_size_out = 128; /* Limit copy_size */
	/* Kernel side alloc packet memory area that is zero init */
	err = bpf_prog_test_run_xattr(&tattr);

	CHECK_ATTR(errno != ENOSPC /* Due limit copy_size in bpf_test_finish */
		   || tattr.retval != XDP_TX
		   || tattr.data_size_out != 192, /* Expected grow size */
		   "case-64",
		   "err %d errno %d retval %d size %d\n",
		   err, errno, tattr.retval, tattr.data_size_out);

	/* Extra checks for data contents */
	CHECK_ATTR(tattr.data_size_out != 192
		   || buf[0]   != 1 ||  buf[63]  != 1  /*  0-63  memset to 1 */
		   || buf[64]  != 0 ||  buf[127] != 0  /* 64-127 memset to 0 */
		   || buf[128] != 1 ||  buf[191] != 1, /*128-191 memset to 1 */
		   "case-64-data",
		   "err %d errno %d retval %d size %d\n",
		   err, errno, tattr.retval, tattr.data_size_out);

	/* Test case-128 */
	memset(buf, 2, sizeof(buf));
	tattr.data_size_in  = 128; /* Determine test case via pkt size */
	tattr.data_size_out = sizeof(buf);   /* Copy everything */
	err = bpf_prog_test_run_xattr(&tattr);

	max_grow = 4096 - XDP_PACKET_HEADROOM -	tailroom; /* 3520 */
	CHECK_ATTR(err
		   || tattr.retval != XDP_TX
		   || tattr.data_size_out != max_grow,/* Expect max grow size */
		   "case-128",
		   "err %d errno %d retval %d size %d expect-size %d\n",
		   err, errno, tattr.retval, tattr.data_size_out, max_grow);

	/* Extra checks for data content: Count grow size, will contain zeros */
	for (i = 0, cnt = 0; i < sizeof(buf); i++) {
		if (buf[i] == 0)
			cnt++;
	}
	CHECK_ATTR((cnt != (max_grow - tattr.data_size_in)) /* Grow increase */
		   || tattr.data_size_out != max_grow, /* Total grow size */
		   "case-128-data",
		   "err %d errno %d retval %d size %d grow-size %d\n",
		   err, errno, tattr.retval, tattr.data_size_out, cnt);

	bpf_object__close(obj);
}

void test_xdp_adjust_tail(void)
{
	if (test__start_subtest("xdp_adjust_tail_shrink"))
		test_xdp_adjust_tail_shrink();
	if (test__start_subtest("xdp_adjust_tail_grow"))
		test_xdp_adjust_tail_grow();
	if (test__start_subtest("xdp_adjust_tail_grow2"))
		test_xdp_adjust_tail_grow2();
}
