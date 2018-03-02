// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/list.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "main.h"

struct cfg {
	struct list_head funcs;
	int func_num;
};

struct func_node {
	struct list_head l;
	struct bpf_insn *start;
	struct bpf_insn *end;
	int idx;
};

#define func_prev(func)		list_prev_entry(func, l)
#define func_next(func)		list_next_entry(func, l)
#define cfg_first_func(cfg)	\
	list_first_entry(&cfg->funcs, struct func_node, l)
#define cfg_last_func(cfg)	\
	list_last_entry(&cfg->funcs, struct func_node, l)

static struct func_node *cfg_append_func(struct cfg *cfg, struct bpf_insn *insn)
{
	struct func_node *new_func, *func;

	list_for_each_entry(func, &cfg->funcs, l) {
		if (func->start == insn)
			return func;
		else if (func->start > insn)
			break;
	}

	func = func_prev(func);
	new_func = calloc(1, sizeof(*new_func));
	if (!new_func) {
		p_err("OOM when allocating FUNC node");
		return NULL;
	}
	new_func->start = insn;
	new_func->idx = cfg->func_num;
	list_add(&new_func->l, &func->l);
	cfg->func_num++;

	return new_func;
}

static bool cfg_partition_funcs(struct cfg *cfg, struct bpf_insn *cur,
				struct bpf_insn *end)
{
	struct func_node *func, *last_func;

	func = cfg_append_func(cfg, cur);
	if (!func)
		return true;

	for (; cur < end; cur++) {
		if (cur->code != (BPF_JMP | BPF_CALL))
			continue;
		if (cur->src_reg != BPF_PSEUDO_CALL)
			continue;
		func = cfg_append_func(cfg, cur + cur->off + 1);
		if (!func)
			return true;
	}

	last_func = cfg_last_func(cfg);
	last_func->end = end - 1;
	func = cfg_first_func(cfg);
	list_for_each_entry_from(func, &last_func->l, l) {
		func->end = func_next(func)->start - 1;
	}

	return false;
}

static bool cfg_build(struct cfg *cfg, struct bpf_insn *insn, unsigned int len)
{
	int cnt = len / sizeof(*insn);

	INIT_LIST_HEAD(&cfg->funcs);

	return cfg_partition_funcs(cfg, insn, insn + cnt);
}

static void cfg_destroy(struct cfg *cfg)
{
	struct func_node *func, *func2;

	list_for_each_entry_safe(func, func2, &cfg->funcs, l) {
		list_del(&func->l);
		free(func);
	}
}

void dump_xlated_cfg(void *buf, unsigned int len)
{
	struct bpf_insn *insn = buf;
	struct cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	if (cfg_build(&cfg, insn, len))
		return;

	cfg_destroy(&cfg);
}
