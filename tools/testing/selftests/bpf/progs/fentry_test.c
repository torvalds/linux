// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_trace_helpers.h"

char _license[] SEC("license") = "GPL";

__u64 test1_result = 0;
BPF_TRACE_1("fentry/bpf_fentry_test1", test1, int, a)
{
	test1_result = a == 1;
	return 0;
}

__u64 test2_result = 0;
BPF_TRACE_2("fentry/bpf_fentry_test2", test2, int, a, __u64, b)
{
	test2_result = a == 2 && b == 3;
	return 0;
}

__u64 test3_result = 0;
BPF_TRACE_3("fentry/bpf_fentry_test3", test3, char, a, int, b, __u64, c)
{
	test3_result = a == 4 && b == 5 && c == 6;
	return 0;
}

__u64 test4_result = 0;
BPF_TRACE_4("fentry/bpf_fentry_test4", test4,
	    void *, a, char, b, int, c, __u64, d)
{
	test4_result = a == (void *)7 && b == 8 && c == 9 && d == 10;
	return 0;
}

__u64 test5_result = 0;
BPF_TRACE_5("fentry/bpf_fentry_test5", test5,
	    __u64, a, void *, b, short, c, int, d, __u64, e)
{
	test5_result = a == 11 && b == (void *)12 && c == 13 && d == 14 &&
		e == 15;
	return 0;
}

__u64 test6_result = 0;
BPF_TRACE_6("fentry/bpf_fentry_test6", test6,
	    __u64, a, void *, b, short, c, int, d, void *, e, __u64, f)
{
	test6_result = a == 16 && b == (void *)17 && c == 18 && d == 19 &&
		e == (void *)20 && f == 21;
	return 0;
}
