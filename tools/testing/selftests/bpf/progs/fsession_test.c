// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 ChinaTelecom */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u64 test1_entry_result = 0;
__u64 test1_exit_result = 0;

SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test1, int a, int ret)
{
	bool is_exit = bpf_session_is_return(ctx);

	if (!is_exit) {
		test1_entry_result = a == 1 && ret == 0;
		return 0;
	}

	test1_exit_result = a == 1 && ret == 2;
	return 0;
}

__u64 test2_entry_result = 0;
__u64 test2_exit_result = 0;

SEC("fsession/bpf_fentry_test3")
int BPF_PROG(test2, char a, int b, __u64 c, int ret)
{
	bool is_exit = bpf_session_is_return(ctx);

	if (!is_exit) {
		test2_entry_result = a == 4 && b == 5 && c == 6 && ret == 0;
		return 0;
	}

	test2_exit_result = a == 4 && b == 5 && c == 6 && ret == 15;
	return 0;
}

__u64 test3_entry_result = 0;
__u64 test3_exit_result = 0;

SEC("fsession/bpf_fentry_test4")
int BPF_PROG(test3, void *a, char b, int c, __u64 d, int ret)
{
	bool is_exit = bpf_session_is_return(ctx);

	if (!is_exit) {
		test3_entry_result = a == (void *)7 && b == 8 && c == 9 && d == 10 && ret == 0;
		return 0;
	}

	test3_exit_result = a == (void *)7 && b == 8 && c == 9 && d == 10 && ret == 34;
	return 0;
}

__u64 test4_entry_result = 0;
__u64 test4_exit_result = 0;

SEC("fsession/bpf_fentry_test5")
int BPF_PROG(test4, __u64 a, void *b, short c, int d, __u64 e, int ret)
{
	bool is_exit = bpf_session_is_return(ctx);

	if (!is_exit) {
		test4_entry_result = a == 11 && b == (void *)12 && c == 13 && d == 14 &&
			e == 15 && ret == 0;
		return 0;
	}

	test4_exit_result = a == 11 && b == (void *)12 && c == 13 && d == 14 &&
		e == 15 && ret == 65;
	return 0;
}

__u64 test5_entry_result = 0;
__u64 test5_exit_result = 0;

SEC("fsession/bpf_fentry_test7")
int BPF_PROG(test5, struct bpf_fentry_test_t *arg, int ret)
{
	bool is_exit = bpf_session_is_return(ctx);

	if (!is_exit) {
		if (!arg)
			test5_entry_result = ret == 0;
		return 0;
	}

	if (!arg)
		test5_exit_result = 1;
	return 0;
}

__u64 test6_entry_result = 0;
__u64 test6_exit_result = 0;
SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test6, int a)
{
	__u64 addr = bpf_get_func_ip(ctx);

	if (bpf_session_is_return(ctx))
		test6_exit_result = (const void *) addr == &bpf_fentry_test1;
	else
		test6_entry_result = (const void *) addr == &bpf_fentry_test1;
	return 0;
}

__u64 test7_entry_ok = 0;
__u64 test7_exit_ok = 0;
SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test7, int a)
{
	volatile __u64 *cookie = bpf_session_cookie(ctx);

	if (!bpf_session_is_return(ctx)) {
		*cookie = 0xAAAABBBBCCCCDDDDull;
		test7_entry_ok = *cookie == 0xAAAABBBBCCCCDDDDull;
		return 0;
	}

	test7_exit_ok = *cookie == 0xAAAABBBBCCCCDDDDull;
	return 0;
}

__u64 test8_entry_ok = 0;
__u64 test8_exit_ok = 0;

SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test8, int a)
{
	volatile __u64 *cookie = bpf_session_cookie(ctx);

	if (!bpf_session_is_return(ctx)) {
		*cookie = 0x1111222233334444ull;
		test8_entry_ok = *cookie == 0x1111222233334444ull;
		return 0;
	}

	test8_exit_ok = *cookie == 0x1111222233334444ull;
	return 0;
}

__u64 test9_entry_result = 0;
__u64 test9_exit_result = 0;

SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test9, int a, int ret)
{
	__u64 *cookie = bpf_session_cookie(ctx);

	if (!bpf_session_is_return(ctx)) {
		test9_entry_result = a == 1 && ret == 0;
		*cookie = 0x123456ULL;
		return 0;
	}

	test9_exit_result = a == 1 && ret == 2 && *cookie == 0x123456ULL;
	return 0;
}

__u64 test10_result = 0;
SEC("fexit/bpf_fentry_test1")
int BPF_PROG(test10, int a, int ret)
{
	test10_result = a == 1 && ret == 2;
	return 0;
}

__u64 test11_result = 0;
SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test11, int a)
{
	test11_result = a == 1;
	return 0;
}
