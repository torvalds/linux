/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#ifndef _BPF_TESTMOD_H
#define _BPF_TESTMOD_H

#include <linux/types.h>

struct bpf_testmod_test_read_ctx {
	char *buf;
	loff_t off;
	size_t len;
};

#endif /* _BPF_TESTMOD_H */
