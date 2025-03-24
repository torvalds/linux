// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Isovalent */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

const volatile long foo = 42;
long bar;
long bart = 96;

SEC("tc/ingress")
__description("rodata/strtol: write rejected")
__failure __msg("write into map forbidden")
int tcx1(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, (long *)&foo);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("bss/strtol: write accepted")
__success
int tcx2(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, &bar);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("data/strtol: write accepted")
__success
int tcx3(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, &bart);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("rodata/mtu: write rejected")
__failure __msg("write into map forbidden")
int tcx4(struct __sk_buff *skb)
{
	bpf_check_mtu(skb, skb->ifindex, (__u32 *)&foo, 0, 0);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("bss/mtu: write accepted")
__success
int tcx5(struct __sk_buff *skb)
{
	bpf_check_mtu(skb, skb->ifindex, (__u32 *)&bar, 0, 0);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("data/mtu: write accepted")
__success
int tcx6(struct __sk_buff *skb)
{
	bpf_check_mtu(skb, skb->ifindex, (__u32 *)&bart, 0, 0);
	return TCX_PASS;
}

static inline void write_fixed(volatile void *p, __u32 val)
{
	*(volatile __u32 *)p = val;
}

static inline void write_dyn(void *p, void *val, int len)
{
	bpf_copy_from_user(p, len, val);
}

SEC("tc/ingress")
__description("rodata/mark: write with unknown reg rejected")
__failure __msg("write into map forbidden")
int tcx7(struct __sk_buff *skb)
{
	write_fixed((void *)&foo, skb->mark);
	return TCX_PASS;
}

SEC("lsm.s/bprm_committed_creds")
__description("rodata/mark: write with unknown reg rejected")
__failure __msg("write into map forbidden")
int BPF_PROG(bprm, struct linux_binprm *bprm)
{
	write_dyn((void *)&foo, &bart, bpf_get_prandom_u32() & 3);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
