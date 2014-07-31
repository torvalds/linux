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
 * Kris Katterjohn - Added many additional checks in bpf_check_classic()
 */
#include <linux/filter.h>
#include <linux/skbuff.h>
#include <asm/unaligned.h>

/* Registers */
#define BPF_R0	regs[BPF_REG_0]
#define BPF_R1	regs[BPF_REG_1]
#define BPF_R2	regs[BPF_REG_2]
#define BPF_R3	regs[BPF_REG_3]
#define BPF_R4	regs[BPF_REG_4]
#define BPF_R5	regs[BPF_REG_5]
#define BPF_R6	regs[BPF_REG_6]
#define BPF_R7	regs[BPF_REG_7]
#define BPF_R8	regs[BPF_REG_8]
#define BPF_R9	regs[BPF_REG_9]
#define BPF_R10	regs[BPF_REG_10]

/* Named registers */
#define DST	regs[insn->dst_reg]
#define SRC	regs[insn->src_reg]
#define FP	regs[BPF_REG_FP]
#define ARG1	regs[BPF_REG_ARG1]
#define CTX	regs[BPF_REG_CTX]
#define IMM	insn->imm

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

/* Base function for offset calculation. Needs to go into .text section,
 * therefore keeping it non-static as well; will also be used by JITs
 * anyway later on, so do not let the compiler omit it.
 */
noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	return 0;
}

/**
 *	__sk_run_filter - run a filter on a given context
 *	@ctx: buffer to run the filter on
 *	@insn: filter to apply
 *
 * Decode and apply filter instructions to the skb->data. Return length to
 * keep, 0 for none. @ctx is the data we are operating on, @insn is the
 * array of filter instructions.
 */
static unsigned int __sk_run_filter(void *ctx, const struct bpf_insn *insn)
{
	u64 stack[MAX_BPF_STACK / sizeof(u64)];
	u64 regs[MAX_BPF_REG], tmp;
	static const void *jumptable[256] = {
		[0 ... 255] = &&default_label,
		/* Now overwrite non-defaults ... */
		/* 32 bit ALU operations */
		[BPF_ALU | BPF_ADD | BPF_X] = &&ALU_ADD_X,
		[BPF_ALU | BPF_ADD | BPF_K] = &&ALU_ADD_K,
		[BPF_ALU | BPF_SUB | BPF_X] = &&ALU_SUB_X,
		[BPF_ALU | BPF_SUB | BPF_K] = &&ALU_SUB_K,
		[BPF_ALU | BPF_AND | BPF_X] = &&ALU_AND_X,
		[BPF_ALU | BPF_AND | BPF_K] = &&ALU_AND_K,
		[BPF_ALU | BPF_OR | BPF_X]  = &&ALU_OR_X,
		[BPF_ALU | BPF_OR | BPF_K]  = &&ALU_OR_K,
		[BPF_ALU | BPF_LSH | BPF_X] = &&ALU_LSH_X,
		[BPF_ALU | BPF_LSH | BPF_K] = &&ALU_LSH_K,
		[BPF_ALU | BPF_RSH | BPF_X] = &&ALU_RSH_X,
		[BPF_ALU | BPF_RSH | BPF_K] = &&ALU_RSH_K,
		[BPF_ALU | BPF_XOR | BPF_X] = &&ALU_XOR_X,
		[BPF_ALU | BPF_XOR | BPF_K] = &&ALU_XOR_K,
		[BPF_ALU | BPF_MUL | BPF_X] = &&ALU_MUL_X,
		[BPF_ALU | BPF_MUL | BPF_K] = &&ALU_MUL_K,
		[BPF_ALU | BPF_MOV | BPF_X] = &&ALU_MOV_X,
		[BPF_ALU | BPF_MOV | BPF_K] = &&ALU_MOV_K,
		[BPF_ALU | BPF_DIV | BPF_X] = &&ALU_DIV_X,
		[BPF_ALU | BPF_DIV | BPF_K] = &&ALU_DIV_K,
		[BPF_ALU | BPF_MOD | BPF_X] = &&ALU_MOD_X,
		[BPF_ALU | BPF_MOD | BPF_K] = &&ALU_MOD_K,
		[BPF_ALU | BPF_NEG] = &&ALU_NEG,
		[BPF_ALU | BPF_END | BPF_TO_BE] = &&ALU_END_TO_BE,
		[BPF_ALU | BPF_END | BPF_TO_LE] = &&ALU_END_TO_LE,
		/* 64 bit ALU operations */
		[BPF_ALU64 | BPF_ADD | BPF_X] = &&ALU64_ADD_X,
		[BPF_ALU64 | BPF_ADD | BPF_K] = &&ALU64_ADD_K,
		[BPF_ALU64 | BPF_SUB | BPF_X] = &&ALU64_SUB_X,
		[BPF_ALU64 | BPF_SUB | BPF_K] = &&ALU64_SUB_K,
		[BPF_ALU64 | BPF_AND | BPF_X] = &&ALU64_AND_X,
		[BPF_ALU64 | BPF_AND | BPF_K] = &&ALU64_AND_K,
		[BPF_ALU64 | BPF_OR | BPF_X] = &&ALU64_OR_X,
		[BPF_ALU64 | BPF_OR | BPF_K] = &&ALU64_OR_K,
		[BPF_ALU64 | BPF_LSH | BPF_X] = &&ALU64_LSH_X,
		[BPF_ALU64 | BPF_LSH | BPF_K] = &&ALU64_LSH_K,
		[BPF_ALU64 | BPF_RSH | BPF_X] = &&ALU64_RSH_X,
		[BPF_ALU64 | BPF_RSH | BPF_K] = &&ALU64_RSH_K,
		[BPF_ALU64 | BPF_XOR | BPF_X] = &&ALU64_XOR_X,
		[BPF_ALU64 | BPF_XOR | BPF_K] = &&ALU64_XOR_K,
		[BPF_ALU64 | BPF_MUL | BPF_X] = &&ALU64_MUL_X,
		[BPF_ALU64 | BPF_MUL | BPF_K] = &&ALU64_MUL_K,
		[BPF_ALU64 | BPF_MOV | BPF_X] = &&ALU64_MOV_X,
		[BPF_ALU64 | BPF_MOV | BPF_K] = &&ALU64_MOV_K,
		[BPF_ALU64 | BPF_ARSH | BPF_X] = &&ALU64_ARSH_X,
		[BPF_ALU64 | BPF_ARSH | BPF_K] = &&ALU64_ARSH_K,
		[BPF_ALU64 | BPF_DIV | BPF_X] = &&ALU64_DIV_X,
		[BPF_ALU64 | BPF_DIV | BPF_K] = &&ALU64_DIV_K,
		[BPF_ALU64 | BPF_MOD | BPF_X] = &&ALU64_MOD_X,
		[BPF_ALU64 | BPF_MOD | BPF_K] = &&ALU64_MOD_K,
		[BPF_ALU64 | BPF_NEG] = &&ALU64_NEG,
		/* Call instruction */
		[BPF_JMP | BPF_CALL] = &&JMP_CALL,
		/* Jumps */
		[BPF_JMP | BPF_JA] = &&JMP_JA,
		[BPF_JMP | BPF_JEQ | BPF_X] = &&JMP_JEQ_X,
		[BPF_JMP | BPF_JEQ | BPF_K] = &&JMP_JEQ_K,
		[BPF_JMP | BPF_JNE | BPF_X] = &&JMP_JNE_X,
		[BPF_JMP | BPF_JNE | BPF_K] = &&JMP_JNE_K,
		[BPF_JMP | BPF_JGT | BPF_X] = &&JMP_JGT_X,
		[BPF_JMP | BPF_JGT | BPF_K] = &&JMP_JGT_K,
		[BPF_JMP | BPF_JGE | BPF_X] = &&JMP_JGE_X,
		[BPF_JMP | BPF_JGE | BPF_K] = &&JMP_JGE_K,
		[BPF_JMP | BPF_JSGT | BPF_X] = &&JMP_JSGT_X,
		[BPF_JMP | BPF_JSGT | BPF_K] = &&JMP_JSGT_K,
		[BPF_JMP | BPF_JSGE | BPF_X] = &&JMP_JSGE_X,
		[BPF_JMP | BPF_JSGE | BPF_K] = &&JMP_JSGE_K,
		[BPF_JMP | BPF_JSET | BPF_X] = &&JMP_JSET_X,
		[BPF_JMP | BPF_JSET | BPF_K] = &&JMP_JSET_K,
		/* Program return */
		[BPF_JMP | BPF_EXIT] = &&JMP_EXIT,
		/* Store instructions */
		[BPF_STX | BPF_MEM | BPF_B] = &&STX_MEM_B,
		[BPF_STX | BPF_MEM | BPF_H] = &&STX_MEM_H,
		[BPF_STX | BPF_MEM | BPF_W] = &&STX_MEM_W,
		[BPF_STX | BPF_MEM | BPF_DW] = &&STX_MEM_DW,
		[BPF_STX | BPF_XADD | BPF_W] = &&STX_XADD_W,
		[BPF_STX | BPF_XADD | BPF_DW] = &&STX_XADD_DW,
		[BPF_ST | BPF_MEM | BPF_B] = &&ST_MEM_B,
		[BPF_ST | BPF_MEM | BPF_H] = &&ST_MEM_H,
		[BPF_ST | BPF_MEM | BPF_W] = &&ST_MEM_W,
		[BPF_ST | BPF_MEM | BPF_DW] = &&ST_MEM_DW,
		/* Load instructions */
		[BPF_LDX | BPF_MEM | BPF_B] = &&LDX_MEM_B,
		[BPF_LDX | BPF_MEM | BPF_H] = &&LDX_MEM_H,
		[BPF_LDX | BPF_MEM | BPF_W] = &&LDX_MEM_W,
		[BPF_LDX | BPF_MEM | BPF_DW] = &&LDX_MEM_DW,
		[BPF_LD | BPF_ABS | BPF_W] = &&LD_ABS_W,
		[BPF_LD | BPF_ABS | BPF_H] = &&LD_ABS_H,
		[BPF_LD | BPF_ABS | BPF_B] = &&LD_ABS_B,
		[BPF_LD | BPF_IND | BPF_W] = &&LD_IND_W,
		[BPF_LD | BPF_IND | BPF_H] = &&LD_IND_H,
		[BPF_LD | BPF_IND | BPF_B] = &&LD_IND_B,
	};
	void *ptr;
	int off;

#define CONT	 ({ insn++; goto select_insn; })
#define CONT_JMP ({ insn++; goto select_insn; })

	FP = (u64) (unsigned long) &stack[ARRAY_SIZE(stack)];
	ARG1 = (u64) (unsigned long) ctx;

	/* Registers used in classic BPF programs need to be reset first. */
	regs[BPF_REG_A] = 0;
	regs[BPF_REG_X] = 0;

select_insn:
	goto *jumptable[insn->code];

	/* ALU */
#define ALU(OPCODE, OP)			\
	ALU64_##OPCODE##_X:		\
		DST = DST OP SRC;	\
		CONT;			\
	ALU_##OPCODE##_X:		\
		DST = (u32) DST OP (u32) SRC;	\
		CONT;			\
	ALU64_##OPCODE##_K:		\
		DST = DST OP IMM;		\
		CONT;			\
	ALU_##OPCODE##_K:		\
		DST = (u32) DST OP (u32) IMM;	\
		CONT;

	ALU(ADD,  +)
	ALU(SUB,  -)
	ALU(AND,  &)
	ALU(OR,   |)
	ALU(LSH, <<)
	ALU(RSH, >>)
	ALU(XOR,  ^)
	ALU(MUL,  *)
#undef ALU
	ALU_NEG:
		DST = (u32) -DST;
		CONT;
	ALU64_NEG:
		DST = -DST;
		CONT;
	ALU_MOV_X:
		DST = (u32) SRC;
		CONT;
	ALU_MOV_K:
		DST = (u32) IMM;
		CONT;
	ALU64_MOV_X:
		DST = SRC;
		CONT;
	ALU64_MOV_K:
		DST = IMM;
		CONT;
	ALU64_ARSH_X:
		(*(s64 *) &DST) >>= SRC;
		CONT;
	ALU64_ARSH_K:
		(*(s64 *) &DST) >>= IMM;
		CONT;
	ALU64_MOD_X:
		if (unlikely(SRC == 0))
			return 0;
		tmp = DST;
		DST = do_div(tmp, SRC);
		CONT;
	ALU_MOD_X:
		if (unlikely(SRC == 0))
			return 0;
		tmp = (u32) DST;
		DST = do_div(tmp, (u32) SRC);
		CONT;
	ALU64_MOD_K:
		tmp = DST;
		DST = do_div(tmp, IMM);
		CONT;
	ALU_MOD_K:
		tmp = (u32) DST;
		DST = do_div(tmp, (u32) IMM);
		CONT;
	ALU64_DIV_X:
		if (unlikely(SRC == 0))
			return 0;
		do_div(DST, SRC);
		CONT;
	ALU_DIV_X:
		if (unlikely(SRC == 0))
			return 0;
		tmp = (u32) DST;
		do_div(tmp, (u32) SRC);
		DST = (u32) tmp;
		CONT;
	ALU64_DIV_K:
		do_div(DST, IMM);
		CONT;
	ALU_DIV_K:
		tmp = (u32) DST;
		do_div(tmp, (u32) IMM);
		DST = (u32) tmp;
		CONT;
	ALU_END_TO_BE:
		switch (IMM) {
		case 16:
			DST = (__force u16) cpu_to_be16(DST);
			break;
		case 32:
			DST = (__force u32) cpu_to_be32(DST);
			break;
		case 64:
			DST = (__force u64) cpu_to_be64(DST);
			break;
		}
		CONT;
	ALU_END_TO_LE:
		switch (IMM) {
		case 16:
			DST = (__force u16) cpu_to_le16(DST);
			break;
		case 32:
			DST = (__force u32) cpu_to_le32(DST);
			break;
		case 64:
			DST = (__force u64) cpu_to_le64(DST);
			break;
		}
		CONT;

	/* CALL */
	JMP_CALL:
		/* Function call scratches BPF_R1-BPF_R5 registers,
		 * preserves BPF_R6-BPF_R9, and stores return value
		 * into BPF_R0.
		 */
		BPF_R0 = (__bpf_call_base + insn->imm)(BPF_R1, BPF_R2, BPF_R3,
						       BPF_R4, BPF_R5);
		CONT;

	/* JMP */
	JMP_JA:
		insn += insn->off;
		CONT;
	JMP_JEQ_X:
		if (DST == SRC) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JEQ_K:
		if (DST == IMM) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JNE_X:
		if (DST != SRC) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JNE_K:
		if (DST != IMM) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JGT_X:
		if (DST > SRC) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JGT_K:
		if (DST > IMM) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JGE_X:
		if (DST >= SRC) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JGE_K:
		if (DST >= IMM) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSGT_X:
		if (((s64) DST) > ((s64) SRC)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSGT_K:
		if (((s64) DST) > ((s64) IMM)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSGE_X:
		if (((s64) DST) >= ((s64) SRC)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSGE_K:
		if (((s64) DST) >= ((s64) IMM)) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSET_X:
		if (DST & SRC) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_JSET_K:
		if (DST & IMM) {
			insn += insn->off;
			CONT_JMP;
		}
		CONT;
	JMP_EXIT:
		return BPF_R0;

	/* STX and ST and LDX*/
#define LDST(SIZEOP, SIZE)						\
	STX_MEM_##SIZEOP:						\
		*(SIZE *)(unsigned long) (DST + insn->off) = SRC;	\
		CONT;							\
	ST_MEM_##SIZEOP:						\
		*(SIZE *)(unsigned long) (DST + insn->off) = IMM;	\
		CONT;							\
	LDX_MEM_##SIZEOP:						\
		DST = *(SIZE *)(unsigned long) (SRC + insn->off);	\
		CONT;

	LDST(B,   u8)
	LDST(H,  u16)
	LDST(W,  u32)
	LDST(DW, u64)
#undef LDST
	STX_XADD_W: /* lock xadd *(u32 *)(dst_reg + off16) += src_reg */
		atomic_add((u32) SRC, (atomic_t *)(unsigned long)
			   (DST + insn->off));
		CONT;
	STX_XADD_DW: /* lock xadd *(u64 *)(dst_reg + off16) += src_reg */
		atomic64_add((u64) SRC, (atomic64_t *)(unsigned long)
			     (DST + insn->off));
		CONT;
	LD_ABS_W: /* BPF_R0 = ntohl(*(u32 *) (skb->data + imm32)) */
		off = IMM;
load_word:
		/* BPF_LD + BPD_ABS and BPF_LD + BPF_IND insns are
		 * only appearing in the programs where ctx ==
		 * skb. All programs keep 'ctx' in regs[BPF_REG_CTX]
		 * == BPF_R6, bpf_convert_filter() saves it in BPF_R6,
		 * internal BPF verifier will check that BPF_R6 ==
		 * ctx.
		 *
		 * BPF_ABS and BPF_IND are wrappers of function calls,
		 * so they scratch BPF_R1-BPF_R5 registers, preserve
		 * BPF_R6-BPF_R9, and store return value into BPF_R0.
		 *
		 * Implicit input:
		 *   ctx == skb == BPF_R6 == CTX
		 *
		 * Explicit input:
		 *   SRC == any register
		 *   IMM == 32-bit immediate
		 *
		 * Output:
		 *   BPF_R0 - 8/16/32-bit skb data converted to cpu endianness
		 */

		ptr = bpf_load_pointer((struct sk_buff *) (unsigned long) CTX, off, 4, &tmp);
		if (likely(ptr != NULL)) {
			BPF_R0 = get_unaligned_be32(ptr);
			CONT;
		}

		return 0;
	LD_ABS_H: /* BPF_R0 = ntohs(*(u16 *) (skb->data + imm32)) */
		off = IMM;
load_half:
		ptr = bpf_load_pointer((struct sk_buff *) (unsigned long) CTX, off, 2, &tmp);
		if (likely(ptr != NULL)) {
			BPF_R0 = get_unaligned_be16(ptr);
			CONT;
		}

		return 0;
	LD_ABS_B: /* BPF_R0 = *(u8 *) (skb->data + imm32) */
		off = IMM;
load_byte:
		ptr = bpf_load_pointer((struct sk_buff *) (unsigned long) CTX, off, 1, &tmp);
		if (likely(ptr != NULL)) {
			BPF_R0 = *(u8 *)ptr;
			CONT;
		}

		return 0;
	LD_IND_W: /* BPF_R0 = ntohl(*(u32 *) (skb->data + src_reg + imm32)) */
		off = IMM + SRC;
		goto load_word;
	LD_IND_H: /* BPF_R0 = ntohs(*(u16 *) (skb->data + src_reg + imm32)) */
		off = IMM + SRC;
		goto load_half;
	LD_IND_B: /* BPF_R0 = *(u8 *) (skb->data + src_reg + imm32) */
		off = IMM + SRC;
		goto load_byte;

	default_label:
		/* If we ever reach this, we have a bug somewhere. */
		WARN_RATELIMIT(1, "unknown opcode %02x\n", insn->code);
		return 0;
}

void __weak bpf_int_jit_compile(struct sk_filter *prog)
{
}

/**
 *	sk_filter_select_runtime - select execution runtime for BPF program
 *	@fp: sk_filter populated with internal BPF program
 *
 * try to JIT internal BPF program, if JIT is not available select interpreter
 * BPF program will be executed via SK_RUN_FILTER() macro
 */
void sk_filter_select_runtime(struct sk_filter *fp)
{
	fp->bpf_func = (void *) __sk_run_filter;

	/* Probe if internal BPF can be JITed */
	bpf_int_jit_compile(fp);
}
EXPORT_SYMBOL_GPL(sk_filter_select_runtime);

/* free internal BPF program */
void sk_filter_free(struct sk_filter *fp)
{
	bpf_jit_free(fp);
}
EXPORT_SYMBOL_GPL(sk_filter_free);
