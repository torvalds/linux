// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/var_off.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("lwt_in")
__description("variable-offset ctx access")
__failure __msg("variable ctx access var_off=(0x0; 0x4)")
__naked void variable_offset_ctx_access(void)
{
	asm volatile ("					\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned */		\
	r2 &= 4;					\
	/* add it to skb.  We now have either &skb->len or\
	 * &skb->pkt_type, but we don't know which	\
	 */						\
	r1 += r2;					\
	/* dereference it */				\
	r0 = *(u32*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/skb")
__description("variable-offset stack read, priv vs unpriv")
__success __failure_unpriv
__msg_unpriv("R2 variable stack access prohibited for !root")
__retval(0)
__naked void stack_read_priv_vs_unpriv(void)
{
	asm volatile ("					\
	/* Fill the top 8 bytes of the stack */		\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned */		\
	r2 &= 4;					\
	r2 -= 8;					\
	/* add it to fp.  We now have either fp-4 or fp-8, but\
	 * we don't know which				\
	 */						\
	r2 += r10;					\
	/* dereference it for a stack read */		\
	r0 = *(u32*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("lwt_in")
__description("variable-offset stack read, uninitialized")
__failure __msg("invalid variable-offset read from stack R2")
__naked void variable_offset_stack_read_uninitialized(void)
{
	asm volatile ("					\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned */		\
	r2 &= 4;					\
	r2 -= 8;					\
	/* add it to fp.  We now have either fp-4 or fp-8, but\
	 * we don't know which				\
	 */						\
	r2 += r10;					\
	/* dereference it for a stack read */		\
	r0 = *(u32*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("variable-offset stack write, priv vs unpriv")
__success __failure_unpriv
/* Variable stack access is rejected for unprivileged.
 */
__msg_unpriv("R2 variable stack access prohibited for !root")
__retval(0)
__naked void stack_write_priv_vs_unpriv(void)
{
	asm volatile ("					\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 8-byte aligned */		\
	r2 &= 8;					\
	r2 -= 16;					\
	/* Add it to fp.  We now have either fp-8 or fp-16, but\
	 * we don't know which				\
	 */						\
	r2 += r10;					\
	/* Dereference it for a stack write */		\
	r0 = 0;						\
	*(u64*)(r2 + 0) = r0;				\
	/* Now read from the address we just wrote. This shows\
	 * that, after a variable-offset write, a priviledged\
	 * program can read the slots that were in the range of\
	 * that write (even if the verifier doesn't actually know\
	 * if the slot being read was really written to or not.\
	 */						\
	r3 = *(u64*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("variable-offset stack write clobbers spilled regs")
__failure
/* In the priviledged case, dereferencing a spilled-and-then-filled
 * register is rejected because the previous variable offset stack
 * write might have overwritten the spilled pointer (i.e. we lose track
 * of the spilled register when we analyze the write).
 */
__msg("R2 invalid mem access 'scalar'")
__failure_unpriv
/* The unprivileged case is not too interesting; variable
 * stack access is rejected.
 */
__msg_unpriv("R2 variable stack access prohibited for !root")
__naked void stack_write_clobbers_spilled_regs(void)
{
	asm volatile ("					\
	/* Dummy instruction; needed because we need to patch the next one\
	 * and we can't patch the first instruction.	\
	 */						\
	r6 = 0;						\
	/* Make R0 a map ptr */				\
	r0 = %[map_hash_8b] ll;				\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 8-byte aligned */		\
	r2 &= 8;					\
	r2 -= 16;					\
	/* Add it to fp. We now have either fp-8 or fp-16, but\
	 * we don't know which.				\
	 */						\
	r2 += r10;					\
	/* Spill R0(map ptr) into stack */		\
	*(u64*)(r10 - 8) = r0;				\
	/* Dereference the unknown value for a stack write */\
	r0 = 0;						\
	*(u64*)(r2 + 0) = r0;				\
	/* Fill the register back into R2 */		\
	r2 = *(u64*)(r10 - 8);				\
	/* Try to dereference R2 for a memory load */	\
	r0 = *(u64*)(r2 + 8);				\
	exit;						\
"	:
	: __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("sockops")
__description("indirect variable-offset stack access, unbounded")
__failure __msg("invalid unbounded variable-offset indirect access to stack R4")
__naked void variable_offset_stack_access_unbounded(void)
{
	asm volatile ("					\
	r2 = 6;						\
	r3 = 28;					\
	/* Fill the top 16 bytes of the stack. */	\
	r4 = 0;						\
	*(u64*)(r10 - 16) = r4;				\
	r4 = 0;						\
	*(u64*)(r10 - 8) = r4;				\
	/* Get an unknown value. */			\
	r4 = *(u64*)(r1 + %[bpf_sock_ops_bytes_received]);\
	/* Check the lower bound but don't check the upper one. */\
	if r4 s< 0 goto l0_%=;				\
	/* Point the lower bound to initialized stack. Offset is now in range\
	 * from fp-16 to fp+0x7fffffffffffffef, i.e. max value is unbounded.\
	 */						\
	r4 -= 16;					\
	r4 += r10;					\
	r5 = 8;						\
	/* Dereference it indirectly. */		\
	call %[bpf_getsockopt];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_getsockopt),
	  __imm_const(bpf_sock_ops_bytes_received, offsetof(struct bpf_sock_ops, bytes_received))
	: __clobber_all);
}

SEC("lwt_in")
__description("indirect variable-offset stack access, max out of bound")
__failure __msg("invalid variable-offset indirect access to stack R2")
__naked void access_max_out_of_bound(void)
{
	asm volatile ("					\
	/* Fill the top 8 bytes of the stack */		\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned */		\
	r2 &= 4;					\
	r2 -= 8;					\
	/* add it to fp.  We now have either fp-4 or fp-8, but\
	 * we don't know which				\
	 */						\
	r2 += r10;					\
	/* dereference it indirectly */			\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("lwt_in")
__description("indirect variable-offset stack access, min out of bound")
__failure __msg("invalid variable-offset indirect access to stack R2")
__naked void access_min_out_of_bound(void)
{
	asm volatile ("					\
	/* Fill the top 8 bytes of the stack */		\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned */		\
	r2 &= 4;					\
	r2 -= 516;					\
	/* add it to fp.  We now have either fp-516 or fp-512, but\
	 * we don't know which				\
	 */						\
	r2 += r10;					\
	/* dereference it indirectly */			\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("lwt_in")
__description("indirect variable-offset stack access, min_off < min_initialized")
__failure __msg("invalid indirect read from stack R2 var_off")
__naked void access_min_off_min_initialized(void)
{
	asm volatile ("					\
	/* Fill only the top 8 bytes of the stack. */	\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned. */		\
	r2 &= 4;					\
	r2 -= 16;					\
	/* Add it to fp.  We now have either fp-12 or fp-16, but we don't know\
	 * which. fp-16 size 8 is partially uninitialized stack.\
	 */						\
	r2 += r10;					\
	/* Dereference it indirectly. */		\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("indirect variable-offset stack access, priv vs unpriv")
__success __failure_unpriv
__msg_unpriv("R2 variable stack access prohibited for !root")
__retval(0)
__naked void stack_access_priv_vs_unpriv(void)
{
	asm volatile ("					\
	/* Fill the top 16 bytes of the stack. */	\
	r2 = 0;						\
	*(u64*)(r10 - 16) = r2;				\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	/* Get an unknown value. */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned. */		\
	r2 &= 4;					\
	r2 -= 16;					\
	/* Add it to fp.  We now have either fp-12 or fp-16, we don't know\
	 * which, but either way it points to initialized stack.\
	 */						\
	r2 += r10;					\
	/* Dereference it indirectly. */		\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("lwt_in")
__description("indirect variable-offset stack access, ok")
__success __retval(0)
__naked void variable_offset_stack_access_ok(void)
{
	asm volatile ("					\
	/* Fill the top 16 bytes of the stack. */	\
	r2 = 0;						\
	*(u64*)(r10 - 16) = r2;				\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	/* Get an unknown value. */			\
	r2 = *(u32*)(r1 + 0);				\
	/* Make it small and 4-byte aligned. */		\
	r2 &= 4;					\
	r2 -= 16;					\
	/* Add it to fp.  We now have either fp-12 or fp-16, we don't know\
	 * which, but either way it points to initialized stack.\
	 */						\
	r2 += r10;					\
	/* Dereference it indirectly. */		\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
