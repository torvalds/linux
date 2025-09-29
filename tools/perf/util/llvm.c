// SPDX-License-Identifier: GPL-2.0
#include "llvm.h"
#include "annotate.h"
#include "debug.h"
#include "dso.h"
#include "map.h"
#include "namespaces.h"
#include "srcline.h"
#include "symbol.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/zalloc.h>

#ifdef HAVE_LIBLLVM_SUPPORT
#include "llvm-c-helpers.h"
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#endif

#ifdef HAVE_LIBLLVM_SUPPORT
static void free_llvm_inline_frames(struct llvm_a2l_frame *inline_frames,
				    int num_frames)
{
	if (inline_frames != NULL) {
		for (int i = 0; i < num_frames; ++i) {
			zfree(&inline_frames[i].filename);
			zfree(&inline_frames[i].funcname);
		}
		zfree(&inline_frames);
	}
}
#endif

int llvm__addr2line(const char *dso_name __maybe_unused, u64 addr __maybe_unused,
		     char **file __maybe_unused, unsigned int *line __maybe_unused,
		     struct dso *dso __maybe_unused, bool unwind_inlines __maybe_unused,
		     struct inline_node *node __maybe_unused, struct symbol *sym __maybe_unused)
{
#ifdef HAVE_LIBLLVM_SUPPORT
	struct llvm_a2l_frame *inline_frames = NULL;
	int num_frames = llvm_addr2line(dso_name, addr, file, line,
					node && unwind_inlines, &inline_frames);

	if (num_frames == 0 || !inline_frames) {
		/* Error, or we didn't want inlines. */
		return num_frames;
	}

	for (int i = 0; i < num_frames; ++i) {
		struct symbol *inline_sym =
			new_inline_sym(dso, sym, inline_frames[i].funcname);
		char *srcline = NULL;

		if (inline_frames[i].filename) {
			srcline =
				srcline_from_fileline(inline_frames[i].filename,
						      inline_frames[i].line);
		}
		if (inline_list__append(inline_sym, srcline, node) != 0) {
			free_llvm_inline_frames(inline_frames, num_frames);
			return 0;
		}
	}
	free_llvm_inline_frames(inline_frames, num_frames);

	return num_frames;
#else
	return -1;
#endif
}

void dso__free_a2l_llvm(struct dso *dso __maybe_unused)
{
	/* Nothing to free. */
}


#if defined(HAVE_LIBLLVM_SUPPORT)
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

static u8 *
read_symbol(const char *filename, struct map *map, struct symbol *sym,
	    u64 *len, bool *is_64bit)
{
	struct dso *dso = map__dso(map);
	struct nscookie nsc;
	u64 start = map__rip_2objdump(map, sym->start);
	u64 end = map__rip_2objdump(map, sym->end);
	int fd, count;
	u8 *buf = NULL;
	struct find_file_offset_data data = {
		.ip = start,
	};

	*is_64bit = false;

	nsinfo__mountns_enter(dso__nsinfo(dso), &nsc);
	fd = open(filename, O_RDONLY);
	nsinfo__mountns_exit(&nsc);
	if (fd < 0)
		return NULL;

	if (file__read_maps(fd, /*exe=*/true, find_file_offset, &data,
			    is_64bit) == 0)
		goto err;

	*len = end - start;
	buf = malloc(*len);
	if (buf == NULL)
		goto err;

	count = pread(fd, buf, *len, data.offset);
	close(fd);
	fd = -1;

	if ((u64)count != *len)
		goto err;

	return buf;

err:
	if (fd >= 0)
		close(fd);
	free(buf);
	return NULL;
}
#endif

/*
 * Whenever LLVM wants to resolve an address into a symbol, it calls this
 * callback. We don't ever actually _return_ anything (in particular, because
 * it puts quotation marks around what we return), but we use this as a hint
 * that there is a branch or PC-relative address in the expression that we
 * should add some textual annotation for after the instruction. The caller
 * will use this information to add the actual annotation.
 */
#ifdef HAVE_LIBLLVM_SUPPORT
struct symbol_lookup_storage {
	u64 branch_addr;
	u64 pcrel_load_addr;
};

static const char *
symbol_lookup_callback(void *disinfo, uint64_t value,
		       uint64_t *ref_type,
		       uint64_t address __maybe_unused,
		       const char **ref __maybe_unused)
{
	struct symbol_lookup_storage *storage = disinfo;

	if (*ref_type == LLVMDisassembler_ReferenceType_In_Branch)
		storage->branch_addr = value;
	else if (*ref_type == LLVMDisassembler_ReferenceType_In_PCrel_Load)
		storage->pcrel_load_addr = value;
	*ref_type = LLVMDisassembler_ReferenceType_InOut_None;
	return NULL;
}
#endif

int symbol__disassemble_llvm(const char *filename, struct symbol *sym,
			     struct annotate_args *args __maybe_unused)
{
#ifdef HAVE_LIBLLVM_SUPPORT
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms.map;
	struct dso *dso = map__dso(map);
	u64 start = map__rip_2objdump(map, sym->start);
	u8 *buf;
	u64 len;
	u64 pc;
	bool is_64bit;
	char triplet[64];
	char disasm_buf[2048];
	size_t disasm_len;
	struct disasm_line *dl;
	LLVMDisasmContextRef disasm = NULL;
	struct symbol_lookup_storage storage;
	char *line_storage = NULL;
	size_t line_storage_len = 0;
	int ret = -1;

	if (args->options->objdump_path)
		return -1;

	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllDisassemblers();

	buf = read_symbol(filename, map, sym, &len, &is_64bit);
	if (buf == NULL)
		return -1;

	if (arch__is(args->arch, "x86")) {
		if (is_64bit)
			scnprintf(triplet, sizeof(triplet), "x86_64-pc-linux");
		else
			scnprintf(triplet, sizeof(triplet), "i686-pc-linux");
	} else {
		scnprintf(triplet, sizeof(triplet), "%s-linux-gnu",
			  args->arch->name);
	}

	disasm = LLVMCreateDisasm(triplet, &storage, 0, NULL,
				  symbol_lookup_callback);
	if (disasm == NULL)
		goto err;

	if (args->options->disassembler_style &&
	    !strcmp(args->options->disassembler_style, "intel"))
		LLVMSetDisasmOptions(disasm,
				     LLVMDisassembler_Option_AsmPrinterVariant);

	/*
	 * This needs to be set after AsmPrinterVariant, due to a bug in LLVM;
	 * setting AsmPrinterVariant makes a new instruction printer, making it
	 * forget about the PrintImmHex flag (which is applied before if both
	 * are given to the same call).
	 */
	LLVMSetDisasmOptions(disasm, LLVMDisassembler_Option_PrintImmHex);

	/* add the function address and name */
	scnprintf(disasm_buf, sizeof(disasm_buf), "%#"PRIx64" <%s>:",
		  start, sym->name);

	args->offset = -1;
	args->line = disasm_buf;
	args->line_nr = 0;
	args->fileloc = NULL;
	args->ms.sym = sym;

	dl = disasm_line__new(args);
	if (dl == NULL)
		goto err;

	annotation_line__add(&dl->al, &notes->src->source);

	pc = start;
	for (u64 offset = 0; offset < len; ) {
		unsigned int ins_len;

		storage.branch_addr = 0;
		storage.pcrel_load_addr = 0;

		ins_len = LLVMDisasmInstruction(disasm, buf + offset,
						len - offset, pc,
						disasm_buf, sizeof(disasm_buf));
		if (ins_len == 0)
			goto err;
		disasm_len = strlen(disasm_buf);

		if (storage.branch_addr != 0) {
			char *name = llvm_name_for_code(dso, filename,
							storage.branch_addr);
			if (name != NULL) {
				disasm_len += scnprintf(disasm_buf + disasm_len,
							sizeof(disasm_buf) -
								disasm_len,
							" <%s>", name);
				free(name);
			}
		}
		if (storage.pcrel_load_addr != 0) {
			char *name = llvm_name_for_data(dso, filename,
							storage.pcrel_load_addr);
			disasm_len += scnprintf(disasm_buf + disasm_len,
						sizeof(disasm_buf) - disasm_len,
						"  # %#"PRIx64,
						storage.pcrel_load_addr);
			if (name) {
				disasm_len += scnprintf(disasm_buf + disasm_len,
							sizeof(disasm_buf) -
							disasm_len,
							" <%s>", name);
				free(name);
			}
		}

		args->offset = offset;
		args->line = expand_tabs(disasm_buf, &line_storage,
					 &line_storage_len);
		args->line_nr = 0;
		args->fileloc = NULL;
		args->ms.sym = sym;

		llvm_addr2line(filename, pc, &args->fileloc,
			       (unsigned int *)&args->line_nr, false, NULL);

		dl = disasm_line__new(args);
		if (dl == NULL)
			goto err;

		annotation_line__add(&dl->al, &notes->src->source);

		free(args->fileloc);
		pc += ins_len;
		offset += ins_len;
	}

	ret = 0;

err:
	LLVMDisasmDispose(disasm);
	free(buf);
	free(line_storage);
	return ret;
#else // HAVE_LIBLLVM_SUPPORT
	pr_debug("The LLVM disassembler isn't linked in for %s in %s\n",
		 sym->name, filename);
	return -1;
#endif
}
