// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/filter.h>

#include <bpf/bpf.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#define CG_PATH			"/foo"
#define MAX_INSNS		512
#define FIXUP_SYSCTL_VALUE	0

char bpf_log_buf[BPF_LOG_BUF_SIZE];

struct sysctl_test {
	const char *descr;
	size_t fixup_value_insn;
	struct bpf_insn	insns[MAX_INSNS];
	enum bpf_attach_type attach_type;
	const char *sysctl;
	int open_flags;
	const char *newval;
	enum {
		LOAD_REJECT,
		ATTACH_REJECT,
		OP_EPERM,
		SUCCESS,
	} result;
};

static struct sysctl_test tests[] = {
	{
		.descr = "sysctl wrong attach_type",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = 0,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "sysctl:read allow all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl:read deny all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = OP_EPERM,
	},
	{
		.descr = "ctx:write sysctl:read read ok",
		.insns = {
			/* If (write) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 1, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "ctx:write sysctl:write read ok",
		.insns = {
			/* If (write) */
			BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 1, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/domainname",
		.open_flags = O_WRONLY,
		.newval = "(none)", /* same as default, should fail anyway */
		.result = OP_EPERM,
	},
	{
		.descr = "ctx:write sysctl:read write reject",
		.insns = {
			/* write = X */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sysctl, write)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = LOAD_REJECT,
	},
	{
		.descr = "sysctl_get_name sysctl_value:base ok",
		.insns = {
			/* sysctl_get_name arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_name arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* sysctl_get_name arg4 (flags) */
			BPF_MOV64_IMM(BPF_REG_4, BPF_F_SYSCTL_BASE_NAME),

			/* sysctl_get_name(ctx, buf, buf_len, flags) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_name),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, sizeof("tcp_mem") - 1, 6),
			/*     buf == "tcp_mem\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x006d656d5f706374ULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_name sysctl_value:base E2BIG truncated",
		.insns = {
			/* sysctl_get_name arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_name arg3 (buf_len) too small */
			BPF_MOV64_IMM(BPF_REG_3, 7),

			/* sysctl_get_name arg4 (flags) */
			BPF_MOV64_IMM(BPF_REG_4, BPF_F_SYSCTL_BASE_NAME),

			/* sysctl_get_name(ctx, buf, buf_len, flags) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_name),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -E2BIG, 6),

			/*     buf[0:7] == "tcp_me\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x00656d5f706374ULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_name sysctl:full ok",
		.insns = {
			/* sysctl_get_name arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -24),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 16),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_name arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 17),

			/* sysctl_get_name arg4 (flags) */
			BPF_MOV64_IMM(BPF_REG_4, 0),

			/* sysctl_get_name(ctx, buf, buf_len, flags) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_name),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 16, 14),

			/*     buf[0:8] == "net/ipv4" && */
			BPF_LD_IMM64(BPF_REG_8, 0x347670692f74656eULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 10),

			/*     buf[8:16] == "/tcp_mem" && */
			BPF_LD_IMM64(BPF_REG_8, 0x6d656d5f7063742fULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 8),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 6),

			/*     buf[16:24] == "\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x0ULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 16),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_name sysctl:full E2BIG truncated",
		.insns = {
			/* sysctl_get_name arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -16),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 8),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_name arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 16),

			/* sysctl_get_name arg4 (flags) */
			BPF_MOV64_IMM(BPF_REG_4, 0),

			/* sysctl_get_name(ctx, buf, buf_len, flags) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_name),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -E2BIG, 10),

			/*     buf[0:8] == "net/ipv4" && */
			BPF_LD_IMM64(BPF_REG_8, 0x347670692f74656eULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 6),

			/*     buf[8:16] == "/tcp_me\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x00656d5f7063742fULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 8),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_name sysctl:full E2BIG truncated small",
		.insns = {
			/* sysctl_get_name arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_name arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 7),

			/* sysctl_get_name arg4 (flags) */
			BPF_MOV64_IMM(BPF_REG_4, 0),

			/* sysctl_get_name(ctx, buf, buf_len, flags) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_name),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -E2BIG, 6),

			/*     buf[0:8] == "net/ip\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x000070692f74656eULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_current_value sysctl:read ok, gt",
		.insns = {
			/* sysctl_get_current_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_current_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* sysctl_get_current_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_current_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 6, 6),

			/*     buf[0:6] == "Linux\n\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x000a78756e694cULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_current_value sysctl:read ok, eq",
		.insns = {
			/* sysctl_get_current_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_B, BPF_REG_7, BPF_REG_0, 7),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_current_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 7),

			/* sysctl_get_current_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_current_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 6, 6),

			/*     buf[0:6] == "Linux\n\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x000a78756e694cULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_current_value sysctl:read E2BIG truncated",
		.insns = {
			/* sysctl_get_current_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_H, BPF_REG_7, BPF_REG_0, 6),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_current_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 6),

			/* sysctl_get_current_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_current_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -E2BIG, 6),

			/*     buf[0:6] == "Linux\0") */
			BPF_LD_IMM64(BPF_REG_8, 0x000078756e694cULL),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_get_current_value sysctl:read EINVAL",
		.insns = {
			/* sysctl_get_current_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_current_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* sysctl_get_current_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_current_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 4),

			/*     buf[0:8] is NUL-filled) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 0, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv6/conf/lo/stable_secret", /* -EIO */
		.open_flags = O_RDONLY,
		.result = OP_EPERM,
	},
	{
		.descr = "sysctl_get_current_value sysctl:write ok",
		.fixup_value_insn = 6,
		.insns = {
			/* sysctl_get_current_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_current_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* sysctl_get_current_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_current_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 4, 6),

			/*     buf[0:4] == expected) */
			BPF_LD_IMM64(BPF_REG_8, FIXUP_SYSCTL_VALUE),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_WRONLY,
		.newval = "600", /* same as default, should fail anyway */
		.result = OP_EPERM,
	},
};

static size_t probe_prog_length(const struct bpf_insn *fp)
{
	size_t len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static int fixup_sysctl_value(const char *buf, size_t buf_len,
			      struct bpf_insn *prog, size_t insn_num)
{
	uint32_t value_num = 0;
	uint8_t c, i;

	if (buf_len > sizeof(value_num)) {
		log_err("Value is too big (%zd) to use in fixup", buf_len);
		return -1;
	}

	for (i = 0; i < buf_len; ++i) {
		c = buf[i];
		value_num |= (c << i * 8);
	}

	prog[insn_num].imm = value_num;

	return 0;
}

static int load_sysctl_prog(struct sysctl_test *test, const char *sysctl_path)
{
	struct bpf_insn *prog = test->insns;
	struct bpf_load_program_attr attr;
	int ret;

	memset(&attr, 0, sizeof(struct bpf_load_program_attr));
	attr.prog_type = BPF_PROG_TYPE_CGROUP_SYSCTL;
	attr.insns = prog;
	attr.insns_cnt = probe_prog_length(attr.insns);
	attr.license = "GPL";

	if (test->fixup_value_insn) {
		char buf[128];
		ssize_t len;
		int fd;

		fd = open(sysctl_path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			log_err("open(%s) failed", sysctl_path);
			return -1;
		}
		len = read(fd, buf, sizeof(buf));
		if (len == -1) {
			log_err("read(%s) failed", sysctl_path);
			close(fd);
			return -1;
		}
		close(fd);
		if (fixup_sysctl_value(buf, len, prog, test->fixup_value_insn))
			return -1;
	}

	ret = bpf_load_program_xattr(&attr, bpf_log_buf, BPF_LOG_BUF_SIZE);
	if (ret < 0 && test->result != LOAD_REJECT) {
		log_err(">>> Loading program error.\n"
			">>> Verifier output:\n%s\n-------\n", bpf_log_buf);
	}

	return ret;
}

static int access_sysctl(const char *sysctl_path,
			 const struct sysctl_test *test)
{
	int err = 0;
	int fd;

	fd = open(sysctl_path, test->open_flags | O_CLOEXEC);
	if (fd < 0)
		return fd;

	if (test->open_flags == O_RDONLY) {
		char buf[128];

		if (read(fd, buf, sizeof(buf)) == -1)
			goto err;
	} else if (test->open_flags == O_WRONLY) {
		if (!test->newval) {
			log_err("New value for sysctl is not set");
			goto err;
		}
		if (write(fd, test->newval, strlen(test->newval)) == -1)
			goto err;
	} else {
		log_err("Unexpected sysctl access: neither read nor write");
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	close(fd);
	return err;
}

static int run_test_case(int cgfd, struct sysctl_test *test)
{
	enum bpf_attach_type atype = test->attach_type;
	char sysctl_path[128];
	int progfd = -1;
	int err = 0;

	printf("Test case: %s .. ", test->descr);

	snprintf(sysctl_path, sizeof(sysctl_path), "/proc/sys/%s",
		 test->sysctl);

	progfd = load_sysctl_prog(test, sysctl_path);
	if (progfd < 0) {
		if (test->result == LOAD_REJECT)
			goto out;
		else
			goto err;
	}

	if (bpf_prog_attach(progfd, cgfd, atype, BPF_F_ALLOW_OVERRIDE) == -1) {
		if (test->result == ATTACH_REJECT)
			goto out;
		else
			goto err;
	}

	if (access_sysctl(sysctl_path, test) == -1) {
		if (test->result == OP_EPERM && errno == EPERM)
			goto out;
		else
			goto err;
	}

	if (test->result != SUCCESS) {
		log_err("Unexpected failure");
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	/* Detaching w/o checking return code: best effort attempt. */
	if (progfd != -1)
		bpf_prog_detach(cgfd, atype);
	close(progfd);
	printf("[%s]\n", err ? "FAIL" : "PASS");
	return err;
}

static int run_tests(int cgfd)
{
	int passes = 0;
	int fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (run_test_case(cgfd, &tests[i]))
			++fails;
		else
			++passes;
	}
	printf("Summary: %d PASSED, %d FAILED\n", passes, fails);
	return fails ? -1 : 0;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	if (setup_cgroup_environment())
		goto err;

	cgfd = create_and_get_cgroup(CG_PATH);
	if (cgfd < 0)
		goto err;

	if (join_cgroup(CG_PATH))
		goto err;

	if (run_tests(cgfd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	return err;
}
