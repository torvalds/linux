// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <../../../tools/include/linux/filter.h>

char _license[] SEC("license") = "GPL";

struct args {
	__u64 log_buf;
	__u32 log_size;
	int max_entries;
	int map_fd;
	int prog_fd;
};

SEC("syscall")
int bpf_prog(struct args *ctx)
{
	static char license[] = "GPL";
	static struct bpf_insn insns[] = {
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	static union bpf_attr map_create_attr = {
		.map_type = BPF_MAP_TYPE_HASH,
		.key_size = 8,
		.value_size = 8,
	};
	static union bpf_attr map_update_attr = { .map_fd = 1, };
	static __u64 key = 12;
	static __u64 value = 34;
	static union bpf_attr prog_load_attr = {
		.prog_type = BPF_PROG_TYPE_XDP,
		.insn_cnt = sizeof(insns) / sizeof(insns[0]),
	};
	int ret;

	map_create_attr.max_entries = ctx->max_entries;
	prog_load_attr.license = (long) license;
	prog_load_attr.insns = (long) insns;
	prog_load_attr.log_buf = ctx->log_buf;
	prog_load_attr.log_size = ctx->log_size;
	prog_load_attr.log_level = 1;

	ret = bpf_sys_bpf(BPF_MAP_CREATE, &map_create_attr, sizeof(map_create_attr));
	if (ret <= 0)
		return ret;
	ctx->map_fd = ret;
	insns[3].imm = ret;

	map_update_attr.map_fd = ret;
	map_update_attr.key = (long) &key;
	map_update_attr.value = (long) &value;
	ret = bpf_sys_bpf(BPF_MAP_UPDATE_ELEM, &map_update_attr, sizeof(map_update_attr));
	if (ret < 0)
		return ret;

	ret = bpf_sys_bpf(BPF_PROG_LOAD, &prog_load_attr, sizeof(prog_load_attr));
	if (ret <= 0)
		return ret;
	ctx->prog_fd = ret;
	return 1;
}
