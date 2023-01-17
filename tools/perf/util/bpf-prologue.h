/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015, He Kuang <hekuang@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */
#ifndef __BPF_PROLOGUE_H
#define __BPF_PROLOGUE_H

struct probe_trace_arg;
struct bpf_insn;

#define BPF_PROLOGUE_MAX_ARGS 3
#define BPF_PROLOGUE_START_ARG_REG BPF_REG_3
#define BPF_PROLOGUE_FETCH_RESULT_REG BPF_REG_2

#ifdef HAVE_BPF_PROLOGUE
int bpf__gen_prologue(struct probe_trace_arg *args, int nargs,
		      struct bpf_insn *new_prog, size_t *new_cnt,
		      size_t cnt_space);
#else
#include <linux/compiler.h>
#include <errno.h>

static inline int
bpf__gen_prologue(struct probe_trace_arg *args __maybe_unused,
		  int nargs __maybe_unused,
		  struct bpf_insn *new_prog __maybe_unused,
		  size_t *new_cnt,
		  size_t cnt_space __maybe_unused)
{
	if (!new_cnt)
		return -EINVAL;
	*new_cnt = 0;
	return -ENOTSUP;
}
#endif
#endif /* __BPF_PROLOGUE_H */
