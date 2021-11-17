// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2021 Facebook */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/filter.h>
#include "btf.h"
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "hashmap.h"
#include "bpf_gen_internal.h"
#include "skel_internal.h"

#define MAX_USED_MAPS 64
#define MAX_USED_PROGS 32

/* The following structure describes the stack layout of the loader program.
 * In addition R6 contains the pointer to context.
 * R7 contains the result of the last sys_bpf command (typically error or FD).
 * R9 contains the result of the last sys_close command.
 *
 * Naming convention:
 * ctx - bpf program context
 * stack - bpf program stack
 * blob - bpf_attr-s, strings, insns, map data.
 *        All the bytes that loader prog will use for read/write.
 */
struct loader_stack {
	__u32 btf_fd;
	__u32 map_fd[MAX_USED_MAPS];
	__u32 prog_fd[MAX_USED_PROGS];
	__u32 inner_map_fd;
};

#define stack_off(field) \
	(__s16)(-sizeof(struct loader_stack) + offsetof(struct loader_stack, field))

#define attr_field(attr, field) (attr + offsetof(union bpf_attr, field))

static int realloc_insn_buf(struct bpf_gen *gen, __u32 size)
{
	size_t off = gen->insn_cur - gen->insn_start;
	void *insn_start;

	if (gen->error)
		return gen->error;
	if (size > INT32_MAX || off + size > INT32_MAX) {
		gen->error = -ERANGE;
		return -ERANGE;
	}
	insn_start = realloc(gen->insn_start, off + size);
	if (!insn_start) {
		gen->error = -ENOMEM;
		free(gen->insn_start);
		gen->insn_start = NULL;
		return -ENOMEM;
	}
	gen->insn_start = insn_start;
	gen->insn_cur = insn_start + off;
	return 0;
}

static int realloc_data_buf(struct bpf_gen *gen, __u32 size)
{
	size_t off = gen->data_cur - gen->data_start;
	void *data_start;

	if (gen->error)
		return gen->error;
	if (size > INT32_MAX || off + size > INT32_MAX) {
		gen->error = -ERANGE;
		return -ERANGE;
	}
	data_start = realloc(gen->data_start, off + size);
	if (!data_start) {
		gen->error = -ENOMEM;
		free(gen->data_start);
		gen->data_start = NULL;
		return -ENOMEM;
	}
	gen->data_start = data_start;
	gen->data_cur = data_start + off;
	return 0;
}

static void emit(struct bpf_gen *gen, struct bpf_insn insn)
{
	if (realloc_insn_buf(gen, sizeof(insn)))
		return;
	memcpy(gen->insn_cur, &insn, sizeof(insn));
	gen->insn_cur += sizeof(insn);
}

static void emit2(struct bpf_gen *gen, struct bpf_insn insn1, struct bpf_insn insn2)
{
	emit(gen, insn1);
	emit(gen, insn2);
}

void bpf_gen__init(struct bpf_gen *gen, int log_level)
{
	size_t stack_sz = sizeof(struct loader_stack);
	int i;

	gen->log_level = log_level;
	/* save ctx pointer into R6 */
	emit(gen, BPF_MOV64_REG(BPF_REG_6, BPF_REG_1));

	/* bzero stack */
	emit(gen, BPF_MOV64_REG(BPF_REG_1, BPF_REG_10));
	emit(gen, BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -stack_sz));
	emit(gen, BPF_MOV64_IMM(BPF_REG_2, stack_sz));
	emit(gen, BPF_MOV64_IMM(BPF_REG_3, 0));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_probe_read_kernel));

	/* jump over cleanup code */
	emit(gen, BPF_JMP_IMM(BPF_JA, 0, 0,
			      /* size of cleanup code below */
			      (stack_sz / 4) * 3 + 2));

	/* remember the label where all error branches will jump to */
	gen->cleanup_label = gen->insn_cur - gen->insn_start;
	/* emit cleanup code: close all temp FDs */
	for (i = 0; i < stack_sz; i += 4) {
		emit(gen, BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, -stack_sz + i));
		emit(gen, BPF_JMP_IMM(BPF_JSLE, BPF_REG_1, 0, 1));
		emit(gen, BPF_EMIT_CALL(BPF_FUNC_sys_close));
	}
	/* R7 contains the error code from sys_bpf. Copy it into R0 and exit. */
	emit(gen, BPF_MOV64_REG(BPF_REG_0, BPF_REG_7));
	emit(gen, BPF_EXIT_INSN());
}

static int add_data(struct bpf_gen *gen, const void *data, __u32 size)
{
	void *prev;

	if (realloc_data_buf(gen, size))
		return 0;
	prev = gen->data_cur;
	memcpy(gen->data_cur, data, size);
	gen->data_cur += size;
	return prev - gen->data_start;
}

static int insn_bytes_to_bpf_size(__u32 sz)
{
	switch (sz) {
	case 8: return BPF_DW;
	case 4: return BPF_W;
	case 2: return BPF_H;
	case 1: return BPF_B;
	default: return -1;
	}
}

/* *(u64 *)(blob + off) = (u64)(void *)(blob + data) */
static void emit_rel_store(struct bpf_gen *gen, int off, int data)
{
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, data));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, off));
	emit(gen, BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0));
}

/* *(u64 *)(blob + off) = (u64)(void *)(%sp + stack_off) */
static void emit_rel_store_sp(struct bpf_gen *gen, int off, int stack_off)
{
	emit(gen, BPF_MOV64_REG(BPF_REG_0, BPF_REG_10));
	emit(gen, BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, stack_off));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, off));
	emit(gen, BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0, 0));
}

static void move_ctx2blob(struct bpf_gen *gen, int off, int size, int ctx_off,
				   bool check_non_zero)
{
	emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_6, ctx_off));
	if (check_non_zero)
		/* If value in ctx is zero don't update the blob.
		 * For example: when ctx->map.max_entries == 0, keep default max_entries from bpf.c
		 */
		emit(gen, BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, off));
	emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_1, BPF_REG_0, 0));
}

static void move_stack2blob(struct bpf_gen *gen, int off, int size, int stack_off)
{
	emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_10, stack_off));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, off));
	emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_1, BPF_REG_0, 0));
}

static void move_stack2ctx(struct bpf_gen *gen, int ctx_off, int size, int stack_off)
{
	emit(gen, BPF_LDX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_0, BPF_REG_10, stack_off));
	emit(gen, BPF_STX_MEM(insn_bytes_to_bpf_size(size), BPF_REG_6, BPF_REG_0, ctx_off));
}

static void emit_sys_bpf(struct bpf_gen *gen, int cmd, int attr, int attr_size)
{
	emit(gen, BPF_MOV64_IMM(BPF_REG_1, cmd));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_2, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, attr));
	emit(gen, BPF_MOV64_IMM(BPF_REG_3, attr_size));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_sys_bpf));
	/* remember the result in R7 */
	emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
}

static bool is_simm16(__s64 value)
{
	return value == (__s64)(__s16)value;
}

static void emit_check_err(struct bpf_gen *gen)
{
	__s64 off = -(gen->insn_cur - gen->insn_start - gen->cleanup_label) / 8 - 1;

	/* R7 contains result of last sys_bpf command.
	 * if (R7 < 0) goto cleanup;
	 */
	if (is_simm16(off)) {
		emit(gen, BPF_JMP_IMM(BPF_JSLT, BPF_REG_7, 0, off));
	} else {
		gen->error = -ERANGE;
		emit(gen, BPF_JMP_IMM(BPF_JA, 0, 0, -1));
	}
}

/* reg1 and reg2 should not be R1 - R5. They can be R0, R6 - R10 */
static void emit_debug(struct bpf_gen *gen, int reg1, int reg2,
		       const char *fmt, va_list args)
{
	char buf[1024];
	int addr, len, ret;

	if (!gen->log_level)
		return;
	ret = vsnprintf(buf, sizeof(buf), fmt, args);
	if (ret < 1024 - 7 && reg1 >= 0 && reg2 < 0)
		/* The special case to accommodate common debug_ret():
		 * to avoid specifying BPF_REG_7 and adding " r=%%d" to
		 * prints explicitly.
		 */
		strcat(buf, " r=%d");
	len = strlen(buf) + 1;
	addr = add_data(gen, buf, len);

	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, addr));
	emit(gen, BPF_MOV64_IMM(BPF_REG_2, len));
	if (reg1 >= 0)
		emit(gen, BPF_MOV64_REG(BPF_REG_3, reg1));
	if (reg2 >= 0)
		emit(gen, BPF_MOV64_REG(BPF_REG_4, reg2));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_trace_printk));
}

static void debug_regs(struct bpf_gen *gen, int reg1, int reg2, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	emit_debug(gen, reg1, reg2, fmt, args);
	va_end(args);
}

static void debug_ret(struct bpf_gen *gen, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	emit_debug(gen, BPF_REG_7, -1, fmt, args);
	va_end(args);
}

static void __emit_sys_close(struct bpf_gen *gen)
{
	emit(gen, BPF_JMP_IMM(BPF_JSLE, BPF_REG_1, 0,
			      /* 2 is the number of the following insns
			       * * 6 is additional insns in debug_regs
			       */
			      2 + (gen->log_level ? 6 : 0)));
	emit(gen, BPF_MOV64_REG(BPF_REG_9, BPF_REG_1));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_sys_close));
	debug_regs(gen, BPF_REG_9, BPF_REG_0, "close(%%d) = %%d");
}

static void emit_sys_close_stack(struct bpf_gen *gen, int stack_off)
{
	emit(gen, BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_10, stack_off));
	__emit_sys_close(gen);
}

static void emit_sys_close_blob(struct bpf_gen *gen, int blob_off)
{
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, blob_off));
	emit(gen, BPF_LDX_MEM(BPF_W, BPF_REG_1, BPF_REG_0, 0));
	__emit_sys_close(gen);
}

int bpf_gen__finish(struct bpf_gen *gen)
{
	int i;

	emit_sys_close_stack(gen, stack_off(btf_fd));
	for (i = 0; i < gen->nr_progs; i++)
		move_stack2ctx(gen,
			       sizeof(struct bpf_loader_ctx) +
			       sizeof(struct bpf_map_desc) * gen->nr_maps +
			       sizeof(struct bpf_prog_desc) * i +
			       offsetof(struct bpf_prog_desc, prog_fd), 4,
			       stack_off(prog_fd[i]));
	for (i = 0; i < gen->nr_maps; i++)
		move_stack2ctx(gen,
			       sizeof(struct bpf_loader_ctx) +
			       sizeof(struct bpf_map_desc) * i +
			       offsetof(struct bpf_map_desc, map_fd), 4,
			       stack_off(map_fd[i]));
	emit(gen, BPF_MOV64_IMM(BPF_REG_0, 0));
	emit(gen, BPF_EXIT_INSN());
	pr_debug("gen: finish %d\n", gen->error);
	if (!gen->error) {
		struct gen_loader_opts *opts = gen->opts;

		opts->insns = gen->insn_start;
		opts->insns_sz = gen->insn_cur - gen->insn_start;
		opts->data = gen->data_start;
		opts->data_sz = gen->data_cur - gen->data_start;
	}
	return gen->error;
}

void bpf_gen__free(struct bpf_gen *gen)
{
	if (!gen)
		return;
	free(gen->data_start);
	free(gen->insn_start);
	free(gen);
}

void bpf_gen__load_btf(struct bpf_gen *gen, const void *btf_raw_data,
		       __u32 btf_raw_size)
{
	int attr_size = offsetofend(union bpf_attr, btf_log_level);
	int btf_data, btf_load_attr;
	union bpf_attr attr;

	memset(&attr, 0, attr_size);
	pr_debug("gen: load_btf: size %d\n", btf_raw_size);
	btf_data = add_data(gen, btf_raw_data, btf_raw_size);

	attr.btf_size = btf_raw_size;
	btf_load_attr = add_data(gen, &attr, attr_size);

	/* populate union bpf_attr with user provided log details */
	move_ctx2blob(gen, attr_field(btf_load_attr, btf_log_level), 4,
		      offsetof(struct bpf_loader_ctx, log_level), false);
	move_ctx2blob(gen, attr_field(btf_load_attr, btf_log_size), 4,
		      offsetof(struct bpf_loader_ctx, log_size), false);
	move_ctx2blob(gen, attr_field(btf_load_attr, btf_log_buf), 8,
		      offsetof(struct bpf_loader_ctx, log_buf), false);
	/* populate union bpf_attr with a pointer to the BTF data */
	emit_rel_store(gen, attr_field(btf_load_attr, btf), btf_data);
	/* emit BTF_LOAD command */
	emit_sys_bpf(gen, BPF_BTF_LOAD, btf_load_attr, attr_size);
	debug_ret(gen, "btf_load size %d", btf_raw_size);
	emit_check_err(gen);
	/* remember btf_fd in the stack, if successful */
	emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7, stack_off(btf_fd)));
}

void bpf_gen__map_create(struct bpf_gen *gen,
			 struct bpf_create_map_attr *map_attr, int map_idx)
{
	int attr_size = offsetofend(union bpf_attr, btf_vmlinux_value_type_id);
	bool close_inner_map_fd = false;
	int map_create_attr;
	union bpf_attr attr;

	memset(&attr, 0, attr_size);
	attr.map_type = map_attr->map_type;
	attr.key_size = map_attr->key_size;
	attr.value_size = map_attr->value_size;
	attr.map_flags = map_attr->map_flags;
	memcpy(attr.map_name, map_attr->name,
	       min((unsigned)strlen(map_attr->name), BPF_OBJ_NAME_LEN - 1));
	attr.numa_node = map_attr->numa_node;
	attr.map_ifindex = map_attr->map_ifindex;
	attr.max_entries = map_attr->max_entries;
	switch (attr.map_type) {
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_STACK_TRACE:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
	case BPF_MAP_TYPE_CPUMAP:
	case BPF_MAP_TYPE_XSKMAP:
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
	case BPF_MAP_TYPE_RINGBUF:
		break;
	default:
		attr.btf_key_type_id = map_attr->btf_key_type_id;
		attr.btf_value_type_id = map_attr->btf_value_type_id;
	}

	pr_debug("gen: map_create: %s idx %d type %d value_type_id %d\n",
		 attr.map_name, map_idx, map_attr->map_type, attr.btf_value_type_id);

	map_create_attr = add_data(gen, &attr, attr_size);
	if (attr.btf_value_type_id)
		/* populate union bpf_attr with btf_fd saved in the stack earlier */
		move_stack2blob(gen, attr_field(map_create_attr, btf_fd), 4,
				stack_off(btf_fd));
	switch (attr.map_type) {
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		move_stack2blob(gen, attr_field(map_create_attr, inner_map_fd), 4,
				stack_off(inner_map_fd));
		close_inner_map_fd = true;
		break;
	default:
		break;
	}
	/* conditionally update max_entries */
	if (map_idx >= 0)
		move_ctx2blob(gen, attr_field(map_create_attr, max_entries), 4,
			      sizeof(struct bpf_loader_ctx) +
			      sizeof(struct bpf_map_desc) * map_idx +
			      offsetof(struct bpf_map_desc, max_entries),
			      true /* check that max_entries != 0 */);
	/* emit MAP_CREATE command */
	emit_sys_bpf(gen, BPF_MAP_CREATE, map_create_attr, attr_size);
	debug_ret(gen, "map_create %s idx %d type %d value_size %d value_btf_id %d",
		  attr.map_name, map_idx, map_attr->map_type, attr.value_size,
		  attr.btf_value_type_id);
	emit_check_err(gen);
	/* remember map_fd in the stack, if successful */
	if (map_idx < 0) {
		/* This bpf_gen__map_create() function is called with map_idx >= 0
		 * for all maps that libbpf loading logic tracks.
		 * It's called with -1 to create an inner map.
		 */
		emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7,
				      stack_off(inner_map_fd)));
	} else if (map_idx != gen->nr_maps) {
		gen->error = -EDOM; /* internal bug */
		return;
	} else {
		emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7,
				      stack_off(map_fd[map_idx])));
		gen->nr_maps++;
	}
	if (close_inner_map_fd)
		emit_sys_close_stack(gen, stack_off(inner_map_fd));
}

void bpf_gen__record_attach_target(struct bpf_gen *gen, const char *attach_name,
				   enum bpf_attach_type type)
{
	const char *prefix;
	int kind, ret;

	btf_get_kernel_prefix_kind(type, &prefix, &kind);
	gen->attach_kind = kind;
	ret = snprintf(gen->attach_target, sizeof(gen->attach_target), "%s%s",
		       prefix, attach_name);
	if (ret == sizeof(gen->attach_target))
		gen->error = -ENOSPC;
}

static void emit_find_attach_target(struct bpf_gen *gen)
{
	int name, len = strlen(gen->attach_target) + 1;

	pr_debug("gen: find_attach_tgt %s %d\n", gen->attach_target, gen->attach_kind);
	name = add_data(gen, gen->attach_target, len);

	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, name));
	emit(gen, BPF_MOV64_IMM(BPF_REG_2, len));
	emit(gen, BPF_MOV64_IMM(BPF_REG_3, gen->attach_kind));
	emit(gen, BPF_MOV64_IMM(BPF_REG_4, 0));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_btf_find_by_name_kind));
	emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
	debug_ret(gen, "find_by_name_kind(%s,%d)",
		  gen->attach_target, gen->attach_kind);
	emit_check_err(gen);
	/* if successful, btf_id is in lower 32-bit of R7 and
	 * btf_obj_fd is in upper 32-bit
	 */
}

void bpf_gen__record_extern(struct bpf_gen *gen, const char *name, int kind,
			    int insn_idx)
{
	struct ksym_relo_desc *relo;

	relo = libbpf_reallocarray(gen->relos, gen->relo_cnt + 1, sizeof(*relo));
	if (!relo) {
		gen->error = -ENOMEM;
		return;
	}
	gen->relos = relo;
	relo += gen->relo_cnt;
	relo->name = name;
	relo->kind = kind;
	relo->insn_idx = insn_idx;
	gen->relo_cnt++;
}

static void emit_relo(struct bpf_gen *gen, struct ksym_relo_desc *relo, int insns)
{
	int name, insn, len = strlen(relo->name) + 1;

	pr_debug("gen: emit_relo: %s at %d\n", relo->name, relo->insn_idx);
	name = add_data(gen, relo->name, len);

	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, name));
	emit(gen, BPF_MOV64_IMM(BPF_REG_2, len));
	emit(gen, BPF_MOV64_IMM(BPF_REG_3, relo->kind));
	emit(gen, BPF_MOV64_IMM(BPF_REG_4, 0));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_btf_find_by_name_kind));
	emit(gen, BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
	debug_ret(gen, "find_by_name_kind(%s,%d)", relo->name, relo->kind);
	emit_check_err(gen);
	/* store btf_id into insn[insn_idx].imm */
	insn = insns + sizeof(struct bpf_insn) * relo->insn_idx +
		offsetof(struct bpf_insn, imm);
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, insn));
	emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_7, 0));
	if (relo->kind == BTF_KIND_VAR) {
		/* store btf_obj_fd into insn[insn_idx + 1].imm */
		emit(gen, BPF_ALU64_IMM(BPF_RSH, BPF_REG_7, 32));
		emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_7,
				      sizeof(struct bpf_insn)));
	}
}

static void emit_relos(struct bpf_gen *gen, int insns)
{
	int i;

	for (i = 0; i < gen->relo_cnt; i++)
		emit_relo(gen, gen->relos + i, insns);
}

static void cleanup_relos(struct bpf_gen *gen, int insns)
{
	int i, insn;

	for (i = 0; i < gen->relo_cnt; i++) {
		if (gen->relos[i].kind != BTF_KIND_VAR)
			continue;
		/* close fd recorded in insn[insn_idx + 1].imm */
		insn = insns +
			sizeof(struct bpf_insn) * (gen->relos[i].insn_idx + 1) +
			offsetof(struct bpf_insn, imm);
		emit_sys_close_blob(gen, insn);
	}
	if (gen->relo_cnt) {
		free(gen->relos);
		gen->relo_cnt = 0;
		gen->relos = NULL;
	}
}

void bpf_gen__prog_load(struct bpf_gen *gen,
			struct bpf_prog_load_params *load_attr, int prog_idx)
{
	int attr_size = offsetofend(union bpf_attr, fd_array);
	int prog_load_attr, license, insns, func_info, line_info;
	union bpf_attr attr;

	memset(&attr, 0, attr_size);
	pr_debug("gen: prog_load: type %d insns_cnt %zd\n",
		 load_attr->prog_type, load_attr->insn_cnt);
	/* add license string to blob of bytes */
	license = add_data(gen, load_attr->license, strlen(load_attr->license) + 1);
	/* add insns to blob of bytes */
	insns = add_data(gen, load_attr->insns,
			 load_attr->insn_cnt * sizeof(struct bpf_insn));

	attr.prog_type = load_attr->prog_type;
	attr.expected_attach_type = load_attr->expected_attach_type;
	attr.attach_btf_id = load_attr->attach_btf_id;
	attr.prog_ifindex = load_attr->prog_ifindex;
	attr.kern_version = 0;
	attr.insn_cnt = (__u32)load_attr->insn_cnt;
	attr.prog_flags = load_attr->prog_flags;

	attr.func_info_rec_size = load_attr->func_info_rec_size;
	attr.func_info_cnt = load_attr->func_info_cnt;
	func_info = add_data(gen, load_attr->func_info,
			     attr.func_info_cnt * attr.func_info_rec_size);

	attr.line_info_rec_size = load_attr->line_info_rec_size;
	attr.line_info_cnt = load_attr->line_info_cnt;
	line_info = add_data(gen, load_attr->line_info,
			     attr.line_info_cnt * attr.line_info_rec_size);

	memcpy(attr.prog_name, load_attr->name,
	       min((unsigned)strlen(load_attr->name), BPF_OBJ_NAME_LEN - 1));
	prog_load_attr = add_data(gen, &attr, attr_size);

	/* populate union bpf_attr with a pointer to license */
	emit_rel_store(gen, attr_field(prog_load_attr, license), license);

	/* populate union bpf_attr with a pointer to instructions */
	emit_rel_store(gen, attr_field(prog_load_attr, insns), insns);

	/* populate union bpf_attr with a pointer to func_info */
	emit_rel_store(gen, attr_field(prog_load_attr, func_info), func_info);

	/* populate union bpf_attr with a pointer to line_info */
	emit_rel_store(gen, attr_field(prog_load_attr, line_info), line_info);

	/* populate union bpf_attr fd_array with a pointer to stack where map_fds are saved */
	emit_rel_store_sp(gen, attr_field(prog_load_attr, fd_array),
			  stack_off(map_fd[0]));

	/* populate union bpf_attr with user provided log details */
	move_ctx2blob(gen, attr_field(prog_load_attr, log_level), 4,
		      offsetof(struct bpf_loader_ctx, log_level), false);
	move_ctx2blob(gen, attr_field(prog_load_attr, log_size), 4,
		      offsetof(struct bpf_loader_ctx, log_size), false);
	move_ctx2blob(gen, attr_field(prog_load_attr, log_buf), 8,
		      offsetof(struct bpf_loader_ctx, log_buf), false);
	/* populate union bpf_attr with btf_fd saved in the stack earlier */
	move_stack2blob(gen, attr_field(prog_load_attr, prog_btf_fd), 4,
			stack_off(btf_fd));
	if (gen->attach_kind) {
		emit_find_attach_target(gen);
		/* populate union bpf_attr with btf_id and btf_obj_fd found by helper */
		emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_0, BPF_PSEUDO_MAP_IDX_VALUE,
						 0, 0, 0, prog_load_attr));
		emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_7,
				      offsetof(union bpf_attr, attach_btf_id)));
		emit(gen, BPF_ALU64_IMM(BPF_RSH, BPF_REG_7, 32));
		emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_0, BPF_REG_7,
				      offsetof(union bpf_attr, attach_btf_obj_fd)));
	}
	emit_relos(gen, insns);
	/* emit PROG_LOAD command */
	emit_sys_bpf(gen, BPF_PROG_LOAD, prog_load_attr, attr_size);
	debug_ret(gen, "prog_load %s insn_cnt %d", attr.prog_name, attr.insn_cnt);
	/* successful or not, close btf module FDs used in extern ksyms and attach_btf_obj_fd */
	cleanup_relos(gen, insns);
	if (gen->attach_kind)
		emit_sys_close_blob(gen,
				    attr_field(prog_load_attr, attach_btf_obj_fd));
	emit_check_err(gen);
	/* remember prog_fd in the stack, if successful */
	emit(gen, BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_7,
			      stack_off(prog_fd[gen->nr_progs])));
	gen->nr_progs++;
}

void bpf_gen__map_update_elem(struct bpf_gen *gen, int map_idx, void *pvalue,
			      __u32 value_size)
{
	int attr_size = offsetofend(union bpf_attr, flags);
	int map_update_attr, value, key;
	union bpf_attr attr;
	int zero = 0;

	memset(&attr, 0, attr_size);
	pr_debug("gen: map_update_elem: idx %d\n", map_idx);

	value = add_data(gen, pvalue, value_size);
	key = add_data(gen, &zero, sizeof(zero));

	/* if (map_desc[map_idx].initial_value)
	 *    copy_from_user(value, initial_value, value_size);
	 */
	emit(gen, BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_6,
			      sizeof(struct bpf_loader_ctx) +
			      sizeof(struct bpf_map_desc) * map_idx +
			      offsetof(struct bpf_map_desc, initial_value)));
	emit(gen, BPF_JMP_IMM(BPF_JEQ, BPF_REG_3, 0, 4));
	emit2(gen, BPF_LD_IMM64_RAW_FULL(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE,
					 0, 0, 0, value));
	emit(gen, BPF_MOV64_IMM(BPF_REG_2, value_size));
	emit(gen, BPF_EMIT_CALL(BPF_FUNC_copy_from_user));

	map_update_attr = add_data(gen, &attr, attr_size);
	move_stack2blob(gen, attr_field(map_update_attr, map_fd), 4,
			stack_off(map_fd[map_idx]));
	emit_rel_store(gen, attr_field(map_update_attr, key), key);
	emit_rel_store(gen, attr_field(map_update_attr, value), value);
	/* emit MAP_UPDATE_ELEM command */
	emit_sys_bpf(gen, BPF_MAP_UPDATE_ELEM, map_update_attr, attr_size);
	debug_ret(gen, "update_elem idx %d value_size %d", map_idx, value_size);
	emit_check_err(gen);
}

void bpf_gen__map_freeze(struct bpf_gen *gen, int map_idx)
{
	int attr_size = offsetofend(union bpf_attr, map_fd);
	int map_freeze_attr;
	union bpf_attr attr;

	memset(&attr, 0, attr_size);
	pr_debug("gen: map_freeze: idx %d\n", map_idx);
	map_freeze_attr = add_data(gen, &attr, attr_size);
	move_stack2blob(gen, attr_field(map_freeze_attr, map_fd), 4,
			stack_off(map_fd[map_idx]));
	/* emit MAP_FREEZE command */
	emit_sys_bpf(gen, BPF_MAP_FREEZE, map_freeze_attr, attr_size);
	debug_ret(gen, "map_freeze");
	emit_check_err(gen);
}
