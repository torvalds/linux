/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#ifndef _BPF_TESTMOD_H
#define _BPF_TESTMOD_H

#include <linux/types.h>

struct task_struct;
struct cgroup;

struct bpf_testmod_test_read_ctx {
	char *buf;
	loff_t off;
	size_t len;
};

struct bpf_testmod_test_write_ctx {
	char *buf;
	loff_t off;
	size_t len;
};

struct bpf_testmod_test_writable_ctx {
	bool early_ret;
	int val;
};

/* BPF iter that returns *value* *n* times in a row */
struct bpf_iter_testmod_seq {
	s64 value;
	int cnt;
};

struct bpf_testmod_ops {
	int (*test_1)(void);
	void (*test_2)(int a, int b);
	/* Used to test nullable arguments. */
	int (*test_maybe_null)(int dummy, struct task_struct *task);
	int (*unsupported_ops)(void);
	/* Used to test ref_acquired arguments. */
	int (*test_refcounted)(int dummy, struct task_struct *task);
	/* Used to test checking of __ref arguments when it not the first argument. */
	int (*test_refcounted_multi)(int dummy, struct task_struct *task,
				     struct task_struct *task2);
	/* Used to test returning referenced kptr. */
	struct task_struct *(*test_return_ref_kptr)(int dummy, struct task_struct *task,
						    struct cgroup *cgrp);

	/* Used to test indirect struct_ops trampoline with stack-passed arguments. */
	int (*test_trampoline_stack_args)(int arg1, int arg2, int arg3,
					  int arg4, int arg5, int arg6,
					  int arg7, int arg8, int arg9);

	/* The following fields are used to test shadow copies. */
	char onebyte;
	struct {
		int a;
		int b;
	} unsupported;
	int data;

	/* The following pointers are used to test the maps having multiple
	 * pages of trampolines.
	 */
	int (*tramp_1)(int value);
	int (*tramp_2)(int value);
	int (*tramp_3)(int value);
	int (*tramp_4)(int value);
	int (*tramp_5)(int value);
	int (*tramp_6)(int value);
	int (*tramp_7)(int value);
	int (*tramp_8)(int value);
	int (*tramp_9)(int value);
	int (*tramp_10)(int value);
	int (*tramp_11)(int value);
	int (*tramp_12)(int value);
	int (*tramp_13)(int value);
	int (*tramp_14)(int value);
	int (*tramp_15)(int value);
	int (*tramp_16)(int value);
	int (*tramp_17)(int value);
	int (*tramp_18)(int value);
	int (*tramp_19)(int value);
	int (*tramp_20)(int value);
	int (*tramp_21)(int value);
	int (*tramp_22)(int value);
	int (*tramp_23)(int value);
	int (*tramp_24)(int value);
	int (*tramp_25)(int value);
	int (*tramp_26)(int value);
	int (*tramp_27)(int value);
	int (*tramp_28)(int value);
	int (*tramp_29)(int value);
	int (*tramp_30)(int value);
	int (*tramp_31)(int value);
	int (*tramp_32)(int value);
	int (*tramp_33)(int value);
	int (*tramp_34)(int value);
	int (*tramp_35)(int value);
	int (*tramp_36)(int value);
	int (*tramp_37)(int value);
	int (*tramp_38)(int value);
	int (*tramp_39)(int value);
	int (*tramp_40)(int value);
};

struct bpf_testmod_ops2 {
	int (*test_1)(void);
};

/* 16 bytes, so it takes two argument slots when passed by value */
struct bpf_testmod_arena_pair {
	u64 a;
	u64 b;
};

/*
 * The qualifiers are BTF type tags for the verifier. The module gets them
 * from bpf_compiler_types.h, user space and BPF programs need them empty.
 */
#ifdef __KERNEL__
#include <linux/bpf_compiler_types.h>
#endif
#ifndef __nullable
#define __nullable
#endif
#ifndef __percpu
#define __percpu
#endif

struct bpf_testmod_pcpu {
	int val;
};

/* Arguments of test_nullable(), trusted through the __safe_trusted alias in the module */
struct bpf_testmod_nullable_args {
	/* never NULL */
	struct task_struct *task;
	/* NULL on the first call, set on the second */
	struct task_struct __nullable *task_or_null;
	/* NULL on the first call, a per-CPU variable on the second */
	struct bpf_testmod_pcpu __percpu __nullable *pcpu_or_null;
	/* NULL on the first call, set on the second */
	int __nullable *int_or_null;
	/* this struct again, through a plain pointer, which the verifier probes */
	struct bpf_testmod_nullable_args *self;
};

struct bpf_testmod_ops3 {
	int (*test_1)(void);
	int (*test_2)(void);
	/* Used to test arena pointer arguments. */
	int (*test_arena)(u64 *ptr);
	int (*test_arena_nullable)(u64 *ptr);
	/* enough leading args to force @ptr onto the stack on x86 and arm64 */
	int (*test_arena_stack)(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f,
				u64 g, u64 h, u64 *ptr);
	/* a multi-slot leading arg, so @ptr is not at the slot its arg index suggests */
	int (*test_arena_multislot)(struct bpf_testmod_arena_pair p, u64 *ptr);
	/* Used to test __nullable pointer members. */
	int (*test_nullable)(struct bpf_testmod_nullable_args *args);
};

struct st_ops_args {
	u64 a;
};

struct bpf_testmod_st_ops {
	int (*test_prologue)(struct st_ops_args *args);
	int (*test_epilogue)(struct st_ops_args *args);
	int (*test_pro_epilogue)(struct st_ops_args *args);
	struct module *owner;
};

struct bpf_testmod_multi_st_ops {
	int (*test_1)(struct st_ops_args *args);
	struct hlist_node node;
	int id;
};

#endif /* _BPF_TESTMOD_H */
