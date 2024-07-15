/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BPF_TESTMOD_KFUNC_H
#define _BPF_TESTMOD_KFUNC_H

#ifndef __KERNEL__
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#else
#define __ksym
struct prog_test_member1 {
	int a;
};

struct prog_test_member {
	struct prog_test_member1 m;
	int c;
};

struct prog_test_ref_kfunc {
	int a;
	int b;
	struct prog_test_member memb;
	struct prog_test_ref_kfunc *next;
	refcount_t cnt;
};
#endif

struct prog_test_pass1 {
	int x0;
	struct {
		int x1;
		struct {
			int x2;
			struct {
				int x3;
			};
		};
	};
};

struct prog_test_pass2 {
	int len;
	short arr1[4];
	struct {
		char arr2[4];
		unsigned long arr3[8];
	} x;
};

struct prog_test_fail1 {
	void *p;
	int x;
};

struct prog_test_fail2 {
	int x8;
	struct prog_test_pass1 x;
};

struct prog_test_fail3 {
	int len;
	char arr1[2];
	char arr2[];
};

struct prog_test_ref_kfunc *
bpf_kfunc_call_test_acquire(unsigned long *scalar_ptr) __ksym;
void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;
void bpf_kfunc_call_test_ref(struct prog_test_ref_kfunc *p) __ksym;

void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;
int *bpf_kfunc_call_test_get_rdwr_mem(struct prog_test_ref_kfunc *p, const int rdwr_buf_size) __ksym;
int *bpf_kfunc_call_test_get_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
int *bpf_kfunc_call_test_acq_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
void bpf_kfunc_call_int_mem_release(int *p) __ksym;

/* The bpf_kfunc_call_test_static_unused_arg is defined as static,
 * but bpf program compilation needs to see it as global symbol.
 */
#ifndef __KERNEL__
u32 bpf_kfunc_call_test_static_unused_arg(u32 arg, u32 unused) __ksym;
#endif

void bpf_testmod_test_mod_kfunc(int i) __ksym;

__u64 bpf_kfunc_call_test1(struct sock *sk, __u32 a, __u64 b,
				__u32 c, __u64 d) __ksym;
int bpf_kfunc_call_test2(struct sock *sk, __u32 a, __u32 b) __ksym;
struct sock *bpf_kfunc_call_test3(struct sock *sk) __ksym;
long bpf_kfunc_call_test4(signed char a, short b, int c, long d) __ksym;

void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb) __ksym;
void bpf_kfunc_call_test_pass1(struct prog_test_pass1 *p) __ksym;
void bpf_kfunc_call_test_pass2(struct prog_test_pass2 *p) __ksym;
void bpf_kfunc_call_test_mem_len_fail2(__u64 *mem, int len) __ksym;

void bpf_kfunc_call_test_destructive(void) __ksym;

void bpf_kfunc_call_test_offset(struct prog_test_ref_kfunc *p);
struct prog_test_member *bpf_kfunc_call_memb_acquire(void);
void bpf_kfunc_call_memb1_release(struct prog_test_member1 *p);
void bpf_kfunc_call_test_fail1(struct prog_test_fail1 *p);
void bpf_kfunc_call_test_fail2(struct prog_test_fail2 *p);
void bpf_kfunc_call_test_fail3(struct prog_test_fail3 *p);
void bpf_kfunc_call_test_mem_len_fail1(void *mem, int len);

void bpf_kfunc_common_test(void) __ksym;
#endif /* _BPF_TESTMOD_KFUNC_H */
