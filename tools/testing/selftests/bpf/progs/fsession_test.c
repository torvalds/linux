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

