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
};

#endif /* _BPF_TESTMOD_H */
