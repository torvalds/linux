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
#include "xlated_dumper.h"

struct cfg {
	struct list_head funcs;
	int func_num;
};

struct func_node {
	struct list_head l;
	struct list_head bbs;
	struct bpf_insn *start;
	struct bpf_insn *end;
	int idx;
	int bb_num;
};

struct bb_node {
	struct list_head l;
	struct list_head e_prevs;
	struct list_head e_succs;
	struct bpf_insn *head;
	struct bpf_insn *tail;
	int idx;
};

#define EDGE_FLAG_EMPTY		0x0
#define EDGE_FLAG_FALLTHROUGH	0x1
#define EDGE_FLAG_JUMP		0x2
struct edge_node {
	struct list_head l;
	struct bb_node *src;
	struct bb_node *dst;
	int flags;
};

#define ENTRY_BLOCK_INDEX	0
#define EXIT_BLOCK_INDEX	1
#define NUM_FIXED_BLOCKS	2
#define func_prev(func)		list_prev_entry(func, l)
#define func_next(func)		list_next_entry(func, l)
#define bb_prev(bb)		list_prev_entry(bb, l)
#define bb_next(bb)		list_next_entry(bb, l)
#define entry_bb(func)		func_first_bb(func)
#define exit_bb(func)		func_last_bb(func)
#define cfg_first_func(cfg)	\
	list_first_entry(&cfg->funcs, struct func_node, l)
#define cfg_last_func(cfg)	\
	list_last_entry(&cfg->funcs, struct func_node, l)
#define func_first_bb(func)	\
	list_first_entry(&func->bbs, struct bb_node, l)
#define func_last_bb(func)	\
	list_last_entry(&func->bbs, struct bb_node, l)

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

static struct bb_node *func_append_bb(struct func_node *func,
				      struct bpf_insn *insn)
{
	struct bb_node *new_bb, *bb;

	list_for_each_entry(bb, &func->bbs, l) {
		if (bb->head == insn)
			return bb;
		else if (bb->head > insn)
			break;
	}

	bb = bb_prev(bb);
	new_bb = calloc(1, sizeof(*new_bb));
	if (!new_bb) {
		p_err("OOM when allocating BB node");
		return NULL;
	}
	new_bb->head = insn;
	INIT_LIST_HEAD(&new_bb->e_prevs);
	INIT_LIST_HEAD(&new_bb->e_succs);
	list_add(&new_bb->l, &bb->l);

	return new_bb;
}

static struct bb_node *func_insert_dummy_bb(struct list_head *after)
{
	struct bb_node *bb;

	bb = calloc(1, sizeof(*bb));
	if (!bb) {
		p_err("OOM when allocating BB node");
		return NULL;
	}

	INIT_LIST_HEAD(&bb->e_prevs);
	INIT_LIST_HEAD(&bb->e_succs);
	list_add(&bb->l, after);

	return bb;
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

static bool func_partition_bb_head(struct func_node *func)
{
	struct bpf_insn *cur, *end;
	struct bb_node *bb;

	cur = func->start;
	end = func->end;
	INIT_LIST_HEAD(&func->bbs);
	bb = func_append_bb(func, cur);
	if (!bb)
		return true;

	for (; cur <= end; cur++) {
		if (BPF_CLASS(cur->code) == BPF_JMP) {
			u8 opcode = BPF_OP(cur->code);

			if (opcode == BPF_EXIT || opcode == BPF_CALL)
				continue;

			bb = func_append_bb(func, cur + cur->off + 1);
			if (!bb)
				return true;

			if (opcode != BPF_JA) {
				bb = func_append_bb(func, cur + 1);
				if (!bb)
					return true;
			}
		}
	}

	return false;
}

static void func_partition_bb_tail(struct func_node *func)
{
	unsigned int bb_idx = NUM_FIXED_BLOCKS;
	struct bb_node *bb, *last;

	last = func_last_bb(func);
	last->tail = func->end;
	bb = func_first_bb(func);
	list_for_each_entry_from(bb, &last->l, l) {
		bb->tail = bb_next(bb)->head - 1;
		bb->idx = bb_idx++;
	}

	last->idx = bb_idx++;
	func->bb_num = bb_idx;
}

static bool func_add_special_bb(struct func_node *func)
{
	struct bb_node *bb;

	bb = func_insert_dummy_bb(&func->bbs);
	if (!bb)
		return true;
	bb->idx = ENTRY_BLOCK_INDEX;

	bb = func_insert_dummy_bb(&func_last_bb(func)->l);
	if (!bb)
		return true;
	bb->idx = EXIT_BLOCK_INDEX;

	return false;
}

static bool func_partition_bb(struct func_node *func)
{
	if (func_partition_bb_head(func))
		return true;

	func_partition_bb_tail(func);

	return false;
}

static struct bb_node *func_search_bb_with_head(struct func_node *func,
						struct bpf_insn *insn)
{
	struct bb_node *bb;

	list_for_each_entry(bb, &func->bbs, l) {
		if (bb->head == insn)
			return bb;
	}

	return NULL;
}

static struct edge_node *new_edge(struct bb_node *src, struct bb_node *dst,
				  int flags)
{
	struct edge_node *e;

	e = calloc(1, sizeof(*e));
	if (!e) {
		p_err("OOM when allocating edge node");
		return NULL;
	}

	if (src)
		e->src = src;
	if (dst)
		e->dst = dst;

	e->flags |= flags;

	return e;
}

static bool func_add_bb_edges(struct func_node *func)
{
	struct bpf_insn *insn;
	struct edge_node *e;
	struct bb_node *bb;

	bb = entry_bb(func);
	e = new_edge(bb, bb_next(bb), EDGE_FLAG_FALLTHROUGH);
	if (!e)
		return true;
	list_add_tail(&e->l, &bb->e_succs);

	bb = exit_bb(func);
	e = new_edge(bb_prev(bb), bb, EDGE_FLAG_FALLTHROUGH);
	if (!e)
		return true;
	list_add_tail(&e->l, &bb->e_prevs);

	bb = entry_bb(func);
	bb = bb_next(bb);
	list_for_each_entry_from(bb, &exit_bb(func)->l, l) {
		e = new_edge(bb, NULL, EDGE_FLAG_EMPTY);
		if (!e)
			return true;
		e->src = bb;

		insn = bb->tail;
		if (BPF_CLASS(insn->code) != BPF_JMP ||
		    BPF_OP(insn->code) == BPF_EXIT) {
			e->dst = bb_next(bb);
			e->flags |= EDGE_FLAG_FALLTHROUGH;
			list_add_tail(&e->l, &bb->e_succs);
			continue;
		} else if (BPF_OP(insn->code) == BPF_JA) {
			e->dst = func_search_bb_with_head(func,
							  insn + insn->off + 1);
			e->flags |= EDGE_FLAG_JUMP;
			list_add_tail(&e->l, &bb->e_succs);
			continue;
		}

		e->dst = bb_next(bb);
		e->flags |= EDGE_FLAG_FALLTHROUGH;
		list_add_tail(&e->l, &bb->e_succs);

		e = new_edge(bb, NULL, EDGE_FLAG_JUMP);
		if (!e)
			return true;
		e->src = bb;
		e->dst = func_search_bb_with_head(func, insn + insn->off + 1);
		list_add_tail(&e->l, &bb->e_succs);
	}

	return false;
}

static bool cfg_build(struct cfg *cfg, struct bpf_insn *insn, unsigned int len)
{
	int cnt = len / sizeof(*insn);
	struct func_node *func;

	INIT_LIST_HEAD(&cfg->funcs);

	if (cfg_partition_funcs(cfg, insn, insn + cnt))
		return true;

	list_for_each_entry(func, &cfg->funcs, l) {
		if (func_partition_bb(func) || func_add_special_bb(func))
			return true;

		if (func_add_bb_edges(func))
			return true;
	}

	return false;
}

static void cfg_destroy(struct cfg *cfg)
{
	struct func_node *func, *func2;

	list_for_each_entry_safe(func, func2, &cfg->funcs, l) {
		struct bb_node *bb, *bb2;

		list_for_each_entry_safe(bb, bb2, &func->bbs, l) {
			struct edge_node *e, *e2;

			list_for_each_entry_safe(e, e2, &bb->e_prevs, l) {
				list_del(&e->l);
				free(e);
			}

			list_for_each_entry_safe(e, e2, &bb->e_succs, l) {
				list_del(&e->l);
				free(e);
			}

			list_del(&bb->l);
			free(bb);
		}

		list_del(&func->l);
		free(func);
	}
}

static void draw_bb_node(struct func_node *func, struct bb_node *bb)
{
	const char *shape;

	if (bb->idx == ENTRY_BLOCK_INDEX || bb->idx == EXIT_BLOCK_INDEX)
		shape = "Mdiamond";
	else
		shape = "record";

	printf("\tfn_%d_bb_%d [shape=%s,style=filled,label=\"",
	       func->idx, bb->idx, shape);

	if (bb->idx == ENTRY_BLOCK_INDEX) {
		printf("ENTRY");
	} else if (bb->idx == EXIT_BLOCK_INDEX) {
		printf("EXIT");
	} else {
		unsigned int start_idx;
		struct dump_data dd = {};

		printf("{");
		kernel_syms_load(&dd);
		start_idx = bb->head - func->start;
		dump_xlated_for_graph(&dd, bb->head, bb->tail, start_idx);
		kernel_syms_destroy(&dd);
		printf("}");
	}

	printf("\"];\n\n");
}

static void draw_bb_succ_edges(struct func_node *func, struct bb_node *bb)
{
	const char *style = "\"solid,bold\"";
	const char *color = "black";
	int func_idx = func->idx;
	struct edge_node *e;
	int weight = 10;

	if (list_empty(&bb->e_succs))
		return;

	list_for_each_entry(e, &bb->e_succs, l) {
		printf("\tfn_%d_bb_%d:s -> fn_%d_bb_%d:n [style=%s, color=%s, weight=%d, constraint=true",
		       func_idx, e->src->idx, func_idx, e->dst->idx,
		       style, color, weight);
		printf("];\n");
	}
}

static void func_output_bb_def(struct func_node *func)
{
	struct bb_node *bb;

	list_for_each_entry(bb, &func->bbs, l) {
		draw_bb_node(func, bb);
	}
}

static void func_output_edges(struct func_node *func)
{
	int func_idx = func->idx;
	struct bb_node *bb;

	list_for_each_entry(bb, &func->bbs, l) {
		draw_bb_succ_edges(func, bb);
	}

	/* Add an invisible edge from ENTRY to EXIT, this is to
	 * improve the graph layout.
	 */
	printf("\tfn_%d_bb_%d:s -> fn_%d_bb_%d:n [style=\"invis\", constraint=true];\n",
	       func_idx, ENTRY_BLOCK_INDEX, func_idx, EXIT_BLOCK_INDEX);
}

static void cfg_dump(struct cfg *cfg)
{
	struct func_node *func;

	printf("digraph \"DOT graph for eBPF program\" {\n");
	list_for_each_entry(func, &cfg->funcs, l) {
		printf("subgraph \"cluster_%d\" {\n\tstyle=\"dashed\";\n\tcolor=\"black\";\n\tlabel=\"func_%d ()\";\n",
		       func->idx, func->idx);
		func_output_bb_def(func);
		func_output_edges(func);
		printf("}\n");
	}
	printf("}\n");
}

void dump_xlated_cfg(void *buf, unsigned int len)
{
	struct bpf_insn *insn = buf;
	struct cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	if (cfg_build(&cfg, insn, len))
		return;

	cfg_dump(&cfg);

	cfg_destroy(&cfg);
}
