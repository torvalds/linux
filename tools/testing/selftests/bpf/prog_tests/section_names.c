// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook
#include <test_progs.h>

static int duration = 0;

struct sec_name_test {
	const char sec_name[32];
	struct {
		int rc;
		enum bpf_prog_type prog_type;
		enum bpf_attach_type expected_attach_type;
	} expected_load;
	struct {
		int rc;
		enum bpf_attach_type attach_type;
	} expected_attach;
};

static struct sec_name_test tests[] = {
	{"InvAliD", {-ESRCH, 0, 0}, {-EINVAL, 0} },
	{"cgroup", {-ESRCH, 0, 0}, {-EINVAL, 0} },
	{"socket", {0, BPF_PROG_TYPE_SOCKET_FILTER, 0}, {-EINVAL, 0} },
	{"kprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"uprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"kretprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"uretprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"classifier", {0, BPF_PROG_TYPE_SCHED_CLS, 0}, {-EINVAL, 0} },
	{"action", {0, BPF_PROG_TYPE_SCHED_ACT, 0}, {-EINVAL, 0} },
	{"tracepoint/", {0, BPF_PROG_TYPE_TRACEPOINT, 0}, {-EINVAL, 0} },
	{"tp/", {0, BPF_PROG_TYPE_TRACEPOINT, 0}, {-EINVAL, 0} },
	{
		"raw_tracepoint/",
		{0, BPF_PROG_TYPE_RAW_TRACEPOINT, 0},
		{-EINVAL, 0},
	},
	{"raw_tp/", {0, BPF_PROG_TYPE_RAW_TRACEPOINT, 0}, {-EINVAL, 0} },
	{"xdp", {0, BPF_PROG_TYPE_XDP, BPF_XDP}, {0, BPF_XDP} },
	{"perf_event", {0, BPF_PROG_TYPE_PERF_EVENT, 0}, {-EINVAL, 0} },
	{"lwt_in", {0, BPF_PROG_TYPE_LWT_IN, 0}, {-EINVAL, 0} },
	{"lwt_out", {0, BPF_PROG_TYPE_LWT_OUT, 0}, {-EINVAL, 0} },
	{"lwt_xmit", {0, BPF_PROG_TYPE_LWT_XMIT, 0}, {-EINVAL, 0} },
	{"lwt_seg6local", {0, BPF_PROG_TYPE_LWT_SEG6LOCAL, 0}, {-EINVAL, 0} },
	{
		"cgroup_skb/ingress",
		{0, BPF_PROG_TYPE_CGROUP_SKB, BPF_CGROUP_INET_INGRESS},
		{0, BPF_CGROUP_INET_INGRESS},
	},
	{
		"cgroup_skb/egress",
		{0, BPF_PROG_TYPE_CGROUP_SKB, BPF_CGROUP_INET_EGRESS},
		{0, BPF_CGROUP_INET_EGRESS},
	},
	{"cgroup/skb", {0, BPF_PROG_TYPE_CGROUP_SKB, 0}, {-EINVAL, 0} },
	{
		"cgroup/sock",
		{0, BPF_PROG_TYPE_CGROUP_SOCK, BPF_CGROUP_INET_SOCK_CREATE},
		{0, BPF_CGROUP_INET_SOCK_CREATE},
	},
	{
		"cgroup/post_bind4",
		{0, BPF_PROG_TYPE_CGROUP_SOCK, BPF_CGROUP_INET4_POST_BIND},
		{0, BPF_CGROUP_INET4_POST_BIND},
	},
	{
		"cgroup/post_bind6",
		{0, BPF_PROG_TYPE_CGROUP_SOCK, BPF_CGROUP_INET6_POST_BIND},
		{0, BPF_CGROUP_INET6_POST_BIND},
	},
	{
		"cgroup/dev",
		{0, BPF_PROG_TYPE_CGROUP_DEVICE, BPF_CGROUP_DEVICE},
		{0, BPF_CGROUP_DEVICE},
	},
	{
		"sockops",
		{0, BPF_PROG_TYPE_SOCK_OPS, BPF_CGROUP_SOCK_OPS},
		{0, BPF_CGROUP_SOCK_OPS},
	},
	{
		"sk_skb/stream_parser",
		{0, BPF_PROG_TYPE_SK_SKB, BPF_SK_SKB_STREAM_PARSER},
		{0, BPF_SK_SKB_STREAM_PARSER},
	},
	{
		"sk_skb/stream_verdict",
		{0, BPF_PROG_TYPE_SK_SKB, BPF_SK_SKB_STREAM_VERDICT},
		{0, BPF_SK_SKB_STREAM_VERDICT},
	},
	{"sk_skb", {0, BPF_PROG_TYPE_SK_SKB, 0}, {-EINVAL, 0} },
	{
		"sk_msg",
		{0, BPF_PROG_TYPE_SK_MSG, BPF_SK_MSG_VERDICT},
		{0, BPF_SK_MSG_VERDICT},
	},
	{
		"lirc_mode2",
		{0, BPF_PROG_TYPE_LIRC_MODE2, BPF_LIRC_MODE2},
		{0, BPF_LIRC_MODE2},
	},
	{
		"flow_dissector",
		{0, BPF_PROG_TYPE_FLOW_DISSECTOR, BPF_FLOW_DISSECTOR},
		{0, BPF_FLOW_DISSECTOR},
	},
	{
		"cgroup/bind4",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_BIND},
		{0, BPF_CGROUP_INET4_BIND},
	},
	{
		"cgroup/bind6",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_BIND},
		{0, BPF_CGROUP_INET6_BIND},
	},
	{
		"cgroup/connect4",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_CONNECT},
		{0, BPF_CGROUP_INET4_CONNECT},
	},
	{
		"cgroup/connect6",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_CONNECT},
		{0, BPF_CGROUP_INET6_CONNECT},
	},
	{
		"cgroup/sendmsg4",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP4_SENDMSG},
		{0, BPF_CGROUP_UDP4_SENDMSG},
	},
	{
		"cgroup/sendmsg6",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP6_SENDMSG},
		{0, BPF_CGROUP_UDP6_SENDMSG},
	},
	{
		"cgroup/recvmsg4",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP4_RECVMSG},
		{0, BPF_CGROUP_UDP4_RECVMSG},
	},
	{
		"cgroup/recvmsg6",
		{0, BPF_PROG_TYPE_CGROUP_SOCK_ADDR, BPF_CGROUP_UDP6_RECVMSG},
		{0, BPF_CGROUP_UDP6_RECVMSG},
	},
	{
		"cgroup/sysctl",
		{0, BPF_PROG_TYPE_CGROUP_SYSCTL, BPF_CGROUP_SYSCTL},
		{0, BPF_CGROUP_SYSCTL},
	},
	{
		"cgroup/getsockopt",
		{0, BPF_PROG_TYPE_CGROUP_SOCKOPT, BPF_CGROUP_GETSOCKOPT},
		{0, BPF_CGROUP_GETSOCKOPT},
	},
	{
		"cgroup/setsockopt",
		{0, BPF_PROG_TYPE_CGROUP_SOCKOPT, BPF_CGROUP_SETSOCKOPT},
		{0, BPF_CGROUP_SETSOCKOPT},
	},
};

static void test_prog_type_by_name(const struct sec_name_test *test)
{
	enum bpf_attach_type expected_attach_type;
	enum bpf_prog_type prog_type;
	int rc;

	rc = libbpf_prog_type_by_name(test->sec_name, &prog_type,
				      &expected_attach_type);

	CHECK(rc != test->expected_load.rc, "check_code",
	      "prog: unexpected rc=%d for %s\n", rc, test->sec_name);

	if (rc)
		return;

	CHECK(prog_type != test->expected_load.prog_type, "check_prog_type",
	      "prog: unexpected prog_type=%d for %s\n",
	      prog_type, test->sec_name);

	CHECK(expected_attach_type != test->expected_load.expected_attach_type,
	      "check_attach_type", "prog: unexpected expected_attach_type=%d for %s\n",
	      expected_attach_type, test->sec_name);
}

static void test_attach_type_by_name(const struct sec_name_test *test)
{
	enum bpf_attach_type attach_type;
	int rc;

	rc = libbpf_attach_type_by_name(test->sec_name, &attach_type);

	CHECK(rc != test->expected_attach.rc, "check_ret",
	      "attach: unexpected rc=%d for %s\n", rc, test->sec_name);

	if (rc)
		return;

	CHECK(attach_type != test->expected_attach.attach_type,
	      "check_attach_type", "attach: unexpected attach_type=%d for %s\n",
	      attach_type, test->sec_name);
}

void test_section_names(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		struct sec_name_test *test = &tests[i];

		test_prog_type_by_name(test);
		test_attach_type_by_name(test);
	}
}
