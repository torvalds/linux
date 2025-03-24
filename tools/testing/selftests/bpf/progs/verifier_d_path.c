// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/d_path.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("fentry/dentry_open")
__description("d_path accept")
__success __retval(0)
__naked void d_path_accept(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + 0);				\
	r2 = r10;					\
	r2 += -8;					\
	r6 = 0;						\
	*(u64*)(r2 + 0) = r6;				\
	r3 = 8 ll;					\
	call %[bpf_d_path];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_d_path)
	: __clobber_all);
}

SEC("fentry/d_path")
__description("d_path reject")
__failure __msg("helper call is not allowed in probe")
__naked void d_path_reject(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + 0);				\
	r2 = r10;					\
	r2 += -8;					\
	r6 = 0;						\
	*(u64*)(r2 + 0) = r6;				\
	r3 = 8 ll;					\
	call %[bpf_d_path];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_d_path)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
