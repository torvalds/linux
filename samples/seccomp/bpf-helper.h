/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Example wrapper around BPF macros.
 *
 * Copyright (c) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 * Author: Will Drewry <wad@chromium.org>
 *
 * The code may be used by anyone for any purpose,
 * and can serve as a starting point for developing
 * applications using prctl(PR_SET_SECCOMP, 2, ...).
 *
 * No guarantees are provided with respect to the correctness
 * or functionality of this code.
 */
#ifndef __BPF_HELPER_H__
#define __BPF_HELPER_H__

#include <asm/bitsperlong.h>	/* for __BITS_PER_LONG */
#include <endian.h>
#include <linux/filter.h>
#include <linux/seccomp.h>	/* for seccomp_data */
#include <linux/types.h>
#include <linux/unistd.h>
#include <stddef.h>

#define BPF_LABELS_MAX 256
struct bpf_labels {
	int count;
	struct __bpf_label {
		const char *label;
		__u32 location;
	} labels[BPF_LABELS_MAX];
};

int bpf_resolve_jumps(struct bpf_labels *labels,
		      struct sock_filter *filter, size_t count);
__u32 seccomp_bpf_label(struct bpf_labels *labels, const char *label);
void seccomp_bpf_print(struct sock_filter *filter, size_t count);

#define JUMP_JT 0xff
#define JUMP_JF 0xff
#define LABEL_JT 0xfe
#define LABEL_JF 0xfe

#define ALLOW \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)
#define DENY \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL)
#define JUMP(labels, label) \
	BPF_JUMP(BPF_JMP+BPF_JA, FIND_LABEL((labels), (label)), \
		 JUMP_JT, JUMP_JF)
#define LABEL(labels, label) \
	BPF_JUMP(BPF_JMP+BPF_JA, FIND_LABEL((labels), (label)), \
		 LABEL_JT, LABEL_JF)
#define SYSCALL(nr, jt) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (nr), 0, 1), \
	jt

/* Lame, but just an example */
#define FIND_LABEL(labels, label) seccomp_bpf_label((labels), #label)

#define EXPAND(...) __VA_ARGS__

/* Ensure that we load the logically correct offset. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LO_ARG(idx) offsetof(struct seccomp_data, args[(idx)])
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define LO_ARG(idx) offsetof(struct seccomp_data, args[(idx)]) + sizeof(__u32)
#else
#error "Unknown endianness"
#endif

/* Map all width-sensitive operations */
#if __BITS_PER_LONG == 32

#define JEQ(x, jt) JEQ32(x, EXPAND(jt))
#define JNE(x, jt) JNE32(x, EXPAND(jt))
#define JGT(x, jt) JGT32(x, EXPAND(jt))
#define JLT(x, jt) JLT32(x, EXPAND(jt))
#define JGE(x, jt) JGE32(x, EXPAND(jt))
#define JLE(x, jt) JLE32(x, EXPAND(jt))
#define JA(x, jt) JA32(x, EXPAND(jt))
#define ARG(i) ARG_32(i)

#elif __BITS_PER_LONG == 64

/* Ensure that we load the logically correct offset. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ENDIAN(_lo, _hi) _lo, _hi
#define HI_ARG(idx) offsetof(struct seccomp_data, args[(idx)]) + sizeof(__u32)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ENDIAN(_lo, _hi) _hi, _lo
#define HI_ARG(idx) offsetof(struct seccomp_data, args[(idx)])
#endif

union arg64 {
	struct {
		__u32 ENDIAN(lo32, hi32);
	};
	__u64 u64;
};

#define JEQ(x, jt) \
	JEQ64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))
#define JGT(x, jt) \
	JGT64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))
#define JGE(x, jt) \
	JGE64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))
#define JNE(x, jt) \
	JNE64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))
#define JLT(x, jt) \
	JLT64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))
#define JLE(x, jt) \
	JLE64(((union arg64){.u64 = (x)}).lo32, \
	      ((union arg64){.u64 = (x)}).hi32, \
	      EXPAND(jt))

#define JA(x, jt) \
	JA64(((union arg64){.u64 = (x)}).lo32, \
	       ((union arg64){.u64 = (x)}).hi32, \
	       EXPAND(jt))
#define ARG(i) ARG_64(i)

#else
#error __BITS_PER_LONG value unusable.
#endif

/* Loads the arg into A */
#define ARG_32(idx) \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, LO_ARG(idx))

/* Loads lo into M[0] and hi into M[1] and A */
#define ARG_64(idx) \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, LO_ARG(idx)), \
	BPF_STMT(BPF_ST, 0), /* lo -> M[0] */ \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, HI_ARG(idx)), \
	BPF_STMT(BPF_ST, 1) /* hi -> M[1] */

#define JEQ32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (value), 0, 1), \
	jt

#define JNE32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (value), 1, 0), \
	jt

#define JA32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, (value), 0, 1), \
	jt

#define JGE32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (value), 0, 1), \
	jt

#define JGT32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (value), 0, 1), \
	jt

#define JLE32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (value), 1, 0), \
	jt

#define JLT32(value, jt) \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (value), 1, 0), \
	jt

/*
 * All the JXX64 checks assume lo is saved in M[0] and hi is saved in both
 * A and M[1]. This invariant is kept by restoring A if necessary.
 */
#define JEQ64(lo, hi, jt) \
	/* if (hi != arg.hi) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 5), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), /* swap in lo */ \
	/* if (lo != arg.lo) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (lo), 0, 2), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JNE64(lo, hi, jt) \
	/* if (hi != arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 3), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo != arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (lo), 2, 0), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JA64(lo, hi, jt) \
	/* if (hi & arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, (hi), 3, 0), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo & arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JSET+BPF_K, (lo), 0, 2), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JGE64(lo, hi, jt) \
	/* if (hi > arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (hi), 4, 0), \
	/* if (hi != arg.hi) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 5), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo >= arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (lo), 0, 2), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JGT64(lo, hi, jt) \
	/* if (hi > arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (hi), 4, 0), \
	/* if (hi != arg.hi) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 5), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo > arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (lo), 0, 2), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JLE64(lo, hi, jt) \
	/* if (hi < arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (hi), 0, 4), \
	/* if (hi != arg.hi) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 5), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo <= arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K, (lo), 2, 0), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define JLT64(lo, hi, jt) \
	/* if (hi < arg.hi) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (hi), 0, 4), \
	/* if (hi != arg.hi) goto NOMATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (hi), 0, 5), \
	BPF_STMT(BPF_LD+BPF_MEM, 0), \
	/* if (lo < arg.lo) goto MATCH; */ \
	BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K, (lo), 2, 0), \
	BPF_STMT(BPF_LD+BPF_MEM, 1), \
	jt, \
	BPF_STMT(BPF_LD+BPF_MEM, 1)

#define LOAD_SYSCALL_NR \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, \
		 offsetof(struct seccomp_data, nr))

#endif  /* __BPF_HELPER_H__ */
