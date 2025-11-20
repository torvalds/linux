// SPDX-License-Identifier: GPL-2.0
#include "libbfd.h"
#include "annotate.h"
#include "bpf-event.h"
#include "bpf-utils.h"
#include "debug.h"
#include "dso.h"
#include "env.h"
#include "map.h"
#include "srcline.h"
#include "symbol.h"
#include "symbol_conf.h"
#include "util.h"
#include <tools/dis-asm-compat.h>
#ifdef HAVE_LIBBPF_SUPPORT
#include <bpf/bpf.h>
#include <bpf/btf.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#define PACKAGE "perf"
#include <bfd.h>

/*
 * Implement addr2line using libbfd.
 */
struct a2l_data {
	const char *input;
	u64 addr;

	bool found;
	const char *filename;
	const char *funcname;
	unsigned int line;

	bfd *abfd;
	asymbol **syms;
};

static bool perf_bfd_lock(void *bfd_mutex)
{
	mutex_lock(bfd_mutex);
	return true;
}

static bool perf_bfd_unlock(void *bfd_mutex)
{
	mutex_unlock(bfd_mutex);
	return true;
}

static void perf_bfd_init(void)
{
	static struct mutex bfd_mutex;

	mutex_init_recursive(&bfd_mutex);

	if (bfd_init() != BFD_INIT_MAGIC) {
		pr_err("Error initializing libbfd\n");
		return;
	}
	if (!bfd_thread_init(perf_bfd_lock, perf_bfd_unlock, &bfd_mutex))
		pr_err("Error initializing libbfd threading\n");
}

static void ensure_bfd_init(void)
{
	static pthread_once_t bfd_init_once = PTHREAD_ONCE_INIT;

	pthread_once(&bfd_init_once, perf_bfd_init);
}

static int bfd_error(const char *string)
{
	const char *errmsg;

	errmsg = bfd_errmsg(bfd_get_error());
	fflush(stdout);

	if (string)
		pr_debug("%s: %s\n", string, errmsg);
	else
		pr_debug("%s\n", errmsg);

	return -1;
}

static int slurp_symtab(bfd *abfd, struct a2l_data *a2l)
{
	long storage;
	long symcount;
	asymbol **syms;
	bfd_boolean dynamic = FALSE;

	if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
		return bfd_error(bfd_get_filename(abfd));

	storage = bfd_get_symtab_upper_bound(abfd);
	if (storage == 0L) {
		storage = bfd_get_dynamic_symtab_upper_bound(abfd);
		dynamic = TRUE;
	}
	if (storage < 0L)
		return bfd_error(bfd_get_filename(abfd));

	syms = malloc(storage);
	if (dynamic)
		symcount = bfd_canonicalize_dynamic_symtab(abfd, syms);
	else
		symcount = bfd_canonicalize_symtab(abfd, syms);

	if (symcount < 0) {
		free(syms);
		return bfd_error(bfd_get_filename(abfd));
	}

	a2l->syms = syms;
	return 0;
}

static void find_address_in_section(bfd *abfd, asection *section, void *data)
{
	bfd_vma pc, vma;
	bfd_size_type size;
	struct a2l_data *a2l = data;
	flagword flags;

	if (a2l->found)
		return;

#ifdef bfd_get_section_flags
	flags = bfd_get_section_flags(abfd, section);
#else
	flags = bfd_section_flags(section);
#endif
	if ((flags & SEC_ALLOC) == 0)
		return;

	pc = a2l->addr;
#ifdef bfd_get_section_vma
	vma = bfd_get_section_vma(abfd, section);
#else
	vma = bfd_section_vma(section);
#endif
#ifdef bfd_get_section_size
	size = bfd_get_section_size(section);
#else
	size = bfd_section_size(section);
#endif

	if (pc < vma || pc >= vma + size)
		return;

	a2l->found = bfd_find_nearest_line(abfd, section, a2l->syms, pc - vma,
					   &a2l->filename, &a2l->funcname,
					   &a2l->line);

	if (a2l->filename && !strlen(a2l->filename))
		a2l->filename = NULL;
}

static struct a2l_data *addr2line_init(const char *path)
{
	bfd *abfd;
	struct a2l_data *a2l = NULL;

	ensure_bfd_init();
	abfd = bfd_openr(path, NULL);
	if (abfd == NULL)
		return NULL;

	if (!bfd_check_format(abfd, bfd_object))
		goto out;

	a2l = zalloc(sizeof(*a2l));
	if (a2l == NULL)
		goto out;

	a2l->abfd = abfd;
	a2l->input = strdup(path);
	if (a2l->input == NULL)
		goto out;

	if (slurp_symtab(abfd, a2l))
		goto out;

	return a2l;

out:
	if (a2l) {
		zfree((char **)&a2l->input);
		free(a2l);
	}
	bfd_close(abfd);
	return NULL;
}

static void addr2line_cleanup(struct a2l_data *a2l)
{
	if (a2l->abfd)
		bfd_close(a2l->abfd);
	zfree((char **)&a2l->input);
	zfree(&a2l->syms);
	free(a2l);
}

static int inline_list__append_dso_a2l(struct dso *dso,
				       struct inline_node *node,
				       struct symbol *sym)
{
	struct a2l_data *a2l = dso__a2l(dso);
	struct symbol *inline_sym = new_inline_sym(dso, sym, a2l->funcname);
	char *srcline = NULL;

	if (a2l->filename)
		srcline = srcline_from_fileline(a2l->filename, a2l->line);

	return inline_list__append(inline_sym, srcline, node);
}

int libbfd__addr2line(const char *dso_name, u64 addr,
		      char **file, unsigned int *line, struct dso *dso,
		      bool unwind_inlines, struct inline_node *node,
		      struct symbol *sym)
{
	int ret = 0;
	struct a2l_data *a2l = dso__a2l(dso);

	if (!a2l) {
		a2l = addr2line_init(dso_name);
		dso__set_a2l(dso, a2l);
	}

	if (a2l == NULL) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("addr2line_init failed for %s\n", dso_name);
		return 0;
	}

	a2l->addr = addr;
	a2l->found = false;

	bfd_map_over_sections(a2l->abfd, find_address_in_section, a2l);

	if (!a2l->found)
		return 0;

	if (unwind_inlines) {
		int cnt = 0;

		if (node && inline_list__append_dso_a2l(dso, node, sym))
			return 0;

		while (bfd_find_inliner_info(a2l->abfd, &a2l->filename,
					     &a2l->funcname, &a2l->line) &&
		       cnt++ < MAX_INLINE_NEST) {

			if (a2l->filename && !strlen(a2l->filename))
				a2l->filename = NULL;

			if (node != NULL) {
				if (inline_list__append_dso_a2l(dso, node, sym))
					return 0;
				// found at least one inline frame
				ret = 1;
			}
		}
	}

	if (file) {
		*file = a2l->filename ? strdup(a2l->filename) : NULL;
		ret = *file ? 1 : 0;
	}

	if (line)
		*line = a2l->line;

	return ret;
}

void dso__free_a2l_libbfd(struct dso *dso)
{
	struct a2l_data *a2l = dso__a2l(dso);

	if (!a2l)
		return;

	addr2line_cleanup(a2l);

	dso__set_a2l(dso, NULL);
}

static int bfd_symbols__cmpvalue(const void *a, const void *b)
{
	const asymbol *as = *(const asymbol **)a, *bs = *(const asymbol **)b;

	if (bfd_asymbol_value(as) != bfd_asymbol_value(bs))
		return bfd_asymbol_value(as) - bfd_asymbol_value(bs);

	return bfd_asymbol_name(as)[0] - bfd_asymbol_name(bs)[0];
}

static int bfd2elf_binding(asymbol *symbol)
{
	if (symbol->flags & BSF_WEAK)
		return STB_WEAK;
	if (symbol->flags & BSF_GLOBAL)
		return STB_GLOBAL;
	if (symbol->flags & BSF_LOCAL)
		return STB_LOCAL;
	return -1;
}

int dso__load_bfd_symbols(struct dso *dso, const char *debugfile)
{
	int err = -1;
	long symbols_size, symbols_count, i;
	asection *section;
	asymbol **symbols, *sym;
	struct symbol *symbol;
	bfd *abfd;
	u64 start, len;

	ensure_bfd_init();
	abfd = bfd_openr(debugfile, NULL);
	if (!abfd)
		return -1;

	if (!bfd_check_format(abfd, bfd_object)) {
		pr_debug2("%s: cannot read %s bfd file.\n", __func__,
			  dso__long_name(dso));
		goto out_close;
	}

	if (bfd_get_flavour(abfd) == bfd_target_elf_flavour)
		goto out_close;

	symbols_size = bfd_get_symtab_upper_bound(abfd);
	if (symbols_size == 0) {
		bfd_close(abfd);
		return 0;
	}

	if (symbols_size < 0)
		goto out_close;

	symbols = malloc(symbols_size);
	if (!symbols)
		goto out_close;

	symbols_count = bfd_canonicalize_symtab(abfd, symbols);
	if (symbols_count < 0)
		goto out_free;

	section = bfd_get_section_by_name(abfd, ".text");
	if (section) {
		for (i = 0; i < symbols_count; ++i) {
			if (!strcmp(bfd_asymbol_name(symbols[i]), "__ImageBase") ||
			    !strcmp(bfd_asymbol_name(symbols[i]), "__image_base__"))
				break;
		}
		if (i < symbols_count) {
			/* PE symbols can only have 4 bytes, so use .text high bits */
			u64 text_offset = (section->vma - (u32)section->vma)
				+ (u32)bfd_asymbol_value(symbols[i]);
			dso__set_text_offset(dso, text_offset);
			dso__set_text_end(dso, (section->vma - text_offset) + section->size);
		} else {
			dso__set_text_offset(dso, section->vma - section->filepos);
			dso__set_text_end(dso, section->filepos + section->size);
		}
	}

	qsort(symbols, symbols_count, sizeof(asymbol *), bfd_symbols__cmpvalue);

#ifdef bfd_get_section
#define bfd_asymbol_section bfd_get_section
#endif
	for (i = 0; i < symbols_count; ++i) {
		sym = symbols[i];
		section = bfd_asymbol_section(sym);
		if (bfd2elf_binding(sym) < 0)
			continue;

		while (i + 1 < symbols_count &&
		       bfd_asymbol_section(symbols[i + 1]) == section &&
		       bfd2elf_binding(symbols[i + 1]) < 0)
			i++;

		if (i + 1 < symbols_count &&
		    bfd_asymbol_section(symbols[i + 1]) == section)
			len = symbols[i + 1]->value - sym->value;
		else
			len = section->size - sym->value;

		start = bfd_asymbol_value(sym) - dso__text_offset(dso);
		symbol = symbol__new(start, len, bfd2elf_binding(sym), STT_FUNC,
				     bfd_asymbol_name(sym));
		if (!symbol)
			goto out_free;

		symbols__insert(dso__symbols(dso), symbol);
	}
#ifdef bfd_get_section
#undef bfd_asymbol_section
#endif

	symbols__fixup_end(dso__symbols(dso), false);
	symbols__fixup_duplicate(dso__symbols(dso));
	dso__set_adjust_symbols(dso, true);

	err = 0;
out_free:
	free(symbols);
out_close:
	bfd_close(abfd);
	return err;
}

int libbfd__read_build_id(const char *filename, struct build_id *bid, bool block)
{
	size_t size = sizeof(bid->data);
	int err = -1, fd;
	bfd *abfd;

	fd = open(filename, block ? O_RDONLY : (O_RDONLY | O_NONBLOCK));
	if (fd < 0)
		return -1;

	ensure_bfd_init();
	abfd = bfd_fdopenr(filename, /*target=*/NULL, fd);
	if (!abfd)
		return -1;

	if (!bfd_check_format(abfd, bfd_object)) {
		pr_debug2("%s: cannot read %s bfd file.\n", __func__, filename);
		goto out_close;
	}

	if (!abfd->build_id || abfd->build_id->size > size)
		goto out_close;

	memcpy(bid->data, abfd->build_id->data, abfd->build_id->size);
	memset(bid->data + abfd->build_id->size, 0, size - abfd->build_id->size);
	err = bid->size = abfd->build_id->size;

out_close:
	bfd_close(abfd);
	return err;
}

int libbfd_filename__read_debuglink(const char *filename, char *debuglink,
				    size_t size)
{
	int err = -1;
	asection *section;
	bfd *abfd;

	ensure_bfd_init();
	abfd = bfd_openr(filename, NULL);
	if (!abfd)
		return -1;

	if (!bfd_check_format(abfd, bfd_object)) {
		pr_debug2("%s: cannot read %s bfd file.\n", __func__, filename);
		goto out_close;
	}

	section = bfd_get_section_by_name(abfd, ".gnu_debuglink");
	if (!section)
		goto out_close;

	if (section->size > size)
		goto out_close;

	if (!bfd_get_section_contents(abfd, section, debuglink, 0,
				      section->size))
		goto out_close;

	err = 0;

out_close:
	bfd_close(abfd);
	return err;
}

int symbol__disassemble_bpf_libbfd(struct symbol *sym __maybe_unused,
				   struct annotate_args *args  __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
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

	ensure_bfd_init();
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
#else
	return SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF;
#endif
}
