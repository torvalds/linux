/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef _UAPI__LINUX_BPF_H__
#define _UAPI__LINUX_BPF_H__

#include <linux/types.h>
#include <linux/bpf_common.h>

/* Extended instruction set based on top of classic BPF */

/* instruction classes */
#define BPF_JMP32	0x06	/* jmp mode in word width */
#define BPF_ALU64	0x07	/* alu mode in double word width */

/* ld/ldx fields */
#define BPF_DW		0x18	/* double word (64-bit) */
#define BPF_XADD	0xc0	/* exclusive add */

/* alu/jmp fields */
#define BPF_MOV		0xb0	/* mov reg to reg */
#define BPF_ARSH	0xc0	/* sign extending arithmetic shift right */

/* change endianness of a register */
#define BPF_END		0xd0	/* flags for endianness conversion: */
#define BPF_TO_LE	0x00	/* convert to little-endian */
#define BPF_TO_BE	0x08	/* convert to big-endian */
#define BPF_FROM_LE	BPF_TO_LE
#define BPF_FROM_BE	BPF_TO_BE

/* jmp encodings */
#define BPF_JNE		0x50	/* jump != */
#define BPF_JLT		0xa0	/* LT is unsigned, '<' */
#define BPF_JLE		0xb0	/* LE is unsigned, '<=' */
#define BPF_JSGT	0x60	/* SGT is signed '>', GT in x86 */
#define BPF_JSGE	0x70	/* SGE is signed '>=', GE in x86 */
#define BPF_JSLT	0xc0	/* SLT is signed, '<' */
#define BPF_JSLE	0xd0	/* SLE is signed, '<=' */
#define BPF_CALL	0x80	/* function call */
#define BPF_EXIT	0x90	/* function return */

/* Register numbers */
enum {
	BPF_REG_0 = 0,
	BPF_REG_1,
	BPF_REG_2,
	BPF_REG_3,
	BPF_REG_4,
	BPF_REG_5,
	BPF_REG_6,
	BPF_REG_7,
	BPF_REG_8,
	BPF_REG_9,
	BPF_REG_10,
	__MAX_BPF_REG,
};

/* BPF has 10 general purpose 64-bit registers and stack frame. */
#define MAX_BPF_REG	__MAX_BPF_REG

struct bpf_insn {
	__u8	code;		/* opcode */
	__u8	dst_reg:4;	/* dest register */
	__u8	src_reg:4;	/* source register */
	__s16	off;		/* signed offset */
	__s32	imm;		/* signed immediate constant */
};

/* Key of an a BPF_MAP_TYPE_LPM_TRIE entry */
struct bpf_lpm_trie_key {
	__u32	prefixlen;	/* up to 32 for AF_INET, 128 for AF_INET6 */
	__u8	data[0];	/* Arbitrary size */
};

struct bpf_cgroup_storage_key {
	__u64	cgroup_inode_id;	/* cgroup inode id */
	__u32	attach_type;		/* program attach type */
};

union bpf_iter_link_info {
	struct {
		__u32	map_fd;
	} map;
};

/* BPF syscall commands, see bpf(2) man-page for details. */
enum bpf_cmd {
	BPF_MAP_CREATE,
	BPF_MAP_LOOKUP_ELEM,
	BPF_MAP_UPDATE_ELEM,
	BPF_MAP_DELETE_ELEM,
	BPF_MAP_GET_NEXT_KEY,
	BPF_PROG_LOAD,
	BPF_OBJ_PIN,
	BPF_OBJ_GET,
	BPF_PROG_ATTACH,
	BPF_PROG_DETACH,
	BPF_PROG_TEST_RUN,
	BPF_PROG_GET_NEXT_ID,
	BPF_MAP_GET_NEXT_ID,
	BPF_PROG_GET_FD_BY_ID,
	BPF_MAP_GET_FD_BY_ID,
	BPF_OBJ_GET_INFO_BY_FD,
	BPF_PROG_QUERY,
	BPF_RAW_TRACEPOINT_OPEN,
	BPF_BTF_LOAD,
	BPF_BTF_GET_FD_BY_ID,
	BPF_TASK_FD_QUERY,
	BPF_MAP_LOOKUP_AND_DELETE_ELEM,
	BPF_MAP_FREEZE,
	BPF_BTF_GET_NEXT_ID,
	BPF_MAP_LOOKUP_BATCH,
	BPF_MAP_LOOKUP_AND_DELETE_BATCH,
	BPF_MAP_UPDATE_BATCH,
	BPF_MAP_DELETE_BATCH,
	BPF_LINK_CREATE,
	BPF_LINK_UPDATE,
	BPF_LINK_GET_FD_BY_ID,
	BPF_LINK_GET_NEXT_ID,
	BPF_ENABLE_STATS,
	BPF_ITER_CREATE,
	BPF_LINK_DETACH,
	BPF_PROG_BIND_MAP,
};

enum bpf_map_type {
	BPF_MAP_TYPE_UNSPEC,
	BPF_MAP_TYPE_HASH,
	BPF_MAP_TYPE_ARRAY,
	BPF_MAP_TYPE_PROG_ARRAY,
	BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	BPF_MAP_TYPE_PERCPU_HASH,
	BPF_MAP_TYPE_PERCPU_ARRAY,
	BPF_MAP_TYPE_STACK_TRACE,
	BPF_MAP_TYPE_CGROUP_ARRAY,
	BPF_MAP_TYPE_LRU_HASH,
	BPF_MAP_TYPE_LRU_PERCPU_HASH,
	BPF_MAP_TYPE_LPM_TRIE,
	BPF_MAP_TYPE_ARRAY_OF_MAPS,
	BPF_MAP_TYPE_HASH_OF_MAPS,
	BPF_MAP_TYPE_DEVMAP,
	BPF_MAP_TYPE_SOCKMAP,
	BPF_MAP_TYPE_CPUMAP,
	BPF_MAP_TYPE_XSKMAP,
	BPF_MAP_TYPE_SOCKHASH,
	BPF_MAP_TYPE_CGROUP_STORAGE,
	BPF_MAP_TYPE_REUSEPORT_SOCKARRAY,
	BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE,
	BPF_MAP_TYPE_QUEUE,
	BPF_MAP_TYPE_STACK,
	BPF_MAP_TYPE_SK_STORAGE,
	BPF_MAP_TYPE_DEVMAP_HASH,
	BPF_MAP_TYPE_STRUCT_OPS,
	BPF_MAP_TYPE_RINGBUF,
	BPF_MAP_TYPE_INODE_STORAGE,
};

/* Note that tracing related programs such as
 * BPF_PROG_TYPE_{KPROBE,TRACEPOINT,PERF_EVENT,RAW_TRACEPOINT}
 * are not subject to a stable API since kernel internal data
 * structures can change from release to release and may
 * therefore break existing tracing BPF programs. Tracing BPF
 * programs correspond to /a/ specific kernel which is to be
 * analyzed, and not /a/ specific kernel /and/ all future ones.
 */
enum bpf_prog_type {
	BPF_PROG_TYPE_UNSPEC,
	BPF_PROG_TYPE_SOCKET_FILTER,
	BPF_PROG_TYPE_KPROBE,
	BPF_PROG_TYPE_SCHED_CLS,
	BPF_PROG_TYPE_SCHED_ACT,
	BPF_PROG_TYPE_TRACEPOINT,
	BPF_PROG_TYPE_XDP,
	BPF_PROG_TYPE_PERF_EVENT,
	BPF_PROG_TYPE_CGROUP_SKB,
	BPF_PROG_TYPE_CGROUP_SOCK,
	BPF_PROG_TYPE_LWT_IN,
	BPF_PROG_TYPE_LWT_OUT,
	BPF_PROG_TYPE_LWT_XMIT,
	BPF_PROG_TYPE_SOCK_OPS,
	BPF_PROG_TYPE_SK_SKB,
	BPF_PROG_TYPE_CGROUP_DEVICE,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_RAW_TRACEPOINT,
	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
	BPF_PROG_TYPE_LWT_SEG6LOCAL,
	BPF_PROG_TYPE_LIRC_MODE2,
	BPF_PROG_TYPE_SK_REUSEPORT,
	BPF_PROG_TYPE_FLOW_DISSECTOR,
	BPF_PROG_TYPE_CGROUP_SYSCTL,
	BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE,
	BPF_PROG_TYPE_CGROUP_SOCKOPT,
	BPF_PROG_TYPE_TRACING,
	BPF_PROG_TYPE_STRUCT_OPS,
	BPF_PROG_TYPE_EXT,
	BPF_PROG_TYPE_LSM,
	BPF_PROG_TYPE_SK_LOOKUP,
};

enum bpf_attach_type {
	BPF_CGROUP_INET_INGRESS,
	BPF_CGROUP_INET_EGRESS,
	BPF_CGROUP_INET_SOCK_CREATE,
	BPF_CGROUP_SOCK_OPS,
	BPF_SK_SKB_STREAM_PARSER,
	BPF_SK_SKB_STREAM_VERDICT,
	BPF_CGROUP_DEVICE,
	BPF_SK_MSG_VERDICT,
	BPF_CGROUP_INET4_BIND,
	BPF_CGROUP_INET6_BIND,
	BPF_CGROUP_INET4_CONNECT,
	BPF_CGROUP_INET6_CONNECT,
	BPF_CGROUP_INET4_POST_BIND,
	BPF_CGROUP_INET6_POST_BIND,
	BPF_CGROUP_UDP4_SENDMSG,
	BPF_CGROUP_UDP6_SENDMSG,
	BPF_LIRC_MODE2,
	BPF_FLOW_DISSECTOR,
	BPF_CGROUP_SYSCTL,
	BPF_CGROUP_UDP4_RECVMSG,
	BPF_CGROUP_UDP6_RECVMSG,
	BPF_CGROUP_GETSOCKOPT,
	BPF_CGROUP_SETSOCKOPT,
	BPF_TRACE_RAW_TP,
	BPF_TRACE_FENTRY,
	BPF_TRACE_FEXIT,
	BPF_MODIFY_RETURN,
	BPF_LSM_MAC,
	BPF_TRACE_ITER,
	BPF_CGROUP_INET4_GETPEERNAME,
	BPF_CGROUP_INET6_GETPEERNAME,
	BPF_CGROUP_INET4_GETSOCKNAME,
	BPF_CGROUP_INET6_GETSOCKNAME,
	BPF_XDP_DEVMAP,
	BPF_CGROUP_INET_SOCK_RELEASE,
	BPF_XDP_CPUMAP,
	BPF_SK_LOOKUP,
	BPF_XDP,
	__MAX_BPF_ATTACH_TYPE
};

#define MAX_BPF_ATTACH_TYPE __MAX_BPF_ATTACH_TYPE

enum bpf_link_type {
	BPF_LINK_TYPE_UNSPEC = 0,
	BPF_LINK_TYPE_RAW_TRACEPOINT = 1,
	BPF_LINK_TYPE_TRACING = 2,
	BPF_LINK_TYPE_CGROUP = 3,
	BPF_LINK_TYPE_ITER = 4,
	BPF_LINK_TYPE_NETNS = 5,
	BPF_LINK_TYPE_XDP = 6,

	MAX_BPF_LINK_TYPE,
};

/* cgroup-bpf attach flags used in BPF_PROG_ATTACH command
 *
 * NONE(default): No further bpf programs allowed in the subtree.
 *
 * BPF_F_ALLOW_OVERRIDE: If a sub-cgroup installs some bpf program,
 * the program in this cgroup yields to sub-cgroup program.
 *
 * BPF_F_ALLOW_MULTI: If a sub-cgroup installs some bpf program,
 * that cgroup program gets run in addition to the program in this cgroup.
 *
 * Only one program is allowed to be attached to a cgroup with
 * NONE or BPF_F_ALLOW_OVERRIDE flag.
 * Attaching another program on top of NONE or BPF_F_ALLOW_OVERRIDE will
 * release old program and attach the new one. Attach flags has to match.
 *
 * Multiple programs are allowed to be attached to a cgroup with
 * BPF_F_ALLOW_MULTI flag. They are executed in FIFO order
 * (those that were attached first, run first)
 * The programs of sub-cgroup are executed first, then programs of
 * this cgroup and then programs of parent cgroup.
 * When children program makes decision (like picking TCP CA or sock bind)
 * parent program has a chance to override it.
 *
 * With BPF_F_ALLOW_MULTI a new program is added to the end of the list of
 * programs for a cgroup. Though it's possible to replace an old program at
 * any position by also specifying BPF_F_REPLACE flag and position itself in
 * replace_bpf_fd attribute. Old program at this position will be released.
 *
 * A cgroup with MULTI or OVERRIDE flag allows any attach flags in sub-cgroups.
 * A cgroup with NONE doesn't allow any programs in sub-cgroups.
 * Ex1:
 * cgrp1 (MULTI progs A, B) ->
 *    cgrp2 (OVERRIDE prog C) ->
 *      cgrp3 (MULTI prog D) ->
 *        cgrp4 (OVERRIDE prog E) ->
 *          cgrp5 (NONE prog F)
 * the event in cgrp5 triggers execution of F,D,A,B in that order.
 * if prog F is detached, the execution is E,D,A,B
 * if prog F and D are detached, the execution is E,A,B
 * if prog F, E and D are detached, the execution is C,A,B
 *
 * All eligible programs are executed regardless of return code from
 * earlier programs.
 */
#define BPF_F_ALLOW_OVERRIDE	(1U << 0)
#define BPF_F_ALLOW_MULTI	(1U << 1)
#define BPF_F_REPLACE		(1U << 2)

/* If BPF_F_STRICT_ALIGNMENT is used in BPF_PROG_LOAD command, the
 * verifier will perform strict alignment checking as if the kernel
 * has been built with CONFIG_EFFICIENT_UNALIGNED_ACCESS not set,
 * and NET_IP_ALIGN defined to 2.
 */
#define BPF_F_STRICT_ALIGNMENT	(1U << 0)

/* If BPF_F_ANY_ALIGNMENT is used in BPF_PROF_LOAD command, the
 * verifier will allow any alignment whatsoever.  On platforms
 * with strict alignment requirements for loads ands stores (such
 * as sparc and mips) the verifier validates that all loads and
 * stores provably follow this requirement.  This flag turns that
 * checking and enforcement off.
 *
 * It is mostly used for testing when we want to validate the
 * context and memory access aspects of the verifier, but because
 * of an unaligned access the alignment check would trigger before
 * the one we are interested in.
 */
#define BPF_F_ANY_ALIGNMENT	(1U << 1)

/* BPF_F_TEST_RND_HI32 is used in BPF_PROG_LOAD command for testing purpose.
 * Verifier does sub-register def/use analysis and identifies instructions whose
 * def only matters for low 32-bit, high 32-bit is never referenced later
 * through implicit zero extension. Therefore verifier notifies JIT back-ends
 * that it is safe to ignore clearing high 32-bit for these instructions. This
 * saves some back-ends a lot of code-gen. However such optimization is not
 * necessary on some arches, for example x86_64, arm64 etc, whose JIT back-ends
 * hence hasn't used verifier's analysis result. But, we really want to have a
 * way to be able to verify the correctness of the described optimization on
 * x86_64 on which testsuites are frequently exercised.
 *
 * So, this flag is introduced. Once it is set, verifier will randomize high
 * 32-bit for those instructions who has been identified as safe to ignore them.
 * Then, if verifier is not doing correct analysis, such randomization will
 * regress tests to expose bugs.
 */
#define BPF_F_TEST_RND_HI32	(1U << 2)

/* The verifier internal test flag. Behavior is undefined */
#define BPF_F_TEST_STATE_FREQ	(1U << 3)

/* If BPF_F_SLEEPABLE is used in BPF_PROG_LOAD command, the verifier will
 * restrict map and helper usage for such programs. Sleepable BPF programs can
 * only be attached to hooks where kernel execution context allows sleeping.
 * Such programs are allowed to use helpers that may sleep like
 * bpf_copy_from_user().
 */
#define BPF_F_SLEEPABLE		(1U << 4)

/* When BPF ldimm64's insn[0].src_reg != 0 then this can have
 * the following extensions:
 *
 * insn[0].src_reg:  BPF_PSEUDO_MAP_FD
 * insn[0].imm:      map fd
 * insn[1].imm:      0
 * insn[0].off:      0
 * insn[1].off:      0
 * ldimm64 rewrite:  address of map
 * verifier type:    CONST_PTR_TO_MAP
 */
#define BPF_PSEUDO_MAP_FD	1
/* insn[0].src_reg:  BPF_PSEUDO_MAP_VALUE
 * insn[0].imm:      map fd
 * insn[1].imm:      offset into value
 * insn[0].off:      0
 * insn[1].off:      0
 * ldimm64 rewrite:  address of map[0]+offset
 * verifier type:    PTR_TO_MAP_VALUE
 */
#define BPF_PSEUDO_MAP_VALUE	2
/* insn[0].src_reg:  BPF_PSEUDO_BTF_ID
 * insn[0].imm:      kernel btd id of VAR
 * insn[1].imm:      0
 * insn[0].off:      0
 * insn[1].off:      0
 * ldimm64 rewrite:  address of the kernel variable
 * verifier type:    PTR_TO_BTF_ID or PTR_TO_MEM, depending on whether the var
 *                   is struct/union.
 */
#define BPF_PSEUDO_BTF_ID	3

/* when bpf_call->src_reg == BPF_PSEUDO_CALL, bpf_call->imm == pc-relative
 * offset to another bpf function
 */
#define BPF_PSEUDO_CALL		1

/* flags for BPF_MAP_UPDATE_ELEM command */
enum {
	BPF_ANY		= 0, /* create new element or update existing */
	BPF_NOEXIST	= 1, /* create new element if it didn't exist */
	BPF_EXIST	= 2, /* update existing element */
	BPF_F_LOCK	= 4, /* spin_lock-ed map_lookup/map_update */
};

/* flags for BPF_MAP_CREATE command */
enum {
	BPF_F_NO_PREALLOC	= (1U << 0),
/* Instead of having one common LRU list in the
 * BPF_MAP_TYPE_LRU_[PERCPU_]HASH map, use a percpu LRU list
 * which can scale and perform better.
 * Note, the LRU nodes (including free nodes) cannot be moved
 * across different LRU lists.
 */
	BPF_F_NO_COMMON_LRU	= (1U << 1),
/* Specify numa node during map creation */
	BPF_F_NUMA_NODE		= (1U << 2),

/* Flags for accessing BPF object from syscall side. */
	BPF_F_RDONLY		= (1U << 3),
	BPF_F_WRONLY		= (1U << 4),

/* Flag for stack_map, store build_id+offset instead of pointer */
	BPF_F_STACK_BUILD_ID	= (1U << 5),

/* Zero-initialize hash function seed. This should only be used for testing. */
	BPF_F_ZERO_SEED		= (1U << 6),

/* Flags for accessing BPF object from program side. */
	BPF_F_RDONLY_PROG	= (1U << 7),
	BPF_F_WRONLY_PROG	= (1U << 8),

/* Clone map from listener for newly accepted socket */
	BPF_F_CLONE		= (1U << 9),

/* Enable memory-mapping BPF map */
	BPF_F_MMAPABLE		= (1U << 10),

/* Share perf_event among processes */
	BPF_F_PRESERVE_ELEMS	= (1U << 11),
};

/* Flags for BPF_PROG_QUERY. */

/* Query effective (directly attached + inherited from ancestor cgroups)
 * programs that will be executed for events within a cgroup.
 * attach_flags with this flag are returned only for directly attached programs.
 */
#define BPF_F_QUERY_EFFECTIVE	(1U << 0)

/* Flags for BPF_PROG_TEST_RUN */

/* If set, run the test on the cpu specified by bpf_attr.test.cpu */
#define BPF_F_TEST_RUN_ON_CPU	(1U << 0)

/* type for BPF_ENABLE_STATS */
enum bpf_stats_type {
	/* enabled run_time_ns and run_cnt */
	BPF_STATS_RUN_TIME = 0,
};

enum bpf_stack_build_id_status {
	/* user space need an empty entry to identify end of a trace */
	BPF_STACK_BUILD_ID_EMPTY = 0,
	/* with valid build_id and offset */
	BPF_STACK_BUILD_ID_VALID = 1,
	/* couldn't get build_id, fallback to ip */
	BPF_STACK_BUILD_ID_IP = 2,
};

#define BPF_BUILD_ID_SIZE 20
struct bpf_stack_build_id {
	__s32		status;
	unsigned char	build_id[BPF_BUILD_ID_SIZE];
	union {
		__u64	offset;
		__u64	ip;
	};
};

#define BPF_OBJ_NAME_LEN 16U

union bpf_attr {
	struct { /* anonymous struct used by BPF_MAP_CREATE command */
		__u32	map_type;	/* one of enum bpf_map_type */
		__u32	key_size;	/* size of key in bytes */
		__u32	value_size;	/* size of value in bytes */
		__u32	max_entries;	/* max number of entries in a map */
		__u32	map_flags;	/* BPF_MAP_CREATE related
					 * flags defined above.
					 */
		__u32	inner_map_fd;	/* fd pointing to the inner map */
		__u32	numa_node;	/* numa node (effective only if
					 * BPF_F_NUMA_NODE is set).
					 */
		char	map_name[BPF_OBJ_NAME_LEN];
		__u32	map_ifindex;	/* ifindex of netdev to create on */
		__u32	btf_fd;		/* fd pointing to a BTF type data */
		__u32	btf_key_type_id;	/* BTF type_id of the key */
		__u32	btf_value_type_id;	/* BTF type_id of the value */
		__u32	btf_vmlinux_value_type_id;/* BTF type_id of a kernel-
						   * struct stored as the
						   * map value
						   */
	};

	struct { /* anonymous struct used by BPF_MAP_*_ELEM commands */
		__u32		map_fd;
		__aligned_u64	key;
		union {
			__aligned_u64 value;
			__aligned_u64 next_key;
		};
		__u64		flags;
	};

	struct { /* struct used by BPF_MAP_*_BATCH commands */
		__aligned_u64	in_batch;	/* start batch,
						 * NULL to start from beginning
						 */
		__aligned_u64	out_batch;	/* output: next start batch */
		__aligned_u64	keys;
		__aligned_u64	values;
		__u32		count;		/* input/output:
						 * input: # of key/value
						 * elements
						 * output: # of filled elements
						 */
		__u32		map_fd;
		__u64		elem_flags;
		__u64		flags;
	} batch;

	struct { /* anonymous struct used by BPF_PROG_LOAD command */
		__u32		prog_type;	/* one of enum bpf_prog_type */
		__u32		insn_cnt;
		__aligned_u64	insns;
		__aligned_u64	license;
		__u32		log_level;	/* verbosity level of verifier */
		__u32		log_size;	/* size of user buffer */
		__aligned_u64	log_buf;	/* user supplied buffer */
		__u32		kern_version;	/* not used */
		__u32		prog_flags;
		char		prog_name[BPF_OBJ_NAME_LEN];
		__u32		prog_ifindex;	/* ifindex of netdev to prep for */
		/* For some prog types expected attach type must be known at
		 * load time to verify attach type specific parts of prog
		 * (context accesses, allowed helpers, etc).
		 */
		__u32		expected_attach_type;
		__u32		prog_btf_fd;	/* fd pointing to BTF type data */
		__u32		func_info_rec_size;	/* userspace bpf_func_info size */
		__aligned_u64	func_info;	/* func info */
		__u32		func_info_cnt;	/* number of bpf_func_info records */
		__u32		line_info_rec_size;	/* userspace bpf_line_info size */
		__aligned_u64	line_info;	/* line info */
		__u32		line_info_cnt;	/* number of bpf_line_info records */
		__u32		attach_btf_id;	/* in-kernel BTF type id to attach to */
		__u32		attach_prog_fd; /* 0 to attach to vmlinux */
	};

	struct { /* anonymous struct used by BPF_OBJ_* commands */
		__aligned_u64	pathname;
		__u32		bpf_fd;
		__u32		file_flags;
	};

	struct { /* anonymous struct used by BPF_PROG_ATTACH/DETACH commands */
		__u32		target_fd;	/* container object to attach to */
		__u32		attach_bpf_fd;	/* eBPF program to attach */
		__u32		attach_type;
		__u32		attach_flags;
		__u32		replace_bpf_fd;	/* previously attached eBPF
						 * program to replace if
						 * BPF_F_REPLACE is used
						 */
	};

	struct { /* anonymous struct used by BPF_PROG_TEST_RUN command */
		__u32		prog_fd;
		__u32		retval;
		__u32		data_size_in;	/* input: len of data_in */
		__u32		data_size_out;	/* input/output: len of data_out
						 *   returns ENOSPC if data_out
						 *   is too small.
						 */
		__aligned_u64	data_in;
		__aligned_u64	data_out;
		__u32		repeat;
		__u32		duration;
		__u32		ctx_size_in;	/* input: len of ctx_in */
		__u32		ctx_size_out;	/* input/output: len of ctx_out
						 *   returns ENOSPC if ctx_out
						 *   is too small.
						 */
		__aligned_u64	ctx_in;
		__aligned_u64	ctx_out;
		__u32		flags;
		__u32		cpu;
	} test;

	struct { /* anonymous struct used by BPF_*_GET_*_ID */
		union {
			__u32		start_id;
			__u32		prog_id;
			__u32		map_id;
			__u32		btf_id;
			__u32		link_id;
		};
		__u32		next_id;
		__u32		open_flags;
	};

	struct { /* anonymous struct used by BPF_OBJ_GET_INFO_BY_FD */
		__u32		bpf_fd;
		__u32		info_len;
		__aligned_u64	info;
	} info;

	struct { /* anonymous struct used by BPF_PROG_QUERY command */
		__u32		target_fd;	/* container object to query */
		__u32		attach_type;
		__u32		query_flags;
		__u32		attach_flags;
		__aligned_u64	prog_ids;
		__u32		prog_cnt;
	} query;

	struct { /* anonymous struct used by BPF_RAW_TRACEPOINT_OPEN command */
		__u64 name;
		__u32 prog_fd;
	} raw_tracepoint;

	struct { /* anonymous struct for BPF_BTF_LOAD */
		__aligned_u64	btf;
		__aligned_u64	btf_log_buf;
		__u32		btf_size;
		__u32		btf_log_size;
		__u32		btf_log_level;
	};

	struct {
		__u32		pid;		/* input: pid */
		__u32		fd;		/* input: fd */
		__u32		flags;		/* input: flags */
		__u32		buf_len;	/* input/output: buf len */
		__aligned_u64	buf;		/* input/output:
						 *   tp_name for tracepoint
						 *   symbol for kprobe
						 *   filename for uprobe
						 */
		__u32		prog_id;	/* output: prod_id */
		__u32		fd_type;	/* output: BPF_FD_TYPE_* */
		__u64		probe_offset;	/* output: probe_offset */
		__u64		probe_addr;	/* output: probe_addr */
	} task_fd_query;

	struct { /* struct used by BPF_LINK_CREATE command */
		__u32		prog_fd;	/* eBPF program to attach */
		union {
			__u32		target_fd;	/* object to attach to */
			__u32		target_ifindex; /* target ifindex */
		};
		__u32		attach_type;	/* attach type */
		__u32		flags;		/* extra flags */
		union {
			__u32		target_btf_id;	/* btf_id of target to attach to */
			struct {
				__aligned_u64	iter_info;	/* extra bpf_iter_link_info */
				__u32		iter_info_len;	/* iter_info length */
			};
		};
	} link_create;

	struct { /* struct used by BPF_LINK_UPDATE command */
		__u32		link_fd;	/* link fd */
		/* new program fd to update link with */
		__u32		new_prog_fd;
		__u32		flags;		/* extra flags */
		/* expected link's program fd; is specified only if
		 * BPF_F_REPLACE flag is set in flags */
		__u32		old_prog_fd;
	} link_update;

	struct {
		__u32		link_fd;
	} link_detach;

	struct { /* struct used by BPF_ENABLE_STATS command */
		__u32		type;
	} enable_stats;

	struct { /* struct used by BPF_ITER_CREATE command */
		__u32		link_fd;
		__u32		flags;
	} iter_create;

	struct { /* struct used by BPF_PROG_BIND_MAP command */
		__u32		prog_fd;
		__u32		map_fd;
		__u32		flags;		/* extra flags */
	} prog_bind_map;

} __attribute__((aligned(8)));

/* The description below is an attempt at providing documentation to eBPF
 * developers about the multiple available eBPF helper functions. It can be
 * parsed and used to produce a manual page. The workflow is the following,
 * and requires the rst2man utility:
 *
 *     $ ./scripts/bpf_helpers_doc.py \
 *             --filename include/uapi/linux/bpf.h > /tmp/bpf-helpers.rst
 *     $ rst2man /tmp/bpf-helpers.rst > /tmp/bpf-helpers.7
 *     $ man /tmp/bpf-helpers.7
 *
 * Note that in order to produce this external documentation, some RST
 * formatting is used in the descriptions to get "bold" and "italics" in
 * manual pages. Also note that the few trailing white spaces are
 * intentional, removing them would break paragraphs for rst2man.
 *
 * Start of BPF helper function descriptions:
 *
 * void *bpf_map_lookup_elem(struct bpf_map *map, const void *key)
 * 	Description
 * 		Perform a lookup in *map* for an entry associated to *key*.
 * 	Return
 * 		Map value associated to *key*, or **NULL** if no entry was
 * 		found.
 *
 * long bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, u64 flags)
 * 	Description
 * 		Add or update the value of the entry associated to *key* in
 * 		*map* with *value*. *flags* is one of:
 *
 * 		**BPF_NOEXIST**
 * 			The entry for *key* must not exist in the map.
 * 		**BPF_EXIST**
 * 			The entry for *key* must already exist in the map.
 * 		**BPF_ANY**
 * 			No condition on the existence of the entry for *key*.
 *
 * 		Flag value **BPF_NOEXIST** cannot be used for maps of types
 * 		**BPF_MAP_TYPE_ARRAY** or **BPF_MAP_TYPE_PERCPU_ARRAY**  (all
 * 		elements always exist), the helper would return an error.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_map_delete_elem(struct bpf_map *map, const void *key)
 * 	Description
 * 		Delete entry with *key* from *map*.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_probe_read(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		For tracing programs, safely attempt to read *size* bytes from
 * 		kernel space address *unsafe_ptr* and store the data in *dst*.
 *
 * 		Generally, use **bpf_probe_read_user**\ () or
 * 		**bpf_probe_read_kernel**\ () instead.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * u64 bpf_ktime_get_ns(void)
 * 	Description
 * 		Return the time elapsed since system boot, in nanoseconds.
 * 		Does not include time the system was suspended.
 * 		See: **clock_gettime**\ (**CLOCK_MONOTONIC**)
 * 	Return
 * 		Current *ktime*.
 *
 * long bpf_trace_printk(const char *fmt, u32 fmt_size, ...)
 * 	Description
 * 		This helper is a "printk()-like" facility for debugging. It
 * 		prints a message defined by format *fmt* (of size *fmt_size*)
 * 		to file *\/sys/kernel/debug/tracing/trace* from DebugFS, if
 * 		available. It can take up to three additional **u64**
 * 		arguments (as an eBPF helpers, the total number of arguments is
 * 		limited to five).
 *
 * 		Each time the helper is called, it appends a line to the trace.
 * 		Lines are discarded while *\/sys/kernel/debug/tracing/trace* is
 * 		open, use *\/sys/kernel/debug/tracing/trace_pipe* to avoid this.
 * 		The format of the trace is customizable, and the exact output
 * 		one will get depends on the options set in
 * 		*\/sys/kernel/debug/tracing/trace_options* (see also the
 * 		*README* file under the same directory). However, it usually
 * 		defaults to something like:
 *
 * 		::
 *
 * 			telnet-470   [001] .N.. 419421.045894: 0x00000001: <formatted msg>
 *
 * 		In the above:
 *
 * 			* ``telnet`` is the name of the current task.
 * 			* ``470`` is the PID of the current task.
 * 			* ``001`` is the CPU number on which the task is
 * 			  running.
 * 			* In ``.N..``, each character refers to a set of
 * 			  options (whether irqs are enabled, scheduling
 * 			  options, whether hard/softirqs are running, level of
 * 			  preempt_disabled respectively). **N** means that
 * 			  **TIF_NEED_RESCHED** and **PREEMPT_NEED_RESCHED**
 * 			  are set.
 * 			* ``419421.045894`` is a timestamp.
 * 			* ``0x00000001`` is a fake value used by BPF for the
 * 			  instruction pointer register.
 * 			* ``<formatted msg>`` is the message formatted with
 * 			  *fmt*.
 *
 * 		The conversion specifiers supported by *fmt* are similar, but
 * 		more limited than for printk(). They are **%d**, **%i**,
 * 		**%u**, **%x**, **%ld**, **%li**, **%lu**, **%lx**, **%lld**,
 * 		**%lli**, **%llu**, **%llx**, **%p**, **%s**. No modifier (size
 * 		of field, padding with zeroes, etc.) is available, and the
 * 		helper will return **-EINVAL** (but print nothing) if it
 * 		encounters an unknown specifier.
 *
 * 		Also, note that **bpf_trace_printk**\ () is slow, and should
 * 		only be used for debugging purposes. For this reason, a notice
 * 		block (spanning several lines) is printed to kernel logs and
 * 		states that the helper should not be used "for production use"
 * 		the first time this helper is used (or more precisely, when
 * 		**trace_printk**\ () buffers are allocated). For passing values
 * 		to user space, perf events should be preferred.
 * 	Return
 * 		The number of bytes written to the buffer, or a negative error
 * 		in case of failure.
 *
 * u32 bpf_get_prandom_u32(void)
 * 	Description
 * 		Get a pseudo-random number.
 *
 * 		From a security point of view, this helper uses its own
 * 		pseudo-random internal state, and cannot be used to infer the
 * 		seed of other random functions in the kernel. However, it is
 * 		essential to note that the generator used by the helper is not
 * 		cryptographically secure.
 * 	Return
 * 		A random 32-bit unsigned value.
 *
 * u32 bpf_get_smp_processor_id(void)
 * 	Description
 * 		Get the SMP (symmetric multiprocessing) processor id. Note that
 * 		all programs run with preemption disabled, which means that the
 * 		SMP processor id is stable during all the execution of the
 * 		program.
 * 	Return
 * 		The SMP id of the processor running the program.
 *
 * long bpf_skb_store_bytes(struct sk_buff *skb, u32 offset, const void *from, u32 len, u64 flags)
 * 	Description
 * 		Store *len* bytes from address *from* into the packet
 * 		associated to *skb*, at *offset*. *flags* are a combination of
 * 		**BPF_F_RECOMPUTE_CSUM** (automatically recompute the
 * 		checksum for the packet after storing the bytes) and
 * 		**BPF_F_INVALIDATE_HASH** (set *skb*\ **->hash**, *skb*\
 * 		**->swhash** and *skb*\ **->l4hash** to 0).
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_l3_csum_replace(struct sk_buff *skb, u32 offset, u64 from, u64 to, u64 size)
 * 	Description
 * 		Recompute the layer 3 (e.g. IP) checksum for the packet
 * 		associated to *skb*. Computation is incremental, so the helper
 * 		must know the former value of the header field that was
 * 		modified (*from*), the new value of this field (*to*), and the
 * 		number of bytes (2 or 4) for this field, stored in *size*.
 * 		Alternatively, it is possible to store the difference between
 * 		the previous and the new values of the header field in *to*, by
 * 		setting *from* and *size* to 0. For both methods, *offset*
 * 		indicates the location of the IP checksum within the packet.
 *
 * 		This helper works in combination with **bpf_csum_diff**\ (),
 * 		which does not update the checksum in-place, but offers more
 * 		flexibility and can handle sizes larger than 2 or 4 for the
 * 		checksum to update.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_l4_csum_replace(struct sk_buff *skb, u32 offset, u64 from, u64 to, u64 flags)
 * 	Description
 * 		Recompute the layer 4 (e.g. TCP, UDP or ICMP) checksum for the
 * 		packet associated to *skb*. Computation is incremental, so the
 * 		helper must know the former value of the header field that was
 * 		modified (*from*), the new value of this field (*to*), and the
 * 		number of bytes (2 or 4) for this field, stored on the lowest
 * 		four bits of *flags*. Alternatively, it is possible to store
 * 		the difference between the previous and the new values of the
 * 		header field in *to*, by setting *from* and the four lowest
 * 		bits of *flags* to 0. For both methods, *offset* indicates the
 * 		location of the IP checksum within the packet. In addition to
 * 		the size of the field, *flags* can be added (bitwise OR) actual
 * 		flags. With **BPF_F_MARK_MANGLED_0**, a null checksum is left
 * 		untouched (unless **BPF_F_MARK_ENFORCE** is added as well), and
 * 		for updates resulting in a null checksum the value is set to
 * 		**CSUM_MANGLED_0** instead. Flag **BPF_F_PSEUDO_HDR** indicates
 * 		the checksum is to be computed against a pseudo-header.
 *
 * 		This helper works in combination with **bpf_csum_diff**\ (),
 * 		which does not update the checksum in-place, but offers more
 * 		flexibility and can handle sizes larger than 2 or 4 for the
 * 		checksum to update.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_tail_call(void *ctx, struct bpf_map *prog_array_map, u32 index)
 * 	Description
 * 		This special helper is used to trigger a "tail call", or in
 * 		other words, to jump into another eBPF program. The same stack
 * 		frame is used (but values on stack and in registers for the
 * 		caller are not accessible to the callee). This mechanism allows
 * 		for program chaining, either for raising the maximum number of
 * 		available eBPF instructions, or to execute given programs in
 * 		conditional blocks. For security reasons, there is an upper
 * 		limit to the number of successive tail calls that can be
 * 		performed.
 *
 * 		Upon call of this helper, the program attempts to jump into a
 * 		program referenced at index *index* in *prog_array_map*, a
 * 		special map of type **BPF_MAP_TYPE_PROG_ARRAY**, and passes
 * 		*ctx*, a pointer to the context.
 *
 * 		If the call succeeds, the kernel immediately runs the first
 * 		instruction of the new program. This is not a function call,
 * 		and it never returns to the previous program. If the call
 * 		fails, then the helper has no effect, and the caller continues
 * 		to run its subsequent instructions. A call can fail if the
 * 		destination program for the jump does not exist (i.e. *index*
 * 		is superior to the number of entries in *prog_array_map*), or
 * 		if the maximum number of tail calls has been reached for this
 * 		chain of programs. This limit is defined in the kernel by the
 * 		macro **MAX_TAIL_CALL_CNT** (not accessible to user space),
 * 		which is currently set to 32.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_clone_redirect(struct sk_buff *skb, u32 ifindex, u64 flags)
 * 	Description
 * 		Clone and redirect the packet associated to *skb* to another
 * 		net device of index *ifindex*. Both ingress and egress
 * 		interfaces can be used for redirection. The **BPF_F_INGRESS**
 * 		value in *flags* is used to make the distinction (ingress path
 * 		is selected if the flag is present, egress path otherwise).
 * 		This is the only flag supported for now.
 *
 * 		In comparison with **bpf_redirect**\ () helper,
 * 		**bpf_clone_redirect**\ () has the associated cost of
 * 		duplicating the packet buffer, but this can be executed out of
 * 		the eBPF program. Conversely, **bpf_redirect**\ () is more
 * 		efficient, but it is handled through an action code where the
 * 		redirection happens only after the eBPF program has returned.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * u64 bpf_get_current_pid_tgid(void)
 * 	Return
 * 		A 64-bit integer containing the current tgid and pid, and
 * 		created as such:
 * 		*current_task*\ **->tgid << 32 \|**
 * 		*current_task*\ **->pid**.
 *
 * u64 bpf_get_current_uid_gid(void)
 * 	Return
 * 		A 64-bit integer containing the current GID and UID, and
 * 		created as such: *current_gid* **<< 32 \|** *current_uid*.
 *
 * long bpf_get_current_comm(void *buf, u32 size_of_buf)
 * 	Description
 * 		Copy the **comm** attribute of the current task into *buf* of
 * 		*size_of_buf*. The **comm** attribute contains the name of
 * 		the executable (excluding the path) for the current task. The
 * 		*size_of_buf* must be strictly positive. On success, the
 * 		helper makes sure that the *buf* is NUL-terminated. On failure,
 * 		it is filled with zeroes.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * u32 bpf_get_cgroup_classid(struct sk_buff *skb)
 * 	Description
 * 		Retrieve the classid for the current task, i.e. for the net_cls
 * 		cgroup to which *skb* belongs.
 *
 * 		This helper can be used on TC egress path, but not on ingress.
 *
 * 		The net_cls cgroup provides an interface to tag network packets
 * 		based on a user-provided identifier for all traffic coming from
 * 		the tasks belonging to the related cgroup. See also the related
 * 		kernel documentation, available from the Linux sources in file
 * 		*Documentation/admin-guide/cgroup-v1/net_cls.rst*.
 *
 * 		The Linux kernel has two versions for cgroups: there are
 * 		cgroups v1 and cgroups v2. Both are available to users, who can
 * 		use a mixture of them, but note that the net_cls cgroup is for
 * 		cgroup v1 only. This makes it incompatible with BPF programs
 * 		run on cgroups, which is a cgroup-v2-only feature (a socket can
 * 		only hold data for one version of cgroups at a time).
 *
 * 		This helper is only available is the kernel was compiled with
 * 		the **CONFIG_CGROUP_NET_CLASSID** configuration option set to
 * 		"**y**" or to "**m**".
 * 	Return
 * 		The classid, or 0 for the default unconfigured classid.
 *
 * long bpf_skb_vlan_push(struct sk_buff *skb, __be16 vlan_proto, u16 vlan_tci)
 * 	Description
 * 		Push a *vlan_tci* (VLAN tag control information) of protocol
 * 		*vlan_proto* to the packet associated to *skb*, then update
 * 		the checksum. Note that if *vlan_proto* is different from
 * 		**ETH_P_8021Q** and **ETH_P_8021AD**, it is considered to
 * 		be **ETH_P_8021Q**.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_vlan_pop(struct sk_buff *skb)
 * 	Description
 * 		Pop a VLAN header from the packet associated to *skb*.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_get_tunnel_key(struct sk_buff *skb, struct bpf_tunnel_key *key, u32 size, u64 flags)
 * 	Description
 * 		Get tunnel metadata. This helper takes a pointer *key* to an
 * 		empty **struct bpf_tunnel_key** of **size**, that will be
 * 		filled with tunnel metadata for the packet associated to *skb*.
 * 		The *flags* can be set to **BPF_F_TUNINFO_IPV6**, which
 * 		indicates that the tunnel is based on IPv6 protocol instead of
 * 		IPv4.
 *
 * 		The **struct bpf_tunnel_key** is an object that generalizes the
 * 		principal parameters used by various tunneling protocols into a
 * 		single struct. This way, it can be used to easily make a
 * 		decision based on the contents of the encapsulation header,
 * 		"summarized" in this struct. In particular, it holds the IP
 * 		address of the remote end (IPv4 or IPv6, depending on the case)
 * 		in *key*\ **->remote_ipv4** or *key*\ **->remote_ipv6**. Also,
 * 		this struct exposes the *key*\ **->tunnel_id**, which is
 * 		generally mapped to a VNI (Virtual Network Identifier), making
 * 		it programmable together with the **bpf_skb_set_tunnel_key**\
 * 		() helper.
 *
 * 		Let's imagine that the following code is part of a program
 * 		attached to the TC ingress interface, on one end of a GRE
 * 		tunnel, and is supposed to filter out all messages coming from
 * 		remote ends with IPv4 address other than 10.0.0.1:
 *
 * 		::
 *
 * 			int ret;
 * 			struct bpf_tunnel_key key = {};
 *
 * 			ret = bpf_skb_get_tunnel_key(skb, &key, sizeof(key), 0);
 * 			if (ret < 0)
 * 				return TC_ACT_SHOT;	// drop packet
 *
 * 			if (key.remote_ipv4 != 0x0a000001)
 * 				return TC_ACT_SHOT;	// drop packet
 *
 * 			return TC_ACT_OK;		// accept packet
 *
 * 		This interface can also be used with all encapsulation devices
 * 		that can operate in "collect metadata" mode: instead of having
 * 		one network device per specific configuration, the "collect
 * 		metadata" mode only requires a single device where the
 * 		configuration can be extracted from this helper.
 *
 * 		This can be used together with various tunnels such as VXLan,
 * 		Geneve, GRE or IP in IP (IPIP).
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_set_tunnel_key(struct sk_buff *skb, struct bpf_tunnel_key *key, u32 size, u64 flags)
 * 	Description
 * 		Populate tunnel metadata for packet associated to *skb.* The
 * 		tunnel metadata is set to the contents of *key*, of *size*. The
 * 		*flags* can be set to a combination of the following values:
 *
 * 		**BPF_F_TUNINFO_IPV6**
 * 			Indicate that the tunnel is based on IPv6 protocol
 * 			instead of IPv4.
 * 		**BPF_F_ZERO_CSUM_TX**
 * 			For IPv4 packets, add a flag to tunnel metadata
 * 			indicating that checksum computation should be skipped
 * 			and checksum set to zeroes.
 * 		**BPF_F_DONT_FRAGMENT**
 * 			Add a flag to tunnel metadata indicating that the
 * 			packet should not be fragmented.
 * 		**BPF_F_SEQ_NUMBER**
 * 			Add a flag to tunnel metadata indicating that a
 * 			sequence number should be added to tunnel header before
 * 			sending the packet. This flag was added for GRE
 * 			encapsulation, but might be used with other protocols
 * 			as well in the future.
 *
 * 		Here is a typical usage on the transmit path:
 *
 * 		::
 *
 * 			struct bpf_tunnel_key key;
 * 			     populate key ...
 * 			bpf_skb_set_tunnel_key(skb, &key, sizeof(key), 0);
 * 			bpf_clone_redirect(skb, vxlan_dev_ifindex, 0);
 *
 * 		See also the description of the **bpf_skb_get_tunnel_key**\ ()
 * 		helper for additional information.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * u64 bpf_perf_event_read(struct bpf_map *map, u64 flags)
 * 	Description
 * 		Read the value of a perf event counter. This helper relies on a
 * 		*map* of type **BPF_MAP_TYPE_PERF_EVENT_ARRAY**. The nature of
 * 		the perf event counter is selected when *map* is updated with
 * 		perf event file descriptors. The *map* is an array whose size
 * 		is the number of available CPUs, and each cell contains a value
 * 		relative to one CPU. The value to retrieve is indicated by
 * 		*flags*, that contains the index of the CPU to look up, masked
 * 		with **BPF_F_INDEX_MASK**. Alternatively, *flags* can be set to
 * 		**BPF_F_CURRENT_CPU** to indicate that the value for the
 * 		current CPU should be retrieved.
 *
 * 		Note that before Linux 4.13, only hardware perf event can be
 * 		retrieved.
 *
 * 		Also, be aware that the newer helper
 * 		**bpf_perf_event_read_value**\ () is recommended over
 * 		**bpf_perf_event_read**\ () in general. The latter has some ABI
 * 		quirks where error and counter value are used as a return code
 * 		(which is wrong to do since ranges may overlap). This issue is
 * 		fixed with **bpf_perf_event_read_value**\ (), which at the same
 * 		time provides more features over the **bpf_perf_event_read**\
 * 		() interface. Please refer to the description of
 * 		**bpf_perf_event_read_value**\ () for details.
 * 	Return
 * 		The value of the perf event counter read from the map, or a
 * 		negative error code in case of failure.
 *
 * long bpf_redirect(u32 ifindex, u64 flags)
 * 	Description
 * 		Redirect the packet to another net device of index *ifindex*.
 * 		This helper is somewhat similar to **bpf_clone_redirect**\
 * 		(), except that the packet is not cloned, which provides
 * 		increased performance.
 *
 * 		Except for XDP, both ingress and egress interfaces can be used
 * 		for redirection. The **BPF_F_INGRESS** value in *flags* is used
 * 		to make the distinction (ingress path is selected if the flag
 * 		is present, egress path otherwise). Currently, XDP only
 * 		supports redirection to the egress interface, and accepts no
 * 		flag at all.
 *
 * 		The same effect can also be attained with the more generic
 * 		**bpf_redirect_map**\ (), which uses a BPF map to store the
 * 		redirect target instead of providing it directly to the helper.
 * 	Return
 * 		For XDP, the helper returns **XDP_REDIRECT** on success or
 * 		**XDP_ABORTED** on error. For other program types, the values
 * 		are **TC_ACT_REDIRECT** on success or **TC_ACT_SHOT** on
 * 		error.
 *
 * u32 bpf_get_route_realm(struct sk_buff *skb)
 * 	Description
 * 		Retrieve the realm or the route, that is to say the
 * 		**tclassid** field of the destination for the *skb*. The
 * 		identifier retrieved is a user-provided tag, similar to the
 * 		one used with the net_cls cgroup (see description for
 * 		**bpf_get_cgroup_classid**\ () helper), but here this tag is
 * 		held by a route (a destination entry), not by a task.
 *
 * 		Retrieving this identifier works with the clsact TC egress hook
 * 		(see also **tc-bpf(8)**), or alternatively on conventional
 * 		classful egress qdiscs, but not on TC ingress path. In case of
 * 		clsact TC egress hook, this has the advantage that, internally,
 * 		the destination entry has not been dropped yet in the transmit
 * 		path. Therefore, the destination entry does not need to be
 * 		artificially held via **netif_keep_dst**\ () for a classful
 * 		qdisc until the *skb* is freed.
 *
 * 		This helper is available only if the kernel was compiled with
 * 		**CONFIG_IP_ROUTE_CLASSID** configuration option.
 * 	Return
 * 		The realm of the route for the packet associated to *skb*, or 0
 * 		if none was found.
 *
 * long bpf_perf_event_output(void *ctx, struct bpf_map *map, u64 flags, void *data, u64 size)
 * 	Description
 * 		Write raw *data* blob into a special BPF perf event held by
 * 		*map* of type **BPF_MAP_TYPE_PERF_EVENT_ARRAY**. This perf
 * 		event must have the following attributes: **PERF_SAMPLE_RAW**
 * 		as **sample_type**, **PERF_TYPE_SOFTWARE** as **type**, and
 * 		**PERF_COUNT_SW_BPF_OUTPUT** as **config**.
 *
 * 		The *flags* are used to indicate the index in *map* for which
 * 		the value must be put, masked with **BPF_F_INDEX_MASK**.
 * 		Alternatively, *flags* can be set to **BPF_F_CURRENT_CPU**
 * 		to indicate that the index of the current CPU core should be
 * 		used.
 *
 * 		The value to write, of *size*, is passed through eBPF stack and
 * 		pointed by *data*.
 *
 * 		The context of the program *ctx* needs also be passed to the
 * 		helper.
 *
 * 		On user space, a program willing to read the values needs to
 * 		call **perf_event_open**\ () on the perf event (either for
 * 		one or for all CPUs) and to store the file descriptor into the
 * 		*map*. This must be done before the eBPF program can send data
 * 		into it. An example is available in file
 * 		*samples/bpf/trace_output_user.c* in the Linux kernel source
 * 		tree (the eBPF program counterpart is in
 * 		*samples/bpf/trace_output_kern.c*).
 *
 * 		**bpf_perf_event_output**\ () achieves better performance
 * 		than **bpf_trace_printk**\ () for sharing data with user
 * 		space, and is much better suitable for streaming data from eBPF
 * 		programs.
 *
 * 		Note that this helper is not restricted to tracing use cases
 * 		and can be used with programs attached to TC or XDP as well,
 * 		where it allows for passing data to user space listeners. Data
 * 		can be:
 *
 * 		* Only custom structs,
 * 		* Only the packet payload, or
 * 		* A combination of both.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_load_bytes(const void *skb, u32 offset, void *to, u32 len)
 * 	Description
 * 		This helper was provided as an easy way to load data from a
 * 		packet. It can be used to load *len* bytes from *offset* from
 * 		the packet associated to *skb*, into the buffer pointed by
 * 		*to*.
 *
 * 		Since Linux 4.7, usage of this helper has mostly been replaced
 * 		by "direct packet access", enabling packet data to be
 * 		manipulated with *skb*\ **->data** and *skb*\ **->data_end**
 * 		pointing respectively to the first byte of packet data and to
 * 		the byte after the last byte of packet data. However, it
 * 		remains useful if one wishes to read large quantities of data
 * 		at once from a packet into the eBPF stack.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_get_stackid(void *ctx, struct bpf_map *map, u64 flags)
 * 	Description
 * 		Walk a user or a kernel stack and return its id. To achieve
 * 		this, the helper needs *ctx*, which is a pointer to the context
 * 		on which the tracing program is executed, and a pointer to a
 * 		*map* of type **BPF_MAP_TYPE_STACK_TRACE**.
 *
 * 		The last argument, *flags*, holds the number of stack frames to
 * 		skip (from 0 to 255), masked with
 * 		**BPF_F_SKIP_FIELD_MASK**. The next bits can be used to set
 * 		a combination of the following flags:
 *
 * 		**BPF_F_USER_STACK**
 * 			Collect a user space stack instead of a kernel stack.
 * 		**BPF_F_FAST_STACK_CMP**
 * 			Compare stacks by hash only.
 * 		**BPF_F_REUSE_STACKID**
 * 			If two different stacks hash into the same *stackid*,
 * 			discard the old one.
 *
 * 		The stack id retrieved is a 32 bit long integer handle which
 * 		can be further combined with other data (including other stack
 * 		ids) and used as a key into maps. This can be useful for
 * 		generating a variety of graphs (such as flame graphs or off-cpu
 * 		graphs).
 *
 * 		For walking a stack, this helper is an improvement over
 * 		**bpf_probe_read**\ (), which can be used with unrolled loops
 * 		but is not efficient and consumes a lot of eBPF instructions.
 * 		Instead, **bpf_get_stackid**\ () can collect up to
 * 		**PERF_MAX_STACK_DEPTH** both kernel and user frames. Note that
 * 		this limit can be controlled with the **sysctl** program, and
 * 		that it should be manually increased in order to profile long
 * 		user stacks (such as stacks for Java programs). To do so, use:
 *
 * 		::
 *
 * 			# sysctl kernel.perf_event_max_stack=<new value>
 * 	Return
 * 		The positive or null stack id on success, or a negative error
 * 		in case of failure.
 *
 * s64 bpf_csum_diff(__be32 *from, u32 from_size, __be32 *to, u32 to_size, __wsum seed)
 * 	Description
 * 		Compute a checksum difference, from the raw buffer pointed by
 * 		*from*, of length *from_size* (that must be a multiple of 4),
 * 		towards the raw buffer pointed by *to*, of size *to_size*
 * 		(same remark). An optional *seed* can be added to the value
 * 		(this can be cascaded, the seed may come from a previous call
 * 		to the helper).
 *
 * 		This is flexible enough to be used in several ways:
 *
 * 		* With *from_size* == 0, *to_size* > 0 and *seed* set to
 * 		  checksum, it can be used when pushing new data.
 * 		* With *from_size* > 0, *to_size* == 0 and *seed* set to
 * 		  checksum, it can be used when removing data from a packet.
 * 		* With *from_size* > 0, *to_size* > 0 and *seed* set to 0, it
 * 		  can be used to compute a diff. Note that *from_size* and
 * 		  *to_size* do not need to be equal.
 *
 * 		This helper can be used in combination with
 * 		**bpf_l3_csum_replace**\ () and **bpf_l4_csum_replace**\ (), to
 * 		which one can feed in the difference computed with
 * 		**bpf_csum_diff**\ ().
 * 	Return
 * 		The checksum result, or a negative error code in case of
 * 		failure.
 *
 * long bpf_skb_get_tunnel_opt(struct sk_buff *skb, void *opt, u32 size)
 * 	Description
 * 		Retrieve tunnel options metadata for the packet associated to
 * 		*skb*, and store the raw tunnel option data to the buffer *opt*
 * 		of *size*.
 *
 * 		This helper can be used with encapsulation devices that can
 * 		operate in "collect metadata" mode (please refer to the related
 * 		note in the description of **bpf_skb_get_tunnel_key**\ () for
 * 		more details). A particular example where this can be used is
 * 		in combination with the Geneve encapsulation protocol, where it
 * 		allows for pushing (with **bpf_skb_get_tunnel_opt**\ () helper)
 * 		and retrieving arbitrary TLVs (Type-Length-Value headers) from
 * 		the eBPF program. This allows for full customization of these
 * 		headers.
 * 	Return
 * 		The size of the option data retrieved.
 *
 * long bpf_skb_set_tunnel_opt(struct sk_buff *skb, void *opt, u32 size)
 * 	Description
 * 		Set tunnel options metadata for the packet associated to *skb*
 * 		to the option data contained in the raw buffer *opt* of *size*.
 *
 * 		See also the description of the **bpf_skb_get_tunnel_opt**\ ()
 * 		helper for additional information.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_change_proto(struct sk_buff *skb, __be16 proto, u64 flags)
 * 	Description
 * 		Change the protocol of the *skb* to *proto*. Currently
 * 		supported are transition from IPv4 to IPv6, and from IPv6 to
 * 		IPv4. The helper takes care of the groundwork for the
 * 		transition, including resizing the socket buffer. The eBPF
 * 		program is expected to fill the new headers, if any, via
 * 		**skb_store_bytes**\ () and to recompute the checksums with
 * 		**bpf_l3_csum_replace**\ () and **bpf_l4_csum_replace**\
 * 		(). The main case for this helper is to perform NAT64
 * 		operations out of an eBPF program.
 *
 * 		Internally, the GSO type is marked as dodgy so that headers are
 * 		checked and segments are recalculated by the GSO/GRO engine.
 * 		The size for GSO target is adapted as well.
 *
 * 		All values for *flags* are reserved for future usage, and must
 * 		be left at zero.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_change_type(struct sk_buff *skb, u32 type)
 * 	Description
 * 		Change the packet type for the packet associated to *skb*. This
 * 		comes down to setting *skb*\ **->pkt_type** to *type*, except
 * 		the eBPF program does not have a write access to *skb*\
 * 		**->pkt_type** beside this helper. Using a helper here allows
 * 		for graceful handling of errors.
 *
 * 		The major use case is to change incoming *skb*s to
 * 		**PACKET_HOST** in a programmatic way instead of having to
 * 		recirculate via **redirect**\ (..., **BPF_F_INGRESS**), for
 * 		example.
 *
 * 		Note that *type* only allows certain values. At this time, they
 * 		are:
 *
 * 		**PACKET_HOST**
 * 			Packet is for us.
 * 		**PACKET_BROADCAST**
 * 			Send packet to all.
 * 		**PACKET_MULTICAST**
 * 			Send packet to group.
 * 		**PACKET_OTHERHOST**
 * 			Send packet to someone else.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_under_cgroup(struct sk_buff *skb, struct bpf_map *map, u32 index)
 * 	Description
 * 		Check whether *skb* is a descendant of the cgroup2 held by
 * 		*map* of type **BPF_MAP_TYPE_CGROUP_ARRAY**, at *index*.
 * 	Return
 * 		The return value depends on the result of the test, and can be:
 *
 * 		* 0, if the *skb* failed the cgroup2 descendant test.
 * 		* 1, if the *skb* succeeded the cgroup2 descendant test.
 * 		* A negative error code, if an error occurred.
 *
 * u32 bpf_get_hash_recalc(struct sk_buff *skb)
 * 	Description
 * 		Retrieve the hash of the packet, *skb*\ **->hash**. If it is
 * 		not set, in particular if the hash was cleared due to mangling,
 * 		recompute this hash. Later accesses to the hash can be done
 * 		directly with *skb*\ **->hash**.
 *
 * 		Calling **bpf_set_hash_invalid**\ (), changing a packet
 * 		prototype with **bpf_skb_change_proto**\ (), or calling
 * 		**bpf_skb_store_bytes**\ () with the
 * 		**BPF_F_INVALIDATE_HASH** are actions susceptible to clear
 * 		the hash and to trigger a new computation for the next call to
 * 		**bpf_get_hash_recalc**\ ().
 * 	Return
 * 		The 32-bit hash.
 *
 * u64 bpf_get_current_task(void)
 * 	Return
 * 		A pointer to the current task struct.
 *
 * long bpf_probe_write_user(void *dst, const void *src, u32 len)
 * 	Description
 * 		Attempt in a safe way to write *len* bytes from the buffer
 * 		*src* to *dst* in memory. It only works for threads that are in
 * 		user context, and *dst* must be a valid user space address.
 *
 * 		This helper should not be used to implement any kind of
 * 		security mechanism because of TOC-TOU attacks, but rather to
 * 		debug, divert, and manipulate execution of semi-cooperative
 * 		processes.
 *
 * 		Keep in mind that this feature is meant for experiments, and it
 * 		has a risk of crashing the system and running programs.
 * 		Therefore, when an eBPF program using this helper is attached,
 * 		a warning including PID and process name is printed to kernel
 * 		logs.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_current_task_under_cgroup(struct bpf_map *map, u32 index)
 * 	Description
 * 		Check whether the probe is being run is the context of a given
 * 		subset of the cgroup2 hierarchy. The cgroup2 to test is held by
 * 		*map* of type **BPF_MAP_TYPE_CGROUP_ARRAY**, at *index*.
 * 	Return
 * 		The return value depends on the result of the test, and can be:
 *
 *		* 0, if current task belongs to the cgroup2.
 *		* 1, if current task does not belong to the cgroup2.
 * 		* A negative error code, if an error occurred.
 *
 * long bpf_skb_change_tail(struct sk_buff *skb, u32 len, u64 flags)
 * 	Description
 * 		Resize (trim or grow) the packet associated to *skb* to the
 * 		new *len*. The *flags* are reserved for future usage, and must
 * 		be left at zero.
 *
 * 		The basic idea is that the helper performs the needed work to
 * 		change the size of the packet, then the eBPF program rewrites
 * 		the rest via helpers like **bpf_skb_store_bytes**\ (),
 * 		**bpf_l3_csum_replace**\ (), **bpf_l3_csum_replace**\ ()
 * 		and others. This helper is a slow path utility intended for
 * 		replies with control messages. And because it is targeted for
 * 		slow path, the helper itself can afford to be slow: it
 * 		implicitly linearizes, unclones and drops offloads from the
 * 		*skb*.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_pull_data(struct sk_buff *skb, u32 len)
 * 	Description
 * 		Pull in non-linear data in case the *skb* is non-linear and not
 * 		all of *len* are part of the linear section. Make *len* bytes
 * 		from *skb* readable and writable. If a zero value is passed for
 * 		*len*, then the whole length of the *skb* is pulled.
 *
 * 		This helper is only needed for reading and writing with direct
 * 		packet access.
 *
 * 		For direct packet access, testing that offsets to access
 * 		are within packet boundaries (test on *skb*\ **->data_end**) is
 * 		susceptible to fail if offsets are invalid, or if the requested
 * 		data is in non-linear parts of the *skb*. On failure the
 * 		program can just bail out, or in the case of a non-linear
 * 		buffer, use a helper to make the data available. The
 * 		**bpf_skb_load_bytes**\ () helper is a first solution to access
 * 		the data. Another one consists in using **bpf_skb_pull_data**
 * 		to pull in once the non-linear parts, then retesting and
 * 		eventually access the data.
 *
 * 		At the same time, this also makes sure the *skb* is uncloned,
 * 		which is a necessary condition for direct write. As this needs
 * 		to be an invariant for the write part only, the verifier
 * 		detects writes and adds a prologue that is calling
 * 		**bpf_skb_pull_data()** to effectively unclone the *skb* from
 * 		the very beginning in case it is indeed cloned.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * s64 bpf_csum_update(struct sk_buff *skb, __wsum csum)
 * 	Description
 * 		Add the checksum *csum* into *skb*\ **->csum** in case the
 * 		driver has supplied a checksum for the entire packet into that
 * 		field. Return an error otherwise. This helper is intended to be
 * 		used in combination with **bpf_csum_diff**\ (), in particular
 * 		when the checksum needs to be updated after data has been
 * 		written into the packet through direct packet access.
 * 	Return
 * 		The checksum on success, or a negative error code in case of
 * 		failure.
 *
 * void bpf_set_hash_invalid(struct sk_buff *skb)
 * 	Description
 * 		Invalidate the current *skb*\ **->hash**. It can be used after
 * 		mangling on headers through direct packet access, in order to
 * 		indicate that the hash is outdated and to trigger a
 * 		recalculation the next time the kernel tries to access this
 * 		hash or when the **bpf_get_hash_recalc**\ () helper is called.
 *
 * long bpf_get_numa_node_id(void)
 * 	Description
 * 		Return the id of the current NUMA node. The primary use case
 * 		for this helper is the selection of sockets for the local NUMA
 * 		node, when the program is attached to sockets using the
 * 		**SO_ATTACH_REUSEPORT_EBPF** option (see also **socket(7)**),
 * 		but the helper is also available to other eBPF program types,
 * 		similarly to **bpf_get_smp_processor_id**\ ().
 * 	Return
 * 		The id of current NUMA node.
 *
 * long bpf_skb_change_head(struct sk_buff *skb, u32 len, u64 flags)
 * 	Description
 * 		Grows headroom of packet associated to *skb* and adjusts the
 * 		offset of the MAC header accordingly, adding *len* bytes of
 * 		space. It automatically extends and reallocates memory as
 * 		required.
 *
 * 		This helper can be used on a layer 3 *skb* to push a MAC header
 * 		for redirection into a layer 2 device.
 *
 * 		All values for *flags* are reserved for future usage, and must
 * 		be left at zero.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_xdp_adjust_head(struct xdp_buff *xdp_md, int delta)
 * 	Description
 * 		Adjust (move) *xdp_md*\ **->data** by *delta* bytes. Note that
 * 		it is possible to use a negative value for *delta*. This helper
 * 		can be used to prepare the packet for pushing or popping
 * 		headers.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_probe_read_str(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		Copy a NUL terminated string from an unsafe kernel address
 * 		*unsafe_ptr* to *dst*. See **bpf_probe_read_kernel_str**\ () for
 * 		more details.
 *
 * 		Generally, use **bpf_probe_read_user_str**\ () or
 * 		**bpf_probe_read_kernel_str**\ () instead.
 * 	Return
 * 		On success, the strictly positive length of the string,
 * 		including the trailing NUL character. On error, a negative
 * 		value.
 *
 * u64 bpf_get_socket_cookie(struct sk_buff *skb)
 * 	Description
 * 		If the **struct sk_buff** pointed by *skb* has a known socket,
 * 		retrieve the cookie (generated by the kernel) of this socket.
 * 		If no cookie has been set yet, generate a new cookie. Once
 * 		generated, the socket cookie remains stable for the life of the
 * 		socket. This helper can be useful for monitoring per socket
 * 		networking traffic statistics as it provides a global socket
 * 		identifier that can be assumed unique.
 * 	Return
 * 		A 8-byte long non-decreasing number on success, or 0 if the
 * 		socket field is missing inside *skb*.
 *
 * u64 bpf_get_socket_cookie(struct bpf_sock_addr *ctx)
 * 	Description
 * 		Equivalent to bpf_get_socket_cookie() helper that accepts
 * 		*skb*, but gets socket from **struct bpf_sock_addr** context.
 * 	Return
 * 		A 8-byte long non-decreasing number.
 *
 * u64 bpf_get_socket_cookie(struct bpf_sock_ops *ctx)
 * 	Description
 * 		Equivalent to **bpf_get_socket_cookie**\ () helper that accepts
 * 		*skb*, but gets socket from **struct bpf_sock_ops** context.
 * 	Return
 * 		A 8-byte long non-decreasing number.
 *
 * u32 bpf_get_socket_uid(struct sk_buff *skb)
 * 	Return
 * 		The owner UID of the socket associated to *skb*. If the socket
 * 		is **NULL**, or if it is not a full socket (i.e. if it is a
 * 		time-wait or a request socket instead), **overflowuid** value
 * 		is returned (note that **overflowuid** might also be the actual
 * 		UID value for the socket).
 *
 * long bpf_set_hash(struct sk_buff *skb, u32 hash)
 * 	Description
 * 		Set the full hash for *skb* (set the field *skb*\ **->hash**)
 * 		to value *hash*.
 * 	Return
 * 		0
 *
 * long bpf_setsockopt(void *bpf_socket, int level, int optname, void *optval, int optlen)
 * 	Description
 * 		Emulate a call to **setsockopt()** on the socket associated to
 * 		*bpf_socket*, which must be a full socket. The *level* at
 * 		which the option resides and the name *optname* of the option
 * 		must be specified, see **setsockopt(2)** for more information.
 * 		The option value of length *optlen* is pointed by *optval*.
 *
 * 		*bpf_socket* should be one of the following:
 *
 * 		* **struct bpf_sock_ops** for **BPF_PROG_TYPE_SOCK_OPS**.
 * 		* **struct bpf_sock_addr** for **BPF_CGROUP_INET4_CONNECT**
 * 		  and **BPF_CGROUP_INET6_CONNECT**.
 *
 * 		This helper actually implements a subset of **setsockopt()**.
 * 		It supports the following *level*\ s:
 *
 * 		* **SOL_SOCKET**, which supports the following *optname*\ s:
 * 		  **SO_RCVBUF**, **SO_SNDBUF**, **SO_MAX_PACING_RATE**,
 * 		  **SO_PRIORITY**, **SO_RCVLOWAT**, **SO_MARK**,
 * 		  **SO_BINDTODEVICE**, **SO_KEEPALIVE**.
 * 		* **IPPROTO_TCP**, which supports the following *optname*\ s:
 * 		  **TCP_CONGESTION**, **TCP_BPF_IW**,
 * 		  **TCP_BPF_SNDCWND_CLAMP**, **TCP_SAVE_SYN**,
 * 		  **TCP_KEEPIDLE**, **TCP_KEEPINTVL**, **TCP_KEEPCNT**,
 * 		  **TCP_SYNCNT**, **TCP_USER_TIMEOUT**.
 * 		* **IPPROTO_IP**, which supports *optname* **IP_TOS**.
 * 		* **IPPROTO_IPV6**, which supports *optname* **IPV6_TCLASS**.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_adjust_room(struct sk_buff *skb, s32 len_diff, u32 mode, u64 flags)
 * 	Description
 * 		Grow or shrink the room for data in the packet associated to
 * 		*skb* by *len_diff*, and according to the selected *mode*.
 *
 * 		By default, the helper will reset any offloaded checksum
 * 		indicator of the skb to CHECKSUM_NONE. This can be avoided
 * 		by the following flag:
 *
 * 		* **BPF_F_ADJ_ROOM_NO_CSUM_RESET**: Do not reset offloaded
 * 		  checksum data of the skb to CHECKSUM_NONE.
 *
 *		There are two supported modes at this time:
 *
 *		* **BPF_ADJ_ROOM_MAC**: Adjust room at the mac layer
 *		  (room space is added or removed below the layer 2 header).
 *
 * 		* **BPF_ADJ_ROOM_NET**: Adjust room at the network layer
 * 		  (room space is added or removed below the layer 3 header).
 *
 *		The following flags are supported at this time:
 *
 *		* **BPF_F_ADJ_ROOM_FIXED_GSO**: Do not adjust gso_size.
 *		  Adjusting mss in this way is not allowed for datagrams.
 *
 *		* **BPF_F_ADJ_ROOM_ENCAP_L3_IPV4**,
 *		  **BPF_F_ADJ_ROOM_ENCAP_L3_IPV6**:
 *		  Any new space is reserved to hold a tunnel header.
 *		  Configure skb offsets and other fields accordingly.
 *
 *		* **BPF_F_ADJ_ROOM_ENCAP_L4_GRE**,
 *		  **BPF_F_ADJ_ROOM_ENCAP_L4_UDP**:
 *		  Use with ENCAP_L3 flags to further specify the tunnel type.
 *
 *		* **BPF_F_ADJ_ROOM_ENCAP_L2**\ (*len*):
 *		  Use with ENCAP_L3/L4 flags to further specify the tunnel
 *		  type; *len* is the length of the inner MAC header.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_redirect_map(struct bpf_map *map, u32 key, u64 flags)
 * 	Description
 * 		Redirect the packet to the endpoint referenced by *map* at
 * 		index *key*. Depending on its type, this *map* can contain
 * 		references to net devices (for forwarding packets through other
 * 		ports), or to CPUs (for redirecting XDP frames to another CPU;
 * 		but this is only implemented for native XDP (with driver
 * 		support) as of this writing).
 *
 * 		The lower two bits of *flags* are used as the return code if
 * 		the map lookup fails. This is so that the return value can be
 * 		one of the XDP program return codes up to **XDP_TX**, as chosen
 * 		by the caller. Any higher bits in the *flags* argument must be
 * 		unset.
 *
 * 		See also **bpf_redirect**\ (), which only supports redirecting
 * 		to an ifindex, but doesn't require a map to do so.
 * 	Return
 * 		**XDP_REDIRECT** on success, or the value of the two lower bits
 * 		of the *flags* argument on error.
 *
 * long bpf_sk_redirect_map(struct sk_buff *skb, struct bpf_map *map, u32 key, u64 flags)
 * 	Description
 * 		Redirect the packet to the socket referenced by *map* (of type
 * 		**BPF_MAP_TYPE_SOCKMAP**) at index *key*. Both ingress and
 * 		egress interfaces can be used for redirection. The
 * 		**BPF_F_INGRESS** value in *flags* is used to make the
 * 		distinction (ingress path is selected if the flag is present,
 * 		egress path otherwise). This is the only flag supported for now.
 * 	Return
 * 		**SK_PASS** on success, or **SK_DROP** on error.
 *
 * long bpf_sock_map_update(struct bpf_sock_ops *skops, struct bpf_map *map, void *key, u64 flags)
 * 	Description
 * 		Add an entry to, or update a *map* referencing sockets. The
 * 		*skops* is used as a new value for the entry associated to
 * 		*key*. *flags* is one of:
 *
 * 		**BPF_NOEXIST**
 * 			The entry for *key* must not exist in the map.
 * 		**BPF_EXIST**
 * 			The entry for *key* must already exist in the map.
 * 		**BPF_ANY**
 * 			No condition on the existence of the entry for *key*.
 *
 * 		If the *map* has eBPF programs (parser and verdict), those will
 * 		be inherited by the socket being added. If the socket is
 * 		already attached to eBPF programs, this results in an error.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_xdp_adjust_meta(struct xdp_buff *xdp_md, int delta)
 * 	Description
 * 		Adjust the address pointed by *xdp_md*\ **->data_meta** by
 * 		*delta* (which can be positive or negative). Note that this
 * 		operation modifies the address stored in *xdp_md*\ **->data**,
 * 		so the latter must be loaded only after the helper has been
 * 		called.
 *
 * 		The use of *xdp_md*\ **->data_meta** is optional and programs
 * 		are not required to use it. The rationale is that when the
 * 		packet is processed with XDP (e.g. as DoS filter), it is
 * 		possible to push further meta data along with it before passing
 * 		to the stack, and to give the guarantee that an ingress eBPF
 * 		program attached as a TC classifier on the same device can pick
 * 		this up for further post-processing. Since TC works with socket
 * 		buffers, it remains possible to set from XDP the **mark** or
 * 		**priority** pointers, or other pointers for the socket buffer.
 * 		Having this scratch space generic and programmable allows for
 * 		more flexibility as the user is free to store whatever meta
 * 		data they need.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_perf_event_read_value(struct bpf_map *map, u64 flags, struct bpf_perf_event_value *buf, u32 buf_size)
 * 	Description
 * 		Read the value of a perf event counter, and store it into *buf*
 * 		of size *buf_size*. This helper relies on a *map* of type
 * 		**BPF_MAP_TYPE_PERF_EVENT_ARRAY**. The nature of the perf event
 * 		counter is selected when *map* is updated with perf event file
 * 		descriptors. The *map* is an array whose size is the number of
 * 		available CPUs, and each cell contains a value relative to one
 * 		CPU. The value to retrieve is indicated by *flags*, that
 * 		contains the index of the CPU to look up, masked with
 * 		**BPF_F_INDEX_MASK**. Alternatively, *flags* can be set to
 * 		**BPF_F_CURRENT_CPU** to indicate that the value for the
 * 		current CPU should be retrieved.
 *
 * 		This helper behaves in a way close to
 * 		**bpf_perf_event_read**\ () helper, save that instead of
 * 		just returning the value observed, it fills the *buf*
 * 		structure. This allows for additional data to be retrieved: in
 * 		particular, the enabled and running times (in *buf*\
 * 		**->enabled** and *buf*\ **->running**, respectively) are
 * 		copied. In general, **bpf_perf_event_read_value**\ () is
 * 		recommended over **bpf_perf_event_read**\ (), which has some
 * 		ABI issues and provides fewer functionalities.
 *
 * 		These values are interesting, because hardware PMU (Performance
 * 		Monitoring Unit) counters are limited resources. When there are
 * 		more PMU based perf events opened than available counters,
 * 		kernel will multiplex these events so each event gets certain
 * 		percentage (but not all) of the PMU time. In case that
 * 		multiplexing happens, the number of samples or counter value
 * 		will not reflect the case compared to when no multiplexing
 * 		occurs. This makes comparison between different runs difficult.
 * 		Typically, the counter value should be normalized before
 * 		comparing to other experiments. The usual normalization is done
 * 		as follows.
 *
 * 		::
 *
 * 			normalized_counter = counter * t_enabled / t_running
 *
 * 		Where t_enabled is the time enabled for event and t_running is
 * 		the time running for event since last normalization. The
 * 		enabled and running times are accumulated since the perf event
 * 		open. To achieve scaling factor between two invocations of an
 * 		eBPF program, users can use CPU id as the key (which is
 * 		typical for perf array usage model) to remember the previous
 * 		value and do the calculation inside the eBPF program.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_perf_prog_read_value(struct bpf_perf_event_data *ctx, struct bpf_perf_event_value *buf, u32 buf_size)
 * 	Description
 * 		For en eBPF program attached to a perf event, retrieve the
 * 		value of the event counter associated to *ctx* and store it in
 * 		the structure pointed by *buf* and of size *buf_size*. Enabled
 * 		and running times are also stored in the structure (see
 * 		description of helper **bpf_perf_event_read_value**\ () for
 * 		more details).
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_getsockopt(void *bpf_socket, int level, int optname, void *optval, int optlen)
 * 	Description
 * 		Emulate a call to **getsockopt()** on the socket associated to
 * 		*bpf_socket*, which must be a full socket. The *level* at
 * 		which the option resides and the name *optname* of the option
 * 		must be specified, see **getsockopt(2)** for more information.
 * 		The retrieved value is stored in the structure pointed by
 * 		*opval* and of length *optlen*.
 *
 * 		*bpf_socket* should be one of the following:
 *
 * 		* **struct bpf_sock_ops** for **BPF_PROG_TYPE_SOCK_OPS**.
 * 		* **struct bpf_sock_addr** for **BPF_CGROUP_INET4_CONNECT**
 * 		  and **BPF_CGROUP_INET6_CONNECT**.
 *
 * 		This helper actually implements a subset of **getsockopt()**.
 * 		It supports the following *level*\ s:
 *
 * 		* **IPPROTO_TCP**, which supports *optname*
 * 		  **TCP_CONGESTION**.
 * 		* **IPPROTO_IP**, which supports *optname* **IP_TOS**.
 * 		* **IPPROTO_IPV6**, which supports *optname* **IPV6_TCLASS**.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_override_return(struct pt_regs *regs, u64 rc)
 * 	Description
 * 		Used for error injection, this helper uses kprobes to override
 * 		the return value of the probed function, and to set it to *rc*.
 * 		The first argument is the context *regs* on which the kprobe
 * 		works.
 *
 * 		This helper works by setting the PC (program counter)
 * 		to an override function which is run in place of the original
 * 		probed function. This means the probed function is not run at
 * 		all. The replacement function just returns with the required
 * 		value.
 *
 * 		This helper has security implications, and thus is subject to
 * 		restrictions. It is only available if the kernel was compiled
 * 		with the **CONFIG_BPF_KPROBE_OVERRIDE** configuration
 * 		option, and in this case it only works on functions tagged with
 * 		**ALLOW_ERROR_INJECTION** in the kernel code.
 *
 * 		Also, the helper is only available for the architectures having
 * 		the CONFIG_FUNCTION_ERROR_INJECTION option. As of this writing,
 * 		x86 architecture is the only one to support this feature.
 * 	Return
 * 		0
 *
 * long bpf_sock_ops_cb_flags_set(struct bpf_sock_ops *bpf_sock, int argval)
 * 	Description
 * 		Attempt to set the value of the **bpf_sock_ops_cb_flags** field
 * 		for the full TCP socket associated to *bpf_sock_ops* to
 * 		*argval*.
 *
 * 		The primary use of this field is to determine if there should
 * 		be calls to eBPF programs of type
 * 		**BPF_PROG_TYPE_SOCK_OPS** at various points in the TCP
 * 		code. A program of the same type can change its value, per
 * 		connection and as necessary, when the connection is
 * 		established. This field is directly accessible for reading, but
 * 		this helper must be used for updates in order to return an
 * 		error if an eBPF program tries to set a callback that is not
 * 		supported in the current kernel.
 *
 * 		*argval* is a flag array which can combine these flags:
 *
 * 		* **BPF_SOCK_OPS_RTO_CB_FLAG** (retransmission time out)
 * 		* **BPF_SOCK_OPS_RETRANS_CB_FLAG** (retransmission)
 * 		* **BPF_SOCK_OPS_STATE_CB_FLAG** (TCP state change)
 * 		* **BPF_SOCK_OPS_RTT_CB_FLAG** (every RTT)
 *
 * 		Therefore, this function can be used to clear a callback flag by
 * 		setting the appropriate bit to zero. e.g. to disable the RTO
 * 		callback:
 *
 * 		**bpf_sock_ops_cb_flags_set(bpf_sock,**
 * 			**bpf_sock->bpf_sock_ops_cb_flags & ~BPF_SOCK_OPS_RTO_CB_FLAG)**
 *
 * 		Here are some examples of where one could call such eBPF
 * 		program:
 *
 * 		* When RTO fires.
 * 		* When a packet is retransmitted.
 * 		* When the connection terminates.
 * 		* When a packet is sent.
 * 		* When a packet is received.
 * 	Return
 * 		Code **-EINVAL** if the socket is not a full TCP socket;
 * 		otherwise, a positive number containing the bits that could not
 * 		be set is returned (which comes down to 0 if all bits were set
 * 		as required).
 *
 * long bpf_msg_redirect_map(struct sk_msg_buff *msg, struct bpf_map *map, u32 key, u64 flags)
 * 	Description
 * 		This helper is used in programs implementing policies at the
 * 		socket level. If the message *msg* is allowed to pass (i.e. if
 * 		the verdict eBPF program returns **SK_PASS**), redirect it to
 * 		the socket referenced by *map* (of type
 * 		**BPF_MAP_TYPE_SOCKMAP**) at index *key*. Both ingress and
 * 		egress interfaces can be used for redirection. The
 * 		**BPF_F_INGRESS** value in *flags* is used to make the
 * 		distinction (ingress path is selected if the flag is present,
 * 		egress path otherwise). This is the only flag supported for now.
 * 	Return
 * 		**SK_PASS** on success, or **SK_DROP** on error.
 *
 * long bpf_msg_apply_bytes(struct sk_msg_buff *msg, u32 bytes)
 * 	Description
 * 		For socket policies, apply the verdict of the eBPF program to
 * 		the next *bytes* (number of bytes) of message *msg*.
 *
 * 		For example, this helper can be used in the following cases:
 *
 * 		* A single **sendmsg**\ () or **sendfile**\ () system call
 * 		  contains multiple logical messages that the eBPF program is
 * 		  supposed to read and for which it should apply a verdict.
 * 		* An eBPF program only cares to read the first *bytes* of a
 * 		  *msg*. If the message has a large payload, then setting up
 * 		  and calling the eBPF program repeatedly for all bytes, even
 * 		  though the verdict is already known, would create unnecessary
 * 		  overhead.
 *
 * 		When called from within an eBPF program, the helper sets a
 * 		counter internal to the BPF infrastructure, that is used to
 * 		apply the last verdict to the next *bytes*. If *bytes* is
 * 		smaller than the current data being processed from a
 * 		**sendmsg**\ () or **sendfile**\ () system call, the first
 * 		*bytes* will be sent and the eBPF program will be re-run with
 * 		the pointer for start of data pointing to byte number *bytes*
 * 		**+ 1**. If *bytes* is larger than the current data being
 * 		processed, then the eBPF verdict will be applied to multiple
 * 		**sendmsg**\ () or **sendfile**\ () calls until *bytes* are
 * 		consumed.
 *
 * 		Note that if a socket closes with the internal counter holding
 * 		a non-zero value, this is not a problem because data is not
 * 		being buffered for *bytes* and is sent as it is received.
 * 	Return
 * 		0
 *
 * long bpf_msg_cork_bytes(struct sk_msg_buff *msg, u32 bytes)
 * 	Description
 * 		For socket policies, prevent the execution of the verdict eBPF
 * 		program for message *msg* until *bytes* (byte number) have been
 * 		accumulated.
 *
 * 		This can be used when one needs a specific number of bytes
 * 		before a verdict can be assigned, even if the data spans
 * 		multiple **sendmsg**\ () or **sendfile**\ () calls. The extreme
 * 		case would be a user calling **sendmsg**\ () repeatedly with
 * 		1-byte long message segments. Obviously, this is bad for
 * 		performance, but it is still valid. If the eBPF program needs
 * 		*bytes* bytes to validate a header, this helper can be used to
 * 		prevent the eBPF program to be called again until *bytes* have
 * 		been accumulated.
 * 	Return
 * 		0
 *
 * long bpf_msg_pull_data(struct sk_msg_buff *msg, u32 start, u32 end, u64 flags)
 * 	Description
 * 		For socket policies, pull in non-linear data from user space
 * 		for *msg* and set pointers *msg*\ **->data** and *msg*\
 * 		**->data_end** to *start* and *end* bytes offsets into *msg*,
 * 		respectively.
 *
 * 		If a program of type **BPF_PROG_TYPE_SK_MSG** is run on a
 * 		*msg* it can only parse data that the (**data**, **data_end**)
 * 		pointers have already consumed. For **sendmsg**\ () hooks this
 * 		is likely the first scatterlist element. But for calls relying
 * 		on the **sendpage** handler (e.g. **sendfile**\ ()) this will
 * 		be the range (**0**, **0**) because the data is shared with
 * 		user space and by default the objective is to avoid allowing
 * 		user space to modify data while (or after) eBPF verdict is
 * 		being decided. This helper can be used to pull in data and to
 * 		set the start and end pointer to given values. Data will be
 * 		copied if necessary (i.e. if data was not linear and if start
 * 		and end pointers do not point to the same chunk).
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 *
 * 		All values for *flags* are reserved for future usage, and must
 * 		be left at zero.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_bind(struct bpf_sock_addr *ctx, struct sockaddr *addr, int addr_len)
 * 	Description
 * 		Bind the socket associated to *ctx* to the address pointed by
 * 		*addr*, of length *addr_len*. This allows for making outgoing
 * 		connection from the desired IP address, which can be useful for
 * 		example when all processes inside a cgroup should use one
 * 		single IP address on a host that has multiple IP configured.
 *
 * 		This helper works for IPv4 and IPv6, TCP and UDP sockets. The
 * 		domain (*addr*\ **->sa_family**) must be **AF_INET** (or
 * 		**AF_INET6**). It's advised to pass zero port (**sin_port**
 * 		or **sin6_port**) which triggers IP_BIND_ADDRESS_NO_PORT-like
 * 		behavior and lets the kernel efficiently pick up an unused
 * 		port as long as 4-tuple is unique. Passing non-zero port might
 * 		lead to degraded performance.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_xdp_adjust_tail(struct xdp_buff *xdp_md, int delta)
 * 	Description
 * 		Adjust (move) *xdp_md*\ **->data_end** by *delta* bytes. It is
 * 		possible to both shrink and grow the packet tail.
 * 		Shrink done via *delta* being a negative integer.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_skb_get_xfrm_state(struct sk_buff *skb, u32 index, struct bpf_xfrm_state *xfrm_state, u32 size, u64 flags)
 * 	Description
 * 		Retrieve the XFRM state (IP transform framework, see also
 * 		**ip-xfrm(8)**) at *index* in XFRM "security path" for *skb*.
 *
 * 		The retrieved value is stored in the **struct bpf_xfrm_state**
 * 		pointed by *xfrm_state* and of length *size*.
 *
 * 		All values for *flags* are reserved for future usage, and must
 * 		be left at zero.
 *
 * 		This helper is available only if the kernel was compiled with
 * 		**CONFIG_XFRM** configuration option.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_get_stack(void *ctx, void *buf, u32 size, u64 flags)
 * 	Description
 * 		Return a user or a kernel stack in bpf program provided buffer.
 * 		To achieve this, the helper needs *ctx*, which is a pointer
 * 		to the context on which the tracing program is executed.
 * 		To store the stacktrace, the bpf program provides *buf* with
 * 		a nonnegative *size*.
 *
 * 		The last argument, *flags*, holds the number of stack frames to
 * 		skip (from 0 to 255), masked with
 * 		**BPF_F_SKIP_FIELD_MASK**. The next bits can be used to set
 * 		the following flags:
 *
 * 		**BPF_F_USER_STACK**
 * 			Collect a user space stack instead of a kernel stack.
 * 		**BPF_F_USER_BUILD_ID**
 * 			Collect buildid+offset instead of ips for user stack,
 * 			only valid if **BPF_F_USER_STACK** is also specified.
 *
 * 		**bpf_get_stack**\ () can collect up to
 * 		**PERF_MAX_STACK_DEPTH** both kernel and user frames, subject
 * 		to sufficient large buffer size. Note that
 * 		this limit can be controlled with the **sysctl** program, and
 * 		that it should be manually increased in order to profile long
 * 		user stacks (such as stacks for Java programs). To do so, use:
 *
 * 		::
 *
 * 			# sysctl kernel.perf_event_max_stack=<new value>
 * 	Return
 * 		A non-negative value equal to or less than *size* on success,
 * 		or a negative error in case of failure.
 *
 * long bpf_skb_load_bytes_relative(const void *skb, u32 offset, void *to, u32 len, u32 start_header)
 * 	Description
 * 		This helper is similar to **bpf_skb_load_bytes**\ () in that
 * 		it provides an easy way to load *len* bytes from *offset*
 * 		from the packet associated to *skb*, into the buffer pointed
 * 		by *to*. The difference to **bpf_skb_load_bytes**\ () is that
 * 		a fifth argument *start_header* exists in order to select a
 * 		base offset to start from. *start_header* can be one of:
 *
 * 		**BPF_HDR_START_MAC**
 * 			Base offset to load data from is *skb*'s mac header.
 * 		**BPF_HDR_START_NET**
 * 			Base offset to load data from is *skb*'s network header.
 *
 * 		In general, "direct packet access" is the preferred method to
 * 		access packet data, however, this helper is in particular useful
 * 		in socket filters where *skb*\ **->data** does not always point
 * 		to the start of the mac header and where "direct packet access"
 * 		is not available.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_fib_lookup(void *ctx, struct bpf_fib_lookup *params, int plen, u32 flags)
 *	Description
 *		Do FIB lookup in kernel tables using parameters in *params*.
 *		If lookup is successful and result shows packet is to be
 *		forwarded, the neighbor tables are searched for the nexthop.
 *		If successful (ie., FIB lookup shows forwarding and nexthop
 *		is resolved), the nexthop address is returned in ipv4_dst
 *		or ipv6_dst based on family, smac is set to mac address of
 *		egress device, dmac is set to nexthop mac address, rt_metric
 *		is set to metric from route (IPv4/IPv6 only), and ifindex
 *		is set to the device index of the nexthop from the FIB lookup.
 *
 *		*plen* argument is the size of the passed in struct.
 *		*flags* argument can be a combination of one or more of the
 *		following values:
 *
 *		**BPF_FIB_LOOKUP_DIRECT**
 *			Do a direct table lookup vs full lookup using FIB
 *			rules.
 *		**BPF_FIB_LOOKUP_OUTPUT**
 *			Perform lookup from an egress perspective (default is
 *			ingress).
 *
 *		*ctx* is either **struct xdp_md** for XDP programs or
 *		**struct sk_buff** tc cls_act programs.
 *	Return
 *		* < 0 if any input argument is invalid
 *		*   0 on success (packet is forwarded, nexthop neighbor exists)
 *		* > 0 one of **BPF_FIB_LKUP_RET_** codes explaining why the
 *		  packet is not forwarded or needs assist from full stack
 *
 * long bpf_sock_hash_update(struct bpf_sock_ops *skops, struct bpf_map *map, void *key, u64 flags)
 *	Description
 *		Add an entry to, or update a sockhash *map* referencing sockets.
 *		The *skops* is used as a new value for the entry associated to
 *		*key*. *flags* is one of:
 *
 *		**BPF_NOEXIST**
 *			The entry for *key* must not exist in the map.
 *		**BPF_EXIST**
 *			The entry for *key* must already exist in the map.
 *		**BPF_ANY**
 *			No condition on the existence of the entry for *key*.
 *
 *		If the *map* has eBPF programs (parser and verdict), those will
 *		be inherited by the socket being added. If the socket is
 *		already attached to eBPF programs, this results in an error.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_msg_redirect_hash(struct sk_msg_buff *msg, struct bpf_map *map, void *key, u64 flags)
 *	Description
 *		This helper is used in programs implementing policies at the
 *		socket level. If the message *msg* is allowed to pass (i.e. if
 *		the verdict eBPF program returns **SK_PASS**), redirect it to
 *		the socket referenced by *map* (of type
 *		**BPF_MAP_TYPE_SOCKHASH**) using hash *key*. Both ingress and
 *		egress interfaces can be used for redirection. The
 *		**BPF_F_INGRESS** value in *flags* is used to make the
 *		distinction (ingress path is selected if the flag is present,
 *		egress path otherwise). This is the only flag supported for now.
 *	Return
 *		**SK_PASS** on success, or **SK_DROP** on error.
 *
 * long bpf_sk_redirect_hash(struct sk_buff *skb, struct bpf_map *map, void *key, u64 flags)
 *	Description
 *		This helper is used in programs implementing policies at the
 *		skb socket level. If the sk_buff *skb* is allowed to pass (i.e.
 *		if the verdeict eBPF program returns **SK_PASS**), redirect it
 *		to the socket referenced by *map* (of type
 *		**BPF_MAP_TYPE_SOCKHASH**) using hash *key*. Both ingress and
 *		egress interfaces can be used for redirection. The
 *		**BPF_F_INGRESS** value in *flags* is used to make the
 *		distinction (ingress path is selected if the flag is present,
 *		egress otherwise). This is the only flag supported for now.
 *	Return
 *		**SK_PASS** on success, or **SK_DROP** on error.
 *
 * long bpf_lwt_push_encap(struct sk_buff *skb, u32 type, void *hdr, u32 len)
 *	Description
 *		Encapsulate the packet associated to *skb* within a Layer 3
 *		protocol header. This header is provided in the buffer at
 *		address *hdr*, with *len* its size in bytes. *type* indicates
 *		the protocol of the header and can be one of:
 *
 *		**BPF_LWT_ENCAP_SEG6**
 *			IPv6 encapsulation with Segment Routing Header
 *			(**struct ipv6_sr_hdr**). *hdr* only contains the SRH,
 *			the IPv6 header is computed by the kernel.
 *		**BPF_LWT_ENCAP_SEG6_INLINE**
 *			Only works if *skb* contains an IPv6 packet. Insert a
 *			Segment Routing Header (**struct ipv6_sr_hdr**) inside
 *			the IPv6 header.
 *		**BPF_LWT_ENCAP_IP**
 *			IP encapsulation (GRE/GUE/IPIP/etc). The outer header
 *			must be IPv4 or IPv6, followed by zero or more
 *			additional headers, up to **LWT_BPF_MAX_HEADROOM**
 *			total bytes in all prepended headers. Please note that
 *			if **skb_is_gso**\ (*skb*) is true, no more than two
 *			headers can be prepended, and the inner header, if
 *			present, should be either GRE or UDP/GUE.
 *
 *		**BPF_LWT_ENCAP_SEG6**\ \* types can be called by BPF programs
 *		of type **BPF_PROG_TYPE_LWT_IN**; **BPF_LWT_ENCAP_IP** type can
 *		be called by bpf programs of types **BPF_PROG_TYPE_LWT_IN** and
 *		**BPF_PROG_TYPE_LWT_XMIT**.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 *	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_lwt_seg6_store_bytes(struct sk_buff *skb, u32 offset, const void *from, u32 len)
 *	Description
 *		Store *len* bytes from address *from* into the packet
 *		associated to *skb*, at *offset*. Only the flags, tag and TLVs
 *		inside the outermost IPv6 Segment Routing Header can be
 *		modified through this helper.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 *	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_lwt_seg6_adjust_srh(struct sk_buff *skb, u32 offset, s32 delta)
 *	Description
 *		Adjust the size allocated to TLVs in the outermost IPv6
 *		Segment Routing Header contained in the packet associated to
 *		*skb*, at position *offset* by *delta* bytes. Only offsets
 *		after the segments are accepted. *delta* can be as well
 *		positive (growing) as negative (shrinking).
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 *	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_lwt_seg6_action(struct sk_buff *skb, u32 action, void *param, u32 param_len)
 *	Description
 *		Apply an IPv6 Segment Routing action of type *action* to the
 *		packet associated to *skb*. Each action takes a parameter
 *		contained at address *param*, and of length *param_len* bytes.
 *		*action* can be one of:
 *
 *		**SEG6_LOCAL_ACTION_END_X**
 *			End.X action: Endpoint with Layer-3 cross-connect.
 *			Type of *param*: **struct in6_addr**.
 *		**SEG6_LOCAL_ACTION_END_T**
 *			End.T action: Endpoint with specific IPv6 table lookup.
 *			Type of *param*: **int**.
 *		**SEG6_LOCAL_ACTION_END_B6**
 *			End.B6 action: Endpoint bound to an SRv6 policy.
 *			Type of *param*: **struct ipv6_sr_hdr**.
 *		**SEG6_LOCAL_ACTION_END_B6_ENCAP**
 *			End.B6.Encap action: Endpoint bound to an SRv6
 *			encapsulation policy.
 *			Type of *param*: **struct ipv6_sr_hdr**.
 *
 * 		A call to this helper is susceptible to change the underlying
 * 		packet buffer. Therefore, at load time, all checks on pointers
 * 		previously done by the verifier are invalidated and must be
 * 		performed again, if the helper is used in combination with
 * 		direct packet access.
 *	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_rc_repeat(void *ctx)
 *	Description
 *		This helper is used in programs implementing IR decoding, to
 *		report a successfully decoded repeat key message. This delays
 *		the generation of a key up event for previously generated
 *		key down event.
 *
 *		Some IR protocols like NEC have a special IR message for
 *		repeating last button, for when a button is held down.
 *
 *		The *ctx* should point to the lirc sample as passed into
 *		the program.
 *
 *		This helper is only available is the kernel was compiled with
 *		the **CONFIG_BPF_LIRC_MODE2** configuration option set to
 *		"**y**".
 *	Return
 *		0
 *
 * long bpf_rc_keydown(void *ctx, u32 protocol, u64 scancode, u32 toggle)
 *	Description
 *		This helper is used in programs implementing IR decoding, to
 *		report a successfully decoded key press with *scancode*,
 *		*toggle* value in the given *protocol*. The scancode will be
 *		translated to a keycode using the rc keymap, and reported as
 *		an input key down event. After a period a key up event is
 *		generated. This period can be extended by calling either
 *		**bpf_rc_keydown**\ () again with the same values, or calling
 *		**bpf_rc_repeat**\ ().
 *
 *		Some protocols include a toggle bit, in case the button was
 *		released and pressed again between consecutive scancodes.
 *
 *		The *ctx* should point to the lirc sample as passed into
 *		the program.
 *
 *		The *protocol* is the decoded protocol number (see
 *		**enum rc_proto** for some predefined values).
 *
 *		This helper is only available is the kernel was compiled with
 *		the **CONFIG_BPF_LIRC_MODE2** configuration option set to
 *		"**y**".
 *	Return
 *		0
 *
 * u64 bpf_skb_cgroup_id(struct sk_buff *skb)
 * 	Description
 * 		Return the cgroup v2 id of the socket associated with the *skb*.
 * 		This is roughly similar to the **bpf_get_cgroup_classid**\ ()
 * 		helper for cgroup v1 by providing a tag resp. identifier that
 * 		can be matched on or used for map lookups e.g. to implement
 * 		policy. The cgroup v2 id of a given path in the hierarchy is
 * 		exposed in user space through the f_handle API in order to get
 * 		to the same 64-bit id.
 *
 * 		This helper can be used on TC egress path, but not on ingress,
 * 		and is available only if the kernel was compiled with the
 * 		**CONFIG_SOCK_CGROUP_DATA** configuration option.
 * 	Return
 * 		The id is returned or 0 in case the id could not be retrieved.
 *
 * u64 bpf_get_current_cgroup_id(void)
 * 	Return
 * 		A 64-bit integer containing the current cgroup id based
 * 		on the cgroup within which the current task is running.
 *
 * void *bpf_get_local_storage(void *map, u64 flags)
 *	Description
 *		Get the pointer to the local storage area.
 *		The type and the size of the local storage is defined
 *		by the *map* argument.
 *		The *flags* meaning is specific for each map type,
 *		and has to be 0 for cgroup local storage.
 *
 *		Depending on the BPF program type, a local storage area
 *		can be shared between multiple instances of the BPF program,
 *		running simultaneously.
 *
 *		A user should care about the synchronization by himself.
 *		For example, by using the **BPF_STX_XADD** instruction to alter
 *		the shared data.
 *	Return
 *		A pointer to the local storage area.
 *
 * long bpf_sk_select_reuseport(struct sk_reuseport_md *reuse, struct bpf_map *map, void *key, u64 flags)
 *	Description
 *		Select a **SO_REUSEPORT** socket from a
 *		**BPF_MAP_TYPE_REUSEPORT_ARRAY** *map*.
 *		It checks the selected socket is matching the incoming
 *		request in the socket buffer.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * u64 bpf_skb_ancestor_cgroup_id(struct sk_buff *skb, int ancestor_level)
 *	Description
 *		Return id of cgroup v2 that is ancestor of cgroup associated
 *		with the *skb* at the *ancestor_level*.  The root cgroup is at
 *		*ancestor_level* zero and each step down the hierarchy
 *		increments the level. If *ancestor_level* == level of cgroup
 *		associated with *skb*, then return value will be same as that
 *		of **bpf_skb_cgroup_id**\ ().
 *
 *		The helper is useful to implement policies based on cgroups
 *		that are upper in hierarchy than immediate cgroup associated
 *		with *skb*.
 *
 *		The format of returned id and helper limitations are same as in
 *		**bpf_skb_cgroup_id**\ ().
 *	Return
 *		The id is returned or 0 in case the id could not be retrieved.
 *
 * struct bpf_sock *bpf_sk_lookup_tcp(void *ctx, struct bpf_sock_tuple *tuple, u32 tuple_size, u64 netns, u64 flags)
 *	Description
 *		Look for TCP socket matching *tuple*, optionally in a child
 *		network namespace *netns*. The return value must be checked,
 *		and if non-**NULL**, released via **bpf_sk_release**\ ().
 *
 *		The *ctx* should point to the context of the program, such as
 *		the skb or socket (depending on the hook in use). This is used
 *		to determine the base network namespace for the lookup.
 *
 *		*tuple_size* must be one of:
 *
 *		**sizeof**\ (*tuple*\ **->ipv4**)
 *			Look for an IPv4 socket.
 *		**sizeof**\ (*tuple*\ **->ipv6**)
 *			Look for an IPv6 socket.
 *
 *		If the *netns* is a negative signed 32-bit integer, then the
 *		socket lookup table in the netns associated with the *ctx*
 *		will be used. For the TC hooks, this is the netns of the device
 *		in the skb. For socket hooks, this is the netns of the socket.
 *		If *netns* is any other signed 32-bit value greater than or
 *		equal to zero then it specifies the ID of the netns relative to
 *		the netns associated with the *ctx*. *netns* values beyond the
 *		range of 32-bit integers are reserved for future use.
 *
 *		All values for *flags* are reserved for future usage, and must
 *		be left at zero.
 *
 *		This helper is available only if the kernel was compiled with
 *		**CONFIG_NET** configuration option.
 *	Return
 *		Pointer to **struct bpf_sock**, or **NULL** in case of failure.
 *		For sockets with reuseport option, the **struct bpf_sock**
 *		result is from *reuse*\ **->socks**\ [] using the hash of the
 *		tuple.
 *
 * struct bpf_sock *bpf_sk_lookup_udp(void *ctx, struct bpf_sock_tuple *tuple, u32 tuple_size, u64 netns, u64 flags)
 *	Description
 *		Look for UDP socket matching *tuple*, optionally in a child
 *		network namespace *netns*. The return value must be checked,
 *		and if non-**NULL**, released via **bpf_sk_release**\ ().
 *
 *		The *ctx* should point to the context of the program, such as
 *		the skb or socket (depending on the hook in use). This is used
 *		to determine the base network namespace for the lookup.
 *
 *		*tuple_size* must be one of:
 *
 *		**sizeof**\ (*tuple*\ **->ipv4**)
 *			Look for an IPv4 socket.
 *		**sizeof**\ (*tuple*\ **->ipv6**)
 *			Look for an IPv6 socket.
 *
 *		If the *netns* is a negative signed 32-bit integer, then the
 *		socket lookup table in the netns associated with the *ctx*
 *		will be used. For the TC hooks, this is the netns of the device
 *		in the skb. For socket hooks, this is the netns of the socket.
 *		If *netns* is any other signed 32-bit value greater than or
 *		equal to zero then it specifies the ID of the netns relative to
 *		the netns associated with the *ctx*. *netns* values beyond the
 *		range of 32-bit integers are reserved for future use.
 *
 *		All values for *flags* are reserved for future usage, and must
 *		be left at zero.
 *
 *		This helper is available only if the kernel was compiled with
 *		**CONFIG_NET** configuration option.
 *	Return
 *		Pointer to **struct bpf_sock**, or **NULL** in case of failure.
 *		For sockets with reuseport option, the **struct bpf_sock**
 *		result is from *reuse*\ **->socks**\ [] using the hash of the
 *		tuple.
 *
 * long bpf_sk_release(void *sock)
 *	Description
 *		Release the reference held by *sock*. *sock* must be a
 *		non-**NULL** pointer that was returned from
 *		**bpf_sk_lookup_xxx**\ ().
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_map_push_elem(struct bpf_map *map, const void *value, u64 flags)
 * 	Description
 * 		Push an element *value* in *map*. *flags* is one of:
 *
 * 		**BPF_EXIST**
 * 			If the queue/stack is full, the oldest element is
 * 			removed to make room for this.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_map_pop_elem(struct bpf_map *map, void *value)
 * 	Description
 * 		Pop an element from *map*.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_map_peek_elem(struct bpf_map *map, void *value)
 * 	Description
 * 		Get an element from *map* without removing it.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_msg_push_data(struct sk_msg_buff *msg, u32 start, u32 len, u64 flags)
 *	Description
 *		For socket policies, insert *len* bytes into *msg* at offset
 *		*start*.
 *
 *		If a program of type **BPF_PROG_TYPE_SK_MSG** is run on a
 *		*msg* it may want to insert metadata or options into the *msg*.
 *		This can later be read and used by any of the lower layer BPF
 *		hooks.
 *
 *		This helper may fail if under memory pressure (a malloc
 *		fails) in these cases BPF programs will get an appropriate
 *		error and BPF programs will need to handle them.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_msg_pop_data(struct sk_msg_buff *msg, u32 start, u32 len, u64 flags)
 *	Description
 *		Will remove *len* bytes from a *msg* starting at byte *start*.
 *		This may result in **ENOMEM** errors under certain situations if
 *		an allocation and copy are required due to a full ring buffer.
 *		However, the helper will try to avoid doing the allocation
 *		if possible. Other errors can occur if input parameters are
 *		invalid either due to *start* byte not being valid part of *msg*
 *		payload and/or *pop* value being to large.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_rc_pointer_rel(void *ctx, s32 rel_x, s32 rel_y)
 *	Description
 *		This helper is used in programs implementing IR decoding, to
 *		report a successfully decoded pointer movement.
 *
 *		The *ctx* should point to the lirc sample as passed into
 *		the program.
 *
 *		This helper is only available is the kernel was compiled with
 *		the **CONFIG_BPF_LIRC_MODE2** configuration option set to
 *		"**y**".
 *	Return
 *		0
 *
 * long bpf_spin_lock(struct bpf_spin_lock *lock)
 *	Description
 *		Acquire a spinlock represented by the pointer *lock*, which is
 *		stored as part of a value of a map. Taking the lock allows to
 *		safely update the rest of the fields in that value. The
 *		spinlock can (and must) later be released with a call to
 *		**bpf_spin_unlock**\ (\ *lock*\ ).
 *
 *		Spinlocks in BPF programs come with a number of restrictions
 *		and constraints:
 *
 *		* **bpf_spin_lock** objects are only allowed inside maps of
 *		  types **BPF_MAP_TYPE_HASH** and **BPF_MAP_TYPE_ARRAY** (this
 *		  list could be extended in the future).
 *		* BTF description of the map is mandatory.
 *		* The BPF program can take ONE lock at a time, since taking two
 *		  or more could cause dead locks.
 *		* Only one **struct bpf_spin_lock** is allowed per map element.
 *		* When the lock is taken, calls (either BPF to BPF or helpers)
 *		  are not allowed.
 *		* The **BPF_LD_ABS** and **BPF_LD_IND** instructions are not
 *		  allowed inside a spinlock-ed region.
 *		* The BPF program MUST call **bpf_spin_unlock**\ () to release
 *		  the lock, on all execution paths, before it returns.
 *		* The BPF program can access **struct bpf_spin_lock** only via
 *		  the **bpf_spin_lock**\ () and **bpf_spin_unlock**\ ()
 *		  helpers. Loading or storing data into the **struct
 *		  bpf_spin_lock** *lock*\ **;** field of a map is not allowed.
 *		* To use the **bpf_spin_lock**\ () helper, the BTF description
 *		  of the map value must be a struct and have **struct
 *		  bpf_spin_lock** *anyname*\ **;** field at the top level.
 *		  Nested lock inside another struct is not allowed.
 *		* The **struct bpf_spin_lock** *lock* field in a map value must
 *		  be aligned on a multiple of 4 bytes in that value.
 *		* Syscall with command **BPF_MAP_LOOKUP_ELEM** does not copy
 *		  the **bpf_spin_lock** field to user space.
 *		* Syscall with command **BPF_MAP_UPDATE_ELEM**, or update from
 *		  a BPF program, do not update the **bpf_spin_lock** field.
 *		* **bpf_spin_lock** cannot be on the stack or inside a
 *		  networking packet (it can only be inside of a map values).
 *		* **bpf_spin_lock** is available to root only.
 *		* Tracing programs and socket filter programs cannot use
 *		  **bpf_spin_lock**\ () due to insufficient preemption checks
 *		  (but this may change in the future).
 *		* **bpf_spin_lock** is not allowed in inner maps of map-in-map.
 *	Return
 *		0
 *
 * long bpf_spin_unlock(struct bpf_spin_lock *lock)
 *	Description
 *		Release the *lock* previously locked by a call to
 *		**bpf_spin_lock**\ (\ *lock*\ ).
 *	Return
 *		0
 *
 * struct bpf_sock *bpf_sk_fullsock(struct bpf_sock *sk)
 *	Description
 *		This helper gets a **struct bpf_sock** pointer such
 *		that all the fields in this **bpf_sock** can be accessed.
 *	Return
 *		A **struct bpf_sock** pointer on success, or **NULL** in
 *		case of failure.
 *
 * struct bpf_tcp_sock *bpf_tcp_sock(struct bpf_sock *sk)
 *	Description
 *		This helper gets a **struct bpf_tcp_sock** pointer from a
 *		**struct bpf_sock** pointer.
 *	Return
 *		A **struct bpf_tcp_sock** pointer on success, or **NULL** in
 *		case of failure.
 *
 * long bpf_skb_ecn_set_ce(struct sk_buff *skb)
 *	Description
 *		Set ECN (Explicit Congestion Notification) field of IP header
 *		to **CE** (Congestion Encountered) if current value is **ECT**
 *		(ECN Capable Transport). Otherwise, do nothing. Works with IPv6
 *		and IPv4.
 *	Return
 *		1 if the **CE** flag is set (either by the current helper call
 *		or because it was already present), 0 if it is not set.
 *
 * struct bpf_sock *bpf_get_listener_sock(struct bpf_sock *sk)
 *	Description
 *		Return a **struct bpf_sock** pointer in **TCP_LISTEN** state.
 *		**bpf_sk_release**\ () is unnecessary and not allowed.
 *	Return
 *		A **struct bpf_sock** pointer on success, or **NULL** in
 *		case of failure.
 *
 * struct bpf_sock *bpf_skc_lookup_tcp(void *ctx, struct bpf_sock_tuple *tuple, u32 tuple_size, u64 netns, u64 flags)
 *	Description
 *		Look for TCP socket matching *tuple*, optionally in a child
 *		network namespace *netns*. The return value must be checked,
 *		and if non-**NULL**, released via **bpf_sk_release**\ ().
 *
 *		This function is identical to **bpf_sk_lookup_tcp**\ (), except
 *		that it also returns timewait or request sockets. Use
 *		**bpf_sk_fullsock**\ () or **bpf_tcp_sock**\ () to access the
 *		full structure.
 *
 *		This helper is available only if the kernel was compiled with
 *		**CONFIG_NET** configuration option.
 *	Return
 *		Pointer to **struct bpf_sock**, or **NULL** in case of failure.
 *		For sockets with reuseport option, the **struct bpf_sock**
 *		result is from *reuse*\ **->socks**\ [] using the hash of the
 *		tuple.
 *
 * long bpf_tcp_check_syncookie(void *sk, void *iph, u32 iph_len, struct tcphdr *th, u32 th_len)
 * 	Description
 * 		Check whether *iph* and *th* contain a valid SYN cookie ACK for
 * 		the listening socket in *sk*.
 *
 * 		*iph* points to the start of the IPv4 or IPv6 header, while
 * 		*iph_len* contains **sizeof**\ (**struct iphdr**) or
 * 		**sizeof**\ (**struct ip6hdr**).
 *
 * 		*th* points to the start of the TCP header, while *th_len*
 * 		contains **sizeof**\ (**struct tcphdr**).
 * 	Return
 * 		0 if *iph* and *th* are a valid SYN cookie ACK, or a negative
 * 		error otherwise.
 *
 * long bpf_sysctl_get_name(struct bpf_sysctl *ctx, char *buf, size_t buf_len, u64 flags)
 *	Description
 *		Get name of sysctl in /proc/sys/ and copy it into provided by
 *		program buffer *buf* of size *buf_len*.
 *
 *		The buffer is always NUL terminated, unless it's zero-sized.
 *
 *		If *flags* is zero, full name (e.g. "net/ipv4/tcp_mem") is
 *		copied. Use **BPF_F_SYSCTL_BASE_NAME** flag to copy base name
 *		only (e.g. "tcp_mem").
 *	Return
 *		Number of character copied (not including the trailing NUL).
 *
 *		**-E2BIG** if the buffer wasn't big enough (*buf* will contain
 *		truncated name in this case).
 *
 * long bpf_sysctl_get_current_value(struct bpf_sysctl *ctx, char *buf, size_t buf_len)
 *	Description
 *		Get current value of sysctl as it is presented in /proc/sys
 *		(incl. newline, etc), and copy it as a string into provided
 *		by program buffer *buf* of size *buf_len*.
 *
 *		The whole value is copied, no matter what file position user
 *		space issued e.g. sys_read at.
 *
 *		The buffer is always NUL terminated, unless it's zero-sized.
 *	Return
 *		Number of character copied (not including the trailing NUL).
 *
 *		**-E2BIG** if the buffer wasn't big enough (*buf* will contain
 *		truncated name in this case).
 *
 *		**-EINVAL** if current value was unavailable, e.g. because
 *		sysctl is uninitialized and read returns -EIO for it.
 *
 * long bpf_sysctl_get_new_value(struct bpf_sysctl *ctx, char *buf, size_t buf_len)
 *	Description
 *		Get new value being written by user space to sysctl (before
 *		the actual write happens) and copy it as a string into
 *		provided by program buffer *buf* of size *buf_len*.
 *
 *		User space may write new value at file position > 0.
 *
 *		The buffer is always NUL terminated, unless it's zero-sized.
 *	Return
 *		Number of character copied (not including the trailing NUL).
 *
 *		**-E2BIG** if the buffer wasn't big enough (*buf* will contain
 *		truncated name in this case).
 *
 *		**-EINVAL** if sysctl is being read.
 *
 * long bpf_sysctl_set_new_value(struct bpf_sysctl *ctx, const char *buf, size_t buf_len)
 *	Description
 *		Override new value being written by user space to sysctl with
 *		value provided by program in buffer *buf* of size *buf_len*.
 *
 *		*buf* should contain a string in same form as provided by user
 *		space on sysctl write.
 *
 *		User space may write new value at file position > 0. To override
 *		the whole sysctl value file position should be set to zero.
 *	Return
 *		0 on success.
 *
 *		**-E2BIG** if the *buf_len* is too big.
 *
 *		**-EINVAL** if sysctl is being read.
 *
 * long bpf_strtol(const char *buf, size_t buf_len, u64 flags, long *res)
 *	Description
 *		Convert the initial part of the string from buffer *buf* of
 *		size *buf_len* to a long integer according to the given base
 *		and save the result in *res*.
 *
 *		The string may begin with an arbitrary amount of white space
 *		(as determined by **isspace**\ (3)) followed by a single
 *		optional '**-**' sign.
 *
 *		Five least significant bits of *flags* encode base, other bits
 *		are currently unused.
 *
 *		Base must be either 8, 10, 16 or 0 to detect it automatically
 *		similar to user space **strtol**\ (3).
 *	Return
 *		Number of characters consumed on success. Must be positive but
 *		no more than *buf_len*.
 *
 *		**-EINVAL** if no valid digits were found or unsupported base
 *		was provided.
 *
 *		**-ERANGE** if resulting value was out of range.
 *
 * long bpf_strtoul(const char *buf, size_t buf_len, u64 flags, unsigned long *res)
 *	Description
 *		Convert the initial part of the string from buffer *buf* of
 *		size *buf_len* to an unsigned long integer according to the
 *		given base and save the result in *res*.
 *
 *		The string may begin with an arbitrary amount of white space
 *		(as determined by **isspace**\ (3)).
 *
 *		Five least significant bits of *flags* encode base, other bits
 *		are currently unused.
 *
 *		Base must be either 8, 10, 16 or 0 to detect it automatically
 *		similar to user space **strtoul**\ (3).
 *	Return
 *		Number of characters consumed on success. Must be positive but
 *		no more than *buf_len*.
 *
 *		**-EINVAL** if no valid digits were found or unsupported base
 *		was provided.
 *
 *		**-ERANGE** if resulting value was out of range.
 *
 * void *bpf_sk_storage_get(struct bpf_map *map, void *sk, void *value, u64 flags)
 *	Description
 *		Get a bpf-local-storage from a *sk*.
 *
 *		Logically, it could be thought of getting the value from
 *		a *map* with *sk* as the **key**.  From this
 *		perspective,  the usage is not much different from
 *		**bpf_map_lookup_elem**\ (*map*, **&**\ *sk*) except this
 *		helper enforces the key must be a full socket and the map must
 *		be a **BPF_MAP_TYPE_SK_STORAGE** also.
 *
 *		Underneath, the value is stored locally at *sk* instead of
 *		the *map*.  The *map* is used as the bpf-local-storage
 *		"type". The bpf-local-storage "type" (i.e. the *map*) is
 *		searched against all bpf-local-storages residing at *sk*.
 *
 *		*sk* is a kernel **struct sock** pointer for LSM program.
 *		*sk* is a **struct bpf_sock** pointer for other program types.
 *
 *		An optional *flags* (**BPF_SK_STORAGE_GET_F_CREATE**) can be
 *		used such that a new bpf-local-storage will be
 *		created if one does not exist.  *value* can be used
 *		together with **BPF_SK_STORAGE_GET_F_CREATE** to specify
 *		the initial value of a bpf-local-storage.  If *value* is
 *		**NULL**, the new bpf-local-storage will be zero initialized.
 *	Return
 *		A bpf-local-storage pointer is returned on success.
 *
 *		**NULL** if not found or there was an error in adding
 *		a new bpf-local-storage.
 *
 * long bpf_sk_storage_delete(struct bpf_map *map, void *sk)
 *	Description
 *		Delete a bpf-local-storage from a *sk*.
 *	Return
 *		0 on success.
 *
 *		**-ENOENT** if the bpf-local-storage cannot be found.
 *		**-EINVAL** if sk is not a fullsock (e.g. a request_sock).
 *
 * long bpf_send_signal(u32 sig)
 *	Description
 *		Send signal *sig* to the process of the current task.
 *		The signal may be delivered to any of this process's threads.
 *	Return
 *		0 on success or successfully queued.
 *
 *		**-EBUSY** if work queue under nmi is full.
 *
 *		**-EINVAL** if *sig* is invalid.
 *
 *		**-EPERM** if no permission to send the *sig*.
 *
 *		**-EAGAIN** if bpf program can try again.
 *
 * s64 bpf_tcp_gen_syncookie(void *sk, void *iph, u32 iph_len, struct tcphdr *th, u32 th_len)
 *	Description
 *		Try to issue a SYN cookie for the packet with corresponding
 *		IP/TCP headers, *iph* and *th*, on the listening socket in *sk*.
 *
 *		*iph* points to the start of the IPv4 or IPv6 header, while
 *		*iph_len* contains **sizeof**\ (**struct iphdr**) or
 *		**sizeof**\ (**struct ip6hdr**).
 *
 *		*th* points to the start of the TCP header, while *th_len*
 *		contains the length of the TCP header.
 *	Return
 *		On success, lower 32 bits hold the generated SYN cookie in
 *		followed by 16 bits which hold the MSS value for that cookie,
 *		and the top 16 bits are unused.
 *
 *		On failure, the returned value is one of the following:
 *
 *		**-EINVAL** SYN cookie cannot be issued due to error
 *
 *		**-ENOENT** SYN cookie should not be issued (no SYN flood)
 *
 *		**-EOPNOTSUPP** kernel configuration does not enable SYN cookies
 *
 *		**-EPROTONOSUPPORT** IP packet version is not 4 or 6
 *
 * long bpf_skb_output(void *ctx, struct bpf_map *map, u64 flags, void *data, u64 size)
 * 	Description
 * 		Write raw *data* blob into a special BPF perf event held by
 * 		*map* of type **BPF_MAP_TYPE_PERF_EVENT_ARRAY**. This perf
 * 		event must have the following attributes: **PERF_SAMPLE_RAW**
 * 		as **sample_type**, **PERF_TYPE_SOFTWARE** as **type**, and
 * 		**PERF_COUNT_SW_BPF_OUTPUT** as **config**.
 *
 * 		The *flags* are used to indicate the index in *map* for which
 * 		the value must be put, masked with **BPF_F_INDEX_MASK**.
 * 		Alternatively, *flags* can be set to **BPF_F_CURRENT_CPU**
 * 		to indicate that the index of the current CPU core should be
 * 		used.
 *
 * 		The value to write, of *size*, is passed through eBPF stack and
 * 		pointed by *data*.
 *
 * 		*ctx* is a pointer to in-kernel struct sk_buff.
 *
 * 		This helper is similar to **bpf_perf_event_output**\ () but
 * 		restricted to raw_tracepoint bpf programs.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_probe_read_user(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		Safely attempt to read *size* bytes from user space address
 * 		*unsafe_ptr* and store the data in *dst*.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_probe_read_kernel(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		Safely attempt to read *size* bytes from kernel space address
 * 		*unsafe_ptr* and store the data in *dst*.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_probe_read_user_str(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		Copy a NUL terminated string from an unsafe user address
 * 		*unsafe_ptr* to *dst*. The *size* should include the
 * 		terminating NUL byte. In case the string length is smaller than
 * 		*size*, the target is not padded with further NUL bytes. If the
 * 		string length is larger than *size*, just *size*-1 bytes are
 * 		copied and the last byte is set to NUL.
 *
 * 		On success, the length of the copied string is returned. This
 * 		makes this helper useful in tracing programs for reading
 * 		strings, and more importantly to get its length at runtime. See
 * 		the following snippet:
 *
 * 		::
 *
 * 			SEC("kprobe/sys_open")
 * 			void bpf_sys_open(struct pt_regs *ctx)
 * 			{
 * 			        char buf[PATHLEN]; // PATHLEN is defined to 256
 * 			        int res = bpf_probe_read_user_str(buf, sizeof(buf),
 * 				                                  ctx->di);
 *
 * 				// Consume buf, for example push it to
 * 				// userspace via bpf_perf_event_output(); we
 * 				// can use res (the string length) as event
 * 				// size, after checking its boundaries.
 * 			}
 *
 * 		In comparison, using **bpf_probe_read_user**\ () helper here
 * 		instead to read the string would require to estimate the length
 * 		at compile time, and would often result in copying more memory
 * 		than necessary.
 *
 * 		Another useful use case is when parsing individual process
 * 		arguments or individual environment variables navigating
 * 		*current*\ **->mm->arg_start** and *current*\
 * 		**->mm->env_start**: using this helper and the return value,
 * 		one can quickly iterate at the right offset of the memory area.
 * 	Return
 * 		On success, the strictly positive length of the string,
 * 		including the trailing NUL character. On error, a negative
 * 		value.
 *
 * long bpf_probe_read_kernel_str(void *dst, u32 size, const void *unsafe_ptr)
 * 	Description
 * 		Copy a NUL terminated string from an unsafe kernel address *unsafe_ptr*
 * 		to *dst*. Same semantics as with **bpf_probe_read_user_str**\ () apply.
 * 	Return
 * 		On success, the strictly positive length of the string, including
 * 		the trailing NUL character. On error, a negative value.
 *
 * long bpf_tcp_send_ack(void *tp, u32 rcv_nxt)
 *	Description
 *		Send out a tcp-ack. *tp* is the in-kernel struct **tcp_sock**.
 *		*rcv_nxt* is the ack_seq to be sent out.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_send_signal_thread(u32 sig)
 *	Description
 *		Send signal *sig* to the thread corresponding to the current task.
 *	Return
 *		0 on success or successfully queued.
 *
 *		**-EBUSY** if work queue under nmi is full.
 *
 *		**-EINVAL** if *sig* is invalid.
 *
 *		**-EPERM** if no permission to send the *sig*.
 *
 *		**-EAGAIN** if bpf program can try again.
 *
 * u64 bpf_jiffies64(void)
 *	Description
 *		Obtain the 64bit jiffies
 *	Return
 *		The 64 bit jiffies
 *
 * long bpf_read_branch_records(struct bpf_perf_event_data *ctx, void *buf, u32 size, u64 flags)
 *	Description
 *		For an eBPF program attached to a perf event, retrieve the
 *		branch records (**struct perf_branch_entry**) associated to *ctx*
 *		and store it in the buffer pointed by *buf* up to size
 *		*size* bytes.
 *	Return
 *		On success, number of bytes written to *buf*. On error, a
 *		negative value.
 *
 *		The *flags* can be set to **BPF_F_GET_BRANCH_RECORDS_SIZE** to
 *		instead return the number of bytes required to store all the
 *		branch entries. If this flag is set, *buf* may be NULL.
 *
 *		**-EINVAL** if arguments invalid or **size** not a multiple
 *		of **sizeof**\ (**struct perf_branch_entry**\ ).
 *
 *		**-ENOENT** if architecture does not support branch records.
 *
 * long bpf_get_ns_current_pid_tgid(u64 dev, u64 ino, struct bpf_pidns_info *nsdata, u32 size)
 *	Description
 *		Returns 0 on success, values for *pid* and *tgid* as seen from the current
 *		*namespace* will be returned in *nsdata*.
 *	Return
 *		0 on success, or one of the following in case of failure:
 *
 *		**-EINVAL** if dev and inum supplied don't match dev_t and inode number
 *              with nsfs of current task, or if dev conversion to dev_t lost high bits.
 *
 *		**-ENOENT** if pidns does not exists for the current task.
 *
 * long bpf_xdp_output(void *ctx, struct bpf_map *map, u64 flags, void *data, u64 size)
 *	Description
 *		Write raw *data* blob into a special BPF perf event held by
 *		*map* of type **BPF_MAP_TYPE_PERF_EVENT_ARRAY**. This perf
 *		event must have the following attributes: **PERF_SAMPLE_RAW**
 *		as **sample_type**, **PERF_TYPE_SOFTWARE** as **type**, and
 *		**PERF_COUNT_SW_BPF_OUTPUT** as **config**.
 *
 *		The *flags* are used to indicate the index in *map* for which
 *		the value must be put, masked with **BPF_F_INDEX_MASK**.
 *		Alternatively, *flags* can be set to **BPF_F_CURRENT_CPU**
 *		to indicate that the index of the current CPU core should be
 *		used.
 *
 *		The value to write, of *size*, is passed through eBPF stack and
 *		pointed by *data*.
 *
 *		*ctx* is a pointer to in-kernel struct xdp_buff.
 *
 *		This helper is similar to **bpf_perf_eventoutput**\ () but
 *		restricted to raw_tracepoint bpf programs.
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * u64 bpf_get_netns_cookie(void *ctx)
 * 	Description
 * 		Retrieve the cookie (generated by the kernel) of the network
 * 		namespace the input *ctx* is associated with. The network
 * 		namespace cookie remains stable for its lifetime and provides
 * 		a global identifier that can be assumed unique. If *ctx* is
 * 		NULL, then the helper returns the cookie for the initial
 * 		network namespace. The cookie itself is very similar to that
 * 		of **bpf_get_socket_cookie**\ () helper, but for network
 * 		namespaces instead of sockets.
 * 	Return
 * 		A 8-byte long opaque number.
 *
 * u64 bpf_get_current_ancestor_cgroup_id(int ancestor_level)
 * 	Description
 * 		Return id of cgroup v2 that is ancestor of the cgroup associated
 * 		with the current task at the *ancestor_level*. The root cgroup
 * 		is at *ancestor_level* zero and each step down the hierarchy
 * 		increments the level. If *ancestor_level* == level of cgroup
 * 		associated with the current task, then return value will be the
 * 		same as that of **bpf_get_current_cgroup_id**\ ().
 *
 * 		The helper is useful to implement policies based on cgroups
 * 		that are upper in hierarchy than immediate cgroup associated
 * 		with the current task.
 *
 * 		The format of returned id and helper limitations are same as in
 * 		**bpf_get_current_cgroup_id**\ ().
 * 	Return
 * 		The id is returned or 0 in case the id could not be retrieved.
 *
 * long bpf_sk_assign(struct sk_buff *skb, void *sk, u64 flags)
 *	Description
 *		Helper is overloaded depending on BPF program type. This
 *		description applies to **BPF_PROG_TYPE_SCHED_CLS** and
 *		**BPF_PROG_TYPE_SCHED_ACT** programs.
 *
 *		Assign the *sk* to the *skb*. When combined with appropriate
 *		routing configuration to receive the packet towards the socket,
 *		will cause *skb* to be delivered to the specified socket.
 *		Subsequent redirection of *skb* via  **bpf_redirect**\ (),
 *		**bpf_clone_redirect**\ () or other methods outside of BPF may
 *		interfere with successful delivery to the socket.
 *
 *		This operation is only valid from TC ingress path.
 *
 *		The *flags* argument must be zero.
 *	Return
 *		0 on success, or a negative error in case of failure:
 *
 *		**-EINVAL** if specified *flags* are not supported.
 *
 *		**-ENOENT** if the socket is unavailable for assignment.
 *
 *		**-ENETUNREACH** if the socket is unreachable (wrong netns).
 *
 *		**-EOPNOTSUPP** if the operation is not supported, for example
 *		a call from outside of TC ingress.
 *
 *		**-ESOCKTNOSUPPORT** if the socket type is not supported
 *		(reuseport).
 *
 * long bpf_sk_assign(struct bpf_sk_lookup *ctx, struct bpf_sock *sk, u64 flags)
 *	Description
 *		Helper is overloaded depending on BPF program type. This
 *		description applies to **BPF_PROG_TYPE_SK_LOOKUP** programs.
 *
 *		Select the *sk* as a result of a socket lookup.
 *
 *		For the operation to succeed passed socket must be compatible
 *		with the packet description provided by the *ctx* object.
 *
 *		L4 protocol (**IPPROTO_TCP** or **IPPROTO_UDP**) must
 *		be an exact match. While IP family (**AF_INET** or
 *		**AF_INET6**) must be compatible, that is IPv6 sockets
 *		that are not v6-only can be selected for IPv4 packets.
 *
 *		Only TCP listeners and UDP unconnected sockets can be
 *		selected. *sk* can also be NULL to reset any previous
 *		selection.
 *
 *		*flags* argument can combination of following values:
 *
 *		* **BPF_SK_LOOKUP_F_REPLACE** to override the previous
 *		  socket selection, potentially done by a BPF program
 *		  that ran before us.
 *
 *		* **BPF_SK_LOOKUP_F_NO_REUSEPORT** to skip
 *		  load-balancing within reuseport group for the socket
 *		  being selected.
 *
 *		On success *ctx->sk* will point to the selected socket.
 *
 *	Return
 *		0 on success, or a negative errno in case of failure.
 *
 *		* **-EAFNOSUPPORT** if socket family (*sk->family*) is
 *		  not compatible with packet family (*ctx->family*).
 *
 *		* **-EEXIST** if socket has been already selected,
 *		  potentially by another program, and
 *		  **BPF_SK_LOOKUP_F_REPLACE** flag was not specified.
 *
 *		* **-EINVAL** if unsupported flags were specified.
 *
 *		* **-EPROTOTYPE** if socket L4 protocol
 *		  (*sk->protocol*) doesn't match packet protocol
 *		  (*ctx->protocol*).
 *
 *		* **-ESOCKTNOSUPPORT** if socket is not in allowed
 *		  state (TCP listening or UDP unconnected).
 *
 * u64 bpf_ktime_get_boot_ns(void)
 * 	Description
 * 		Return the time elapsed since system boot, in nanoseconds.
 * 		Does include the time the system was suspended.
 * 		See: **clock_gettime**\ (**CLOCK_BOOTTIME**)
 * 	Return
 * 		Current *ktime*.
 *
 * long bpf_seq_printf(struct seq_file *m, const char *fmt, u32 fmt_size, const void *data, u32 data_len)
 * 	Description
 * 		**bpf_seq_printf**\ () uses seq_file **seq_printf**\ () to print
 * 		out the format string.
 * 		The *m* represents the seq_file. The *fmt* and *fmt_size* are for
 * 		the format string itself. The *data* and *data_len* are format string
 * 		arguments. The *data* are a **u64** array and corresponding format string
 * 		values are stored in the array. For strings and pointers where pointees
 * 		are accessed, only the pointer values are stored in the *data* array.
 * 		The *data_len* is the size of *data* in bytes.
 *
 *		Formats **%s**, **%p{i,I}{4,6}** requires to read kernel memory.
 *		Reading kernel memory may fail due to either invalid address or
 *		valid address but requiring a major memory fault. If reading kernel memory
 *		fails, the string for **%s** will be an empty string, and the ip
 *		address for **%p{i,I}{4,6}** will be 0. Not returning error to
 *		bpf program is consistent with what **bpf_trace_printk**\ () does for now.
 * 	Return
 * 		0 on success, or a negative error in case of failure:
 *
 *		**-EBUSY** if per-CPU memory copy buffer is busy, can try again
 *		by returning 1 from bpf program.
 *
 *		**-EINVAL** if arguments are invalid, or if *fmt* is invalid/unsupported.
 *
 *		**-E2BIG** if *fmt* contains too many format specifiers.
 *
 *		**-EOVERFLOW** if an overflow happened: The same object will be tried again.
 *
 * long bpf_seq_write(struct seq_file *m, const void *data, u32 len)
 * 	Description
 * 		**bpf_seq_write**\ () uses seq_file **seq_write**\ () to write the data.
 * 		The *m* represents the seq_file. The *data* and *len* represent the
 * 		data to write in bytes.
 * 	Return
 * 		0 on success, or a negative error in case of failure:
 *
 *		**-EOVERFLOW** if an overflow happened: The same object will be tried again.
 *
 * u64 bpf_sk_cgroup_id(void *sk)
 *	Description
 *		Return the cgroup v2 id of the socket *sk*.
 *
 *		*sk* must be a non-**NULL** pointer to a socket, e.g. one
 *		returned from **bpf_sk_lookup_xxx**\ (),
 *		**bpf_sk_fullsock**\ (), etc. The format of returned id is
 *		same as in **bpf_skb_cgroup_id**\ ().
 *
 *		This helper is available only if the kernel was compiled with
 *		the **CONFIG_SOCK_CGROUP_DATA** configuration option.
 *	Return
 *		The id is returned or 0 in case the id could not be retrieved.
 *
 * u64 bpf_sk_ancestor_cgroup_id(void *sk, int ancestor_level)
 *	Description
 *		Return id of cgroup v2 that is ancestor of cgroup associated
 *		with the *sk* at the *ancestor_level*.  The root cgroup is at
 *		*ancestor_level* zero and each step down the hierarchy
 *		increments the level. If *ancestor_level* == level of cgroup
 *		associated with *sk*, then return value will be same as that
 *		of **bpf_sk_cgroup_id**\ ().
 *
 *		The helper is useful to implement policies based on cgroups
 *		that are upper in hierarchy than immediate cgroup associated
 *		with *sk*.
 *
 *		The format of returned id and helper limitations are same as in
 *		**bpf_sk_cgroup_id**\ ().
 *	Return
 *		The id is returned or 0 in case the id could not be retrieved.
 *
 * long bpf_ringbuf_output(void *ringbuf, void *data, u64 size, u64 flags)
 * 	Description
 * 		Copy *size* bytes from *data* into a ring buffer *ringbuf*.
 * 		If **BPF_RB_NO_WAKEUP** is specified in *flags*, no notification
 * 		of new data availability is sent.
 * 		If **BPF_RB_FORCE_WAKEUP** is specified in *flags*, notification
 * 		of new data availability is sent unconditionally.
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * void *bpf_ringbuf_reserve(void *ringbuf, u64 size, u64 flags)
 * 	Description
 * 		Reserve *size* bytes of payload in a ring buffer *ringbuf*.
 * 	Return
 * 		Valid pointer with *size* bytes of memory available; NULL,
 * 		otherwise.
 *
 * void bpf_ringbuf_submit(void *data, u64 flags)
 * 	Description
 * 		Submit reserved ring buffer sample, pointed to by *data*.
 * 		If **BPF_RB_NO_WAKEUP** is specified in *flags*, no notification
 * 		of new data availability is sent.
 * 		If **BPF_RB_FORCE_WAKEUP** is specified in *flags*, notification
 * 		of new data availability is sent unconditionally.
 * 	Return
 * 		Nothing. Always succeeds.
 *
 * void bpf_ringbuf_discard(void *data, u64 flags)
 * 	Description
 * 		Discard reserved ring buffer sample, pointed to by *data*.
 * 		If **BPF_RB_NO_WAKEUP** is specified in *flags*, no notification
 * 		of new data availability is sent.
 * 		If **BPF_RB_FORCE_WAKEUP** is specified in *flags*, notification
 * 		of new data availability is sent unconditionally.
 * 	Return
 * 		Nothing. Always succeeds.
 *
 * u64 bpf_ringbuf_query(void *ringbuf, u64 flags)
 *	Description
 *		Query various characteristics of provided ring buffer. What
 *		exactly is queries is determined by *flags*:
 *
 *		* **BPF_RB_AVAIL_DATA**: Amount of data not yet consumed.
 *		* **BPF_RB_RING_SIZE**: The size of ring buffer.
 *		* **BPF_RB_CONS_POS**: Consumer position (can wrap around).
 *		* **BPF_RB_PROD_POS**: Producer(s) position (can wrap around).
 *
 *		Data returned is just a momentary snapshot of actual values
 *		and could be inaccurate, so this facility should be used to
 *		power heuristics and for reporting, not to make 100% correct
 *		calculation.
 *	Return
 *		Requested value, or 0, if *flags* are not recognized.
 *
 * long bpf_csum_level(struct sk_buff *skb, u64 level)
 * 	Description
 * 		Change the skbs checksum level by one layer up or down, or
 * 		reset it entirely to none in order to have the stack perform
 * 		checksum validation. The level is applicable to the following
 * 		protocols: TCP, UDP, GRE, SCTP, FCOE. For example, a decap of
 * 		| ETH | IP | UDP | GUE | IP | TCP | into | ETH | IP | TCP |
 * 		through **bpf_skb_adjust_room**\ () helper with passing in
 * 		**BPF_F_ADJ_ROOM_NO_CSUM_RESET** flag would require one	call
 * 		to **bpf_csum_level**\ () with **BPF_CSUM_LEVEL_DEC** since
 * 		the UDP header is removed. Similarly, an encap of the latter
 * 		into the former could be accompanied by a helper call to
 * 		**bpf_csum_level**\ () with **BPF_CSUM_LEVEL_INC** if the
 * 		skb is still intended to be processed in higher layers of the
 * 		stack instead of just egressing at tc.
 *
 * 		There are three supported level settings at this time:
 *
 * 		* **BPF_CSUM_LEVEL_INC**: Increases skb->csum_level for skbs
 * 		  with CHECKSUM_UNNECESSARY.
 * 		* **BPF_CSUM_LEVEL_DEC**: Decreases skb->csum_level for skbs
 * 		  with CHECKSUM_UNNECESSARY.
 * 		* **BPF_CSUM_LEVEL_RESET**: Resets skb->csum_level to 0 and
 * 		  sets CHECKSUM_NONE to force checksum validation by the stack.
 * 		* **BPF_CSUM_LEVEL_QUERY**: No-op, returns the current
 * 		  skb->csum_level.
 * 	Return
 * 		0 on success, or a negative error in case of failure. In the
 * 		case of **BPF_CSUM_LEVEL_QUERY**, the current skb->csum_level
 * 		is returned or the error code -EACCES in case the skb is not
 * 		subject to CHECKSUM_UNNECESSARY.
 *
 * struct tcp6_sock *bpf_skc_to_tcp6_sock(void *sk)
 *	Description
 *		Dynamically cast a *sk* pointer to a *tcp6_sock* pointer.
 *	Return
 *		*sk* if casting is valid, or **NULL** otherwise.
 *
 * struct tcp_sock *bpf_skc_to_tcp_sock(void *sk)
 *	Description
 *		Dynamically cast a *sk* pointer to a *tcp_sock* pointer.
 *	Return
 *		*sk* if casting is valid, or **NULL** otherwise.
 *
 * struct tcp_timewait_sock *bpf_skc_to_tcp_timewait_sock(void *sk)
 * 	Description
 *		Dynamically cast a *sk* pointer to a *tcp_timewait_sock* pointer.
 *	Return
 *		*sk* if casting is valid, or **NULL** otherwise.
 *
 * struct tcp_request_sock *bpf_skc_to_tcp_request_sock(void *sk)
 * 	Description
 *		Dynamically cast a *sk* pointer to a *tcp_request_sock* pointer.
 *	Return
 *		*sk* if casting is valid, or **NULL** otherwise.
 *
 * struct udp6_sock *bpf_skc_to_udp6_sock(void *sk)
 * 	Description
 *		Dynamically cast a *sk* pointer to a *udp6_sock* pointer.
 *	Return
 *		*sk* if casting is valid, or **NULL** otherwise.
 *
 * long bpf_get_task_stack(struct task_struct *task, void *buf, u32 size, u64 flags)
 *	Description
 *		Return a user or a kernel stack in bpf program provided buffer.
 *		To achieve this, the helper needs *task*, which is a valid
 *		pointer to **struct task_struct**. To store the stacktrace, the
 *		bpf program provides *buf* with a nonnegative *size*.
 *
 *		The last argument, *flags*, holds the number of stack frames to
 *		skip (from 0 to 255), masked with
 *		**BPF_F_SKIP_FIELD_MASK**. The next bits can be used to set
 *		the following flags:
 *
 *		**BPF_F_USER_STACK**
 *			Collect a user space stack instead of a kernel stack.
 *		**BPF_F_USER_BUILD_ID**
 *			Collect buildid+offset instead of ips for user stack,
 *			only valid if **BPF_F_USER_STACK** is also specified.
 *
 *		**bpf_get_task_stack**\ () can collect up to
 *		**PERF_MAX_STACK_DEPTH** both kernel and user frames, subject
 *		to sufficient large buffer size. Note that
 *		this limit can be controlled with the **sysctl** program, and
 *		that it should be manually increased in order to profile long
 *		user stacks (such as stacks for Java programs). To do so, use:
 *
 *		::
 *
 *			# sysctl kernel.perf_event_max_stack=<new value>
 *	Return
 *		A non-negative value equal to or less than *size* on success,
 *		or a negative error in case of failure.
 *
 * long bpf_load_hdr_opt(struct bpf_sock_ops *skops, void *searchby_res, u32 len, u64 flags)
 *	Description
 *		Load header option.  Support reading a particular TCP header
 *		option for bpf program (**BPF_PROG_TYPE_SOCK_OPS**).
 *
 *		If *flags* is 0, it will search the option from the
 *		*skops*\ **->skb_data**.  The comment in **struct bpf_sock_ops**
 *		has details on what skb_data contains under different
 *		*skops*\ **->op**.
 *
 *		The first byte of the *searchby_res* specifies the
 *		kind that it wants to search.
 *
 *		If the searching kind is an experimental kind
 *		(i.e. 253 or 254 according to RFC6994).  It also
 *		needs to specify the "magic" which is either
 *		2 bytes or 4 bytes.  It then also needs to
 *		specify the size of the magic by using
 *		the 2nd byte which is "kind-length" of a TCP
 *		header option and the "kind-length" also
 *		includes the first 2 bytes "kind" and "kind-length"
 *		itself as a normal TCP header option also does.
 *
 *		For example, to search experimental kind 254 with
 *		2 byte magic 0xeB9F, the searchby_res should be
 *		[ 254, 4, 0xeB, 0x9F, 0, 0, .... 0 ].
 *
 *		To search for the standard window scale option (3),
 *		the *searchby_res* should be [ 3, 0, 0, .... 0 ].
 *		Note, kind-length must be 0 for regular option.
 *
 *		Searching for No-Op (0) and End-of-Option-List (1) are
 *		not supported.
 *
 *		*len* must be at least 2 bytes which is the minimal size
 *		of a header option.
 *
 *		Supported flags:
 *
 *		* **BPF_LOAD_HDR_OPT_TCP_SYN** to search from the
 *		  saved_syn packet or the just-received syn packet.
 *
 *	Return
 *		> 0 when found, the header option is copied to *searchby_res*.
 *		The return value is the total length copied. On failure, a
 *		negative error code is returned:
 *
 *		**-EINVAL** if a parameter is invalid.
 *
 *		**-ENOMSG** if the option is not found.
 *
 *		**-ENOENT** if no syn packet is available when
 *		**BPF_LOAD_HDR_OPT_TCP_SYN** is used.
 *
 *		**-ENOSPC** if there is not enough space.  Only *len* number of
 *		bytes are copied.
 *
 *		**-EFAULT** on failure to parse the header options in the
 *		packet.
 *
 *		**-EPERM** if the helper cannot be used under the current
 *		*skops*\ **->op**.
 *
 * long bpf_store_hdr_opt(struct bpf_sock_ops *skops, const void *from, u32 len, u64 flags)
 *	Description
 *		Store header option.  The data will be copied
 *		from buffer *from* with length *len* to the TCP header.
 *
 *		The buffer *from* should have the whole option that
 *		includes the kind, kind-length, and the actual
 *		option data.  The *len* must be at least kind-length
 *		long.  The kind-length does not have to be 4 byte
 *		aligned.  The kernel will take care of the padding
 *		and setting the 4 bytes aligned value to th->doff.
 *
 *		This helper will check for duplicated option
 *		by searching the same option in the outgoing skb.
 *
 *		This helper can only be called during
 *		**BPF_SOCK_OPS_WRITE_HDR_OPT_CB**.
 *
 *	Return
 *		0 on success, or negative error in case of failure:
 *
 *		**-EINVAL** If param is invalid.
 *
 *		**-ENOSPC** if there is not enough space in the header.
 *		Nothing has been written
 *
 *		**-EEXIST** if the option already exists.
 *
 *		**-EFAULT** on failrue to parse the existing header options.
 *
 *		**-EPERM** if the helper cannot be used under the current
 *		*skops*\ **->op**.
 *
 * long bpf_reserve_hdr_opt(struct bpf_sock_ops *skops, u32 len, u64 flags)
 *	Description
 *		Reserve *len* bytes for the bpf header option.  The
 *		space will be used by **bpf_store_hdr_opt**\ () later in
 *		**BPF_SOCK_OPS_WRITE_HDR_OPT_CB**.
 *
 *		If **bpf_reserve_hdr_opt**\ () is called multiple times,
 *		the total number of bytes will be reserved.
 *
 *		This helper can only be called during
 *		**BPF_SOCK_OPS_HDR_OPT_LEN_CB**.
 *
 *	Return
 *		0 on success, or negative error in case of failure:
 *
 *		**-EINVAL** if a parameter is invalid.
 *
 *		**-ENOSPC** if there is not enough space in the header.
 *
 *		**-EPERM** if the helper cannot be used under the current
 *		*skops*\ **->op**.
 *
 * void *bpf_inode_storage_get(struct bpf_map *map, void *inode, void *value, u64 flags)
 *	Description
 *		Get a bpf_local_storage from an *inode*.
 *
 *		Logically, it could be thought of as getting the value from
 *		a *map* with *inode* as the **key**.  From this
 *		perspective,  the usage is not much different from
 *		**bpf_map_lookup_elem**\ (*map*, **&**\ *inode*) except this
 *		helper enforces the key must be an inode and the map must also
 *		be a **BPF_MAP_TYPE_INODE_STORAGE**.
 *
 *		Underneath, the value is stored locally at *inode* instead of
 *		the *map*.  The *map* is used as the bpf-local-storage
 *		"type". The bpf-local-storage "type" (i.e. the *map*) is
 *		searched against all bpf_local_storage residing at *inode*.
 *
 *		An optional *flags* (**BPF_LOCAL_STORAGE_GET_F_CREATE**) can be
 *		used such that a new bpf_local_storage will be
 *		created if one does not exist.  *value* can be used
 *		together with **BPF_LOCAL_STORAGE_GET_F_CREATE** to specify
 *		the initial value of a bpf_local_storage.  If *value* is
 *		**NULL**, the new bpf_local_storage will be zero initialized.
 *	Return
 *		A bpf_local_storage pointer is returned on success.
 *
 *		**NULL** if not found or there was an error in adding
 *		a new bpf_local_storage.
 *
 * int bpf_inode_storage_delete(struct bpf_map *map, void *inode)
 *	Description
 *		Delete a bpf_local_storage from an *inode*.
 *	Return
 *		0 on success.
 *
 *		**-ENOENT** if the bpf_local_storage cannot be found.
 *
 * long bpf_d_path(struct path *path, char *buf, u32 sz)
 *	Description
 *		Return full path for given **struct path** object, which
 *		needs to be the kernel BTF *path* object. The path is
 *		returned in the provided buffer *buf* of size *sz* and
 *		is zero terminated.
 *
 *	Return
 *		On success, the strictly positive length of the string,
 *		including the trailing NUL character. On error, a negative
 *		value.
 *
 * long bpf_copy_from_user(void *dst, u32 size, const void *user_ptr)
 * 	Description
 * 		Read *size* bytes from user space address *user_ptr* and store
 * 		the data in *dst*. This is a wrapper of **copy_from_user**\ ().
 * 	Return
 * 		0 on success, or a negative error in case of failure.
 *
 * long bpf_snprintf_btf(char *str, u32 str_size, struct btf_ptr *ptr, u32 btf_ptr_size, u64 flags)
 *	Description
 *		Use BTF to store a string representation of *ptr*->ptr in *str*,
 *		using *ptr*->type_id.  This value should specify the type
 *		that *ptr*->ptr points to. LLVM __builtin_btf_type_id(type, 1)
 *		can be used to look up vmlinux BTF type ids. Traversing the
 *		data structure using BTF, the type information and values are
 *		stored in the first *str_size* - 1 bytes of *str*.  Safe copy of
 *		the pointer data is carried out to avoid kernel crashes during
 *		operation.  Smaller types can use string space on the stack;
 *		larger programs can use map data to store the string
 *		representation.
 *
 *		The string can be subsequently shared with userspace via
 *		bpf_perf_event_output() or ring buffer interfaces.
 *		bpf_trace_printk() is to be avoided as it places too small
 *		a limit on string size to be useful.
 *
 *		*flags* is a combination of
 *
 *		**BTF_F_COMPACT**
 *			no formatting around type information
 *		**BTF_F_NONAME**
 *			no struct/union member names/types
 *		**BTF_F_PTR_RAW**
 *			show raw (unobfuscated) pointer values;
 *			equivalent to printk specifier %px.
 *		**BTF_F_ZERO**
 *			show zero-valued struct/union members; they
 *			are not displayed by default
 *
 *	Return
 *		The number of bytes that were written (or would have been
 *		written if output had to be truncated due to string size),
 *		or a negative error in cases of failure.
 *
 * long bpf_seq_printf_btf(struct seq_file *m, struct btf_ptr *ptr, u32 ptr_size, u64 flags)
 *	Description
 *		Use BTF to write to seq_write a string representation of
 *		*ptr*->ptr, using *ptr*->type_id as per bpf_snprintf_btf().
 *		*flags* are identical to those used for bpf_snprintf_btf.
 *	Return
 *		0 on success or a negative error in case of failure.
 *
 * u64 bpf_skb_cgroup_classid(struct sk_buff *skb)
 * 	Description
 * 		See **bpf_get_cgroup_classid**\ () for the main description.
 * 		This helper differs from **bpf_get_cgroup_classid**\ () in that
 * 		the cgroup v1 net_cls class is retrieved only from the *skb*'s
 * 		associated socket instead of the current process.
 * 	Return
 * 		The id is returned or 0 in case the id could not be retrieved.
 *
 * long bpf_redirect_neigh(u32 ifindex, u64 flags)
 * 	Description
 * 		Redirect the packet to another net device of index *ifindex*
 * 		and fill in L2 addresses from neighboring subsystem. This helper
 * 		is somewhat similar to **bpf_redirect**\ (), except that it
 * 		fills in e.g. MAC addresses based on the L3 information from
 * 		the packet. This helper is supported for IPv4 and IPv6 protocols.
 * 		The *flags* argument is reserved and must be 0. The helper is
 * 		currently only supported for tc BPF program types.
 * 	Return
 * 		The helper returns **TC_ACT_REDIRECT** on success or
 * 		**TC_ACT_SHOT** on error.
 *
 * void *bpf_per_cpu_ptr(const void *percpu_ptr, u32 cpu)
 *     Description
 *             Take a pointer to a percpu ksym, *percpu_ptr*, and return a
 *             pointer to the percpu kernel variable on *cpu*. A ksym is an
 *             extern variable decorated with '__ksym'. For ksym, there is a
 *             global var (either static or global) defined of the same name
 *             in the kernel. The ksym is percpu if the global var is percpu.
 *             The returned pointer points to the global percpu var on *cpu*.
 *
 *             bpf_per_cpu_ptr() has the same semantic as per_cpu_ptr() in the
 *             kernel, except that bpf_per_cpu_ptr() may return NULL. This
 *             happens if *cpu* is larger than nr_cpu_ids. The caller of
 *             bpf_per_cpu_ptr() must check the returned value.
 *     Return
 *             A pointer pointing to the kernel percpu variable on *cpu*, or
 *             NULL, if *cpu* is invalid.
 */
#define __BPF_FUNC_MAPPER(FN)		\
	FN(unspec),			\
	FN(map_lookup_elem),		\
	FN(map_update_elem),		\
	FN(map_delete_elem),		\
	FN(probe_read),			\
	FN(ktime_get_ns),		\
	FN(trace_printk),		\
	FN(get_prandom_u32),		\
	FN(get_smp_processor_id),	\
	FN(skb_store_bytes),		\
	FN(l3_csum_replace),		\
	FN(l4_csum_replace),		\
	FN(tail_call),			\
	FN(clone_redirect),		\
	FN(get_current_pid_tgid),	\
	FN(get_current_uid_gid),	\
	FN(get_current_comm),		\
	FN(get_cgroup_classid),		\
	FN(skb_vlan_push),		\
	FN(skb_vlan_pop),		\
	FN(skb_get_tunnel_key),		\
	FN(skb_set_tunnel_key),		\
	FN(perf_event_read),		\
	FN(redirect),			\
	FN(get_route_realm),		\
	FN(perf_event_output),		\
	FN(skb_load_bytes),		\
	FN(get_stackid),		\
	FN(csum_diff),			\
	FN(skb_get_tunnel_opt),		\
	FN(skb_set_tunnel_opt),		\
	FN(skb_change_proto),		\
	FN(skb_change_type),		\
	FN(skb_under_cgroup),		\
	FN(get_hash_recalc),		\
	FN(get_current_task),		\
	FN(probe_write_user),		\
	FN(current_task_under_cgroup),	\
	FN(skb_change_tail),		\
	FN(skb_pull_data),		\
	FN(csum_update),		\
	FN(set_hash_invalid),		\
	FN(get_numa_node_id),		\
	FN(skb_change_head),		\
	FN(xdp_adjust_head),		\
	FN(probe_read_str),		\
	FN(get_socket_cookie),		\
	FN(get_socket_uid),		\
	FN(set_hash),			\
	FN(setsockopt),			\
	FN(skb_adjust_room),		\
	FN(redirect_map),		\
	FN(sk_redirect_map),		\
	FN(sock_map_update),		\
	FN(xdp_adjust_meta),		\
	FN(perf_event_read_value),	\
	FN(perf_prog_read_value),	\
	FN(getsockopt),			\
	FN(override_return),		\
	FN(sock_ops_cb_flags_set),	\
	FN(msg_redirect_map),		\
	FN(msg_apply_bytes),		\
	FN(msg_cork_bytes),		\
	FN(msg_pull_data),		\
	FN(bind),			\
	FN(xdp_adjust_tail),		\
	FN(skb_get_xfrm_state),		\
	FN(get_stack),			\
	FN(skb_load_bytes_relative),	\
	FN(fib_lookup),			\
	FN(sock_hash_update),		\
	FN(msg_redirect_hash),		\
	FN(sk_redirect_hash),		\
	FN(lwt_push_encap),		\
	FN(lwt_seg6_store_bytes),	\
	FN(lwt_seg6_adjust_srh),	\
	FN(lwt_seg6_action),		\
	FN(rc_repeat),			\
	FN(rc_keydown),			\
	FN(skb_cgroup_id),		\
	FN(get_current_cgroup_id),	\
	FN(get_local_storage),		\
	FN(sk_select_reuseport),	\
	FN(skb_ancestor_cgroup_id),	\
	FN(sk_lookup_tcp),		\
	FN(sk_lookup_udp),		\
	FN(sk_release),			\
	FN(map_push_elem),		\
	FN(map_pop_elem),		\
	FN(map_peek_elem),		\
	FN(msg_push_data),		\
	FN(msg_pop_data),		\
	FN(rc_pointer_rel),		\
	FN(spin_lock),			\
	FN(spin_unlock),		\
	FN(sk_fullsock),		\
	FN(tcp_sock),			\
	FN(skb_ecn_set_ce),		\
	FN(get_listener_sock),		\
	FN(skc_lookup_tcp),		\
	FN(tcp_check_syncookie),	\
	FN(sysctl_get_name),		\
	FN(sysctl_get_current_value),	\
	FN(sysctl_get_new_value),	\
	FN(sysctl_set_new_value),	\
	FN(strtol),			\
	FN(strtoul),			\
	FN(sk_storage_get),		\
	FN(sk_storage_delete),		\
	FN(send_signal),		\
	FN(tcp_gen_syncookie),		\
	FN(skb_output),			\
	FN(probe_read_user),		\
	FN(probe_read_kernel),		\
	FN(probe_read_user_str),	\
	FN(probe_read_kernel_str),	\
	FN(tcp_send_ack),		\
	FN(send_signal_thread),		\
	FN(jiffies64),			\
	FN(read_branch_records),	\
	FN(get_ns_current_pid_tgid),	\
	FN(xdp_output),			\
	FN(get_netns_cookie),		\
	FN(get_current_ancestor_cgroup_id),	\
	FN(sk_assign),			\
	FN(ktime_get_boot_ns),		\
	FN(seq_printf),			\
	FN(seq_write),			\
	FN(sk_cgroup_id),		\
	FN(sk_ancestor_cgroup_id),	\
	FN(ringbuf_output),		\
	FN(ringbuf_reserve),		\
	FN(ringbuf_submit),		\
	FN(ringbuf_discard),		\
	FN(ringbuf_query),		\
	FN(csum_level),			\
	FN(skc_to_tcp6_sock),		\
	FN(skc_to_tcp_sock),		\
	FN(skc_to_tcp_timewait_sock),	\
	FN(skc_to_tcp_request_sock),	\
	FN(skc_to_udp6_sock),		\
	FN(get_task_stack),		\
	FN(load_hdr_opt),		\
	FN(store_hdr_opt),		\
	FN(reserve_hdr_opt),		\
	FN(inode_storage_get),		\
	FN(inode_storage_delete),	\
	FN(d_path),			\
	FN(copy_from_user),		\
	FN(snprintf_btf),		\
	FN(seq_printf_btf),		\
	FN(skb_cgroup_classid),		\
	FN(redirect_neigh),		\
	FN(bpf_per_cpu_ptr),            \
	/* */

/* integer value in 'imm' field of BPF_CALL instruction selects which helper
 * function eBPF program intends to call
 */
#define __BPF_ENUM_FN(x) BPF_FUNC_ ## x
enum bpf_func_id {
	__BPF_FUNC_MAPPER(__BPF_ENUM_FN)
	__BPF_FUNC_MAX_ID,
};
#undef __BPF_ENUM_FN

/* All flags used by eBPF helper functions, placed here. */

/* BPF_FUNC_skb_store_bytes flags. */
enum {
	BPF_F_RECOMPUTE_CSUM		= (1ULL << 0),
	BPF_F_INVALIDATE_HASH		= (1ULL << 1),
};

/* BPF_FUNC_l3_csum_replace and BPF_FUNC_l4_csum_replace flags.
 * First 4 bits are for passing the header field size.
 */
enum {
	BPF_F_HDR_FIELD_MASK		= 0xfULL,
};

/* BPF_FUNC_l4_csum_replace flags. */
enum {
	BPF_F_PSEUDO_HDR		= (1ULL << 4),
	BPF_F_MARK_MANGLED_0		= (1ULL << 5),
	BPF_F_MARK_ENFORCE		= (1ULL << 6),
};

/* BPF_FUNC_clone_redirect and BPF_FUNC_redirect flags. */
enum {
	BPF_F_INGRESS			= (1ULL << 0),
};

/* BPF_FUNC_skb_set_tunnel_key and BPF_FUNC_skb_get_tunnel_key flags. */
enum {
	BPF_F_TUNINFO_IPV6		= (1ULL << 0),
};

/* flags for both BPF_FUNC_get_stackid and BPF_FUNC_get_stack. */
enum {
	BPF_F_SKIP_FIELD_MASK		= 0xffULL,
	BPF_F_USER_STACK		= (1ULL << 8),
/* flags used by BPF_FUNC_get_stackid only. */
	BPF_F_FAST_STACK_CMP		= (1ULL << 9),
	BPF_F_REUSE_STACKID		= (1ULL << 10),
/* flags used by BPF_FUNC_get_stack only. */
	BPF_F_USER_BUILD_ID		= (1ULL << 11),
};

/* BPF_FUNC_skb_set_tunnel_key flags. */
enum {
	BPF_F_ZERO_CSUM_TX		= (1ULL << 1),
	BPF_F_DONT_FRAGMENT		= (1ULL << 2),
	BPF_F_SEQ_NUMBER		= (1ULL << 3),
};

/* BPF_FUNC_perf_event_output, BPF_FUNC_perf_event_read and
 * BPF_FUNC_perf_event_read_value flags.
 */
enum {
	BPF_F_INDEX_MASK		= 0xffffffffULL,
	BPF_F_CURRENT_CPU		= BPF_F_INDEX_MASK,
/* BPF_FUNC_perf_event_output for sk_buff input context. */
	BPF_F_CTXLEN_MASK		= (0xfffffULL << 32),
};

/* Current network namespace */
enum {
	BPF_F_CURRENT_NETNS		= (-1L),
};

/* BPF_FUNC_csum_level level values. */
enum {
	BPF_CSUM_LEVEL_QUERY,
	BPF_CSUM_LEVEL_INC,
	BPF_CSUM_LEVEL_DEC,
	BPF_CSUM_LEVEL_RESET,
};

/* BPF_FUNC_skb_adjust_room flags. */
enum {
	BPF_F_ADJ_ROOM_FIXED_GSO	= (1ULL << 0),
	BPF_F_ADJ_ROOM_ENCAP_L3_IPV4	= (1ULL << 1),
	BPF_F_ADJ_ROOM_ENCAP_L3_IPV6	= (1ULL << 2),
	BPF_F_ADJ_ROOM_ENCAP_L4_GRE	= (1ULL << 3),
	BPF_F_ADJ_ROOM_ENCAP_L4_UDP	= (1ULL << 4),
	BPF_F_ADJ_ROOM_NO_CSUM_RESET	= (1ULL << 5),
};

enum {
	BPF_ADJ_ROOM_ENCAP_L2_MASK	= 0xff,
	BPF_ADJ_ROOM_ENCAP_L2_SHIFT	= 56,
};

#define BPF_F_ADJ_ROOM_ENCAP_L2(len)	(((__u64)len & \
					  BPF_ADJ_ROOM_ENCAP_L2_MASK) \
					 << BPF_ADJ_ROOM_ENCAP_L2_SHIFT)

/* BPF_FUNC_sysctl_get_name flags. */
enum {
	BPF_F_SYSCTL_BASE_NAME		= (1ULL << 0),
};

/* BPF_FUNC_<kernel_obj>_storage_get flags */
enum {
	BPF_LOCAL_STORAGE_GET_F_CREATE	= (1ULL << 0),
	/* BPF_SK_STORAGE_GET_F_CREATE is only kept for backward compatibility
	 * and BPF_LOCAL_STORAGE_GET_F_CREATE must be used instead.
	 */
	BPF_SK_STORAGE_GET_F_CREATE  = BPF_LOCAL_STORAGE_GET_F_CREATE,
};

/* BPF_FUNC_read_branch_records flags. */
enum {
	BPF_F_GET_BRANCH_RECORDS_SIZE	= (1ULL << 0),
};

/* BPF_FUNC_bpf_ringbuf_commit, BPF_FUNC_bpf_ringbuf_discard, and
 * BPF_FUNC_bpf_ringbuf_output flags.
 */
enum {
	BPF_RB_NO_WAKEUP		= (1ULL << 0),
	BPF_RB_FORCE_WAKEUP		= (1ULL << 1),
};

/* BPF_FUNC_bpf_ringbuf_query flags */
enum {
	BPF_RB_AVAIL_DATA = 0,
	BPF_RB_RING_SIZE = 1,
	BPF_RB_CONS_POS = 2,
	BPF_RB_PROD_POS = 3,
};

/* BPF ring buffer constants */
enum {
	BPF_RINGBUF_BUSY_BIT		= (1U << 31),
	BPF_RINGBUF_DISCARD_BIT		= (1U << 30),
	BPF_RINGBUF_HDR_SZ		= 8,
};

/* BPF_FUNC_sk_assign flags in bpf_sk_lookup context. */
enum {
	BPF_SK_LOOKUP_F_REPLACE		= (1ULL << 0),
	BPF_SK_LOOKUP_F_NO_REUSEPORT	= (1ULL << 1),
};

/* Mode for BPF_FUNC_skb_adjust_room helper. */
enum bpf_adj_room_mode {
	BPF_ADJ_ROOM_NET,
	BPF_ADJ_ROOM_MAC,
};

/* Mode for BPF_FUNC_skb_load_bytes_relative helper. */
enum bpf_hdr_start_off {
	BPF_HDR_START_MAC,
	BPF_HDR_START_NET,
};

/* Encapsulation type for BPF_FUNC_lwt_push_encap helper. */
enum bpf_lwt_encap_mode {
	BPF_LWT_ENCAP_SEG6,
	BPF_LWT_ENCAP_SEG6_INLINE,
	BPF_LWT_ENCAP_IP,
};

#define __bpf_md_ptr(type, name)	\
union {					\
	type name;			\
	__u64 :64;			\
} __attribute__((aligned(8)))

/* user accessible mirror of in-kernel sk_buff.
 * new fields can only be added to the end of this structure
 */
struct __sk_buff {
	__u32 len;
	__u32 pkt_type;
	__u32 mark;
	__u32 queue_mapping;
	__u32 protocol;
	__u32 vlan_present;
	__u32 vlan_tci;
	__u32 vlan_proto;
	__u32 priority;
	__u32 ingress_ifindex;
	__u32 ifindex;
	__u32 tc_index;
	__u32 cb[5];
	__u32 hash;
	__u32 tc_classid;
	__u32 data;
	__u32 data_end;
	__u32 napi_id;

	/* Accessed by BPF_PROG_TYPE_sk_skb types from here to ... */
	__u32 family;
	__u32 remote_ip4;	/* Stored in network byte order */
	__u32 local_ip4;	/* Stored in network byte order */
	__u32 remote_ip6[4];	/* Stored in network byte order */
	__u32 local_ip6[4];	/* Stored in network byte order */
	__u32 remote_port;	/* Stored in network byte order */
	__u32 local_port;	/* stored in host byte order */
	/* ... here. */

	__u32 data_meta;
	__bpf_md_ptr(struct bpf_flow_keys *, flow_keys);
	__u64 tstamp;
	__u32 wire_len;
	__u32 gso_segs;
	__bpf_md_ptr(struct bpf_sock *, sk);
	__u32 gso_size;
};

struct bpf_tunnel_key {
	__u32 tunnel_id;
	union {
		__u32 remote_ipv4;
		__u32 remote_ipv6[4];
	};
	__u8 tunnel_tos;
	__u8 tunnel_ttl;
	__u16 tunnel_ext;	/* Padding, future use. */
	__u32 tunnel_label;
};

/* user accessible mirror of in-kernel xfrm_state.
 * new fields can only be added to the end of this structure
 */
struct bpf_xfrm_state {
	__u32 reqid;
	__u32 spi;	/* Stored in network byte order */
	__u16 family;
	__u16 ext;	/* Padding, future use. */
	union {
		__u32 remote_ipv4;	/* Stored in network byte order */
		__u32 remote_ipv6[4];	/* Stored in network byte order */
	};
};

/* Generic BPF return codes which all BPF program types may support.
 * The values are binary compatible with their TC_ACT_* counter-part to
 * provide backwards compatibility with existing SCHED_CLS and SCHED_ACT
 * programs.
 *
 * XDP is handled seprately, see XDP_*.
 */
enum bpf_ret_code {
	BPF_OK = 0,
	/* 1 reserved */
	BPF_DROP = 2,
	/* 3-6 reserved */
	BPF_REDIRECT = 7,
	/* >127 are reserved for prog type specific return codes.
	 *
	 * BPF_LWT_REROUTE: used by BPF_PROG_TYPE_LWT_IN and
	 *    BPF_PROG_TYPE_LWT_XMIT to indicate that skb had been
	 *    changed and should be routed based on its new L3 header.
	 *    (This is an L3 redirect, as opposed to L2 redirect
	 *    represented by BPF_REDIRECT above).
	 */
	BPF_LWT_REROUTE = 128,
};

struct bpf_sock {
	__u32 bound_dev_if;
	__u32 family;
	__u32 type;
	__u32 protocol;
	__u32 mark;
	__u32 priority;
	/* IP address also allows 1 and 2 bytes access */
	__u32 src_ip4;
	__u32 src_ip6[4];
	__u32 src_port;		/* host byte order */
	__u32 dst_port;		/* network byte order */
	__u32 dst_ip4;
	__u32 dst_ip6[4];
	__u32 state;
	__s32 rx_queue_mapping;
};

struct bpf_tcp_sock {
	__u32 snd_cwnd;		/* Sending congestion window		*/
	__u32 srtt_us;		/* smoothed round trip time << 3 in usecs */
	__u32 rtt_min;
	__u32 snd_ssthresh;	/* Slow start size threshold		*/
	__u32 rcv_nxt;		/* What we want to receive next		*/
	__u32 snd_nxt;		/* Next sequence we send		*/
	__u32 snd_una;		/* First byte we want an ack for	*/
	__u32 mss_cache;	/* Cached effective mss, not including SACKS */
	__u32 ecn_flags;	/* ECN status bits.			*/
	__u32 rate_delivered;	/* saved rate sample: packets delivered */
	__u32 rate_interval_us;	/* saved rate sample: time elapsed */
	__u32 packets_out;	/* Packets which are "in flight"	*/
	__u32 retrans_out;	/* Retransmitted packets out		*/
	__u32 total_retrans;	/* Total retransmits for entire connection */
	__u32 segs_in;		/* RFC4898 tcpEStatsPerfSegsIn
				 * total number of segments in.
				 */
	__u32 data_segs_in;	/* RFC4898 tcpEStatsPerfDataSegsIn
				 * total number of data segments in.
				 */
	__u32 segs_out;		/* RFC4898 tcpEStatsPerfSegsOut
				 * The total number of segments sent.
				 */
	__u32 data_segs_out;	/* RFC4898 tcpEStatsPerfDataSegsOut
				 * total number of data segments sent.
				 */
	__u32 lost_out;		/* Lost packets			*/
	__u32 sacked_out;	/* SACK'd packets			*/
	__u64 bytes_received;	/* RFC4898 tcpEStatsAppHCThruOctetsReceived
				 * sum(delta(rcv_nxt)), or how many bytes
				 * were acked.
				 */
	__u64 bytes_acked;	/* RFC4898 tcpEStatsAppHCThruOctetsAcked
				 * sum(delta(snd_una)), or how many bytes
				 * were acked.
				 */
	__u32 dsack_dups;	/* RFC4898 tcpEStatsStackDSACKDups
				 * total number of DSACK blocks received
				 */
	__u32 delivered;	/* Total data packets delivered incl. rexmits */
	__u32 delivered_ce;	/* Like the above but only ECE marked packets */
	__u32 icsk_retransmits;	/* Number of unrecovered [RTO] timeouts */
};

struct bpf_sock_tuple {
	union {
		struct {
			__be32 saddr;
			__be32 daddr;
			__be16 sport;
			__be16 dport;
		} ipv4;
		struct {
			__be32 saddr[4];
			__be32 daddr[4];
			__be16 sport;
			__be16 dport;
		} ipv6;
	};
};

struct bpf_xdp_sock {
	__u32 queue_id;
};

#define XDP_PACKET_HEADROOM 256

/* User return codes for XDP prog type.
 * A valid XDP program must return one of these defined values. All other
 * return codes are reserved for future use. Unknown return codes will
 * result in packet drops and a warning via bpf_warn_invalid_xdp_action().
 */
enum xdp_action {
	XDP_ABORTED = 0,
	XDP_DROP,
	XDP_PASS,
	XDP_TX,
	XDP_REDIRECT,
};

/* user accessible metadata for XDP packet hook
 * new fields must be added to the end of this structure
 */
struct xdp_md {
	__u32 data;
	__u32 data_end;
	__u32 data_meta;
	/* Below access go through struct xdp_rxq_info */
	__u32 ingress_ifindex; /* rxq->dev->ifindex */
	__u32 rx_queue_index;  /* rxq->queue_index  */

	__u32 egress_ifindex;  /* txq->dev->ifindex */
};

/* DEVMAP map-value layout
 *
 * The struct data-layout of map-value is a configuration interface.
 * New members can only be added to the end of this structure.
 */
struct bpf_devmap_val {
	__u32 ifindex;   /* device index */
	union {
		int   fd;  /* prog fd on map write */
		__u32 id;  /* prog id on map read */
	} bpf_prog;
};

/* CPUMAP map-value layout
 *
 * The struct data-layout of map-value is a configuration interface.
 * New members can only be added to the end of this structure.
 */
struct bpf_cpumap_val {
	__u32 qsize;	/* queue size to remote target CPU */
	union {
		int   fd;	/* prog fd on map write */
		__u32 id;	/* prog id on map read */
	} bpf_prog;
};

enum sk_action {
	SK_DROP = 0,
	SK_PASS,
};

/* user accessible metadata for SK_MSG packet hook, new fields must
 * be added to the end of this structure
 */
struct sk_msg_md {
	__bpf_md_ptr(void *, data);
	__bpf_md_ptr(void *, data_end);

	__u32 family;
	__u32 remote_ip4;	/* Stored in network byte order */
	__u32 local_ip4;	/* Stored in network byte order */
	__u32 remote_ip6[4];	/* Stored in network byte order */
	__u32 local_ip6[4];	/* Stored in network byte order */
	__u32 remote_port;	/* Stored in network byte order */
	__u32 local_port;	/* stored in host byte order */
	__u32 size;		/* Total size of sk_msg */

	__bpf_md_ptr(struct bpf_sock *, sk); /* current socket */
};

struct sk_reuseport_md {
	/*
	 * Start of directly accessible data. It begins from
	 * the tcp/udp header.
	 */
	__bpf_md_ptr(void *, data);
	/* End of directly accessible data */
	__bpf_md_ptr(void *, data_end);
	/*
	 * Total length of packet (starting from the tcp/udp header).
	 * Note that the directly accessible bytes (data_end - data)
	 * could be less than this "len".  Those bytes could be
	 * indirectly read by a helper "bpf_skb_load_bytes()".
	 */
	__u32 len;
	/*
	 * Eth protocol in the mac header (network byte order). e.g.
	 * ETH_P_IP(0x0800) and ETH_P_IPV6(0x86DD)
	 */
	__u32 eth_protocol;
	__u32 ip_protocol;	/* IP protocol. e.g. IPPROTO_TCP, IPPROTO_UDP */
	__u32 bind_inany;	/* Is sock bound to an INANY address? */
	__u32 hash;		/* A hash of the packet 4 tuples */
};

#define BPF_TAG_SIZE	8

struct bpf_prog_info {
	__u32 type;
	__u32 id;
	__u8  tag[BPF_TAG_SIZE];
	__u32 jited_prog_len;
	__u32 xlated_prog_len;
	__aligned_u64 jited_prog_insns;
	__aligned_u64 xlated_prog_insns;
	__u64 load_time;	/* ns since boottime */
	__u32 created_by_uid;
	__u32 nr_map_ids;
	__aligned_u64 map_ids;
	char name[BPF_OBJ_NAME_LEN];
	__u32 ifindex;
	__u32 gpl_compatible:1;
	__u32 :31; /* alignment pad */
	__u64 netns_dev;
	__u64 netns_ino;
	__u32 nr_jited_ksyms;
	__u32 nr_jited_func_lens;
	__aligned_u64 jited_ksyms;
	__aligned_u64 jited_func_lens;
	__u32 btf_id;
	__u32 func_info_rec_size;
	__aligned_u64 func_info;
	__u32 nr_func_info;
	__u32 nr_line_info;
	__aligned_u64 line_info;
	__aligned_u64 jited_line_info;
	__u32 nr_jited_line_info;
	__u32 line_info_rec_size;
	__u32 jited_line_info_rec_size;
	__u32 nr_prog_tags;
	__aligned_u64 prog_tags;
	__u64 run_time_ns;
	__u64 run_cnt;
} __attribute__((aligned(8)));

struct bpf_map_info {
	__u32 type;
	__u32 id;
	__u32 key_size;
	__u32 value_size;
	__u32 max_entries;
	__u32 map_flags;
	char  name[BPF_OBJ_NAME_LEN];
	__u32 ifindex;
	__u32 btf_vmlinux_value_type_id;
	__u64 netns_dev;
	__u64 netns_ino;
	__u32 btf_id;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
} __attribute__((aligned(8)));

struct bpf_btf_info {
	__aligned_u64 btf;
	__u32 btf_size;
	__u32 id;
} __attribute__((aligned(8)));

struct bpf_link_info {
	__u32 type;
	__u32 id;
	__u32 prog_id;
	union {
		struct {
			__aligned_u64 tp_name; /* in/out: tp_name buffer ptr */
			__u32 tp_name_len;     /* in/out: tp_name buffer len */
		} raw_tracepoint;
		struct {
			__u32 attach_type;
		} tracing;
		struct {
			__u64 cgroup_id;
			__u32 attach_type;
		} cgroup;
		struct {
			__aligned_u64 target_name; /* in/out: target_name buffer ptr */
			__u32 target_name_len;	   /* in/out: target_name buffer len */
			union {
				struct {
					__u32 map_id;
				} map;
			};
		} iter;
		struct  {
			__u32 netns_ino;
			__u32 attach_type;
		} netns;
		struct {
			__u32 ifindex;
		} xdp;
	};
} __attribute__((aligned(8)));

/* User bpf_sock_addr struct to access socket fields and sockaddr struct passed
 * by user and intended to be used by socket (e.g. to bind to, depends on
 * attach type).
 */
struct bpf_sock_addr {
	__u32 user_family;	/* Allows 4-byte read, but no write. */
	__u32 user_ip4;		/* Allows 1,2,4-byte read and 4-byte write.
				 * Stored in network byte order.
				 */
	__u32 user_ip6[4];	/* Allows 1,2,4,8-byte read and 4,8-byte write.
				 * Stored in network byte order.
				 */
	__u32 user_port;	/* Allows 1,2,4-byte read and 4-byte write.
				 * Stored in network byte order
				 */
	__u32 family;		/* Allows 4-byte read, but no write */
	__u32 type;		/* Allows 4-byte read, but no write */
	__u32 protocol;		/* Allows 4-byte read, but no write */
	__u32 msg_src_ip4;	/* Allows 1,2,4-byte read and 4-byte write.
				 * Stored in network byte order.
				 */
	__u32 msg_src_ip6[4];	/* Allows 1,2,4,8-byte read and 4,8-byte write.
				 * Stored in network byte order.
				 */
	__bpf_md_ptr(struct bpf_sock *, sk);
};

/* User bpf_sock_ops struct to access socket values and specify request ops
 * and their replies.
 * Some of this fields are in network (bigendian) byte order and may need
 * to be converted before use (bpf_ntohl() defined in samples/bpf/bpf_endian.h).
 * New fields can only be added at the end of this structure
 */
struct bpf_sock_ops {
	__u32 op;
	union {
		__u32 args[4];		/* Optionally passed to bpf program */
		__u32 reply;		/* Returned by bpf program	    */
		__u32 replylong[4];	/* Optionally returned by bpf prog  */
	};
	__u32 family;
	__u32 remote_ip4;	/* Stored in network byte order */
	__u32 local_ip4;	/* Stored in network byte order */
	__u32 remote_ip6[4];	/* Stored in network byte order */
	__u32 local_ip6[4];	/* Stored in network byte order */
	__u32 remote_port;	/* Stored in network byte order */
	__u32 local_port;	/* stored in host byte order */
	__u32 is_fullsock;	/* Some TCP fields are only valid if
				 * there is a full socket. If not, the
				 * fields read as zero.
				 */
	__u32 snd_cwnd;
	__u32 srtt_us;		/* Averaged RTT << 3 in usecs */
	__u32 bpf_sock_ops_cb_flags; /* flags defined in uapi/linux/tcp.h */
	__u32 state;
	__u32 rtt_min;
	__u32 snd_ssthresh;
	__u32 rcv_nxt;
	__u32 snd_nxt;
	__u32 snd_una;
	__u32 mss_cache;
	__u32 ecn_flags;
	__u32 rate_delivered;
	__u32 rate_interval_us;
	__u32 packets_out;
	__u32 retrans_out;
	__u32 total_retrans;
	__u32 segs_in;
	__u32 data_segs_in;
	__u32 segs_out;
	__u32 data_segs_out;
	__u32 lost_out;
	__u32 sacked_out;
	__u32 sk_txhash;
	__u64 bytes_received;
	__u64 bytes_acked;
	__bpf_md_ptr(struct bpf_sock *, sk);
	/* [skb_data, skb_data_end) covers the whole TCP header.
	 *
	 * BPF_SOCK_OPS_PARSE_HDR_OPT_CB: The packet received
	 * BPF_SOCK_OPS_HDR_OPT_LEN_CB:   Not useful because the
	 *                                header has not been written.
	 * BPF_SOCK_OPS_WRITE_HDR_OPT_CB: The header and options have
	 *				  been written so far.
	 * BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:  The SYNACK that concludes
	 *					the 3WHS.
	 * BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB: The ACK that concludes
	 *					the 3WHS.
	 *
	 * bpf_load_hdr_opt() can also be used to read a particular option.
	 */
	__bpf_md_ptr(void *, skb_data);
	__bpf_md_ptr(void *, skb_data_end);
	__u32 skb_len;		/* The total length of a packet.
				 * It includes the header, options,
				 * and payload.
				 */
	__u32 skb_tcp_flags;	/* tcp_flags of the header.  It provides
				 * an easy way to check for tcp_flags
				 * without parsing skb_data.
				 *
				 * In particular, the skb_tcp_flags
				 * will still be available in
				 * BPF_SOCK_OPS_HDR_OPT_LEN even though
				 * the outgoing header has not
				 * been written yet.
				 */
};

/* Definitions for bpf_sock_ops_cb_flags */
enum {
	BPF_SOCK_OPS_RTO_CB_FLAG	= (1<<0),
	BPF_SOCK_OPS_RETRANS_CB_FLAG	= (1<<1),
	BPF_SOCK_OPS_STATE_CB_FLAG	= (1<<2),
	BPF_SOCK_OPS_RTT_CB_FLAG	= (1<<3),
	/* Call bpf for all received TCP headers.  The bpf prog will be
	 * called under sock_ops->op == BPF_SOCK_OPS_PARSE_HDR_OPT_CB
	 *
	 * Please refer to the comment in BPF_SOCK_OPS_PARSE_HDR_OPT_CB
	 * for the header option related helpers that will be useful
	 * to the bpf programs.
	 *
	 * It could be used at the client/active side (i.e. connect() side)
	 * when the server told it that the server was in syncookie
	 * mode and required the active side to resend the bpf-written
	 * options.  The active side can keep writing the bpf-options until
	 * it received a valid packet from the server side to confirm
	 * the earlier packet (and options) has been received.  The later
	 * example patch is using it like this at the active side when the
	 * server is in syncookie mode.
	 *
	 * The bpf prog will usually turn this off in the common cases.
	 */
	BPF_SOCK_OPS_PARSE_ALL_HDR_OPT_CB_FLAG	= (1<<4),
	/* Call bpf when kernel has received a header option that
	 * the kernel cannot handle.  The bpf prog will be called under
	 * sock_ops->op == BPF_SOCK_OPS_PARSE_HDR_OPT_CB.
	 *
	 * Please refer to the comment in BPF_SOCK_OPS_PARSE_HDR_OPT_CB
	 * for the header option related helpers that will be useful
	 * to the bpf programs.
	 */
	BPF_SOCK_OPS_PARSE_UNKNOWN_HDR_OPT_CB_FLAG = (1<<5),
	/* Call bpf when the kernel is writing header options for the
	 * outgoing packet.  The bpf prog will first be called
	 * to reserve space in a skb under
	 * sock_ops->op == BPF_SOCK_OPS_HDR_OPT_LEN_CB.  Then
	 * the bpf prog will be called to write the header option(s)
	 * under sock_ops->op == BPF_SOCK_OPS_WRITE_HDR_OPT_CB.
	 *
	 * Please refer to the comment in BPF_SOCK_OPS_HDR_OPT_LEN_CB
	 * and BPF_SOCK_OPS_WRITE_HDR_OPT_CB for the header option
	 * related helpers that will be useful to the bpf programs.
	 *
	 * The kernel gets its chance to reserve space and write
	 * options first before the BPF program does.
	 */
	BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG = (1<<6),
/* Mask of all currently supported cb flags */
	BPF_SOCK_OPS_ALL_CB_FLAGS       = 0x7F,
};

/* List of known BPF sock_ops operators.
 * New entries can only be added at the end
 */
enum {
	BPF_SOCK_OPS_VOID,
	BPF_SOCK_OPS_TIMEOUT_INIT,	/* Should return SYN-RTO value to use or
					 * -1 if default value should be used
					 */
	BPF_SOCK_OPS_RWND_INIT,		/* Should return initial advertized
					 * window (in packets) or -1 if default
					 * value should be used
					 */
	BPF_SOCK_OPS_TCP_CONNECT_CB,	/* Calls BPF program right before an
					 * active connection is initialized
					 */
	BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB,	/* Calls BPF program when an
						 * active connection is
						 * established
						 */
	BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB,	/* Calls BPF program when a
						 * passive connection is
						 * established
						 */
	BPF_SOCK_OPS_NEEDS_ECN,		/* If connection's congestion control
					 * needs ECN
					 */
	BPF_SOCK_OPS_BASE_RTT,		/* Get base RTT. The correct value is
					 * based on the path and may be
					 * dependent on the congestion control
					 * algorithm. In general it indicates
					 * a congestion threshold. RTTs above
					 * this indicate congestion
					 */
	BPF_SOCK_OPS_RTO_CB,		/* Called when an RTO has triggered.
					 * Arg1: value of icsk_retransmits
					 * Arg2: value of icsk_rto
					 * Arg3: whether RTO has expired
					 */
	BPF_SOCK_OPS_RETRANS_CB,	/* Called when skb is retransmitted.
					 * Arg1: sequence number of 1st byte
					 * Arg2: # segments
					 * Arg3: return value of
					 *       tcp_transmit_skb (0 => success)
					 */
	BPF_SOCK_OPS_STATE_CB,		/* Called when TCP changes state.
					 * Arg1: old_state
					 * Arg2: new_state
					 */
	BPF_SOCK_OPS_TCP_LISTEN_CB,	/* Called on listen(2), right after
					 * socket transition to LISTEN state.
					 */
	BPF_SOCK_OPS_RTT_CB,		/* Called on every RTT.
					 */
	BPF_SOCK_OPS_PARSE_HDR_OPT_CB,	/* Parse the header option.
					 * It will be called to handle
					 * the packets received at
					 * an already established
					 * connection.
					 *
					 * sock_ops->skb_data:
					 * Referring to the received skb.
					 * It covers the TCP header only.
					 *
					 * bpf_load_hdr_opt() can also
					 * be used to search for a
					 * particular option.
					 */
	BPF_SOCK_OPS_HDR_OPT_LEN_CB,	/* Reserve space for writing the
					 * header option later in
					 * BPF_SOCK_OPS_WRITE_HDR_OPT_CB.
					 * Arg1: bool want_cookie. (in
					 *       writing SYNACK only)
					 *
					 * sock_ops->skb_data:
					 * Not available because no header has
					 * been	written yet.
					 *
					 * sock_ops->skb_tcp_flags:
					 * The tcp_flags of the
					 * outgoing skb. (e.g. SYN, ACK, FIN).
					 *
					 * bpf_reserve_hdr_opt() should
					 * be used to reserve space.
					 */
	BPF_SOCK_OPS_WRITE_HDR_OPT_CB,	/* Write the header options
					 * Arg1: bool want_cookie. (in
					 *       writing SYNACK only)
					 *
					 * sock_ops->skb_data:
					 * Referring to the outgoing skb.
					 * It covers the TCP header
					 * that has already been written
					 * by the kernel and the
					 * earlier bpf-progs.
					 *
					 * sock_ops->skb_tcp_flags:
					 * The tcp_flags of the outgoing
					 * skb. (e.g. SYN, ACK, FIN).
					 *
					 * bpf_store_hdr_opt() should
					 * be used to write the
					 * option.
					 *
					 * bpf_load_hdr_opt() can also
					 * be used to search for a
					 * particular option that
					 * has already been written
					 * by the kernel or the
					 * earlier bpf-progs.
					 */
};

/* List of TCP states. There is a build check in net/ipv4/tcp.c to detect
 * changes between the TCP and BPF versions. Ideally this should never happen.
 * If it does, we need to add code to convert them before calling
 * the BPF sock_ops function.
 */
enum {
	BPF_TCP_ESTABLISHED = 1,
	BPF_TCP_SYN_SENT,
	BPF_TCP_SYN_RECV,
	BPF_TCP_FIN_WAIT1,
	BPF_TCP_FIN_WAIT2,
	BPF_TCP_TIME_WAIT,
	BPF_TCP_CLOSE,
	BPF_TCP_CLOSE_WAIT,
	BPF_TCP_LAST_ACK,
	BPF_TCP_LISTEN,
	BPF_TCP_CLOSING,	/* Now a valid state */
	BPF_TCP_NEW_SYN_RECV,

	BPF_TCP_MAX_STATES	/* Leave at the end! */
};

enum {
	TCP_BPF_IW		= 1001,	/* Set TCP initial congestion window */
	TCP_BPF_SNDCWND_CLAMP	= 1002,	/* Set sndcwnd_clamp */
	TCP_BPF_DELACK_MAX	= 1003, /* Max delay ack in usecs */
	TCP_BPF_RTO_MIN		= 1004, /* Min delay ack in usecs */
	/* Copy the SYN pkt to optval
	 *
	 * BPF_PROG_TYPE_SOCK_OPS only.  It is similar to the
	 * bpf_getsockopt(TCP_SAVED_SYN) but it does not limit
	 * to only getting from the saved_syn.  It can either get the
	 * syn packet from:
	 *
	 * 1. the just-received SYN packet (only available when writing the
	 *    SYNACK).  It will be useful when it is not necessary to
	 *    save the SYN packet for latter use.  It is also the only way
	 *    to get the SYN during syncookie mode because the syn
	 *    packet cannot be saved during syncookie.
	 *
	 * OR
	 *
	 * 2. the earlier saved syn which was done by
	 *    bpf_setsockopt(TCP_SAVE_SYN).
	 *
	 * The bpf_getsockopt(TCP_BPF_SYN*) option will hide where the
	 * SYN packet is obtained.
	 *
	 * If the bpf-prog does not need the IP[46] header,  the
	 * bpf-prog can avoid parsing the IP header by using
	 * TCP_BPF_SYN.  Otherwise, the bpf-prog can get both
	 * IP[46] and TCP header by using TCP_BPF_SYN_IP.
	 *
	 *      >0: Total number of bytes copied
	 * -ENOSPC: Not enough space in optval. Only optlen number of
	 *          bytes is copied.
	 * -ENOENT: The SYN skb is not available now and the earlier SYN pkt
	 *	    is not saved by setsockopt(TCP_SAVE_SYN).
	 */
	TCP_BPF_SYN		= 1005, /* Copy the TCP header */
	TCP_BPF_SYN_IP		= 1006, /* Copy the IP[46] and TCP header */
	TCP_BPF_SYN_MAC         = 1007, /* Copy the MAC, IP[46], and TCP header */
};

enum {
	BPF_LOAD_HDR_OPT_TCP_SYN = (1ULL << 0),
};

/* args[0] value during BPF_SOCK_OPS_HDR_OPT_LEN_CB and
 * BPF_SOCK_OPS_WRITE_HDR_OPT_CB.
 */
enum {
	BPF_WRITE_HDR_TCP_CURRENT_MSS = 1,	/* Kernel is finding the
						 * total option spaces
						 * required for an established
						 * sk in order to calculate the
						 * MSS.  No skb is actually
						 * sent.
						 */
	BPF_WRITE_HDR_TCP_SYNACK_COOKIE = 2,	/* Kernel is in syncookie mode
						 * when sending a SYN.
						 */
};

struct bpf_perf_event_value {
	__u64 counter;
	__u64 enabled;
	__u64 running;
};

enum {
	BPF_DEVCG_ACC_MKNOD	= (1ULL << 0),
	BPF_DEVCG_ACC_READ	= (1ULL << 1),
	BPF_DEVCG_ACC_WRITE	= (1ULL << 2),
};

enum {
	BPF_DEVCG_DEV_BLOCK	= (1ULL << 0),
	BPF_DEVCG_DEV_CHAR	= (1ULL << 1),
};

struct bpf_cgroup_dev_ctx {
	/* access_type encoded as (BPF_DEVCG_ACC_* << 16) | BPF_DEVCG_DEV_* */
	__u32 access_type;
	__u32 major;
	__u32 minor;
};

struct bpf_raw_tracepoint_args {
	__u64 args[0];
};

/* DIRECT:  Skip the FIB rules and go to FIB table associated with device
 * OUTPUT:  Do lookup from egress perspective; default is ingress
 */
enum {
	BPF_FIB_LOOKUP_DIRECT  = (1U << 0),
	BPF_FIB_LOOKUP_OUTPUT  = (1U << 1),
};

enum {
	BPF_FIB_LKUP_RET_SUCCESS,      /* lookup successful */
	BPF_FIB_LKUP_RET_BLACKHOLE,    /* dest is blackholed; can be dropped */
	BPF_FIB_LKUP_RET_UNREACHABLE,  /* dest is unreachable; can be dropped */
	BPF_FIB_LKUP_RET_PROHIBIT,     /* dest not allowed; can be dropped */
	BPF_FIB_LKUP_RET_NOT_FWDED,    /* packet is not forwarded */
	BPF_FIB_LKUP_RET_FWD_DISABLED, /* fwding is not enabled on ingress */
	BPF_FIB_LKUP_RET_UNSUPP_LWT,   /* fwd requires encapsulation */
	BPF_FIB_LKUP_RET_NO_NEIGH,     /* no neighbor entry for nh */
	BPF_FIB_LKUP_RET_FRAG_NEEDED,  /* fragmentation required to fwd */
};

struct bpf_fib_lookup {
	/* input:  network family for lookup (AF_INET, AF_INET6)
	 * output: network family of egress nexthop
	 */
	__u8	family;

	/* set if lookup is to consider L4 data - e.g., FIB rules */
	__u8	l4_protocol;
	__be16	sport;
	__be16	dport;

	/* total length of packet from network header - used for MTU check */
	__u16	tot_len;

	/* input: L3 device index for lookup
	 * output: device index from FIB lookup
	 */
	__u32	ifindex;

	union {
		/* inputs to lookup */
		__u8	tos;		/* AF_INET  */
		__be32	flowinfo;	/* AF_INET6, flow_label + priority */

		/* output: metric of fib result (IPv4/IPv6 only) */
		__u32	rt_metric;
	};

	union {
		__be32		ipv4_src;
		__u32		ipv6_src[4];  /* in6_addr; network order */
	};

	/* input to bpf_fib_lookup, ipv{4,6}_dst is destination address in
	 * network header. output: bpf_fib_lookup sets to gateway address
	 * if FIB lookup returns gateway route
	 */
	union {
		__be32		ipv4_dst;
		__u32		ipv6_dst[4];  /* in6_addr; network order */
	};

	/* output */
	__be16	h_vlan_proto;
	__be16	h_vlan_TCI;
	__u8	smac[6];     /* ETH_ALEN */
	__u8	dmac[6];     /* ETH_ALEN */
};

enum bpf_task_fd_type {
	BPF_FD_TYPE_RAW_TRACEPOINT,	/* tp name */
	BPF_FD_TYPE_TRACEPOINT,		/* tp name */
	BPF_FD_TYPE_KPROBE,		/* (symbol + offset) or addr */
	BPF_FD_TYPE_KRETPROBE,		/* (symbol + offset) or addr */
	BPF_FD_TYPE_UPROBE,		/* filename + offset */
	BPF_FD_TYPE_URETPROBE,		/* filename + offset */
};

enum {
	BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG		= (1U << 0),
	BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL		= (1U << 1),
	BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP		= (1U << 2),
};

struct bpf_flow_keys {
	__u16	nhoff;
	__u16	thoff;
	__u16	addr_proto;			/* ETH_P_* of valid addrs */
	__u8	is_frag;
	__u8	is_first_frag;
	__u8	is_encap;
	__u8	ip_proto;
	__be16	n_proto;
	__be16	sport;
	__be16	dport;
	union {
		struct {
			__be32	ipv4_src;
			__be32	ipv4_dst;
		};
		struct {
			__u32	ipv6_src[4];	/* in6_addr; network order */
			__u32	ipv6_dst[4];	/* in6_addr; network order */
		};
	};
	__u32	flags;
	__be32	flow_label;
};

struct bpf_func_info {
	__u32	insn_off;
	__u32	type_id;
};

#define BPF_LINE_INFO_LINE_NUM(line_col)	((line_col) >> 10)
#define BPF_LINE_INFO_LINE_COL(line_col)	((line_col) & 0x3ff)

struct bpf_line_info {
	__u32	insn_off;
	__u32	file_name_off;
	__u32	line_off;
	__u32	line_col;
};

struct bpf_spin_lock {
	__u32	val;
};

struct bpf_sysctl {
	__u32	write;		/* Sysctl is being read (= 0) or written (= 1).
				 * Allows 1,2,4-byte read, but no write.
				 */
	__u32	file_pos;	/* Sysctl file position to read from, write to.
				 * Allows 1,2,4-byte read an 4-byte write.
				 */
};

struct bpf_sockopt {
	__bpf_md_ptr(struct bpf_sock *, sk);
	__bpf_md_ptr(void *, optval);
	__bpf_md_ptr(void *, optval_end);

	__s32	level;
	__s32	optname;
	__s32	optlen;
	__s32	retval;
};

struct bpf_pidns_info {
	__u32 pid;
	__u32 tgid;
};

/* User accessible data for SK_LOOKUP programs. Add new fields at the end. */
struct bpf_sk_lookup {
	__bpf_md_ptr(struct bpf_sock *, sk); /* Selected socket */

	__u32 family;		/* Protocol family (AF_INET, AF_INET6) */
	__u32 protocol;		/* IP protocol (IPPROTO_TCP, IPPROTO_UDP) */
	__u32 remote_ip4;	/* Network byte order */
	__u32 remote_ip6[4];	/* Network byte order */
	__u32 remote_port;	/* Network byte order */
	__u32 local_ip4;	/* Network byte order */
	__u32 local_ip6[4];	/* Network byte order */
	__u32 local_port;	/* Host byte order */
};

/*
 * struct btf_ptr is used for typed pointer representation; the
 * type id is used to render the pointer data as the appropriate type
 * via the bpf_snprintf_btf() helper described above.  A flags field -
 * potentially to specify additional details about the BTF pointer
 * (rather than its mode of display) - is included for future use.
 * Display flags - BTF_F_* - are passed to bpf_snprintf_btf separately.
 */
struct btf_ptr {
	void *ptr;
	__u32 type_id;
	__u32 flags;		/* BTF ptr flags; unused at present. */
};

/*
 * Flags to control bpf_snprintf_btf() behaviour.
 *     - BTF_F_COMPACT: no formatting around type information
 *     - BTF_F_NONAME: no struct/union member names/types
 *     - BTF_F_PTR_RAW: show raw (unobfuscated) pointer values;
 *       equivalent to %px.
 *     - BTF_F_ZERO: show zero-valued struct/union members; they
 *       are not displayed by default
 */
enum {
	BTF_F_COMPACT	=	(1ULL << 0),
	BTF_F_NONAME	=	(1ULL << 1),
	BTF_F_PTR_RAW	=	(1ULL << 2),
	BTF_F_ZERO	=	(1ULL << 3),
};

#endif /* _UAPI__LINUX_BPF_H__ */
