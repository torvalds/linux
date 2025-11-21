// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <objtool/arch.h>
#include <objtool/check.h>
#include <objtool/disas.h>
#include <objtool/warn.h>

#include <bfd.h>
#include <linux/string.h>
#include <tools/dis-asm-compat.h>

struct disas_context {
	struct objtool_file *file;
	disassembler_ftype disassembler;
	struct disassemble_info info;
};

#define DINFO_FPRINTF(dinfo, ...)	\
	((*(dinfo)->fprintf_func)((dinfo)->stream, __VA_ARGS__))

/*
 * Initialize disassemble info arch, mach (32 or 64-bit) and options.
 */
int disas_info_init(struct disassemble_info *dinfo,
		    int arch, int mach32, int mach64,
		    const char *options)
{
	struct disas_context *dctx = dinfo->application_data;
	struct objtool_file *file = dctx->file;

	dinfo->arch = arch;

	switch (file->elf->ehdr.e_ident[EI_CLASS]) {
	case ELFCLASS32:
		dinfo->mach = mach32;
		break;
	case ELFCLASS64:
		dinfo->mach = mach64;
		break;
	default:
		return -1;
	}

	dinfo->disassembler_options = options;

	return 0;
}

struct disas_context *disas_context_create(struct objtool_file *file)
{
	struct disas_context *dctx;
	struct disassemble_info *dinfo;
	int err;

	dctx = malloc(sizeof(*dctx));
	if (!dctx) {
		WARN("failed to allocate disassembly context");
		return NULL;
	}

	dctx->file = file;
	dinfo = &dctx->info;

	init_disassemble_info_compat(dinfo, stdout,
				     (fprintf_ftype)fprintf,
				     fprintf_styled);

	dinfo->read_memory_func = buffer_read_memory;
	dinfo->application_data = dctx;

	/*
	 * bfd_openr() is not used to avoid doing ELF data processing
	 * and caching that has already being done. Here, we just need
	 * to identify the target file so we call an arch specific
	 * function to fill some disassemble info (arch, mach).
	 */

	dinfo->arch = bfd_arch_unknown;
	dinfo->mach = 0;

	err = arch_disas_info_init(dinfo);
	if (err || dinfo->arch == bfd_arch_unknown || dinfo->mach == 0) {
		WARN("failed to init disassembly arch");
		goto error;
	}

	dinfo->endian = (file->elf->ehdr.e_ident[EI_DATA] == ELFDATA2MSB) ?
		BFD_ENDIAN_BIG : BFD_ENDIAN_LITTLE;

	disassemble_init_for_target(dinfo);

	dctx->disassembler = disassembler(dinfo->arch,
					  dinfo->endian == BFD_ENDIAN_BIG,
					  dinfo->mach, NULL);
	if (!dctx->disassembler) {
		WARN("failed to create disassembler function");
		goto error;
	}

	return dctx;

error:
	free(dctx);
	return NULL;
}

void disas_context_destroy(struct disas_context *dctx)
{
	free(dctx);
}

/*
 * Disassemble a single instruction. Return the size of the instruction.
 */
static size_t disas_insn(struct disas_context *dctx,
			 struct instruction *insn)
{
	disassembler_ftype disasm = dctx->disassembler;
	struct disassemble_info *dinfo = &dctx->info;

	if (insn->type == INSN_NOP) {
		DINFO_FPRINTF(dinfo, "nop%d", insn->len);
		return insn->len;
	}

	/*
	 * Set the disassembler buffer to read data from the section
	 * containing the instruction to disassemble.
	 */
	dinfo->buffer = insn->sec->data->d_buf;
	dinfo->buffer_vma = 0;
	dinfo->buffer_length = insn->sec->sh.sh_size;

	return disasm(insn->offset, &dctx->info);
}

/*
 * Disassemble a function.
 */
static void disas_func(struct disas_context *dctx, struct symbol *func)
{
	struct instruction *insn;
	size_t addr;

	printf("%s:\n", func->name);
	sym_for_each_insn(dctx->file, func, insn) {
		addr = insn->offset;
		printf(" %6lx:  %s+0x%-6lx      ",
		       addr, func->name, addr - func->offset);
		disas_insn(dctx, insn);
		printf("\n");
	}
	printf("\n");
}

/*
 * Disassemble all warned functions.
 */
void disas_warned_funcs(struct disas_context *dctx)
{
	struct symbol *sym;

	if (!dctx)
		return;

	for_each_sym(dctx->file->elf, sym) {
		if (sym->warned)
			disas_func(dctx, sym);
	}
}
