// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#pragma clang attribute push (__attribute__((preserve_access_index)), apply_to = record)
struct seq_file;
struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
};

struct bpf_map {
	__u32 id;
	char name[16];
	__u32 max_entries;
};

struct bpf_iter__bpf_map {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
};

struct btf_type {
	__u32 name_off;
};

struct btf_header {
	__u32   str_len;
};

struct btf {
	const char *strings;
	struct btf_type **types;
	struct btf_header hdr;
};

struct bpf_prog_aux {
	__u32 id;
	char name[16];
	const char *attach_func_name;
	struct bpf_prog *dst_prog;
	struct bpf_func_info *func_info;
	struct btf *btf;
};

struct bpf_prog {
	struct bpf_prog_aux *aux;
};

struct bpf_iter__bpf_prog {
	struct bpf_iter_meta *meta;
	struct bpf_prog *prog;
};
#pragma clang attribute pop

static const char *get_name(struct btf *btf, long btf_id, const char *fallback)
{
	struct btf_type **types, *t;
	unsigned int name_off;
	const char *str;

	if (!btf)
		return fallback;
	str = btf->strings;
	types = btf->types;
	bpf_probe_read_kernel(&t, sizeof(t), types + btf_id);
	name_off = BPF_CORE_READ(t, name_off);
	if (name_off >= btf->hdr.str_len)
		return fallback;
	return str + name_off;
}

SEC("iter/bpf_map")
int dump_bpf_map(struct bpf_iter__bpf_map *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	__u64 seq_num = ctx->meta->seq_num;
	struct bpf_map *map = ctx->map;

	if (!map)
		return 0;

	if (seq_num == 0)
		BPF_SEQ_PRINTF(seq, "  id name             max_entries\n");

	BPF_SEQ_PRINTF(seq, "%4u %-16s%6d\n", map->id, map->name, map->max_entries);
	return 0;
}

SEC("iter/bpf_prog")
int dump_bpf_prog(struct bpf_iter__bpf_prog *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	__u64 seq_num = ctx->meta->seq_num;
	struct bpf_prog *prog = ctx->prog;
	struct bpf_prog_aux *aux;

	if (!prog)
		return 0;

	aux = prog->aux;
	if (seq_num == 0)
		BPF_SEQ_PRINTF(seq, "  id name             attached\n");

	BPF_SEQ_PRINTF(seq, "%4u %-16s %s %s\n", aux->id,
		       get_name(aux->btf, aux->func_info[0].type_id, aux->name),
		       aux->attach_func_name, aux->dst_prog->aux->name);
	return 0;
}
char LICENSE[] SEC("license") = "GPL";
