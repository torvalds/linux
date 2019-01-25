/*
 * Testsuite for eBPF verifier
 *
 * Copyright (c) 2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2017 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <endian.h>
#include <asm/types.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <sched.h>
#include <limits.h>
#include <assert.h>

#include <sys/capability.h>

#include <linux/unistd.h>
#include <linux/filter.h>
#include <linux/bpf_perf_event.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>

#include <bpf/bpf.h>

#ifdef HAVE_GENHDR
# include "autoconf.h"
#else
# if defined(__i386) || defined(__x86_64) || defined(__s390x__) || defined(__aarch64__)
#  define CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS 1
# endif
#endif
#include "bpf_rlimit.h"
#include "bpf_rand.h"
#include "bpf_util.h"
#include "../../../include/linux/filter.h"

#define MAX_INSNS	BPF_MAXINSNS
#define MAX_FIXUPS	8
#define MAX_NR_MAPS	13
#define MAX_TEST_RUNS	8
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64

#define F_NEEDS_EFFICIENT_UNALIGNED_ACCESS	(1 << 0)
#define F_LOAD_WITH_STRICT_ALIGNMENT		(1 << 1)

#define UNPRIV_SYSCTL "kernel/unprivileged_bpf_disabled"
static bool unpriv_disabled = false;

struct bpf_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	int fixup_map_hash_8b[MAX_FIXUPS];
	int fixup_map_hash_48b[MAX_FIXUPS];
	int fixup_map_hash_16b[MAX_FIXUPS];
	int fixup_map_array_48b[MAX_FIXUPS];
	int fixup_map_sockmap[MAX_FIXUPS];
	int fixup_map_sockhash[MAX_FIXUPS];
	int fixup_map_xskmap[MAX_FIXUPS];
	int fixup_map_stacktrace[MAX_FIXUPS];
	int fixup_prog1[MAX_FIXUPS];
	int fixup_prog2[MAX_FIXUPS];
	int fixup_map_in_map[MAX_FIXUPS];
	int fixup_cgroup_storage[MAX_FIXUPS];
	int fixup_percpu_cgroup_storage[MAX_FIXUPS];
	const char *errstr;
	const char *errstr_unpriv;
	uint32_t retval, retval_unpriv, insn_processed;
	enum {
		UNDEF,
		ACCEPT,
		REJECT
	} result, result_unpriv;
	enum bpf_prog_type prog_type;
	uint8_t flags;
	__u8 data[TEST_DATA_LEN];
	void (*fill_helper)(struct bpf_test *self);
	uint8_t runs;
	struct {
		uint32_t retval, retval_unpriv;
		union {
			__u8 data[TEST_DATA_LEN];
			__u64 data64[TEST_DATA_LEN / 8];
		};
	} retvals[MAX_TEST_RUNS];
};

/* Note we want this to be 64 bit aligned so that the end of our array is
 * actually the end of the structure.
 */
#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct other_val {
	long long foo;
	long long bar;
};

static void bpf_fill_ld_abs_vlan_push_pop(struct bpf_test *self)
{
	/* test: {skb->data[0], vlan_push} x 68 + {skb->data[0], vlan_pop} x 68 */
#define PUSH_CNT 51
	unsigned int len = BPF_MAXINSNS;
	struct bpf_insn *insn = self->insns;
	int i = 0, j, k = 0;

	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
loop:
	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0x34, len - i - 2);
		i++;
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
		insn[i++] = BPF_MOV64_IMM(BPF_REG_2, 1);
		insn[i++] = BPF_MOV64_IMM(BPF_REG_3, 2);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_skb_vlan_push),
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, len - i - 2);
		i++;
	}

	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0x34, len - i - 2);
		i++;
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_skb_vlan_pop),
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, len - i - 2);
		i++;
	}
	if (++k < 5)
		goto loop;

	for (; i < len - 1; i++)
		insn[i] = BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 0xbef);
	insn[len - 1] = BPF_EXIT_INSN();
}

static void bpf_fill_jump_around_ld_abs(struct bpf_test *self)
{
	struct bpf_insn *insn = self->insns;
	unsigned int len = BPF_MAXINSNS;
	int i = 0;

	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	insn[i++] = BPF_LD_ABS(BPF_B, 0);
	insn[i] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 10, len - i - 2);
	i++;
	while (i < len - 1)
		insn[i++] = BPF_LD_ABS(BPF_B, 1);
	insn[i] = BPF_EXIT_INSN();
}

static void bpf_fill_rand_ld_dw(struct bpf_test *self)
{
	struct bpf_insn *insn = self->insns;
	uint64_t res = 0;
	int i = 0;

	insn[i++] = BPF_MOV32_IMM(BPF_REG_0, 0);
	while (i < self->retval) {
		uint64_t val = bpf_semi_rand_get();
		struct bpf_insn tmp[2] = { BPF_LD_IMM64(BPF_REG_1, val) };

		res ^= val;
		insn[i++] = tmp[0];
		insn[i++] = tmp[1];
		insn[i++] = BPF_ALU64_REG(BPF_XOR, BPF_REG_0, BPF_REG_1);
	}
	insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_0);
	insn[i++] = BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 32);
	insn[i++] = BPF_ALU64_REG(BPF_XOR, BPF_REG_0, BPF_REG_1);
	insn[i] = BPF_EXIT_INSN();
	res ^= (res >> 32);
	self->retval = (uint32_t)res;
}

/* BPF_SK_LOOKUP contains 13 instructions, if you need to fix up maps */
#define BPF_SK_LOOKUP							\
	/* struct bpf_sock_tuple tuple = {} */				\
	BPF_MOV64_IMM(BPF_REG_2, 0),					\
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_2, -8),			\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -16),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -24),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -32),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -40),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -48),		\
	/* sk = sk_lookup_tcp(ctx, &tuple, sizeof tuple, 0, 0) */	\
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),				\
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -48),				\
	BPF_MOV64_IMM(BPF_REG_3, sizeof(struct bpf_sock_tuple)),	\
	BPF_MOV64_IMM(BPF_REG_4, 0),					\
	BPF_MOV64_IMM(BPF_REG_5, 0),					\
	BPF_EMIT_CALL(BPF_FUNC_sk_lookup_tcp)

/* BPF_DIRECT_PKT_R2 contains 7 instructions, it initializes default return
 * value into 0 and does necessary preparation for direct packet access
 * through r2. The allowed access range is 8 bytes.
 */
#define BPF_DIRECT_PKT_R2						\
	BPF_MOV64_IMM(BPF_REG_0, 0),					\
	BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,			\
		    offsetof(struct __sk_buff, data)),			\
	BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,			\
		    offsetof(struct __sk_buff, data_end)),		\
	BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),				\
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 8),				\
	BPF_JMP_REG(BPF_JLE, BPF_REG_4, BPF_REG_3, 1),			\
	BPF_EXIT_INSN()

/* BPF_RAND_UEXT_R7 contains 4 instructions, it initializes R7 into a random
 * positive u32, and zero-extend it into 64-bit.
 */
#define BPF_RAND_UEXT_R7						\
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,			\
		     BPF_FUNC_get_prandom_u32),				\
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),				\
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 33),				\
	BPF_ALU64_IMM(BPF_RSH, BPF_REG_7, 33)

/* BPF_RAND_SEXT_R7 contains 5 instructions, it initializes R7 into a random
 * negative u32, and sign-extend it into 64-bit.
 */
#define BPF_RAND_SEXT_R7						\
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,			\
		     BPF_FUNC_get_prandom_u32),				\
	BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),				\
	BPF_ALU64_IMM(BPF_OR, BPF_REG_7, 0x80000000),			\
	BPF_ALU64_IMM(BPF_LSH, BPF_REG_7, 32),				\
	BPF_ALU64_IMM(BPF_ARSH, BPF_REG_7, 32)

static struct bpf_test tests[] = {
#define FILL_ARRAY
#include <verifier/tests.h>
#undef FILL_ARRAY
	{
		"invalid 64-bit BPF_END",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_0, 0),
			{
				.code  = BPF_ALU64 | BPF_END | BPF_TO_LE,
				.dst_reg = BPF_REG_0,
				.src_reg = 0,
				.off   = 0,
				.imm   = 32,
			},
			BPF_EXIT_INSN(),
		},
		.errstr = "unknown opcode d7",
		.result = REJECT,
	},
	{
		"XDP, using ifindex from netdev",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, ingress_ifindex)),
			BPF_JMP_IMM(BPF_JLT, BPF_REG_2, 1, 1),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.retval = 1,
	},
	{
		"meta access, test1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_SUB, BPF_REG_0, 8),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet, off=-8",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test3",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test4",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_4),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test5",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_3),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_4, 3),
			BPF_MOV64_IMM(BPF_REG_2, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_xdp_adjust_meta),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_3, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R3 !read_ok",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test6",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_3),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_0, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test7",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_3),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test8",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 0xFFFF),
			BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test9",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 0xFFFF),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 1),
			BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test10",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_IMM(BPF_REG_5, 42),
			BPF_MOV64_IMM(BPF_REG_6, 24),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_5, -8),
			BPF_STX_XADD(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -8),
			BPF_JMP_IMM(BPF_JGT, BPF_REG_5, 100, 6),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_3, BPF_REG_5),
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_3),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_5, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_2, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "invalid access to packet",
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test11",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_IMM(BPF_REG_5, 42),
			BPF_MOV64_IMM(BPF_REG_6, 24),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_5, -8),
			BPF_STX_XADD(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -8),
			BPF_JMP_IMM(BPF_JGT, BPF_REG_5, 100, 6),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_2, BPF_REG_5),
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_5, BPF_REG_5, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"meta access, test12",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_4, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_3),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 16),
			BPF_JMP_REG(BPF_JGT, BPF_REG_5, BPF_REG_4, 5),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_3, 0),
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, 16),
			BPF_JMP_REG(BPF_JGT, BPF_REG_5, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"arithmetic ops make PTR_TO_CTX unusable",
		.insns = {
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1,
				      offsetof(struct __sk_buff, data) -
				      offsetof(struct __sk_buff, mark)),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_EXIT_INSN(),
		},
		.errstr = "dereference of modified ctx ptr",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	},
	{
		"pkt_end - pkt_start is allowed",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_2),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = TEST_DATA_LEN,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	},
	{
		"XDP pkt read, pkt_end mangling, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_3, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R3 pointer arithmetic on pkt_end",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"XDP pkt read, pkt_end mangling, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_ALU64_IMM(BPF_SUB, BPF_REG_3, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R3 pointer arithmetic on pkt_end",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"XDP pkt read, pkt_data' > pkt_end, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' > pkt_end, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' > pkt_end, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 0),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end > pkt_data', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end > pkt_data', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end > pkt_data', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' < pkt_end, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' < pkt_end, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' < pkt_end, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end < pkt_data', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end < pkt_data', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end < pkt_data', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 0),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' >= pkt_end, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' >= pkt_end, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' >= pkt_end, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end >= pkt_data', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end >= pkt_data', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end >= pkt_data', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' <= pkt_end, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' <= pkt_end, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data' <= pkt_end, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end <= pkt_data', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end <= pkt_data', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_end <= pkt_data', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' > pkt_data, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' > pkt_data, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' > pkt_data, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_1, BPF_REG_3, 0),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data > pkt_meta', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data > pkt_meta', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data > pkt_meta', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' < pkt_data, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' < pkt_data, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' < pkt_data, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data < pkt_meta', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data < pkt_meta', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data < pkt_meta', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_3, BPF_REG_1, 0),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' >= pkt_data, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' >= pkt_data, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' >= pkt_data, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_1, BPF_REG_3, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data >= pkt_meta', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data >= pkt_meta', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data >= pkt_meta', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' <= pkt_data, good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' <= pkt_data, bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -4),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_meta' <= pkt_data, bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_1, BPF_REG_3, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data <= pkt_meta', good access",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data <= pkt_meta', bad access 1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"XDP pkt read, pkt_data <= pkt_meta', bad access 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data_meta)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_3, BPF_REG_1, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, -5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R1 offset is outside of the packet",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"check deducing bounds from const, 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 1, 0),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R0 tried to subtract pointer from scalar",
	},
	{
		"check deducing bounds from const, 2",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 1, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSLE, BPF_REG_0, 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_1, BPF_REG_0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"check deducing bounds from const, 3",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSLE, BPF_REG_0, 0, 0),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R0 tried to subtract pointer from scalar",
	},
	{
		"check deducing bounds from const, 4",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSLE, BPF_REG_0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_1, BPF_REG_0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
	},
	{
		"check deducing bounds from const, 5",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 1, 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R0 tried to subtract pointer from scalar",
	},
	{
		"check deducing bounds from const, 6",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R0 tried to subtract pointer from scalar",
	},
	{
		"check deducing bounds from const, 7",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, ~0),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 0),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_1, BPF_REG_0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "dereference of modified ctx ptr",
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"check deducing bounds from const, 8",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, ~0),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 1),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "dereference of modified ctx ptr",
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"check deducing bounds from const, 9",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_0, 0, 0),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R0 tried to subtract pointer from scalar",
	},
	{
		"check deducing bounds from const, 10",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSLE, BPF_REG_0, 0, 0),
			/* Marks reg as unknown. */
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_0, 0),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "math between ctx pointer and register with unbounded min value is not allowed",
	},
	{
		"bpf_exit with invalid return code. test1",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_EXIT_INSN(),
		},
		.errstr = "R0 has value (0x0; 0xffffffff)",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test3",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_0, 3),
			BPF_EXIT_INSN(),
		},
		.errstr = "R0 has value (0x0; 0x3)",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test4",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test5",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.errstr = "R0 has value (0x2; 0x0)",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test6",
		.insns = {
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.errstr = "R0 is not a known value (ctx)",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"bpf_exit with invalid return code. test7",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 4),
			BPF_ALU64_REG(BPF_MUL, BPF_REG_0, BPF_REG_2),
			BPF_EXIT_INSN(),
		},
		.errstr = "R0 has unknown scalar value",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK,
	},
	{
		"calls: basic sanity",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.result = ACCEPT,
	},
	{
		"calls: not on unpriviledged",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"calls: div by 0 in subprog",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 8),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_2, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(BPF_REG_2, 0),
			BPF_MOV32_IMM(BPF_REG_3, 1),
			BPF_ALU32_REG(BPF_DIV, BPF_REG_3, BPF_REG_2),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"calls: multiple ret types in subprog 1",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 8),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_2, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_MOV32_IMM(BPF_REG_0, 42),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.errstr = "R0 invalid mem access 'inv'",
	},
	{
		"calls: multiple ret types in subprog 2",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 8),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_0),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_2, BPF_REG_1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_0, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 9),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_6,
				    offsetof(struct __sk_buff, data)),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 64),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 16 },
		.result = REJECT,
		.errstr = "R0 min value is outside of the array range",
	},
	{
		"calls: overlapping caller/callee",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "last insn is not an exit or jmp",
		.result = REJECT,
	},
	{
		"calls: wrong recursive calls",
		.insns = {
			BPF_JMP_IMM(BPF_JA, 0, 0, 4),
			BPF_JMP_IMM(BPF_JA, 0, 0, 4),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "jump out of range",
		.result = REJECT,
	},
	{
		"calls: wrong src reg",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 2, 0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "BPF_CALL uses reserved fields",
		.result = REJECT,
	},
	{
		"calls: wrong off value",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, -1, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "BPF_CALL uses reserved fields",
		.result = REJECT,
	},
	{
		"calls: jump back loop",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -1),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge from insn 0 to 0",
		.result = REJECT,
	},
	{
		"calls: conditional call",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "jump out of range",
		.result = REJECT,
	},
	{
		"calls: conditional call 2",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.result = ACCEPT,
	},
	{
		"calls: conditional call 3",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_JMP_IMM(BPF_JA, 0, 0, 4),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, -6),
			BPF_MOV64_IMM(BPF_REG_0, 3),
			BPF_JMP_IMM(BPF_JA, 0, 0, -6),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge from insn",
		.result = REJECT,
	},
	{
		"calls: conditional call 4",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, -5),
			BPF_MOV64_IMM(BPF_REG_0, 3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.result = ACCEPT,
	},
	{
		"calls: conditional call 5",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, -6),
			BPF_MOV64_IMM(BPF_REG_0, 3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge from insn",
		.result = REJECT,
	},
	{
		"calls: conditional call 6",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, -2),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge from insn",
		.result = REJECT,
	},
	{
		"calls: using r0 returned by callee",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.result = ACCEPT,
	},
	{
		"calls: using uninit r0 from callee",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"calls: callee is using r1",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, len)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_ACT,
		.result = ACCEPT,
		.retval = TEST_DATA_LEN,
	},
	{
		"calls: callee using args1",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = POINTER_VALUE,
	},
	{
		"calls: callee using wrong args2",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "R2 !read_ok",
		.result = REJECT,
	},
	{
		"calls: callee using two args",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_6,
				    offsetof(struct __sk_buff, len)),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6,
				    offsetof(struct __sk_buff, len)),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_2),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = TEST_DATA_LEN + TEST_DATA_LEN - ETH_HLEN - ETH_HLEN,
	},
	{
		"calls: callee changing pkt pointers",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_8, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_8, 8),
			BPF_JMP_REG(BPF_JGT, BPF_REG_8, BPF_REG_7, 2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			/* clear_all_pkt_pointers() has to walk all frames
			 * to make sure that pkt pointers in the caller
			 * are cleared when callee is calling a helper that
			 * adjusts packet size
			 */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
			BPF_MOV32_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_xdp_adjust_head),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "R6 invalid mem access 'inv'",
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: two calls with args",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, len)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = TEST_DATA_LEN + TEST_DATA_LEN,
	},
	{
		"calls: calls with stack arith",
		.insns = {
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -64),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -64),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -64),
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 42,
	},
	{
		"calls: calls with misaligned stack access",
		.insns = {
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -63),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -61),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -63),
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.flags = F_LOAD_WITH_STRICT_ALIGNMENT,
		.errstr = "misaligned stack access",
		.result = REJECT,
	},
	{
		"calls: calls control flow, jump test",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 43),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, -3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 43,
	},
	{
		"calls: calls control flow, jump test 2",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 43),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "jump out of range from insn 1 to 4",
		.result = REJECT,
	},
	{
		"calls: two calls with bad jump",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, len)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, -3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "jump out of range from insn 11 to 9",
		.result = REJECT,
	},
	{
		"calls: recursive call. test1",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -1),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge",
		.result = REJECT,
	},
	{
		"calls: recursive call. test2",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "back-edge",
		.result = REJECT,
	},
	{
		"calls: unreachable code",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "unreachable insn 6",
		.result = REJECT,
	},
	{
		"calls: invalid call",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -4),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "invalid destination",
		.result = REJECT,
	},
	{
		"calls: invalid call 2",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 0x7fffffff),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "invalid destination",
		.result = REJECT,
	},
	{
		"calls: jumping across function bodies. test1",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, -3),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "jump out of range",
		.result = REJECT,
	},
	{
		"calls: jumping across function bodies. test2",
		.insns = {
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "jump out of range",
		.result = REJECT,
	},
	{
		"calls: call without exit",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, -2),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "not an exit",
		.result = REJECT,
	},
	{
		"calls: call into middle of ld_imm64",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_LD_IMM64(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "last insn",
		.result = REJECT,
	},
	{
		"calls: call into middle of other call",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "last insn",
		.result = REJECT,
	},
	{
		"calls: ld_abs with changing ctx data in callee",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 5),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_7),
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_2, 1),
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_skb_vlan_push),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "BPF_LD_[ABS|IND] instructions cannot be mixed",
		.result = REJECT,
	},
	{
		"calls: two calls with bad fallthrough",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_0),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, len)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.errstr = "not an exit",
		.result = REJECT,
	},
	{
		"calls: two calls with stack read",
		.insns = {
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 6),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.result = ACCEPT,
	},
	{
		"calls: two calls with stack write",
		.insns = {
			/* main prog */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -16),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 7),
			BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_8, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_8),
			/* write into stack frame of main prog */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* read from stack frame of main prog */
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.result = ACCEPT,
	},
	{
		"calls: stack overflow using two frames (pre-call access)",
		.insns = {
			/* prog 1 */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* prog 2 */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.errstr = "combined stack size",
		.result = REJECT,
	},
	{
		"calls: stack overflow using two frames (post-call access)",
		.insns = {
			/* prog 1 */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 2),
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_EXIT_INSN(),

			/* prog 2 */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.errstr = "combined stack size",
		.result = REJECT,
	},
	{
		"calls: stack depth check using three frames. test1",
		.insns = {
			/* main */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 4), /* call A */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 5), /* call B */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -32, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* A */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -256, 0),
			BPF_EXIT_INSN(),
			/* B */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, -3), /* call A */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -64, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		/* stack_main=32, stack_A=256, stack_B=64
		 * and max(main+A, main+A+B) < 512
		 */
		.result = ACCEPT,
	},
	{
		"calls: stack depth check using three frames. test2",
		.insns = {
			/* main */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 4), /* call A */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 5), /* call B */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -32, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* A */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -64, 0),
			BPF_EXIT_INSN(),
			/* B */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, -3), /* call A */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -256, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		/* stack_main=32, stack_A=64, stack_B=256
		 * and max(main+A, main+A+B) < 512
		 */
		.result = ACCEPT,
	},
	{
		"calls: stack depth check using three frames. test3",
		.insns = {
			/* main */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 6), /* call A */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 8), /* call B */
			BPF_JMP_IMM(BPF_JGE, BPF_REG_6, 0, 1),
			BPF_ST_MEM(BPF_B, BPF_REG_10, -64, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* A */
			BPF_JMP_IMM(BPF_JLT, BPF_REG_1, 10, 1),
			BPF_EXIT_INSN(),
			BPF_ST_MEM(BPF_B, BPF_REG_10, -224, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, -3),
			/* B */
			BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 2, 1),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, -6), /* call A */
			BPF_ST_MEM(BPF_B, BPF_REG_10, -256, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		/* stack_main=64, stack_A=224, stack_B=256
		 * and max(main+A, main+A+B) > 512
		 */
		.errstr = "combined stack",
		.result = REJECT,
	},
	{
		"calls: stack depth check using three frames. test4",
		/* void main(void) {
		 *   func1(0);
		 *   func1(1);
		 *   func2(1);
		 * }
		 * void func1(int alloc_or_recurse) {
		 *   if (alloc_or_recurse) {
		 *     frame_pointer[-300] = 1;
		 *   } else {
		 *     func2(alloc_or_recurse);
		 *   }
		 * }
		 * void func2(int alloc_or_recurse) {
		 *   if (alloc_or_recurse) {
		 *     frame_pointer[-300] = 1;
		 *   }
		 * }
		 */
		.insns = {
			/* main */
			BPF_MOV64_IMM(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 6), /* call A */
			BPF_MOV64_IMM(BPF_REG_1, 1),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 4), /* call A */
			BPF_MOV64_IMM(BPF_REG_1, 1),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 7), /* call B */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* A */
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 2),
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call B */
			BPF_EXIT_INSN(),
			/* B */
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
			BPF_ST_MEM(BPF_B, BPF_REG_10, -300, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.result = REJECT,
		.errstr = "combined stack",
	},
	{
		"calls: stack depth check using three frames. test5",
		.insns = {
			/* main */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call A */
			BPF_EXIT_INSN(),
			/* A */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call B */
			BPF_EXIT_INSN(),
			/* B */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call C */
			BPF_EXIT_INSN(),
			/* C */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call D */
			BPF_EXIT_INSN(),
			/* D */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call E */
			BPF_EXIT_INSN(),
			/* E */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call F */
			BPF_EXIT_INSN(),
			/* F */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call G */
			BPF_EXIT_INSN(),
			/* G */
			BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 1), /* call H */
			BPF_EXIT_INSN(),
			/* H */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.errstr = "call stack",
		.result = REJECT,
	},
	{
		"calls: spill into caller stack frame",
		.insns = {
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.errstr = "cannot spill",
		.result = REJECT,
	},
	{
		"calls: write into caller stack frame",
		.insns = {
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
			BPF_EXIT_INSN(),
			BPF_ST_MEM(BPF_DW, BPF_REG_1, 0, 42),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.result = ACCEPT,
		.retval = 42,
	},
	{
		"calls: write into callee stack frame",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 42),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, -8),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.errstr = "cannot return stack pointer",
		.result = REJECT,
	},
	{
		"calls: two calls with stack write and void return",
		.insns = {
			/* main prog */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -16),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* write into stack frame of main prog */
			BPF_ST_MEM(BPF_DW, BPF_REG_1, 0, 0),
			BPF_EXIT_INSN(), /* void return */
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.result = ACCEPT,
	},
	{
		"calls: ambiguous return value",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 5),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "allowed for root only",
		.result_unpriv = REJECT,
		.errstr = "R0 !read_ok",
		.result = REJECT,
	},
	{
		"calls: two calls that return map_value",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 8),

			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			/* fetch secound map_value_ptr from the stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -16),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			/* call 3rd function twice */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* first time with fp-8 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			/* second time with fp-16 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			/* lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr into stack frame of main prog */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(), /* return 0 */
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.fixup_map_hash_8b = { 23 },
		.result = ACCEPT,
	},
	{
		"calls: two calls that return map_value with bool condition",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			/* call 3rd function twice */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* first time with fp-8 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 9),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			/* second time with fp-16 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 1, 2),
			/* fetch secound map_value_ptr from the stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_7, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			/* lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(), /* return 0 */
			/* write map_value_ptr into stack frame of main prog */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(), /* return 1 */
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.fixup_map_hash_8b = { 23 },
		.result = ACCEPT,
	},
	{
		"calls: two calls that return map_value with incorrect bool check",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			/* call 3rd function twice */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* first time with fp-8 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 9),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_6, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			/* second time with fp-16 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			/* fetch secound map_value_ptr from the stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_7, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			/* lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(), /* return 0 */
			/* write map_value_ptr into stack frame of main prog */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(), /* return 1 */
		},
		.prog_type = BPF_PROG_TYPE_XDP,
		.fixup_map_hash_8b = { 23 },
		.result = REJECT,
		.errstr = "invalid read from stack off -16+0 size 8",
	},
	{
		"calls: two calls that receive map_value via arg=ptr_stack_of_caller. test1",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* 1st lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_8, 1),

			/* 2nd lookup from map */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10), /* 20 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, /* 24 */
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_9, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-16 */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_9, 1),

			/* call 3rd func with fp-8, 0|1, fp-16, 0|1 */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6), /* 30 */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_9),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),  /* 34 */
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* if arg2 == 1 do *arg1 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),

			/* if arg4 == 1 do *arg3 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_4, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_3, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 2, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 12, 22 },
		.result = REJECT,
		.errstr = "invalid access to map value, value_size=8 off=2 size=8",
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: two calls that receive map_value via arg=ptr_stack_of_caller. test2",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* 1st lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_8, 1),

			/* 2nd lookup from map */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10), /* 20 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, /* 24 */
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_9, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-16 */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_9, 1),

			/* call 3rd func with fp-8, 0|1, fp-16, 0|1 */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6), /* 30 */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_9),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),  /* 34 */
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* if arg2 == 1 do *arg1 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),

			/* if arg4 == 1 do *arg3 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_4, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_3, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 12, 22 },
		.result = ACCEPT,
	},
	{
		"calls: two jumps that receive map_value via arg=ptr_stack_of_jumper. test3",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* 1st lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -24, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -24),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_8, 1),

			/* 2nd lookup from map */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -24),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_9, 0),  // 26
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			/* write map_value_ptr into stack frame of main prog at fp-16 */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_9, 1),

			/* call 3rd func with fp-8, 0|1, fp-16, 0|1 */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6), // 30
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_9),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 1), // 34
			BPF_JMP_IMM(BPF_JA, 0, 0, -30),

			/* subprog 2 */
			/* if arg2 == 1 do *arg1 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),

			/* if arg4 == 1 do *arg3 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_4, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_3, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 2, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, -8),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 12, 22 },
		.result = REJECT,
		.errstr = "invalid access to map value, value_size=8 off=2 size=8",
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: two calls that receive map_value_ptr_or_null via arg. test1",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* 1st lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr_or_null into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_8, 1),

			/* 2nd lookup from map */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr_or_null into stack frame of main prog at fp-16 */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_9, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_9, 1),

			/* call 3rd func with fp-8, 0|1, fp-16, 0|1 */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_9),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* if arg2 == 1 do *arg1 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),

			/* if arg4 == 1 do *arg3 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_4, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_3, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 12, 22 },
		.result = ACCEPT,
	},
	{
		"calls: two calls that receive map_value_ptr_or_null via arg. test2",
		.insns = {
			/* main prog */
			/* pass fp-16, fp-8 into a function */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -16),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_2),
			/* 1st lookup from map */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr_or_null into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_8, 1),

			/* 2nd lookup from map */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr_or_null into stack frame of main prog at fp-16 */
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_9, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_9, 1),

			/* call 3rd func with fp-8, 0|1, fp-16, 0|1 */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_9),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			/* if arg2 == 1 do *arg1 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_2, 1, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),

			/* if arg4 == 0 do *arg3 = 0 */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_4, 0, 2),
			/* fetch map_value_ptr from the stack of this function */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_3, 0),
			/* write into map value */
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_8b = { 12, 22 },
		.result = REJECT,
		.errstr = "R0 invalid mem access 'inv'",
	},
	{
		"calls: pkt_ptr spill into caller stack",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			/* spill unchecked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 2),
			/* now the pkt range is verified, read pkt_ptr from stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_4, 0),
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.retval = POINTER_VALUE,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 2",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			/* Marking is still kept, but not in all cases safe. */
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_ST_MEM(BPF_W, BPF_REG_4, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			/* spill unchecked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 2),
			/* now the pkt range is verified, read pkt_ptr from stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_4, 0),
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "invalid access to packet",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 3",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			/* Marking is still kept and safe here. */
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_ST_MEM(BPF_W, BPF_REG_4, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			/* spill unchecked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 3),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* now the pkt range is verified, read pkt_ptr from stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_4, 0),
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 1,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 4",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			/* Check marking propagated. */
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_ST_MEM(BPF_W, BPF_REG_4, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			/* spill unchecked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 2),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 1,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 5",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_4, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 3),
			/* spill checked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "same insn cannot be used with different",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 6",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_4, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 3),
			/* spill checked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "R4 invalid mem access",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 7",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_4, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 3),
			/* spill checked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "R4 invalid mem access",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 8",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_0, BPF_REG_3, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_4, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 3),
			/* spill checked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: pkt_ptr spill into caller stack 9",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_JMP_REG(BPF_JLE, BPF_REG_0, BPF_REG_3, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_10, -8),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_4, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 8),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			/* spill unchecked pkt_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_2, 0),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 2),
			BPF_MOV64_IMM(BPF_REG_5, 1),
			/* don't read back pkt_ptr from stack here */
			/* write 4 bytes into packet */
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_5),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "invalid access to packet",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"calls: caller stack init to zero or map_value_or_null",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			/* fetch map_value_or_null or const_zero from stack */
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			/* store into map_value */
			BPF_ST_MEM(BPF_W, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			/* if (ctx == 0) return; */
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 8),
			/* else bpf_map_lookup() and *(fp - 8) = r0 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			/* write map_value_ptr_or_null into stack frame of main prog at fp-8 */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_8b = { 13 },
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"calls: stack init to zero and pruning",
		.insns = {
			/* first make allocated_stack 16 byte */
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -16, 0),
			/* now fork the execution such that the false branch
			 * of JGT insn will be verified second and it skisp zero
			 * init of fp-8 stack slot. If stack liveness marking
			 * is missing live_read marks from call map_lookup
			 * processing then pruning will incorrectly assume
			 * that fp-8 stack slot was unused in the fall-through
			 * branch and will accept the program incorrectly
			 */
			BPF_JMP_IMM(BPF_JGT, BPF_REG_1, 2, 2),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_48b = { 6 },
		.errstr = "invalid indirect read from stack off -8+0 size 8",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_XDP,
	},
	{
		"calls: two calls returning different map pointers for lookup (hash, array)",
		.insns = {
			/* main prog */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 2),
			BPF_CALL_REL(11),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_CALL_REL(12),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0,
				   offsetof(struct test_val, foo)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			/* subprog 1 */
			BPF_LD_MAP_FD(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* subprog 2 */
			BPF_LD_MAP_FD(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_hash_48b = { 13 },
		.fixup_map_array_48b = { 16 },
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"calls: two calls returning different map pointers for lookup (hash, map in map)",
		.insns = {
			/* main prog */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 2),
			BPF_CALL_REL(11),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_CALL_REL(12),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			BPF_ST_MEM(BPF_DW, BPF_REG_0, 0,
				   offsetof(struct test_val, foo)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			/* subprog 1 */
			BPF_LD_MAP_FD(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			/* subprog 2 */
			BPF_LD_MAP_FD(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.fixup_map_in_map = { 16 },
		.fixup_map_array_48b = { 13 },
		.result = REJECT,
		.errstr = "R0 invalid mem access 'map_ptr'",
	},
	{
		"cond: two branches returning different map pointers for lookup (tail, tail)",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 0, 3),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_3, 7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.fixup_prog1 = { 5 },
		.fixup_prog2 = { 2 },
		.result_unpriv = REJECT,
		.errstr_unpriv = "tail_call abusing map_ptr",
		.result = ACCEPT,
		.retval = 42,
	},
	{
		"cond: two branches returning same map pointers for lookup (tail, tail)",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct __sk_buff, mark)),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 3),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_IMM(BPF_REG_3, 7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.fixup_prog2 = { 2, 5 },
		.result_unpriv = ACCEPT,
		.result = ACCEPT,
		.retval = 42,
	},
	{
		"search pruning: all branches should be verified (nop operation)",
		.insns = {
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 11),
			BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_3, 0xbeef, 2),
			BPF_MOV64_IMM(BPF_REG_4, 0),
			BPF_JMP_A(1),
			BPF_MOV64_IMM(BPF_REG_4, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -16),
			BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
			BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -16),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_5, 0, 2),
			BPF_MOV64_IMM(BPF_REG_6, 0),
			BPF_ST_MEM(BPF_DW, BPF_REG_6, 0, 0xdead),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_8b = { 3 },
		.errstr = "R6 invalid mem access 'inv'",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	},
	{
		"search pruning: all branches should be verified (invalid stack access)",
		.insns = {
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_ST_MEM(BPF_DW, BPF_REG_2, 0, 0),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_4, 0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_3, 0xbeef, 2),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -16),
			BPF_JMP_A(1),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_4, -24),
			BPF_EMIT_CALL(BPF_FUNC_ktime_get_ns),
			BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_10, -16),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_8b = { 3 },
		.errstr = "invalid read from stack off -16+0 size 8",
		.result = REJECT,
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	},
	{
		"jit: lsh, rsh, arsh by 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_MOV64_IMM(BPF_REG_1, 0xff),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 1),
			BPF_ALU32_IMM(BPF_LSH, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x3fc, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 1),
			BPF_ALU32_IMM(BPF_RSH, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0xff, 1),
			BPF_EXIT_INSN(),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_1, 1),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0x7f, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jit: mov32 for ldimm64, 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_1, 0xfeffffffffffffffULL),
			BPF_ALU64_IMM(BPF_RSH, BPF_REG_1, 32),
			BPF_LD_IMM64(BPF_REG_2, 0xfeffffffULL),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_1, BPF_REG_2, 1),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jit: mov32 for ldimm64, 2",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_LD_IMM64(BPF_REG_1, 0x1ffffffffULL),
			BPF_LD_IMM64(BPF_REG_2, 0xffffffffULL),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_1, BPF_REG_2, 1),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jit: various mul tests",
		.insns = {
			BPF_LD_IMM64(BPF_REG_2, 0xeeff0d413122ULL),
			BPF_LD_IMM64(BPF_REG_0, 0xfefefeULL),
			BPF_LD_IMM64(BPF_REG_1, 0xefefefULL),
			BPF_ALU64_REG(BPF_MUL, BPF_REG_0, BPF_REG_1),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_0, BPF_REG_2, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_LD_IMM64(BPF_REG_3, 0xfefefeULL),
			BPF_ALU64_REG(BPF_MUL, BPF_REG_3, BPF_REG_1),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_3, BPF_REG_2, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV32_REG(BPF_REG_2, BPF_REG_2),
			BPF_LD_IMM64(BPF_REG_0, 0xfefefeULL),
			BPF_ALU32_REG(BPF_MUL, BPF_REG_0, BPF_REG_1),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_0, BPF_REG_2, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_LD_IMM64(BPF_REG_3, 0xfefefeULL),
			BPF_ALU32_REG(BPF_MUL, BPF_REG_3, BPF_REG_1),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_3, BPF_REG_2, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_LD_IMM64(BPF_REG_0, 0x952a7bbcULL),
			BPF_LD_IMM64(BPF_REG_1, 0xfefefeULL),
			BPF_LD_IMM64(BPF_REG_2, 0xeeff0d413122ULL),
			BPF_ALU32_REG(BPF_MUL, BPF_REG_2, BPF_REG_1),
			BPF_JMP_REG(BPF_JEQ, BPF_REG_2, BPF_REG_0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"xadd/w check unaligned stack",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
			BPF_STX_XADD(BPF_W, BPF_REG_10, BPF_REG_0, -7),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "misaligned stack access off",
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	},
	{
		"xadd/w check unaligned map",
		.insns = {
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_1, 1),
			BPF_STX_XADD(BPF_W, BPF_REG_0, BPF_REG_1, 3),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_0, 3),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_8b = { 3 },
		.result = REJECT,
		.errstr = "misaligned value access off",
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
	},
	{
		"xadd/w check unaligned pkt",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct xdp_md, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct xdp_md, data_end)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 8),
			BPF_JMP_REG(BPF_JLT, BPF_REG_1, BPF_REG_3, 2),
			BPF_MOV64_IMM(BPF_REG_0, 99),
			BPF_JMP_IMM(BPF_JA, 0, 0, 6),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_ST_MEM(BPF_W, BPF_REG_2, 0, 0),
			BPF_ST_MEM(BPF_W, BPF_REG_2, 3, 0),
			BPF_STX_XADD(BPF_W, BPF_REG_2, BPF_REG_0, 1),
			BPF_STX_XADD(BPF_W, BPF_REG_2, BPF_REG_0, 2),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_2, 1),
			BPF_EXIT_INSN(),
		},
		.result = REJECT,
		.errstr = "BPF_XADD stores into R2 pkt is not allowed",
		.prog_type = BPF_PROG_TYPE_XDP,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"xadd/w check whether src/dst got mangled, 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
			BPF_STX_XADD(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
			BPF_STX_XADD(BPF_DW, BPF_REG_10, BPF_REG_0, -8),
			BPF_JMP_REG(BPF_JNE, BPF_REG_6, BPF_REG_0, 3),
			BPF_JMP_REG(BPF_JNE, BPF_REG_7, BPF_REG_10, 2),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, -8),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.retval = 3,
	},
	{
		"xadd/w check whether src/dst got mangled, 2",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -8),
			BPF_STX_XADD(BPF_W, BPF_REG_10, BPF_REG_0, -8),
			BPF_STX_XADD(BPF_W, BPF_REG_10, BPF_REG_0, -8),
			BPF_JMP_REG(BPF_JNE, BPF_REG_6, BPF_REG_0, 3),
			BPF_JMP_REG(BPF_JNE, BPF_REG_7, BPF_REG_10, 2),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -8),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.retval = 3,
	},
	{
		"bpf_get_stack return R0 within range",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
			BPF_LD_MAP_FD(BPF_REG_1, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_map_lookup_elem),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 28),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_9, sizeof(struct test_val)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),
			BPF_MOV64_IMM(BPF_REG_3, sizeof(struct test_val)),
			BPF_MOV64_IMM(BPF_REG_4, 256),
			BPF_EMIT_CALL(BPF_FUNC_get_stack),
			BPF_MOV64_IMM(BPF_REG_1, 0),
			BPF_MOV64_REG(BPF_REG_8, BPF_REG_0),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_8, 32),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_8, 32),
			BPF_JMP_REG(BPF_JSLT, BPF_REG_1, BPF_REG_8, 16),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_9, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_2, BPF_REG_8),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_9),
			BPF_ALU64_IMM(BPF_LSH, BPF_REG_1, 32),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_1, 32),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_2),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_3, BPF_REG_1),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_MOV64_IMM(BPF_REG_5, sizeof(struct test_val)),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_5),
			BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_1, 4),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_MOV64_REG(BPF_REG_3, BPF_REG_9),
			BPF_MOV64_IMM(BPF_REG_4, 0),
			BPF_EMIT_CALL(BPF_FUNC_get_stack),
			BPF_EXIT_INSN(),
		},
		.fixup_map_hash_48b = { 4 },
		.result = ACCEPT,
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	},
	{
		"ld_abs: invalid op 1",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_DW, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.errstr = "unknown opcode",
	},
	{
		"ld_abs: invalid op 2",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_0, 256),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LD_IND(BPF_DW, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.errstr = "unknown opcode",
	},
	{
		"ld_abs: nmap reduced",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0x806, 28),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0x806, 26),
			BPF_MOV32_IMM(BPF_REG_0, 18),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -64),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_10, -64),
			BPF_LD_IND(BPF_W, BPF_REG_7, 14),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -60),
			BPF_MOV32_IMM(BPF_REG_0, 280971478),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -56),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_10, -56),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -60),
			BPF_ALU32_REG(BPF_SUB, BPF_REG_0, BPF_REG_7),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 15),
			BPF_LD_ABS(BPF_H, 12),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0x806, 13),
			BPF_MOV32_IMM(BPF_REG_0, 22),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -56),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_10, -56),
			BPF_LD_IND(BPF_H, BPF_REG_7, 14),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -52),
			BPF_MOV32_IMM(BPF_REG_0, 17366),
			BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -48),
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_10, -48),
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_10, -52),
			BPF_ALU32_REG(BPF_SUB, BPF_REG_0, BPF_REG_7),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),
			BPF_MOV32_IMM(BPF_REG_0, 256),
			BPF_EXIT_INSN(),
			BPF_MOV32_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.data = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0x06, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0x10, 0xbf, 0x48, 0xd6, 0x43, 0xd6,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 256,
	},
	{
		"ld_abs: div + abs, test 1",
		.insns = {
			BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU64_IMM(BPF_MOV, BPF_REG_2, 2),
			BPF_ALU32_REG(BPF_DIV, BPF_REG_0, BPF_REG_2),
			BPF_ALU64_REG(BPF_MOV, BPF_REG_8, BPF_REG_0),
			BPF_LD_ABS(BPF_B, 4),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_8, BPF_REG_0),
			BPF_LD_IND(BPF_B, BPF_REG_8, -70),
			BPF_EXIT_INSN(),
		},
		.data = {
			10, 20, 30, 40, 50,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 10,
	},
	{
		"ld_abs: div + abs, test 2",
		.insns = {
			BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU64_IMM(BPF_MOV, BPF_REG_2, 2),
			BPF_ALU32_REG(BPF_DIV, BPF_REG_0, BPF_REG_2),
			BPF_ALU64_REG(BPF_MOV, BPF_REG_8, BPF_REG_0),
			BPF_LD_ABS(BPF_B, 128),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_8, BPF_REG_0),
			BPF_LD_IND(BPF_B, BPF_REG_8, -70),
			BPF_EXIT_INSN(),
		},
		.data = {
			10, 20, 30, 40, 50,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"ld_abs: div + abs, test 3",
		.insns = {
			BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
			BPF_ALU64_IMM(BPF_MOV, BPF_REG_7, 0),
			BPF_LD_ABS(BPF_B, 3),
			BPF_ALU32_REG(BPF_DIV, BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
		},
		.data = {
			10, 20, 30, 40, 50,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"ld_abs: div + abs, test 4",
		.insns = {
			BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
			BPF_ALU64_IMM(BPF_MOV, BPF_REG_7, 0),
			BPF_LD_ABS(BPF_B, 256),
			BPF_ALU32_REG(BPF_DIV, BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
		},
		.data = {
			10, 20, 30, 40, 50,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"ld_abs: vlan + abs, test 1",
		.insns = { },
		.data = {
			0x34,
		},
		.fill_helper = bpf_fill_ld_abs_vlan_push_pop,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 0xbef,
	},
	{
		"ld_abs: vlan + abs, test 2",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_MOV64_IMM(BPF_REG_6, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_MOV64_IMM(BPF_REG_2, 1),
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_skb_vlan_push),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_7),
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_MOV64_IMM(BPF_REG_0, 42),
			BPF_EXIT_INSN(),
		},
		.data = {
			0x34,
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 42,
	},
	{
		"ld_abs: jump around ld_abs",
		.insns = { },
		.data = {
			10, 11,
		},
		.fill_helper = bpf_fill_jump_around_ld_abs,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 10,
	},
	{
		"ld_dw: xor semi-random 64 bit imms, test 1",
		.insns = { },
		.data = { },
		.fill_helper = bpf_fill_rand_ld_dw,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 4090,
	},
	{
		"ld_dw: xor semi-random 64 bit imms, test 2",
		.insns = { },
		.data = { },
		.fill_helper = bpf_fill_rand_ld_dw,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 2047,
	},
	{
		"ld_dw: xor semi-random 64 bit imms, test 3",
		.insns = { },
		.data = { },
		.fill_helper = bpf_fill_rand_ld_dw,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 511,
	},
	{
		"ld_dw: xor semi-random 64 bit imms, test 4",
		.insns = { },
		.data = { },
		.fill_helper = bpf_fill_rand_ld_dw,
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 5,
	},
	{
		"pass unmodified ctx pointer to helper",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_csum_update),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: leak potential reference",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0), /* leak reference */
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking: leak potential reference on stack",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking: leak potential reference on stack 2",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_ST_MEM(BPF_DW, BPF_REG_4, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking: zero potential reference",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_IMM(BPF_REG_0, 0), /* leak reference */
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking: copy and zero potential references",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_MOV64_IMM(BPF_REG_7, 0), /* leak reference */
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking: release reference without check",
		.insns = {
			BPF_SK_LOOKUP,
			/* reference in r0 may be NULL */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "type=sock_or_null expected=sock",
		.result = REJECT,
	},
	{
		"reference tracking: release reference",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: release reference 2",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_EXIT_INSN(),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: release reference twice",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "type=inv expected=sock",
		.result = REJECT,
	},
	{
		"reference tracking: release reference twice inside branch",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3), /* goto end */
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "type=inv expected=sock",
		.result = REJECT,
	},
	{
		"reference tracking: alloc, check, free in one subbranch",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 16),
			/* if (offsetof(skb, mark) > data_len) exit; */
			BPF_JMP_REG(BPF_JLE, BPF_REG_0, BPF_REG_3, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_2,
				    offsetof(struct __sk_buff, mark)),
			BPF_SK_LOOKUP,
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 1), /* mark == 0? */
			/* Leak reference in R0 */
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2), /* sk NULL? */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"reference tracking: alloc, check, free in both subbranches",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 16),
			/* if (offsetof(skb, mark) > data_len) exit; */
			BPF_JMP_REG(BPF_JLE, BPF_REG_0, BPF_REG_3, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_2,
				    offsetof(struct __sk_buff, mark)),
			BPF_SK_LOOKUP,
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_6, 0, 4), /* mark == 0? */
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2), /* sk NULL? */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2), /* sk NULL? */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.flags = F_NEEDS_EFFICIENT_UNALIGNED_ACCESS,
	},
	{
		"reference tracking in call: free reference in subprog",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0), /* unchecked reference */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_1),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_2, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"pass modified ctx pointer to helper, 1",
		.insns = {
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -612),
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_csum_update),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.errstr = "dereference of modified ctx ptr",
	},
	{
		"pass modified ctx pointer to helper, 2",
		.insns = {
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -612),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_socket_cookie),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result_unpriv = REJECT,
		.result = REJECT,
		.errstr_unpriv = "dereference of modified ctx ptr",
		.errstr = "dereference of modified ctx ptr",
	},
	{
		"pass modified ctx pointer to helper, 3",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1, 0),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_3, 4),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_3),
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_csum_update),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = REJECT,
		.errstr = "variable ctx access var_off=(0x0; 0x4)",
	},
	{
		"mov64 src == dst",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_2),
			// Check bounds are OK
			BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"mov64 src != dst",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, 0),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_3),
			// Check bounds are OK
			BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"allocated_stack",
		.insns = {
			BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
			BPF_ALU64_REG(BPF_MOV, BPF_REG_7, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, -8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_10, -8),
			BPF_STX_MEM(BPF_B, BPF_REG_10, BPF_REG_7, -9),
			BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_10, -9),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.result_unpriv = ACCEPT,
		.insn_processed = 15,
	},
	{
		"masking, test out of bounds 1",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 5),
			BPF_MOV32_IMM(BPF_REG_2, 5 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 2",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 1),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 3",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0xffffffff),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 4",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0xffffffff),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 5",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, -1),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 6",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, -1),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 7",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 5),
			BPF_MOV32_IMM(BPF_REG_2, 5 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 8",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 1),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 9",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 0xffffffff),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 10",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 0xffffffff),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 11",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, -1),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test out of bounds 12",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, -1),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test in bounds 1",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 4),
			BPF_MOV32_IMM(BPF_REG_2, 5 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 4,
	},
	{
		"masking, test in bounds 2",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test in bounds 3",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0xfffffffe),
			BPF_MOV32_IMM(BPF_REG_2, 0xffffffff - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0xfffffffe,
	},
	{
		"masking, test in bounds 4",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0xabcde),
			BPF_MOV32_IMM(BPF_REG_2, 0xabcdef - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0xabcde,
	},
	{
		"masking, test in bounds 5",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 0),
			BPF_MOV32_IMM(BPF_REG_2, 1 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"masking, test in bounds 6",
		.insns = {
			BPF_MOV32_IMM(BPF_REG_1, 46),
			BPF_MOV32_IMM(BPF_REG_2, 47 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_1),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_1, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 46,
	},
	{
		"masking, test in bounds 7",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, -46),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_3, -1),
			BPF_MOV32_IMM(BPF_REG_2, 47 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_3),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_3),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_3, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_3),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 46,
	},
	{
		"masking, test in bounds 8",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_3, -47),
			BPF_ALU64_IMM(BPF_MUL, BPF_REG_3, -1),
			BPF_MOV32_IMM(BPF_REG_2, 47 - 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_3),
			BPF_ALU64_REG(BPF_OR, BPF_REG_2, BPF_REG_3),
			BPF_ALU64_IMM(BPF_NEG, BPF_REG_2, 0),
			BPF_ALU64_IMM(BPF_ARSH, BPF_REG_2, 63),
			BPF_ALU64_REG(BPF_AND, BPF_REG_3, BPF_REG_2),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_3),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 0,
	},
	{
		"reference tracking in call: free reference in subprog and outside",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0), /* unchecked reference */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_1),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_2, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "type=inv expected=sock",
		.result = REJECT,
	},
	{
		"reference tracking in call: alloc & leak reference in subprog",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_4),
			BPF_SK_LOOKUP,
			/* spill unchecked sk_ptr into stack of caller */
			BPF_STX_MEM(BPF_DW, BPF_REG_6, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking in call: alloc in subprog, release outside",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_SK_LOOKUP,
			BPF_EXIT_INSN(), /* return sk */
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.retval = POINTER_VALUE,
		.result = ACCEPT,
	},
	{
		"reference tracking in call: sk_ptr leak into caller stack",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_5, BPF_REG_4, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 5),
			/* spill unchecked sk_ptr into stack of caller */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, -8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_5, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			BPF_SK_LOOKUP,
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "Unreleased reference",
		.result = REJECT,
	},
	{
		"reference tracking in call: sk_ptr spill into caller stack",
		.insns = {
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),

			/* subprog 1 */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_5, BPF_REG_4, 0),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 8),
			/* spill unchecked sk_ptr into stack of caller */
			BPF_MOV64_REG(BPF_REG_5, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_5, -8),
			BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_5, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_4, BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			/* now the sk_ptr is verified, free the reference */
			BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_4, 0),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),

			/* subprog 2 */
			BPF_SK_LOOKUP,
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: allow LD_ABS",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: forbid LD_ABS while holding reference",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_SK_LOOKUP,
			BPF_LD_ABS(BPF_B, 0),
			BPF_LD_ABS(BPF_H, 0),
			BPF_LD_ABS(BPF_W, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "BPF_LD_[ABS|IND] cannot be mixed with socket references",
		.result = REJECT,
	},
	{
		"reference tracking: allow LD_IND",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_MOV64_IMM(BPF_REG_7, 1),
			BPF_LD_IND(BPF_W, BPF_REG_7, -0x200000),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"reference tracking: forbid LD_IND while holding reference",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_7, 1),
			BPF_LD_IND(BPF_W, BPF_REG_7, -0x200000),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_7),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_4),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "BPF_LD_[ABS|IND] cannot be mixed with socket references",
		.result = REJECT,
	},
	{
		"reference tracking: check reference or tail call",
		.insns = {
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_1),
			BPF_SK_LOOKUP,
			/* if (sk) bpf_sk_release() */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_1, 0, 7),
			/* bpf_tail_call() */
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.fixup_prog1 = { 17 },
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: release reference then tail call",
		.insns = {
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_1),
			BPF_SK_LOOKUP,
			/* if (sk) bpf_sk_release() */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			/* bpf_tail_call() */
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.fixup_prog1 = { 18 },
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: leak possible reference over tail call",
		.insns = {
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_1),
			/* Look up socket and store in REG_6 */
			BPF_SK_LOOKUP,
			/* bpf_tail_call() */
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_3, 2),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* if (sk) bpf_sk_release() */
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.fixup_prog1 = { 16 },
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "tail_call would lead to reference leak",
		.result = REJECT,
	},
	{
		"reference tracking: leak checked reference over tail call",
		.insns = {
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_1),
			/* Look up socket and store in REG_6 */
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			/* if (!sk) goto end */
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 7),
			/* bpf_tail_call() */
			BPF_MOV64_IMM(BPF_REG_3, 0),
			BPF_LD_MAP_FD(BPF_REG_2, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_tail_call),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.fixup_prog1 = { 17 },
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "tail_call would lead to reference leak",
		.result = REJECT,
	},
	{
		"reference tracking: mangle and release sock_or_null",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 5),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "R1 pointer arithmetic on sock_or_null prohibited",
		.result = REJECT,
	},
	{
		"reference tracking: mangle and release sock",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 5),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "R1 pointer arithmetic on sock prohibited",
		.result = REJECT,
	},
	{
		"reference tracking: access member",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_0, 4),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"reference tracking: write to member",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 5),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_LD_IMM64(BPF_REG_2, 42),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_2,
				    offsetof(struct bpf_sock, mark)),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_LD_IMM64(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "cannot write into socket",
		.result = REJECT,
	},
	{
		"reference tracking: invalid 64-bit access of member",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "invalid bpf_sock access off=0 size=8",
		.result = REJECT,
	},
	{
		"reference tracking: access after release",
		.insns = {
			BPF_SK_LOOKUP,
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"reference tracking: direct access for lookup",
		.insns = {
			/* Check that the packet is at least 64B long */
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
				    offsetof(struct __sk_buff, data)),
			BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_1,
				    offsetof(struct __sk_buff, data_end)),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_2),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 64),
			BPF_JMP_REG(BPF_JGT, BPF_REG_0, BPF_REG_3, 9),
			/* sk = sk_lookup_tcp(ctx, skb->data, ...) */
			BPF_MOV64_IMM(BPF_REG_3, sizeof(struct bpf_sock_tuple)),
			BPF_MOV64_IMM(BPF_REG_4, 0),
			BPF_MOV64_IMM(BPF_REG_5, 0),
			BPF_EMIT_CALL(BPF_FUNC_sk_lookup_tcp),
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_0),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3),
			BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_0, 4),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_EMIT_CALL(BPF_FUNC_sk_release),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"calls: ctx read at start of subprog",
		.insns = {
			BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 5),
			BPF_JMP_REG(BPF_JSGT, BPF_REG_0, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_6),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_B, BPF_REG_9, BPF_REG_1, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
	},
	{
		"check wire_len is not readable by sockets",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, wire_len)),
			BPF_EXIT_INSN(),
		},
		.errstr = "invalid bpf_context access",
		.result = REJECT,
	},
	{
		"check wire_len is readable by tc classifier",
		.insns = {
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_1,
				    offsetof(struct __sk_buff, wire_len)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
	},
	{
		"check wire_len is not writable by tc classifier",
		.insns = {
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_1,
				    offsetof(struct __sk_buff, wire_len)),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.errstr = "invalid bpf_context access",
		.errstr_unpriv = "R1 leaks addr",
		.result = REJECT,
	},
	{
		"calls: cross frame pruning",
		.insns = {
			/* r8 = !!random();
			 * call pruner()
			 * if (r8)
			 *     do something bad;
			 */
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_MOV64_IMM(BPF_REG_8, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_8, 1),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_8),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 4),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_8, 1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_9, BPF_REG_1, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"jset: functional",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),

			/* reg, bit 63 or bit 0 set, taken */
			BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
			BPF_JMP_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),

			/* reg, bit 62, not taken */
			BPF_LD_IMM64(BPF_REG_8, 0x4000000000000000),
			BPF_JMP_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_EXIT_INSN(),

			/* imm, any bit set, taken */
			BPF_JMP_IMM(BPF_JSET, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),

			/* imm, bit 31 set, taken */
			BPF_JMP_IMM(BPF_JSET, BPF_REG_7, 0x80000000, 1),
			BPF_EXIT_INSN(),

			/* all good - return r0 == 2 */
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 7,
		.retvals = {
			{ .retval = 2,
			  .data64 = { (1ULL << 63) | (1U << 31) | (1U << 0), }
			},
			{ .retval = 2,
			  .data64 = { (1ULL << 63) | (1U << 31), }
			},
			{ .retval = 2,
			  .data64 = { (1ULL << 31) | (1U << 0), }
			},
			{ .retval = 2,
			  .data64 = { (__u32)-1, }
			},
			{ .retval = 2,
			  .data64 = { ~0x4000000000000000ULL, }
			},
			{ .retval = 0,
			  .data64 = { 0, }
			},
			{ .retval = 0,
			  .data64 = { ~0ULL, }
			},
		},
	},
	{
		"jset: sign-extend",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),

			BPF_JMP_IMM(BPF_JSET, BPF_REG_7, 0x80000000, 1),
			BPF_EXIT_INSN(),

			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.retval = 2,
		.data = { 1, 0, 0, 0, 0, 0, 0, 1, },
	},
	{
		"jset: known const compare",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.retval_unpriv = 1,
		.result_unpriv = ACCEPT,
		.retval = 1,
		.result = ACCEPT,
	},
	{
		"jset: known const compare bad",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.errstr_unpriv = "!read_ok",
		.result_unpriv = REJECT,
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"jset: unknown const compare taken",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.errstr_unpriv = "!read_ok",
		.result_unpriv = REJECT,
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"jset: unknown const compare not taken",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.errstr_unpriv = "!read_ok",
		.result_unpriv = REJECT,
		.errstr = "!read_ok",
		.result = REJECT,
	},
	{
		"jset: half-known const compare",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_ALU64_IMM(BPF_OR, BPF_REG_0, 2),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 3, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.result_unpriv = ACCEPT,
		.result = ACCEPT,
	},
	{
		"jset: range",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_0),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_ALU64_IMM(BPF_AND, BPF_REG_1, 0xff),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_1, 0xf0, 3),
			BPF_JMP_IMM(BPF_JLT, BPF_REG_1, 0x10, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_1, 0x10, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_1, 0x10, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.result_unpriv = ACCEPT,
		.result = ACCEPT,
	},
	{
		"dead code: start",
		.insns = {
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, -4),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: mid 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 0, 1),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: mid 2",
		.insns = {
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
				     BPF_FUNC_get_prandom_u32),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 4),
			BPF_JMP_IMM(BPF_JSET, BPF_REG_0, 1, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 2),
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 1,
	},
	{
		"dead code: end 1",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, 1),
			BPF_EXIT_INSN(),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: end 2",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 12),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: end 3",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_MOV64_IMM(BPF_REG_0, 12),
			BPF_JMP_IMM(BPF_JA, 0, 0, -5),
		},
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: tail of main + func",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 8, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 12),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: tail of main + two functions",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 8, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 12),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: function in the middle and mid of another func",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 7),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 3),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 12),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 7),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_1, 7, 1),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, -5),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 7,
	},
	{
		"dead code: middle of main before call",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 2),
			BPF_JMP_IMM(BPF_JGE, BPF_REG_1, 2, 1),
			BPF_MOV64_IMM(BPF_REG_1, 5),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"dead code: start of a function",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_1, 2),
			BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JA, 0, 0, 0),
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		},
		.errstr_unpriv = "function calls to other bpf functions are allowed for root only",
		.result_unpriv = REJECT,
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jset32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			/* reg, high bits shouldn't be tested */
			BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, -2, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_EXIT_INSN(),

			BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, 1, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { 1ULL << 63, }
			},
			{ .retval = 2,
			  .data64 = { 1, }
			},
			{ .retval = 2,
			  .data64 = { 1ULL << 63 | 1, }
			},
		},
	},
	{
		"jset32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_LD_IMM64(BPF_REG_8, 0x8000000000000000),
			BPF_JMP32_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
			BPF_JMP_IMM(BPF_JA, 0, 0, 1),
			BPF_EXIT_INSN(),

			BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
			BPF_JMP32_REG(BPF_JSET, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { 1ULL << 63, }
			},
			{ .retval = 2,
			  .data64 = { 1, }
			},
			{ .retval = 2,
			  .data64 = { 1ULL << 63 | 1, }
			},
		},
	},
	{
		"jset32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP32_IMM(BPF_JSET, BPF_REG_7, 0x10, 1),
			BPF_EXIT_INSN(),
			BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, 0x10, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
	},
	{
		"jeq32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JEQ, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 2,
		.retvals = {
			{ .retval = 0,
			  .data64 = { -2, }
			},
			{ .retval = 2,
			  .data64 = { -1, }
			},
		},
	},
	{
		"jeq32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_LD_IMM64(BPF_REG_8, 0x7000000000000001),
			BPF_JMP32_REG(BPF_JEQ, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { 2, }
			},
			{ .retval = 2,
			  .data64 = { 1, }
			},
			{ .retval = 2,
			  .data64 = { 1ULL << 63 | 1, }
			},
		},
	},
	{
		"jeq32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP32_IMM(BPF_JEQ, BPF_REG_7, 0x10, 1),
			BPF_EXIT_INSN(),
			BPF_JMP32_IMM(BPF_JSGE, BPF_REG_7, 0xf, 1),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
	},
	{
		"jne32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JNE, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 2,
		.retvals = {
			{ .retval = 2,
			  .data64 = { 1, }
			},
			{ .retval = 0,
			  .data64 = { -1, }
			},
		},
	},
	{
		"jne32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_LD_IMM64(BPF_REG_8, 0x8000000000000001),
			BPF_JMP32_REG(BPF_JNE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { 1, }
			},
			{ .retval = 2,
			  .data64 = { 2, }
			},
			{ .retval = 2,
			  .data64 = { 1ULL << 63 | 2, }
			},
		},
	},
	{
		"jne32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP32_IMM(BPF_JNE, BPF_REG_7, 0x10, 1),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 0x10, 1),
			BPF_EXIT_INSN(),
			BPF_LDX_MEM(BPF_B, BPF_REG_8, BPF_REG_9, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
	},
	{
		"jge32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, UINT_MAX - 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 2,
			  .data64 = { UINT_MAX - 1, }
			},
			{ .retval = 0,
			  .data64 = { 0, }
			},
		},
	},
	{
		"jge32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, UINT_MAX | 1ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JGE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 0,
			  .data64 = { INT_MAX, }
			},
			{ .retval = 0,
			  .data64 = { (UINT_MAX - 1) | 2ULL << 32, }
			},
		},
	},
	{
		"jge32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JGE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP32_IMM(BPF_JGE, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jgt32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JGT, BPF_REG_7, UINT_MAX - 1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX - 1, }
			},
			{ .retval = 0,
			  .data64 = { 0, }
			},
		},
	},
	{
		"jgt32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, (UINT_MAX - 1) | 1ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX - 1, }
			},
			{ .retval = 0,
			  .data64 = { (UINT_MAX - 1) | 2ULL << 32, }
			},
		},
	},
	{
		"jgt32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JGT, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jle32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JLE, BPF_REG_7, INT_MAX, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { INT_MAX - 1, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 2,
			  .data64 = { INT_MAX, }
			},
		},
	},
	{
		"jle32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, (INT_MAX - 1) | 2ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JLE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { INT_MAX | 1ULL << 32, }
			},
			{ .retval = 2,
			  .data64 = { INT_MAX - 2, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX, }
			},
		},
	},
	{
		"jle32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JLE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP32_IMM(BPF_JLE, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jlt32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JLT, BPF_REG_7, INT_MAX, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { INT_MAX, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 2,
			  .data64 = { INT_MAX - 1, }
			},
		},
	},
	{
		"jlt32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, INT_MAX | 2ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JLT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { INT_MAX | 1ULL << 32, }
			},
			{ .retval = 0,
			  .data64 = { UINT_MAX, }
			},
			{ .retval = 2,
			  .data64 = { (INT_MAX - 1) | 3ULL << 32, }
			},
		},
	},
	{
		"jlt32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JLT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSLT, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jsge32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JSGE, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { 0, }
			},
			{ .retval = 2,
			  .data64 = { -1, }
			},
			{ .retval = 0,
			  .data64 = { -2, }
			},
		},
	},
	{
		"jsge32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, (__u32)-1 | 2ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JSGE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { -1, }
			},
			{ .retval = 2,
			  .data64 = { 0x7fffffff | 1ULL << 32, }
			},
			{ .retval = 0,
			  .data64 = { -2, }
			},
		},
	},
	{
		"jsge32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JSGE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSGE, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jsgt32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JSGT, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { (__u32)-2, }
			},
			{ .retval = 0,
			  .data64 = { -1, }
			},
			{ .retval = 2,
			  .data64 = { 1, }
			},
		},
	},
	{
		"jsgt32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffffe | 1ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JSGT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 0,
			  .data64 = { 0x7ffffffe, }
			},
			{ .retval = 0,
			  .data64 = { 0x1ffffffffULL, }
			},
			{ .retval = 2,
			  .data64 = { 0x7fffffff, }
			},
		},
	},
	{
		"jsgt32: min/max deduction",
		.insns = {
			BPF_RAND_SEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, (__u32)(-2) | 1ULL << 32),
			BPF_JMP32_REG(BPF_JSGT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSGT, BPF_REG_7, -2, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jsle32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JSLE, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { (__u32)-2, }
			},
			{ .retval = 2,
			  .data64 = { -1, }
			},
			{ .retval = 0,
			  .data64 = { 1, }
			},
		},
	},
	{
		"jsle32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffffe | 1ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JSLE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { 0x7ffffffe, }
			},
			{ .retval = 2,
			  .data64 = { (__u32)-1, }
			},
			{ .retval = 0,
			  .data64 = { 0x7fffffff | 2ULL << 32, }
			},
		},
	},
	{
		"jsle32: min/max deduction",
		.insns = {
			BPF_RAND_UEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, 0x7ffffff0 | 1ULL << 32),
			BPF_JMP32_REG(BPF_JSLE, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP_IMM(BPF_JSLE, BPF_REG_7, 0x7ffffff0, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
	{
		"jslt32: BPF_K",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_IMM(BPF_JSLT, BPF_REG_7, -1, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { (__u32)-2, }
			},
			{ .retval = 0,
			  .data64 = { -1, }
			},
			{ .retval = 0,
			  .data64 = { 1, }
			},
		},
	},
	{
		"jslt32: BPF_X",
		.insns = {
			BPF_DIRECT_PKT_R2,
			BPF_LD_IMM64(BPF_REG_8, 0x7fffffff | 1ULL << 32),
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_2, 0),
			BPF_JMP32_REG(BPF_JSLT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_EXIT_INSN(),
		},
		.prog_type = BPF_PROG_TYPE_SCHED_CLS,
		.result = ACCEPT,
		.runs = 3,
		.retvals = {
			{ .retval = 2,
			  .data64 = { 0x7ffffffe, }
			},
			{ .retval = 2,
			  .data64 = { 0xffffffff, }
			},
			{ .retval = 0,
			  .data64 = { 0x7fffffff | 2ULL << 32, }
			},
		},
	},
	{
		"jslt32: min/max deduction",
		.insns = {
			BPF_RAND_SEXT_R7,
			BPF_ALU32_IMM(BPF_MOV, BPF_REG_0, 2),
			BPF_LD_IMM64(BPF_REG_8, (__u32)(-1) | 1ULL << 32),
			BPF_JMP32_REG(BPF_JSLT, BPF_REG_7, BPF_REG_8, 1),
			BPF_EXIT_INSN(),
			BPF_JMP32_IMM(BPF_JSLT, BPF_REG_7, -1, 1),
			BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.result = ACCEPT,
		.retval = 2,
	},
};

static int probe_filter_length(const struct bpf_insn *fp)
{
	int len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static int create_map(uint32_t type, uint32_t size_key,
		      uint32_t size_value, uint32_t max_elem)
{
	int fd;

	fd = bpf_create_map(type, size_key, size_value, max_elem,
			    type == BPF_MAP_TYPE_HASH ? BPF_F_NO_PREALLOC : 0);
	if (fd < 0)
		printf("Failed to create hash map '%s'!\n", strerror(errno));

	return fd;
}

static void update_map(int fd, int index)
{
	struct test_val value = {
		.index = (6 + 1) * sizeof(int),
		.foo[6] = 0xabcdef12,
	};

	assert(!bpf_map_update_elem(fd, &index, &value, 0));
}

static int create_prog_dummy1(enum bpf_prog_type prog_type)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, 42),
		BPF_EXIT_INSN(),
	};

	return bpf_load_program(prog_type, prog,
				ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
}

static int create_prog_dummy2(enum bpf_prog_type prog_type, int mfd, int idx)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_3, idx),
		BPF_LD_MAP_FD(BPF_REG_2, mfd),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			     BPF_FUNC_tail_call),
		BPF_MOV64_IMM(BPF_REG_0, 41),
		BPF_EXIT_INSN(),
	};

	return bpf_load_program(prog_type, prog,
				ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
}

static int create_prog_array(enum bpf_prog_type prog_type, uint32_t max_elem,
			     int p1key)
{
	int p2key = 1;
	int mfd, p1fd, p2fd;

	mfd = bpf_create_map(BPF_MAP_TYPE_PROG_ARRAY, sizeof(int),
			     sizeof(int), max_elem, 0);
	if (mfd < 0) {
		printf("Failed to create prog array '%s'!\n", strerror(errno));
		return -1;
	}

	p1fd = create_prog_dummy1(prog_type);
	p2fd = create_prog_dummy2(prog_type, mfd, p2key);
	if (p1fd < 0 || p2fd < 0)
		goto out;
	if (bpf_map_update_elem(mfd, &p1key, &p1fd, BPF_ANY) < 0)
		goto out;
	if (bpf_map_update_elem(mfd, &p2key, &p2fd, BPF_ANY) < 0)
		goto out;
	close(p2fd);
	close(p1fd);

	return mfd;
out:
	close(p2fd);
	close(p1fd);
	close(mfd);
	return -1;
}

static int create_map_in_map(void)
{
	int inner_map_fd, outer_map_fd;

	inner_map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(int),
				      sizeof(int), 1, 0);
	if (inner_map_fd < 0) {
		printf("Failed to create array '%s'!\n", strerror(errno));
		return inner_map_fd;
	}

	outer_map_fd = bpf_create_map_in_map(BPF_MAP_TYPE_ARRAY_OF_MAPS, NULL,
					     sizeof(int), inner_map_fd, 1, 0);
	if (outer_map_fd < 0)
		printf("Failed to create array of maps '%s'!\n",
		       strerror(errno));

	close(inner_map_fd);

	return outer_map_fd;
}

static int create_cgroup_storage(bool percpu)
{
	enum bpf_map_type type = percpu ? BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE :
		BPF_MAP_TYPE_CGROUP_STORAGE;
	int fd;

	fd = bpf_create_map(type, sizeof(struct bpf_cgroup_storage_key),
			    TEST_DATA_LEN, 0, 0);
	if (fd < 0)
		printf("Failed to create cgroup storage '%s'!\n",
		       strerror(errno));

	return fd;
}

static char bpf_vlog[UINT_MAX >> 8];

static void do_test_fixup(struct bpf_test *test, enum bpf_prog_type prog_type,
			  struct bpf_insn *prog, int *map_fds)
{
	int *fixup_map_hash_8b = test->fixup_map_hash_8b;
	int *fixup_map_hash_48b = test->fixup_map_hash_48b;
	int *fixup_map_hash_16b = test->fixup_map_hash_16b;
	int *fixup_map_array_48b = test->fixup_map_array_48b;
	int *fixup_map_sockmap = test->fixup_map_sockmap;
	int *fixup_map_sockhash = test->fixup_map_sockhash;
	int *fixup_map_xskmap = test->fixup_map_xskmap;
	int *fixup_map_stacktrace = test->fixup_map_stacktrace;
	int *fixup_prog1 = test->fixup_prog1;
	int *fixup_prog2 = test->fixup_prog2;
	int *fixup_map_in_map = test->fixup_map_in_map;
	int *fixup_cgroup_storage = test->fixup_cgroup_storage;
	int *fixup_percpu_cgroup_storage = test->fixup_percpu_cgroup_storage;

	if (test->fill_helper)
		test->fill_helper(test);

	/* Allocating HTs with 1 elem is fine here, since we only test
	 * for verifier and not do a runtime lookup, so the only thing
	 * that really matters is value size in this case.
	 */
	if (*fixup_map_hash_8b) {
		map_fds[0] = create_map(BPF_MAP_TYPE_HASH, sizeof(long long),
					sizeof(long long), 1);
		do {
			prog[*fixup_map_hash_8b].imm = map_fds[0];
			fixup_map_hash_8b++;
		} while (*fixup_map_hash_8b);
	}

	if (*fixup_map_hash_48b) {
		map_fds[1] = create_map(BPF_MAP_TYPE_HASH, sizeof(long long),
					sizeof(struct test_val), 1);
		do {
			prog[*fixup_map_hash_48b].imm = map_fds[1];
			fixup_map_hash_48b++;
		} while (*fixup_map_hash_48b);
	}

	if (*fixup_map_hash_16b) {
		map_fds[2] = create_map(BPF_MAP_TYPE_HASH, sizeof(long long),
					sizeof(struct other_val), 1);
		do {
			prog[*fixup_map_hash_16b].imm = map_fds[2];
			fixup_map_hash_16b++;
		} while (*fixup_map_hash_16b);
	}

	if (*fixup_map_array_48b) {
		map_fds[3] = create_map(BPF_MAP_TYPE_ARRAY, sizeof(int),
					sizeof(struct test_val), 1);
		update_map(map_fds[3], 0);
		do {
			prog[*fixup_map_array_48b].imm = map_fds[3];
			fixup_map_array_48b++;
		} while (*fixup_map_array_48b);
	}

	if (*fixup_prog1) {
		map_fds[4] = create_prog_array(prog_type, 4, 0);
		do {
			prog[*fixup_prog1].imm = map_fds[4];
			fixup_prog1++;
		} while (*fixup_prog1);
	}

	if (*fixup_prog2) {
		map_fds[5] = create_prog_array(prog_type, 8, 7);
		do {
			prog[*fixup_prog2].imm = map_fds[5];
			fixup_prog2++;
		} while (*fixup_prog2);
	}

	if (*fixup_map_in_map) {
		map_fds[6] = create_map_in_map();
		do {
			prog[*fixup_map_in_map].imm = map_fds[6];
			fixup_map_in_map++;
		} while (*fixup_map_in_map);
	}

	if (*fixup_cgroup_storage) {
		map_fds[7] = create_cgroup_storage(false);
		do {
			prog[*fixup_cgroup_storage].imm = map_fds[7];
			fixup_cgroup_storage++;
		} while (*fixup_cgroup_storage);
	}

	if (*fixup_percpu_cgroup_storage) {
		map_fds[8] = create_cgroup_storage(true);
		do {
			prog[*fixup_percpu_cgroup_storage].imm = map_fds[8];
			fixup_percpu_cgroup_storage++;
		} while (*fixup_percpu_cgroup_storage);
	}
	if (*fixup_map_sockmap) {
		map_fds[9] = create_map(BPF_MAP_TYPE_SOCKMAP, sizeof(int),
					sizeof(int), 1);
		do {
			prog[*fixup_map_sockmap].imm = map_fds[9];
			fixup_map_sockmap++;
		} while (*fixup_map_sockmap);
	}
	if (*fixup_map_sockhash) {
		map_fds[10] = create_map(BPF_MAP_TYPE_SOCKHASH, sizeof(int),
					sizeof(int), 1);
		do {
			prog[*fixup_map_sockhash].imm = map_fds[10];
			fixup_map_sockhash++;
		} while (*fixup_map_sockhash);
	}
	if (*fixup_map_xskmap) {
		map_fds[11] = create_map(BPF_MAP_TYPE_XSKMAP, sizeof(int),
					sizeof(int), 1);
		do {
			prog[*fixup_map_xskmap].imm = map_fds[11];
			fixup_map_xskmap++;
		} while (*fixup_map_xskmap);
	}
	if (*fixup_map_stacktrace) {
		map_fds[12] = create_map(BPF_MAP_TYPE_STACK_TRACE, sizeof(u32),
					 sizeof(u64), 1);
		do {
			prog[*fixup_map_stacktrace].imm = map_fds[12];
			fixup_map_stacktrace++;
		} while (*fixup_map_stacktrace);
	}
}

static int set_admin(bool admin)
{
	cap_t caps;
	const cap_value_t cap_val = CAP_SYS_ADMIN;
	int ret = -1;

	caps = cap_get_proc();
	if (!caps) {
		perror("cap_get_proc");
		return -1;
	}
	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap_val,
				admin ? CAP_SET : CAP_CLEAR)) {
		perror("cap_set_flag");
		goto out;
	}
	if (cap_set_proc(caps)) {
		perror("cap_set_proc");
		goto out;
	}
	ret = 0;
out:
	if (cap_free(caps))
		perror("cap_free");
	return ret;
}

static int do_prog_test_run(int fd_prog, bool unpriv, uint32_t expected_val,
			    void *data, size_t size_data)
{
	__u8 tmp[TEST_DATA_LEN << 2];
	__u32 size_tmp = sizeof(tmp);
	uint32_t retval;
	int err;

	if (unpriv)
		set_admin(true);
	err = bpf_prog_test_run(fd_prog, 1, data, size_data,
				tmp, &size_tmp, &retval, NULL);
	if (unpriv)
		set_admin(false);
	if (err && errno != 524/*ENOTSUPP*/ && errno != EPERM) {
		printf("Unexpected bpf_prog_test_run error ");
		return err;
	}
	if (!err && retval != expected_val &&
	    expected_val != POINTER_VALUE) {
		printf("FAIL retval %d != %d ", retval, expected_val);
		return 1;
	}

	return 0;
}

static void do_test_single(struct bpf_test *test, bool unpriv,
			   int *passes, int *errors)
{
	int fd_prog, expected_ret, alignment_prevented_execution;
	int prog_len, prog_type = test->prog_type;
	struct bpf_insn *prog = test->insns;
	int run_errs, run_successes;
	int map_fds[MAX_NR_MAPS];
	const char *expected_err;
	__u32 pflags;
	int i, err;

	for (i = 0; i < MAX_NR_MAPS; i++)
		map_fds[i] = -1;

	if (!prog_type)
		prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	do_test_fixup(test, prog_type, prog, map_fds);
	prog_len = probe_filter_length(prog);

	pflags = 0;
	if (test->flags & F_LOAD_WITH_STRICT_ALIGNMENT)
		pflags |= BPF_F_STRICT_ALIGNMENT;
	if (test->flags & F_NEEDS_EFFICIENT_UNALIGNED_ACCESS)
		pflags |= BPF_F_ANY_ALIGNMENT;
	fd_prog = bpf_verify_program(prog_type, prog, prog_len, pflags,
				     "GPL", 0, bpf_vlog, sizeof(bpf_vlog), 1);

	expected_ret = unpriv && test->result_unpriv != UNDEF ?
		       test->result_unpriv : test->result;
	expected_err = unpriv && test->errstr_unpriv ?
		       test->errstr_unpriv : test->errstr;

	alignment_prevented_execution = 0;

	if (expected_ret == ACCEPT) {
		if (fd_prog < 0) {
			printf("FAIL\nFailed to load prog '%s'!\n",
			       strerror(errno));
			goto fail_log;
		}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
		if (fd_prog >= 0 &&
		    (test->flags & F_NEEDS_EFFICIENT_UNALIGNED_ACCESS))
			alignment_prevented_execution = 1;
#endif
	} else {
		if (fd_prog >= 0) {
			printf("FAIL\nUnexpected success to load!\n");
			goto fail_log;
		}
		if (!strstr(bpf_vlog, expected_err)) {
			printf("FAIL\nUnexpected error message!\n\tEXP: %s\n\tRES: %s\n",
			      expected_err, bpf_vlog);
			goto fail_log;
		}
	}

	if (test->insn_processed) {
		uint32_t insn_processed;
		char *proc;

		proc = strstr(bpf_vlog, "processed ");
		insn_processed = atoi(proc + 10);
		if (test->insn_processed != insn_processed) {
			printf("FAIL\nUnexpected insn_processed %u vs %u\n",
			       insn_processed, test->insn_processed);
			goto fail_log;
		}
	}

	run_errs = 0;
	run_successes = 0;
	if (!alignment_prevented_execution && fd_prog >= 0) {
		uint32_t expected_val;
		int i;

		if (!test->runs) {
			expected_val = unpriv && test->retval_unpriv ?
				test->retval_unpriv : test->retval;

			err = do_prog_test_run(fd_prog, unpriv, expected_val,
					       test->data, sizeof(test->data));
			if (err)
				run_errs++;
			else
				run_successes++;
		}

		for (i = 0; i < test->runs; i++) {
			if (unpriv && test->retvals[i].retval_unpriv)
				expected_val = test->retvals[i].retval_unpriv;
			else
				expected_val = test->retvals[i].retval;

			err = do_prog_test_run(fd_prog, unpriv, expected_val,
					       test->retvals[i].data,
					       sizeof(test->retvals[i].data));
			if (err) {
				printf("(run %d/%d) ", i + 1, test->runs);
				run_errs++;
			} else {
				run_successes++;
			}
		}
	}

	if (!run_errs) {
		(*passes)++;
		if (run_successes > 1)
			printf("%d cases ", run_successes);
		printf("OK");
		if (alignment_prevented_execution)
			printf(" (NOTE: not executed due to unknown alignment)");
		printf("\n");
	} else {
		printf("\n");
		goto fail_log;
	}
close_fds:
	close(fd_prog);
	for (i = 0; i < MAX_NR_MAPS; i++)
		close(map_fds[i]);
	sched_yield();
	return;
fail_log:
	(*errors)++;
	printf("%s", bpf_vlog);
	goto close_fds;
}

static bool is_admin(void)
{
	cap_t caps;
	cap_flag_value_t sysadmin = CAP_CLEAR;
	const cap_value_t cap_val = CAP_SYS_ADMIN;

#ifdef CAP_IS_SUPPORTED
	if (!CAP_IS_SUPPORTED(CAP_SETFCAP)) {
		perror("cap_get_flag");
		return false;
	}
#endif
	caps = cap_get_proc();
	if (!caps) {
		perror("cap_get_proc");
		return false;
	}
	if (cap_get_flag(caps, cap_val, CAP_EFFECTIVE, &sysadmin))
		perror("cap_get_flag");
	if (cap_free(caps))
		perror("cap_free");
	return (sysadmin == CAP_SET);
}

static void get_unpriv_disabled()
{
	char buf[2];
	FILE *fd;

	fd = fopen("/proc/sys/"UNPRIV_SYSCTL, "r");
	if (!fd) {
		perror("fopen /proc/sys/"UNPRIV_SYSCTL);
		unpriv_disabled = true;
		return;
	}
	if (fgets(buf, 2, fd) == buf && atoi(buf))
		unpriv_disabled = true;
	fclose(fd);
}

static bool test_as_unpriv(struct bpf_test *test)
{
	return !test->prog_type ||
	       test->prog_type == BPF_PROG_TYPE_SOCKET_FILTER ||
	       test->prog_type == BPF_PROG_TYPE_CGROUP_SKB;
}

static int do_test(bool unpriv, unsigned int from, unsigned int to)
{
	int i, passes = 0, errors = 0, skips = 0;

	for (i = from; i < to; i++) {
		struct bpf_test *test = &tests[i];

		/* Program types that are not supported by non-root we
		 * skip right away.
		 */
		if (test_as_unpriv(test) && unpriv_disabled) {
			printf("#%d/u %s SKIP\n", i, test->descr);
			skips++;
		} else if (test_as_unpriv(test)) {
			if (!unpriv)
				set_admin(false);
			printf("#%d/u %s ", i, test->descr);
			do_test_single(test, true, &passes, &errors);
			if (!unpriv)
				set_admin(true);
		}

		if (unpriv) {
			printf("#%d/p %s SKIP\n", i, test->descr);
			skips++;
		} else {
			printf("#%d/p %s ", i, test->descr);
			do_test_single(test, false, &passes, &errors);
		}
	}

	printf("Summary: %d PASSED, %d SKIPPED, %d FAILED\n", passes,
	       skips, errors);
	return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	unsigned int from = 0, to = ARRAY_SIZE(tests);
	bool unpriv = !is_admin();

	if (argc == 3) {
		unsigned int l = atoi(argv[argc - 2]);
		unsigned int u = atoi(argv[argc - 1]);

		if (l < to && u < to) {
			from = l;
			to   = u + 1;
		}
	} else if (argc == 2) {
		unsigned int t = atoi(argv[argc - 1]);

		if (t < to) {
			from = t;
			to   = t + 1;
		}
	}

	get_unpriv_disabled();
	if (unpriv && unpriv_disabled) {
		printf("Cannot run as unprivileged user with sysctl %s.\n",
		       UNPRIV_SYSCTL);
		return EXIT_FAILURE;
	}

	bpf_semi_rand_init();
	return do_test(unpriv, from, to);
}
