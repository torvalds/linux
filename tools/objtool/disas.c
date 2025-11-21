// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#define _GNU_SOURCE

#include <objtool/arch.h>
#include <objtool/check.h>
#include <objtool/disas.h>
#include <objtool/warn.h>

#include <bfd.h>
#include <linux/string.h>
#include <tools/dis-asm-compat.h>

/*
 * Size of the buffer for storing the result of disassembling
 * a single instruction.
 */
#define DISAS_RESULT_SIZE	1024

struct disas_context {
	struct objtool_file *file;
	struct instruction *insn;
	char result[DISAS_RESULT_SIZE];
	disassembler_ftype disassembler;
	struct disassemble_info info;
};

static int sprint_name(char *str, const char *name, unsigned long offset)
{
	int len;

	if (offset)
		len = sprintf(str, "%s+0x%lx", name, offset);
	else
		len = sprintf(str, "%s", name);

	return len;
}

#define DINFO_FPRINTF(dinfo, ...)	\
	((*(dinfo)->fprintf_func)((dinfo)->stream, __VA_ARGS__))

static int disas_result_fprintf(struct disas_context *dctx,
				const char *fmt, va_list ap)
{
	char *buf = dctx->result;
	int avail, len;

	len = strlen(buf);
	if (len >= DISAS_RESULT_SIZE - 1) {
		WARN_FUNC(dctx->insn->sec, dctx->insn->offset,
			  "disassembly buffer is full");
		return -1;
	}
	avail = DISAS_RESULT_SIZE - len;

	len = vsnprintf(buf + len, avail, fmt, ap);
	if (len < 0 || len >= avail) {
		WARN_FUNC(dctx->insn->sec, dctx->insn->offset,
			  "disassembly buffer is truncated");
		return -1;
	}

	return 0;
}

static int disas_fprintf(void *stream, const char *fmt, ...)
{
	va_list arg;
	int rv;

	va_start(arg, fmt);
	rv = disas_result_fprintf(stream, fmt, arg);
	va_end(arg);

	return rv;
}

/*
 * For init_disassemble_info_compat().
 */
static int disas_fprintf_styled(void *stream,
				enum disassembler_style style,
				const char *fmt, ...)
{
	va_list arg;
	int rv;

	va_start(arg, fmt);
	rv = disas_result_fprintf(stream, fmt, arg);
	va_end(arg);

	return rv;
}

static void disas_print_addr_sym(struct section *sec, struct symbol *sym,
				 bfd_vma addr, struct disassemble_info *dinfo)
{
	char symstr[1024];
	char *str;

	if (sym) {
		sprint_name(symstr, sym->name, addr - sym->offset);
		DINFO_FPRINTF(dinfo, "0x%lx <%s>", addr, symstr);
	} else {
		str = offstr(sec, addr);
		DINFO_FPRINTF(dinfo, "0x%lx <%s>", addr, str);
		free(str);
	}
}

static void disas_print_addr_noreloc(bfd_vma addr,
				     struct disassemble_info *dinfo)
{
	struct disas_context *dctx = dinfo->application_data;
	struct instruction *insn = dctx->insn;
	struct symbol *sym = NULL;

	if (insn->sym && addr >= insn->sym->offset &&
	    addr < insn->sym->offset + insn->sym->len) {
		sym = insn->sym;
	}

	disas_print_addr_sym(insn->sec, sym, addr, dinfo);
}

static void disas_print_addr_reloc(bfd_vma addr, struct disassemble_info *dinfo)
{
	struct disas_context *dctx = dinfo->application_data;
	struct instruction *insn = dctx->insn;
	unsigned long offset;
	struct reloc *reloc;
	char symstr[1024];
	char *str;

	reloc = find_reloc_by_dest_range(dctx->file->elf, insn->sec,
					 insn->offset, insn->len);
	if (!reloc) {
		/*
		 * There is no relocation for this instruction although
		 * the address to resolve points to the next instruction.
		 * So this is an effective reference to the next IP, for
		 * example: "lea 0x0(%rip),%rdi". The kernel can reference
		 * the next IP with _THIS_IP_ macro.
		 */
		DINFO_FPRINTF(dinfo, "0x%lx <_THIS_IP_>", addr);
		return;
	}

	offset = arch_insn_adjusted_addend(insn, reloc);

	/*
	 * If the relocation symbol is a section name (for example ".bss")
	 * then we try to further resolve the name.
	 */
	if (reloc->sym->type == STT_SECTION) {
		str = offstr(reloc->sym->sec, reloc->sym->offset + offset);
		DINFO_FPRINTF(dinfo, "0x%lx <%s>", addr, str);
		free(str);
	} else {
		sprint_name(symstr, reloc->sym->name, offset);
		DINFO_FPRINTF(dinfo, "0x%lx <%s>", addr, symstr);
	}
}

/*
 * Resolve an address into a "<symbol>+<offset>" string.
 */
static void disas_print_address(bfd_vma addr, struct disassemble_info *dinfo)
{
	struct disas_context *dctx = dinfo->application_data;
	struct instruction *insn = dctx->insn;
	struct instruction *jump_dest;
	struct symbol *sym;
	bool is_reloc;

	/*
	 * If the instruction is a call/jump and it references a
	 * destination then this is likely the address we are looking
	 * up. So check it first.
	 */
	jump_dest = insn->jump_dest;
	if (jump_dest && jump_dest->sym && jump_dest->offset == addr) {
		disas_print_addr_sym(jump_dest->sec, jump_dest->sym,
				     addr, dinfo);
		return;
	}

	/*
	 * If the address points to the next instruction then there is
	 * probably a relocation. It can be a false positive when the
	 * current instruction is referencing the address of the next
	 * instruction. This particular case will be handled in
	 * disas_print_addr_reloc().
	 */
	is_reloc = (addr == insn->offset + insn->len);

	/*
	 * The call destination offset can be the address we are looking
	 * up, or 0 if there is a relocation.
	 */
	sym = insn_call_dest(insn);
	if (sym && (sym->offset == addr || (sym->offset == 0 && is_reloc))) {
		DINFO_FPRINTF(dinfo, "0x%lx <%s>", addr, sym->name);
		return;
	}

	if (!is_reloc)
		disas_print_addr_noreloc(addr, dinfo);
	else
		disas_print_addr_reloc(addr, dinfo);
}

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

	init_disassemble_info_compat(dinfo, dctx,
				     disas_fprintf, disas_fprintf_styled);

	dinfo->read_memory_func = buffer_read_memory;
	dinfo->print_address_func = disas_print_address;
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

char *disas_result(struct disas_context *dctx)
{
	return dctx->result;
}

#define DISAS_INSN_OFFSET_SPACE		10
#define DISAS_INSN_SPACE		60

/*
 * Print a message in the instruction flow. If insn is not NULL then
 * the instruction address is printed in addition of the message,
 * otherwise only the message is printed. In all cases, the instruction
 * itself is not printed.
 */
static int disas_vprint(FILE *stream, struct section *sec, unsigned long offset,
			int depth, const char *format, va_list ap)
{
	const char *addr_str;
	int i, n;
	int len;

	len = sym_name_max_len + DISAS_INSN_OFFSET_SPACE;
	if (depth < 0) {
		len += depth;
		depth = 0;
	}

	n = 0;

	if (sec) {
		addr_str = offstr(sec, offset);
		n += fprintf(stream, "%6lx:  %-*s  ", offset, len, addr_str);
		free((char *)addr_str);
	} else {
		len += DISAS_INSN_OFFSET_SPACE + 1;
		n += fprintf(stream, "%-*s", len, "");
	}

	/* print vertical bars to show the code flow */
	for (i = 0; i < depth; i++)
		n += fprintf(stream, "| ");

	if (format)
		n += vfprintf(stream, format, ap);

	return n;
}

/*
 * Print a message in the instruction flow. If insn is not NULL then
 * the instruction address is printed in addition of the message,
 * otherwise only the message is printed. In all cases, the instruction
 * itself is not printed.
 */
void disas_print_info(FILE *stream, struct instruction *insn, int depth,
		      const char *format, ...)
{
	struct section *sec;
	unsigned long off;
	va_list args;

	if (insn) {
		sec = insn->sec;
		off = insn->offset;
	} else {
		sec = NULL;
		off = 0;
	}

	va_start(args, format);
	disas_vprint(stream, sec, off, depth, format, args);
	va_end(args);
}

/*
 * Print an instruction address (offset and function), the instruction itself
 * and an optional message.
 */
void disas_print_insn(FILE *stream, struct disas_context *dctx,
		      struct instruction *insn, int depth,
		      const char *format, ...)
{
	char fake_nop_insn[32];
	const char *insn_str;
	bool fake_nop;
	va_list args;
	int len;

	/*
	 * Alternative can insert a fake nop, sometimes with no
	 * associated section so nothing to disassemble.
	 */
	fake_nop = (!insn->sec && insn->type == INSN_NOP);
	if (fake_nop) {
		snprintf(fake_nop_insn, 32, "<fake nop> (%d bytes)", insn->len);
		insn_str = fake_nop_insn;
	} else {
		disas_insn(dctx, insn);
		insn_str = disas_result(dctx);
	}

	/* print the instruction */
	len = (depth + 1) * 2 < DISAS_INSN_SPACE ? DISAS_INSN_SPACE - (depth+1) * 2 : 1;
	disas_print_info(stream, insn, depth, "%-*s", len, insn_str);

	/* print message if any */
	if (!format)
		return;

	if (strcmp(format, "\n") == 0) {
		fprintf(stream, "\n");
		return;
	}

	fprintf(stream, " - ");
	va_start(args, format);
	vfprintf(stream, format, args);
	va_end(args);
}

/*
 * Disassemble a single instruction. Return the size of the instruction.
 */
size_t disas_insn(struct disas_context *dctx, struct instruction *insn)
{
	disassembler_ftype disasm = dctx->disassembler;
	struct disassemble_info *dinfo = &dctx->info;

	dctx->insn = insn;
	dctx->result[0] = '\0';

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
 * Provide a name for the type of alternatives present at the
 * specified instruction.
 *
 * An instruction can have alternatives with different types, for
 * example alternative instructions and an exception table. In that
 * case the name for the alternative instructions type is used.
 *
 * Return NULL if the instruction as no alternative.
 */
const char *disas_alt_type_name(struct instruction *insn)
{
	struct alternative *alt;
	const char *name;

	name = NULL;
	for (alt = insn->alts; alt; alt = alt->next) {
		if (alt->type == ALT_TYPE_INSTRUCTIONS) {
			name = "alternative";
			break;
		}

		switch (alt->type) {
		case ALT_TYPE_EX_TABLE:
			name = "ex_table";
			break;
		case ALT_TYPE_JUMP_TABLE:
			name = "jump_table";
			break;
		default:
			name = "unknown";
			break;
		}
	}

	return name;
}

/*
 * Provide a name for an alternative.
 */
char *disas_alt_name(struct alternative *alt)
{
	char *str = NULL;

	switch (alt->type) {

	case ALT_TYPE_EX_TABLE:
		str = strdup("EXCEPTION");
		break;

	case ALT_TYPE_JUMP_TABLE:
		str = strdup("JUMP");
		break;

	case ALT_TYPE_INSTRUCTIONS:
		/*
		 * This is a non-default group alternative. Create a unique
		 * name using the offset of the first original and alternative
		 * instructions.
		 */
		asprintf(&str, "ALTERNATIVE %lx.%lx",
			 alt->insn->alt_group->orig_group->first_insn->offset,
			 alt->insn->alt_group->first_insn->offset);
		break;
	}

	return str;
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
		disas_insn(dctx, insn);
		printf(" %6lx:  %s+0x%-6lx      %s\n",
		       addr, func->name, addr - func->offset,
		       disas_result(dctx));
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
