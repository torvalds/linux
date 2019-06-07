// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <err.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"

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
	{"InvAliD", {-EINVAL, 0, 0}, {-EINVAL, 0} },
	{"cgroup", {-EINVAL, 0, 0}, {-EINVAL, 0} },
	{"socket", {0, BPF_PROG_TYPE_SOCKET_FILTER, 0}, {-EINVAL, 0} },
	{"kprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"kretprobe/", {0, BPF_PROG_TYPE_KPROBE, 0}, {-EINVAL, 0} },
	{"classifier", {0, BPF_PROG_TYPE_SCHED_CLS, 0}, {-EINVAL, 0} },
	{"action", {0, BPF_PROG_TYPE_SCHED_ACT, 0}, {-EINVAL, 0} },
	{"tracepoint/", {0, BPF_PROG_TYPE_TRACEPOINT, 0}, {-EINVAL, 0} },
	{
		"raw_tracepoint/",
		{0, BPF_PROG_TYPE_RAW_TRACEPOINT, 0},
		{-EINVAL, 0},
	},
	{"xdp", {0, BPF_PROG_TYPE_XDP, 0}, {-EINVAL, 0} },
	{"perf_event", {0, BPF_PROG_TYPE_PERF_EVENT, 0}, {-EINVAL, 0} },
	{"lwt_in", {0, BPF_PROG_TYPE_LWT_IN, 0}, {-EINVAL, 0} },
	{"lwt_out", {0, BPF_PROG_TYPE_LWT_OUT, 0}, {-EINVAL, 0} },
	{"lwt_xmit", {0, BPF_PROG_TYPE_LWT_XMIT, 0}, {-EINVAL, 0} },
	{"lwt_seg6local", {0, BPF_PROG_TYPE_LWT_SEG6LOCAL, 0}, {-EINVAL, 0} },
	{
		"cgroup_skb/ingress",
		{0, BPF_PROG_TYPE_CGROUP_SKB, 0},
		{0, BPF_CGROUP_INET_INGRESS},
	},
	{
		"cgroup_skb/egress",
		{0, BPF_PROG_TYPE_CGROUP_SKB, 0},
		{0, BPF_CGROUP_INET_EGRESS},
	},
	{"cgroup/skb", {0, BPF_PROG_TYPE_CGROUP_SKB, 0}, {-EINVAL, 0} },
	{
		"cgroup/sock",
		{0, BPF_PROG_TYPE_CGROUP_SOCK, 0},
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
		{0, BPF_PROG_TYPE_CGROUP_DEVICE, 0},
		{0, BPF_CGROUP_DEVICE},
	},
	{"sockops", {0, BPF_PROG_TYPE_SOCK_OPS, 0}, {0, BPF_CGROUP_SOCK_OPS} },
	{
		"sk_skb/stream_parser",
		{0, BPF_PROG_TYPE_SK_SKB, 0},
		{0, BPF_SK_SKB_STREAM_PARSER},
	},
	{
		"sk_skb/stream_verdict",
		{0, BPF_PROG_TYPE_SK_SKB, 0},
		{0, BPF_SK_SKB_STREAM_VERDICT},
	},
	{"sk_skb", {0, BPF_PROG_TYPE_SK_SKB, 0}, {-EINVAL, 0} },
	{"sk_msg", {0, BPF_PROG_TYPE_SK_MSG, 0}, {0, BPF_SK_MSG_VERDICT} },
	{"lirc_mode2", {0, BPF_PROG_TYPE_LIRC_MODE2, 0}, {0, BPF_LIRC_MODE2} },
	{
		"flow_dissector",
		{0, BPF_PROG_TYPE_FLOW_DISSECTOR, 0},
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
		"cgroup/sysctl",
		{0, BPF_PROG_TYPE_CGROUP_SYSCTL, BPF_CGROUP_SYSCTL},
		{0, BPF_CGROUP_SYSCTL},
	},
};

static int test_prog_type_by_name(const struct sec_name_test *test)
{
	enum bpf_attach_type expected_attach_type;
	enum bpf_prog_type prog_type;
	int rc;

	rc = libbpf_prog_type_by_name(test->sec_name, &prog_type,
				      &expected_attach_type);

	if (rc != test->expected_load.rc) {
		warnx("prog: unexpected rc=%d for %s", rc, test->sec_name);
		return -1;
	}

	if (rc)
		return 0;

	if (prog_type != test->expected_load.prog_type) {
		warnx("prog: unexpected prog_type=%d for %s", prog_type,
		      test->sec_name);
		return -1;
	}

	if (expected_attach_type != test->expected_load.expected_attach_type) {
		warnx("prog: unexpected expected_attach_type=%d for %s",
		      expected_attach_type, test->sec_name);
		return -1;
	}

	return 0;
}

static int test_attach_type_by_name(const struct sec_name_test *test)
{
	enum bpf_attach_type attach_type;
	int rc;

	rc = libbpf_attach_type_by_name(test->sec_name, &attach_type);

	if (rc != test->expected_attach.rc) {
		warnx("attach: unexpected rc=%d for %s", rc, test->sec_name);
		return -1;
	}

	if (rc)
		return 0;

	if (attach_type != test->expected_attach.attach_type) {
		warnx("attach: unexpected attach_type=%d for %s", attach_type,
		      test->sec_name);
		return -1;
	}

	return 0;
}

static int run_test_case(const struct sec_name_test *test)
{
	if (test_prog_type_by_name(test))
		return -1;
	if (test_attach_type_by_name(test))
		return -1;
	return 0;
}

static int run_tests(void)
{
	int passes = 0;
	int fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (run_test_case(&tests[i]))
			++fails;
		else
			++passes;
	}
	printf("Summary: %d PASSED, %d FAILED\n", passes, fails);
	return fails ? -1 : 0;
}

int main(int argc, char **argv)
{
	return run_tests();
}
