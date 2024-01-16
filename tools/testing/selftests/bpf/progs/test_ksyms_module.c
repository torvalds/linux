// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define X_0(x)
#define X_1(x) x X_0(x)
#define X_2(x) x X_1(x)
#define X_3(x) x X_2(x)
#define X_4(x) x X_3(x)
#define X_5(x) x X_4(x)
#define X_6(x) x X_5(x)
#define X_7(x) x X_6(x)
#define X_8(x) x X_7(x)
#define X_9(x) x X_8(x)
#define X_10(x) x X_9(x)
#define REPEAT_256(Y) X_2(X_10(X_10(Y))) X_5(X_10(Y)) X_6(Y)

extern const int bpf_testmod_ksym_percpu __ksym;
extern void bpf_testmod_test_mod_kfunc(int i) __ksym;
extern void bpf_testmod_invalid_mod_kfunc(void) __ksym __weak;

int out_bpf_testmod_ksym = 0;
const volatile int x = 0;

SEC("tc")
int load(struct __sk_buff *skb)
{
	/* This will be kept by clang, but removed by verifier. Since it is
	 * marked as __weak, libbpf and gen_loader don't error out if BTF ID
	 * is not found for it, instead imm and off is set to 0 for it.
	 */
	if (x)
		bpf_testmod_invalid_mod_kfunc();
	bpf_testmod_test_mod_kfunc(42);
	out_bpf_testmod_ksym = *(int *)bpf_this_cpu_ptr(&bpf_testmod_ksym_percpu);
	return 0;
}

SEC("tc")
int load_256(struct __sk_buff *skb)
{
	/* this will fail if kfunc doesn't reuse its own btf fd index */
	REPEAT_256(bpf_testmod_test_mod_kfunc(42););
	bpf_testmod_test_mod_kfunc(42);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
