/*
 * Linux Socket Filter - Kernel level socket filtering
 *
 * Based on the design of the Berkeley Packet Filter. The new
 * internal format has been designed by PLUMgrid:
 *
 *	Copyright (c) 2011 - 2014 PLUMgrid, http://plumgrid.com
 *
 * Authors:
 *
 *	Jay Schulist <jschlst@samba.org>
 *	Alexei Starovoitov <ast@plumgrid.com>
 *	Daniel Borkmann <dborkman@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Andi Kleen - Fix a few bad bugs and races.
 * Kris Katterjohn - Added many additional checks in sk_chk_filter()
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/gfp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <linux/filter.h>
#include <linux/ratelimit.h>
#include <linux/seccomp.h>
#include <linux/if_vlan.h>

/* No hurry in this branch
 *
 * Exported for the bpf jit load helper.
 */
void *bpf_internal_load_pointer_neg_helper(const struct sk_buff *skb, int k, unsigned int size)
{
	u8 *ptr = NULL;

	if (k >= SKF_NET_OFF)
		ptr = skb_network_header(skb) + k - SKF_NET_OFF;
	else if (k >= SKF_LL_OFF)
		ptr = skb_mac_header(skb) + k - SKF_LL_OFF;

	if (ptr >= skb->head && ptr + size <= skb_tail_pointer(skb))
		return ptr;
	return NULL;
}

static inline void *load_pointer(const struct sk_buff *skb, int k,
				 unsigned int size, void *buffer)
{
	if (k >= 0)
		return skb_header_pointer(skb, k, size, buffer);
	return bpf_internal_load_pointer_neg_helper(skb, k, size);
}

/**
 *	sk_filter - run a packet through a socket filter
 *	@sk: sock associated with &sk_buff
 *	@skb: buffer to filter
 *
 * Run the filter code and then cut skb->data to correct size returned by
 * sk_run_filter. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to sk_run_filter. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 */
int sk_filter(struct sock *sk, struct sk_buff *skb)
{
	int err;
	struct sk_filter *filter;

	/*
	 * If the skb was allocated from pfmemalloc reserves, only
	 * allow SOCK_MEMALLOC sockets to use it as this socket is
	 * helping free memory
	 */
	if (skb_pfmemalloc(skb) && !sock_flag(sk, SOCK_MEMALLOC))
		return -ENOMEM;

	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter) {
		unsigned int pkt_len = SK_RUN_FILTER(filter, skb);

		err = pkt_len ? pskb_trim(skb, pkt_len) : -EPERM;
	}
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(sk_filter);

/* Base function for offset calculation. Needs to go into .text section,
 * therefore keeping it non-static as well; will also be used by JITs
 * anyway later on, so do not let the compiler omit it.
 */
noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	return 0;
}

/* Register mappings for user programs. */
#define A_REG		0
#define X_REG		7
#define TMP_REG		8
#define ARG2_REG	2
#define ARG3_REG	3

/**
 *	__sk_run_filter - run a filter on a given context
 *	@ctx: buffer to run the filter on
 *	@insn: filter to apply
 *
 * Decode and apply filter instructions to the skb->data. Return length to
 * keep, 0 for none. @ctx is the data we are operating on, @insn is the
 * array of filter instructions.
 */
unsigned int __sk_run_filter(void *ctx, const struct sock_filter_int *insn)
{
	u64 stack[MAX_BPF_STACK / sizeof(u64)];
	u64 regs[MAX_BPF_REG], tmp;
	void *ptr;
	int off;

#define K  insn->imm
#define A  regs[insn->a_reg]
#define X  regs[insn->x_reg]
#define R0 regs[0]

#define CONT	 ({insn++; goto select_insn; })
#define CONT_JMP ({insn++; goto select_insn; })

	static const void *jumptable[256] = {
		[0 ... 255] = &&default_label,
		/* Now overwrite non-defaults ... */
#define DL(A, B, C)	[A|B|C] = &&A##_##B##_##C
		DL(BPF_ALU, BPF_ADD, BPF_X),
		DL(BPF_ALU, BPF_ADD, BPF_K),
		DL(BPF_ALU, BPF_SUB, BPF_X),
		DL(BPF_ALU, BPF_SUB, BPF_K),
		DL(BPF_ALU, BPF_AND, BPF_X),
		DL(BPF_ALU, BPF_AND, BPF_K),
		DL(BPF_ALU, BPF_OR, BPF_X),
		DL(BPF_ALU, BPF_OR, BPF_K),
		DL(BPF_ALU, BPF_LSH, BPF_X),
		DL(BPF_ALU, BPF_LSH, BPF_K),
		DL(BPF_ALU, BPF_RSH, BPF_X),
		DL(BPF_ALU, BPF_RSH, BPF_K),
		DL(BPF_ALU, BPF_XOR, BPF_X),
		DL(BPF_ALU, BPF_XOR, BPF_K),
		DL(BPF_ALU, BPF_MUL, BPF_X),
		DL(BPF_ALU, BPF_MUL, BPF_K),
		DL(BPF_ALU, BPF_MOV, BPF_X),
		DL(BPF_ALU, BPF_MOV, BPF_K),
		DL(BPF_ALU, BPF_DIV, BPF_X),
		DL(BPF_ALU, BPF_DIV, BPF_K),
		DL(BPF_ALU, BPF_MOD, BPF_X),
		DL(BPF_ALU, BPF_MOD, BPF_K),
		DL(BPF_ALU, BPF_NEG, 0),
		DL(BPF_ALU, BPF_END, BPF_TO_BE),
		DL(BPF_ALU, BPF_END, BPF_TO_LE),
		DL(BPF_ALU64, BPF_ADD, BPF_X),
		DL(BPF_ALU64, BPF_ADD, BPF_K),
		DL(BPF_ALU64, BPF_SUB, BPF_X),
		DL(BPF_ALU64, BPF_SUB, BPF_K),
		DL(BPF_ALU64, BPF_AND, BPF_X),
		DL(BPF_ALU64, BPF_AND, BPF_K),
		DL(BPF_ALU64, BPF_OR, BPF_X),
		DL(BPF_ALU64, BPF_OR, BPF_K),
		DL(BPF_ALU64, BPF_LSH, BPF_X),
		DL(BPF_ALU64, BPF_LSH, BPF_K),
		DL(BPF_ALU64, BPF_RSH, BPF_X),
		DL(BPF_ALU64, BPF_RSH, BPF_K),
		DL(BPF_ALU64, BPF_XOR, BPF_X),
		DL(BPF_ALU64, BPF_XOR, BPF_K),
		DL(BPF_ALU64, BPF_MUL, BPF_X),
		DL(BPF_ALU64, BPF_MUL, BPF_K),
		DL(BPF_ALU64, BPF_MOV, BPF_X),
		DL(BPF_ALU64, BPF_MOV, BPF_K),
		DL(BPF_ALU64, BPF_ARSH, BPF_X),
		DL(BPF_ALU64, BPF_ARSH, BPF_K),
		DL(BPF_ALU64, BPF_DIV, BPF_X),
		DL(BPF_ALU64, BPF_DIV, BPF_K),
		DL(BPF_ALU64, BPF_MOD, BPF_X),
		DL(BPF_ALU64, BPF_MOD, BPF_K),
		DL(BPF_ALU64, BPF_NEG, 0),
		DL(BPF_JMP, BPF_CALL, 0),
		DL(BPF_JMP, BPF_JA, 0),
		DL(BPF_JMP, BPF_JEQ, BPF_X),
		DL(BPF_JMP, BPF_JEQ, BPF_K),
		DL(BPF_JMP, BPF_JNE, BPF_X),
		DL(BPF_JMP, BPF_JNE, BPF_K),
		DL(BPF_JMP, BPF_JGT, BPF_X),
		DL(BPF_JMP, BPF_JGT, BPF_K),
		DL(BPF_JMP, BPF_JGE, BPF_X),
		DL(BPF_JMP, BPF_JGE, BPF_K),
		DL(BPF_JMP, BPF_JSGT, BPF_X),
		DL(BPF_JMP, BPF_JSGT, BPF_K),
		DL(BPF_JMP, BPF_JSGE, BPF_X),
		DL(BPF_JMP, BPF_JSGE, BPF_K),
		DL(BPF_JMP, BPF_JSET, BPF_X),
		DL(BPF_JMP, BPF_JSET, BPF_K),
		DL(BPF_JMP, BPF_EXIT, 0),
		DL(BPF_STX, BPF_MEM, BPF_B),
		DL(BPF_STX, BPF_MEM, BPF_H),
		DL(BPF_STX, BPF_MEM, BPF_W),
		DL(BPF_STX, BPF_MEM, BPF_DW),
		DL(BPF_STX, BPF_XADD, BPF_W),
		DL(BPF_STX, BPF_XADD, BPF_DW),
		DL(BPF_ST, BPF_MEM, BPF_B),
		DL(BPF_ST, BPF_MEM, BPF_H),
		DL(BPF_ST, BPF_MEM, BPF_W),
		DL(BPF_ST, BPF_MEM, BPF_DW),
		DL(BPF_LDX, BPF_MEM, BPF_B),
		DL(BPF_LDX, BPF_MEM, BPF_H),
		DL(BPF_LDX, BPF_MEM, BPF_W),
		DL(BPF_LDX, BPF_MEM, BPF_DW),
		DL(BPF_LD, BPF_ABS, BPF_W),
		DL(BPF_LD, BPF_ABS, BPF_H),
		DL(BPF_LD, BPF_ABS, BPF_B),
		DL(BPF_LD, BPF_IND, BPF_W),
		DL(BPF_LD, BPF_IND, BPF_H),
		DL(BPF_LD, BPF_IND, BPF_B),
#undef DL
	};

	regs[FP_REG]  = (u64) (unsigned long) &stack[ARRAY_SIZE(stack)];
	regs[ARG1_REG] = (u64) (unsigned long) ctx;
	regs[A_REG] = 0;
	regs[X_REG] = 0;

select_insn:
	goto *jumptable[insn->code];

	/* ALU */
#define ALU(OPCODE, OP)			\
	BPF_ALU64_##OPCODE##_BPF_X:	\
		A = A OP X;		\
		CONT;			\
	BPF_ALU_##OPCODE##_BPF_X:	\
		A = (u32) A OP (u32) X;	\
		CONT;			\
	BPF_ALU64_##OPCODE##_BPF_K:	\
		A = A OP K;		\
		CONT;			\
	BPF_ALU_##OPCODE##_BPF_K:	\
		A = (u32) A OP (u32) K;	\
		CONT;

	ALU(BPF_ADD,  +)
	ALU(BPF_SUB,  -)
	ALU(BPF_AND,  &)
	ALU(BPF_OR,   |)
	ALU(BPF_LSH, <<)
	ALU(BPF_RSH, >>)
	ALU(BPF_XOR,  ^)
	ALU(BPF_MUL,  *)
#undef ALU
	BPF_ALU_BPF_NEG_0:
		A = (u32) -A;
		CONT;
	BPF_ALU64_BPF_NEG_0:
		A = -A;
		CONT;
	BPF_ALU_BPF_MOV_BPF_X:
		A = (u32) X;
		CONT;
	BPF_ALU_BPF_MOV_BPF_K:
		A = (u32) K;
		CONT;
	BPF_ALU64_BPF_MOV_BPF_X:
		A = X;
		CONT;
	BPF_ALU64_BPF_MOV_BPF_K:
		A = K;
		CONT;
	BPF_ALU64_BPF_ARSH_BPF_X:
		(*(s64 *) &A) >>= X;
		CONT;
	BPF_ALU64_BPF_ARSH_BPF_K:
		(*(s64 *) &A) >>= K;
		CONT;
	BPF_ALU64_BPF_MOD_BPF_X:
		if (unlikely(X == 0))
			return 0;
		tmp = A;
		A = do_div(tmp, X);
		CONT;
	BPF_ALU_BPF_MOD_BPF_X:
		if (unlikely(X == 0))
			return 0;
		tmp = (u32) A;
		A = do_div(tmp, (u32) X);
		CONT;
	BPF_ALU64_BPF_MOD_BPF_K:
		tmp = A;
		A = do_div(tmp, K);
		CONT;
	BPF_ALU_BPF_MOD_BPF_K:
		tmp = (u32) A;
		A = do_div(tmp, (u32) K);
		CONT;
	BPF_ALU64_BPF_DIV_BPF_X:
		if (unlikely(X == 0))
			return 0;
		do_div(A, X);
		CONT;
	BPF_ALU_BPF_DIV_BPF_X:
		if (unlikely(X == 0))
			return 0;
		tmp = (u32) A;
		do_div(tmp, (u32) X);
		A = (u32) tmp;
		CONT;
	BPF_ALU64_BPF_DIV_BPF_K:
		do_div(A, K);
		CONT;
	BPF_ALU_BPF_DIV_BPF_K:
		tmp = (u32) A;
		do_div(tmp, (u32) K);
		A = (u32) tmp;
		CONT;
	BPF_ALU_BPF_END_BPF_TO_BE:
		switch (K) {
		case 16:
			A = (__force u16) cpu_to_be16(A);
			break;
		case 32:
			A = (__force u32) cpu_to_be32(A);
			break;
		case 64:
			A = (__force u64) cpu_to_be64(A);
			break;
		}
		CONT;
	BPF_ALU_BPF_END_BPF_TO_LE:
		switch (K) {
		case 16:
			A = (__force u16) cpu_to_le16(A);
			break;
		case 32:
			A = (__force u32) cpu_to_le32(A);
			break;
		case 64:
			A = (__force u64) cpu_to_le64(A);
			break;
		}
		CONT;

	/* CALL */
	BPF_JMP_BPF_CALL_0:
		/* Function call scratches R1-R5 registers, preserves R6-R9,
		 * and stores return value into R0.
		 */
		R0 = (__bpf_call_base + insn->imm)(regs[1], regs[2], regs[3],
						   regs[4], regs[5]);
		CONT;

	/* JMP */
	BPF_JMP_BPF_JA_0:
		insn += insn->off;
		CONT;
	BPF_JMP_BPF_JEQ_BPF_X:
		if (A == X) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JEQ_BPF_K:
		if (A == K) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JNE_BPF_X:
		if (A != X) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JNE_BPF_K:
		if (A != K) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JGT_BPF_X:
		if (A > X) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JGT_BPF_K:
		if (A > K) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JGE_BPF_X:
		if (A >= X) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JGE_BPF_K:
		if (A >= K) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSGT_BPF_X:
		if (((s64)A) > ((s64)X)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSGT_BPF_K:
		if (((s64)A) > ((s64)K)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSGE_BPF_X:
		if (((s64)A) >= ((s64)X)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSGE_BPF_K:
		if (((s64)A) >= ((s64)K)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSET_BPF_X:
		if (A & X) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_JSET_BPF_K:
		if (A & K) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	BPF_JMP_BPF_EXIT_0:
		return R0;

	/* STX and ST and LDX*/
#define LDST(SIZEOP, SIZE)					\
	BPF_STX_BPF_MEM_##SIZEOP:				\
		*(SIZE *)(unsigned long) (A + insn->off) = X;	\
		CONT;						\
	BPF_ST_BPF_MEM_##SIZEOP:				\
		*(SIZE *)(unsigned long) (A + insn->off) = K;	\
		CONT;						\
	BPF_LDX_BPF_MEM_##SIZEOP:				\
		A = *(SIZE *)(unsigned long) (X + insn->off);	\
		CONT;

	LDST(BPF_B,   u8)
	LDST(BPF_H,  u16)
	LDST(BPF_W,  u32)
	LDST(BPF_DW, u64)
#undef LDST
	BPF_STX_BPF_XADD_BPF_W: /* lock xadd *(u32 *)(A + insn->off) += X */
		atomic_add((u32) X, (atomic_t *)(unsigned long)
			   (A + insn->off));
		CONT;
	BPF_STX_BPF_XADD_BPF_DW: /* lock xadd *(u64 *)(A + insn->off) += X */
		atomic64_add((u64) X, (atomic64_t *)(unsigned long)
			     (A + insn->off));
		CONT;
	BPF_LD_BPF_ABS_BPF_W: /* R0 = ntohl(*(u32 *) (skb->data + K)) */
		off = K;
load_word:
		/* BPF_LD + BPD_ABS and BPF_LD + BPF_IND insns are only
		 * appearing in the programs where ctx == skb. All programs
		 * keep 'ctx' in regs[CTX_REG] == R6, sk_convert_filter()
		 * saves it in R6, internal BPF verifier will check that
		 * R6 == ctx.
		 *
		 * BPF_ABS and BPF_IND are wrappers of function calls, so
		 * they scratch R1-R5 registers, preserve R6-R9, and store
		 * return value into R0.
		 *
		 * Implicit input:
		 *   ctx
		 *
		 * Explicit input:
		 *   X == any register
		 *   K == 32-bit immediate
		 *
		 * Output:
		 *   R0 - 8/16/32-bit skb data converted to cpu endianness
		 */
		ptr = load_pointer((struct sk_buff *) ctx, off, 4, &tmp);
		if (likely(ptr != NULL)) {
			R0 = get_unaligned_be32(ptr);
			CONT;
		}
		return 0;
	BPF_LD_BPF_ABS_BPF_H: /* R0 = ntohs(*(u16 *) (skb->data + K)) */
		off = K;
load_half:
		ptr = load_pointer((struct sk_buff *) ctx, off, 2, &tmp);
		if (likely(ptr != NULL)) {
			R0 = get_unaligned_be16(ptr);
			CONT;
		}
		return 0;
	BPF_LD_BPF_ABS_BPF_B: /* R0 = *(u8 *) (ctx + K) */
		off = K;
load_byte:
		ptr = load_pointer((struct sk_buff *) ctx, off, 1, &tmp);
		if (likely(ptr != NULL)) {
			R0 = *(u8 *)ptr;
			CONT;
		}
		return 0;
	BPF_LD_BPF_IND_BPF_W: /* R0 = ntohl(*(u32 *) (skb->data + X + K)) */
		off = K + X;
		goto load_word;
	BPF_LD_BPF_IND_BPF_H: /* R0 = ntohs(*(u16 *) (skb->data + X + K)) */
		off = K + X;
		goto load_half;
	BPF_LD_BPF_IND_BPF_B: /* R0 = *(u8 *) (skb->data + X + K) */
		off = K + X;
		goto load_byte;

	default_label:
		/* If we ever reach this, we have a bug somewhere. */
		WARN_RATELIMIT(1, "unknown opcode %02x\n", insn->code);
		return 0;
#undef CONT_JMP
#undef CONT

#undef R0
#undef X
#undef A
#undef K
}

u32 sk_run_filter_int_seccomp(const struct seccomp_data *ctx,
			      const struct sock_filter_int *insni)
    __attribute__ ((alias ("__sk_run_filter")));

u32 sk_run_filter_int_skb(const struct sk_buff *ctx,
			  const struct sock_filter_int *insni)
    __attribute__ ((alias ("__sk_run_filter")));
EXPORT_SYMBOL_GPL(sk_run_filter_int_skb);

/* Helper to find the offset of pkt_type in sk_buff structure. We want
 * to make sure its still a 3bit field starting at a byte boundary;
 * taken from arch/x86/net/bpf_jit_comp.c.
 */
#define PKT_TYPE_MAX	7
static unsigned int pkt_type_offset(void)
{
	struct sk_buff skb_probe = { .pkt_type = ~0, };
	u8 *ct = (u8 *) &skb_probe;
	unsigned int off;

	for (off = 0; off < sizeof(struct sk_buff); off++) {
		if (ct[off] == PKT_TYPE_MAX)
			return off;
	}

	pr_err_once("Please fix %s, as pkt_type couldn't be found!\n", __func__);
	return -1;
}

static u64 __skb_get_pay_offset(u64 ctx, u64 A, u64 X, u64 r4, u64 r5)
{
	struct sk_buff *skb = (struct sk_buff *)(long) ctx;

	return __skb_get_poff(skb);
}

static u64 __skb_get_nlattr(u64 ctx, u64 A, u64 X, u64 r4, u64 r5)
{
	struct sk_buff *skb = (struct sk_buff *)(long) ctx;
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (A > skb->len - sizeof(struct nlattr))
		return 0;

	nla = nla_find((struct nlattr *) &skb->data[A], skb->len - A, X);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

static u64 __skb_get_nlattr_nest(u64 ctx, u64 A, u64 X, u64 r4, u64 r5)
{
	struct sk_buff *skb = (struct sk_buff *)(long) ctx;
	struct nlattr *nla;

	if (skb_is_nonlinear(skb))
		return 0;

	if (skb->len < sizeof(struct nlattr))
		return 0;

	if (A > skb->len - sizeof(struct nlattr))
		return 0;

	nla = (struct nlattr *) &skb->data[A];
	if (nla->nla_len > skb->len - A)
		return 0;

	nla = nla_find_nested(nla, X);
	if (nla)
		return (void *) nla - (void *) skb->data;

	return 0;
}

static u64 __get_raw_cpu_id(u64 ctx, u64 A, u64 X, u64 r4, u64 r5)
{
	return raw_smp_processor_id();
}

static bool convert_bpf_extensions(struct sock_filter *fp,
				   struct sock_filter_int **insnp)
{
	struct sock_filter_int *insn = *insnp;

	switch (fp->k) {
	case SKF_AD_OFF + SKF_AD_PROTOCOL:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, protocol) != 2);

		insn->code = BPF_LDX | BPF_MEM | BPF_H;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, protocol);
		insn++;

		/* A = ntohs(A) [emitting a nop or swap16] */
		insn->code = BPF_ALU | BPF_END | BPF_FROM_BE;
		insn->a_reg = A_REG;
		insn->imm = 16;
		break;

	case SKF_AD_OFF + SKF_AD_PKTTYPE:
		insn->code = BPF_LDX | BPF_MEM | BPF_B;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = pkt_type_offset();
		if (insn->off < 0)
			return false;
		insn++;

		insn->code = BPF_ALU | BPF_AND | BPF_K;
		insn->a_reg = A_REG;
		insn->imm = PKT_TYPE_MAX;
		break;

	case SKF_AD_OFF + SKF_AD_IFINDEX:
	case SKF_AD_OFF + SKF_AD_HATYPE:
		if (FIELD_SIZEOF(struct sk_buff, dev) == 8)
			insn->code = BPF_LDX | BPF_MEM | BPF_DW;
		else
			insn->code = BPF_LDX | BPF_MEM | BPF_W;
		insn->a_reg = TMP_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, dev);
		insn++;

		insn->code = BPF_JMP | BPF_JNE | BPF_K;
		insn->a_reg = TMP_REG;
		insn->imm = 0;
		insn->off = 1;
		insn++;

		insn->code = BPF_JMP | BPF_EXIT;
		insn++;

		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, ifindex) != 4);
		BUILD_BUG_ON(FIELD_SIZEOF(struct net_device, type) != 2);

		insn->a_reg = A_REG;
		insn->x_reg = TMP_REG;

		if (fp->k == SKF_AD_OFF + SKF_AD_IFINDEX) {
			insn->code = BPF_LDX | BPF_MEM | BPF_W;
			insn->off = offsetof(struct net_device, ifindex);
		} else {
			insn->code = BPF_LDX | BPF_MEM | BPF_H;
			insn->off = offsetof(struct net_device, type);
		}
		break;

	case SKF_AD_OFF + SKF_AD_MARK:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);

		insn->code = BPF_LDX | BPF_MEM | BPF_W;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, mark);
		break;

	case SKF_AD_OFF + SKF_AD_RXHASH:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);

		insn->code = BPF_LDX | BPF_MEM | BPF_W;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, hash);
		break;

	case SKF_AD_OFF + SKF_AD_QUEUE:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, queue_mapping) != 2);

		insn->code = BPF_LDX | BPF_MEM | BPF_H;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, queue_mapping);
		break;

	case SKF_AD_OFF + SKF_AD_VLAN_TAG:
	case SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT:
		BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);

		insn->code = BPF_LDX | BPF_MEM | BPF_H;
		insn->a_reg = A_REG;
		insn->x_reg = CTX_REG;
		insn->off = offsetof(struct sk_buff, vlan_tci);
		insn++;

		BUILD_BUG_ON(VLAN_TAG_PRESENT != 0x1000);

		if (fp->k == SKF_AD_OFF + SKF_AD_VLAN_TAG) {
			insn->code = BPF_ALU | BPF_AND | BPF_K;
			insn->a_reg = A_REG;
			insn->imm = ~VLAN_TAG_PRESENT;
		} else {
			insn->code = BPF_ALU | BPF_RSH | BPF_K;
			insn->a_reg = A_REG;
			insn->imm = 12;
			insn++;

			insn->code = BPF_ALU | BPF_AND | BPF_K;
			insn->a_reg = A_REG;
			insn->imm = 1;
		}
		break;

	case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
	case SKF_AD_OFF + SKF_AD_NLATTR:
	case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
	case SKF_AD_OFF + SKF_AD_CPU:
		/* arg1 = ctx */
		insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
		insn->a_reg = ARG1_REG;
		insn->x_reg = CTX_REG;
		insn++;

		/* arg2 = A */
		insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
		insn->a_reg = ARG2_REG;
		insn->x_reg = A_REG;
		insn++;

		/* arg3 = X */
		insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
		insn->a_reg = ARG3_REG;
		insn->x_reg = X_REG;
		insn++;

		/* Emit call(ctx, arg2=A, arg3=X) */
		insn->code = BPF_JMP | BPF_CALL;
		switch (fp->k) {
		case SKF_AD_OFF + SKF_AD_PAY_OFFSET:
			insn->imm = __skb_get_pay_offset - __bpf_call_base;
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR:
			insn->imm = __skb_get_nlattr - __bpf_call_base;
			break;
		case SKF_AD_OFF + SKF_AD_NLATTR_NEST:
			insn->imm = __skb_get_nlattr_nest - __bpf_call_base;
			break;
		case SKF_AD_OFF + SKF_AD_CPU:
			insn->imm = __get_raw_cpu_id - __bpf_call_base;
			break;
		}
		break;

	case SKF_AD_OFF + SKF_AD_ALU_XOR_X:
		insn->code = BPF_ALU | BPF_XOR | BPF_X;
		insn->a_reg = A_REG;
		insn->x_reg = X_REG;
		break;

	default:
		/* This is just a dummy call to avoid letting the compiler
		 * evict __bpf_call_base() as an optimization. Placed here
		 * where no-one bothers.
		 */
		BUG_ON(__bpf_call_base(0, 0, 0, 0, 0) != 0);
		return false;
	}

	*insnp = insn;
	return true;
}

/**
 *	sk_convert_filter - convert filter program
 *	@prog: the user passed filter program
 *	@len: the length of the user passed filter program
 *	@new_prog: buffer where converted program will be stored
 *	@new_len: pointer to store length of converted program
 *
 * Remap 'sock_filter' style BPF instruction set to 'sock_filter_ext' style.
 * Conversion workflow:
 *
 * 1) First pass for calculating the new program length:
 *   sk_convert_filter(old_prog, old_len, NULL, &new_len)
 *
 * 2) 2nd pass to remap in two passes: 1st pass finds new
 *    jump offsets, 2nd pass remapping:
 *   new_prog = kmalloc(sizeof(struct sock_filter_int) * new_len);
 *   sk_convert_filter(old_prog, old_len, new_prog, &new_len);
 *
 * User BPF's register A is mapped to our BPF register 6, user BPF
 * register X is mapped to BPF register 7; frame pointer is always
 * register 10; Context 'void *ctx' is stored in register 1, that is,
 * for socket filters: ctx == 'struct sk_buff *', for seccomp:
 * ctx == 'struct seccomp_data *'.
 */
int sk_convert_filter(struct sock_filter *prog, int len,
		      struct sock_filter_int *new_prog, int *new_len)
{
	int new_flen = 0, pass = 0, target, i;
	struct sock_filter_int *new_insn;
	struct sock_filter *fp;
	int *addrs = NULL;
	u8 bpf_src;

	BUILD_BUG_ON(BPF_MEMWORDS * sizeof(u32) > MAX_BPF_STACK);
	BUILD_BUG_ON(FP_REG + 1 != MAX_BPF_REG);

	if (len <= 0 || len >= BPF_MAXINSNS)
		return -EINVAL;

	if (new_prog) {
		addrs = kzalloc(len * sizeof(*addrs), GFP_KERNEL);
		if (!addrs)
			return -ENOMEM;
	}

do_pass:
	new_insn = new_prog;
	fp = prog;

	if (new_insn) {
		new_insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
		new_insn->a_reg = CTX_REG;
		new_insn->x_reg = ARG1_REG;
	}
	new_insn++;

	for (i = 0; i < len; fp++, i++) {
		struct sock_filter_int tmp_insns[6] = { };
		struct sock_filter_int *insn = tmp_insns;

		if (addrs)
			addrs[i] = new_insn - new_prog;

		switch (fp->code) {
		/* All arithmetic insns and skb loads map as-is. */
		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_ADD | BPF_K:
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_K:
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_AND | BPF_K:
		case BPF_ALU | BPF_OR | BPF_X:
		case BPF_ALU | BPF_OR | BPF_K:
		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_K:
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_K:
		case BPF_ALU | BPF_MUL | BPF_X:
		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
		case BPF_ALU | BPF_NEG:
		case BPF_LD | BPF_ABS | BPF_W:
		case BPF_LD | BPF_ABS | BPF_H:
		case BPF_LD | BPF_ABS | BPF_B:
		case BPF_LD | BPF_IND | BPF_W:
		case BPF_LD | BPF_IND | BPF_H:
		case BPF_LD | BPF_IND | BPF_B:
			/* Check for overloaded BPF extension and
			 * directly convert it if found, otherwise
			 * just move on with mapping.
			 */
			if (BPF_CLASS(fp->code) == BPF_LD &&
			    BPF_MODE(fp->code) == BPF_ABS &&
			    convert_bpf_extensions(fp, &insn))
				break;

			insn->code = fp->code;
			insn->a_reg = A_REG;
			insn->x_reg = X_REG;
			insn->imm = fp->k;
			break;

		/* Jump opcodes map as-is, but offsets need adjustment. */
		case BPF_JMP | BPF_JA:
			target = i + fp->k + 1;
			insn->code = fp->code;
#define EMIT_JMP							\
	do {								\
		if (target >= len || target < 0)			\
			goto err;					\
		insn->off = addrs ? addrs[target] - addrs[i] - 1 : 0;	\
		/* Adjust pc relative offset for 2nd or 3rd insn. */	\
		insn->off -= insn - tmp_insns;				\
	} while (0)

			EMIT_JMP;
			break;

		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
			if (BPF_SRC(fp->code) == BPF_K && (int) fp->k < 0) {
				/* BPF immediates are signed, zero extend
				 * immediate into tmp register and use it
				 * in compare insn.
				 */
				insn->code = BPF_ALU | BPF_MOV | BPF_K;
				insn->a_reg = TMP_REG;
				insn->imm = fp->k;
				insn++;

				insn->a_reg = A_REG;
				insn->x_reg = TMP_REG;
				bpf_src = BPF_X;
			} else {
				insn->a_reg = A_REG;
				insn->x_reg = X_REG;
				insn->imm = fp->k;
				bpf_src = BPF_SRC(fp->code);
			}

			/* Common case where 'jump_false' is next insn. */
			if (fp->jf == 0) {
				insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
				target = i + fp->jt + 1;
				EMIT_JMP;
				break;
			}

			/* Convert JEQ into JNE when 'jump_true' is next insn. */
			if (fp->jt == 0 && BPF_OP(fp->code) == BPF_JEQ) {
				insn->code = BPF_JMP | BPF_JNE | bpf_src;
				target = i + fp->jf + 1;
				EMIT_JMP;
				break;
			}

			/* Other jumps are mapped into two insns: Jxx and JA. */
			target = i + fp->jt + 1;
			insn->code = BPF_JMP | BPF_OP(fp->code) | bpf_src;
			EMIT_JMP;
			insn++;

			insn->code = BPF_JMP | BPF_JA;
			target = i + fp->jf + 1;
			EMIT_JMP;
			break;

		/* ldxb 4 * ([14] & 0xf) is remaped into 6 insns. */
		case BPF_LDX | BPF_MSH | BPF_B:
			insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
			insn->a_reg = TMP_REG;
			insn->x_reg = A_REG;
			insn++;

			insn->code = BPF_LD | BPF_ABS | BPF_B;
			insn->a_reg = A_REG;
			insn->imm = fp->k;
			insn++;

			insn->code = BPF_ALU | BPF_AND | BPF_K;
			insn->a_reg = A_REG;
			insn->imm = 0xf;
			insn++;

			insn->code = BPF_ALU | BPF_LSH | BPF_K;
			insn->a_reg = A_REG;
			insn->imm = 2;
			insn++;

			insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
			insn->a_reg = X_REG;
			insn->x_reg = A_REG;
			insn++;

			insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
			insn->a_reg = A_REG;
			insn->x_reg = TMP_REG;
			break;

		/* RET_K, RET_A are remaped into 2 insns. */
		case BPF_RET | BPF_A:
		case BPF_RET | BPF_K:
			insn->code = BPF_ALU | BPF_MOV |
				     (BPF_RVAL(fp->code) == BPF_K ?
				      BPF_K : BPF_X);
			insn->a_reg = 0;
			insn->x_reg = A_REG;
			insn->imm = fp->k;
			insn++;

			insn->code = BPF_JMP | BPF_EXIT;
			break;

		/* Store to stack. */
		case BPF_ST:
		case BPF_STX:
			insn->code = BPF_STX | BPF_MEM | BPF_W;
			insn->a_reg = FP_REG;
			insn->x_reg = fp->code == BPF_ST ? A_REG : X_REG;
			insn->off = -(BPF_MEMWORDS - fp->k) * 4;
			break;

		/* Load from stack. */
		case BPF_LD | BPF_MEM:
		case BPF_LDX | BPF_MEM:
			insn->code = BPF_LDX | BPF_MEM | BPF_W;
			insn->a_reg = BPF_CLASS(fp->code) == BPF_LD ?
				      A_REG : X_REG;
			insn->x_reg = FP_REG;
			insn->off = -(BPF_MEMWORDS - fp->k) * 4;
			break;

		/* A = K or X = K */
		case BPF_LD | BPF_IMM:
		case BPF_LDX | BPF_IMM:
			insn->code = BPF_ALU | BPF_MOV | BPF_K;
			insn->a_reg = BPF_CLASS(fp->code) == BPF_LD ?
				      A_REG : X_REG;
			insn->imm = fp->k;
			break;

		/* X = A */
		case BPF_MISC | BPF_TAX:
			insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
			insn->a_reg = X_REG;
			insn->x_reg = A_REG;
			break;

		/* A = X */
		case BPF_MISC | BPF_TXA:
			insn->code = BPF_ALU64 | BPF_MOV | BPF_X;
			insn->a_reg = A_REG;
			insn->x_reg = X_REG;
			break;

		/* A = skb->len or X = skb->len */
		case BPF_LD | BPF_W | BPF_LEN:
		case BPF_LDX | BPF_W | BPF_LEN:
			insn->code = BPF_LDX | BPF_MEM | BPF_W;
			insn->a_reg = BPF_CLASS(fp->code) == BPF_LD ?
				      A_REG : X_REG;
			insn->x_reg = CTX_REG;
			insn->off = offsetof(struct sk_buff, len);
			break;

		/* access seccomp_data fields */
		case BPF_LDX | BPF_ABS | BPF_W:
			insn->code = BPF_LDX | BPF_MEM | BPF_W;
			insn->a_reg = A_REG;
			insn->x_reg = CTX_REG;
			insn->off = fp->k;
			break;

		default:
			goto err;
		}

		insn++;
		if (new_prog)
			memcpy(new_insn, tmp_insns,
			       sizeof(*insn) * (insn - tmp_insns));

		new_insn += insn - tmp_insns;
	}

	if (!new_prog) {
		/* Only calculating new length. */
		*new_len = new_insn - new_prog;
		return 0;
	}

	pass++;
	if (new_flen != new_insn - new_prog) {
		new_flen = new_insn - new_prog;
		if (pass > 2)
			goto err;

		goto do_pass;
	}

	kfree(addrs);
	BUG_ON(*new_len != new_flen);
	return 0;
err:
	kfree(addrs);
	return -EINVAL;
}

/* Security:
 *
 * A BPF program is able to use 16 cells of memory to store intermediate
 * values (check u32 mem[BPF_MEMWORDS] in sk_run_filter()).
 *
 * As we dont want to clear mem[] array for each packet going through
 * sk_run_filter(), we check that filter loaded by user never try to read
 * a cell if not previously written, and we check all branches to be sure
 * a malicious user doesn't try to abuse us.
 */
static int check_load_and_stores(struct sock_filter *filter, int flen)
{
	u16 *masks, memvalid = 0; /* one bit per cell, 16 cells */
	int pc, ret = 0;

	BUILD_BUG_ON(BPF_MEMWORDS > 16);
	masks = kmalloc(flen * sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return -ENOMEM;
	memset(masks, 0xff, flen * sizeof(*masks));

	for (pc = 0; pc < flen; pc++) {
		memvalid &= masks[pc];

		switch (filter[pc].code) {
		case BPF_S_ST:
		case BPF_S_STX:
			memvalid |= (1 << filter[pc].k);
			break;
		case BPF_S_LD_MEM:
		case BPF_S_LDX_MEM:
			if (!(memvalid & (1 << filter[pc].k))) {
				ret = -EINVAL;
				goto error;
			}
			break;
		case BPF_S_JMP_JA:
			/* a jump must set masks on target */
			masks[pc + 1 + filter[pc].k] &= memvalid;
			memvalid = ~0;
			break;
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_X:
		case BPF_S_JMP_JSET_K:
			/* a jump must set masks on targets */
			masks[pc + 1 + filter[pc].jt] &= memvalid;
			masks[pc + 1 + filter[pc].jf] &= memvalid;
			memvalid = ~0;
			break;
		}
	}
error:
	kfree(masks);
	return ret;
}

/**
 *	sk_chk_filter - verify socket filter code
 *	@filter: filter to verify
 *	@flen: length of filter
 *
 * Check the user's filter code. If we let some ugly
 * filter code slip through kaboom! The filter must contain
 * no references or jumps that are out of range, no illegal
 * instructions, and must end with a RET instruction.
 *
 * All jumps are forward as they are not signed.
 *
 * Returns 0 if the rule set is legal or -EINVAL if not.
 */
int sk_chk_filter(struct sock_filter *filter, unsigned int flen)
{
	/*
	 * Valid instructions are initialized to non-0.
	 * Invalid instructions are initialized to 0.
	 */
	static const u8 codes[] = {
		[BPF_ALU|BPF_ADD|BPF_K]  = BPF_S_ALU_ADD_K,
		[BPF_ALU|BPF_ADD|BPF_X]  = BPF_S_ALU_ADD_X,
		[BPF_ALU|BPF_SUB|BPF_K]  = BPF_S_ALU_SUB_K,
		[BPF_ALU|BPF_SUB|BPF_X]  = BPF_S_ALU_SUB_X,
		[BPF_ALU|BPF_MUL|BPF_K]  = BPF_S_ALU_MUL_K,
		[BPF_ALU|BPF_MUL|BPF_X]  = BPF_S_ALU_MUL_X,
		[BPF_ALU|BPF_DIV|BPF_X]  = BPF_S_ALU_DIV_X,
		[BPF_ALU|BPF_MOD|BPF_K]  = BPF_S_ALU_MOD_K,
		[BPF_ALU|BPF_MOD|BPF_X]  = BPF_S_ALU_MOD_X,
		[BPF_ALU|BPF_AND|BPF_K]  = BPF_S_ALU_AND_K,
		[BPF_ALU|BPF_AND|BPF_X]  = BPF_S_ALU_AND_X,
		[BPF_ALU|BPF_OR|BPF_K]   = BPF_S_ALU_OR_K,
		[BPF_ALU|BPF_OR|BPF_X]   = BPF_S_ALU_OR_X,
		[BPF_ALU|BPF_XOR|BPF_K]  = BPF_S_ALU_XOR_K,
		[BPF_ALU|BPF_XOR|BPF_X]  = BPF_S_ALU_XOR_X,
		[BPF_ALU|BPF_LSH|BPF_K]  = BPF_S_ALU_LSH_K,
		[BPF_ALU|BPF_LSH|BPF_X]  = BPF_S_ALU_LSH_X,
		[BPF_ALU|BPF_RSH|BPF_K]  = BPF_S_ALU_RSH_K,
		[BPF_ALU|BPF_RSH|BPF_X]  = BPF_S_ALU_RSH_X,
		[BPF_ALU|BPF_NEG]        = BPF_S_ALU_NEG,
		[BPF_LD|BPF_W|BPF_ABS]   = BPF_S_LD_W_ABS,
		[BPF_LD|BPF_H|BPF_ABS]   = BPF_S_LD_H_ABS,
		[BPF_LD|BPF_B|BPF_ABS]   = BPF_S_LD_B_ABS,
		[BPF_LD|BPF_W|BPF_LEN]   = BPF_S_LD_W_LEN,
		[BPF_LD|BPF_W|BPF_IND]   = BPF_S_LD_W_IND,
		[BPF_LD|BPF_H|BPF_IND]   = BPF_S_LD_H_IND,
		[BPF_LD|BPF_B|BPF_IND]   = BPF_S_LD_B_IND,
		[BPF_LD|BPF_IMM]         = BPF_S_LD_IMM,
		[BPF_LDX|BPF_W|BPF_LEN]  = BPF_S_LDX_W_LEN,
		[BPF_LDX|BPF_B|BPF_MSH]  = BPF_S_LDX_B_MSH,
		[BPF_LDX|BPF_IMM]        = BPF_S_LDX_IMM,
		[BPF_MISC|BPF_TAX]       = BPF_S_MISC_TAX,
		[BPF_MISC|BPF_TXA]       = BPF_S_MISC_TXA,
		[BPF_RET|BPF_K]          = BPF_S_RET_K,
		[BPF_RET|BPF_A]          = BPF_S_RET_A,
		[BPF_ALU|BPF_DIV|BPF_K]  = BPF_S_ALU_DIV_K,
		[BPF_LD|BPF_MEM]         = BPF_S_LD_MEM,
		[BPF_LDX|BPF_MEM]        = BPF_S_LDX_MEM,
		[BPF_ST]                 = BPF_S_ST,
		[BPF_STX]                = BPF_S_STX,
		[BPF_JMP|BPF_JA]         = BPF_S_JMP_JA,
		[BPF_JMP|BPF_JEQ|BPF_K]  = BPF_S_JMP_JEQ_K,
		[BPF_JMP|BPF_JEQ|BPF_X]  = BPF_S_JMP_JEQ_X,
		[BPF_JMP|BPF_JGE|BPF_K]  = BPF_S_JMP_JGE_K,
		[BPF_JMP|BPF_JGE|BPF_X]  = BPF_S_JMP_JGE_X,
		[BPF_JMP|BPF_JGT|BPF_K]  = BPF_S_JMP_JGT_K,
		[BPF_JMP|BPF_JGT|BPF_X]  = BPF_S_JMP_JGT_X,
		[BPF_JMP|BPF_JSET|BPF_K] = BPF_S_JMP_JSET_K,
		[BPF_JMP|BPF_JSET|BPF_X] = BPF_S_JMP_JSET_X,
	};
	int pc;
	bool anc_found;

	if (flen == 0 || flen > BPF_MAXINSNS)
		return -EINVAL;

	/* check the filter code now */
	for (pc = 0; pc < flen; pc++) {
		struct sock_filter *ftest = &filter[pc];
		u16 code = ftest->code;

		if (code >= ARRAY_SIZE(codes))
			return -EINVAL;
		code = codes[code];
		if (!code)
			return -EINVAL;
		/* Some instructions need special checks */
		switch (code) {
		case BPF_S_ALU_DIV_K:
		case BPF_S_ALU_MOD_K:
			/* check for division by zero */
			if (ftest->k == 0)
				return -EINVAL;
			break;
		case BPF_S_LD_MEM:
		case BPF_S_LDX_MEM:
		case BPF_S_ST:
		case BPF_S_STX:
			/* check for invalid memory addresses */
			if (ftest->k >= BPF_MEMWORDS)
				return -EINVAL;
			break;
		case BPF_S_JMP_JA:
			/*
			 * Note, the large ftest->k might cause loops.
			 * Compare this with conditional jumps below,
			 * where offsets are limited. --ANK (981016)
			 */
			if (ftest->k >= (unsigned int)(flen-pc-1))
				return -EINVAL;
			break;
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_X:
		case BPF_S_JMP_JSET_K:
			/* for conditionals both must be safe */
			if (pc + ftest->jt + 1 >= flen ||
			    pc + ftest->jf + 1 >= flen)
				return -EINVAL;
			break;
		case BPF_S_LD_W_ABS:
		case BPF_S_LD_H_ABS:
		case BPF_S_LD_B_ABS:
			anc_found = false;
#define ANCILLARY(CODE) case SKF_AD_OFF + SKF_AD_##CODE:	\
				code = BPF_S_ANC_##CODE;	\
				anc_found = true;		\
				break
			switch (ftest->k) {
			ANCILLARY(PROTOCOL);
			ANCILLARY(PKTTYPE);
			ANCILLARY(IFINDEX);
			ANCILLARY(NLATTR);
			ANCILLARY(NLATTR_NEST);
			ANCILLARY(MARK);
			ANCILLARY(QUEUE);
			ANCILLARY(HATYPE);
			ANCILLARY(RXHASH);
			ANCILLARY(CPU);
			ANCILLARY(ALU_XOR_X);
			ANCILLARY(VLAN_TAG);
			ANCILLARY(VLAN_TAG_PRESENT);
			ANCILLARY(PAY_OFFSET);
			}

			/* ancillary operation unknown or unsupported */
			if (anc_found == false && ftest->k >= SKF_AD_OFF)
				return -EINVAL;
		}
		ftest->code = code;
	}

	/* last instruction must be a RET code */
	switch (filter[flen - 1].code) {
	case BPF_S_RET_K:
	case BPF_S_RET_A:
		return check_load_and_stores(filter, flen);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(sk_chk_filter);

static int sk_store_orig_filter(struct sk_filter *fp,
				const struct sock_fprog *fprog)
{
	unsigned int fsize = sk_filter_proglen(fprog);
	struct sock_fprog_kern *fkprog;

	fp->orig_prog = kmalloc(sizeof(*fkprog), GFP_KERNEL);
	if (!fp->orig_prog)
		return -ENOMEM;

	fkprog = fp->orig_prog;
	fkprog->len = fprog->len;
	fkprog->filter = kmemdup(fp->insns, fsize, GFP_KERNEL);
	if (!fkprog->filter) {
		kfree(fp->orig_prog);
		return -ENOMEM;
	}

	return 0;
}

static void sk_release_orig_filter(struct sk_filter *fp)
{
	struct sock_fprog_kern *fprog = fp->orig_prog;

	if (fprog) {
		kfree(fprog->filter);
		kfree(fprog);
	}
}

/**
 * 	sk_filter_release_rcu - Release a socket filter by rcu_head
 *	@rcu: rcu_head that contains the sk_filter to free
 */
static void sk_filter_release_rcu(struct rcu_head *rcu)
{
	struct sk_filter *fp = container_of(rcu, struct sk_filter, rcu);

	sk_release_orig_filter(fp);
	bpf_jit_free(fp);
}

/**
 *	sk_filter_release - release a socket filter
 *	@fp: filter to remove
 *
 *	Remove a filter from a socket and release its resources.
 */
static void sk_filter_release(struct sk_filter *fp)
{
	if (atomic_dec_and_test(&fp->refcnt))
		call_rcu(&fp->rcu, sk_filter_release_rcu);
}

void sk_filter_uncharge(struct sock *sk, struct sk_filter *fp)
{
	atomic_sub(sk_filter_size(fp->len), &sk->sk_omem_alloc);
	sk_filter_release(fp);
}

void sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	atomic_inc(&fp->refcnt);
	atomic_add(sk_filter_size(fp->len), &sk->sk_omem_alloc);
}

static struct sk_filter *__sk_migrate_realloc(struct sk_filter *fp,
					      struct sock *sk,
					      unsigned int len)
{
	struct sk_filter *fp_new;

	if (sk == NULL)
		return krealloc(fp, len, GFP_KERNEL);

	fp_new = sock_kmalloc(sk, len, GFP_KERNEL);
	if (fp_new) {
		memcpy(fp_new, fp, sizeof(struct sk_filter));
		/* As we're kepping orig_prog in fp_new along,
		 * we need to make sure we're not evicting it
		 * from the old fp.
		 */
		fp->orig_prog = NULL;
		sk_filter_uncharge(sk, fp);
	}

	return fp_new;
}

static struct sk_filter *__sk_migrate_filter(struct sk_filter *fp,
					     struct sock *sk)
{
	struct sock_filter *old_prog;
	struct sk_filter *old_fp;
	int i, err, new_len, old_len = fp->len;

	/* We are free to overwrite insns et al right here as it
	 * won't be used at this point in time anymore internally
	 * after the migration to the internal BPF instruction
	 * representation.
	 */
	BUILD_BUG_ON(sizeof(struct sock_filter) !=
		     sizeof(struct sock_filter_int));

	/* For now, we need to unfiddle BPF_S_* identifiers in place.
	 * This can sooner or later on be subject to removal, e.g. when
	 * JITs have been converted.
	 */
	for (i = 0; i < fp->len; i++)
		sk_decode_filter(&fp->insns[i], &fp->insns[i]);

	/* Conversion cannot happen on overlapping memory areas,
	 * so we need to keep the user BPF around until the 2nd
	 * pass. At this time, the user BPF is stored in fp->insns.
	 */
	old_prog = kmemdup(fp->insns, old_len * sizeof(struct sock_filter),
			   GFP_KERNEL);
	if (!old_prog) {
		err = -ENOMEM;
		goto out_err;
	}

	/* 1st pass: calculate the new program length. */
	err = sk_convert_filter(old_prog, old_len, NULL, &new_len);
	if (err)
		goto out_err_free;

	/* Expand fp for appending the new filter representation. */
	old_fp = fp;
	fp = __sk_migrate_realloc(old_fp, sk, sk_filter_size(new_len));
	if (!fp) {
		/* The old_fp is still around in case we couldn't
		 * allocate new memory, so uncharge on that one.
		 */
		fp = old_fp;
		err = -ENOMEM;
		goto out_err_free;
	}

	fp->bpf_func = sk_run_filter_int_skb;
	fp->len = new_len;

	/* 2nd pass: remap sock_filter insns into sock_filter_int insns. */
	err = sk_convert_filter(old_prog, old_len, fp->insnsi, &new_len);
	if (err)
		/* 2nd sk_convert_filter() can fail only if it fails
		 * to allocate memory, remapping must succeed. Note,
		 * that at this time old_fp has already been released
		 * by __sk_migrate_realloc().
		 */
		goto out_err_free;

	kfree(old_prog);
	return fp;

out_err_free:
	kfree(old_prog);
out_err:
	/* Rollback filter setup. */
	if (sk != NULL)
		sk_filter_uncharge(sk, fp);
	else
		kfree(fp);
	return ERR_PTR(err);
}

static struct sk_filter *__sk_prepare_filter(struct sk_filter *fp,
					     struct sock *sk)
{
	int err;

	fp->bpf_func = NULL;
	fp->jited = 0;

	err = sk_chk_filter(fp->insns, fp->len);
	if (err)
		return ERR_PTR(err);

	/* Probe if we can JIT compile the filter and if so, do
	 * the compilation of the filter.
	 */
	bpf_jit_compile(fp);

	/* JIT compiler couldn't process this filter, so do the
	 * internal BPF translation for the optimized interpreter.
	 */
	if (!fp->jited)
		fp = __sk_migrate_filter(fp, sk);

	return fp;
}

/**
 *	sk_unattached_filter_create - create an unattached filter
 *	@fprog: the filter program
 *	@pfp: the unattached filter that is created
 *
 * Create a filter independent of any socket. We first run some
 * sanity checks on it to make sure it does not explode on us later.
 * If an error occurs or there is insufficient memory for the filter
 * a negative errno code is returned. On success the return is zero.
 */
int sk_unattached_filter_create(struct sk_filter **pfp,
				struct sock_fprog *fprog)
{
	unsigned int fsize = sk_filter_proglen(fprog);
	struct sk_filter *fp;

	/* Make sure new filter is there and in the right amounts. */
	if (fprog->filter == NULL)
		return -EINVAL;

	fp = kmalloc(sk_filter_size(fprog->len), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	memcpy(fp->insns, fprog->filter, fsize);

	atomic_set(&fp->refcnt, 1);
	fp->len = fprog->len;
	/* Since unattached filters are not copied back to user
	 * space through sk_get_filter(), we do not need to hold
	 * a copy here, and can spare us the work.
	 */
	fp->orig_prog = NULL;

	/* __sk_prepare_filter() already takes care of uncharging
	 * memory in case something goes wrong.
	 */
	fp = __sk_prepare_filter(fp, NULL);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	*pfp = fp;
	return 0;
}
EXPORT_SYMBOL_GPL(sk_unattached_filter_create);

void sk_unattached_filter_destroy(struct sk_filter *fp)
{
	sk_filter_release(fp);
}
EXPORT_SYMBOL_GPL(sk_unattached_filter_destroy);

/**
 *	sk_attach_filter - attach a socket filter
 *	@fprog: the filter program
 *	@sk: the socket to use
 *
 * Attach the user's filter code. We first run some sanity checks on
 * it to make sure it does not explode on us later. If an error
 * occurs or there is insufficient memory for the filter a negative
 * errno code is returned. On success the return is zero.
 */
int sk_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct sk_filter *fp, *old_fp;
	unsigned int fsize = sk_filter_proglen(fprog);
	unsigned int sk_fsize = sk_filter_size(fprog->len);
	int err;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	/* Make sure new filter is there and in the right amounts. */
	if (fprog->filter == NULL)
		return -EINVAL;

	fp = sock_kmalloc(sk, sk_fsize, GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	if (copy_from_user(fp->insns, fprog->filter, fsize)) {
		sock_kfree_s(sk, fp, sk_fsize);
		return -EFAULT;
	}

	atomic_set(&fp->refcnt, 1);
	fp->len = fprog->len;

	err = sk_store_orig_filter(fp, fprog);
	if (err) {
		sk_filter_uncharge(sk, fp);
		return -ENOMEM;
	}

	/* __sk_prepare_filter() already takes care of uncharging
	 * memory in case something goes wrong.
	 */
	fp = __sk_prepare_filter(fp, sk);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	old_fp = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	rcu_assign_pointer(sk->sk_filter, fp);

	if (old_fp)
		sk_filter_uncharge(sk, old_fp);

	return 0;
}
EXPORT_SYMBOL_GPL(sk_attach_filter);

int sk_detach_filter(struct sock *sk)
{
	int ret = -ENOENT;
	struct sk_filter *filter;

	if (sock_flag(sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	filter = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	if (filter) {
		RCU_INIT_POINTER(sk->sk_filter, NULL);
		sk_filter_uncharge(sk, filter);
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sk_detach_filter);

void sk_decode_filter(struct sock_filter *filt, struct sock_filter *to)
{
	static const u16 decodes[] = {
		[BPF_S_ALU_ADD_K]	= BPF_ALU|BPF_ADD|BPF_K,
		[BPF_S_ALU_ADD_X]	= BPF_ALU|BPF_ADD|BPF_X,
		[BPF_S_ALU_SUB_K]	= BPF_ALU|BPF_SUB|BPF_K,
		[BPF_S_ALU_SUB_X]	= BPF_ALU|BPF_SUB|BPF_X,
		[BPF_S_ALU_MUL_K]	= BPF_ALU|BPF_MUL|BPF_K,
		[BPF_S_ALU_MUL_X]	= BPF_ALU|BPF_MUL|BPF_X,
		[BPF_S_ALU_DIV_X]	= BPF_ALU|BPF_DIV|BPF_X,
		[BPF_S_ALU_MOD_K]	= BPF_ALU|BPF_MOD|BPF_K,
		[BPF_S_ALU_MOD_X]	= BPF_ALU|BPF_MOD|BPF_X,
		[BPF_S_ALU_AND_K]	= BPF_ALU|BPF_AND|BPF_K,
		[BPF_S_ALU_AND_X]	= BPF_ALU|BPF_AND|BPF_X,
		[BPF_S_ALU_OR_K]	= BPF_ALU|BPF_OR|BPF_K,
		[BPF_S_ALU_OR_X]	= BPF_ALU|BPF_OR|BPF_X,
		[BPF_S_ALU_XOR_K]	= BPF_ALU|BPF_XOR|BPF_K,
		[BPF_S_ALU_XOR_X]	= BPF_ALU|BPF_XOR|BPF_X,
		[BPF_S_ALU_LSH_K]	= BPF_ALU|BPF_LSH|BPF_K,
		[BPF_S_ALU_LSH_X]	= BPF_ALU|BPF_LSH|BPF_X,
		[BPF_S_ALU_RSH_K]	= BPF_ALU|BPF_RSH|BPF_K,
		[BPF_S_ALU_RSH_X]	= BPF_ALU|BPF_RSH|BPF_X,
		[BPF_S_ALU_NEG]		= BPF_ALU|BPF_NEG,
		[BPF_S_LD_W_ABS]	= BPF_LD|BPF_W|BPF_ABS,
		[BPF_S_LD_H_ABS]	= BPF_LD|BPF_H|BPF_ABS,
		[BPF_S_LD_B_ABS]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PROTOCOL]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PKTTYPE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_IFINDEX]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_NLATTR]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_NLATTR_NEST]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_MARK]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_QUEUE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_HATYPE]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_RXHASH]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_CPU]		= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_ALU_XOR_X]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_VLAN_TAG]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_VLAN_TAG_PRESENT] = BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_ANC_PAY_OFFSET]	= BPF_LD|BPF_B|BPF_ABS,
		[BPF_S_LD_W_LEN]	= BPF_LD|BPF_W|BPF_LEN,
		[BPF_S_LD_W_IND]	= BPF_LD|BPF_W|BPF_IND,
		[BPF_S_LD_H_IND]	= BPF_LD|BPF_H|BPF_IND,
		[BPF_S_LD_B_IND]	= BPF_LD|BPF_B|BPF_IND,
		[BPF_S_LD_IMM]		= BPF_LD|BPF_IMM,
		[BPF_S_LDX_W_LEN]	= BPF_LDX|BPF_W|BPF_LEN,
		[BPF_S_LDX_B_MSH]	= BPF_LDX|BPF_B|BPF_MSH,
		[BPF_S_LDX_IMM]		= BPF_LDX|BPF_IMM,
		[BPF_S_MISC_TAX]	= BPF_MISC|BPF_TAX,
		[BPF_S_MISC_TXA]	= BPF_MISC|BPF_TXA,
		[BPF_S_RET_K]		= BPF_RET|BPF_K,
		[BPF_S_RET_A]		= BPF_RET|BPF_A,
		[BPF_S_ALU_DIV_K]	= BPF_ALU|BPF_DIV|BPF_K,
		[BPF_S_LD_MEM]		= BPF_LD|BPF_MEM,
		[BPF_S_LDX_MEM]		= BPF_LDX|BPF_MEM,
		[BPF_S_ST]		= BPF_ST,
		[BPF_S_STX]		= BPF_STX,
		[BPF_S_JMP_JA]		= BPF_JMP|BPF_JA,
		[BPF_S_JMP_JEQ_K]	= BPF_JMP|BPF_JEQ|BPF_K,
		[BPF_S_JMP_JEQ_X]	= BPF_JMP|BPF_JEQ|BPF_X,
		[BPF_S_JMP_JGE_K]	= BPF_JMP|BPF_JGE|BPF_K,
		[BPF_S_JMP_JGE_X]	= BPF_JMP|BPF_JGE|BPF_X,
		[BPF_S_JMP_JGT_K]	= BPF_JMP|BPF_JGT|BPF_K,
		[BPF_S_JMP_JGT_X]	= BPF_JMP|BPF_JGT|BPF_X,
		[BPF_S_JMP_JSET_K]	= BPF_JMP|BPF_JSET|BPF_K,
		[BPF_S_JMP_JSET_X]	= BPF_JMP|BPF_JSET|BPF_X,
	};
	u16 code;

	code = filt->code;

	to->code = decodes[code];
	to->jt = filt->jt;
	to->jf = filt->jf;
	to->k = filt->k;
}

int sk_get_filter(struct sock *sk, struct sock_filter __user *ubuf,
		  unsigned int len)
{
	struct sock_fprog_kern *fprog;
	struct sk_filter *filter;
	int ret = 0;

	lock_sock(sk);
	filter = rcu_dereference_protected(sk->sk_filter,
					   sock_owned_by_user(sk));
	if (!filter)
		goto out;

	/* We're copying the filter that has been originally attached,
	 * so no conversion/decode needed anymore.
	 */
	fprog = filter->orig_prog;

	ret = fprog->len;
	if (!len)
		/* User space only enquires number of filter blocks. */
		goto out;

	ret = -EINVAL;
	if (len < fprog->len)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(ubuf, fprog->filter, sk_filter_proglen(fprog)))
		goto out;

	/* Instead of bytes, the API requests to return the number
	 * of filter blocks.
	 */
	ret = fprog->len;
out:
	release_sock(sk);
	return ret;
}
