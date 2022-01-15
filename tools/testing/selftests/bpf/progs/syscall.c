// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <../../../tools/include/linux/filter.h>
#include <linux/btf.h>

char _license[] SEC("license") = "GPL";

struct args {
	__u64 log_buf;
	__u32 log_size;
	int max_entries;
	int map_fd;
	int prog_fd;
	int btf_fd;
};

#define BTF_INFO_ENC(kind, kind_flag, vlen) \
	((!!(kind_flag) << 31) | ((kind) << 24) | ((vlen) & BTF_MAX_VLEN))
#define BTF_TYPE_ENC(name, info, size_or_type) (name), (info), (size_or_type)
#define BTF_INT_ENC(encoding, bits_offset, nr_bits) \
	((encoding) << 24 | (bits_offset) << 16 | (nr_bits))
#define BTF_TYPE_INT_ENC(name, encoding, bits_offset, bits, sz) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_INT, 0, 0), sz), \
	BTF_INT_ENC(encoding, bits_offset, bits)

static int btf_load(void)
{
	struct btf_blob {
		struct btf_header btf_hdr;
		__u32 types[8];
		__u32 str;
	} raw_btf = {
		.btf_hdr = {
			.magic = BTF_MAGIC,
			.version = BTF_VERSION,
			.hdr_len = sizeof(struct btf_header),
			.type_len = sizeof(__u32) * 8,
			.str_off = sizeof(__u32) * 8,
			.str_len = sizeof(__u32),
		},
		.types = {
			/* long */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 64, 8),  /* [1] */
			/* unsigned long */
			BTF_TYPE_INT_ENC(0, 0, 0, 64, 8),  /* [2] */
		},
	};
	static union bpf_attr btf_load_attr = {
		.btf_size = sizeof(raw_btf),
	};

	btf_load_attr.btf = (long)&raw_btf;
	return bpf_sys_bpf(BPF_BTF_LOAD, &btf_load_attr, sizeof(btf_load_attr));
}

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
		.btf_key_type_id = 1,
		.btf_value_type_id = 2,
	};
	static union bpf_attr map_update_attr = { .map_fd = 1, };
	static __u64 key = 12;
	static __u64 value = 34;
	static union bpf_attr prog_load_attr = {
		.prog_type = BPF_PROG_TYPE_XDP,
		.insn_cnt = sizeof(insns) / sizeof(insns[0]),
	};
	int ret;

	ret = btf_load();
	if (ret <= 0)
		return ret;

	ctx->btf_fd = ret;
	map_create_attr.max_entries = ctx->max_entries;
	map_create_attr.btf_fd = ret;

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
