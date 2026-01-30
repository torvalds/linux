// SPDX-License-Identifier: GPL-2.0
#include "capstone.h"
#include "annotate.h"
#include "addr_location.h"
#include "debug.h"
#include "disasm.h"
#include "dso.h"
#include "machine.h"
#include "map.h"
#include "namespaces.h"
#include "print_insn.h"
#include "symbol.h"
#include "thread.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>

#include <capstone/capstone.h>

#ifdef LIBCAPSTONE_DLOPEN
static void *perf_cs_dll_handle(void)
{
	static bool dll_handle_init;
	static void *dll_handle;

	if (!dll_handle_init) {
		dll_handle_init = true;
		dll_handle = dlopen("libcapstone.so", RTLD_LAZY);
		if (!dll_handle)
			pr_debug("dlopen failed for libcapstone.so\n");
	}
	return dll_handle;
}
#endif

static enum cs_err perf_cs_open(enum cs_arch arch, enum cs_mode mode, csh *handle)
{
#ifndef LIBCAPSTONE_DLOPEN
	return cs_open(arch, mode, handle);
#else
	static bool fn_init;
	static enum cs_err (*fn)(enum cs_arch arch, enum cs_mode mode, csh *handle);

	if (!fn_init) {
		fn = dlsym(perf_cs_dll_handle(), "cs_open");
		if (!fn)
			pr_debug("dlsym failed for cs_open\n");
		fn_init = true;
	}
	if (!fn)
		return CS_ERR_HANDLE;
	return fn(arch, mode, handle);
#endif
}

static enum cs_err perf_cs_option(csh handle, enum cs_opt_type type, size_t value)
{
#ifndef LIBCAPSTONE_DLOPEN
	return cs_option(handle, type, value);
#else
	static bool fn_init;
	static enum cs_err (*fn)(csh handle, enum cs_opt_type type, size_t value);

	if (!fn_init) {
		fn = dlsym(perf_cs_dll_handle(), "cs_option");
		if (!fn)
			pr_debug("dlsym failed for cs_option\n");
		fn_init = true;
	}
	if (!fn)
		return CS_ERR_HANDLE;
	return fn(handle, type, value);
#endif
}

static size_t perf_cs_disasm(csh handle, const uint8_t *code, size_t code_size,
			uint64_t address, size_t count, struct cs_insn **insn)
{
#ifndef LIBCAPSTONE_DLOPEN
	return cs_disasm(handle, code, code_size, address, count, insn);
#else
	static bool fn_init;
	static enum cs_err (*fn)(csh handle, const uint8_t *code, size_t code_size,
				 uint64_t address, size_t count, struct cs_insn **insn);

	if (!fn_init) {
		fn = dlsym(perf_cs_dll_handle(), "cs_disasm");
		if (!fn)
			pr_debug("dlsym failed for cs_disasm\n");
		fn_init = true;
	}
	if (!fn)
		return CS_ERR_HANDLE;
	return fn(handle, code, code_size, address, count, insn);
#endif
}

static void perf_cs_free(struct cs_insn *insn, size_t count)
{
#ifndef LIBCAPSTONE_DLOPEN
	cs_free(insn, count);
#else
	static bool fn_init;
	static void (*fn)(struct cs_insn *insn, size_t count);

	if (!fn_init) {
		fn = dlsym(perf_cs_dll_handle(), "cs_free");
		if (!fn)
			pr_debug("dlsym failed for cs_free\n");
		fn_init = true;
	}
	if (!fn)
		return;
	fn(insn, count);
#endif
}

static enum cs_err perf_cs_close(csh *handle)
{
#ifndef LIBCAPSTONE_DLOPEN
	return cs_close(handle);
#else
	static bool fn_init;
	static enum cs_err (*fn)(csh *handle);

	if (!fn_init) {
		fn = dlsym(perf_cs_dll_handle(), "cs_close");
		if (!fn)
			pr_debug("dlsym failed for cs_close\n");
		fn_init = true;
	}
	if (!fn)
		return CS_ERR_HANDLE;
	return fn(handle);
#endif
}

static int capstone_init(struct machine *machine, csh *cs_handle, bool is64,
			 bool disassembler_style)
{
	enum cs_arch arch;
	enum cs_mode mode;

	if (machine__is(machine, "x86_64") && is64) {
		arch = CS_ARCH_X86;
		mode = CS_MODE_64;
	} else if (machine__normalized_is(machine, "x86")) {
		arch = CS_ARCH_X86;
		mode = CS_MODE_32;
	} else if (machine__normalized_is(machine, "arm64")) {
		arch = CS_ARCH_ARM64;
		mode = CS_MODE_ARM;
	} else if (machine__normalized_is(machine, "arm")) {
		arch = CS_ARCH_ARM;
		mode = CS_MODE_ARM + CS_MODE_V8;
	} else if (machine__normalized_is(machine, "s390")) {
		arch = CS_ARCH_SYSZ;
		mode = CS_MODE_BIG_ENDIAN;
	} else {
		return -1;
	}

	if (perf_cs_open(arch, mode, cs_handle) != CS_ERR_OK) {
		pr_warning_once("cs_open failed\n");
		return -1;
	}

	if (machine__normalized_is(machine, "x86")) {
		/*
		 * In case of using capstone_init while symbol__disassemble
		 * setting CS_OPT_SYNTAX_ATT depends if disassembler_style opts
		 * is set via annotation args
		 */
		if (disassembler_style)
			perf_cs_option(*cs_handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
		/*
		 * Resolving address operands to symbols is implemented
		 * on x86 by investigating instruction details.
		 */
		perf_cs_option(*cs_handle, CS_OPT_DETAIL, CS_OPT_ON);
	}

	return 0;
}

static size_t print_insn_x86(struct thread *thread, u8 cpumode, struct cs_insn *insn,
			     int print_opts, FILE *fp)
{
	struct addr_location al;
	size_t printed = 0;

	if (insn->detail && insn->detail->x86.op_count == 1) {
		struct cs_x86_op *op = &insn->detail->x86.operands[0];

		addr_location__init(&al);
		if (op->type == X86_OP_IMM &&
		    thread__find_symbol(thread, cpumode, op->imm, &al)) {
			printed += fprintf(fp, "%s ", insn[0].mnemonic);
			printed += symbol__fprintf_symname_offs(al.sym, &al, fp);
			if (print_opts & PRINT_INSN_IMM_HEX)
				printed += fprintf(fp, " [%#" PRIx64 "]", op->imm);
			addr_location__exit(&al);
			return printed;
		}
		addr_location__exit(&al);
	}

	printed += fprintf(fp, "%s %s", insn[0].mnemonic, insn[0].op_str);
	return printed;
}


ssize_t capstone__fprintf_insn_asm(struct machine *machine __maybe_unused,
				   struct thread *thread __maybe_unused,
				   u8 cpumode __maybe_unused, bool is64bit __maybe_unused,
				   const uint8_t *code __maybe_unused,
				   size_t code_size __maybe_unused,
				   uint64_t ip __maybe_unused, int *lenp __maybe_unused,
				   int print_opts __maybe_unused, FILE *fp __maybe_unused)
{
	size_t printed;
	struct cs_insn *insn;
	csh cs_handle;
	size_t count;
	int ret;

	/* TODO: Try to initiate capstone only once but need a proper place. */
	ret = capstone_init(machine, &cs_handle, is64bit, true);
	if (ret < 0)
		return ret;

	count = perf_cs_disasm(cs_handle, code, code_size, ip, 1, &insn);
	if (count > 0) {
		if (machine__normalized_is(machine, "x86"))
			printed = print_insn_x86(thread, cpumode, &insn[0], print_opts, fp);
		else
			printed = fprintf(fp, "%s %s", insn[0].mnemonic, insn[0].op_str);
		if (lenp)
			*lenp = insn->size;
		perf_cs_free(insn, count);
	} else {
		printed = -1;
	}

	perf_cs_close(&cs_handle);
	return printed;
}

static void print_capstone_detail(struct cs_insn *insn, char *buf, size_t len,
				  struct annotate_args *args, u64 addr)
{
	int i;
	struct map *map = args->ms->map;
	struct symbol *sym;

	/* TODO: support more architectures */
	if (!arch__is_x86(args->arch))
		return;

	if (insn->detail == NULL)
		return;

	for (i = 0; i < insn->detail->x86.op_count; i++) {
		struct cs_x86_op *op = &insn->detail->x86.operands[i];
		u64 orig_addr;

		if (op->type != X86_OP_MEM)
			continue;

		/* only print RIP-based global symbols for now */
		if (op->mem.base != X86_REG_RIP)
			continue;

		/* get the target address */
		orig_addr = addr + insn->size + op->mem.disp;
		addr = map__objdump_2mem(map, orig_addr);

		if (dso__kernel(map__dso(map))) {
			/*
			 * The kernel maps can be split into sections, let's
			 * find the map first and the search the symbol.
			 */
			map = maps__find(map__kmaps(map), addr);
			if (map == NULL)
				continue;
		}

		/* convert it to map-relative address for search */
		addr = map__map_ip(map, addr);

		sym = map__find_symbol(map, addr);
		if (sym == NULL)
			continue;

		if (addr == sym->start) {
			scnprintf(buf, len, "\t# %"PRIx64" <%s>",
				  orig_addr, sym->name);
		} else {
			scnprintf(buf, len, "\t# %"PRIx64" <%s+%#"PRIx64">",
				  orig_addr, sym->name, addr - sym->start);
		}
		break;
	}
}

struct find_file_offset_data {
	u64 ip;
	u64 offset;
};

/* This will be called for each PHDR in an ELF binary */
static int find_file_offset(u64 start, u64 len, u64 pgoff, void *arg)
{
	struct find_file_offset_data *data = arg;

	if (start <= data->ip && data->ip < start + len) {
		data->offset = pgoff + data->ip - start;
		return 1;
	}
	return 0;
}

int symbol__disassemble_capstone(const char *filename __maybe_unused,
				 struct symbol *sym __maybe_unused,
				 struct annotate_args *args __maybe_unused)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms->map;
	struct dso *dso = map__dso(map);
	u64 start = map__rip_2objdump(map, sym->start);
	u64 offset;
	int i, count, free_count;
	bool is_64bit = false;
	bool needs_cs_close = false;
	/* Malloc-ed buffer containing instructions read from disk. */
	u8 *code_buf = NULL;
	/* Pointer to code to be disassembled. */
	const u8 *buf;
	u64 buf_len;
	csh handle;
	struct cs_insn *insn = NULL;
	char disasm_buf[512];
	struct disasm_line *dl;
	bool disassembler_style = false;

	if (args->options->objdump_path)
		return -1;

	buf = dso__read_symbol(dso, filename, map, sym,
			       &code_buf, &buf_len, &is_64bit);
	if (buf == NULL)
		return errno;

	/* add the function address and name */
	scnprintf(disasm_buf, sizeof(disasm_buf), "%#"PRIx64" <%s>:",
		  start, sym->name);

	args->offset = -1;
	args->line = disasm_buf;
	args->line_nr = 0;
	args->fileloc = NULL;
	args->ms->sym = sym;

	dl = disasm_line__new(args);
	if (dl == NULL)
		goto err;

	annotation_line__add(&dl->al, &notes->src->source);

	if (!args->options->disassembler_style ||
	    !strcmp(args->options->disassembler_style, "att"))
		disassembler_style = true;

	if (capstone_init(maps__machine(thread__maps(args->ms->thread)), &handle, is_64bit,
			  disassembler_style) < 0)
		goto err;

	needs_cs_close = true;

	free_count = count = perf_cs_disasm(handle, buf, buf_len, start, buf_len, &insn);
	for (i = 0, offset = 0; i < count; i++) {
		int printed;

		printed = scnprintf(disasm_buf, sizeof(disasm_buf),
				    "       %-7s %s",
				    insn[i].mnemonic, insn[i].op_str);
		print_capstone_detail(&insn[i], disasm_buf + printed,
				      sizeof(disasm_buf) - printed, args,
				      start + offset);

		args->offset = offset;
		args->line = disasm_buf;

		dl = disasm_line__new(args);
		if (dl == NULL)
			goto err;

		annotation_line__add(&dl->al, &notes->src->source);

		offset += insn[i].size;
	}

	/* It failed in the middle: probably due to unknown instructions */
	if (offset != buf_len) {
		struct list_head *list = &notes->src->source;

		/* Discard all lines and fallback to objdump */
		while (!list_empty(list)) {
			dl = list_first_entry(list, struct disasm_line, al.node);

			list_del_init(&dl->al.node);
			disasm_line__free(dl);
		}
		count = -1;
	}

out:
	if (needs_cs_close) {
		perf_cs_close(&handle);
		if (free_count > 0)
			perf_cs_free(insn, free_count);
	}
	free(code_buf);
	return count < 0 ? count : 0;

err:
	if (needs_cs_close) {
		struct disasm_line *tmp;

		/*
		 * It probably failed in the middle of the above loop.
		 * Release any resources it might add.
		 */
		list_for_each_entry_safe(dl, tmp, &notes->src->source, al.node) {
			list_del(&dl->al.node);
			disasm_line__free(dl);
		}
	}
	count = -1;
	goto out;
}

int symbol__disassemble_capstone_powerpc(const char *filename __maybe_unused,
					 struct symbol *sym __maybe_unused,
					 struct annotate_args *args __maybe_unused)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms->map;
	struct dso *dso = map__dso(map);
	struct nscookie nsc;
	u64 start = map__rip_2objdump(map, sym->start);
	u64 end = map__rip_2objdump(map, sym->end);
	u64 len = end - start;
	u64 offset;
	int i, fd, count;
	bool is_64bit = false;
	bool needs_cs_close = false;
	u8 *buf = NULL;
	struct find_file_offset_data data = {
		.ip = start,
	};
	csh handle;
	char disasm_buf[512];
	struct disasm_line *dl;
	u32 *line;
	bool disassembler_style = false;

	if (args->options->objdump_path)
		return -1;

	nsinfo__mountns_enter(dso__nsinfo(dso), &nsc);
	fd = open(filename, O_RDONLY);
	nsinfo__mountns_exit(&nsc);
	if (fd < 0)
		return -1;

	if (file__read_maps(fd, /*exe=*/true, find_file_offset, &data,
			    &is_64bit) == 0)
		goto err;

	if (!args->options->disassembler_style ||
	    !strcmp(args->options->disassembler_style, "att"))
		disassembler_style = true;

	if (capstone_init(maps__machine(thread__maps(args->ms->thread)), &handle, is_64bit,
			  disassembler_style) < 0)
		goto err;

	needs_cs_close = true;

	buf = malloc(len);
	if (buf == NULL)
		goto err;

	count = pread(fd, buf, len, data.offset);
	close(fd);
	fd = -1;

	if ((u64)count != len)
		goto err;

	line = (u32 *)buf;

	/* add the function address and name */
	scnprintf(disasm_buf, sizeof(disasm_buf), "%#"PRIx64" <%s>:",
		  start, sym->name);

	args->offset = -1;
	args->line = disasm_buf;
	args->line_nr = 0;
	args->fileloc = NULL;
	args->ms->sym = sym;

	dl = disasm_line__new(args);
	if (dl == NULL)
		goto err;

	annotation_line__add(&dl->al, &notes->src->source);

	/*
	 * TODO: enable disassm for powerpc
	 * count = cs_disasm(handle, buf, len, start, len, &insn);
	 *
	 * For now, only binary code is saved in disassembled line
	 * to be used in "type" and "typeoff" sort keys. Each raw code
	 * is 32 bit instruction. So use "len/4" to get the number of
	 * entries.
	 */
	count = len/4;

	for (i = 0, offset = 0; i < count; i++) {
		args->offset = offset;
		sprintf(args->line, "%x", line[i]);

		dl = disasm_line__new(args);
		if (dl == NULL)
			break;

		annotation_line__add(&dl->al, &notes->src->source);

		offset += 4;
	}

	/* It failed in the middle */
	if (offset != len) {
		struct list_head *list = &notes->src->source;

		/* Discard all lines and fallback to objdump */
		while (!list_empty(list)) {
			dl = list_first_entry(list, struct disasm_line, al.node);

			list_del_init(&dl->al.node);
			disasm_line__free(dl);
		}
		count = -1;
	}

out:
	if (needs_cs_close)
		perf_cs_close(&handle);
	free(buf);
	return count < 0 ? count : 0;

err:
	if (fd >= 0)
		close(fd);
	count = -1;
	goto out;
}
