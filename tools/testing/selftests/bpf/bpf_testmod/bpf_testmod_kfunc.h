/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BPF_TESTMOD_KFUNC_H
#define _BPF_TESTMOD_KFUNC_H

#ifndef __KERNEL__
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#else
#define __ksym
#endif

extern struct prog_test_ref_kfunc *
bpf_kfunc_call_test_acquire(unsigned long *scalar_ptr) __ksym;
extern void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;
void bpf_kfunc_call_test_ref(struct prog_test_ref_kfunc *p) __ksym;

extern void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;
extern int *bpf_kfunc_call_test_get_rdwr_mem(struct prog_test_ref_kfunc *p, const int rdwr_buf_size) __ksym;
extern int *bpf_kfunc_call_test_get_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
extern int *bpf_kfunc_call_test_acq_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
extern void bpf_kfunc_call_int_mem_release(int *p) __ksym;
extern u32 bpf_kfunc_call_test_static_unused_arg(u32 arg, u32 unused) __ksym;

extern void bpf_testmod_test_mod_kfunc(int i) __ksym;

extern __u64 bpf_kfunc_call_test1(struct sock *sk, __u32 a, __u64 b,
				__u32 c, __u64 d) __ksym;
extern int bpf_kfunc_call_test2(struct sock *sk, __u32 a, __u32 b) __ksym;
extern struct sock *bpf_kfunc_call_test3(struct sock *sk) __ksym;
extern long bpf_kfunc_call_test4(signed char a, short b, int c, long d) __ksym;

extern void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb) __ksym;
extern void bpf_kfunc_call_test_pass1(struct prog_test_pass1 *p) __ksym;
extern void bpf_kfunc_call_test_pass2(struct prog_test_pass2 *p) __ksym;
extern void bpf_kfunc_call_test_mem_len_fail2(__u64 *mem, int len) __ksym;

extern void bpf_kfunc_call_test_destructive(void) __ksym;

#endif /* _BPF_TESTMOD_KFUNC_H */
