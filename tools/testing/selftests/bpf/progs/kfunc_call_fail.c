// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;

struct syscall_test_args {
	__u8 data[16];
	size_t size;
};

SEC("?syscall")
int kfunc_syscall_test_fail(struct syscall_test_args *args)
{
	bpf_kfunc_call_test_mem_len_pass1(&args->data, sizeof(*args) + 1);

	return 0;
}

SEC("?syscall")
int kfunc_syscall_test_null_fail(struct syscall_test_args *args)
{
	/* Must be called with args as a NULL pointer
	 * we do not check for it to have the verifier consider that
	 * the pointer might not be null, and so we can load it.
	 *
	 * So the following can not be added:
	 *
	 * if (args)
	 *      return -22;
	 */

	bpf_kfunc_call_test_mem_len_pass1(args, sizeof(*args));

	return 0;
}

char _license[] SEC("license") = "GPL";
