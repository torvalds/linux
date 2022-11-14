// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern struct prog_test_ref_kfunc *bpf_kfunc_call_test_acquire(unsigned long *sp) __ksym;
extern void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;
extern void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;
extern int *bpf_kfunc_call_test_get_rdwr_mem(struct prog_test_ref_kfunc *p, const int rdwr_buf_size) __ksym;
extern int *bpf_kfunc_call_test_get_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
extern int *bpf_kfunc_call_test_acq_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
extern void bpf_kfunc_call_int_mem_release(int *p) __ksym;

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

char _license[] SEC("license") = "GPL";
