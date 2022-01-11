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

#include <sys/capability.h>

#include <linux/unistd.h>
#include <linux/filter.h>
#include <linux/bpf_perf_event.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/btf.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#ifdef HAVE_GENHDR
# include "autoconf.h"
#else
# if defined(__i386) || defined(__x86_64) || defined(__s390x__) || defined(__aarch64__)
#  define CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS 1
# endif
#endif
#include "bpf_rand.h"
#include "bpf_util.h"
#include "test_btf.h"
#include "../../../include/linux/filter.h"

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif

#define MAX_INSNS	BPF_MAXINSNS
#define MAX_TEST_INSNS	1000000
#define MAX_FIXUPS	8
#define MAX_NR_MAPS	22
#define MAX_TEST_RUNS	8
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64

#define F_NEEDS_EFFICIENT_UNALIGNED_ACCESS	(1 << 0)
#define F_LOAD_WITH_STRICT_ALIGNMENT		(1 << 1)

#define UNPRIV_SYSCTL "kernel/unprivileged_bpf_disabled"
static bool unpriv_disabled = false;
static int skips;
static bool verbose = false;

struct bpf_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	struct bpf_insn	*fill_insns;
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
					 BPF_FUNC_skb_vlan_push),
		insn[i] = BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, len - i - 3);
		i++;
	}

	for (j = 0; j < PUSH_CNT; j++) {
		insn[i++] = BPF_LD_ABS(BPF_B, 0);
		insn[i] = BPF_JMP32_IMM(BPF_JNE, BPF_REG_0, 0x34, len - i - 3);
		i++;
		insn[i++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
		insn[i++] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
					 BPF_FUNC_skb_vlan_pop),
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
	if (!bpf_probe_map_type(map_type, 0)) {
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
 */
static const char btf_str_sec[] = "\0bpf_spin_lock\0val\0cnt\0l\0bpf_timer\0timer\0t";
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
};

static int load_btf(void)
{
	struct btf_header hdr = {
		.magic = BTF_MAGIC,
		.version = BTF_VERSION,
		.hdr_len = sizeof(struct btf_header),
		.type_len = sizeof(btf_raw_types),
		.str_off = sizeof(btf_raw_types),
		.str_len = sizeof(btf_str_sec),
	};
	void *ptr, *raw_btf;
	int btf_fd;

	ptr = raw_btf = malloc(sizeof(hdr) + sizeof(btf_raw_types) +
			       sizeof(btf_str_sec));

	memcpy(ptr, &hdr, sizeof(hdr));
	ptr += sizeof(hdr);
	memcpy(ptr, btf_raw_types, hdr.type_len);
	ptr += hdr.type_len;
	memcpy(ptr, btf_str_sec, hdr.str_len);
	ptr += hdr.str_len;

	btf_fd = bpf_btf_load(raw_btf, ptr - raw_btf, NULL);
	free(raw_btf);
	if (btf_fd < 0)
		return -1;
	return btf_fd;
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
	int *fixup_map_spin_lock = test->fixup_map_spin_lock;
	int *fixup_map_array_ro = test->fixup_map_array_ro;
	int *fixup_map_array_wo = test->fixup_map_array_wo;
	int *fixup_map_array_small = test->fixup_map_array_small;
	int *fixup_sk_storage_map = test->fixup_sk_storage_map;
	int *fixup_map_event_output = test->fixup_map_event_output;
	int *fixup_map_reuseport_array = test->fixup_map_reuseport_array;
	int *fixup_map_ringbuf = test->fixup_map_ringbuf;
	int *fixup_map_timer = test->fixup_map_timer;

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
					   0, 4096);
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
}

struct libcap {
	struct __user_cap_header_struct hdr;
	struct __user_cap_data_struct data[2];
};

static int set_admin(bool admin)
{
	cap_t caps;
	/* need CAP_BPF, CAP_NET_ADMIN, CAP_PERFMON to load progs */
	const cap_value_t cap_net_admin = CAP_NET_ADMIN;
	const cap_value_t cap_sys_admin = CAP_SYS_ADMIN;
	struct libcap *cap;
	int ret = -1;

	caps = cap_get_proc();
	if (!caps) {
		perror("cap_get_proc");
		return -1;
	}
	cap = (struct libcap *)caps;
	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap_sys_admin, CAP_CLEAR)) {
		perror("cap_set_flag clear admin");
		goto out;
	}
	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap_net_admin,
				admin ? CAP_SET : CAP_CLEAR)) {
		perror("cap_set_flag set_or_clear net");
		goto out;
	}
	/* libcap is likely old and simply ignores CAP_BPF and CAP_PERFMON,
	 * so update effective bits manually
	 */
	if (admin) {
		cap->data[1].effective |= 1 << (38 /* CAP_PERFMON */ - 32);
		cap->data[1].effective |= 1 << (39 /* CAP_BPF */ - 32);
	} else {
		cap->data[1].effective &= ~(1 << (38 - 32));
		cap->data[1].effective &= ~(1 << (39 - 32));
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
	int err, saved_errno;

	if (unpriv)
		set_admin(true);
	err = bpf_prog_test_run(fd_prog, 1, data, size_data,
				tmp, &size_tmp, &retval, NULL);
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

	if (retval != expected_val &&
	    expected_val != POINTER_VALUE) {
		printf("FAIL retval %d != %d ", retval, expected_val);
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

static void do_test_single(struct bpf_test *test, bool unpriv,
			   int *passes, int *errors)
{
	int fd_prog, expected_ret, alignment_prevented_execution;
	int prog_len, prog_type = test->prog_type;
	struct bpf_insn *prog = test->insns;
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	int run_errs, run_successes;
	int map_fds[MAX_NR_MAPS];
	const char *expected_err;
	int saved_errno;
	int fixup_skips;
	__u32 pflags;
	int i, err;

	for (i = 0; i < MAX_NR_MAPS; i++)
		map_fds[i] = -1;

	if (!prog_type)
		prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	fixup_skips = skips;
	do_test_fixup(test, prog_type, prog, map_fds);
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

	pflags = BPF_F_TEST_RND_HI32;
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
		opts.log_level = 1;
	else if (expected_ret == VERBOSE_ACCEPT)
		opts.log_level = 2;
	else
		opts.log_level = 4;
	opts.prog_flags = pflags;

	if (prog_type == BPF_PROG_TYPE_TRACING && test->kfunc) {
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

	opts.log_buf = bpf_vlog;
	opts.log_size = sizeof(bpf_vlog);
	fd_prog = bpf_prog_load(prog_type, NULL, "GPL", prog, prog_len, &opts);
	saved_errno = errno;

	/* BPF_PROG_TYPE_TRACING requires more setup and
	 * bpf_probe_prog_type won't give correct answer
	 */
	if (fd_prog < 0 && prog_type != BPF_PROG_TYPE_TRACING &&
	    !bpf_probe_prog_type(prog_type, 0)) {
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
	cap_flag_value_t net_priv = CAP_CLEAR;
	bool perfmon_priv = false;
	bool bpf_priv = false;
	struct libcap *cap;
	cap_t caps;

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
	cap = (struct libcap *)caps;
	bpf_priv = cap->data[1].effective & (1 << (39/* CAP_BPF */ - 32));
	perfmon_priv = cap->data[1].effective & (1 << (38/* CAP_PERFMON */ - 32));
	if (cap_get_flag(caps, CAP_NET_ADMIN, CAP_EFFECTIVE, &net_priv))
		perror("cap_get_flag NET");
	if (cap_free(caps))
		perror("cap_free");
	return bpf_priv && perfmon_priv && net_priv == CAP_SET;
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
	int arg = 1;

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		arg++;
		verbose = true;
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

	get_unpriv_disabled();
	if (unpriv && unpriv_disabled) {
		printf("Cannot run as unprivileged user with sysctl %s.\n",
		       UNPRIV_SYSCTL);
		return EXIT_FAILURE;
	}

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	bpf_semi_rand_init();
	return do_test(unpriv, from, to);
}
