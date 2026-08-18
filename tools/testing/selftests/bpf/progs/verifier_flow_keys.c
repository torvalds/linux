// SPDX-License-Identifier: GPL-2.0
/* Bounds checks for PTR_TO_FLOW_KEYS pointer arithmetic. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* sizeof(struct bpf_flow_keys) is well under 4096, so +0x1000 is OOB. */

SEC("flow_dissector")
__description("flow_keys: in-bounds constant pointer arithmetic accepted")
__success
__naked void flow_keys_const_inbounds(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + %[flow_keys]);		\
	r1 += 8;					\
	r0 = *(u64 *)(r1 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(flow_keys, offsetof(struct __sk_buff, flow_keys))
	: __clobber_all);
}

SEC("flow_dissector")
__description("flow_keys: OOB via constant pointer arithmetic rejected")
__failure __msg("invalid access to flow keys off=4096 size=8")
__naked void flow_keys_const_oob_read(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + %[flow_keys]);		\
	r1 += 4096;					\
	r0 = *(u64 *)(r1 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(flow_keys, offsetof(struct __sk_buff, flow_keys))
	: __clobber_all);
}

SEC("flow_dissector")
__description("flow_keys: OOB write via constant pointer arithmetic rejected")
__failure __msg("invalid access to flow keys off=4096 size=8")
__naked void flow_keys_const_oob_write(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + %[flow_keys]);		\
	r1 += 4096;					\
	r2 = 0;						\
	*(u64 *)(r1 + 0) = r2;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(flow_keys, offsetof(struct __sk_buff, flow_keys))
	: __clobber_all);
}

/* Equivalent OOB expressed directly in insn->off; this form was always
 * rejected and is kept to show both forms now share one diagnostic.
 */
SEC("flow_dissector")
__description("flow_keys: OOB via insn->off rejected")
__failure __msg("invalid access to flow keys off=4096 size=8")
__naked void flow_keys_insn_off_oob(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 + %[flow_keys]);		\
	r0 = *(u64 *)(r1 + 4096);			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(flow_keys, offsetof(struct __sk_buff, flow_keys))
	: __clobber_all);
}

SEC("flow_dissector")
__description("flow_keys: variable pointer arithmetic rejected")
__failure __msg("R1 pointer arithmetic on flow_keys prohibited")
__naked void flow_keys_var_read(void)
{
	asm volatile ("					\
	r6 = r1;					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xFFFF;					\
	r1 = *(u64 *)(r6 + %[flow_keys]);		\
	r1 += r0;					\
	r0 = *(u64 *)(r1 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(flow_keys, offsetof(struct __sk_buff, flow_keys)),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
