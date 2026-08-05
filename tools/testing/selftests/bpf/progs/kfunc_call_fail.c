// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

static struct bpf_spin_lock kfunc_call_lock SEC(".data.A");

SEC("?tc")
int kfunc_call_test_spin_lock_unsafe(struct __sk_buff *skb)
{
	bpf_spin_lock(&kfunc_call_lock);
	bpf_kfunc_trigger_ctx_check();
	bpf_spin_unlock(&kfunc_call_lock);

	return 0;
}

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

SEC("?tc")
int kfunc_call_test_get_mem_fail_rdonly(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		p = bpf_kfunc_call_test_get_rdonly_mem(pt, 2 * sizeof(int));
		if (p)
			p[0] = 42; /* this is a read-only buffer, so -EACCES */
		else
			ret = -1;

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("?tc")
int kfunc_call_test_get_mem_fail_use_after_free(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		p = bpf_kfunc_call_test_get_rdwr_mem(pt, 2 * sizeof(int));
		if (p) {
			p[0] = 42;
			ret = p[1]; /* 108 */
		} else {
			ret = -1;
		}

		bpf_kfunc_call_test_release(pt);
	}
	if (p)
		ret = p[0]; /* p is not valid anymore */

	return ret;
}

SEC("?tc")
int kfunc_call_test_get_mem_fail_oob(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		p = bpf_kfunc_call_test_get_rdonly_mem(pt, 2 * sizeof(int));
		if (p)
			ret = p[2 * sizeof(int)]; /* oob access, so -EACCES */
		else
			ret = -1;

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("?tc")
int kfunc_call_test_get_mem_fail_zero_size(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		/*
		 * An explicit rdwr_buf_size of 0 gives R0 a zero-sized buffer,
		 * so any access is out of bounds, hence -EACCES. Previously the
		 * verifier treated a zero size as "no size argument" and sized
		 * R0 after the pointed-to return type, wrongly allowing the read.
		 */
		p = bpf_kfunc_call_test_get_rdwr_mem(pt, 0);
		if (p)
			ret = p[0];
		else
			ret = -1;

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("?tc")
int kfunc_call_test_get_mem_fail_oversized(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		/*
		 * rdwr_buf_size is a const int, so a C literal is narrowed to
		 * 32 bits before the call. Force the full 64-bit value 2^64 - 192
		 * (0xffffffffffffff40, > U32_MAX) into the argument register with
		 * a 64-bit immediate load. The verifier records r0_size from the
		 * full register value and must reject it before that value is
		 * truncated into R0's u32 mem_size.
		 */
		asm volatile (
			"r1 = %[pt];"
			"r2 = %[oversized] ll;"
			"call %[get_rdwr_mem];"
			"%[p] = r0;"
			: [p] "=r"(p)
			: [pt] "r"(pt),
			  [oversized] "i"(0xffffffffffffff40LL),
			  [get_rdwr_mem] "i"(bpf_kfunc_call_test_get_rdwr_mem)
			: "r0", "r1", "r2", "r3", "r4", "r5");
		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

int not_const_size = 2 * sizeof(int);

SEC("?tc")
int kfunc_call_test_get_mem_fail_not_const(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		p = bpf_kfunc_call_test_get_rdonly_mem(pt, not_const_size); /* non const size, -EINVAL */
		if (p)
			ret = p[0];
		else
			ret = -1;

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("?tc")
int kfunc_call_test_mem_acquire_fail(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		/* we are failing on this one, because we are not acquiring a PTR_TO_BTF_ID (a struct ptr) */
		p = bpf_kfunc_call_test_acq_rdonly_mem(pt, 2 * sizeof(int));
		if (p)
			ret = p[0];
		else
			ret = -1;

		bpf_kfunc_call_int_mem_release(p);

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("?tc")
int kfunc_call_test_pointer_arg_type_mismatch(struct __sk_buff *skb)
{
	bpf_kfunc_call_test_pass_ctx((void *)10);
	return 0;
}

char _license[] SEC("license") = "GPL";
