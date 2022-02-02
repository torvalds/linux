// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void test_xdp_adjust_tail_shrink(void)
{
	const char *file = "./test_xdp_adjust_tail_shrink.o";
	__u32 expect_sz;
	struct bpf_object *obj;
	int err, prog_fd;
	char buf[128];
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = sizeof(buf),
		.repeat = 1,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (ASSERT_OK(err, "test_xdp_adjust_tail_shrink"))
		return;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv4");
	ASSERT_EQ(topts.retval, XDP_DROP, "ipv4 retval");

	expect_sz = sizeof(pkt_v6) - 20;  /* Test shrink with 20 bytes */
	topts.data_in = &pkt_v6;
	topts.data_size_in = sizeof(pkt_v6);
	topts.data_size_out = sizeof(buf);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv6");
	ASSERT_EQ(topts.retval, XDP_TX, "ipv6 retval");
	ASSERT_EQ(topts.data_size_out, expect_sz, "ipv6 size");

	bpf_object__close(obj);
}

static void test_xdp_adjust_tail_grow(void)
{
	const char *file = "./test_xdp_adjust_tail_grow.o";
	struct bpf_object *obj;
	char buf[4096]; /* avoid segfault: large buf to hold grow results */
	__u32 expect_sz;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = sizeof(buf),
		.repeat = 1,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (ASSERT_OK(err, "test_xdp_adjust_tail_grow"))
		return;

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv4");
	ASSERT_EQ(topts.retval, XDP_DROP, "ipv4 retval");

	expect_sz = sizeof(pkt_v6) + 40; /* Test grow with 40 bytes */
	topts.data_in = &pkt_v6;
	topts.data_size_in = sizeof(pkt_v6);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv6");
	ASSERT_EQ(topts.retval, XDP_TX, "ipv6 retval");
	ASSERT_EQ(topts.data_size_out, expect_sz, "ipv6 size");

	bpf_object__close(obj);
}

static void test_xdp_adjust_tail_grow2(void)
{
	const char *file = "./test_xdp_adjust_tail_grow.o";
	char buf[4096]; /* avoid segfault: large buf to hold grow results */
	int tailroom = 320; /* SKB_DATA_ALIGN(sizeof(struct skb_shared_info))*/;
	struct bpf_object *obj;
	int err, cnt, i;
	int max_grow, prog_fd;

	LIBBPF_OPTS(bpf_test_run_opts, tattr,
		.repeat		= 1,
		.data_in	= &buf,
		.data_out	= &buf,
		.data_size_in	= 0, /* Per test */
		.data_size_out	= 0, /* Per test */
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (ASSERT_OK(err, "test_xdp_adjust_tail_grow"))
		return;

	/* Test case-64 */
	memset(buf, 1, sizeof(buf));
	tattr.data_size_in  =  64; /* Determine test case via pkt size */
	tattr.data_size_out = 128; /* Limit copy_size */
	/* Kernel side alloc packet memory area that is zero init */
	err = bpf_prog_test_run_opts(prog_fd, &tattr);

	ASSERT_EQ(errno, ENOSPC, "case-64 errno"); /* Due limit copy_size in bpf_test_finish */
	ASSERT_EQ(tattr.retval, XDP_TX, "case-64 retval");
	ASSERT_EQ(tattr.data_size_out, 192, "case-64 data_size_out"); /* Expected grow size */

	/* Extra checks for data contents */
	ASSERT_EQ(buf[0], 1, "case-64-data buf[0]"); /*  0-63  memset to 1 */
	ASSERT_EQ(buf[63], 1, "case-64-data buf[63]");
	ASSERT_EQ(buf[64], 0, "case-64-data buf[64]"); /* 64-127 memset to 0 */
	ASSERT_EQ(buf[127], 0, "case-64-data buf[127]");
	ASSERT_EQ(buf[128], 1, "case-64-data buf[128]"); /* 128-191 memset to 1 */
	ASSERT_EQ(buf[191], 1, "case-64-data buf[191]");

	/* Test case-128 */
	memset(buf, 2, sizeof(buf));
	tattr.data_size_in  = 128; /* Determine test case via pkt size */
	tattr.data_size_out = sizeof(buf);   /* Copy everything */
	err = bpf_prog_test_run_opts(prog_fd, &tattr);

	max_grow = 4096 - XDP_PACKET_HEADROOM -	tailroom; /* 3520 */
	ASSERT_OK(err, "case-128");
	ASSERT_EQ(tattr.retval, XDP_TX, "case-128 retval");
	ASSERT_EQ(tattr.data_size_out, max_grow, "case-128 data_size_out"); /* Expect max grow */

	/* Extra checks for data content: Count grow size, will contain zeros */
	for (i = 0, cnt = 0; i < sizeof(buf); i++) {
		if (buf[i] == 0)
			cnt++;
	}
	ASSERT_EQ(cnt, max_grow - tattr.data_size_in, "case-128-data cnt"); /* Grow increase */
	ASSERT_EQ(tattr.data_size_out, max_grow, "case-128-data data_size_out"); /* Total grow */

	bpf_object__close(obj);
}

void test_xdp_adjust_frags_tail_shrink(void)
{
	const char *file = "./test_xdp_adjust_tail_shrink.o";
	__u32 exp_size;
	struct bpf_program *prog;
	struct bpf_object *obj;
	int err, prog_fd;
	__u8 *buf;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	/* For the individual test cases, the first byte in the packet
	 * indicates which test will be run.
	 */
	obj = bpf_object__open(file);
	if (libbpf_get_error(obj))
		return;

	prog = bpf_object__next_program(obj, NULL);
	if (bpf_object__load(obj))
		return;

	prog_fd = bpf_program__fd(prog);

	buf = malloc(9000);
	if (!ASSERT_OK_PTR(buf, "alloc buf 9Kb"))
		goto out;

	memset(buf, 0, 9000);

	/* Test case removing 10 bytes from last frag, NOT freeing it */
	exp_size = 8990; /* 9000 - 10 */
	topts.data_in = buf;
	topts.data_out = buf;
	topts.data_size_in = 9000;
	topts.data_size_out = 9000;
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "9Kb-10b");
	ASSERT_EQ(topts.retval, XDP_TX, "9Kb-10b retval");
	ASSERT_EQ(topts.data_size_out, exp_size, "9Kb-10b size");

	/* Test case removing one of two pages, assuming 4K pages */
	buf[0] = 1;
	exp_size = 4900; /* 9000 - 4100 */

	topts.data_size_out = 9000; /* reset from previous invocation */
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "9Kb-4Kb");
	ASSERT_EQ(topts.retval, XDP_TX, "9Kb-4Kb retval");
	ASSERT_EQ(topts.data_size_out, exp_size, "9Kb-4Kb size");

	/* Test case removing two pages resulting in a linear xdp_buff */
	buf[0] = 2;
	exp_size = 800; /* 9000 - 8200 */
	topts.data_size_out = 9000; /* reset from previous invocation */
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "9Kb-9Kb");
	ASSERT_EQ(topts.retval, XDP_TX, "9Kb-9Kb retval");
	ASSERT_EQ(topts.data_size_out, exp_size, "9Kb-9Kb size");

	free(buf);
out:
	bpf_object__close(obj);
}

void test_xdp_adjust_frags_tail_grow(void)
{
	const char *file = "./test_xdp_adjust_tail_grow.o";
	__u32 exp_size;
	struct bpf_program *prog;
	struct bpf_object *obj;
	int err, i, prog_fd;
	__u8 *buf;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	obj = bpf_object__open(file);
	if (libbpf_get_error(obj))
		return;

	prog = bpf_object__next_program(obj, NULL);
	if (bpf_object__load(obj))
		return;

	prog_fd = bpf_program__fd(prog);

	buf = malloc(16384);
	if (!ASSERT_OK_PTR(buf, "alloc buf 16Kb"))
		goto out;

	/* Test case add 10 bytes to last frag */
	memset(buf, 1, 16384);
	exp_size = 9000 + 10;

	topts.data_in = buf;
	topts.data_out = buf;
	topts.data_size_in = 9000;
	topts.data_size_out = 16384;
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "9Kb+10b");
	ASSERT_EQ(topts.retval, XDP_TX, "9Kb+10b retval");
	ASSERT_EQ(topts.data_size_out, exp_size, "9Kb+10b size");

	for (i = 0; i < 9000; i++)
		ASSERT_EQ(buf[i], 1, "9Kb+10b-old");

	for (i = 9000; i < 9010; i++)
		ASSERT_EQ(buf[i], 0, "9Kb+10b-new");

	for (i = 9010; i < 16384; i++)
		ASSERT_EQ(buf[i], 1, "9Kb+10b-untouched");

	/* Test a too large grow */
	memset(buf, 1, 16384);
	exp_size = 9001;

	topts.data_in = topts.data_out = buf;
	topts.data_size_in = 9001;
	topts.data_size_out = 16384;
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "9Kb+10b");
	ASSERT_EQ(topts.retval, XDP_DROP, "9Kb+10b retval");
	ASSERT_EQ(topts.data_size_out, exp_size, "9Kb+10b size");

	free(buf);
out:
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
	if (test__start_subtest("xdp_adjust_frags_tail_shrink"))
		test_xdp_adjust_frags_tail_shrink();
	if (test__start_subtest("xdp_adjust_frags_tail_grow"))
		test_xdp_adjust_frags_tail_grow();
}
