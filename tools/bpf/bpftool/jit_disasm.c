// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Based on:
 *
 * Minimal BPF JIT image disassembler
 *
 * Disassembles BPF JIT compiler emitted opcodes back to asm insn's for
 * debugging or verification purposes.
 *
 * Copyright 2013 Daniel Borkmann <daniel@iogearbox.net>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <bfd.h>
#include <dis-asm.h>
#include <sys/stat.h>
#include <limits.h>
#include <bpf/libbpf.h>

#include "json_writer.h"
#include "main.h"

static void get_exec_path(char *tpath, size_t size)
{
	const char *path = "/proc/self/exe";
	ssize_t len;

	len = readlink(path, tpath, size - 1);
	assert(len > 0);
	tpath[len] = 0;
}

static int oper_count;
static int fprintf_json(void *out, const char *fmt, ...)
{
	va_list ap;
	char *s;

	va_start(ap, fmt);
	if (vasprintf(&s, fmt, ap) < 0)
		return -1;
	va_end(ap);

	if (!oper_count) {
		int i;

		/* Strip trailing spaces */
		i = strlen(s) - 1;
		while (s[i] == ' ')
			s[i--] = '\0';

		jsonw_string_field(json_wtr, "operation", s);
		jsonw_name(json_wtr, "operands");
		jsonw_start_array(json_wtr);
		oper_count++;
	} else if (!strcmp(fmt, ",")) {
		   /* Skip */
	} else {
		jsonw_string(json_wtr, s);
		oper_count++;
	}
	free(s);
	return 0;
}

void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes,
		       const char *arch, const char *disassembler_options,
		       const struct btf *btf,
		       const struct bpf_prog_linfo *prog_linfo,
		       __u64 func_ksym, unsigned int func_idx,
		       bool linum)
{
	const struct bpf_line_info *linfo = NULL;
	disassembler_ftype disassemble;
	struct disassemble_info info;
	unsigned int nr_skip = 0;
	int count, i, pc = 0;
	char tpath[PATH_MAX];
	bfd *bfdf;

	if (!len)
		return;

	memset(tpath, 0, sizeof(tpath));
	get_exec_path(tpath, sizeof(tpath));

	bfdf = bfd_openr(tpath, NULL);
	assert(bfdf);
	assert(bfd_check_format(bfdf, bfd_object));

	if (json_output)
		init_disassemble_info(&info, stdout,
				      (fprintf_ftype) fprintf_json);
	else
		init_disassemble_info(&info, stdout,
				      (fprintf_ftype) fprintf);

	/* Update architecture info for offload. */
	if (arch) {
		const bfd_arch_info_type *inf = bfd_scan_arch(arch);

		if (inf) {
			bfdf->arch_info = inf;
		} else {
			p_err("No libbfd support for %s", arch);
			return;
		}
	}

	info.arch = bfd_get_arch(bfdf);
	info.mach = bfd_get_mach(bfdf);
	if (disassembler_options)
		info.disassembler_options = disassembler_options;
	info.buffer = image;
	info.buffer_length = len;

	disassemble_init_for_target(&info);

#ifdef DISASM_FOUR_ARGS_SIGNATURE
	disassemble = disassembler(info.arch,
				   bfd_big_endian(bfdf),
				   info.mach,
				   bfdf);
#else
	disassemble = disassembler(bfdf);
#endif
	assert(disassemble);

	if (json_output)
		jsonw_start_array(json_wtr);
	do {
		if (prog_linfo) {
			linfo = bpf_prog_linfo__lfind_addr_func(prog_linfo,
								func_ksym + pc,
								func_idx,
								nr_skip);
			if (linfo)
				nr_skip++;
		}

		if (json_output) {
			jsonw_start_object(json_wtr);
			oper_count = 0;
			if (linfo)
				btf_dump_linfo_json(btf, linfo, linum);
			jsonw_name(json_wtr, "pc");
			jsonw_printf(json_wtr, "\"0x%x\"", pc);
		} else {
			if (linfo)
				btf_dump_linfo_plain(btf, linfo, "; ",
						     linum);
			printf("%4x:\t", pc);
		}

		count = disassemble(pc, &info);
		if (json_output) {
			/* Operand array, was started in fprintf_json. Before
			 * that, make sure we have a _null_ value if no operand
			 * other than operation code was present.
			 */
			if (oper_count == 1)
				jsonw_null(json_wtr);
			jsonw_end_array(json_wtr);
		}

		if (opcodes) {
			if (json_output) {
				jsonw_name(json_wtr, "opcodes");
				jsonw_start_array(json_wtr);
				for (i = 0; i < count; ++i)
					jsonw_printf(json_wtr, "\"0x%02hhx\"",
						     (uint8_t)image[pc + i]);
				jsonw_end_array(json_wtr);
			} else {
				printf("\n\t");
				for (i = 0; i < count; ++i)
					printf("%02x ",
					       (uint8_t)image[pc + i]);
			}
		}
		if (json_output)
			jsonw_end_object(json_wtr);
		else
			printf("\n");

		pc += count;
	} while (count > 0 && pc < len);
	if (json_output)
		jsonw_end_array(json_wtr);

	bfd_close(bfdf);
}

int disasm_init(void)
{
	bfd_init();
	return 0;
}
