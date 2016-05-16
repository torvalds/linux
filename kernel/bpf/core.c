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
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/moduleloader.h>
#include <linux/bpf.h>
#include <linux/frame.h>

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

struct bpf_prog *bpf_prog_alloc(unsigned int size, gfp_t gfp_extra_flags)
{
	gfp_t gfp_flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO |
			  gfp_extra_flags;
	struct bpf_prog_aux *aux;
	struct bpf_prog *fp;

	size = round_up(size, PAGE_SIZE);
	fp = __vmalloc(size, gfp_flags, PAGE_KERNEL);
	if (fp == NULL)
		return NULL;

	kmemcheck_annotate_bitfield(fp, meta);

	aux = kzalloc(sizeof(*aux), GFP_KERNEL | gfp_extra_flags);
	if (aux == NULL) {
		vfree(fp);
		return NULL;
	}

	fp->pages = size / PAGE_SIZE;
	fp->aux = aux;
	fp->aux->prog = fp;

	return fp;
}
EXPORT_SYMBOL_GPL(bpf_prog_alloc);

struct bpf_prog *bpf_prog_realloc(struct bpf_prog *fp_old, unsigned int size,
				  gfp_t gfp_extra_flags)
{
	gfp_t gfp_flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO |
			  gfp_extra_flags;
	struct bpf_prog *fp;

	BUG_ON(fp_old == NULL);

	size = round_up(size, PAGE_SIZE);
	if (size <= fp_old->pages * PAGE_SIZE)
		return fp_old;

	fp = __vmalloc(size, gfp_flags, PAGE_KERNEL);
	if (fp != NULL) {
		kmemcheck_annotate_bitfield(fp, meta);

		memcpy(fp, fp_old, fp_old->pages * PAGE_SIZE);
		fp->pages = size / PAGE_SIZE;
		fp->aux->prog = fp;

		/* We keep fp->aux from fp_old around in the new
		 * reallocated structure.
		 */
		fp_old->aux = NULL;
		__bpf_prog_free(fp_old);
	}

	return fp;
}

void __bpf_prog_free(struct bpf_prog *fp)
{
	kfree(fp->aux);
	vfree(fp);
}

static bool bpf_is_jmp_and_has_target(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_JMP  &&
	       /* Call and Exit are both special jumps with no
		* target inside the BPF instruction image.
		*/
	       BPF_OP(insn->code) != BPF_CALL &&
	       BPF_OP(insn->code) != BPF_EXIT;
}

static void bpf_adj_branches(struct bpf_prog *prog, u32 pos, u32 delta)
{
	struct bpf_insn *insn = prog->insnsi;
	u32 i, insn_cnt = prog->len;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (!bpf_is_jmp_and_has_target(insn))
			continue;

		/* Adjust offset of jmps if we cross boundaries. */
		if (i < pos && i + insn->off + 1 > pos)
			insn->off += delta;
		else if (i > pos + delta && i + insn->off + 1 <= pos + delta)
			insn->off -= delta;
	}
}

struct bpf_prog *bpf_patch_insn_single(struct bpf_prog *prog, u32 off,
				       const struct bpf_insn *patch, u32 len)
{
	u32 insn_adj_cnt, insn_rest, insn_delta = len - 1;
	struct bpf_prog *prog_adj;

	/* Since our patchlet doesn't expand the image, we're done. */
	if (insn_delta == 0) {
		memcpy(prog->insnsi + off, patch, sizeof(*patch));
		return prog;
	}

	insn_adj_cnt = prog->len + insn_delta;

	/* Several new instructions need to be inserted. Make room
	 * for them. Likely, there's no need for a new allocation as
	 * last page could have large enough tailroom.
	 */
	prog_adj = bpf_prog_realloc(prog, bpf_prog_size(insn_adj_cnt),
				    GFP_USER);
	if (!prog_adj)
		return NULL;

	prog_adj->len = insn_adj_cnt;

	/* Patching happens in 3 steps:
	 *
	 * 1) Move over tail of insnsi from next instruction onwards,
	 *    so we can patch the single target insn with one or more
	 *    new ones (patching is always from 1 to n insns, n > 0).
	 * 2) Inject new instructions at the target location.
	 * 3) Adjust branch offsets if necessary.
	 */
	insn_rest = insn_adj_cnt - off - len;

	memmove(prog_adj->insnsi + off + len, prog_adj->insnsi + off + 1,
		sizeof(*patch) * insn_rest);
	memcpy(prog_adj->insnsi + off, patch, sizeof(*patch) * len);

	bpf_adj_branches(prog_adj, off, insn_delta);

	return prog_adj;
}

#ifdef CONFIG_BPF_JIT
struct bpf_binary_header *
bpf_jit_binary_alloc(unsigned int proglen, u8 **image_ptr,
		     unsigned int alignment,
		     bpf_jit_fill_hole_t bpf_fill_ill_insns)
{
	struct bpf_binary_header *hdr;
	unsigned int size, hole, start;

	/* Most of BPF filters are really small, but if some of them
	 * fill a page, allow at least 128 extra bytes to insert a
	 * random section of illegal instructions.
	 */
	size = round_up(proglen + sizeof(*hdr) + 128, PAGE_SIZE);
	hdr = module_alloc(size);
	if (hdr == NULL)
		return NULL;

	/* Fill space with illegal/arch-dep instructions. */
	bpf_fill_ill_insns(hdr, size);

	hdr->pages = size / PAGE_SIZE;
	hole = min_t(unsigned int, size - (proglen + sizeof(*hdr)),
		     PAGE_SIZE - sizeof(*hdr));
	start = (prandom_u32() % hole) & ~(alignment - 1);

	/* Leave a random number of instructions before BPF code. */
	*image_ptr = &hdr->image[start];

	return hdr;
}

void bpf_jit_binary_free(struct bpf_binary_header *hdr)
{
	module_memfree(hdr);
}

int bpf_jit_harden __read_mostly;

static int bpf_jit_blind_insn(const struct bpf_insn *from,
			      const struct bpf_insn *aux,
			      struct bpf_insn *to_buff)
{
	struct bpf_insn *to = to_buff;
	u32 imm_rnd = prandom_u32();
	s16 off;

	BUILD_BUG_ON(BPF_REG_AX  + 1 != MAX_BPF_JIT_REG);
	BUILD_BUG_ON(MAX_BPF_REG + 1 != MAX_BPF_JIT_REG);

	if (from->imm == 0 &&
	    (from->code == (BPF_ALU   | BPF_MOV | BPF_K) ||
	     from->code == (BPF_ALU64 | BPF_MOV | BPF_K))) {
		*to++ = BPF_ALU64_REG(BPF_XOR, from->dst_reg, from->dst_reg);
		goto out;
	}

	switch (from->code) {
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU | BPF_OR  | BPF_K:
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
		*to++ = BPF_ALU32_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU32_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU32_REG(from->code, from->dst_reg, BPF_REG_AX);
		break;

	case BPF_ALU64 | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_OR  | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU64_REG(from->code, from->dst_reg, BPF_REG_AX);
		break;

	case BPF_JMP | BPF_JEQ  | BPF_K:
	case BPF_JMP | BPF_JNE  | BPF_K:
	case BPF_JMP | BPF_JGT  | BPF_K:
	case BPF_JMP | BPF_JGE  | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSET | BPF_K:
		/* Accommodate for extra offset in case of a backjump. */
		off = from->off;
		if (off < 0)
			off -= 2;
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_JMP_REG(from->code, from->dst_reg, BPF_REG_AX, off);
		break;

	case BPF_LD | BPF_ABS | BPF_W:
	case BPF_LD | BPF_ABS | BPF_H:
	case BPF_LD | BPF_ABS | BPF_B:
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_LD_IND(from->code, BPF_REG_AX, 0);
		break;

	case BPF_LD | BPF_IND | BPF_W:
	case BPF_LD | BPF_IND | BPF_H:
	case BPF_LD | BPF_IND | BPF_B:
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU32_REG(BPF_ADD, BPF_REG_AX, from->src_reg);
		*to++ = BPF_LD_IND(from->code, BPF_REG_AX, 0);
		break;

	case BPF_LD | BPF_IMM | BPF_DW:
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ aux[1].imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU64_IMM(BPF_LSH, BPF_REG_AX, 32);
		*to++ = BPF_ALU64_REG(BPF_MOV, aux[0].dst_reg, BPF_REG_AX);
		break;
	case 0: /* Part 2 of BPF_LD | BPF_IMM | BPF_DW. */
		*to++ = BPF_ALU32_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ aux[0].imm);
		*to++ = BPF_ALU32_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU64_REG(BPF_OR,  aux[0].dst_reg, BPF_REG_AX);
		break;

	case BPF_ST | BPF_MEM | BPF_DW:
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
		*to++ = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU64_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_STX_MEM(from->code, from->dst_reg, BPF_REG_AX, from->off);
		break;
	}
out:
	return to - to_buff;
}

static struct bpf_prog *bpf_prog_clone_create(struct bpf_prog *fp_other,
					      gfp_t gfp_extra_flags)
{
	gfp_t gfp_flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO |
			  gfp_extra_flags;
	struct bpf_prog *fp;

	fp = __vmalloc(fp_other->pages * PAGE_SIZE, gfp_flags, PAGE_KERNEL);
	if (fp != NULL) {
		kmemcheck_annotate_bitfield(fp, meta);

		/* aux->prog still points to the fp_other one, so
		 * when promoting the clone to the real program,
		 * this still needs to be adapted.
		 */
		memcpy(fp, fp_other, fp_other->pages * PAGE_SIZE);
	}

	return fp;
}

static void bpf_prog_clone_free(struct bpf_prog *fp)
{
	/* aux was stolen by the other clone, so we cannot free
	 * it from this path! It will be freed eventually by the
	 * other program on release.
	 *
	 * At this point, we don't need a deferred release since
	 * clone is guaranteed to not be locked.
	 */
	fp->aux = NULL;
	__bpf_prog_free(fp);
}

void bpf_jit_prog_release_other(struct bpf_prog *fp, struct bpf_prog *fp_other)
{
	/* We have to repoint aux->prog to self, as we don't
	 * know whether fp here is the clone or the original.
	 */
	fp->aux->prog = fp;
	bpf_prog_clone_free(fp_other);
}

struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)
{
	struct bpf_insn insn_buff[16], aux[2];
	struct bpf_prog *clone, *tmp;
	int insn_delta, insn_cnt;
	struct bpf_insn *insn;
	int i, rewritten;

	if (!bpf_jit_blinding_enabled())
		return prog;

	clone = bpf_prog_clone_create(prog, GFP_USER);
	if (!clone)
		return ERR_PTR(-ENOMEM);

	insn_cnt = clone->len;
	insn = clone->insnsi;

	for (i = 0; i < insn_cnt; i++, insn++) {
		/* We temporarily need to hold the original ld64 insn
		 * so that we can still access the first part in the
		 * second blinding run.
		 */
		if (insn[0].code == (BPF_LD | BPF_IMM | BPF_DW) &&
		    insn[1].code == 0)
			memcpy(aux, insn, sizeof(aux));

		rewritten = bpf_jit_blind_insn(insn, aux, insn_buff);
		if (!rewritten)
			continue;

		tmp = bpf_patch_insn_single(clone, i, insn_buff, rewritten);
		if (!tmp) {
			/* Patching may have repointed aux->prog during
			 * realloc from the original one, so we need to
			 * fix it up here on error.
			 */
			bpf_jit_prog_release_other(prog, clone);
			return ERR_PTR(-ENOMEM);
		}

		clone = tmp;
		insn_delta = rewritten - 1;

		/* Walk new program and skip insns we just inserted. */
		insn = clone->insnsi + i + insn_delta;
		insn_cnt += insn_delta;
		i        += insn_delta;
	}

	return clone;
}
#endif /* CONFIG_BPF_JIT */

/* Base function for offset calculation. Needs to go into .text section,
 * therefore keeping it non-static as well; will also be used by JITs
 * anyway later on, so do not let the compiler omit it.
 */
noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	return 0;
}
EXPORT_SYMBOL_GPL(__bpf_call_base);

/**
 *	__bpf_prog_run - run eBPF program on a given context
 *	@ctx: is the data we are operating on
 *	@insn: is the array of eBPF instructions
 *
 * Decode and execute eBPF instructions.
 */
static unsigned int __bpf_prog_run(void *ctx, const struct bpf_insn *insn)
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
		[BPF_JMP | BPF_CALL | BPF_X] = &&JMP_TAIL_CALL,
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
		[BPF_LD | BPF_IMM | BPF_DW] = &&LD_IMM_DW,
	};
	u32 tail_call_cnt = 0;
	void *ptr;
	int off;

#define CONT	 ({ insn++; goto select_insn; })
#define CONT_JMP ({ insn++; goto select_insn; })

	FP = (u64) (unsigned long) &stack[ARRAY_SIZE(stack)];
	ARG1 = (u64) (unsigned long) ctx;

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
	LD_IMM_DW:
		DST = (u64) (u32) insn[0].imm | ((u64) (u32) insn[1].imm) << 32;
		insn++;
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
		div64_u64_rem(DST, SRC, &tmp);
		DST = tmp;
		CONT;
	ALU_MOD_X:
		if (unlikely(SRC == 0))
			return 0;
		tmp = (u32) DST;
		DST = do_div(tmp, (u32) SRC);
		CONT;
	ALU64_MOD_K:
		div64_u64_rem(DST, IMM, &tmp);
		DST = tmp;
		CONT;
	ALU_MOD_K:
		tmp = (u32) DST;
		DST = do_div(tmp, (u32) IMM);
		CONT;
	ALU64_DIV_X:
		if (unlikely(SRC == 0))
			return 0;
		DST = div64_u64(DST, SRC);
		CONT;
	ALU_DIV_X:
		if (unlikely(SRC == 0))
			return 0;
		tmp = (u32) DST;
		do_div(tmp, (u32) SRC);
		DST = (u32) tmp;
		CONT;
	ALU64_DIV_K:
		DST = div64_u64(DST, IMM);
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

	JMP_TAIL_CALL: {
		struct bpf_map *map = (struct bpf_map *) (unsigned long) BPF_R2;
		struct bpf_array *array = container_of(map, struct bpf_array, map);
		struct bpf_prog *prog;
		u64 index = BPF_R3;

		if (unlikely(index >= array->map.max_entries))
			goto out;

		if (unlikely(tail_call_cnt > MAX_TAIL_CALL_CNT))
			goto out;

		tail_call_cnt++;

		prog = READ_ONCE(array->ptrs[index]);
		if (unlikely(!prog))
			goto out;

		/* ARG1 at this point is guaranteed to point to CTX from
		 * the verifier side due to the fact that the tail call is
		 * handeled like a helper, that is, bpf_tail_call_proto,
		 * where arg1_type is ARG_PTR_TO_CTX.
		 */
		insn = prog->insnsi;
		goto select_insn;
out:
		CONT;
	}
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
STACK_FRAME_NON_STANDARD(__bpf_prog_run); /* jump table */

bool bpf_prog_array_compatible(struct bpf_array *array,
			       const struct bpf_prog *fp)
{
	if (!array->owner_prog_type) {
		/* There's no owner yet where we could check for
		 * compatibility.
		 */
		array->owner_prog_type = fp->type;
		array->owner_jited = fp->jited;

		return true;
	}

	return array->owner_prog_type == fp->type &&
	       array->owner_jited == fp->jited;
}

static int bpf_check_tail_call(const struct bpf_prog *fp)
{
	struct bpf_prog_aux *aux = fp->aux;
	int i;

	for (i = 0; i < aux->used_map_cnt; i++) {
		struct bpf_map *map = aux->used_maps[i];
		struct bpf_array *array;

		if (map->map_type != BPF_MAP_TYPE_PROG_ARRAY)
			continue;

		array = container_of(map, struct bpf_array, map);
		if (!bpf_prog_array_compatible(array, fp))
			return -EINVAL;
	}

	return 0;
}

/**
 *	bpf_prog_select_runtime - select exec runtime for BPF program
 *	@fp: bpf_prog populated with internal BPF program
 *	@err: pointer to error variable
 *
 * Try to JIT eBPF program, if JIT is not available, use interpreter.
 * The BPF program will be executed via BPF_PROG_RUN() macro.
 */
struct bpf_prog *bpf_prog_select_runtime(struct bpf_prog *fp, int *err)
{
	fp->bpf_func = (void *) __bpf_prog_run;

	/* eBPF JITs can rewrite the program in case constant
	 * blinding is active. However, in case of error during
	 * blinding, bpf_int_jit_compile() must always return a
	 * valid program, which in this case would simply not
	 * be JITed, but falls back to the interpreter.
	 */
	fp = bpf_int_jit_compile(fp);
	bpf_prog_lock_ro(fp);

	/* The tail call compatibility check can only be done at
	 * this late stage as we need to determine, if we deal
	 * with JITed or non JITed program concatenations and not
	 * all eBPF JITs might immediately support all features.
	 */
	*err = bpf_check_tail_call(fp);

	return fp;
}
EXPORT_SYMBOL_GPL(bpf_prog_select_runtime);

static void bpf_prog_free_deferred(struct work_struct *work)
{
	struct bpf_prog_aux *aux;

	aux = container_of(work, struct bpf_prog_aux, work);
	bpf_jit_free(aux->prog);
}

/* Free internal BPF program */
void bpf_prog_free(struct bpf_prog *fp)
{
	struct bpf_prog_aux *aux = fp->aux;

	INIT_WORK(&aux->work, bpf_prog_free_deferred);
	schedule_work(&aux->work);
}
EXPORT_SYMBOL_GPL(bpf_prog_free);

/* RNG for unpriviledged user space with separated state from prandom_u32(). */
static DEFINE_PER_CPU(struct rnd_state, bpf_user_rnd_state);

void bpf_user_rnd_init_once(void)
{
	prandom_init_once(&bpf_user_rnd_state);
}

u64 bpf_user_rnd_u32(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	/* Should someone ever have the rather unwise idea to use some
	 * of the registers passed into this function, then note that
	 * this function is called from native eBPF and classic-to-eBPF
	 * transformations. Register assignments from both sides are
	 * different, f.e. classic always sets fn(ctx, A, X) here.
	 */
	struct rnd_state *state;
	u32 res;

	state = &get_cpu_var(bpf_user_rnd_state);
	res = prandom_u32_state(state);
	put_cpu_var(state);

	return res;
}

/* Weak definitions of helper functions in case we don't have bpf syscall. */
const struct bpf_func_proto bpf_map_lookup_elem_proto __weak;
const struct bpf_func_proto bpf_map_update_elem_proto __weak;
const struct bpf_func_proto bpf_map_delete_elem_proto __weak;

const struct bpf_func_proto bpf_get_prandom_u32_proto __weak;
const struct bpf_func_proto bpf_get_smp_processor_id_proto __weak;
const struct bpf_func_proto bpf_ktime_get_ns_proto __weak;

const struct bpf_func_proto bpf_get_current_pid_tgid_proto __weak;
const struct bpf_func_proto bpf_get_current_uid_gid_proto __weak;
const struct bpf_func_proto bpf_get_current_comm_proto __weak;

const struct bpf_func_proto * __weak bpf_get_trace_printk_proto(void)
{
	return NULL;
}

const struct bpf_func_proto * __weak bpf_get_event_output_proto(void)
{
	return NULL;
}

/* Always built-in helper functions. */
const struct bpf_func_proto bpf_tail_call_proto = {
	.func		= NULL,
	.gpl_only	= false,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

/* For classic BPF JITs that don't implement bpf_int_jit_compile(). */
struct bpf_prog * __weak bpf_int_jit_compile(struct bpf_prog *prog)
{
	return prog;
}

bool __weak bpf_helper_changes_skb_data(void *func)
{
	return false;
}

/* To execute LD_ABS/LD_IND instructions __bpf_prog_run() may call
 * skb_copy_bits(), so provide a weak definition of it for NET-less config.
 */
int __weak skb_copy_bits(const struct sk_buff *skb, int offset, void *to,
			 int len)
{
	return -EFAULT;
}
