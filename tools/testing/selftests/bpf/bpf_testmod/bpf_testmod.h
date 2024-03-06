/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#ifndef _BPF_TESTMOD_H
#define _BPF_TESTMOD_H

#include <linux/types.h>

struct task_struct;

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

#endif /* _BPF_TESTMOD_H */
