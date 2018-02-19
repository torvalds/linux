/*
 * Testsuite for BPF interpreter and BPF JIT compiler
 *
 * Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/random.h>
#include <linux/highmem.h>

/* General test specific settings */
#define MAX_SUBTESTS	3
#define MAX_TESTRUNS	10000
#define MAX_DATA	128
#define MAX_INSNS	512
#define MAX_K		0xffffFFFF

/* Few constants used to init test 'skb' */
#define SKB_TYPE	3
#define SKB_MARK	0x1234aaaa
#define SKB_HASH	0x1234aaab
#define SKB_QUEUE_MAP	123
#define SKB_VLAN_TCI	0xffff
#define SKB_DEV_IFINDEX	577
#define SKB_DEV_TYPE	588

/* Redefine REGs to make tests less verbose */
#define R0		BPF_REG_0
#define R1		BPF_REG_1
#define R2		BPF_REG_2
#define R3		BPF_REG_3
#define R4		BPF_REG_4
#define R5		BPF_REG_5
#define R6		BPF_REG_6
#define R7		BPF_REG_7
#define R8		BPF_REG_8
#define R9		BPF_REG_9
#define R10		BPF_REG_10

/* Flags that can be passed to test cases */
#define FLAG_NO_DATA		BIT(0)
#define FLAG_EXPECTED_FAIL	BIT(1)
#define FLAG_SKB_FRAG		BIT(2)

enum {
	CLASSIC  = BIT(6),	/* Old BPF instructions only. */
	INTERNAL = BIT(7),	/* Extended instruction set.  */
};

#define TEST_TYPE_MASK		(CLASSIC | INTERNAL)

struct bpf_test {
	const char *descr;
	union {
		struct sock_filter insns[MAX_INSNS];
		struct bpf_insn insns_int[MAX_INSNS];
		struct {
			void *insns;
			unsigned int len;
		} ptr;
	} u;
	__u8 aux;
	__u8 data[MAX_DATA];
	struct {
		int data_size;
		__u32 result;
	} test[MAX_SUBTESTS];
	int (*fill_helper)(struct bpf_test *self);
	__u8 frag_data[MAX_DATA];
	int stack_depth; /* for eBPF only, since tests don't call verifier */
};

/* Large test cases need separate allocation and fill handler. */

static int bpf_fill_maxinsns1(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	__u32 k = ~0;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len; i++, k--)
		insn[i] = __BPF_STMT(BPF_RET | BPF_K, k);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns2(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len; i++)
		insn[i] = __BPF_STMT(BPF_RET | BPF_K, 0xfefefefe);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns3(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	struct rnd_state rnd;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	prandom_seed_state(&rnd, 3141592653589793238ULL);

	for (i = 0; i < len - 1; i++) {
		__u32 k = prandom_u32_state(&rnd);

		insn[i] = __BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, k);
	}

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_A, 0);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns4(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS + 1;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len; i++)
		insn[i] = __BPF_STMT(BPF_RET | BPF_K, 0xfefefefe);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns5(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[0] = __BPF_JUMP(BPF_JMP | BPF_JA, len - 2, 0, 0);

	for (i = 1; i < len - 1; i++)
		insn[i] = __BPF_STMT(BPF_RET | BPF_K, 0xfefefefe);

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_K, 0xabababab);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns6(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len - 1; i++)
		insn[i] = __BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF +
				     SKF_AD_VLAN_TAG_PRESENT);

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_A, 0);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns7(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len - 4; i++)
		insn[i] = __BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF +
				     SKF_AD_CPU);

	insn[len - 4] = __BPF_STMT(BPF_MISC | BPF_TAX, 0);
	insn[len - 3] = __BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_AD_OFF +
				   SKF_AD_CPU);
	insn[len - 2] = __BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0);
	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_A, 0);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns8(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i, jmp_off = len - 3;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[0] = __BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff);

	for (i = 1; i < len - 1; i++)
		insn[i] = __BPF_JUMP(BPF_JMP | BPF_JGT, 0xffffffff, jmp_off--, 0);

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_A, 0);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns9(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct bpf_insn *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[0] = BPF_JMP_IMM(BPF_JA, 0, 0, len - 2);
	insn[1] = BPF_ALU32_IMM(BPF_MOV, R0, 0xcbababab);
	insn[2] = BPF_EXIT_INSN();

	for (i = 3; i < len - 2; i++)
		insn[i] = BPF_ALU32_IMM(BPF_MOV, R0, 0xfefefefe);

	insn[len - 2] = BPF_EXIT_INSN();
	insn[len - 1] = BPF_JMP_IMM(BPF_JA, 0, 0, -(len - 1));

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns10(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS, hlen = len - 2;
	struct bpf_insn *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < hlen / 2; i++)
		insn[i] = BPF_JMP_IMM(BPF_JA, 0, 0, hlen - 2 - 2 * i);
	for (i = hlen - 1; i > hlen / 2; i--)
		insn[i] = BPF_JMP_IMM(BPF_JA, 0, 0, hlen - 1 - 2 * i);

	insn[hlen / 2] = BPF_JMP_IMM(BPF_JA, 0, 0, hlen / 2 - 1);
	insn[hlen]     = BPF_ALU32_IMM(BPF_MOV, R0, 0xabababac);
	insn[hlen + 1] = BPF_EXIT_INSN();

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int __bpf_fill_ja(struct bpf_test *self, unsigned int len,
			 unsigned int plen)
{
	struct sock_filter *insn;
	unsigned int rlen;
	int i, j;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	rlen = (len % plen) - 1;

	for (i = 0; i + plen < len; i += plen)
		for (j = 0; j < plen; j++)
			insn[i + j] = __BPF_JUMP(BPF_JMP | BPF_JA,
						 plen - 1 - j, 0, 0);
	for (j = 0; j < rlen; j++)
		insn[i + j] = __BPF_JUMP(BPF_JMP | BPF_JA, rlen - 1 - j,
					 0, 0);

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_K, 0xababcbac);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_maxinsns11(struct bpf_test *self)
{
	/* Hits 70 passes on x86_64, so cannot get JITed there. */
	return __bpf_fill_ja(self, BPF_MAXINSNS, 68);
}

static int bpf_fill_ja(struct bpf_test *self)
{
	/* Hits exactly 11 passes on x86_64 JIT. */
	return __bpf_fill_ja(self, 12, 9);
}

static int bpf_fill_ld_abs_get_processor_id(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct sock_filter *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	for (i = 0; i < len - 1; i += 2) {
		insn[i] = __BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 0);
		insn[i + 1] = __BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					 SKF_AD_OFF + SKF_AD_CPU);
	}

	insn[len - 1] = __BPF_STMT(BPF_RET | BPF_K, 0xbee);

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

#define PUSH_CNT 68
/* test: {skb->data[0], vlan_push} x 68 + {skb->data[0], vlan_pop} x 68 */
static int bpf_fill_ld_abs_vlan_push_pop(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct bpf_insn *insn;
	int i = 0, j, k = 0;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[i++] = BPF_MOV64_REG(R6, R1);
loop:
	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP_IMM(BPF_JNE, R0, 0x34, len - i - 2);
		i++;
		insn[i++] = BPF_MOV64_REG(R1, R6);
		insn[i++] = BPF_MOV64_IMM(R2, 1);
		insn[i++] = BPF_MOV64_IMM(R3, 2);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 bpf_skb_vlan_push_proto.func - __bpf_call_base);
		insn[i] = BPF_JMP_IMM(BPF_JNE, R0, 0, len - i - 2);
		i++;
	}

	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP_IMM(BPF_JNE, R0, 0x34, len - i - 2);
		i++;
		insn[i++] = BPF_MOV64_REG(R1, R6);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 bpf_skb_vlan_pop_proto.func - __bpf_call_base);
		insn[i] = BPF_JMP_IMM(BPF_JNE, R0, 0, len - i - 2);
		i++;
	}
	if (++k < 5)
		goto loop;

	for (; i < len - 1; i++)
		insn[i] = BPF_ALU32_IMM(BPF_MOV, R0, 0xbef);

	insn[len - 1] = BPF_EXIT_INSN();

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int bpf_fill_ld_abs_vlan_push_pop2(struct bpf_test *self)
{
	struct bpf_insn *insn;

	insn = kmalloc_array(16, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	/* Due to func address being non-const, we need to
	 * assemble this here.
	 */
	insn[0] = BPF_MOV64_REG(R6, R1);
	insn[1] = BPF_LD_ABS(BPF_B, 0);
	insn[2] = BPF_LD_ABS(BPF_H, 0);
	insn[3] = BPF_LD_ABS(BPF_W, 0);
	insn[4] = BPF_MOV64_REG(R7, R6);
	insn[5] = BPF_MOV64_IMM(R6, 0);
	insn[6] = BPF_MOV64_REG(R1, R7);
	insn[7] = BPF_MOV64_IMM(R2, 1);
	insn[8] = BPF_MOV64_IMM(R3, 2);
	insn[9] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			       bpf_skb_vlan_push_proto.func - __bpf_call_base);
	insn[10] = BPF_MOV64_REG(R6, R7);
	insn[11] = BPF_LD_ABS(BPF_B, 0);
	insn[12] = BPF_LD_ABS(BPF_H, 0);
	insn[13] = BPF_LD_ABS(BPF_W, 0);
	insn[14] = BPF_MOV64_IMM(R0, 42);
	insn[15] = BPF_EXIT_INSN();

	self->u.ptr.insns = insn;
	self->u.ptr.len = 16;

	return 0;
}

static int bpf_fill_jump_around_ld_abs(struct bpf_test *self)
{
	unsigned int len = BPF_MAXINSNS;
	struct bpf_insn *insn;
	int i = 0;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[i++] = BPF_MOV64_REG(R6, R1);
	insn[i++] = BPF_LD_ABS(BPF_B, 0);
	insn[i] = BPF_JMP_IMM(BPF_JEQ, R0, 10, len - i - 2);
	i++;
	while (i < len - 1)
		insn[i++] = BPF_LD_ABS(BPF_B, 1);
	insn[i] = BPF_EXIT_INSN();

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;

	return 0;
}

static int __bpf_fill_stxdw(struct bpf_test *self, int size)
{
	unsigned int len = BPF_MAXINSNS;
	struct bpf_insn *insn;
	int i;

	insn = kmalloc_array(len, sizeof(*insn), GFP_KERNEL);
	if (!insn)
		return -ENOMEM;

	insn[0] = BPF_ALU32_IMM(BPF_MOV, R0, 1);
	insn[1] = BPF_ST_MEM(size, R10, -40, 42);

	for (i = 2; i < len - 2; i++)
		insn[i] = BPF_STX_XADD(size, R10, R0, -40);

	insn[len - 2] = BPF_LDX_MEM(size, R0, R10, -40);
	insn[len - 1] = BPF_EXIT_INSN();

	self->u.ptr.insns = insn;
	self->u.ptr.len = len;
	self->stack_depth = 40;

	return 0;
}

static int bpf_fill_stxw(struct bpf_test *self)
{
	return __bpf_fill_stxdw(self, BPF_W);
}

static int bpf_fill_stxdw(struct bpf_test *self)
{
	return __bpf_fill_stxdw(self, BPF_DW);
}

static struct bpf_test tests[] = {
	{
		"TAX",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_ALU | BPF_NEG, 0), /* A == -3 */
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0), /* X == len - 3 */
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 1),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 10, 20, 30, 40, 50 },
		{ { 2, 10 }, { 3, 20 }, { 4, 30 } },
	},
	{
		"TXA",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0) /* A == len * 2 */
		},
		CLASSIC,
		{ 10, 20, 30, 40, 50 },
		{ { 1, 2 }, { 3, 6 }, { 4, 8 } },
	},
	{
		"ADD_SUB_MUL_K",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 2),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_MUL | BPF_K, 3),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xfffffffd } }
	},
	{
		"DIV_MOD_KX",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 8),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 2),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 0x70000000),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_MOD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xffffffff),
			BPF_STMT(BPF_ALU | BPF_MOD | BPF_K, 0x70000000),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0x20000000 } }
	},
	{
		"AND_OR_LSH_K",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 0xff),
			BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf0),
			BPF_STMT(BPF_ALU | BPF_LSH | BPF_K, 27),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 0xf),
			BPF_STMT(BPF_ALU | BPF_OR | BPF_K, 0xf0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0x800000ff }, { 1, 0x800000ff } },
	},
	{
		"LD_IMM_0",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 0), /* ld #0 */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
		},
		CLASSIC,
		{ },
		{ { 1, 1 } },
	},
	{
		"LD_IND",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, MAX_K),
			BPF_STMT(BPF_RET | BPF_K, 1)
		},
		CLASSIC,
		{ },
		{ { 1, 0 }, { 10, 0 }, { 60, 0 } },
	},
	{
		"LD_ABS",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 1000),
			BPF_STMT(BPF_RET | BPF_K, 1)
		},
		CLASSIC,
		{ },
		{ { 1, 0 }, { 10, 0 }, { 60, 0 } },
	},
	{
		"LD_ABS_LL",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_LL_OFF),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_LL_OFF + 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 1, 2, 3 },
		{ { 1, 0 }, { 2, 3 } },
	},
	{
		"LD_IND_LL",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, SKF_LL_OFF - 1),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 1, 2, 3, 0xff },
		{ { 1, 1 }, { 3, 3 }, { 4, 0xff } },
	},
	{
		"LD_ABS_NET",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF + 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3 },
		{ { 15, 0 }, { 16, 3 } },
	},
	{
		"LD_IND_NET",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, SKF_NET_OFF - 15),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3 },
		{ { 14, 0 }, { 15, 1 }, { 17, 3 } },
	},
	{
		"LD_PKTTYPE",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PKTTYPE),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SKB_TYPE, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, 3 }, { 10, 3 } },
	},
	{
		"LD_MARK",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_MARK),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, SKB_MARK}, { 10, SKB_MARK} },
	},
	{
		"LD_RXHASH",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_RXHASH),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, SKB_HASH}, { 10, SKB_HASH} },
	},
	{
		"LD_QUEUE",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_QUEUE),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, SKB_QUEUE_MAP }, { 10, SKB_QUEUE_MAP } },
	},
	{
		"LD_PROTOCOL",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 1),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 20, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PROTOCOL),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 30, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ 10, 20, 30 },
		{ { 10, ETH_P_IP }, { 100, ETH_P_IP } },
	},
	{
		"LD_VLAN_TAG",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_VLAN_TAG),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{
			{ 1, SKB_VLAN_TCI & ~VLAN_TAG_PRESENT },
			{ 10, SKB_VLAN_TCI & ~VLAN_TAG_PRESENT }
		},
	},
	{
		"LD_VLAN_TAG_PRESENT",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_VLAN_TAG_PRESENT),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{
			{ 1, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) },
			{ 10, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) }
		},
	},
	{
		"LD_IFINDEX",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_IFINDEX),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, SKB_DEV_IFINDEX }, { 10, SKB_DEV_IFINDEX } },
	},
	{
		"LD_HATYPE",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_HATYPE),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, SKB_DEV_TYPE }, { 10, SKB_DEV_TYPE } },
	},
	{
		"LD_CPU",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_CPU),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_CPU),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, 0 }, { 10, 0 } },
	},
	{
		"LD_NLATTR",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 2),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
#ifdef __BIG_ENDIAN
		{ 0xff, 0xff, 0, 4, 0, 2, 0, 4, 0, 3 },
#else
		{ 0xff, 0xff, 4, 0, 2, 0, 4, 0, 3, 0 },
#endif
		{ { 4, 0 }, { 20, 6 } },
	},
	{
		"LD_NLATTR_NEST",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LDX | BPF_IMM, 3),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_NLATTR_NEST),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
#ifdef __BIG_ENDIAN
		{ 0xff, 0xff, 0, 12, 0, 1, 0, 4, 0, 2, 0, 4, 0, 3 },
#else
		{ 0xff, 0xff, 12, 0, 1, 0, 4, 0, 2, 0, 4, 0, 3, 0 },
#endif
		{ { 4, 0 }, { 20, 10 } },
	},
	{
		"LD_PAYLOAD_OFF",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_PAY_OFFSET),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		/* 00:00:00:00:00:00 > 00:00:00:00:00:00, ethtype IPv4 (0x0800),
		 * length 98: 127.0.0.1 > 127.0.0.1: ICMP echo request,
		 * id 9737, seq 1, length 64
		 */
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x08, 0x00,
		  0x45, 0x00, 0x00, 0x54, 0xac, 0x8b, 0x40, 0x00, 0x40,
		  0x01, 0x90, 0x1b, 0x7f, 0x00, 0x00, 0x01 },
		{ { 30, 0 }, { 100, 42 } },
	},
	{
		"LD_ANC_XOR",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 10),
			BPF_STMT(BPF_LDX | BPF_IMM, 300),
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_ALU_XOR_X),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 4, 10 ^ 300 }, { 20, 10 ^ 300 } },
	},
	{
		"SPILL_FILL",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_IMM, 2),
			BPF_STMT(BPF_ALU | BPF_RSH, 1),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_ST, 1), /* M1 = 1 ^ len */
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_K, 0x80000000),
			BPF_STMT(BPF_ST, 2), /* M2 = 1 ^ len ^ 0x80000000 */
			BPF_STMT(BPF_STX, 15), /* M3 = len */
			BPF_STMT(BPF_LDX | BPF_MEM, 1),
			BPF_STMT(BPF_LD | BPF_MEM, 2),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 15),
			BPF_STMT(BPF_ALU | BPF_XOR | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ { 1, 0x80000001 }, { 2, 0x80000002 }, { 60, 0x80000000 ^ 60 } }
	},
	{
		"JEQ",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 3, 3, 3, 3, 3 },
		{ { 1, 0 }, { 3, 1 }, { 4, MAX_K } },
	},
	{
		"JGT",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JGT | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 4, 4, 4, 3, 3 },
		{ { 2, 0 }, { 3, 1 }, { 4, MAX_K } },
	},
	{
		"JGE (jt 0), test 1",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 4, 4, 4, 3, 3 },
		{ { 2, 0 }, { 3, 1 }, { 4, 1 } },
	},
	{
		"JGE (jt 0), test 2",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 4, 4, 5, 3, 3 },
		{ { 4, 1 }, { 5, 1 }, { 6, MAX_K } },
	},
	{
		"JGE",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, MAX_K),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 1, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 10),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 2, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 20),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 3, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 4, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 40),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 1, 2, 3, 4, 5 },
		{ { 1, 20 }, { 3, 40 }, { 5, MAX_K } },
	},
	{
		"JSET",
		.u.insns = {
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_JUMP(BPF_JMP | BPF_JA, 1, 1, 1),
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_JUMP(BPF_JMP | BPF_JA, 0, 0, 0),
			BPF_STMT(BPF_LDX | BPF_LEN, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_K, 4),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_LD | BPF_W | BPF_IND, 0),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 1, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 10),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x80000000, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 20),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0xffffff, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 30),
			BPF_STMT(BPF_RET | BPF_K, MAX_K)
		},
		CLASSIC,
		{ 0, 0xAA, 0x55, 1 },
		{ { 4, 10 }, { 5, 20 }, { 6, MAX_K } },
	},
	{
		"tcpdump port 22",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x86dd, 0, 8), /* IPv6 */
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 20),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x84, 2, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x6, 1, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x11, 0, 17),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 54),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 14, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 56),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 12, 13),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0800, 0, 12), /* IPv4 */
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x84, 2, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x6, 1, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x11, 0, 8),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x1fff, 6, 0),
			BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, 14),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 2, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 0xffff),
			BPF_STMT(BPF_RET | BPF_K, 0),
		},
		CLASSIC,
		/* 3c:07:54:43:e5:76 > 10:bf:48:d6:43:d6, ethertype IPv4(0x0800)
		 * length 114: 10.1.1.149.49700 > 10.1.2.10.22: Flags [P.],
		 * seq 1305692979:1305693027, ack 3650467037, win 65535,
		 * options [nop,nop,TS val 2502645400 ecr 3971138], length 48
		 */
		{ 0x10, 0xbf, 0x48, 0xd6, 0x43, 0xd6,
		  0x3c, 0x07, 0x54, 0x43, 0xe5, 0x76,
		  0x08, 0x00,
		  0x45, 0x10, 0x00, 0x64, 0x75, 0xb5,
		  0x40, 0x00, 0x40, 0x06, 0xad, 0x2e, /* IP header */
		  0x0a, 0x01, 0x01, 0x95, /* ip src */
		  0x0a, 0x01, 0x02, 0x0a, /* ip dst */
		  0xc2, 0x24,
		  0x00, 0x16 /* dst port */ },
		{ { 10, 0 }, { 30, 0 }, { 100, 65535 } },
	},
	{
		"tcpdump complex",
		.u.insns = {
			/* tcpdump -nei eth0 'tcp port 22 and (((ip[2:2] -
			 * ((ip[0]&0xf)<<2)) - ((tcp[12]&0xf0)>>2)) != 0) and
			 * (len > 115 or len < 30000000000)' -d
			 */
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x86dd, 30, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x800, 0, 29),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x6, 0, 27),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
			BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x1fff, 25, 0),
			BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, 14),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 2, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 22, 0, 20),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 16),
			BPF_STMT(BPF_ST, 1),
			BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 14),
			BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf),
			BPF_STMT(BPF_ALU | BPF_LSH | BPF_K, 2),
			BPF_STMT(BPF_MISC | BPF_TAX, 0x5), /* libpcap emits K on TAX */
			BPF_STMT(BPF_LD | BPF_MEM, 1),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_ST, 5),
			BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),
			BPF_STMT(BPF_LD | BPF_B | BPF_IND, 26),
			BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf0),
			BPF_STMT(BPF_ALU | BPF_RSH | BPF_K, 2),
			BPF_STMT(BPF_MISC | BPF_TAX, 0x9), /* libpcap emits K on TAX */
			BPF_STMT(BPF_LD | BPF_MEM, 5),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_X, 0, 4, 0),
			BPF_STMT(BPF_LD | BPF_LEN, 0),
			BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, 0x73, 1, 0),
			BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0xfc23ac00, 1, 0),
			BPF_STMT(BPF_RET | BPF_K, 0xffff),
			BPF_STMT(BPF_RET | BPF_K, 0),
		},
		CLASSIC,
		{ 0x10, 0xbf, 0x48, 0xd6, 0x43, 0xd6,
		  0x3c, 0x07, 0x54, 0x43, 0xe5, 0x76,
		  0x08, 0x00,
		  0x45, 0x10, 0x00, 0x64, 0x75, 0xb5,
		  0x40, 0x00, 0x40, 0x06, 0xad, 0x2e, /* IP header */
		  0x0a, 0x01, 0x01, 0x95, /* ip src */
		  0x0a, 0x01, 0x02, 0x0a, /* ip dst */
		  0xc2, 0x24,
		  0x00, 0x16 /* dst port */ },
		{ { 10, 0 }, { 30, 0 }, { 100, 65535 } },
	},
	{
		"RET_A",
		.u.insns = {
			/* check that unitialized X and A contain zeros */
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_RET | BPF_A, 0)
		},
		CLASSIC,
		{ },
		{ {1, 0}, {2, 0} },
	},
	{
		"INT: ADD trivial",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_ADD, R1, 2),
			BPF_ALU64_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_SUB, R1, R2),
			BPF_ALU64_IMM(BPF_ADD, R1, -1),
			BPF_ALU64_IMM(BPF_MUL, R1, 3),
			BPF_ALU64_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfffffffd } }
	},
	{
		"INT: MUL_X",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU64_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xfffffffd, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } }
	},
	{
		"INT: MUL_X2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, -1),
			BPF_ALU32_IMM(BPF_MOV, R1, -1),
			BPF_ALU32_IMM(BPF_MOV, R2, 3),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_ALU64_IMM(BPF_RSH, R1, 8),
			BPF_JMP_IMM(BPF_JEQ, R1, 0x2ffffff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } }
	},
	{
		"INT: MUL32_X",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU32_IMM(BPF_MOV, R2, 3),
			BPF_ALU32_REG(BPF_MUL, R1, R2),
			BPF_ALU64_IMM(BPF_RSH, R1, 8),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xffffff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } }
	},
	{
		/* Have to test all register combinations, since
		 * JITing of different registers will produce
		 * different asm code.
		 */
		"INT: ADD 64-bit",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_IMM(BPF_ADD, R0, 20),
			BPF_ALU64_IMM(BPF_ADD, R1, 20),
			BPF_ALU64_IMM(BPF_ADD, R2, 20),
			BPF_ALU64_IMM(BPF_ADD, R3, 20),
			BPF_ALU64_IMM(BPF_ADD, R4, 20),
			BPF_ALU64_IMM(BPF_ADD, R5, 20),
			BPF_ALU64_IMM(BPF_ADD, R6, 20),
			BPF_ALU64_IMM(BPF_ADD, R7, 20),
			BPF_ALU64_IMM(BPF_ADD, R8, 20),
			BPF_ALU64_IMM(BPF_ADD, R9, 20),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_ALU64_IMM(BPF_SUB, R1, 10),
			BPF_ALU64_IMM(BPF_SUB, R2, 10),
			BPF_ALU64_IMM(BPF_SUB, R3, 10),
			BPF_ALU64_IMM(BPF_SUB, R4, 10),
			BPF_ALU64_IMM(BPF_SUB, R5, 10),
			BPF_ALU64_IMM(BPF_SUB, R6, 10),
			BPF_ALU64_IMM(BPF_SUB, R7, 10),
			BPF_ALU64_IMM(BPF_SUB, R8, 10),
			BPF_ALU64_IMM(BPF_SUB, R9, 10),
			BPF_ALU64_REG(BPF_ADD, R0, R0),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_ALU64_REG(BPF_ADD, R0, R2),
			BPF_ALU64_REG(BPF_ADD, R0, R3),
			BPF_ALU64_REG(BPF_ADD, R0, R4),
			BPF_ALU64_REG(BPF_ADD, R0, R5),
			BPF_ALU64_REG(BPF_ADD, R0, R6),
			BPF_ALU64_REG(BPF_ADD, R0, R7),
			BPF_ALU64_REG(BPF_ADD, R0, R8),
			BPF_ALU64_REG(BPF_ADD, R0, R9), /* R0 == 155 */
			BPF_JMP_IMM(BPF_JEQ, R0, 155, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R1, R0),
			BPF_ALU64_REG(BPF_ADD, R1, R1),
			BPF_ALU64_REG(BPF_ADD, R1, R2),
			BPF_ALU64_REG(BPF_ADD, R1, R3),
			BPF_ALU64_REG(BPF_ADD, R1, R4),
			BPF_ALU64_REG(BPF_ADD, R1, R5),
			BPF_ALU64_REG(BPF_ADD, R1, R6),
			BPF_ALU64_REG(BPF_ADD, R1, R7),
			BPF_ALU64_REG(BPF_ADD, R1, R8),
			BPF_ALU64_REG(BPF_ADD, R1, R9), /* R1 == 456 */
			BPF_JMP_IMM(BPF_JEQ, R1, 456, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R2, R0),
			BPF_ALU64_REG(BPF_ADD, R2, R1),
			BPF_ALU64_REG(BPF_ADD, R2, R2),
			BPF_ALU64_REG(BPF_ADD, R2, R3),
			BPF_ALU64_REG(BPF_ADD, R2, R4),
			BPF_ALU64_REG(BPF_ADD, R2, R5),
			BPF_ALU64_REG(BPF_ADD, R2, R6),
			BPF_ALU64_REG(BPF_ADD, R2, R7),
			BPF_ALU64_REG(BPF_ADD, R2, R8),
			BPF_ALU64_REG(BPF_ADD, R2, R9), /* R2 == 1358 */
			BPF_JMP_IMM(BPF_JEQ, R2, 1358, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R3, R0),
			BPF_ALU64_REG(BPF_ADD, R3, R1),
			BPF_ALU64_REG(BPF_ADD, R3, R2),
			BPF_ALU64_REG(BPF_ADD, R3, R3),
			BPF_ALU64_REG(BPF_ADD, R3, R4),
			BPF_ALU64_REG(BPF_ADD, R3, R5),
			BPF_ALU64_REG(BPF_ADD, R3, R6),
			BPF_ALU64_REG(BPF_ADD, R3, R7),
			BPF_ALU64_REG(BPF_ADD, R3, R8),
			BPF_ALU64_REG(BPF_ADD, R3, R9), /* R3 == 4063 */
			BPF_JMP_IMM(BPF_JEQ, R3, 4063, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R4, R0),
			BPF_ALU64_REG(BPF_ADD, R4, R1),
			BPF_ALU64_REG(BPF_ADD, R4, R2),
			BPF_ALU64_REG(BPF_ADD, R4, R3),
			BPF_ALU64_REG(BPF_ADD, R4, R4),
			BPF_ALU64_REG(BPF_ADD, R4, R5),
			BPF_ALU64_REG(BPF_ADD, R4, R6),
			BPF_ALU64_REG(BPF_ADD, R4, R7),
			BPF_ALU64_REG(BPF_ADD, R4, R8),
			BPF_ALU64_REG(BPF_ADD, R4, R9), /* R4 == 12177 */
			BPF_JMP_IMM(BPF_JEQ, R4, 12177, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R5, R0),
			BPF_ALU64_REG(BPF_ADD, R5, R1),
			BPF_ALU64_REG(BPF_ADD, R5, R2),
			BPF_ALU64_REG(BPF_ADD, R5, R3),
			BPF_ALU64_REG(BPF_ADD, R5, R4),
			BPF_ALU64_REG(BPF_ADD, R5, R5),
			BPF_ALU64_REG(BPF_ADD, R5, R6),
			BPF_ALU64_REG(BPF_ADD, R5, R7),
			BPF_ALU64_REG(BPF_ADD, R5, R8),
			BPF_ALU64_REG(BPF_ADD, R5, R9), /* R5 == 36518 */
			BPF_JMP_IMM(BPF_JEQ, R5, 36518, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R6, R0),
			BPF_ALU64_REG(BPF_ADD, R6, R1),
			BPF_ALU64_REG(BPF_ADD, R6, R2),
			BPF_ALU64_REG(BPF_ADD, R6, R3),
			BPF_ALU64_REG(BPF_ADD, R6, R4),
			BPF_ALU64_REG(BPF_ADD, R6, R5),
			BPF_ALU64_REG(BPF_ADD, R6, R6),
			BPF_ALU64_REG(BPF_ADD, R6, R7),
			BPF_ALU64_REG(BPF_ADD, R6, R8),
			BPF_ALU64_REG(BPF_ADD, R6, R9), /* R6 == 109540 */
			BPF_JMP_IMM(BPF_JEQ, R6, 109540, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R7, R0),
			BPF_ALU64_REG(BPF_ADD, R7, R1),
			BPF_ALU64_REG(BPF_ADD, R7, R2),
			BPF_ALU64_REG(BPF_ADD, R7, R3),
			BPF_ALU64_REG(BPF_ADD, R7, R4),
			BPF_ALU64_REG(BPF_ADD, R7, R5),
			BPF_ALU64_REG(BPF_ADD, R7, R6),
			BPF_ALU64_REG(BPF_ADD, R7, R7),
			BPF_ALU64_REG(BPF_ADD, R7, R8),
			BPF_ALU64_REG(BPF_ADD, R7, R9), /* R7 == 328605 */
			BPF_JMP_IMM(BPF_JEQ, R7, 328605, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R8, R0),
			BPF_ALU64_REG(BPF_ADD, R8, R1),
			BPF_ALU64_REG(BPF_ADD, R8, R2),
			BPF_ALU64_REG(BPF_ADD, R8, R3),
			BPF_ALU64_REG(BPF_ADD, R8, R4),
			BPF_ALU64_REG(BPF_ADD, R8, R5),
			BPF_ALU64_REG(BPF_ADD, R8, R6),
			BPF_ALU64_REG(BPF_ADD, R8, R7),
			BPF_ALU64_REG(BPF_ADD, R8, R8),
			BPF_ALU64_REG(BPF_ADD, R8, R9), /* R8 == 985799 */
			BPF_JMP_IMM(BPF_JEQ, R8, 985799, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_ADD, R9, R0),
			BPF_ALU64_REG(BPF_ADD, R9, R1),
			BPF_ALU64_REG(BPF_ADD, R9, R2),
			BPF_ALU64_REG(BPF_ADD, R9, R3),
			BPF_ALU64_REG(BPF_ADD, R9, R4),
			BPF_ALU64_REG(BPF_ADD, R9, R5),
			BPF_ALU64_REG(BPF_ADD, R9, R6),
			BPF_ALU64_REG(BPF_ADD, R9, R7),
			BPF_ALU64_REG(BPF_ADD, R9, R8),
			BPF_ALU64_REG(BPF_ADD, R9, R9), /* R9 == 2957380 */
			BPF_ALU64_REG(BPF_MOV, R0, R9),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2957380 } }
	},
	{
		"INT: ADD 32-bit",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 20),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU32_IMM(BPF_MOV, R2, 2),
			BPF_ALU32_IMM(BPF_MOV, R3, 3),
			BPF_ALU32_IMM(BPF_MOV, R4, 4),
			BPF_ALU32_IMM(BPF_MOV, R5, 5),
			BPF_ALU32_IMM(BPF_MOV, R6, 6),
			BPF_ALU32_IMM(BPF_MOV, R7, 7),
			BPF_ALU32_IMM(BPF_MOV, R8, 8),
			BPF_ALU32_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_IMM(BPF_ADD, R1, 10),
			BPF_ALU64_IMM(BPF_ADD, R2, 10),
			BPF_ALU64_IMM(BPF_ADD, R3, 10),
			BPF_ALU64_IMM(BPF_ADD, R4, 10),
			BPF_ALU64_IMM(BPF_ADD, R5, 10),
			BPF_ALU64_IMM(BPF_ADD, R6, 10),
			BPF_ALU64_IMM(BPF_ADD, R7, 10),
			BPF_ALU64_IMM(BPF_ADD, R8, 10),
			BPF_ALU64_IMM(BPF_ADD, R9, 10),
			BPF_ALU32_REG(BPF_ADD, R0, R1),
			BPF_ALU32_REG(BPF_ADD, R0, R2),
			BPF_ALU32_REG(BPF_ADD, R0, R3),
			BPF_ALU32_REG(BPF_ADD, R0, R4),
			BPF_ALU32_REG(BPF_ADD, R0, R5),
			BPF_ALU32_REG(BPF_ADD, R0, R6),
			BPF_ALU32_REG(BPF_ADD, R0, R7),
			BPF_ALU32_REG(BPF_ADD, R0, R8),
			BPF_ALU32_REG(BPF_ADD, R0, R9), /* R0 == 155 */
			BPF_JMP_IMM(BPF_JEQ, R0, 155, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R1, R0),
			BPF_ALU32_REG(BPF_ADD, R1, R1),
			BPF_ALU32_REG(BPF_ADD, R1, R2),
			BPF_ALU32_REG(BPF_ADD, R1, R3),
			BPF_ALU32_REG(BPF_ADD, R1, R4),
			BPF_ALU32_REG(BPF_ADD, R1, R5),
			BPF_ALU32_REG(BPF_ADD, R1, R6),
			BPF_ALU32_REG(BPF_ADD, R1, R7),
			BPF_ALU32_REG(BPF_ADD, R1, R8),
			BPF_ALU32_REG(BPF_ADD, R1, R9), /* R1 == 456 */
			BPF_JMP_IMM(BPF_JEQ, R1, 456, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R2, R0),
			BPF_ALU32_REG(BPF_ADD, R2, R1),
			BPF_ALU32_REG(BPF_ADD, R2, R2),
			BPF_ALU32_REG(BPF_ADD, R2, R3),
			BPF_ALU32_REG(BPF_ADD, R2, R4),
			BPF_ALU32_REG(BPF_ADD, R2, R5),
			BPF_ALU32_REG(BPF_ADD, R2, R6),
			BPF_ALU32_REG(BPF_ADD, R2, R7),
			BPF_ALU32_REG(BPF_ADD, R2, R8),
			BPF_ALU32_REG(BPF_ADD, R2, R9), /* R2 == 1358 */
			BPF_JMP_IMM(BPF_JEQ, R2, 1358, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R3, R0),
			BPF_ALU32_REG(BPF_ADD, R3, R1),
			BPF_ALU32_REG(BPF_ADD, R3, R2),
			BPF_ALU32_REG(BPF_ADD, R3, R3),
			BPF_ALU32_REG(BPF_ADD, R3, R4),
			BPF_ALU32_REG(BPF_ADD, R3, R5),
			BPF_ALU32_REG(BPF_ADD, R3, R6),
			BPF_ALU32_REG(BPF_ADD, R3, R7),
			BPF_ALU32_REG(BPF_ADD, R3, R8),
			BPF_ALU32_REG(BPF_ADD, R3, R9), /* R3 == 4063 */
			BPF_JMP_IMM(BPF_JEQ, R3, 4063, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R4, R0),
			BPF_ALU32_REG(BPF_ADD, R4, R1),
			BPF_ALU32_REG(BPF_ADD, R4, R2),
			BPF_ALU32_REG(BPF_ADD, R4, R3),
			BPF_ALU32_REG(BPF_ADD, R4, R4),
			BPF_ALU32_REG(BPF_ADD, R4, R5),
			BPF_ALU32_REG(BPF_ADD, R4, R6),
			BPF_ALU32_REG(BPF_ADD, R4, R7),
			BPF_ALU32_REG(BPF_ADD, R4, R8),
			BPF_ALU32_REG(BPF_ADD, R4, R9), /* R4 == 12177 */
			BPF_JMP_IMM(BPF_JEQ, R4, 12177, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R5, R0),
			BPF_ALU32_REG(BPF_ADD, R5, R1),
			BPF_ALU32_REG(BPF_ADD, R5, R2),
			BPF_ALU32_REG(BPF_ADD, R5, R3),
			BPF_ALU32_REG(BPF_ADD, R5, R4),
			BPF_ALU32_REG(BPF_ADD, R5, R5),
			BPF_ALU32_REG(BPF_ADD, R5, R6),
			BPF_ALU32_REG(BPF_ADD, R5, R7),
			BPF_ALU32_REG(BPF_ADD, R5, R8),
			BPF_ALU32_REG(BPF_ADD, R5, R9), /* R5 == 36518 */
			BPF_JMP_IMM(BPF_JEQ, R5, 36518, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R6, R0),
			BPF_ALU32_REG(BPF_ADD, R6, R1),
			BPF_ALU32_REG(BPF_ADD, R6, R2),
			BPF_ALU32_REG(BPF_ADD, R6, R3),
			BPF_ALU32_REG(BPF_ADD, R6, R4),
			BPF_ALU32_REG(BPF_ADD, R6, R5),
			BPF_ALU32_REG(BPF_ADD, R6, R6),
			BPF_ALU32_REG(BPF_ADD, R6, R7),
			BPF_ALU32_REG(BPF_ADD, R6, R8),
			BPF_ALU32_REG(BPF_ADD, R6, R9), /* R6 == 109540 */
			BPF_JMP_IMM(BPF_JEQ, R6, 109540, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R7, R0),
			BPF_ALU32_REG(BPF_ADD, R7, R1),
			BPF_ALU32_REG(BPF_ADD, R7, R2),
			BPF_ALU32_REG(BPF_ADD, R7, R3),
			BPF_ALU32_REG(BPF_ADD, R7, R4),
			BPF_ALU32_REG(BPF_ADD, R7, R5),
			BPF_ALU32_REG(BPF_ADD, R7, R6),
			BPF_ALU32_REG(BPF_ADD, R7, R7),
			BPF_ALU32_REG(BPF_ADD, R7, R8),
			BPF_ALU32_REG(BPF_ADD, R7, R9), /* R7 == 328605 */
			BPF_JMP_IMM(BPF_JEQ, R7, 328605, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R8, R0),
			BPF_ALU32_REG(BPF_ADD, R8, R1),
			BPF_ALU32_REG(BPF_ADD, R8, R2),
			BPF_ALU32_REG(BPF_ADD, R8, R3),
			BPF_ALU32_REG(BPF_ADD, R8, R4),
			BPF_ALU32_REG(BPF_ADD, R8, R5),
			BPF_ALU32_REG(BPF_ADD, R8, R6),
			BPF_ALU32_REG(BPF_ADD, R8, R7),
			BPF_ALU32_REG(BPF_ADD, R8, R8),
			BPF_ALU32_REG(BPF_ADD, R8, R9), /* R8 == 985799 */
			BPF_JMP_IMM(BPF_JEQ, R8, 985799, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_REG(BPF_ADD, R9, R0),
			BPF_ALU32_REG(BPF_ADD, R9, R1),
			BPF_ALU32_REG(BPF_ADD, R9, R2),
			BPF_ALU32_REG(BPF_ADD, R9, R3),
			BPF_ALU32_REG(BPF_ADD, R9, R4),
			BPF_ALU32_REG(BPF_ADD, R9, R5),
			BPF_ALU32_REG(BPF_ADD, R9, R6),
			BPF_ALU32_REG(BPF_ADD, R9, R7),
			BPF_ALU32_REG(BPF_ADD, R9, R8),
			BPF_ALU32_REG(BPF_ADD, R9, R9), /* R9 == 2957380 */
			BPF_ALU32_REG(BPF_MOV, R0, R9),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2957380 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: SUB",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_REG(BPF_SUB, R0, R0),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_ALU64_REG(BPF_SUB, R0, R2),
			BPF_ALU64_REG(BPF_SUB, R0, R3),
			BPF_ALU64_REG(BPF_SUB, R0, R4),
			BPF_ALU64_REG(BPF_SUB, R0, R5),
			BPF_ALU64_REG(BPF_SUB, R0, R6),
			BPF_ALU64_REG(BPF_SUB, R0, R7),
			BPF_ALU64_REG(BPF_SUB, R0, R8),
			BPF_ALU64_REG(BPF_SUB, R0, R9),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_JMP_IMM(BPF_JEQ, R0, -55, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R1, R0),
			BPF_ALU64_REG(BPF_SUB, R1, R2),
			BPF_ALU64_REG(BPF_SUB, R1, R3),
			BPF_ALU64_REG(BPF_SUB, R1, R4),
			BPF_ALU64_REG(BPF_SUB, R1, R5),
			BPF_ALU64_REG(BPF_SUB, R1, R6),
			BPF_ALU64_REG(BPF_SUB, R1, R7),
			BPF_ALU64_REG(BPF_SUB, R1, R8),
			BPF_ALU64_REG(BPF_SUB, R1, R9),
			BPF_ALU64_IMM(BPF_SUB, R1, 10),
			BPF_ALU64_REG(BPF_SUB, R2, R0),
			BPF_ALU64_REG(BPF_SUB, R2, R1),
			BPF_ALU64_REG(BPF_SUB, R2, R3),
			BPF_ALU64_REG(BPF_SUB, R2, R4),
			BPF_ALU64_REG(BPF_SUB, R2, R5),
			BPF_ALU64_REG(BPF_SUB, R2, R6),
			BPF_ALU64_REG(BPF_SUB, R2, R7),
			BPF_ALU64_REG(BPF_SUB, R2, R8),
			BPF_ALU64_REG(BPF_SUB, R2, R9),
			BPF_ALU64_IMM(BPF_SUB, R2, 10),
			BPF_ALU64_REG(BPF_SUB, R3, R0),
			BPF_ALU64_REG(BPF_SUB, R3, R1),
			BPF_ALU64_REG(BPF_SUB, R3, R2),
			BPF_ALU64_REG(BPF_SUB, R3, R4),
			BPF_ALU64_REG(BPF_SUB, R3, R5),
			BPF_ALU64_REG(BPF_SUB, R3, R6),
			BPF_ALU64_REG(BPF_SUB, R3, R7),
			BPF_ALU64_REG(BPF_SUB, R3, R8),
			BPF_ALU64_REG(BPF_SUB, R3, R9),
			BPF_ALU64_IMM(BPF_SUB, R3, 10),
			BPF_ALU64_REG(BPF_SUB, R4, R0),
			BPF_ALU64_REG(BPF_SUB, R4, R1),
			BPF_ALU64_REG(BPF_SUB, R4, R2),
			BPF_ALU64_REG(BPF_SUB, R4, R3),
			BPF_ALU64_REG(BPF_SUB, R4, R5),
			BPF_ALU64_REG(BPF_SUB, R4, R6),
			BPF_ALU64_REG(BPF_SUB, R4, R7),
			BPF_ALU64_REG(BPF_SUB, R4, R8),
			BPF_ALU64_REG(BPF_SUB, R4, R9),
			BPF_ALU64_IMM(BPF_SUB, R4, 10),
			BPF_ALU64_REG(BPF_SUB, R5, R0),
			BPF_ALU64_REG(BPF_SUB, R5, R1),
			BPF_ALU64_REG(BPF_SUB, R5, R2),
			BPF_ALU64_REG(BPF_SUB, R5, R3),
			BPF_ALU64_REG(BPF_SUB, R5, R4),
			BPF_ALU64_REG(BPF_SUB, R5, R6),
			BPF_ALU64_REG(BPF_SUB, R5, R7),
			BPF_ALU64_REG(BPF_SUB, R5, R8),
			BPF_ALU64_REG(BPF_SUB, R5, R9),
			BPF_ALU64_IMM(BPF_SUB, R5, 10),
			BPF_ALU64_REG(BPF_SUB, R6, R0),
			BPF_ALU64_REG(BPF_SUB, R6, R1),
			BPF_ALU64_REG(BPF_SUB, R6, R2),
			BPF_ALU64_REG(BPF_SUB, R6, R3),
			BPF_ALU64_REG(BPF_SUB, R6, R4),
			BPF_ALU64_REG(BPF_SUB, R6, R5),
			BPF_ALU64_REG(BPF_SUB, R6, R7),
			BPF_ALU64_REG(BPF_SUB, R6, R8),
			BPF_ALU64_REG(BPF_SUB, R6, R9),
			BPF_ALU64_IMM(BPF_SUB, R6, 10),
			BPF_ALU64_REG(BPF_SUB, R7, R0),
			BPF_ALU64_REG(BPF_SUB, R7, R1),
			BPF_ALU64_REG(BPF_SUB, R7, R2),
			BPF_ALU64_REG(BPF_SUB, R7, R3),
			BPF_ALU64_REG(BPF_SUB, R7, R4),
			BPF_ALU64_REG(BPF_SUB, R7, R5),
			BPF_ALU64_REG(BPF_SUB, R7, R6),
			BPF_ALU64_REG(BPF_SUB, R7, R8),
			BPF_ALU64_REG(BPF_SUB, R7, R9),
			BPF_ALU64_IMM(BPF_SUB, R7, 10),
			BPF_ALU64_REG(BPF_SUB, R8, R0),
			BPF_ALU64_REG(BPF_SUB, R8, R1),
			BPF_ALU64_REG(BPF_SUB, R8, R2),
			BPF_ALU64_REG(BPF_SUB, R8, R3),
			BPF_ALU64_REG(BPF_SUB, R8, R4),
			BPF_ALU64_REG(BPF_SUB, R8, R5),
			BPF_ALU64_REG(BPF_SUB, R8, R6),
			BPF_ALU64_REG(BPF_SUB, R8, R7),
			BPF_ALU64_REG(BPF_SUB, R8, R9),
			BPF_ALU64_IMM(BPF_SUB, R8, 10),
			BPF_ALU64_REG(BPF_SUB, R9, R0),
			BPF_ALU64_REG(BPF_SUB, R9, R1),
			BPF_ALU64_REG(BPF_SUB, R9, R2),
			BPF_ALU64_REG(BPF_SUB, R9, R3),
			BPF_ALU64_REG(BPF_SUB, R9, R4),
			BPF_ALU64_REG(BPF_SUB, R9, R5),
			BPF_ALU64_REG(BPF_SUB, R9, R6),
			BPF_ALU64_REG(BPF_SUB, R9, R7),
			BPF_ALU64_REG(BPF_SUB, R9, R8),
			BPF_ALU64_IMM(BPF_SUB, R9, 10),
			BPF_ALU64_IMM(BPF_SUB, R0, 10),
			BPF_ALU64_IMM(BPF_NEG, R0, 0),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_ALU64_REG(BPF_SUB, R0, R2),
			BPF_ALU64_REG(BPF_SUB, R0, R3),
			BPF_ALU64_REG(BPF_SUB, R0, R4),
			BPF_ALU64_REG(BPF_SUB, R0, R5),
			BPF_ALU64_REG(BPF_SUB, R0, R6),
			BPF_ALU64_REG(BPF_SUB, R0, R7),
			BPF_ALU64_REG(BPF_SUB, R0, R8),
			BPF_ALU64_REG(BPF_SUB, R0, R9),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 11 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: XOR",
		.u.insns_int = {
			BPF_ALU64_REG(BPF_SUB, R0, R0),
			BPF_ALU64_REG(BPF_XOR, R1, R1),
			BPF_JMP_REG(BPF_JEQ, R0, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 10),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_ALU64_REG(BPF_SUB, R1, R1),
			BPF_ALU64_REG(BPF_XOR, R2, R2),
			BPF_JMP_REG(BPF_JEQ, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R2, R2),
			BPF_ALU64_REG(BPF_XOR, R3, R3),
			BPF_ALU64_IMM(BPF_MOV, R0, 10),
			BPF_ALU64_IMM(BPF_MOV, R1, -1),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R3, R3),
			BPF_ALU64_REG(BPF_XOR, R4, R4),
			BPF_ALU64_IMM(BPF_MOV, R2, 1),
			BPF_ALU64_IMM(BPF_MOV, R5, -1),
			BPF_JMP_REG(BPF_JEQ, R3, R4, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R4, R4),
			BPF_ALU64_REG(BPF_XOR, R5, R5),
			BPF_ALU64_IMM(BPF_MOV, R3, 1),
			BPF_ALU64_IMM(BPF_MOV, R7, -1),
			BPF_JMP_REG(BPF_JEQ, R5, R4, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R5, 1),
			BPF_ALU64_REG(BPF_SUB, R5, R5),
			BPF_ALU64_REG(BPF_XOR, R6, R6),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R8, -1),
			BPF_JMP_REG(BPF_JEQ, R5, R6, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R6, R6),
			BPF_ALU64_REG(BPF_XOR, R7, R7),
			BPF_JMP_REG(BPF_JEQ, R7, R6, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R7, R7),
			BPF_ALU64_REG(BPF_XOR, R8, R8),
			BPF_JMP_REG(BPF_JEQ, R7, R8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R8, R8),
			BPF_ALU64_REG(BPF_XOR, R9, R9),
			BPF_JMP_REG(BPF_JEQ, R9, R8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R9, R9),
			BPF_ALU64_REG(BPF_XOR, R0, R0),
			BPF_JMP_REG(BPF_JEQ, R9, R0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, R1, R1),
			BPF_ALU64_REG(BPF_XOR, R0, R0),
			BPF_JMP_REG(BPF_JEQ, R9, R0, 2),
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } }
	},
	{	/* Mainly checking JIT here. */
		"INT: MUL",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 11),
			BPF_ALU64_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_MOV, R3, 3),
			BPF_ALU64_IMM(BPF_MOV, R4, 4),
			BPF_ALU64_IMM(BPF_MOV, R5, 5),
			BPF_ALU64_IMM(BPF_MOV, R6, 6),
			BPF_ALU64_IMM(BPF_MOV, R7, 7),
			BPF_ALU64_IMM(BPF_MOV, R8, 8),
			BPF_ALU64_IMM(BPF_MOV, R9, 9),
			BPF_ALU64_REG(BPF_MUL, R0, R0),
			BPF_ALU64_REG(BPF_MUL, R0, R1),
			BPF_ALU64_REG(BPF_MUL, R0, R2),
			BPF_ALU64_REG(BPF_MUL, R0, R3),
			BPF_ALU64_REG(BPF_MUL, R0, R4),
			BPF_ALU64_REG(BPF_MUL, R0, R5),
			BPF_ALU64_REG(BPF_MUL, R0, R6),
			BPF_ALU64_REG(BPF_MUL, R0, R7),
			BPF_ALU64_REG(BPF_MUL, R0, R8),
			BPF_ALU64_REG(BPF_MUL, R0, R9),
			BPF_ALU64_IMM(BPF_MUL, R0, 10),
			BPF_JMP_IMM(BPF_JEQ, R0, 439084800, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_MUL, R1, R0),
			BPF_ALU64_REG(BPF_MUL, R1, R2),
			BPF_ALU64_REG(BPF_MUL, R1, R3),
			BPF_ALU64_REG(BPF_MUL, R1, R4),
			BPF_ALU64_REG(BPF_MUL, R1, R5),
			BPF_ALU64_REG(BPF_MUL, R1, R6),
			BPF_ALU64_REG(BPF_MUL, R1, R7),
			BPF_ALU64_REG(BPF_MUL, R1, R8),
			BPF_ALU64_REG(BPF_MUL, R1, R9),
			BPF_ALU64_IMM(BPF_MUL, R1, 10),
			BPF_ALU64_REG(BPF_MOV, R2, R1),
			BPF_ALU64_IMM(BPF_RSH, R2, 32),
			BPF_JMP_IMM(BPF_JEQ, R2, 0x5a924, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_LSH, R1, 32),
			BPF_ALU64_IMM(BPF_ARSH, R1, 32),
			BPF_JMP_IMM(BPF_JEQ, R1, 0xebb90000, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_MUL, R2, R0),
			BPF_ALU64_REG(BPF_MUL, R2, R1),
			BPF_ALU64_REG(BPF_MUL, R2, R3),
			BPF_ALU64_REG(BPF_MUL, R2, R4),
			BPF_ALU64_REG(BPF_MUL, R2, R5),
			BPF_ALU64_REG(BPF_MUL, R2, R6),
			BPF_ALU64_REG(BPF_MUL, R2, R7),
			BPF_ALU64_REG(BPF_MUL, R2, R8),
			BPF_ALU64_REG(BPF_MUL, R2, R9),
			BPF_ALU64_IMM(BPF_MUL, R2, 10),
			BPF_ALU64_IMM(BPF_RSH, R2, 32),
			BPF_ALU64_REG(BPF_MOV, R0, R2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x35d97ef2 } }
	},
	{	/* Mainly checking JIT here. */
		"MOV REG64",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffffffffffffLL),
			BPF_MOV64_REG(R1, R0),
			BPF_MOV64_REG(R2, R1),
			BPF_MOV64_REG(R3, R2),
			BPF_MOV64_REG(R4, R3),
			BPF_MOV64_REG(R5, R4),
			BPF_MOV64_REG(R6, R5),
			BPF_MOV64_REG(R7, R6),
			BPF_MOV64_REG(R8, R7),
			BPF_MOV64_REG(R9, R8),
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_ALU64_IMM(BPF_MOV, R1, 0),
			BPF_ALU64_IMM(BPF_MOV, R2, 0),
			BPF_ALU64_IMM(BPF_MOV, R3, 0),
			BPF_ALU64_IMM(BPF_MOV, R4, 0),
			BPF_ALU64_IMM(BPF_MOV, R5, 0),
			BPF_ALU64_IMM(BPF_MOV, R6, 0),
			BPF_ALU64_IMM(BPF_MOV, R7, 0),
			BPF_ALU64_IMM(BPF_MOV, R8, 0),
			BPF_ALU64_IMM(BPF_MOV, R9, 0),
			BPF_ALU64_REG(BPF_ADD, R0, R0),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_ALU64_REG(BPF_ADD, R0, R2),
			BPF_ALU64_REG(BPF_ADD, R0, R3),
			BPF_ALU64_REG(BPF_ADD, R0, R4),
			BPF_ALU64_REG(BPF_ADD, R0, R5),
			BPF_ALU64_REG(BPF_ADD, R0, R6),
			BPF_ALU64_REG(BPF_ADD, R0, R7),
			BPF_ALU64_REG(BPF_ADD, R0, R8),
			BPF_ALU64_REG(BPF_ADD, R0, R9),
			BPF_ALU64_IMM(BPF_ADD, R0, 0xfefe),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfefe } }
	},
	{	/* Mainly checking JIT here. */
		"MOV REG32",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffffffffffffLL),
			BPF_MOV64_REG(R1, R0),
			BPF_MOV64_REG(R2, R1),
			BPF_MOV64_REG(R3, R2),
			BPF_MOV64_REG(R4, R3),
			BPF_MOV64_REG(R5, R4),
			BPF_MOV64_REG(R6, R5),
			BPF_MOV64_REG(R7, R6),
			BPF_MOV64_REG(R8, R7),
			BPF_MOV64_REG(R9, R8),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_ALU32_IMM(BPF_MOV, R1, 0),
			BPF_ALU32_IMM(BPF_MOV, R2, 0),
			BPF_ALU32_IMM(BPF_MOV, R3, 0),
			BPF_ALU32_IMM(BPF_MOV, R4, 0),
			BPF_ALU32_IMM(BPF_MOV, R5, 0),
			BPF_ALU32_IMM(BPF_MOV, R6, 0),
			BPF_ALU32_IMM(BPF_MOV, R7, 0),
			BPF_ALU32_IMM(BPF_MOV, R8, 0),
			BPF_ALU32_IMM(BPF_MOV, R9, 0),
			BPF_ALU64_REG(BPF_ADD, R0, R0),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_ALU64_REG(BPF_ADD, R0, R2),
			BPF_ALU64_REG(BPF_ADD, R0, R3),
			BPF_ALU64_REG(BPF_ADD, R0, R4),
			BPF_ALU64_REG(BPF_ADD, R0, R5),
			BPF_ALU64_REG(BPF_ADD, R0, R6),
			BPF_ALU64_REG(BPF_ADD, R0, R7),
			BPF_ALU64_REG(BPF_ADD, R0, R8),
			BPF_ALU64_REG(BPF_ADD, R0, R9),
			BPF_ALU64_IMM(BPF_ADD, R0, 0xfefe),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfefe } }
	},
	{	/* Mainly checking JIT here. */
		"LD IMM64",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffffffffffffLL),
			BPF_MOV64_REG(R1, R0),
			BPF_MOV64_REG(R2, R1),
			BPF_MOV64_REG(R3, R2),
			BPF_MOV64_REG(R4, R3),
			BPF_MOV64_REG(R5, R4),
			BPF_MOV64_REG(R6, R5),
			BPF_MOV64_REG(R7, R6),
			BPF_MOV64_REG(R8, R7),
			BPF_MOV64_REG(R9, R8),
			BPF_LD_IMM64(R0, 0x0LL),
			BPF_LD_IMM64(R1, 0x0LL),
			BPF_LD_IMM64(R2, 0x0LL),
			BPF_LD_IMM64(R3, 0x0LL),
			BPF_LD_IMM64(R4, 0x0LL),
			BPF_LD_IMM64(R5, 0x0LL),
			BPF_LD_IMM64(R6, 0x0LL),
			BPF_LD_IMM64(R7, 0x0LL),
			BPF_LD_IMM64(R8, 0x0LL),
			BPF_LD_IMM64(R9, 0x0LL),
			BPF_ALU64_REG(BPF_ADD, R0, R0),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_ALU64_REG(BPF_ADD, R0, R2),
			BPF_ALU64_REG(BPF_ADD, R0, R3),
			BPF_ALU64_REG(BPF_ADD, R0, R4),
			BPF_ALU64_REG(BPF_ADD, R0, R5),
			BPF_ALU64_REG(BPF_ADD, R0, R6),
			BPF_ALU64_REG(BPF_ADD, R0, R7),
			BPF_ALU64_REG(BPF_ADD, R0, R8),
			BPF_ALU64_REG(BPF_ADD, R0, R9),
			BPF_ALU64_IMM(BPF_ADD, R0, 0xfefe),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfefe } }
	},
	{
		"INT: ALU MIX",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 11),
			BPF_ALU64_IMM(BPF_ADD, R0, -1),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU64_IMM(BPF_XOR, R2, 3),
			BPF_ALU64_REG(BPF_DIV, R0, R2),
			BPF_JMP_IMM(BPF_JEQ, R0, 10, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOD, R0, 3),
			BPF_JMP_IMM(BPF_JEQ, R0, 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_MOV, R0, -1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -1 } }
	},
	{
		"INT: shifts by register",
		.u.insns_int = {
			BPF_MOV64_IMM(R0, -1234),
			BPF_MOV64_IMM(R1, 1),
			BPF_ALU32_REG(BPF_RSH, R0, R1),
			BPF_JMP_IMM(BPF_JEQ, R0, 0x7ffffd97, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(R2, 1),
			BPF_ALU64_REG(BPF_LSH, R0, R2),
			BPF_MOV32_IMM(R4, -1234),
			BPF_JMP_REG(BPF_JEQ, R0, R4, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_AND, R4, 63),
			BPF_ALU64_REG(BPF_LSH, R0, R4), /* R0 <= 46 */
			BPF_MOV64_IMM(R3, 47),
			BPF_ALU64_REG(BPF_ARSH, R0, R3),
			BPF_JMP_IMM(BPF_JEQ, R0, -617, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(R2, 1),
			BPF_ALU64_REG(BPF_LSH, R4, R2), /* R4 = 46 << 1 */
			BPF_JMP_IMM(BPF_JEQ, R4, 92, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(R4, 4),
			BPF_ALU64_REG(BPF_LSH, R4, R4), /* R4 = 4 << 4 */
			BPF_JMP_IMM(BPF_JEQ, R4, 64, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(R4, 5),
			BPF_ALU32_REG(BPF_LSH, R4, R4), /* R4 = 5 << 5 */
			BPF_JMP_IMM(BPF_JEQ, R4, 160, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(R0, -1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -1 } }
	},
	{
		"INT: DIV + ABS",
		.u.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R6, R1),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU64_IMM(BPF_MOV, R2, 2),
			BPF_ALU32_REG(BPF_DIV, R0, R2),
			BPF_ALU64_REG(BPF_MOV, R8, R0),
			BPF_LD_ABS(BPF_B, 4),
			BPF_ALU64_REG(BPF_ADD, R8, R0),
			BPF_LD_IND(BPF_B, R8, -70),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ 10, 20, 30, 40, 50 },
		{ { 4, 0 }, { 5, 10 } }
	},
	{
		"INT: DIV by zero",
		.u.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R6, R1),
			BPF_ALU64_IMM(BPF_MOV, R7, 0),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU32_REG(BPF_DIV, R0, R7),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ 10, 20, 30, 40, 50 },
		{ { 3, 0 }, { 4, 0 } }
	},
	{
		"check: missing ret",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IMM, 1),
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"check: div_k_0",
		.u.insns = {
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 0),
			BPF_STMT(BPF_RET | BPF_K, 0)
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"check: unknown insn",
		.u.insns = {
			/* seccomp insn, rejected in socket filter */
			BPF_STMT(BPF_LDX | BPF_W | BPF_ABS, 0),
			BPF_STMT(BPF_RET | BPF_K, 0)
		},
		CLASSIC | FLAG_EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"check: out of range spill/fill",
		.u.insns = {
			BPF_STMT(BPF_STX, 16),
			BPF_STMT(BPF_RET | BPF_K, 0)
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ }
	},
	{
		"JUMPS + HOLES",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JGE, 0, 13, 15),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x90c2894d, 3, 4),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x90c2894d, 1, 2),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JGE, 0, 14, 15),
			BPF_JUMP(BPF_JMP | BPF_JGE, 0, 13, 14),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x2ac28349, 2, 3),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x2ac28349, 1, 2),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JGE, 0, 14, 15),
			BPF_JUMP(BPF_JMP | BPF_JGE, 0, 13, 14),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x90d2ff41, 2, 3),
			BPF_JUMP(BPF_JMP | BPF_JEQ, 0x90d2ff41, 1, 2),
			BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 0),
			BPF_STMT(BPF_RET | BPF_A, 0),
			BPF_STMT(BPF_RET | BPF_A, 0),
		},
		CLASSIC,
		{ 0x00, 0x1b, 0x21, 0x3c, 0x9d, 0xf8,
		  0x90, 0xe2, 0xba, 0x0a, 0x56, 0xb4,
		  0x08, 0x00,
		  0x45, 0x00, 0x00, 0x28, 0x00, 0x00,
		  0x20, 0x00, 0x40, 0x11, 0x00, 0x00, /* IP header */
		  0xc0, 0xa8, 0x33, 0x01,
		  0xc0, 0xa8, 0x33, 0x02,
		  0xbb, 0xb6,
		  0xa9, 0xfa,
		  0x00, 0x14, 0x00, 0x00,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		  0xcc, 0xcc, 0xcc, 0xcc },
		{ { 88, 0x001b } }
	},
	{
		"check: RET X",
		.u.insns = {
			BPF_STMT(BPF_RET | BPF_X, 0),
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ },
	},
	{
		"check: LDX + RET X",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 42),
			BPF_STMT(BPF_RET | BPF_X, 0),
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ },
	},
	{	/* Mainly checking JIT here. */
		"M[]: alt STX + LDX",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 100),
			BPF_STMT(BPF_STX, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 1),
			BPF_STMT(BPF_LDX | BPF_MEM, 1),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 2),
			BPF_STMT(BPF_LDX | BPF_MEM, 2),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 3),
			BPF_STMT(BPF_LDX | BPF_MEM, 3),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 4),
			BPF_STMT(BPF_LDX | BPF_MEM, 4),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 5),
			BPF_STMT(BPF_LDX | BPF_MEM, 5),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 6),
			BPF_STMT(BPF_LDX | BPF_MEM, 6),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 7),
			BPF_STMT(BPF_LDX | BPF_MEM, 7),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 8),
			BPF_STMT(BPF_LDX | BPF_MEM, 8),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 9),
			BPF_STMT(BPF_LDX | BPF_MEM, 9),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 10),
			BPF_STMT(BPF_LDX | BPF_MEM, 10),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 11),
			BPF_STMT(BPF_LDX | BPF_MEM, 11),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 12),
			BPF_STMT(BPF_LDX | BPF_MEM, 12),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 13),
			BPF_STMT(BPF_LDX | BPF_MEM, 13),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 14),
			BPF_STMT(BPF_LDX | BPF_MEM, 14),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_STX, 15),
			BPF_STMT(BPF_LDX | BPF_MEM, 15),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 1),
			BPF_STMT(BPF_MISC | BPF_TAX, 0),
			BPF_STMT(BPF_RET | BPF_A, 0),
		},
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 116 } },
	},
	{	/* Mainly checking JIT here. */
		"M[]: full STX + full LDX",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0xbadfeedb),
			BPF_STMT(BPF_STX, 0),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xecabedae),
			BPF_STMT(BPF_STX, 1),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xafccfeaf),
			BPF_STMT(BPF_STX, 2),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xbffdcedc),
			BPF_STMT(BPF_STX, 3),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xfbbbdccb),
			BPF_STMT(BPF_STX, 4),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xfbabcbda),
			BPF_STMT(BPF_STX, 5),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xaedecbdb),
			BPF_STMT(BPF_STX, 6),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xadebbade),
			BPF_STMT(BPF_STX, 7),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xfcfcfaec),
			BPF_STMT(BPF_STX, 8),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xbcdddbdc),
			BPF_STMT(BPF_STX, 9),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xfeefdfac),
			BPF_STMT(BPF_STX, 10),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xcddcdeea),
			BPF_STMT(BPF_STX, 11),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xaccfaebb),
			BPF_STMT(BPF_STX, 12),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xbdcccdcf),
			BPF_STMT(BPF_STX, 13),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xaaedecde),
			BPF_STMT(BPF_STX, 14),
			BPF_STMT(BPF_LDX | BPF_IMM, 0xfaeacdad),
			BPF_STMT(BPF_STX, 15),
			BPF_STMT(BPF_LDX | BPF_MEM, 0),
			BPF_STMT(BPF_MISC | BPF_TXA, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 1),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 2),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 3),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 4),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 5),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 6),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 7),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 8),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 9),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 10),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 11),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 12),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 13),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 14),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_LDX | BPF_MEM, 15),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0),
		},
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0x2a5a5e5 } },
	},
	{
		"check: SKF_AD_MAX",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF + SKF_AD_MAX),
			BPF_STMT(BPF_RET | BPF_A, 0),
		},
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ },
	},
	{	/* Passes checker but fails during runtime. */
		"LD [SKF_AD_OFF-1]",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 SKF_AD_OFF - 1),
			BPF_STMT(BPF_RET | BPF_K, 1),
		},
		CLASSIC,
		{ },
		{ { 1, 0 } },
	},
	{
		"load 64-bit immediate",
		.u.insns_int = {
			BPF_LD_IMM64(R1, 0x567800001234LL),
			BPF_MOV64_REG(R2, R1),
			BPF_MOV64_REG(R3, R2),
			BPF_ALU64_IMM(BPF_RSH, R2, 32),
			BPF_ALU64_IMM(BPF_LSH, R3, 32),
			BPF_ALU64_IMM(BPF_RSH, R3, 32),
			BPF_ALU64_IMM(BPF_MOV, R0, 0),
			BPF_JMP_IMM(BPF_JEQ, R2, 0x5678, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, R3, 0x1234, 1),
			BPF_EXIT_INSN(),
			BPF_LD_IMM64(R0, 0x1ffffffffLL),
			BPF_ALU64_IMM(BPF_RSH, R0, 32), /* R0 = 1 */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } }
	},
	{
		"nmap reduced",
		.u.insns_int = {
			BPF_MOV64_REG(R6, R1),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, R0, 0x806, 28),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, R0, 0x806, 26),
			BPF_MOV32_IMM(R0, 18),
			BPF_STX_MEM(BPF_W, R10, R0, -64),
			BPF_LDX_MEM(BPF_W, R7, R10, -64),
			BPF_LD_IND(BPF_W, R7, 14),
			BPF_STX_MEM(BPF_W, R10, R0, -60),
			BPF_MOV32_IMM(R0, 280971478),
			BPF_STX_MEM(BPF_W, R10, R0, -56),
			BPF_LDX_MEM(BPF_W, R7, R10, -56),
			BPF_LDX_MEM(BPF_W, R0, R10, -60),
			BPF_ALU32_REG(BPF_SUB, R0, R7),
			BPF_JMP_IMM(BPF_JNE, R0, 0, 15),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, R0, 0x806, 13),
			BPF_MOV32_IMM(R0, 22),
			BPF_STX_MEM(BPF_W, R10, R0, -56),
			BPF_LDX_MEM(BPF_W, R7, R10, -56),
			BPF_LD_IND(BPF_H, R7, 14),
			BPF_STX_MEM(BPF_W, R10, R0, -52),
			BPF_MOV32_IMM(R0, 17366),
			BPF_STX_MEM(BPF_W, R10, R0, -48),
			BPF_LDX_MEM(BPF_W, R7, R10, -48),
			BPF_LDX_MEM(BPF_W, R0, R10, -52),
			BPF_ALU32_REG(BPF_SUB, R0, R7),
			BPF_JMP_IMM(BPF_JNE, R0, 0, 2),
			BPF_MOV32_IMM(R0, 256),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0x06, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0x10, 0xbf, 0x48, 0xd6, 0x43, 0xd6},
		{ { 38, 256 } },
		.stack_depth = 64,
	},
	/* BPF_ALU | BPF_MOV | BPF_X */
	{
		"ALU_MOV_X: dst = 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_MOV_X: dst = 4294967295",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967295U),
			BPF_ALU32_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	{
		"ALU64_MOV_X: dst = 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_MOV_X: dst = 4294967295",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967295U),
			BPF_ALU64_REG(BPF_MOV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	/* BPF_ALU | BPF_MOV | BPF_K */
	{
		"ALU_MOV_K: dst = 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_MOV_K: dst = 4294967295",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 4294967295U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	{
		"ALU_MOV_K: 0x0000ffffffff0000 = 0x00000000ffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x00000000ffffffffLL),
			BPF_ALU32_IMM(BPF_MOV, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_MOV_K: dst = 2",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_MOV_K: dst = 2147483647",
		.u.insns_int = {
			BPF_ALU64_IMM(BPF_MOV, R0, 2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2147483647 } },
	},
	{
		"ALU64_OR_K: dst = 0x0",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x0),
			BPF_ALU64_IMM(BPF_MOV, R2, 0x0),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_MOV_K: dst = -1",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_MOV, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_ADD | BPF_X */
	{
		"ALU_ADD_X: 1 + 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_ADD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_ADD_X: 1 + 4294967294 = 4294967295",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967294U),
			BPF_ALU32_REG(BPF_ADD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	{
		"ALU_ADD_X: 2 + 4294967294 = 0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_LD_IMM64(R1, 4294967294U),
			BPF_ALU32_REG(BPF_ADD, R0, R1),
			BPF_JMP_IMM(BPF_JEQ, R0, 0, 2),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_ADD_X: 1 + 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_ADD_X: 1 + 4294967294 = 4294967295",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967294U),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	{
		"ALU64_ADD_X: 2 + 4294967294 = 4294967296",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_LD_IMM64(R1, 4294967294U),
			BPF_LD_IMM64(R2, 4294967296ULL),
			BPF_ALU64_REG(BPF_ADD, R0, R1),
			BPF_JMP_REG(BPF_JEQ, R0, R2, 2),
			BPF_MOV32_IMM(R0, 0),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_ALU | BPF_ADD | BPF_K */
	{
		"ALU_ADD_K: 1 + 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_ADD, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_ADD_K: 3 + 0 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_ADD, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_ADD_K: 1 + 4294967294 = 4294967295",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_ADD, R0, 4294967294U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 4294967295U } },
	},
	{
		"ALU_ADD_K: 4294967294 + 2 = 0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967294U),
			BPF_ALU32_IMM(BPF_ADD, R0, 2),
			BPF_JMP_IMM(BPF_JEQ, R0, 0, 2),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_ADD_K: 0 + (-1) = 0x00000000ffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0x00000000ffffffff),
			BPF_ALU32_IMM(BPF_ADD, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU_ADD_K: 0 + 0xffff = 0xffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0xffff),
			BPF_ALU32_IMM(BPF_ADD, R2, 0xffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU_ADD_K: 0 + 0x7fffffff = 0x7fffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0x7fffffff),
			BPF_ALU32_IMM(BPF_ADD, R2, 0x7fffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU_ADD_K: 0 + 0x80000000 = 0x80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0x80000000),
			BPF_ALU32_IMM(BPF_ADD, R2, 0x80000000),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU_ADD_K: 0 + 0x80008000 = 0x80008000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0x80008000),
			BPF_ALU32_IMM(BPF_ADD, R2, 0x80008000),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_ADD_K: 1 + 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_ADD, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_ADD_K: 3 + 0 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_ADD, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_ADD_K: 1 + 2147483646 = 2147483647",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_ADD, R0, 2147483646),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2147483647 } },
	},
	{
		"ALU64_ADD_K: 4294967294 + 2 = 4294967296",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967294U),
			BPF_LD_IMM64(R1, 4294967296ULL),
			BPF_ALU64_IMM(BPF_ADD, R0, 2),
			BPF_JMP_REG(BPF_JEQ, R0, R1, 2),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_ADD_K: 2147483646 + -2147483647 = -1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483646),
			BPF_ALU64_IMM(BPF_ADD, R0, -2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -1 } },
	},
	{
		"ALU64_ADD_K: 1 + 0 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x1),
			BPF_LD_IMM64(R3, 0x1),
			BPF_ALU64_IMM(BPF_ADD, R2, 0x0),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_ADD_K: 0 + (-1) = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_ADD, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_ADD_K: 0 + 0xffff = 0xffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0xffff),
			BPF_ALU64_IMM(BPF_ADD, R2, 0xffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_ADD_K: 0 + 0x7fffffff = 0x7fffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0x7fffffff),
			BPF_ALU64_IMM(BPF_ADD, R2, 0x7fffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_ADD_K: 0 + 0x80000000 = 0xffffffff80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0xffffffff80000000LL),
			BPF_ALU64_IMM(BPF_ADD, R2, 0x80000000),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU_ADD_K: 0 + 0x80008000 = 0xffffffff80008000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0),
			BPF_LD_IMM64(R3, 0xffffffff80008000LL),
			BPF_ALU64_IMM(BPF_ADD, R2, 0x80008000),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_SUB | BPF_X */
	{
		"ALU_SUB_X: 3 - 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU32_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_SUB_X: 4294967295 - 4294967294 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967294U),
			BPF_ALU32_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_SUB_X: 3 - 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_SUB_X: 4294967295 - 4294967294 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967294U),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_ALU | BPF_SUB | BPF_K */
	{
		"ALU_SUB_K: 3 - 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_SUB, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_SUB_K: 3 - 0 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_SUB, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_SUB_K: 4294967295 - 4294967294 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_SUB, R0, 4294967294U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_SUB_K: 3 - 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_SUB, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_SUB_K: 3 - 0 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_SUB, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_SUB_K: 4294967294 - 4294967295 = -1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967294U),
			BPF_ALU64_IMM(BPF_SUB, R0, 4294967295U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -1 } },
	},
	{
		"ALU64_ADD_K: 2147483646 - 2147483647 = -1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483646),
			BPF_ALU64_IMM(BPF_SUB, R0, 2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -1 } },
	},
	/* BPF_ALU | BPF_MUL | BPF_X */
	{
		"ALU_MUL_X: 2 * 3 = 6",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MOV, R1, 3),
			BPF_ALU32_REG(BPF_MUL, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 6 } },
	},
	{
		"ALU_MUL_X: 2 * 0x7FFFFFF8 = 0xFFFFFFF0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MOV, R1, 0x7FFFFFF8),
			BPF_ALU32_REG(BPF_MUL, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xFFFFFFF0 } },
	},
	{
		"ALU_MUL_X: -1 * -1 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, -1),
			BPF_ALU32_IMM(BPF_MOV, R1, -1),
			BPF_ALU32_REG(BPF_MUL, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_MUL_X: 2 * 3 = 6",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MOV, R1, 3),
			BPF_ALU64_REG(BPF_MUL, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 6 } },
	},
	{
		"ALU64_MUL_X: 1 * 2147483647 = 2147483647",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 2147483647),
			BPF_ALU64_REG(BPF_MUL, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2147483647 } },
	},
	/* BPF_ALU | BPF_MUL | BPF_K */
	{
		"ALU_MUL_K: 2 * 3 = 6",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MUL, R0, 3),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 6 } },
	},
	{
		"ALU_MUL_K: 3 * 1 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MUL, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_MUL_K: 2 * 0x7FFFFFF8 = 0xFFFFFFF0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MUL, R0, 0x7FFFFFF8),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xFFFFFFF0 } },
	},
	{
		"ALU_MUL_K: 1 * (-1) = 0x00000000ffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x1),
			BPF_LD_IMM64(R3, 0x00000000ffffffff),
			BPF_ALU32_IMM(BPF_MUL, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_MUL_K: 2 * 3 = 6",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU64_IMM(BPF_MUL, R0, 3),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 6 } },
	},
	{
		"ALU64_MUL_K: 3 * 1 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_MUL, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_MUL_K: 1 * 2147483647 = 2147483647",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_MUL, R0, 2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2147483647 } },
	},
	{
		"ALU64_MUL_K: 1 * -2147483647 = -2147483647",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_MUL, R0, -2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -2147483647 } },
	},
	{
		"ALU64_MUL_K: 1 * (-1) = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x1),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_MUL, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_DIV | BPF_X */
	{
		"ALU_DIV_X: 6 / 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 6),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_DIV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_DIV_X: 4294967295 / 4294967295 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967295U),
			BPF_ALU32_REG(BPF_DIV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_DIV_X: 6 / 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 6),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_DIV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_DIV_X: 2147483647 / 2147483647 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483647),
			BPF_ALU32_IMM(BPF_MOV, R1, 2147483647),
			BPF_ALU64_REG(BPF_DIV, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_DIV_X: 0xffffffffffffffff / (-1) = 0x0000000000000001",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0xffffffffffffffffLL),
			BPF_LD_IMM64(R4, 0xffffffffffffffffLL),
			BPF_LD_IMM64(R3, 0x0000000000000001LL),
			BPF_ALU64_REG(BPF_DIV, R2, R4),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_DIV | BPF_K */
	{
		"ALU_DIV_K: 6 / 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 6),
			BPF_ALU32_IMM(BPF_DIV, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_DIV_K: 3 / 1 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_DIV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_DIV_K: 4294967295 / 4294967295 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_DIV, R0, 4294967295U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_DIV_K: 0xffffffffffffffff / (-1) = 0x1",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0xffffffffffffffffLL),
			BPF_LD_IMM64(R3, 0x1UL),
			BPF_ALU32_IMM(BPF_DIV, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_DIV_K: 6 / 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 6),
			BPF_ALU64_IMM(BPF_DIV, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_DIV_K: 3 / 1 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_DIV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_DIV_K: 2147483647 / 2147483647 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483647),
			BPF_ALU64_IMM(BPF_DIV, R0, 2147483647),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_DIV_K: 0xffffffffffffffff / (-1) = 0x0000000000000001",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0xffffffffffffffffLL),
			BPF_LD_IMM64(R3, 0x0000000000000001LL),
			BPF_ALU64_IMM(BPF_DIV, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_MOD | BPF_X */
	{
		"ALU_MOD_X: 3 % 2 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_MOD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_MOD_X: 4294967295 % 4294967293 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_MOV, R1, 4294967293U),
			BPF_ALU32_REG(BPF_MOD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_MOD_X: 3 % 2 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_MOD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_MOD_X: 2147483647 % 2147483645 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483647),
			BPF_ALU32_IMM(BPF_MOV, R1, 2147483645),
			BPF_ALU64_REG(BPF_MOD, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	/* BPF_ALU | BPF_MOD | BPF_K */
	{
		"ALU_MOD_K: 3 % 2 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOD, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_MOD_K: 3 % 1 = 0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOD, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0 } },
	},
	{
		"ALU_MOD_K: 4294967295 % 4294967293 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 4294967295U),
			BPF_ALU32_IMM(BPF_MOD, R0, 4294967293U),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_MOD_K: 3 % 2 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_MOD, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_MOD_K: 3 % 1 = 0",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_MOD, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0 } },
	},
	{
		"ALU64_MOD_K: 2147483647 % 2147483645 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2147483647),
			BPF_ALU64_IMM(BPF_MOD, R0, 2147483645),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	/* BPF_ALU | BPF_AND | BPF_X */
	{
		"ALU_AND_X: 3 & 2 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_AND, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_AND_X: 0xffffffff & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffff),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU32_REG(BPF_AND, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_AND_X: 3 & 2 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_AND, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_AND_X: 0xffffffff & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffff),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU64_REG(BPF_AND, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	/* BPF_ALU | BPF_AND | BPF_K */
	{
		"ALU_AND_K: 3 & 2 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU32_IMM(BPF_AND, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_AND_K: 0xffffffff & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffff),
			BPF_ALU32_IMM(BPF_AND, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_AND_K: 3 & 2 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_AND, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_AND_K: 0xffffffff & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xffffffff),
			BPF_ALU64_IMM(BPF_AND, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_AND_K: 0x0000ffffffff0000 & 0x0 = 0x0000ffff00000000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x0000000000000000LL),
			BPF_ALU64_IMM(BPF_AND, R2, 0x0),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_AND_K: 0x0000ffffffff0000 & -1 = 0x0000ffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x0000ffffffff0000LL),
			BPF_ALU64_IMM(BPF_AND, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_AND_K: 0xffffffffffffffff & -1 = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0xffffffffffffffffLL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_AND, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_OR | BPF_X */
	{
		"ALU_OR_X: 1 | 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU32_REG(BPF_OR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_OR_X: 0x0 | 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU32_REG(BPF_OR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_OR_X: 1 | 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 2),
			BPF_ALU64_REG(BPF_OR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_OR_X: 0 | 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU64_REG(BPF_OR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	/* BPF_ALU | BPF_OR | BPF_K */
	{
		"ALU_OR_K: 1 | 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_OR, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_OR_K: 0 & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_ALU32_IMM(BPF_OR, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_OR_K: 1 | 2 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_OR, R0, 2),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_OR_K: 0 & 0xffffffff = 0xffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_ALU64_IMM(BPF_OR, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
	},
	{
		"ALU64_OR_K: 0x0000ffffffff0000 | 0x0 = 0x0000ffff00000000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x0000ffffffff0000LL),
			BPF_ALU64_IMM(BPF_OR, R2, 0x0),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_OR_K: 0x0000ffffffff0000 | -1 = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_OR, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_OR_K: 0x000000000000000 | -1 = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000000000000000LL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_OR, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_XOR | BPF_X */
	{
		"ALU_XOR_X: 5 ^ 6 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 5),
			BPF_ALU32_IMM(BPF_MOV, R1, 6),
			BPF_ALU32_REG(BPF_XOR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_XOR_X: 0x1 ^ 0xffffffff = 0xfffffffe",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU32_REG(BPF_XOR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfffffffe } },
	},
	{
		"ALU64_XOR_X: 5 ^ 6 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 5),
			BPF_ALU32_IMM(BPF_MOV, R1, 6),
			BPF_ALU64_REG(BPF_XOR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_XOR_X: 1 ^ 0xffffffff = 0xfffffffe",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 0xffffffff),
			BPF_ALU64_REG(BPF_XOR, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfffffffe } },
	},
	/* BPF_ALU | BPF_XOR | BPF_K */
	{
		"ALU_XOR_K: 5 ^ 6 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 5),
			BPF_ALU32_IMM(BPF_XOR, R0, 6),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU_XOR_K: 1 ^ 0xffffffff = 0xfffffffe",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_XOR, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfffffffe } },
	},
	{
		"ALU64_XOR_K: 5 ^ 6 = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 5),
			BPF_ALU64_IMM(BPF_XOR, R0, 6),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_XOR_K: 1 & 0xffffffff = 0xfffffffe",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_XOR, R0, 0xffffffff),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xfffffffe } },
	},
	{
		"ALU64_XOR_K: 0x0000ffffffff0000 ^ 0x0 = 0x0000ffffffff0000",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0x0000ffffffff0000LL),
			BPF_ALU64_IMM(BPF_XOR, R2, 0x0),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_XOR_K: 0x0000ffffffff0000 ^ -1 = 0xffff00000000ffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000ffffffff0000LL),
			BPF_LD_IMM64(R3, 0xffff00000000ffffLL),
			BPF_ALU64_IMM(BPF_XOR, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	{
		"ALU64_XOR_K: 0x000000000000000 ^ -1 = 0xffffffffffffffff",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0x0000000000000000LL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ALU64_IMM(BPF_XOR, R2, 0xffffffff),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
	},
	/* BPF_ALU | BPF_LSH | BPF_X */
	{
		"ALU_LSH_X: 1 << 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU32_REG(BPF_LSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_LSH_X: 1 << 31 = 0x80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 31),
			BPF_ALU32_REG(BPF_LSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x80000000 } },
	},
	{
		"ALU64_LSH_X: 1 << 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_REG(BPF_LSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_LSH_X: 1 << 31 = 0x80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_MOV, R1, 31),
			BPF_ALU64_REG(BPF_LSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x80000000 } },
	},
	/* BPF_ALU | BPF_LSH | BPF_K */
	{
		"ALU_LSH_K: 1 << 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_LSH, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU_LSH_K: 1 << 31 = 0x80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU32_IMM(BPF_LSH, R0, 31),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x80000000 } },
	},
	{
		"ALU64_LSH_K: 1 << 1 = 2",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_LSH, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 2 } },
	},
	{
		"ALU64_LSH_K: 1 << 31 = 0x80000000",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 1),
			BPF_ALU64_IMM(BPF_LSH, R0, 31),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x80000000 } },
	},
	/* BPF_ALU | BPF_RSH | BPF_X */
	{
		"ALU_RSH_X: 2 >> 1 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU32_REG(BPF_RSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_RSH_X: 0x80000000 >> 31 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x80000000),
			BPF_ALU32_IMM(BPF_MOV, R1, 31),
			BPF_ALU32_REG(BPF_RSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_RSH_X: 2 >> 1 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_MOV, R1, 1),
			BPF_ALU64_REG(BPF_RSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_RSH_X: 0x80000000 >> 31 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x80000000),
			BPF_ALU32_IMM(BPF_MOV, R1, 31),
			BPF_ALU64_REG(BPF_RSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_ALU | BPF_RSH | BPF_K */
	{
		"ALU_RSH_K: 2 >> 1 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU32_IMM(BPF_RSH, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU_RSH_K: 0x80000000 >> 31 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x80000000),
			BPF_ALU32_IMM(BPF_RSH, R0, 31),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_RSH_K: 2 >> 1 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 2),
			BPF_ALU64_IMM(BPF_RSH, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"ALU64_RSH_K: 0x80000000 >> 31 = 1",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x80000000),
			BPF_ALU64_IMM(BPF_RSH, R0, 31),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_ALU | BPF_ARSH | BPF_X */
	{
		"ALU_ARSH_X: 0xff00ff0000000000 >> 40 = 0xffffffffffff00ff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xff00ff0000000000LL),
			BPF_ALU32_IMM(BPF_MOV, R1, 40),
			BPF_ALU64_REG(BPF_ARSH, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffff00ff } },
	},
	/* BPF_ALU | BPF_ARSH | BPF_K */
	{
		"ALU_ARSH_K: 0xff00ff0000000000 >> 40 = 0xffffffffffff00ff",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0xff00ff0000000000LL),
			BPF_ALU64_IMM(BPF_ARSH, R0, 40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffff00ff } },
	},
	/* BPF_ALU | BPF_NEG */
	{
		"ALU_NEG: -(3) = -3",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 3),
			BPF_ALU32_IMM(BPF_NEG, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -3 } },
	},
	{
		"ALU_NEG: -(-3) = 3",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, -3),
			BPF_ALU32_IMM(BPF_NEG, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	{
		"ALU64_NEG: -(3) = -3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 3),
			BPF_ALU64_IMM(BPF_NEG, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, -3 } },
	},
	{
		"ALU64_NEG: -(-3) = 3",
		.u.insns_int = {
			BPF_LD_IMM64(R0, -3),
			BPF_ALU64_IMM(BPF_NEG, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 3 } },
	},
	/* BPF_ALU | BPF_END | BPF_FROM_BE */
	{
		"ALU_END_FROM_BE 16: 0x0123456789abcdef -> 0xcdef",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_BE, R0, 16),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0,  cpu_to_be16(0xcdef) } },
	},
	{
		"ALU_END_FROM_BE 32: 0x0123456789abcdef -> 0x89abcdef",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_BE, R0, 32),
			BPF_ALU64_REG(BPF_MOV, R1, R0),
			BPF_ALU64_IMM(BPF_RSH, R1, 32),
			BPF_ALU32_REG(BPF_ADD, R0, R1), /* R1 = 0 */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, cpu_to_be32(0x89abcdef) } },
	},
	{
		"ALU_END_FROM_BE 64: 0x0123456789abcdef -> 0x89abcdef",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_BE, R0, 64),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, (u32) cpu_to_be64(0x0123456789abcdefLL) } },
	},
	/* BPF_ALU | BPF_END | BPF_FROM_LE */
	{
		"ALU_END_FROM_LE 16: 0x0123456789abcdef -> 0xefcd",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_LE, R0, 16),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, cpu_to_le16(0xcdef) } },
	},
	{
		"ALU_END_FROM_LE 32: 0x0123456789abcdef -> 0xefcdab89",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_LE, R0, 32),
			BPF_ALU64_REG(BPF_MOV, R1, R0),
			BPF_ALU64_IMM(BPF_RSH, R1, 32),
			BPF_ALU32_REG(BPF_ADD, R0, R1), /* R1 = 0 */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, cpu_to_le32(0x89abcdef) } },
	},
	{
		"ALU_END_FROM_LE 64: 0x0123456789abcdef -> 0x67452301",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0x0123456789abcdefLL),
			BPF_ENDIAN(BPF_FROM_LE, R0, 64),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, (u32) cpu_to_le64(0x0123456789abcdefLL) } },
	},
	/* BPF_ST(X) | BPF_MEM | BPF_B/H/W/DW */
	{
		"ST_MEM_B: Store/Load byte: max negative",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_B, R10, -40, 0xff),
			BPF_LDX_MEM(BPF_B, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_B: Store/Load byte: max positive",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_H, R10, -40, 0x7f),
			BPF_LDX_MEM(BPF_H, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x7f } },
		.stack_depth = 40,
	},
	{
		"STX_MEM_B: Store/Load byte: max negative",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_LD_IMM64(R1, 0xffLL),
			BPF_STX_MEM(BPF_B, R10, R1, -40),
			BPF_LDX_MEM(BPF_B, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_H: Store/Load half word: max negative",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_H, R10, -40, 0xffff),
			BPF_LDX_MEM(BPF_H, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_H: Store/Load half word: max positive",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_H, R10, -40, 0x7fff),
			BPF_LDX_MEM(BPF_H, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x7fff } },
		.stack_depth = 40,
	},
	{
		"STX_MEM_H: Store/Load half word: max negative",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_LD_IMM64(R1, 0xffffLL),
			BPF_STX_MEM(BPF_H, R10, R1, -40),
			BPF_LDX_MEM(BPF_H, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_W: Store/Load word: max negative",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_W, R10, -40, 0xffffffff),
			BPF_LDX_MEM(BPF_W, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_W: Store/Load word: max positive",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_W, R10, -40, 0x7fffffff),
			BPF_LDX_MEM(BPF_W, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x7fffffff } },
		.stack_depth = 40,
	},
	{
		"STX_MEM_W: Store/Load word: max negative",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffLL),
			BPF_STX_MEM(BPF_W, R10, R1, -40),
			BPF_LDX_MEM(BPF_W, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_DW: Store/Load double word: max negative",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_DW, R10, -40, 0xffffffff),
			BPF_LDX_MEM(BPF_DW, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_DW: Store/Load double word: max negative 2",
		.u.insns_int = {
			BPF_LD_IMM64(R2, 0xffff00000000ffffLL),
			BPF_LD_IMM64(R3, 0xffffffffffffffffLL),
			BPF_ST_MEM(BPF_DW, R10, -40, 0xffffffff),
			BPF_LDX_MEM(BPF_DW, R2, R10, -40),
			BPF_JMP_REG(BPF_JEQ, R2, R3, 2),
			BPF_MOV32_IMM(R0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x1 } },
		.stack_depth = 40,
	},
	{
		"ST_MEM_DW: Store/Load double word: max positive",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_ST_MEM(BPF_DW, R10, -40, 0x7fffffff),
			BPF_LDX_MEM(BPF_DW, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x7fffffff } },
		.stack_depth = 40,
	},
	{
		"STX_MEM_DW: Store/Load double word: max negative",
		.u.insns_int = {
			BPF_LD_IMM64(R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_STX_MEM(BPF_W, R10, R1, -40),
			BPF_LDX_MEM(BPF_W, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffff } },
		.stack_depth = 40,
	},
	/* BPF_STX | BPF_XADD | BPF_W/DW */
	{
		"STX_XADD_W: Test: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_W, R10, -40, 0x10),
			BPF_STX_XADD(BPF_W, R10, R0, -40),
			BPF_LDX_MEM(BPF_W, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x22 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_W: Test side-effects, r10: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R1, R10),
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_W, R10, -40, 0x10),
			BPF_STX_XADD(BPF_W, R10, R0, -40),
			BPF_ALU64_REG(BPF_MOV, R0, R10),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_W: Test side-effects, r0: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_W, R10, -40, 0x10),
			BPF_STX_XADD(BPF_W, R10, R0, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x12 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_W: X + 1 + 1 + 1 + ...",
		{ },
		INTERNAL,
		{ },
		{ { 0, 4134 } },
		.fill_helper = bpf_fill_stxw,
	},
	{
		"STX_XADD_DW: Test: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_DW, R10, -40, 0x10),
			BPF_STX_XADD(BPF_DW, R10, R0, -40),
			BPF_LDX_MEM(BPF_DW, R0, R10, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x22 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_DW: Test side-effects, r10: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU64_REG(BPF_MOV, R1, R10),
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_DW, R10, -40, 0x10),
			BPF_STX_XADD(BPF_DW, R10, R0, -40),
			BPF_ALU64_REG(BPF_MOV, R0, R10),
			BPF_ALU64_REG(BPF_SUB, R0, R1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_DW: Test side-effects, r0: 0x12 + 0x10 = 0x22",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0x12),
			BPF_ST_MEM(BPF_DW, R10, -40, 0x10),
			BPF_STX_XADD(BPF_DW, R10, R0, -40),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0x12 } },
		.stack_depth = 40,
	},
	{
		"STX_XADD_DW: X + 1 + 1 + 1 + ...",
		{ },
		INTERNAL,
		{ },
		{ { 0, 4134 } },
		.fill_helper = bpf_fill_stxdw,
	},
	/* BPF_JMP | BPF_EXIT */
	{
		"JMP_EXIT",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0x4711),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0x4712),
		},
		INTERNAL,
		{ },
		{ { 0, 0x4711 } },
	},
	/* BPF_JMP | BPF_JA */
	{
		"JMP_JA: Unconditional jump: if (true) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSLT | BPF_K */
	{
		"JMP_JSLT_K: Signed jump: if (-2 < -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xfffffffffffffffeLL),
			BPF_JMP_IMM(BPF_JSLT, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLT_K: Signed jump: if (-1 < -1) return 0",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSLT, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSGT | BPF_K */
	{
		"JMP_JSGT_K: Signed jump: if (-1 > -2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSGT, R1, -2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGT_K: Signed jump: if (-1 > -1) return 0",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSGT, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSLE | BPF_K */
	{
		"JMP_JSLE_K: Signed jump: if (-2 <= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xfffffffffffffffeLL),
			BPF_JMP_IMM(BPF_JSLE, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLE_K: Signed jump: if (-1 <= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSLE, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLE_K: Signed jump: value walk 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 6),
			BPF_ALU64_IMM(BPF_SUB, R1, 1),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 4),
			BPF_ALU64_IMM(BPF_SUB, R1, 1),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 2),
			BPF_ALU64_IMM(BPF_SUB, R1, 1),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 1),
			BPF_EXIT_INSN(),		/* bad exit */
			BPF_ALU32_IMM(BPF_MOV, R0, 1),	/* good exit */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLE_K: Signed jump: value walk 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 4),
			BPF_ALU64_IMM(BPF_SUB, R1, 2),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 2),
			BPF_ALU64_IMM(BPF_SUB, R1, 2),
			BPF_JMP_IMM(BPF_JSLE, R1, 0, 1),
			BPF_EXIT_INSN(),		/* bad exit */
			BPF_ALU32_IMM(BPF_MOV, R0, 1),	/* good exit */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSGE | BPF_K */
	{
		"JMP_JSGE_K: Signed jump: if (-1 >= -2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSGE, R1, -2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGE_K: Signed jump: if (-1 >= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 0xffffffffffffffffLL),
			BPF_JMP_IMM(BPF_JSGE, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGE_K: Signed jump: value walk 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -3),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 6),
			BPF_ALU64_IMM(BPF_ADD, R1, 1),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 4),
			BPF_ALU64_IMM(BPF_ADD, R1, 1),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 2),
			BPF_ALU64_IMM(BPF_ADD, R1, 1),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 1),
			BPF_EXIT_INSN(),		/* bad exit */
			BPF_ALU32_IMM(BPF_MOV, R0, 1),	/* good exit */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGE_K: Signed jump: value walk 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -3),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 4),
			BPF_ALU64_IMM(BPF_ADD, R1, 2),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 2),
			BPF_ALU64_IMM(BPF_ADD, R1, 2),
			BPF_JMP_IMM(BPF_JSGE, R1, 0, 1),
			BPF_EXIT_INSN(),		/* bad exit */
			BPF_ALU32_IMM(BPF_MOV, R0, 1),	/* good exit */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JGT | BPF_K */
	{
		"JMP_JGT_K: if (3 > 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JGT, R1, 2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JGT_K: Unsigned jump: if (-1 > 1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_JMP_IMM(BPF_JGT, R1, 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JLT | BPF_K */
	{
		"JMP_JLT_K: if (2 < 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 2),
			BPF_JMP_IMM(BPF_JLT, R1, 3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JGT_K: Unsigned jump: if (1 < -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 1),
			BPF_JMP_IMM(BPF_JLT, R1, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JGE | BPF_K */
	{
		"JMP_JGE_K: if (3 >= 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JGE, R1, 2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JLE | BPF_K */
	{
		"JMP_JLE_K: if (2 <= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 2),
			BPF_JMP_IMM(BPF_JLE, R1, 3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JGT | BPF_K jump backwards */
	{
		"JMP_JGT_K: if (3 > 2) return 1 (jump backwards)",
		.u.insns_int = {
			BPF_JMP_IMM(BPF_JA, 0, 0, 2), /* goto start */
			BPF_ALU32_IMM(BPF_MOV, R0, 1), /* out: */
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0), /* start: */
			BPF_LD_IMM64(R1, 3), /* note: this takes 2 insns */
			BPF_JMP_IMM(BPF_JGT, R1, 2, -6), /* goto out */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JGE_K: if (3 >= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JGE, R1, 3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JLT | BPF_K jump backwards */
	{
		"JMP_JGT_K: if (2 < 3) return 1 (jump backwards)",
		.u.insns_int = {
			BPF_JMP_IMM(BPF_JA, 0, 0, 2), /* goto start */
			BPF_ALU32_IMM(BPF_MOV, R0, 1), /* out: */
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0), /* start: */
			BPF_LD_IMM64(R1, 2), /* note: this takes 2 insns */
			BPF_JMP_IMM(BPF_JLT, R1, 3, -6), /* goto out */
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JLE_K: if (3 <= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JLE, R1, 3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JNE | BPF_K */
	{
		"JMP_JNE_K: if (3 != 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JNE, R1, 2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JEQ | BPF_K */
	{
		"JMP_JEQ_K: if (3 == 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JEQ, R1, 3, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSET | BPF_K */
	{
		"JMP_JSET_K: if (0x3 & 0x2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JSET, R1, 2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSET_K: if (0x3 & 0xffffffff) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_JMP_IMM(BPF_JSET, R1, 0xffffffff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSGT | BPF_X */
	{
		"JMP_JSGT_X: Signed jump: if (-1 > -2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -2),
			BPF_JMP_REG(BPF_JSGT, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGT_X: Signed jump: if (-1 > -1) return 0",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -1),
			BPF_JMP_REG(BPF_JSGT, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSLT | BPF_X */
	{
		"JMP_JSLT_X: Signed jump: if (-2 < -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -2),
			BPF_JMP_REG(BPF_JSLT, R2, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLT_X: Signed jump: if (-1 < -1) return 0",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -1),
			BPF_JMP_REG(BPF_JSLT, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSGE | BPF_X */
	{
		"JMP_JSGE_X: Signed jump: if (-1 >= -2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -2),
			BPF_JMP_REG(BPF_JSGE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSGE_X: Signed jump: if (-1 >= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -1),
			BPF_JMP_REG(BPF_JSGE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSLE | BPF_X */
	{
		"JMP_JSLE_X: Signed jump: if (-2 <= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -2),
			BPF_JMP_REG(BPF_JSLE, R2, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSLE_X: Signed jump: if (-1 <= -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, -1),
			BPF_JMP_REG(BPF_JSLE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JGT | BPF_X */
	{
		"JMP_JGT_X: if (3 > 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JGT, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JGT_X: Unsigned jump: if (-1 > 1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, 1),
			BPF_JMP_REG(BPF_JGT, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JLT | BPF_X */
	{
		"JMP_JLT_X: if (2 < 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JLT, R2, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JLT_X: Unsigned jump: if (1 < -1) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, -1),
			BPF_LD_IMM64(R2, 1),
			BPF_JMP_REG(BPF_JLT, R2, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JGE | BPF_X */
	{
		"JMP_JGE_X: if (3 >= 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JGE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JGE_X: if (3 >= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 3),
			BPF_JMP_REG(BPF_JGE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JLE | BPF_X */
	{
		"JMP_JLE_X: if (2 <= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JLE, R2, R1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JLE_X: if (3 <= 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 3),
			BPF_JMP_REG(BPF_JLE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		/* Mainly testing JIT + imm64 here. */
		"JMP_JGE_X: ldimm64 test 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JGE, R1, R2, 2),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_LD_IMM64(R0, 0xeeeeeeeeeeeeeeeeULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xeeeeeeeeU } },
	},
	{
		"JMP_JGE_X: ldimm64 test 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JGE, R1, R2, 0),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffffU } },
	},
	{
		"JMP_JGE_X: ldimm64 test 3",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JGE, R1, R2, 4),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_LD_IMM64(R0, 0xeeeeeeeeeeeeeeeeULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JLE_X: ldimm64 test 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JLE, R2, R1, 2),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_LD_IMM64(R0, 0xeeeeeeeeeeeeeeeeULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xeeeeeeeeU } },
	},
	{
		"JMP_JLE_X: ldimm64 test 2",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JLE, R2, R1, 0),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 0xffffffffU } },
	},
	{
		"JMP_JLE_X: ldimm64 test 3",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JLE, R2, R1, 4),
			BPF_LD_IMM64(R0, 0xffffffffffffffffULL),
			BPF_LD_IMM64(R0, 0xeeeeeeeeeeeeeeeeULL),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JNE | BPF_X */
	{
		"JMP_JNE_X: if (3 != 2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JNE, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JEQ | BPF_X */
	{
		"JMP_JEQ_X: if (3 == 3) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 3),
			BPF_JMP_REG(BPF_JEQ, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	/* BPF_JMP | BPF_JSET | BPF_X */
	{
		"JMP_JSET_X: if (0x3 & 0x2) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 2),
			BPF_JMP_REG(BPF_JSET, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JSET_X: if (0x3 & 0xffffffff) return 1",
		.u.insns_int = {
			BPF_ALU32_IMM(BPF_MOV, R0, 0),
			BPF_LD_IMM64(R1, 3),
			BPF_LD_IMM64(R2, 0xffffffff),
			BPF_JMP_REG(BPF_JSET, R1, R2, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, R0, 1),
			BPF_EXIT_INSN(),
		},
		INTERNAL,
		{ },
		{ { 0, 1 } },
	},
	{
		"JMP_JA: Jump, gap, jump, ...",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xababcbac } },
		.fill_helper = bpf_fill_ja,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Maximum possible literals",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xffffffff } },
		.fill_helper = bpf_fill_maxinsns1,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Single literal",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xfefefefe } },
		.fill_helper = bpf_fill_maxinsns2,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Run/add until end",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0x947bf368 } },
		.fill_helper = bpf_fill_maxinsns3,
	},
	{
		"BPF_MAXINSNS: Too many instructions",
		{ },
		CLASSIC | FLAG_NO_DATA | FLAG_EXPECTED_FAIL,
		{ },
		{ },
		.fill_helper = bpf_fill_maxinsns4,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Very long jump",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xabababab } },
		.fill_helper = bpf_fill_maxinsns5,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Ctx heavy transformations",
		{ },
		CLASSIC,
		{ },
		{
			{  1, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) },
			{ 10, !!(SKB_VLAN_TCI & VLAN_TAG_PRESENT) }
		},
		.fill_helper = bpf_fill_maxinsns6,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Call heavy transformations",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 1, 0 }, { 10, 0 } },
		.fill_helper = bpf_fill_maxinsns7,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Jump heavy test",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xffffffff } },
		.fill_helper = bpf_fill_maxinsns8,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Very long jump backwards",
		{ },
		INTERNAL | FLAG_NO_DATA,
		{ },
		{ { 0, 0xcbababab } },
		.fill_helper = bpf_fill_maxinsns9,
	},
	{	/* Mainly checking JIT here. */
		"BPF_MAXINSNS: Edge hopping nuthouse",
		{ },
		INTERNAL | FLAG_NO_DATA,
		{ },
		{ { 0, 0xabababac } },
		.fill_helper = bpf_fill_maxinsns10,
	},
	{
		"BPF_MAXINSNS: Jump, gap, jump, ...",
		{ },
		CLASSIC | FLAG_NO_DATA,
		{ },
		{ { 0, 0xababcbac } },
		.fill_helper = bpf_fill_maxinsns11,
	},
	{
		"BPF_MAXINSNS: ld_abs+get_processor_id",
		{ },
		CLASSIC,
		{ },
		{ { 1, 0xbee } },
		.fill_helper = bpf_fill_ld_abs_get_processor_id,
	},
	{
		"BPF_MAXINSNS: ld_abs+vlan_push/pop",
		{ },
		INTERNAL,
		{ 0x34 },
		{ { ETH_HLEN, 0xbef } },
		.fill_helper = bpf_fill_ld_abs_vlan_push_pop,
	},
	{
		"BPF_MAXINSNS: jump around ld_abs",
		{ },
		INTERNAL,
		{ 10, 11 },
		{ { 2, 10 } },
		.fill_helper = bpf_fill_jump_around_ld_abs,
	},
	/*
	 * LD_IND / LD_ABS on fragmented SKBs
	 */
	{
		"LD_IND byte frag",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x40),
			BPF_STMT(BPF_LD | BPF_IND | BPF_B, 0x0),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x42} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_IND halfword frag",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x40),
			BPF_STMT(BPF_LD | BPF_IND | BPF_H, 0x4),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x4344} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_IND word frag",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x40),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, 0x8),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x21071983} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_IND halfword mixed head/frag",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x40),
			BPF_STMT(BPF_LD | BPF_IND | BPF_H, -0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ [0x3e] = 0x25, [0x3f] = 0x05, },
		{ {0x40, 0x0519} },
		.frag_data = { 0x19, 0x82 },
	},
	{
		"LD_IND word mixed head/frag",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x40),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, -0x2),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ [0x3e] = 0x25, [0x3f] = 0x05, },
		{ {0x40, 0x25051982} },
		.frag_data = { 0x19, 0x82 },
	},
	{
		"LD_ABS byte frag",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_B, 0x40),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x42} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_ABS halfword frag",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_H, 0x44),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x4344} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_ABS word frag",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x48),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ },
		{ {0x40, 0x21071983} },
		.frag_data = {
			0x42, 0x00, 0x00, 0x00,
			0x43, 0x44, 0x00, 0x00,
			0x21, 0x07, 0x19, 0x83,
		},
	},
	{
		"LD_ABS halfword mixed head/frag",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_H, 0x3f),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ [0x3e] = 0x25, [0x3f] = 0x05, },
		{ {0x40, 0x0519} },
		.frag_data = { 0x19, 0x82 },
	},
	{
		"LD_ABS word mixed head/frag",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x3e),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_SKB_FRAG,
		{ [0x3e] = 0x25, [0x3f] = 0x05, },
		{ {0x40, 0x25051982} },
		.frag_data = { 0x19, 0x82 },
	},
	/*
	 * LD_IND / LD_ABS on non fragmented SKBs
	 */
	{
		/*
		 * this tests that the JIT/interpreter correctly resets X
		 * before using it in an LD_IND instruction.
		 */
		"LD_IND byte default X",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_IND | BPF_B, 0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{ [0x1] = 0x42 },
		{ {0x40, 0x42 } },
	},
	{
		"LD_IND byte positive offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x3e),
			BPF_STMT(BPF_LD | BPF_IND | BPF_B, 0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{ [0x3c] = 0x25, [0x3d] = 0x05,  [0x3e] = 0x19, [0x3f] = 0x82 },
		{ {0x40, 0x82 } },
	},
	{
		"LD_IND byte negative offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x3e),
			BPF_STMT(BPF_LD | BPF_IND | BPF_B, -0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{ [0x3c] = 0x25, [0x3d] = 0x05,  [0x3e] = 0x19, [0x3f] = 0x82 },
		{ {0x40, 0x05 } },
	},
	{
		"LD_IND halfword positive offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_H, 0x2),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
		},
		{ {0x40, 0xdd88 } },
	},
	{
		"LD_IND halfword negative offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_H, -0x2),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
		},
		{ {0x40, 0xbb66 } },
	},
	{
		"LD_IND halfword unaligned",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_H, -0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
		},
		{ {0x40, 0x66cc } },
	},
	{
		"LD_IND word positive offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, 0x4),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xee99ffaa } },
	},
	{
		"LD_IND word negative offset",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, -0x4),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xaa55bb66 } },
	},
	{
		"LD_IND word unaligned (addr & 3 == 2)",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, -0x2),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xbb66cc77 } },
	},
	{
		"LD_IND word unaligned (addr & 3 == 1)",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, -0x3),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0x55bb66cc } },
	},
	{
		"LD_IND word unaligned (addr & 3 == 3)",
		.u.insns = {
			BPF_STMT(BPF_LDX | BPF_IMM, 0x20),
			BPF_STMT(BPF_LD | BPF_IND | BPF_W, -0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0x66cc77dd } },
	},
	{
		"LD_ABS byte",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_B, 0x20),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xcc } },
	},
	{
		"LD_ABS halfword",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_H, 0x22),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xdd88 } },
	},
	{
		"LD_ABS halfword unaligned",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_H, 0x25),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0x99ff } },
	},
	{
		"LD_ABS word",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x1c),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xaa55bb66 } },
	},
	{
		"LD_ABS word unaligned (addr & 3 == 2)",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x22),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0xdd88ee99 } },
	},
	{
		"LD_ABS word unaligned (addr & 3 == 1)",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x21),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0x77dd88ee } },
	},
	{
		"LD_ABS word unaligned (addr & 3 == 3)",
		.u.insns = {
			BPF_STMT(BPF_LD | BPF_ABS | BPF_W, 0x23),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC,
		{
			[0x1c] = 0xaa, [0x1d] = 0x55,
			[0x1e] = 0xbb, [0x1f] = 0x66,
			[0x20] = 0xcc, [0x21] = 0x77,
			[0x22] = 0xdd, [0x23] = 0x88,
			[0x24] = 0xee, [0x25] = 0x99,
			[0x26] = 0xff, [0x27] = 0xaa,
		},
		{ {0x40, 0x88ee99ff } },
	},
	/*
	 * verify that the interpreter or JIT correctly sets A and X
	 * to 0.
	 */
	{
		"ADD default X",
		.u.insns = {
			/*
			 * A = 0x42
			 * A = A + X
			 * ret A
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x42),
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x42 } },
	},
	{
		"ADD default A",
		.u.insns = {
			/*
			 * A = A + 0x42
			 * ret A
			 */
			BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 0x42),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x42 } },
	},
	{
		"SUB default X",
		.u.insns = {
			/*
			 * A = 0x66
			 * A = A - X
			 * ret A
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x66),
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x66 } },
	},
	{
		"SUB default A",
		.u.insns = {
			/*
			 * A = A - -0x66
			 * ret A
			 */
			BPF_STMT(BPF_ALU | BPF_SUB | BPF_K, -0x66),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x66 } },
	},
	{
		"MUL default X",
		.u.insns = {
			/*
			 * A = 0x42
			 * A = A * X
			 * ret A
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x42),
			BPF_STMT(BPF_ALU | BPF_MUL | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"MUL default A",
		.u.insns = {
			/*
			 * A = A * 0x66
			 * ret A
			 */
			BPF_STMT(BPF_ALU | BPF_MUL | BPF_K, 0x66),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"DIV default X",
		.u.insns = {
			/*
			 * A = 0x42
			 * A = A / X ; this halt the filter execution if X is 0
			 * ret 0x42
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x42),
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_K, 0x42),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"DIV default A",
		.u.insns = {
			/*
			 * A = A / 1
			 * ret A
			 */
			BPF_STMT(BPF_ALU | BPF_DIV | BPF_K, 0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"MOD default X",
		.u.insns = {
			/*
			 * A = 0x42
			 * A = A mod X ; this halt the filter execution if X is 0
			 * ret 0x42
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x42),
			BPF_STMT(BPF_ALU | BPF_MOD | BPF_X, 0),
			BPF_STMT(BPF_RET | BPF_K, 0x42),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"MOD default A",
		.u.insns = {
			/*
			 * A = A mod 1
			 * ret A
			 */
			BPF_STMT(BPF_ALU | BPF_MOD | BPF_K, 0x1),
			BPF_STMT(BPF_RET | BPF_A, 0x0),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x0 } },
	},
	{
		"JMP EQ default A",
		.u.insns = {
			/*
			 * cmp A, 0x0, 0, 1
			 * ret 0x42
			 * ret 0x66
			 */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 0x42),
			BPF_STMT(BPF_RET | BPF_K, 0x66),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x42 } },
	},
	{
		"JMP EQ default X",
		.u.insns = {
			/*
			 * A = 0x0
			 * cmp A, X, 0, 1
			 * ret 0x42
			 * ret 0x66
			 */
			BPF_STMT(BPF_LD | BPF_IMM, 0x0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_X, 0x0, 0, 1),
			BPF_STMT(BPF_RET | BPF_K, 0x42),
			BPF_STMT(BPF_RET | BPF_K, 0x66),
		},
		CLASSIC | FLAG_NO_DATA,
		{},
		{ {0x1, 0x42 } },
	},
	{
		"LD_ABS with helper changing skb data",
		{ },
		INTERNAL,
		{ 0x34 },
		{ { ETH_HLEN, 42 } },
		.fill_helper = bpf_fill_ld_abs_vlan_push_pop2,
	},
};

static struct net_device dev;

static struct sk_buff *populate_skb(char *buf, int size)
{
	struct sk_buff *skb;

	if (size >= MAX_DATA)
		return NULL;

	skb = alloc_skb(MAX_DATA, GFP_KERNEL);
	if (!skb)
		return NULL;

	__skb_put_data(skb, buf, size);

	/* Initialize a fake skb with test pattern. */
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = SKB_TYPE;
	skb->mark = SKB_MARK;
	skb->hash = SKB_HASH;
	skb->queue_mapping = SKB_QUEUE_MAP;
	skb->vlan_tci = SKB_VLAN_TCI;
	skb->vlan_proto = htons(ETH_P_IP);
	skb->dev = &dev;
	skb->dev->ifindex = SKB_DEV_IFINDEX;
	skb->dev->type = SKB_DEV_TYPE;
	skb_set_network_header(skb, min(size, ETH_HLEN));

	return skb;
}

static void *generate_test_data(struct bpf_test *test, int sub)
{
	struct sk_buff *skb;
	struct page *page;

	if (test->aux & FLAG_NO_DATA)
		return NULL;

	/* Test case expects an skb, so populate one. Various
	 * subtests generate skbs of different sizes based on
	 * the same data.
	 */
	skb = populate_skb(test->data, test->test[sub].data_size);
	if (!skb)
		return NULL;

	if (test->aux & FLAG_SKB_FRAG) {
		/*
		 * when the test requires a fragmented skb, add a
		 * single fragment to the skb, filled with
		 * test->frag_data.
		 */
		void *ptr;

		page = alloc_page(GFP_KERNEL);

		if (!page)
			goto err_kfree_skb;

		ptr = kmap(page);
		if (!ptr)
			goto err_free_page;
		memcpy(ptr, test->frag_data, MAX_DATA);
		kunmap(page);
		skb_add_rx_frag(skb, 0, page, 0, MAX_DATA, MAX_DATA);
	}

	return skb;

err_free_page:
	__free_page(page);
err_kfree_skb:
	kfree_skb(skb);
	return NULL;
}

static void release_test_data(const struct bpf_test *test, void *data)
{
	if (test->aux & FLAG_NO_DATA)
		return;

	kfree_skb(data);
}

static int filter_length(int which)
{
	struct sock_filter *fp;
	int len;

	if (tests[which].fill_helper)
		return tests[which].u.ptr.len;

	fp = tests[which].u.insns;
	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].k != 0)
			break;

	return len + 1;
}

static void *filter_pointer(int which)
{
	if (tests[which].fill_helper)
		return tests[which].u.ptr.insns;
	else
		return tests[which].u.insns;
}

static struct bpf_prog *generate_filter(int which, int *err)
{
	__u8 test_type = tests[which].aux & TEST_TYPE_MASK;
	unsigned int flen = filter_length(which);
	void *fptr = filter_pointer(which);
	struct sock_fprog_kern fprog;
	struct bpf_prog *fp;

	switch (test_type) {
	case CLASSIC:
		fprog.filter = fptr;
		fprog.len = flen;

		*err = bpf_prog_create(&fp, &fprog);
		if (tests[which].aux & FLAG_EXPECTED_FAIL) {
			if (*err == -EINVAL) {
				pr_cont("PASS\n");
				/* Verifier rejected filter as expected. */
				*err = 0;
				return NULL;
			} else {
				pr_cont("UNEXPECTED_PASS\n");
				/* Verifier didn't reject the test that's
				 * bad enough, just return!
				 */
				*err = -EINVAL;
				return NULL;
			}
		}
		if (*err) {
			pr_cont("FAIL to prog_create err=%d len=%d\n",
				*err, fprog.len);
			return NULL;
		}
		break;

	case INTERNAL:
		fp = bpf_prog_alloc(bpf_prog_size(flen), 0);
		if (fp == NULL) {
			pr_cont("UNEXPECTED_FAIL no memory left\n");
			*err = -ENOMEM;
			return NULL;
		}

		fp->len = flen;
		/* Type doesn't really matter here as long as it's not unspec. */
		fp->type = BPF_PROG_TYPE_SOCKET_FILTER;
		memcpy(fp->insnsi, fptr, fp->len * sizeof(struct bpf_insn));
		fp->aux->stack_depth = tests[which].stack_depth;

		/* We cannot error here as we don't need type compatibility
		 * checks.
		 */
		fp = bpf_prog_select_runtime(fp, err);
		if (*err) {
			pr_cont("FAIL to select_runtime err=%d\n", *err);
			return NULL;
		}
		break;
	}

	*err = 0;
	return fp;
}

static void release_filter(struct bpf_prog *fp, int which)
{
	__u8 test_type = tests[which].aux & TEST_TYPE_MASK;

	switch (test_type) {
	case CLASSIC:
		bpf_prog_destroy(fp);
		break;
	case INTERNAL:
		bpf_prog_free(fp);
		break;
	}
}

static int __run_one(const struct bpf_prog *fp, const void *data,
		     int runs, u64 *duration)
{
	u64 start, finish;
	int ret = 0, i;

	start = ktime_get_ns();

	for (i = 0; i < runs; i++)
		ret = BPF_PROG_RUN(fp, data);

	finish = ktime_get_ns();

	*duration = finish - start;
	do_div(*duration, runs);

	return ret;
}

static int run_one(const struct bpf_prog *fp, struct bpf_test *test)
{
	int err_cnt = 0, i, runs = MAX_TESTRUNS;

	for (i = 0; i < MAX_SUBTESTS; i++) {
		void *data;
		u64 duration;
		u32 ret;

		if (test->test[i].data_size == 0 &&
		    test->test[i].result == 0)
			break;

		data = generate_test_data(test, i);
		if (!data && !(test->aux & FLAG_NO_DATA)) {
			pr_cont("data generation failed ");
			err_cnt++;
			break;
		}
		ret = __run_one(fp, data, runs, &duration);
		release_test_data(test, data);

		if (ret == test->test[i].result) {
			pr_cont("%lld ", duration);
		} else {
			pr_cont("ret %d != %d ", ret,
				test->test[i].result);
			err_cnt++;
		}
	}

	return err_cnt;
}

static char test_name[64];
module_param_string(test_name, test_name, sizeof(test_name), 0);

static int test_id = -1;
module_param(test_id, int, 0);

static int test_range[2] = { 0, ARRAY_SIZE(tests) - 1 };
module_param_array(test_range, int, NULL, 0);

static __init int find_test_index(const char *test_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!strcmp(tests[i].descr, test_name))
			return i;
	}
	return -1;
}

static __init int prepare_bpf_tests(void)
{
	int i;

	if (test_id >= 0) {
		/*
		 * if a test_id was specified, use test_range to
		 * cover only that test.
		 */
		if (test_id >= ARRAY_SIZE(tests)) {
			pr_err("test_bpf: invalid test_id specified.\n");
			return -EINVAL;
		}

		test_range[0] = test_id;
		test_range[1] = test_id;
	} else if (*test_name) {
		/*
		 * if a test_name was specified, find it and setup
		 * test_range to cover only that test.
		 */
		int idx = find_test_index(test_name);

		if (idx < 0) {
			pr_err("test_bpf: no test named '%s' found.\n",
			       test_name);
			return -EINVAL;
		}
		test_range[0] = idx;
		test_range[1] = idx;
	} else {
		/*
		 * check that the supplied test_range is valid.
		 */
		if (test_range[0] >= ARRAY_SIZE(tests) ||
		    test_range[1] >= ARRAY_SIZE(tests) ||
		    test_range[0] < 0 || test_range[1] < 0) {
			pr_err("test_bpf: test_range is out of bound.\n");
			return -EINVAL;
		}

		if (test_range[1] < test_range[0]) {
			pr_err("test_bpf: test_range is ending before it starts.\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (tests[i].fill_helper &&
		    tests[i].fill_helper(&tests[i]) < 0)
			return -ENOMEM;
	}

	return 0;
}

static __init void destroy_bpf_tests(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (tests[i].fill_helper)
			kfree(tests[i].u.ptr.insns);
	}
}

static bool exclude_test(int test_id)
{
	return test_id < test_range[0] || test_id > test_range[1];
}

static __init int test_bpf(void)
{
	int i, err_cnt = 0, pass_cnt = 0;
	int jit_cnt = 0, run_cnt = 0;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct bpf_prog *fp;
		int err;

		if (exclude_test(i))
			continue;

		pr_info("#%d %s ", i, tests[i].descr);

		fp = generate_filter(i, &err);
		if (fp == NULL) {
			if (err == 0) {
				pass_cnt++;
				continue;
			}
			err_cnt++;
			continue;
		}

		pr_cont("jited:%u ", fp->jited);

		run_cnt++;
		if (fp->jited)
			jit_cnt++;

		err = run_one(fp, &tests[i]);
		release_filter(fp, i);

		if (err) {
			pr_cont("FAIL (%d times)\n", err);
			err_cnt++;
		} else {
			pr_cont("PASS\n");
			pass_cnt++;
		}
	}

	pr_info("Summary: %d PASSED, %d FAILED, [%d/%d JIT'ed]\n",
		pass_cnt, err_cnt, jit_cnt, run_cnt);

	return err_cnt ? -EINVAL : 0;
}

static int __init test_bpf_init(void)
{
	int ret;

	ret = prepare_bpf_tests();
	if (ret < 0)
		return ret;

	ret = test_bpf();

	destroy_bpf_tests();
	return ret;
}

static void __exit test_bpf_exit(void)
{
}

module_init(test_bpf_init);
module_exit(test_bpf_exit);

MODULE_LICENSE("GPL");
