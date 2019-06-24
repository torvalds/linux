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
#include "bpf_rlimit.h"
#include "bpf_rand.h"
#include "bpf_util.h"
#include "test_btf.h"
#include "../../../include/linux/filter.h"

#define MAX_INSNS	BPF_MAXINSNS
#define MAX_TEST_INSNS	1000000
#define MAX_FIXUPS	8
#define MAX_NR_MAPS	18
#define MAX_TEST_RUNS	8
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64

#define F_NEEDS_EFFICIENT_UNALIGNED_ACCESS	(1 << 0)
#define F_LOAD_WITH_STRICT_ALIGNMENT		(1 << 1)

#define UNPRIV_SYSCTL "kernel/unprivileged_bpf_disabled"
static bool unpriv_disabled = false;
static int skips;

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
	const char *errstr;
	const char *errstr_unpriv;
	uint32_t retval, retval_unpriv, insn_processed;
	int prog_len;
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
	int fd;

	fd = bpf_create_map(type, size_key, size_value, max_elem,
			    (type == BPF_MAP_TYPE_HASH ?
			     BPF_F_NO_PREALLOC : 0) | extra_flags);
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
		if (skip_unsupported_map(BPF_MAP_TYPE_PROG_ARRAY))
			return -1;
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
		if (skip_unsupported_map(BPF_MAP_TYPE_ARRAY))
			return -1;
		printf("Failed to create array '%s'!\n", strerror(errno));
		return inner_map_fd;
	}

	outer_map_fd = bpf_create_map_in_map(BPF_MAP_TYPE_ARRAY_OF_MAPS, NULL,
					     sizeof(int), inner_map_fd, 1, 0);
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

	fd = bpf_create_map(type, sizeof(struct bpf_cgroup_storage_key),
			    TEST_DATA_LEN, 0, 0);
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
 */
static const char btf_str_sec[] = "\0bpf_spin_lock\0val\0cnt\0l";
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

	btf_fd = bpf_load_btf(raw_btf, ptr - raw_btf, 0, 0, 0);
	free(raw_btf);
	if (btf_fd < 0)
		return -1;
	return btf_fd;
}

static int create_map_spin_lock(void)
{
	struct bpf_create_map_attr attr = {
		.name = "test_map",
		.map_type = BPF_MAP_TYPE_ARRAY,
		.key_size = 4,
		.value_size = 8,
		.max_entries = 1,
		.btf_key_type_id = 1,
		.btf_value_type_id = 3,
	};
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;
	attr.btf_fd = btf_fd;
	fd = bpf_create_map_xattr(&attr);
	if (fd < 0)
		printf("Failed to create map with spin_lock\n");
	return fd;
}

static int create_sk_storage_map(void)
{
	struct bpf_create_map_attr attr = {
		.name = "test_map",
		.map_type = BPF_MAP_TYPE_SK_STORAGE,
		.key_size = 4,
		.value_size = 8,
		.max_entries = 0,
		.map_flags = BPF_F_NO_PREALLOC,
		.btf_key_type_id = 1,
		.btf_value_type_id = 3,
	};
	int fd, btf_fd;

	btf_fd = load_btf();
	if (btf_fd < 0)
		return -1;
	attr.btf_fd = btf_fd;
	fd = bpf_create_map_xattr(&attr);
	close(attr.btf_fd);
	if (fd < 0)
		printf("Failed to create sk_storage_map\n");
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
	fd_prog = bpf_verify_program(prog_type, prog, prog_len, pflags,
				     "GPL", 0, bpf_vlog, sizeof(bpf_vlog), 4);
	if (fd_prog < 0 && !bpf_probe_prog_type(prog_type, 0)) {
		printf("SKIP (unsupported program type %d)\n", prog_type);
		skips++;
		goto close_fds;
	}

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
