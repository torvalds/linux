// SPDX-License-Identifier: GPL-2.0-only
/*
 * Testsuite for eBPF verifier
 *
 * Copyright (c) 2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2017 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
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

#include <linux/unistd.h>
#include <linux/filter.h>
#include <linux/bpf_perf_event.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/btf.h>

#include <bpf/btf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "autoconf_helper.h"
#include "unpriv_helpers.h"
#include "cap_helpers.h"
#include "bpf_rand.h"
#include "bpf_util.h"
#include "test_btf.h"
#include "../../../include/linux/filter.h"
#include "testing_helpers.h"

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif

#define MAX_INSNS	BPF_MAXINSNS
#define MAX_EXPECTED_INSNS	32
#define MAX_UNEXPECTED_INSNS	32
#define MAX_TEST_INSNS	1000000
#define MAX_FIXUPS	8
#define MAX_NR_MAPS	23
#define MAX_TEST_RUNS	8
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64
#define MAX_FUNC_INFOS	8
#define MAX_BTF_STRINGS	256
#define MAX_BTF_TYPES	256

#define INSN_OFF_MASK	((__s16)0xFFFF)
#define INSN_IMM_MASK	((__s32)0xFFFFFFFF)
#define SKIP_INSNS()	BPF_RAW_INSN(0xde, 0xa, 0xd, 0xbeef, 0xdeadbeef)

#define DEFAULT_LIBBPF_LOG_LEVEL	4

#define F_NEEDS_EFFICIENT_UNALIGNED_ACCESS	(1 << 0)
#define F_LOAD_WITH_STRICT_ALIGNMENT		(1 << 1)
#define F_NEEDS_JIT_ENABLED			(1 << 2)

/* need CAP_BPF, CAP_NET_ADMIN, CAP_PERFMON to load progs */
#define ADMIN_CAPS (1ULL << CAP_NET_ADMIN |	\
		    1ULL << CAP_PERFMON |	\
		    1ULL << CAP_BPF)
#define UNPRIV_SYSCTL "kernel/unprivileged_bpf_disabled"
static bool unpriv_disabled = false;
static bool jit_disabled;
static int skips;
static bool verbose = false;
static int verif_log_level = 0;

struct kfunc_btf_id_pair {
	const char *kfunc;
	int insn_idx;
};

struct bpf_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	struct bpf_insn	*fill_insns;
	/* If specified, test engine looks for this sequence of
	 * instructions in the BPF program after loading. Allows to
	 * test rewrites applied by verifier.  Use values
	 * INSN_OFF_MASK and INSN_IMM_MASK to mask `off` and `imm`
	 * fields if content does not matter.  The test case fails if
	 * specified instructions are not found.
	 *
	 * The sequence could be split into sub-sequences by adding
	 * SKIP_INSNS instruction at the end of each sub-sequence. In
	 * such case sub-sequences are searched for one after another.
	 */
	struct bpf_insn expected_insns[MAX_EXPECTED_INSNS];
	/* If specified, test engine applies same pattern matching
	 * logic as for `expected_insns`. If the specified pattern is
	 * matched test case is marked as failed.
	 */
	struct bpf_insn unexpected_insns[MAX_UNEXPECTED_INSNS];
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
	int fixup_map_spin_lock[MAX_FIXUPS];
	int fixup_map_array_ro[MAX_FIXUPS];
	int fixup_map_array_wo[MAX_FIXUPS];
	int fixup_map_array_small[MAX_FIXUPS];
	int fixup_sk_storage_map[MAX_FIXUPS];
	int fixup_map_event_output[MAX_FIXUPS];
	int fixup_map_reuseport_array[MAX_FIXUPS];
	int fixup_map_ringbuf[MAX_FIXUPS];
	int fixup_map_timer[MAX_FIXUPS];
	int fixup_map_kptr[MAX_FIXUPS];
	struct kfunc_btf_id_pair fixup_kfunc_btf_id[MAX_FIXUPS];
	/* Expected verifier log output for result REJECT or VERBOSE_ACCEPT.
	 * Can be a tab-separated sequence of expected strings. An empty string
	 * means no log verification.
	 */
	const char *errstr;
	const char *errstr_unpriv;
	uint32_t insn_processed;
	int prog_len;
	enum {
		UNDEF,
		ACCEPT,
		REJECT,
		VERBOSE_ACCEPT,
	} result, result_unpriv;
	enum bpf_prog_type prog_type;
	uint8_t flags;
	void (*fill_helper)(struct bpf_test *self);
	int runs;
#define bpf_testdata_struct_t					\
	struct {						\
		uint32_t retval, retval_unpriv;			\
		union {						\
			__u8 data[TEST_DATA_LEN];		\
			__u64 data64[TEST_DATA_LEN / 8];	\
		};						\
	}
	union {
		bpf_testdata_struct_t;
		bpf_testdata_struct_t retvals[MAX_TEST_RUNS];
	};
	enum bpf_attach_type expected_attach_type;
	const char *kfunc;
	struct bpf_func_info func_info[MAX_FUNC_INFOS];
	int func_info_cnt;
	char btf_strings[MAX_BTF_STRINGS];
	/* A set of BTF types to load when specified,
	 * use macro definitions from test_btf.h,
	 * must end with BTF_END_RAW
	 */
	__u32 btf_types[MAX_BTF_TYPES];
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
	/* test: {skb->data[0], vlan_push} x 51 + {skb->data[0], vlan_pop} x 51 */
#define PUSH_CNT 51
	/* jump range is limited to 16 bit. PUSH_CNT of ld_abs needs room */
	unsigned int len = (1 << 15) - PUSH_CNT * 2 * 5 * 6;
	struct bpf_insn *insn = self->fill_insns;
	int i = 0, j, k = 0;

	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
loop:
	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		/* jump to error label */
		insn[i] = BPF_JMP32_IMM(BPF_JNE, BPF_REG_0, 0x34, len - i - 3);
		i++;
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
		insn[i++] = BPF_MOV64_IMM(BPF_REG_2, 1);
		insn[i++] = BPF_MOV64_IMM(BPF_REG_3, 2);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_skb_vlan_push);
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, len - i - 3);
		i++;
	}

	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP32_IMM(BPF_JNE, BPF_REG_0, 0x34, len - i - 3);
		i++;
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_skb_vlan_pop);
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, len - i - 3);
		i++;
	}
	if (++k < 5)
		goto loop;

	for (; i < len - 3; i++)
		insn[i] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0xbef);
	insn[len - 3] = BPF_JMP_A(1);
	/* error label */
	insn[len - 2] = BPF_MOV32_IMM(BPF_REG_0, 0);
	insn[len - 1] = BPF_EXIT_INSN();
	self->prog_len = len;
}

static void bpf_fill_jump_around_ld_abs(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
	/* jump range is limited to 16 bit. every ld_abs is replaced by 6 insns,
	 * but on arches like arm, ppc etc, there will be one BPF_ZEXT inserted
	 * to extend the error value of the inlined ld_abs sequence which then
	 * contains 7 insns. so, set the dividend to 7 so the testcase could
	 * work on all arches.
	 */
	unsigned int len = (1 << 15) / 7;
	int i = 0;

	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	insn[i++] = BPF_LD_ABS(BPF_B, 0);
	insn[i] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 10, len - i - 2);
	i++;
	while (i < len - 1)
		insn[i++] = BPF_LD_ABS(BPF_B, 1);
	insn[i] = BPF_EXIT_INSN();
	self->prog_len = i + 1;
}

static void bpf_fill_rand_ld_dw(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
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
	self->prog_len = i + 1;
	res ^= (res >> 32);
	self->retval = (uint32_t)res;
}

#define MAX_JMP_SEQ 8192

/* test the sequence of 8k jumps */
static void bpf_fill_scale1(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
	int i = 0, k = 0;

	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	/* test to check that the long sequence of jumps is acceptable */
	while (k++ < MAX_JMP_SEQ) {
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_get_prandom_u32);
		insn[i++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, bpf_semi_rand_get(), 2);
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_10);
		insn[i++] = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6,
					-8 * (k % 64 + 1));
	}
	/* is_state_visited() doesn't allocate state for pruning for every jump.
	 * Hence multiply jmps by 4 to accommodate that heuristic
	 */
	while (i < MAX_TEST_INSNS - MAX_JMP_SEQ * 4)
		insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 42);
	insn[i] = BPF_EXIT_INSN();
	self->prog_len = i + 1;
	self->retval = 42;
}

/* test the sequence of 8k jumps in inner most function (function depth 8)*/
static void bpf_fill_scale2(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
	int i = 0, k = 0;

#define FUNC_NEST 7
	for (k = 0; k < FUNC_NEST; k++) {
		insn[i++] = BPF_CALL_REL(1);
		insn[i++] = BPF_EXIT_INSN();
	}
	insn[i++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	/* test to check that the long sequence of jumps is acceptable */
	k = 0;
	while (k++ < MAX_JMP_SEQ) {
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_get_prandom_u32);
		insn[i++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, bpf_semi_rand_get(), 2);
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_10);
		insn[i++] = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_6,
					-8 * (k % (64 - 4 * FUNC_NEST) + 1));
	}
	while (i < MAX_TEST_INSNS - MAX_JMP_SEQ * 4)
		insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 42);
	insn[i] = BPF_EXIT_INSN();
	self->prog_len = i + 1;
	self->retval = 42;
}

static void bpf_fill_scale(struct bpf_test *self)
{
	switch (self->retval) {
	case 1:
		return bpf_fill_scale1(self);
	case 2:
		return bpf_fill_scale2(self);
	default:
		self->prog_len = 0;
		break;
	}
}

static int bpf_fill_torturous_jumps_insn_1(struct bpf_insn *insn)
{
	unsigned int len = 259, hlen = 128;
	int i;

	insn[0] = BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32);
	for (i = 1; i <= hlen; i++) {
		insn[i]        = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, i, hlen);
		insn[i + hlen] = BPF_JMP_A(hlen - i);
	}
	insn[len - 2] = BPF_MOV64_IMM(BPF_REG_0, 1);
	insn[len - 1] = BPF_EXIT_INSN();

	return len;
}

static int bpf_fill_torturous_jumps_insn_2(struct bpf_insn *insn)
{
	unsigned int len = 4100, jmp_off = 2048;
	int i, j;

	insn[0] = BPF_EMIT_CALL(BPF_FUNC_get_prandom_u32);
	for (i = 1; i <= jmp_off; i++) {
		insn[i] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, i, jmp_off);
	}
	insn[i++] = BPF_JMP_A(jmp_off);
	for (; i <= jmp_off * 2 + 1; i+=16) {
		for (j = 0; j < 16; j++) {
			insn[i + j] = BPF_JMP_A(16 - j - 1);
		}
	}

	insn[len - 2] = BPF_MOV64_IMM(BPF_REG_0, 2);
	insn[len - 1] = BPF_EXIT_INSN();

	return len;
}

static void bpf_fill_torturous_jumps(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
	int i = 0;

	switch (self->retval) {
	case 1:
		self->prog_len = bpf_fill_torturous_jumps_insn_1(insn);
		return;
	case 2:
		self->prog_len = bpf_fill_torturous_jumps_insn_2(insn);
		return;
	case 3:
		/* main */
		insn[i++] = BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 4);
		insn[i++] = BPF_RAW_INSN(BPF_JMP|BPF_CALL, 0, 1, 0, 262);
		insn[i++] = BPF_ST_MEM(BPF_B, BPF_REG_10, -32, 0);
		insn[i++] = BPF_MOV64_IMM(BPF_REG_0, 3);
		insn[i++] = BPF_EXIT_INSN();

		/* subprog 1 */
		i += bpf_fill_torturous_jumps_insn_1(insn + i);

		/* subprog 2 */
		i += bpf_fill_torturous_jumps_insn_2(insn + i);

		self->prog_len = i;
		return;
	default:
		self->prog_len = 0;
		break;
	}
}

static void bpf_fill_big_prog_with_loop_1(struct bpf_test *self)
{
	struct bpf_insn *insn = self->fill_insns;
	/* This test was added to catch a specific use after free
	 * error, which happened upon BPF program reallocation.
	 * Reallocation is handled by core.c:bpf_prog_realloc, which
	 * reuses old memory if page boundary is not crossed. The
	 * value of `len` is chosen to cross this boundary on bpf_loop
	 * patching.
	 */
	const int len = getpagesize() - 25;
	int callback_load_idx;
	int callback_idx;
	int i = 0;

	insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1);
	callback_load_idx = i;
	insn[i++] = BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW,
				 BPF_REG_2, BPF_PSEUDO_FUNC, 0,
				 777 /* filled below */);
	insn[i++] = BPF_RAW_INSN(0, 0, 0, 0, 0);
	insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0);
	insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_4, 0);
	insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_loop);

	while (i < len - 3)
		insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0);
	insn[i++] = BPF_EXIT_INSN();

	callback_idx = i;
	insn[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0);
	insn[i++] = BPF_EXIT_INSN();

	insn[callback_load_idx].imm = callback_idx - callback_load_idx - 1;
	self->func_info[1].insn_off = callback_idx;
	self->prog_len = i;
	assert(i == len);
}

/* BPF_SK_LOOKUP contains 13 instructions, if you need to fix up maps */
#define BPF_SK_LOOKUP(func)						\
	/* struct bpf_sock_tuple tuple = {} */				\
	BPF_MOV64_IMM(BPF_REG_2, 0),					\
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_2, -8),			\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -16),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -24),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -32),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -40),		\
	BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_2, -48),		\
	/* sk = func(ctx, &tuple, sizeof tuple, 0, 0) */		\
	BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),				\
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -48),				\
	BPF_MOV64_IMM(BPF_REG_3, sizeof(struct bpf_sock_tuple)),	\
	BPF_MOV64_IMM(BPF_REG_4, 0),					\
	BPF_MOV64_IMM(BPF_REG_5, 0),					\
	BPF_EMIT_CALL(BPF_FUNC_ ## func)

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
};

static int probe_filter_length(const struct bpf_insn *fp)
{
	int len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static bool skip_unsupported_map(enum bpf_map_type map_type)
{
	if (!libbpf_probe_bpf_map_type(map_type, NULL)) {
		printf("SKIP (unsupported map type %d)\n", map_type);
		skips++;
		return true;
	}
	return false;
}

static int __create_map(uint32_t type, uint32_t size_key,
			uint32_t size_value, uint32_t max_elem,
			uint32_t extra_flags)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int fd;

	opts.map_flags = (type == BPF_MAP_TYPE_HASH ? BPF_F_NO_PREALLOC : 0) | extra_flags;
	fd = bpf_map_create(type, NULL, size_key, size_value, max_elem, &opts);
	if (fd < 0) {
		if (skip_unsupported_map(type))
			return -1;
		printf("Failed to create hash map '%s'!\n", strerror(errno));
	}

	return fd;
}

static int create_map(uint32_t type, uint32_t size_key,
		      uint32_t size_value, uint32_t max_elem)
{
	return __create_map(type, size_key, size_value, max_elem, 0);
}

static void update_map(int fd, int index)
{
	struct test_val value = {
		.index = (6 + 1) * sizeof(int),
		.foo[6] = 0xabcdef12,
	};

	assert(!bpf_map_update_elem(fd, &index, &value, 0));
}

static int create_prog_dummy_simple(enum bpf_prog_type prog_type, int ret)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, ret),
		BPF_EXIT_INSN(),
	};

	return bpf_prog_load(prog_type, NULL, "GPL", prog, ARRAY_SIZE(prog), NULL);
}

static int create_prog_dummy_loop(enum bpf_prog_type prog_type, int mfd,
				  int idx, int ret)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_3, idx),
		BPF_LD_MAP_FD(BPF_REG_2, mfd),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
			     BPF_FUNC_tail_call),
		BPF_MOV64_IMM(BPF_REG_0, ret),
		BPF_EXIT_INSN(),
	};

	return bpf_prog_load(prog_type, NULL, "GPL", prog, ARRAY_SIZE(prog), NULL);
}

static int create_prog_array(enum bpf_prog_type prog_type, uint32_t max_elem,
			     int p1key, int p2key, int p3key)
{
	int mfd, p1fd, p2fd, p3fd;

	mfd = bpf_map_create(BPF_MAP_TYPE_PROG_ARRAY, NULL, sizeof(int),
			     sizeof(int), max_elem, NULL);
	if (mfd < 0) {
		if (skip_unsupported_map(BPF_MAP_TYPE_PROG_ARRAY))
			return -1;
		printf("Failed to create prog array '%s'!\n", strerror(errno));
		return -1;
	}

	p1fd = create_prog_dummy_simple(prog_type, 42);
	p2fd = create_prog_dummy_loop(prog_type, mfd, p2key, 41);
	p3fd = create_prog_dummy_simple(prog_type, 24);
	if (p1fd < 0 || p2fd < 0 || p3fd < 0)
		goto err;
	if (bpf_map_update_elem(mfd, &p1key, &p1fd, BPF_ANY) < 0)
		goto err;
	if (bpf_map_update_elem(mfd, &p2key, &p2fd, BPF_ANY) < 0)
		goto err;
	if (bpf_map_update_elem(mfd, &p3key, &p3fd, BPF_ANY) < 0) {
err:
		close(mfd);
		mfd = -1;
	}
	close(p3fd);
	close(p2fd);
	close(p1fd);
	return mfd;
}

static int create_map_in_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int inner_map_fd, outer_map_fd;

	inner_map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, sizeof(int),
				      sizeof(int), 1, NULL);
	if (inner_map_fd < 0) {
		if (skip_unsupported_map(BPF_MAP_TYPE_ARRAY))
			return -1;
		printf("Failed to create array '%s'!\n", strerror(errno));
		return inner_map_fd;
	}

	opts.inner_map_fd = inner_map_fd;
	outer_map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS, NULL,
				      sizeof(int), sizeof(int), 1, &opts);
	if (outer_map_fd < 0) {
		if (skip_unsupported_map(BPF_MAP_TYPE_ARRAY_OF_MAPS))
			return -1;
		printf("Failed to create array of maps '%s'!\n",
		       strerror(errno));
	}

	close(inner_map_fd);

	return outer_map_fd;
}

static int create_cgroup_storage(bool percpu)
{
	enum bpf_map_type type = percpu ? BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE :
		BPF_MAP_TYPE_CGROUP_STORAGE;
	int fd;

	fd = bpf_map_create(type, NULL, sizeof(struct bpf_cgroup_storage_key),
			    TEST_DATA_LEN, 0, NULL);
	if (fd < 0) {
		if (skip_unsupported_map(type))
			return -1;
		printf("Failed to create cgroup storage '%s'!\n",
		       strerror(errno));
	}

	return fd;
}

/* struct bpf_spin_lock {
 *   int val;
 * };
 * struct val {
 *   int cnt;
 *   struct bpf_spin_lock l;
 * };
 * struct bpf_timer {
 *   __u64 :64;
 *   __u64 :64;
 * } __attribute__((aligned(8)));
 * struct timer {
 *   struct bpf_timer t;
 * };
 * struct btf_ptr {
 *   struct prog_test_ref_kfunc __kptr_untrusted *ptr;
 *   struct prog_test_ref_kfunc __kptr *ptr;
 *   struct prog_test_member __kptr *ptr;
 * }
 */
static const char btf_str_sec[] = "\0bpf_spin_lock\0val\0cnt\0l\0bpf_timer\0timer\0t"
				  "\0btf_ptr\0prog_test_ref_kfunc\0ptr\0kptr\0kptr_untrusted"
				  "\0prog_test_member";
static __u32 btf_raw_types[] = {
	/* int */
	BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
	/* struct bpf_spin_lock */                      /* [2] */
	BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),
	BTF_MEMBER_ENC(15, 1, 0), /* int val; */
	/* struct val */                                /* [3] */
	BTF_TYPE_ENC(15, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
	BTF_MEMBER_ENC(19, 1, 0), /* int cnt; */
	BTF_MEMBER_ENC(23, 2, 32),/* struct bpf_spin_lock l; */
	/* struct bpf_timer */                          /* [4] */
	BTF_TYPE_ENC(25, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 0), 16),
	/* struct timer */                              /* [5] */
	BTF_TYPE_ENC(35, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 16),
	BTF_MEMBER_ENC(41, 4, 0), /* struct bpf_timer t; */
	/* struct prog_test_ref_kfunc */		/* [6] */
	BTF_STRUCT_ENC(51, 0, 0),
	BTF_STRUCT_ENC(95, 0, 0),			/* [7] */
	/* type tag "kptr_untrusted" */
	BTF_TYPE_TAG_ENC(80, 6),			/* [8] */
	/* type tag "kptr" */
	BTF_TYPE_TAG_ENC(75, 6),			/* [9] */
	BTF_TYPE_TAG_ENC(75, 7),			/* [10] */
	BTF_PTR_ENC(8),					/* [11] */
	BTF_PTR_ENC(9),					/* [12] */
	BTF_PTR_ENC(10),				/* [13] */
	/* struct btf_ptr */				/* [14] */
	BTF_STRUCT_ENC(43, 3, 24),
	BTF_MEMBER_ENC(71, 11, 0), /* struct prog_test_ref_kfunc __kptr_untrusted *ptr; */
	BTF_MEMBER_ENC(71, 12, 64), /* struct prog_test_ref_kfunc __kptr *ptr; */
	BTF_MEMBER_ENC(71, 13, 128), /* struct prog_test_member __kptr *ptr; */
};

static char bpf_vlog[UINT_MAX >> 8];

static int load_btf_spec(__u32 *types, int types_len,
			 const char *strings, int strings_len)
{
	struct btf_header hdr = {
		.magic = BTF_MAGIC,
		.version = BTF_VERSION,
		.hdr_len = sizeof(struct btf_header),
		.type_len = types_len,
		.str_off = types_len,
		.str_len = strings_len,
	};
	void *ptr, *raw_btf;
	int btf_fd;
	LIBBPF_OPTS(bpf_btf_load_opts, opts,
		    .log_buf = bpf_vlog,
		    .log_size = sizeof(bpf_vlog),
		    .log_level = (verbose
				  ? verif_log_level
				  : DEFAULT_LIBBPF_LOG_LEVEL),
	);

	raw_btf = malloc(sizeof(hdr) + types_len + strings_len);

	ptr = raw_btf;
	memcpy(ptr, &hdr, sizeof(hdr));
	ptr += sizeof(hdr);
	memcpy(ptr, types, hdr.type_len);
	ptr += hdr.type_len;
	memcpy(ptr, strings, hdr.str_len);
	ptr += hdr.str_len;

	btf_fd = bpf_btf_load(raw_btf, ptr - raw_btf, &opts);
	if (btf_fd < 0)
		printf("Failed to load BTF spec: '%s'\n", strerror(errno));

	free(raw_btf);

	return btf_fd < 0 ? -1 : btf_fd;
}

static int load_btf(void)
{
	return load_btf_spec(btf_raw_types, sizeof(btf_raw_types),
			     btf_str_sec, sizeof(btf_str_sec));
}

static int load_btf_for_test(struct bpf_test *test)
{
	int types_num = 0;

	while (types_num < MAX_BTF_TYPES &&
	       test->btf_types[types_num] != BTF_END_RAW)
		++types_num;

	int types_len = types_num * sizeof(test->btf_types[0]);

	return load_btf_spec(test->btf_types, types_len,
			     test->btf_strings, sizeof(test->btf_strings));
}

static int create_map_spin_lock(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.btf_key_type_id = 1,
		.btf_value_type_id = 3,
	);
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;
	opts.btf_fd = btf_fd;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "test_map", 4, 8, 1, &opts);
	if (fd < 0)
		printf("Failed to create map with spin_lock\n");
	return fd;
}

static int create_sk_storage_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.map_flags = BPF_F_NO_PREALLOC,
		.btf_key_type_id = 1,
		.btf_value_type_id = 3,
	);
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;
	opts.btf_fd = btf_fd;
	fd = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "test_map", 4, 8, 0, &opts);
	close(opts.btf_fd);
	if (fd < 0)
		printf("Failed to create sk_storage_map\n");
	return fd;
}

static int create_map_timer(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.btf_key_type_id = 1,
		.btf_value_type_id = 5,
	);
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;

	opts.btf_fd = btf_fd;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "test_map", 4, 16, 1, &opts);
	if (fd < 0)
		printf("Failed to create map with timer\n");
	return fd;
}

static int create_map_kptr(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.btf_key_type_id = 1,
		.btf_value_type_id = 14,
	);
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;

	opts.btf_fd = btf_fd;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "test_map", 4, 24, 1, &opts);
	if (fd < 0)
		printf("Failed to create map with btf_id pointer\n");
	return fd;
}

static void set_root(bool set)
{
	__u64 caps;

	if (set) {
		if (cap_enable_effective(1ULL << CAP_SYS_ADMIN, &caps))
			perror("cap_disable_effective(CAP_SYS_ADMIN)");
	} else {
		if (cap_disable_effective(1ULL << CAP_SYS_ADMIN, &caps))
			perror("cap_disable_effective(CAP_SYS_ADMIN)");
	}
}

static __u64 ptr_to_u64(const void *ptr)
{
	return (uintptr_t) ptr;
}

static struct btf *btf__load_testmod_btf(struct btf *vmlinux)
{
	struct bpf_btf_info info;
	__u32 len = sizeof(info);
	struct btf *btf = NULL;
	char name[64];
	__u32 id = 0;
	int err, fd;

	/* Iterate all loaded BTF objects and find bpf_testmod,
	 * we need SYS_ADMIN cap for that.
	 */
	set_root(true);

	while (true) {
		err = bpf_btf_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				break;
			perror("bpf_btf_get_next_id failed");
			break;
		}

		fd = bpf_btf_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			perror("bpf_btf_get_fd_by_id failed");
			break;
		}

		memset(&info, 0, sizeof(info));
		info.name_len = sizeof(name);
		info.name = ptr_to_u64(name);
		len = sizeof(info);

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			close(fd);
			perror("bpf_obj_get_info_by_fd failed");
			break;
		}

		if (strcmp("bpf_testmod", name)) {
			close(fd);
			continue;
		}

		btf = btf__load_from_kernel_by_id_split(id, vmlinux);
		if (!btf) {
			close(fd);
			break;
		}

		/* We need the fd to stay open so it can be used in fd_array.
		 * The final cleanup call to btf__free will free btf object
		 * and close the file descriptor.
		 */
		btf__set_fd(btf, fd);
		break;
	}

	set_root(false);
	return btf;
}

static struct btf *testmod_btf;
static struct btf *vmlinux_btf;

static void kfuncs_cleanup(void)
{
	btf__free(testmod_btf);
	btf__free(vmlinux_btf);
}

static void fixup_prog_kfuncs(struct bpf_insn *prog, int *fd_array,
			      struct kfunc_btf_id_pair *fixup_kfunc_btf_id)
{
	/* Patch in kfunc BTF IDs */
	while (fixup_kfunc_btf_id->kfunc) {
		int btf_id = 0;

		/* try to find kfunc in kernel BTF */
		vmlinux_btf = vmlinux_btf ?: btf__load_vmlinux_btf();
		if (vmlinux_btf) {
			btf_id = btf__find_by_name_kind(vmlinux_btf,
							fixup_kfunc_btf_id->kfunc,
							BTF_KIND_FUNC);
			btf_id = btf_id < 0 ? 0 : btf_id;
		}

		/* kfunc not found in kernel BTF, try bpf_testmod BTF */
		if (!btf_id) {
			testmod_btf = testmod_btf ?: btf__load_testmod_btf(vmlinux_btf);
			if (testmod_btf) {
				btf_id = btf__find_by_name_kind(testmod_btf,
								fixup_kfunc_btf_id->kfunc,
								BTF_KIND_FUNC);
				btf_id = btf_id < 0 ? 0 : btf_id;
				if (btf_id) {
					/* We put bpf_testmod module fd into fd_array
					 * and its index 1 into instruction 'off'.
					 */
					*fd_array = btf__fd(testmod_btf);
					prog[fixup_kfunc_btf_id->insn_idx].off = 1;
				}
			}
		}

		prog[fixup_kfunc_btf_id->insn_idx].imm = btf_id;
		fixup_kfunc_btf_id++;
	}
}

static void do_test_fixup(struct bpf_test *test, enum bpf_prog_type prog_type,
			  struct bpf_insn *prog, int *map_fds, int *fd_array)
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
	int *fixup_map_spin_lock = test->fixup_map_spin_lock;
	int *fixup_map_array_ro = test->fixup_map_array_ro;
	int *fixup_map_array_wo = test->fixup_map_array_wo;
	int *fixup_map_array_small = test->fixup_map_array_small;
	int *fixup_sk_storage_map = test->fixup_sk_storage_map;
	int *fixup_map_event_output = test->fixup_map_event_output;
	int *fixup_map_reuseport_array = test->fixup_map_reuseport_array;
	int *fixup_map_ringbuf = test->fixup_map_ringbuf;
	int *fixup_map_timer = test->fixup_map_timer;
	int *fixup_map_kptr = test->fixup_map_kptr;

	if (test->fill_helper) {
		test->fill_insns = calloc(MAX_TEST_INSNS, sizeof(struct bpf_insn));
		test->fill_helper(test);
	}

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
		map_fds[4] = create_prog_array(prog_type, 4, 0, 1, 2);
		do {
			prog[*fixup_prog1].imm = map_fds[4];
			fixup_prog1++;
		} while (*fixup_prog1);
	}

	if (*fixup_prog2) {
		map_fds[5] = create_prog_array(prog_type, 8, 7, 1, 2);
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
	if (*fixup_map_spin_lock) {
		map_fds[13] = create_map_spin_lock();
		do {
			prog[*fixup_map_spin_lock].imm = map_fds[13];
			fixup_map_spin_lock++;
		} while (*fixup_map_spin_lock);
	}
	if (*fixup_map_array_ro) {
		map_fds[14] = __create_map(BPF_MAP_TYPE_ARRAY, sizeof(int),
					   sizeof(struct test_val), 1,
					   BPF_F_RDONLY_PROG);
		update_map(map_fds[14], 0);
		do {
			prog[*fixup_map_array_ro].imm = map_fds[14];
			fixup_map_array_ro++;
		} while (*fixup_map_array_ro);
	}
	if (*fixup_map_array_wo) {
		map_fds[15] = __create_map(BPF_MAP_TYPE_ARRAY, sizeof(int),
					   sizeof(struct test_val), 1,
					   BPF_F_WRONLY_PROG);
		update_map(map_fds[15], 0);
		do {
			prog[*fixup_map_array_wo].imm = map_fds[15];
			fixup_map_array_wo++;
		} while (*fixup_map_array_wo);
	}
	if (*fixup_map_array_small) {
		map_fds[16] = __create_map(BPF_MAP_TYPE_ARRAY, sizeof(int),
					   1, 1, 0);
		update_map(map_fds[16], 0);
		do {
			prog[*fixup_map_array_small].imm = map_fds[16];
			fixup_map_array_small++;
		} while (*fixup_map_array_small);
	}
	if (*fixup_sk_storage_map) {
		map_fds[17] = create_sk_storage_map();
		do {
			prog[*fixup_sk_storage_map].imm = map_fds[17];
			fixup_sk_storage_map++;
		} while (*fixup_sk_storage_map);
	}
	if (*fixup_map_event_output) {
		map_fds[18] = __create_map(BPF_MAP_TYPE_PERF_EVENT_ARRAY,
					   sizeof(int), sizeof(int), 1, 0);
		do {
			prog[*fixup_map_event_output].imm = map_fds[18];
			fixup_map_event_output++;
		} while (*fixup_map_event_output);
	}
	if (*fixup_map_reuseport_array) {
		map_fds[19] = __create_map(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY,
					   sizeof(u32), sizeof(u64), 1, 0);
		do {
			prog[*fixup_map_reuseport_array].imm = map_fds[19];
			fixup_map_reuseport_array++;
		} while (*fixup_map_reuseport_array);
	}
	if (*fixup_map_ringbuf) {
		map_fds[20] = create_map(BPF_MAP_TYPE_RINGBUF, 0,
					 0, getpagesize());
		do {
			prog[*fixup_map_ringbuf].imm = map_fds[20];
			fixup_map_ringbuf++;
		} while (*fixup_map_ringbuf);
	}
	if (*fixup_map_timer) {
		map_fds[21] = create_map_timer();
		do {
			prog[*fixup_map_timer].imm = map_fds[21];
			fixup_map_timer++;
		} while (*fixup_map_timer);
	}
	if (*fixup_map_kptr) {
		map_fds[22] = create_map_kptr();
		do {
			prog[*fixup_map_kptr].imm = map_fds[22];
			fixup_map_kptr++;
		} while (*fixup_map_kptr);
	}

	fixup_prog_kfuncs(prog, fd_array, test->fixup_kfunc_btf_id);
}

struct libcap {
	struct __user_cap_header_struct hdr;
	struct __user_cap_data_struct data[2];
};

static int set_admin(bool admin)
{
	int err;

	if (admin) {
		err = cap_enable_effective(ADMIN_CAPS, NULL);
		if (err)
			perror("cap_enable_effective(ADMIN_CAPS)");
	} else {
		err = cap_disable_effective(ADMIN_CAPS, NULL);
		if (err)
			perror("cap_disable_effective(ADMIN_CAPS)");
	}

	return err;
}

static int do_prog_test_run(int fd_prog, bool unpriv, uint32_t expected_val,
			    void *data, size_t size_data)
{
	__u8 tmp[TEST_DATA_LEN << 2];
	__u32 size_tmp = sizeof(tmp);
	int err, saved_errno;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = data,
		.data_size_in = size_data,
		.data_out = tmp,
		.data_size_out = size_tmp,
		.repeat = 1,
	);

	if (unpriv)
		set_admin(true);
	err = bpf_prog_test_run_opts(fd_prog, &topts);
	saved_errno = errno;

	if (unpriv)
		set_admin(false);

	if (err) {
		switch (saved_errno) {
		case ENOTSUPP:
			printf("Did not run the program (not supported) ");
			return 0;
		case EPERM:
			if (unpriv) {
				printf("Did not run the program (no permission) ");
				return 0;
			}
			/* fallthrough; */
		default:
			printf("FAIL: Unexpected bpf_prog_test_run error (%s) ",
				strerror(saved_errno));
			return err;
		}
	}

	if (topts.retval != expected_val && expected_val != POINTER_VALUE) {
		printf("FAIL retval %d != %d ", topts.retval, expected_val);
		return 1;
	}

	return 0;
}

/* Returns true if every part of exp (tab-separated) appears in log, in order.
 *
 * If exp is an empty string, returns true.
 */
static bool cmp_str_seq(const char *log, const char *exp)
{
	char needle[200];
	const char *p, *q;
	int len;

	do {
		if (!strlen(exp))
			break;
		p = strchr(exp, '\t');
		if (!p)
			p = exp + strlen(exp);

		len = p - exp;
		if (len >= sizeof(needle) || !len) {
			printf("FAIL\nTestcase bug\n");
			return false;
		}
		strncpy(needle, exp, len);
		needle[len] = 0;
		q = strstr(log, needle);
		if (!q) {
			printf("FAIL\nUnexpected verifier log!\n"
			       "EXP: %s\nRES:\n", needle);
			return false;
		}
		log = q + len;
		exp = p + 1;
	} while (*p);
	return true;
}

static bool is_null_insn(struct bpf_insn *insn)
{
	struct bpf_insn null_insn = {};

	return memcmp(insn, &null_insn, sizeof(null_insn)) == 0;
}

static bool is_skip_insn(struct bpf_insn *insn)
{
	struct bpf_insn skip_insn = SKIP_INSNS();

	return memcmp(insn, &skip_insn, sizeof(skip_insn)) == 0;
}

static int null_terminated_insn_len(struct bpf_insn *seq, int max_len)
{
	int i;

	for (i = 0; i < max_len; ++i) {
		if (is_null_insn(&seq[i]))
			return i;
	}
	return max_len;
}

static bool compare_masked_insn(struct bpf_insn *orig, struct bpf_insn *masked)
{
	struct bpf_insn orig_masked;

	memcpy(&orig_masked, orig, sizeof(orig_masked));
	if (masked->imm == INSN_IMM_MASK)
		orig_masked.imm = INSN_IMM_MASK;
	if (masked->off == INSN_OFF_MASK)
		orig_masked.off = INSN_OFF_MASK;

	return memcmp(&orig_masked, masked, sizeof(orig_masked)) == 0;
}

static int find_insn_subseq(struct bpf_insn *seq, struct bpf_insn *subseq,
			    int seq_len, int subseq_len)
{
	int i, j;

	if (subseq_len > seq_len)
		return -1;

	for (i = 0; i < seq_len - subseq_len + 1; ++i) {
		bool found = true;

		for (j = 0; j < subseq_len; ++j) {
			if (!compare_masked_insn(&seq[i + j], &subseq[j])) {
				found = false;
				break;
			}
		}
		if (found)
			return i;
	}

	return -1;
}

static int find_skip_insn_marker(struct bpf_insn *seq, int len)
{
	int i;

	for (i = 0; i < len; ++i)
		if (is_skip_insn(&seq[i]))
			return i;

	return -1;
}

/* Return true if all sub-sequences in `subseqs` could be found in
 * `seq` one after another. Sub-sequences are separated by a single
 * nil instruction.
 */
static bool find_all_insn_subseqs(struct bpf_insn *seq, struct bpf_insn *subseqs,
				  int seq_len, int max_subseqs_len)
{
	int subseqs_len = null_terminated_insn_len(subseqs, max_subseqs_len);

	while (subseqs_len > 0) {
		int skip_idx = find_skip_insn_marker(subseqs, subseqs_len);
		int cur_subseq_len = skip_idx < 0 ? subseqs_len : skip_idx;
		int subseq_idx = find_insn_subseq(seq, subseqs,
						  seq_len, cur_subseq_len);

		if (subseq_idx < 0)
			return false;
		seq += subseq_idx + cur_subseq_len;
		seq_len -= subseq_idx + cur_subseq_len;
		subseqs += cur_subseq_len + 1;
		subseqs_len -= cur_subseq_len + 1;
	}

	return true;
}

static void print_insn(struct bpf_insn *buf, int cnt)
{
	int i;

	printf("  addr  op d s off  imm\n");
	for (i = 0; i < cnt; ++i) {
		struct bpf_insn *insn = &buf[i];

		if (is_null_insn(insn))
			break;

		if (is_skip_insn(insn))
			printf("  ...\n");
		else
			printf("  %04x: %02x %1x %x %04hx %08x\n",
			       i, insn->code, insn->dst_reg,
			       insn->src_reg, insn->off, insn->imm);
	}
}

static bool check_xlated_program(struct bpf_test *test, int fd_prog)
{
	struct bpf_insn *buf;
	unsigned int cnt;
	bool result = true;
	bool check_expected = !is_null_insn(test->expected_insns);
	bool check_unexpected = !is_null_insn(test->unexpected_insns);

	if (!check_expected && !check_unexpected)
		goto out;

	if (get_xlated_program(fd_prog, &buf, &cnt)) {
		printf("FAIL: can't get xlated program\n");
		result = false;
		goto out;
	}

	if (check_expected &&
	    !find_all_insn_subseqs(buf, test->expected_insns,
				   cnt, MAX_EXPECTED_INSNS)) {
		printf("FAIL: can't find expected subsequence of instructions\n");
		result = false;
		if (verbose) {
			printf("Program:\n");
			print_insn(buf, cnt);
			printf("Expected subsequence:\n");
			print_insn(test->expected_insns, MAX_EXPECTED_INSNS);
		}
	}

	if (check_unexpected &&
	    find_all_insn_subseqs(buf, test->unexpected_insns,
				  cnt, MAX_UNEXPECTED_INSNS)) {
		printf("FAIL: found unexpected subsequence of instructions\n");
		result = false;
		if (verbose) {
			printf("Program:\n");
			print_insn(buf, cnt);
			printf("Un-expected subsequence:\n");
			print_insn(test->unexpected_insns, MAX_UNEXPECTED_INSNS);
		}
	}

	free(buf);
 out:
	return result;
}

static void do_test_single(struct bpf_test *test, bool unpriv,
			   int *passes, int *errors)
{
	int fd_prog, btf_fd, expected_ret, alignment_prevented_execution;
	int prog_len, prog_type = test->prog_type;
	struct bpf_insn *prog = test->insns;
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	int run_errs, run_successes;
	int map_fds[MAX_NR_MAPS];
	const char *expected_err;
	int fd_array[2] = { -1, -1 };
	int saved_errno;
	int fixup_skips;
	__u32 pflags;
	int i, err;

	if ((test->flags & F_NEEDS_JIT_ENABLED) && jit_disabled) {
		printf("SKIP (requires BPF JIT)\n");
		skips++;
		sched_yield();
		return;
	}

	fd_prog = -1;
	for (i = 0; i < MAX_NR_MAPS; i++)
		map_fds[i] = -1;
	btf_fd = -1;

	if (!prog_type)
		prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	fixup_skips = skips;
	do_test_fixup(test, prog_type, prog, map_fds, &fd_array[1]);
	if (test->fill_insns) {
		prog = test->fill_insns;
		prog_len = test->prog_len;
	} else {
		prog_len = probe_filter_length(prog);
	}
	/* If there were some map skips during fixup due to missing bpf
	 * features, skip this test.
	 */
	if (fixup_skips != skips)
		return;

	pflags = testing_prog_flags();
	if (test->flags & F_LOAD_WITH_STRICT_ALIGNMENT)
		pflags |= BPF_F_STRICT_ALIGNMENT;
	if (test->flags & F_NEEDS_EFFICIENT_UNALIGNED_ACCESS)
		pflags |= BPF_F_ANY_ALIGNMENT;
	if (test->flags & ~3)
		pflags |= test->flags;

	expected_ret = unpriv && test->result_unpriv != UNDEF ?
		       test->result_unpriv : test->result;
	expected_err = unpriv && test->errstr_unpriv ?
		       test->errstr_unpriv : test->errstr;

	opts.expected_attach_type = test->expected_attach_type;
	if (verbose)
		opts.log_level = verif_log_level | 4; /* force stats */
	else if (expected_ret == VERBOSE_ACCEPT)
		opts.log_level = 2;
	else
		opts.log_level = DEFAULT_LIBBPF_LOG_LEVEL;
	opts.prog_flags = pflags;
	if (fd_array[1] != -1)
		opts.fd_array = &fd_array[0];

	if ((prog_type == BPF_PROG_TYPE_TRACING ||
	     prog_type == BPF_PROG_TYPE_LSM) && test->kfunc) {
		int attach_btf_id;

		attach_btf_id = libbpf_find_vmlinux_btf_id(test->kfunc,
						opts.expected_attach_type);
		if (attach_btf_id < 0) {
			printf("FAIL\nFailed to find BTF ID for '%s'!\n",
				test->kfunc);
			(*errors)++;
			return;
		}

		opts.attach_btf_id = attach_btf_id;
	}

	if (test->btf_types[0] != 0) {
		btf_fd = load_btf_for_test(test);
		if (btf_fd < 0)
			goto fail_log;
		opts.prog_btf_fd = btf_fd;
	}

	if (test->func_info_cnt != 0) {
		opts.func_info = test->func_info;
		opts.func_info_cnt = test->func_info_cnt;
		opts.func_info_rec_size = sizeof(test->func_info[0]);
	}

	opts.log_buf = bpf_vlog;
	opts.log_size = sizeof(bpf_vlog);
	fd_prog = bpf_prog_load(prog_type, NULL, "GPL", prog, prog_len, &opts);
	saved_errno = errno;

	/* BPF_PROG_TYPE_TRACING requires more setup and
	 * bpf_probe_prog_type won't give correct answer
	 */
	if (fd_prog < 0 && prog_type != BPF_PROG_TYPE_TRACING &&
	    !libbpf_probe_bpf_prog_type(prog_type, NULL)) {
		printf("SKIP (unsupported program type %d)\n", prog_type);
		skips++;
		goto close_fds;
	}

	if (fd_prog < 0 && saved_errno == ENOTSUPP) {
		printf("SKIP (program uses an unsupported feature)\n");
		skips++;
		goto close_fds;
	}

	alignment_prevented_execution = 0;

	if (expected_ret == ACCEPT || expected_ret == VERBOSE_ACCEPT) {
		if (fd_prog < 0) {
			printf("FAIL\nFailed to load prog '%s'!\n",
			       strerror(saved_errno));
			goto fail_log;
		}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
		if (fd_prog >= 0 &&
		    (test->flags & F_NEEDS_EFFICIENT_UNALIGNED_ACCESS))
			alignment_prevented_execution = 1;
#endif
		if (expected_ret == VERBOSE_ACCEPT && !cmp_str_seq(bpf_vlog, expected_err)) {
			goto fail_log;
		}
	} else {
		if (fd_prog >= 0) {
			printf("FAIL\nUnexpected success to load!\n");
			goto fail_log;
		}
		if (!expected_err || !cmp_str_seq(bpf_vlog, expected_err)) {
			printf("FAIL\nUnexpected error message!\n\tEXP: %s\n\tRES: %s\n",
			      expected_err, bpf_vlog);
			goto fail_log;
		}
	}

	if (!unpriv && test->insn_processed) {
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

	if (verbose)
		printf(", verifier log:\n%s", bpf_vlog);

	if (!check_xlated_program(test, fd_prog))
		goto fail_log;

	run_errs = 0;
	run_successes = 0;
	if (!alignment_prevented_execution && fd_prog >= 0 && test->runs >= 0) {
		uint32_t expected_val;
		int i;

		if (!test->runs)
			test->runs = 1;

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
	if (test->fill_insns)
		free(test->fill_insns);
	close(fd_prog);
	close(btf_fd);
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
	__u64 caps;

	/* The test checks for finer cap as CAP_NET_ADMIN,
	 * CAP_PERFMON, and CAP_BPF instead of CAP_SYS_ADMIN.
	 * Thus, disable CAP_SYS_ADMIN at the beginning.
	 */
	if (cap_disable_effective(1ULL << CAP_SYS_ADMIN, &caps)) {
		perror("cap_disable_effective(CAP_SYS_ADMIN)");
		return false;
	}

	return (caps & ADMIN_CAPS) == ADMIN_CAPS;
}

static bool test_as_unpriv(struct bpf_test *test)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	/* Some architectures have strict alignment requirements. In
	 * that case, the BPF verifier detects if a program has
	 * unaligned accesses and rejects them. A user can pass
	 * BPF_F_ANY_ALIGNMENT to a program to override this
	 * check. That, however, will only work when a privileged user
	 * loads a program. An unprivileged user loading a program
	 * with this flag will be rejected prior entering the
	 * verifier.
	 */
	if (test->flags & F_NEEDS_EFFICIENT_UNALIGNED_ACCESS)
		return false;
#endif
	return !test->prog_type ||
	       test->prog_type == BPF_PROG_TYPE_SOCKET_FILTER ||
	       test->prog_type == BPF_PROG_TYPE_CGROUP_SKB;
}

static int do_test(bool unpriv, unsigned int from, unsigned int to)
{
	int i, passes = 0, errors = 0;

	/* ensure previous instance of the module is unloaded */
	unload_bpf_testmod(verbose);

	if (load_bpf_testmod(verbose))
		return EXIT_FAILURE;

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

	unload_bpf_testmod(verbose);
	kfuncs_cleanup();

	printf("Summary: %d PASSED, %d SKIPPED, %d FAILED\n", passes,
	       skips, errors);
	return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	unsigned int from = 0, to = ARRAY_SIZE(tests);
	bool unpriv = !is_admin();
	int arg = 1;

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		arg++;
		verbose = true;
		verif_log_level = 1;
		argc--;
	}
	if (argc > 1 && strcmp(argv[1], "-vv") == 0) {
		arg++;
		verbose = true;
		verif_log_level = 2;
		argc--;
	}

	if (argc == 3) {
		unsigned int l = atoi(argv[arg]);
		unsigned int u = atoi(argv[arg + 1]);

		if (l < to && u < to) {
			from = l;
			to   = u + 1;
		}
	} else if (argc == 2) {
		unsigned int t = atoi(argv[arg]);

		if (t < to) {
			from = t;
			to   = t + 1;
		}
	}

	unpriv_disabled = get_unpriv_disabled();
	if (unpriv && unpriv_disabled) {
		printf("Cannot run as unprivileged user with sysctl %s.\n",
		       UNPRIV_SYSCTL);
		return EXIT_FAILURE;
	}

	jit_disabled = !is_jit_enabled();

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	bpf_semi_rand_init();
	return do_test(unpriv, from, to);
}
