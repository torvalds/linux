// SPDX-License-Identifier: GPL-2.0-only

#include "util/annotate.h"
#include "util/disasm_bpf.h"
#include "util/symbol.h"
#include <linux/zalloc.h>
#include <string.h>

#if defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)
#define PACKAGE "perf"
#include <bfd.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <dis-asm.h>
#include <errno.h>
#include <linux/btf.h>
#include <tools/dis-asm-compat.h>

#include "util/bpf-event.h"
#include "util/bpf-utils.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/map.h"
#include "util/env.h"
#include "util/util.h"

int symbol__disassemble_bpf(struct symbol *sym, struct annotate_args *args)
{
	struct annotation *notes = symbol__annotation(sym);
	struct bpf_prog_linfo *prog_linfo = NULL;
	struct bpf_prog_info_node *info_node;
	int len = sym->end - sym->start;
	disassembler_ftype disassemble;
	struct map *map = args->ms.map;
	struct perf_bpil *info_linear;
	struct disassemble_info info;
	struct dso *dso = map__dso(map);
	int pc = 0, count, sub_id;
	struct btf *btf = NULL;
	char tpath[PATH_MAX];
	size_t buf_size;
	int nr_skip = 0;
	char *buf;
	bfd *bfdf;
	int ret;
	FILE *s;

	if (dso__binary_type(dso) != DSO_BINARY_TYPE__BPF_PROG_INFO)
		return SYMBOL_ANNOTATE_ERRNO__BPF_INVALID_FILE;

	pr_debug("%s: handling sym %s addr %" PRIx64 " len %" PRIx64 "\n", __func__,
		  sym->name, sym->start, sym->end - sym->start);

	memset(tpath, 0, sizeof(tpath));
	perf_exe(tpath, sizeof(tpath));

	bfdf = bfd_openr(tpath, NULL);
	if (bfdf == NULL)
		abort();

	if (!bfd_check_format(bfdf, bfd_object))
		abort();

	s = open_memstream(&buf, &buf_size);
	if (!s) {
		ret = errno;
		goto out;
	}
	init_disassemble_info_compat(&info, s,
				     (fprintf_ftype) fprintf,
				     fprintf_styled);
	info.arch = bfd_get_arch(bfdf);
	info.mach = bfd_get_mach(bfdf);

	info_node = perf_env__find_bpf_prog_info(dso__bpf_prog(dso)->env,
						 dso__bpf_prog(dso)->id);
	if (!info_node) {
		ret = SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF;
		goto out;
	}
	info_linear = info_node->info_linear;
	sub_id = dso__bpf_prog(dso)->sub_id;

	info.buffer = (void *)(uintptr_t)(info_linear->info.jited_prog_insns);
	info.buffer_length = info_linear->info.jited_prog_len;

	if (info_linear->info.nr_line_info)
		prog_linfo = bpf_prog_linfo__new(&info_linear->info);

	if (info_linear->info.btf_id) {
		struct btf_node *node;

		node = perf_env__find_btf(dso__bpf_prog(dso)->env,
					  info_linear->info.btf_id);
		if (node)
			btf = btf__new((__u8 *)(node->data),
				       node->data_size);
	}

	disassemble_init_for_target(&info);

#ifdef DISASM_FOUR_ARGS_SIGNATURE
	disassemble = disassembler(info.arch,
				   bfd_big_endian(bfdf),
				   info.mach,
				   bfdf);
#else
	disassemble = disassembler(bfdf);
#endif
	if (disassemble == NULL)
		abort();

	fflush(s);
	do {
		const struct bpf_line_info *linfo = NULL;
		struct disasm_line *dl;
		size_t prev_buf_size;
		const char *srcline;
		u64 addr;

		addr = pc + ((u64 *)(uintptr_t)(info_linear->info.jited_ksyms))[sub_id];
		count = disassemble(pc, &info);

		if (prog_linfo)
			linfo = bpf_prog_linfo__lfind_addr_func(prog_linfo,
								addr, sub_id,
								nr_skip);

		if (linfo && btf) {
			srcline = btf__name_by_offset(btf, linfo->line_off);
			nr_skip++;
		} else
			srcline = NULL;

		fprintf(s, "\n");
		prev_buf_size = buf_size;
		fflush(s);

		if (!annotate_opts.hide_src_code && srcline) {
			args->offset = -1;
			args->line = strdup(srcline);
			args->line_nr = 0;
			args->fileloc = NULL;
			args->ms.sym  = sym;
			dl = disasm_line__new(args);
			if (dl) {
				annotation_line__add(&dl->al,
						     &notes->src->source);
			}
		}

		args->offset = pc;
		args->line = buf + prev_buf_size;
		args->line_nr = 0;
		args->fileloc = NULL;
		args->ms.sym  = sym;
		dl = disasm_line__new(args);
		if (dl)
			annotation_line__add(&dl->al, &notes->src->source);

		pc += count;
	} while (count > 0 && pc < len);

	ret = 0;
out:
	free(prog_linfo);
	btf__free(btf);
	fclose(s);
	bfd_close(bfdf);
	return ret;
}
#else // defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)
int symbol__disassemble_bpf(struct symbol *sym __maybe_unused, struct annotate_args *args __maybe_unused)
{
	return SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF;
}
#endif // defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)

int symbol__disassemble_bpf_image(struct symbol *sym, struct annotate_args *args)
{
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *dl;

	args->offset = -1;
	args->line = strdup("to be implemented");
	args->line_nr = 0;
	args->fileloc = NULL;
	dl = disasm_line__new(args);
	if (dl)
		annotation_line__add(&dl->al, &notes->src->source);

	zfree(&args->line);
	return 0;
}
