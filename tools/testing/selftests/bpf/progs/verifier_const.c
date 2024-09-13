// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Isovalent */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

const volatile long foo = 42;
long bar;
long bart = 96;

SEC("tc/ingress")
__description("rodata: write rejected")
__failure __msg("write into map forbidden")
int tcx1(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, (long *)&foo);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("bss: write accepted")
__success
int tcx2(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, &bar);
	return TCX_PASS;
}

SEC("tc/ingress")
__description("data: write accepted")
__success
int tcx3(struct __sk_buff *skb)
{
	char buff[] = { '8', '4', '\0' };
	bpf_strtol(buff, sizeof(buff), 0, &bart);
	return TCX_PASS;
}

char LICENSE[] SEC("license") = "GPL";
