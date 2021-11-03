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
#include <bpf/libbpf.h>

#include <bpf/bpf_endian.h>
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
	const char *prog_file;
	enum bpf_attach_type attach_type;
	const char *sysctl;
	int open_flags;
	int seek;
	const char *newval;
	const char *oldval;
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
		.sysctl = "kernel/domainname",
		.open_flags = O_WRONLY,
		.newval = "(none)", /* same as default, should fail anyway */
		.result = OP_EPERM,
	},
	{
		.descr = "ctx:write sysctl:write read ok narrow",
		.insns = {
			/* u64 w = (u16)write & 1; */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			BPF_LDX_MEM(BPF_H, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write)),
#else
			BPF_LDX_MEM(BPF_H, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write) + 2),
#endif
			BPF_ALU64_IMM(BPF_AND, BPF_REG_7, 1),
			/* return 1 - w; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_0, BPF_REG_7),
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
		.descr = "ctx:file_pos sysctl:read read ok",
		.insns = {
			/* If (file_pos == X) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, file_pos)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 3, 2),

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
		.seek = 3,
		.result = SUCCESS,
	},
	{
		.descr = "ctx:file_pos sysctl:read read ok narrow",
		.insns = {
			/* If (file_pos == X) */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, file_pos)),
#else
			BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, file_pos) + 3),
#endif
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 4, 2),

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
		.seek = 4,
		.result = SUCCESS,
	},
	{
		.descr = "ctx:file_pos sysctl:read write ok",
		.insns = {
			/* file_pos = X */
			BPF_MOV64_IMM(BPF_REG_0, 2),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sysctl, file_pos)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.oldval = "nux\n",
		.result = SUCCESS,
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x7463705f6d656d00ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x7463705f6d650000ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x6e65742f69707634ULL)),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 10),

			/*     buf[8:16] == "/tcp_mem" && */
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x2f7463705f6d656dULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x6e65742f69707634ULL)),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 6),

			/*     buf[8:16] == "/tcp_me\0") */
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x2f7463705f6d6500ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x6e65742f69700000ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x4c696e75780a0000ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x4c696e75780a0000ULL)),
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
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x4c696e7578000000ULL)),
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
	{
		.descr = "sysctl_get_new_value sysctl:read EINVAL",
		.insns = {
			/* sysctl_get_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* sysctl_get_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_new_value),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 2),

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
		.descr = "sysctl_get_new_value sysctl:write ok",
		.insns = {
			/* sysctl_get_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 4),

			/* sysctl_get_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_new_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 3, 4),

			/*     buf[0:4] == "606\0") */
			BPF_LDX_MEM(BPF_W, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9,
				    bpf_ntohl(0x36303600), 2),

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
		.newval = "606",
		.result = OP_EPERM,
	},
	{
		.descr = "sysctl_get_new_value sysctl:write ok long",
		.insns = {
			/* sysctl_get_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -24),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 24),

			/* sysctl_get_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_new_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 23, 14),

			/*     buf[0:8] == "3000000 " && */
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x3330303030303020ULL)),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 10),

			/*     buf[8:16] == "4000000 " && */
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x3430303030303020ULL)),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 8),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 6),

			/*     buf[16:24] == "6000000\0") */
			BPF_LD_IMM64(BPF_REG_8,
				     bpf_be64_to_cpu(0x3630303030303000ULL)),
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 16),
			BPF_JMP_REG(BPF_JNE, BPF_REG_8, BPF_REG_9, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_WRONLY,
		.newval = "3000000 4000000 6000000",
		.result = OP_EPERM,
	},
	{
		.descr = "sysctl_get_new_value sysctl:write E2BIG",
		.insns = {
			/* sysctl_get_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_B, BPF_REG_7, BPF_REG_0, 3),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_get_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 3),

			/* sysctl_get_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_get_new_value),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -E2BIG, 4),

			/*     buf[0:3] == "60\0") */
			BPF_LDX_MEM(BPF_W, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9,
				    bpf_ntohl(0x36300000), 2),

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
		.newval = "606",
		.result = OP_EPERM,
	},
	{
		.descr = "sysctl_set_new_value sysctl:read EINVAL",
		.insns = {
			/* sysctl_set_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x36303000)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_set_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 3),

			/* sysctl_set_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_set_new_value),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl_set_new_value sysctl:write ok",
		.fixup_value_insn = 2,
		.insns = {
			/* sysctl_set_new_value arg2 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_LD_IMM64(BPF_REG_0, FIXUP_SYSCTL_VALUE),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_2, BPF_REG_7),

			/* sysctl_set_new_value arg3 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_3, 3),

			/* sysctl_set_new_value(ctx, buf, buf_len) */
			BPF_EMIT_CALL(BPF_FUNC_sysctl_set_new_value),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_WRONLY,
		.newval = "606",
		.result = SUCCESS,
	},
	{
		"bpf_strtoul one number string",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x36303000)),
			BPF_STX_MEM(BPF_W, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 3, 4),
			/*     res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 600, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtoul multi number string",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			/* "600 602\0" */
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3630302036303200ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 8),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 3, 18),
			/*     res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 600, 16),

			/*     arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_ALU64_REG(BPF_ADD, BPF_REG_7, BPF_REG_0),
			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/*     arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 8),
			BPF_ALU64_REG(BPF_SUB, BPF_REG_2, BPF_REG_0),

			/*     arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/*     arg4 (res) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -16),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/*     if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 4, 4),
			/*         res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 602, 2),

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
		"bpf_strtoul buf_len = 0, reject",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x36303000)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 0),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = LOAD_REJECT,
	},
	{
		"bpf_strtoul supported base, ok",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x30373700)),
			BPF_STX_MEM(BPF_W, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 8),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 3, 4),
			/*     res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 63, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtoul unsupported base, EINVAL",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x36303000)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 3),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtoul buf with spaces only, EINVAL",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x0d0c0a09)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtoul negative number, EINVAL",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			/* " -6\0" */
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x0a2d3600)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtoul),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -EINVAL, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtol negative number, ok",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			/* " -6\0" */
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x0a2d3600)),
			BPF_STX_MEM(BPF_W, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 10),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtol),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 3, 4),
			/*     res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, -6, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtol hex number, ok",
		.insns = {
			/* arg1 (buf) */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			/* "0xfe" */
			BPF_MOV64_IMM(BPF_REG_0,
				      bpf_ntohl(0x30786665)),
			BPF_STX_MEM(BPF_W, BPF_REG_7, BPF_REG_0, 0),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 4),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtol),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 4, 4),
			/*     res == expected) */
			BPF_LDX_MEM(BPF_DW, BPF_REG_9, BPF_REG_7, 0),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_9, 254, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtol max long",
		.insns = {
			/* arg1 (buf) 9223372036854775807 */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -24),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3932323333373230ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3336383534373735ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 8),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3830370000000000ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 16),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 19),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtol),

			/* if (ret == expected && */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 19, 6),
			/*     res == expected) */
			BPF_LD_IMM64(BPF_REG_8, 0x7fffffffffffffffULL),
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
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"bpf_strtol overflow, ERANGE",
		.insns = {
			/* arg1 (buf) 9223372036854775808 */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_10),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -24),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3932323333373230ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3336383534373735ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 8),
			BPF_LD_IMM64(BPF_REG_0,
				     bpf_be64_to_cpu(0x3830380000000000ULL)),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 16),

			BPF_MOV64_REG(BPF_REG_1, BPF_REG_7),

			/* arg2 (buf_len) */
			BPF_MOV64_IMM(BPF_REG_2, 19),

			/* arg3 (flags) */
			BPF_MOV64_IMM(BPF_REG_3, 0),

			/* arg4 (res) */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, -8),
			BPF_STX_MEM(BPF_DW, BPF_REG_7, BPF_REG_0, 0),
			BPF_MOV64_REG(BPF_REG_4, BPF_REG_7),

			BPF_EMIT_CALL(BPF_FUNC_strtol),

			/* if (ret == expected) */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_0, -ERANGE, 2),

			/* return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),

			/* else return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		"C prog: deny all writes",
		.prog_file = "./test_sysctl_prog.o",
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_WRONLY,
		.newval = "123 456 789",
		.result = OP_EPERM,
	},
	{
		"C prog: deny access by name",
		.prog_file = "./test_sysctl_prog.o",
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/route/mtu_expires",
		.open_flags = O_RDONLY,
		.result = OP_EPERM,
	},
	{
		"C prog: read tcp_mem",
		.prog_file = "./test_sysctl_prog.o",
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "net/ipv4/tcp_mem",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
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
	union {
		uint8_t raw[sizeof(uint64_t)];
		uint64_t num;
	} value = {};

	if (buf_len > sizeof(value)) {
		log_err("Value is too big (%zd) to use in fixup", buf_len);
		return -1;
	}
	if (prog[insn_num].code != (BPF_LD | BPF_DW | BPF_IMM)) {
		log_err("Can fixup only BPF_LD_IMM64 insns");
		return -1;
	}

	memcpy(value.raw, buf, buf_len);
	prog[insn_num].imm = (uint32_t)value.num;
	prog[insn_num + 1].imm = (uint32_t)(value.num >> 32);

	return 0;
}

static int load_sysctl_prog_insns(struct sysctl_test *test,
				  const char *sysctl_path)
{
	struct bpf_insn *prog = test->insns;
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	int ret, insn_cnt;

	insn_cnt = probe_prog_length(prog);

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

	opts.log_buf = bpf_log_buf;
	opts.log_size = BPF_LOG_BUF_SIZE;

	ret = bpf_prog_load(BPF_PROG_TYPE_CGROUP_SYSCTL, NULL, "GPL", prog, insn_cnt, &opts);
	if (ret < 0 && test->result != LOAD_REJECT) {
		log_err(">>> Loading program error.\n"
			">>> Verifier output:\n%s\n-------\n", bpf_log_buf);
	}

	return ret;
}

static int load_sysctl_prog_file(struct sysctl_test *test)
{
	struct bpf_object *obj;
	int prog_fd;

	if (bpf_prog_test_load(test->prog_file, BPF_PROG_TYPE_CGROUP_SYSCTL, &obj, &prog_fd)) {
		if (test->result != LOAD_REJECT)
			log_err(">>> Loading program (%s) error.\n",
				test->prog_file);
		return -1;
	}

	return prog_fd;
}

static int load_sysctl_prog(struct sysctl_test *test, const char *sysctl_path)
{
		return test->prog_file
			? load_sysctl_prog_file(test)
			: load_sysctl_prog_insns(test, sysctl_path);
}

static int access_sysctl(const char *sysctl_path,
			 const struct sysctl_test *test)
{
	int err = 0;
	int fd;

	fd = open(sysctl_path, test->open_flags | O_CLOEXEC);
	if (fd < 0)
		return fd;

	if (test->seek && lseek(fd, test->seek, SEEK_SET) == -1) {
		log_err("lseek(%d) failed", test->seek);
		goto err;
	}

	if (test->open_flags == O_RDONLY) {
		char buf[128];

		if (read(fd, buf, sizeof(buf)) == -1)
			goto err;
		if (test->oldval &&
		    strncmp(buf, test->oldval, strlen(test->oldval))) {
			log_err("Read value %s != %s", buf, test->oldval);
			goto err;
		}
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

	errno = 0;
	if (access_sysctl(sysctl_path, test) == -1) {
		if (test->result == OP_EPERM && errno == EPERM)
			goto out;
		else
			goto err;
	}

	if (test->result != SUCCESS) {
		log_err("Unexpected success");
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

	cgfd = cgroup_setup_and_join(CG_PATH);
	if (cgfd < 0)
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
