/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __BPF_TOOL_XLATED_DUMPER_H
#define __BPF_TOOL_XLATED_DUMPER_H

#define SYM_MAX_NAME	256

struct bpf_prog_linfo;

struct kernel_sym {
	unsigned long address;
	char name[SYM_MAX_NAME];
};

struct dump_data {
	unsigned long address_call_base;
	struct kernel_sym *sym_mapping;
	__u32 sym_count;
	__u64 *jited_ksyms;
	__u32 nr_jited_ksyms;
	struct btf *btf;
	void *func_info;
	__u32 finfo_rec_size;
	const struct bpf_prog_linfo *prog_linfo;
	char scratch_buff[SYM_MAX_NAME + 8];
};

void kernel_syms_load(struct dump_data *dd);
void kernel_syms_destroy(struct dump_data *dd);
struct kernel_sym *kernel_syms_search(struct dump_data *dd, unsigned long key);
void dump_xlated_json(struct dump_data *dd, void *buf, unsigned int len,
		       bool opcodes, bool linum);
void dump_xlated_plain(struct dump_data *dd, void *buf, unsigned int len,
		       bool opcodes, bool linum);
void dump_xlated_for_graph(struct dump_data *dd, void *buf, void *buf_end,
			   unsigned int start_index,
			   bool opcodes, bool linum);

#endif
