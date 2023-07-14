// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_internal.h>

#include "disasm.h"
#include "json_writer.h"
#include "main.h"
#include "xlated_dumper.h"

static int kernel_syms_cmp(const void *sym_a, const void *sym_b)
{
	return ((struct kernel_sym *)sym_a)->address -
	       ((struct kernel_sym *)sym_b)->address;
}

void kernel_syms_load(struct dump_data *dd)
{
	struct kernel_sym *sym;
	char buff[256];
	void *tmp, *address;
	FILE *fp;

	fp = fopen("/proc/kallsyms", "r");
	if (!fp)
		return;

	while (fgets(buff, sizeof(buff), fp)) {
		tmp = libbpf_reallocarray(dd->sym_mapping, dd->sym_count + 1,
					  sizeof(*dd->sym_mapping));
		if (!tmp) {
out:
			free(dd->sym_mapping);
			dd->sym_mapping = NULL;
			fclose(fp);
			return;
		}
		dd->sym_mapping = tmp;
		sym = &dd->sym_mapping[dd->sym_count];
		if (sscanf(buff, "%p %*c %s", &address, sym->name) != 2)
			continue;
		sym->address = (unsigned long)address;
		if (!strcmp(sym->name, "__bpf_call_base")) {
			dd->address_call_base = sym->address;
			/* sysctl kernel.kptr_restrict was set */
			if (!sym->address)
				goto out;
		}
		if (sym->address)
			dd->sym_count++;
	}

	fclose(fp);

	qsort(dd->sym_mapping, dd->sym_count,
	      sizeof(*dd->sym_mapping), kernel_syms_cmp);
}

void kernel_syms_destroy(struct dump_data *dd)
{
	free(dd->sym_mapping);
}

struct kernel_sym *kernel_syms_search(struct dump_data *dd,
				      unsigned long key)
{
	struct kernel_sym sym = {
		.address = key,
	};

	return dd->sym_mapping ?
	       bsearch(&sym, dd->sym_mapping, dd->sym_count,
		       sizeof(*dd->sym_mapping), kernel_syms_cmp) : NULL;
}

static void __printf(2, 3) print_insn(void *private_data, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static void __printf(2, 3)
print_insn_for_graph(void *private_data, const char *fmt, ...)
{
	char buf[64], *p;
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	p = buf;
	while (*p != '\0') {
		if (*p == '\n') {
			memmove(p + 3, p, strlen(buf) + 1 - (p - buf));
			/* Align each instruction dump row left. */
			*p++ = '\\';
			*p++ = 'l';
			/* Output multiline concatenation. */
			*p++ = '\\';
		} else if (*p == '<' || *p == '>' || *p == '|' || *p == '&') {
			memmove(p + 1, p, strlen(buf) + 1 - (p - buf));
			/* Escape special character. */
			*p++ = '\\';
		}

		p++;
	}

	printf("%s", buf);
}

static void __printf(2, 3)
print_insn_json(void *private_data, const char *fmt, ...)
{
	unsigned int l = strlen(fmt);
	char chomped_fmt[l];
	va_list args;

	va_start(args, fmt);
	if (l > 0) {
		strncpy(chomped_fmt, fmt, l - 1);
		chomped_fmt[l - 1] = '\0';
	}
	jsonw_vprintf_enquote(json_wtr, chomped_fmt, args);
	va_end(args);
}

static const char *print_call_pcrel(struct dump_data *dd,
				    struct kernel_sym *sym,
				    unsigned long address,
				    const struct bpf_insn *insn)
{
	if (!dd->nr_jited_ksyms)
		/* Do not show address for interpreted programs */
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			"%+d", insn->off);
	else if (sym)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "%+d#%s", insn->off, sym->name);
	else
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "%+d#0x%lx", insn->off, address);
	return dd->scratch_buff;
}

static const char *print_call_helper(struct dump_data *dd,
				     struct kernel_sym *sym,
				     unsigned long address)
{
	if (sym)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "%s", sym->name);
	else
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "0x%lx", address);
	return dd->scratch_buff;
}

static const char *print_call(void *private_data,
			      const struct bpf_insn *insn)
{
	struct dump_data *dd = private_data;
	unsigned long address = dd->address_call_base + insn->imm;
	struct kernel_sym *sym;

	if (insn->src_reg == BPF_PSEUDO_CALL &&
	    (__u32) insn->imm < dd->nr_jited_ksyms && dd->jited_ksyms)
		address = dd->jited_ksyms[insn->imm];

	sym = kernel_syms_search(dd, address);
	if (insn->src_reg == BPF_PSEUDO_CALL)
		return print_call_pcrel(dd, sym, address, insn);
	else
		return print_call_helper(dd, sym, address);
}

static const char *print_imm(void *private_data,
			     const struct bpf_insn *insn,
			     __u64 full_imm)
{
	struct dump_data *dd = private_data;

	if (insn->src_reg == BPF_PSEUDO_MAP_FD)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "map[id:%u]", insn->imm);
	else if (insn->src_reg == BPF_PSEUDO_MAP_VALUE)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "map[id:%u][0]+%u", insn->imm, (insn + 1)->imm);
	else if (insn->src_reg == BPF_PSEUDO_MAP_IDX_VALUE)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "map[idx:%u]+%u", insn->imm, (insn + 1)->imm);
	else if (insn->src_reg == BPF_PSEUDO_FUNC)
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "subprog[%+d]", insn->imm);
	else
		snprintf(dd->scratch_buff, sizeof(dd->scratch_buff),
			 "0x%llx", (unsigned long long)full_imm);
	return dd->scratch_buff;
}

void dump_xlated_json(struct dump_data *dd, void *buf, unsigned int len,
		      bool opcodes, bool linum)
{
	const struct bpf_prog_linfo *prog_linfo = dd->prog_linfo;
	const struct bpf_insn_cbs cbs = {
		.cb_print	= print_insn_json,
		.cb_call	= print_call,
		.cb_imm		= print_imm,
		.private_data	= dd,
	};
	struct bpf_func_info *record;
	struct bpf_insn *insn = buf;
	struct btf *btf = dd->btf;
	bool double_insn = false;
	unsigned int nr_skip = 0;
	char func_sig[1024];
	unsigned int i;

	jsonw_start_array(json_wtr);
	record = dd->func_info;
	for (i = 0; i < len / sizeof(*insn); i++) {
		if (double_insn) {
			double_insn = false;
			continue;
		}
		double_insn = insn[i].code == (BPF_LD | BPF_IMM | BPF_DW);

		jsonw_start_object(json_wtr);

		if (btf && record) {
			if (record->insn_off == i) {
				btf_dumper_type_only(btf, record->type_id,
						     func_sig,
						     sizeof(func_sig));
				if (func_sig[0] != '\0') {
					jsonw_name(json_wtr, "proto");
					jsonw_string(json_wtr, func_sig);
				}
				record = (void *)record + dd->finfo_rec_size;
			}
		}

		if (prog_linfo) {
			const struct bpf_line_info *linfo;

			linfo = bpf_prog_linfo__lfind(prog_linfo, i, nr_skip);
			if (linfo) {
				btf_dump_linfo_json(btf, linfo, linum);
				nr_skip++;
			}
		}

		jsonw_name(json_wtr, "disasm");
		print_bpf_insn(&cbs, insn + i, true);

		if (opcodes) {
			jsonw_name(json_wtr, "opcodes");
			jsonw_start_object(json_wtr);

			jsonw_name(json_wtr, "code");
			jsonw_printf(json_wtr, "\"0x%02hhx\"", insn[i].code);

			jsonw_name(json_wtr, "src_reg");
			jsonw_printf(json_wtr, "\"0x%hhx\"", insn[i].src_reg);

			jsonw_name(json_wtr, "dst_reg");
			jsonw_printf(json_wtr, "\"0x%hhx\"", insn[i].dst_reg);

			jsonw_name(json_wtr, "off");
			print_hex_data_json((uint8_t *)(&insn[i].off), 2);

			jsonw_name(json_wtr, "imm");
			if (double_insn && i < len - 1)
				print_hex_data_json((uint8_t *)(&insn[i].imm),
						    12);
			else
				print_hex_data_json((uint8_t *)(&insn[i].imm),
						    4);
			jsonw_end_object(json_wtr);
		}
		jsonw_end_object(json_wtr);
	}
	jsonw_end_array(json_wtr);
}

void dump_xlated_plain(struct dump_data *dd, void *buf, unsigned int len,
		       bool opcodes, bool linum)
{
	const struct bpf_prog_linfo *prog_linfo = dd->prog_linfo;
	const struct bpf_insn_cbs cbs = {
		.cb_print	= print_insn,
		.cb_call	= print_call,
		.cb_imm		= print_imm,
		.private_data	= dd,
	};
	struct bpf_func_info *record;
	struct bpf_insn *insn = buf;
	struct btf *btf = dd->btf;
	unsigned int nr_skip = 0;
	bool double_insn = false;
	char func_sig[1024];
	unsigned int i;

	record = dd->func_info;
	for (i = 0; i < len / sizeof(*insn); i++) {
		if (double_insn) {
			double_insn = false;
			continue;
		}

		if (btf && record) {
			if (record->insn_off == i) {
				btf_dumper_type_only(btf, record->type_id,
						     func_sig,
						     sizeof(func_sig));
				if (func_sig[0] != '\0')
					printf("%s:\n", func_sig);
				record = (void *)record + dd->finfo_rec_size;
			}
		}

		if (prog_linfo) {
			const struct bpf_line_info *linfo;

			linfo = bpf_prog_linfo__lfind(prog_linfo, i, nr_skip);
			if (linfo) {
				btf_dump_linfo_plain(btf, linfo, "; ",
						     linum);
				nr_skip++;
			}
		}

		double_insn = insn[i].code == (BPF_LD | BPF_IMM | BPF_DW);

		printf("% 4d: ", i);
		print_bpf_insn(&cbs, insn + i, true);

		if (opcodes) {
			printf("       ");
			fprint_hex(stdout, insn + i, 8, " ");
			if (double_insn && i < len - 1) {
				printf(" ");
				fprint_hex(stdout, insn + i + 1, 8, " ");
			}
			printf("\n");
		}
	}
}

void dump_xlated_for_graph(struct dump_data *dd, void *buf_start, void *buf_end,
			   unsigned int start_idx,
			   bool opcodes, bool linum)
{
	const struct bpf_insn_cbs cbs = {
		.cb_print	= print_insn_for_graph,
		.cb_call	= print_call,
		.cb_imm		= print_imm,
		.private_data	= dd,
	};
	const struct bpf_prog_linfo *prog_linfo = dd->prog_linfo;
	const struct bpf_line_info *last_linfo = NULL;
	struct bpf_func_info *record = dd->func_info;
	struct bpf_insn *insn_start = buf_start;
	struct bpf_insn *insn_end = buf_end;
	struct bpf_insn *cur = insn_start;
	struct btf *btf = dd->btf;
	bool double_insn = false;
	char func_sig[1024];

	for (; cur <= insn_end; cur++) {
		unsigned int insn_off;

		if (double_insn) {
			double_insn = false;
			continue;
		}
		double_insn = cur->code == (BPF_LD | BPF_IMM | BPF_DW);

		insn_off = (unsigned int)(cur - insn_start + start_idx);
		if (btf && record) {
			if (record->insn_off == insn_off) {
				btf_dumper_type_only(btf, record->type_id,
						     func_sig,
						     sizeof(func_sig));
				if (func_sig[0] != '\0')
					printf("; %s:\\l\\\n", func_sig);
				record = (void *)record + dd->finfo_rec_size;
			}
		}

		if (prog_linfo) {
			const struct bpf_line_info *linfo;

			linfo = bpf_prog_linfo__lfind(prog_linfo, insn_off, 0);
			if (linfo && linfo != last_linfo) {
				btf_dump_linfo_dotlabel(btf, linfo, linum);
				last_linfo = linfo;
			}
		}

		printf("%d: ", insn_off);
		print_bpf_insn(&cbs, cur, true);

		if (opcodes) {
			printf("\\ \\ \\ \\ ");
			fprint_hex(stdout, cur, 8, " ");
			if (double_insn && cur <= insn_end - 1) {
				printf(" ");
				fprint_hex(stdout, cur + 1, 8, " ");
			}
			printf("\\l\\\n");
		}

		if (cur != insn_end)
			printf("| ");
	}
}
