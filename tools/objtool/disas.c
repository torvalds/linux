// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#define _GNU_SOURCE
#include <fnmatch.h>

#include <objtool/arch.h>
#include <objtool/check.h>
#include <objtool/disas.h>
#include <objtool/special.h>
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
	bool alt_applied;
	char result[DISAS_RESULT_SIZE];
	disassembler_ftype disassembler;
	struct disassemble_info info;
};

/*
 * Maximum number of alternatives
 */
#define DISAS_ALT_MAX		5

/*
 * Maximum number of instructions per alternative
 */
#define DISAS_ALT_INSN_MAX	50

/*
 * Information to disassemble an alternative
 */
struct disas_alt {
	struct instruction *orig_insn;		/* original instruction */
	struct alternative *alt;		/* alternative or NULL if default code */
	char *name;				/* name for this alternative */
	int width;				/* formatting width */
	struct {
		char *str;			/* instruction string */
		int offset;			/* instruction offset */
		int nops;			/* number of nops */
	} insn[DISAS_ALT_INSN_MAX];		/* alternative instructions */
	int insn_idx;				/* index of the next instruction to print */
};

#define DALT_DEFAULT(dalt)	(!(dalt)->alt)
#define DALT_INSN(dalt)		(DALT_DEFAULT(dalt) ? (dalt)->orig_insn : (dalt)->alt->insn)
#define DALT_GROUP(dalt)	(DALT_INSN(dalt)->alt_group)
#define DALT_ALTID(dalt)	((dalt)->orig_insn->offset)

#define ALT_FLAGS_SHIFT		16
#define ALT_FLAG_NOT		(1 << 0)
#define ALT_FLAG_DIRECT_CALL	(1 << 1)
#define ALT_FEATURE_MASK	((1 << ALT_FLAGS_SHIFT) - 1)

static int alt_feature(unsigned int ft_flags)
{
	return (ft_flags & ALT_FEATURE_MASK);
}

static int alt_flags(unsigned int ft_flags)
{
	return (ft_flags >> ALT_FLAGS_SHIFT);
}

/*
 * Wrapper around asprintf() to allocate and format a string.
 * Return the allocated string or NULL on error.
 */
static char *strfmt(const char *fmt, ...)
{
	va_list ap;
	char *str;
	int rv;

	va_start(ap, fmt);
	rv = vasprintf(&str, fmt, ap);
	va_end(ap);

	return rv == -1 ? NULL : str;
}

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
#define bfd_vma_fmt			\
	__builtin_choose_expr(sizeof(bfd_vma) == sizeof(unsigned long), "%#lx <%s>", "%#llx <%s>")

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
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, symstr);
	} else {
		str = offstr(sec, addr);
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, str);
		free(str);
	}
}

static bool disas_print_addr_alt(bfd_vma addr, struct disassemble_info *dinfo)
{
	struct disas_context *dctx = dinfo->application_data;
	struct instruction *orig_first_insn;
	struct alt_group *alt_group;
	unsigned long offset;
	struct symbol *sym;

	/*
	 * Check if we are processing an alternative at the original
	 * instruction address (i.e. if alt_applied is true) and if
	 * we are referencing an address inside the alternative.
	 *
	 * For example, this happens if there is a branch inside an
	 * alternative. In that case, the address should be updated
	 * to a reference inside the original instruction flow.
	 */
	if (!dctx->alt_applied)
		return false;

	alt_group = dctx->insn->alt_group;
	if (!alt_group || !alt_group->orig_group ||
	    addr < alt_group->first_insn->offset ||
	    addr > alt_group->last_insn->offset)
		return false;

	orig_first_insn = alt_group->orig_group->first_insn;
	offset = addr - alt_group->first_insn->offset;

	addr = orig_first_insn->offset + offset;
	sym = orig_first_insn->sym;

	disas_print_addr_sym(orig_first_insn->sec, sym, addr, dinfo);

	return true;
}

static void disas_print_addr_noreloc(bfd_vma addr,
				     struct disassemble_info *dinfo)
{
	struct disas_context *dctx = dinfo->application_data;
	struct instruction *insn = dctx->insn;
	struct symbol *sym = NULL;

	if (disas_print_addr_alt(addr, dinfo))
		return;

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
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, "_THIS_IP_");
		return;
	}

	offset = arch_insn_adjusted_addend(insn, reloc);

	/*
	 * If the relocation symbol is a section name (for example ".bss")
	 * then we try to further resolve the name.
	 */
	if (reloc->sym->type == STT_SECTION) {
		str = offstr(reloc->sym->sec, reloc->sym->offset + offset);
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, str);
		free(str);
	} else {
		sprint_name(symstr, reloc->sym->name, offset);
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, symstr);
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
		if (!disas_print_addr_alt(addr, dinfo))
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
		DINFO_FPRINTF(dinfo, bfd_vma_fmt, addr, sym->name);
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

#define DISAS_PRINSN(dctx, insn, depth)			\
	disas_print_insn(stdout, dctx, insn, depth, "\n")

/*
 * Print a message in the instruction flow. If sec is not NULL then the
 * address at the section offset is printed in addition of the message,
 * otherwise only the message is printed.
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

static int disas_print(FILE *stream, struct section *sec, unsigned long offset,
			int depth, const char *format, ...)
{
	va_list args;
	int len;

	va_start(args, format);
	len = disas_vprint(stream, sec, offset, depth, format, args);
	va_end(args);

	return len;
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
 *
 * If alt_applied is true then insn should be an instruction from of an
 * alternative (i.e. insn->alt_group != NULL), and it is disassembled
 * at the location of the original code it is replacing. When the
 * instruction references any address inside the alternative then
 * these references will be re-adjusted to replace the original code.
 */
static size_t disas_insn_common(struct disas_context *dctx,
				struct instruction *insn,
				bool alt_applied)
{
	disassembler_ftype disasm = dctx->disassembler;
	struct disassemble_info *dinfo = &dctx->info;

	dctx->insn = insn;
	dctx->alt_applied = alt_applied;
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

size_t disas_insn(struct disas_context *dctx, struct instruction *insn)
{
	return disas_insn_common(dctx, insn, false);
}

static size_t disas_insn_alt(struct disas_context *dctx,
			     struct instruction *insn)
{
	return disas_insn_common(dctx, insn, true);
}

static struct instruction *next_insn_same_alt(struct objtool_file *file,
					      struct alt_group *alt_grp,
					      struct instruction *insn)
{
	if (alt_grp->last_insn == insn || alt_grp->nop == insn)
		return NULL;

	return next_insn_same_sec(file, insn);
}

#define alt_for_each_insn(file, alt_grp, insn)			\
	for (insn = alt_grp->first_insn; 			\
	     insn;						\
	     insn = next_insn_same_alt(file, alt_grp, insn))

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
	char pfx[4] = { 0 };
	char *str = NULL;
	const char *name;
	int feature;
	int flags;
	int num;

	switch (alt->type) {

	case ALT_TYPE_EX_TABLE:
		str = strdup("EXCEPTION");
		break;

	case ALT_TYPE_JUMP_TABLE:
		str = strdup("JUMP");
		break;

	case ALT_TYPE_INSTRUCTIONS:
		/*
		 * This is a non-default group alternative. Create a name
		 * based on the feature and flags associated with this
		 * alternative. Use either the feature name (it is available)
		 * or the feature number. And add a prefix to show the flags
		 * used.
		 *
		 * Prefix flags characters:
		 *
		 *   '!'  alternative used when feature not enabled
		 *   '+'  direct call alternative
		 *   '?'  unknown flag
		 */

		if (!alt->insn->alt_group)
			return NULL;

		feature = alt->insn->alt_group->feature;
		num = alt_feature(feature);
		flags = alt_flags(feature);
		str = pfx;

		if (flags & ~(ALT_FLAG_NOT | ALT_FLAG_DIRECT_CALL))
			*str++ = '?';
		if (flags & ALT_FLAG_DIRECT_CALL)
			*str++ = '+';
		if (flags & ALT_FLAG_NOT)
			*str++ = '!';

		name = arch_cpu_feature_name(num);
		if (!name)
			str = strfmt("%sFEATURE 0x%X", pfx, num);
		else
			str = strfmt("%s%s", pfx, name);

		break;
	}

	return str;
}

/*
 * Initialize an alternative. The default alternative should be initialized
 * with alt=NULL.
 */
static int disas_alt_init(struct disas_alt *dalt,
			  struct instruction *orig_insn,
			  struct alternative *alt)
{
	dalt->orig_insn = orig_insn;
	dalt->alt = alt;
	dalt->insn_idx = 0;
	dalt->name = alt ? disas_alt_name(alt) : strdup("DEFAULT");
	if (!dalt->name)
		return -1;
	dalt->width = strlen(dalt->name);

	return 0;
}

static int disas_alt_add_insn(struct disas_alt *dalt, int index, char *insn_str,
			      int offset, int nops)
{
	int len;

	if (index >= DISAS_ALT_INSN_MAX) {
		WARN("Alternative %lx.%s has more instructions than supported",
		     DALT_ALTID(dalt), dalt->name);
		return -1;
	}

	len = strlen(insn_str);
	dalt->insn[index].str = insn_str;
	dalt->insn[index].offset = offset;
	dalt->insn[index].nops = nops;
	if (len > dalt->width)
		dalt->width = len;

	return 0;
}

static int disas_alt_jump(struct disas_alt *dalt)
{
	struct instruction *orig_insn;
	struct instruction *dest_insn;
	char suffix[2] = { 0 };
	char *str;
	int nops;

	orig_insn = dalt->orig_insn;
	dest_insn = dalt->alt->insn;

	if (orig_insn->type == INSN_NOP) {
		if (orig_insn->len == 5)
			suffix[0] = 'q';
		str = strfmt("jmp%-3s %lx <%s+0x%lx>", suffix,
			     dest_insn->offset, dest_insn->sym->name,
			     dest_insn->offset - dest_insn->sym->offset);
		nops = 0;
	} else {
		str = strfmt("nop%d", orig_insn->len);
		nops = orig_insn->len;
	}

	if (!str)
		return -1;

	disas_alt_add_insn(dalt, 0, str, 0, nops);

	return 1;
}

/*
 * Disassemble an exception table alternative.
 */
static int disas_alt_extable(struct disas_alt *dalt)
{
	struct instruction *alt_insn;
	char *str;

	alt_insn = dalt->alt->insn;
	str = strfmt("resume at 0x%lx <%s+0x%lx>",
		     alt_insn->offset, alt_insn->sym->name,
		     alt_insn->offset - alt_insn->sym->offset);
	if (!str)
		return -1;

	disas_alt_add_insn(dalt, 0, str, 0, 0);

	return 1;
}

/*
 * Disassemble an alternative and store instructions in the disas_alt
 * structure. Return the number of instructions in the alternative.
 */
static int disas_alt_group(struct disas_context *dctx, struct disas_alt *dalt)
{
	struct objtool_file *file;
	struct instruction *insn;
	int offset;
	char *str;
	int count;
	int nops;
	int err;

	file = dctx->file;
	count = 0;
	offset = 0;
	nops = 0;

	alt_for_each_insn(file, DALT_GROUP(dalt), insn) {

		disas_insn_alt(dctx, insn);
		str = strdup(disas_result(dctx));
		if (!str)
			return -1;

		nops = insn->type == INSN_NOP ? insn->len : 0;
		err = disas_alt_add_insn(dalt, count, str, offset, nops);
		if (err)
			break;
		offset += insn->len;
		count++;
	}

	return count;
}

/*
 * Disassemble the default alternative.
 */
static int disas_alt_default(struct disas_context *dctx, struct disas_alt *dalt)
{
	char *str;
	int nops;
	int err;

	if (DALT_GROUP(dalt))
		return disas_alt_group(dctx, dalt);

	/*
	 * Default alternative with no alt_group: this is the default
	 * code associated with either a jump table or an exception
	 * table and no other instruction alternatives. In that case
	 * the default alternative is made of a single instruction.
	 */
	disas_insn(dctx, dalt->orig_insn);
	str = strdup(disas_result(dctx));
	if (!str)
		return -1;
	nops = dalt->orig_insn->type == INSN_NOP ? dalt->orig_insn->len : 0;
	err = disas_alt_add_insn(dalt, 0, str, 0, nops);
	if (err)
		return -1;

	return 1;
}

/*
 * For each alternative, if there is an instruction at the specified
 * offset then print this instruction, otherwise print a blank entry.
 * The offset is an offset from the start of the alternative.
 *
 * Return the offset for the next instructions to print, or -1 if all
 * instructions have been printed.
 */
static int disas_alt_print_insn(struct disas_alt *dalts, int alt_count,
				int insn_count, int offset)
{
	struct disas_alt *dalt;
	int offset_next;
	char *str;
	int i, j;

	offset_next = -1;

	for (i = 0; i < alt_count; i++) {
		dalt = &dalts[i];
		j = dalt->insn_idx;
		if (j == -1) {
			printf("| %-*s ", dalt->width, "");
			continue;
		}

		if (dalt->insn[j].offset == offset) {
			str = dalt->insn[j].str;
			printf("| %-*s ", dalt->width, str ?: "");
			if (++j < insn_count) {
				dalt->insn_idx = j;
			} else {
				dalt->insn_idx = -1;
				continue;
			}
		} else {
			printf("| %-*s ", dalt->width, "");
		}

		if (dalt->insn[j].offset > 0 &&
		    (offset_next == -1 ||
		     (dalt->insn[j].offset < offset_next)))
			offset_next = dalt->insn[j].offset;
	}
	printf("\n");

	return offset_next;
}

/*
 * Print all alternatives side-by-side.
 */
static void disas_alt_print_wide(char *alt_name, struct disas_alt *dalts, int alt_count,
				 int insn_count)
{
	struct instruction *orig_insn;
	int offset_next;
	int offset;
	int i;

	orig_insn = dalts[0].orig_insn;

	/*
	 * Print an header with the name of each alternative.
	 */
	disas_print_info(stdout, orig_insn, -2, NULL);

	if (strlen(alt_name) > dalts[0].width)
		dalts[0].width = strlen(alt_name);
	printf("| %-*s ", dalts[0].width, alt_name);

	for (i = 1; i < alt_count; i++)
		printf("| %-*s ", dalts[i].width, dalts[i].name);

	printf("\n");

	/*
	 * Print instructions for each alternative.
	 */
	offset_next = 0;
	do {
		offset = offset_next;
		disas_print(stdout, orig_insn->sec, orig_insn->offset + offset,
			    -2, NULL);
		offset_next = disas_alt_print_insn(dalts, alt_count, insn_count,
						   offset);
	} while (offset_next > offset);
}

/*
 * Print all alternatives one above the other.
 */
static void disas_alt_print_compact(char *alt_name, struct disas_alt *dalts,
				    int alt_count, int insn_count)
{
	struct instruction *orig_insn;
	int width;
	int i, j;
	int len;

	orig_insn = dalts[0].orig_insn;

	len = disas_print(stdout, orig_insn->sec, orig_insn->offset, 0, NULL);
	printf("%s\n", alt_name);

	/*
	 * If all alternatives have a single instruction then print each
	 * alternative on a single line. Otherwise, print alternatives
	 * one above the other with a clear separation.
	 */

	if (insn_count == 1) {
		width = 0;
		for (i = 0; i < alt_count; i++) {
			if (dalts[i].width > width)
				width = dalts[i].width;
		}

		for (i = 0; i < alt_count; i++) {
			printf("%*s= %-*s    (if %s)\n", len, "", width,
			       dalts[i].insn[0].str, dalts[i].name);
		}

		return;
	}

	for (i = 0; i < alt_count; i++) {
		printf("%*s= %s\n", len, "", dalts[i].name);
		for (j = 0; j < insn_count; j++) {
			if (!dalts[i].insn[j].str)
				break;
			disas_print(stdout, orig_insn->sec,
				    orig_insn->offset + dalts[i].insn[j].offset, 0,
				    "| %s\n", dalts[i].insn[j].str);
		}
		printf("%*s|\n", len, "");
	}
}

/*
 * Trim NOPs in alternatives. This replaces trailing NOPs in alternatives
 * with a single indication of the number of bytes covered with NOPs.
 *
 * Return the maximum numbers of instructions in all alternatives after
 * trailing NOPs have been trimmed.
 */
static int disas_alt_trim_nops(struct disas_alt *dalts, int alt_count,
			       int insn_count)
{
	struct disas_alt *dalt;
	int nops_count;
	const char *s;
	int offset;
	int count;
	int nops;
	int i, j;

	count = 0;
	for (i = 0; i < alt_count; i++) {
		offset = 0;
		nops = 0;
		nops_count = 0;
		dalt = &dalts[i];
		for (j = insn_count - 1; j >= 0; j--) {
			if (!dalt->insn[j].str || !dalt->insn[j].nops)
				break;
			offset = dalt->insn[j].offset;
			free(dalt->insn[j].str);
			dalt->insn[j].offset = 0;
			dalt->insn[j].str = NULL;
			nops += dalt->insn[j].nops;
			nops_count++;
		}

		/*
		 * All trailing NOPs have been removed. If there was a single
		 * NOP instruction then re-add it. If there was a block of
		 * NOPs then indicate the number of bytes than the block
		 * covers (nop*<number-of-bytes>).
		 */
		if (nops_count) {
			s = nops_count == 1 ? "" : "*";
			dalt->insn[j + 1].str = strfmt("nop%s%d", s, nops);
			dalt->insn[j + 1].offset = offset;
			dalt->insn[j + 1].nops = nops;
			j++;
		}

		if (j > count)
			count = j;
	}

	return count + 1;
}

/*
 * Disassemble an alternative.
 *
 * Return the last instruction in the default alternative so that
 * disassembly can continue with the next instruction. Return NULL
 * on error.
 */
static void *disas_alt(struct disas_context *dctx,
		       struct instruction *orig_insn)
{
	struct disas_alt dalts[DISAS_ALT_MAX] = { 0 };
	struct instruction *last_insn = NULL;
	struct alternative *alt;
	struct disas_alt *dalt;
	int insn_count = 0;
	int alt_count = 0;
	char *alt_name;
	int count;
	int i, j;
	int err;

	alt_name = strfmt("<%s.%lx>", disas_alt_type_name(orig_insn),
			  orig_insn->offset);
	if (!alt_name) {
		WARN("Failed to define name for alternative at instruction 0x%lx",
		     orig_insn->offset);
		goto done;
	}

	/*
	 * Initialize and disassemble the default alternative.
	 */
	err = disas_alt_init(&dalts[0], orig_insn, NULL);
	if (err) {
		WARN("%s: failed to initialize default alternative", alt_name);
		goto done;
	}

	insn_count = disas_alt_default(dctx, &dalts[0]);
	if (insn_count < 0) {
		WARN("%s: failed to disassemble default alternative", alt_name);
		goto done;
	}

	/*
	 * Initialize and disassemble all other alternatives.
	 */
	i = 1;
	for (alt = orig_insn->alts; alt; alt = alt->next) {
		if (i >= DISAS_ALT_MAX) {
			WARN("%s has more alternatives than supported", alt_name);
			break;
		}

		dalt = &dalts[i];
		err = disas_alt_init(dalt, orig_insn, alt);
		if (err) {
			WARN("%s: failed to disassemble alternative", alt_name);
			goto done;
		}

		count = -1;
		switch (dalt->alt->type) {
		case ALT_TYPE_INSTRUCTIONS:
			count = disas_alt_group(dctx, dalt);
			break;
		case ALT_TYPE_EX_TABLE:
			count = disas_alt_extable(dalt);
			break;
		case ALT_TYPE_JUMP_TABLE:
			count = disas_alt_jump(dalt);
			break;
		}
		if (count < 0) {
			WARN("%s: failed to disassemble alternative %s",
			     alt_name, dalt->name);
			goto done;
		}

		insn_count = count > insn_count ? count : insn_count;
		i++;
	}
	alt_count = i;

	/*
	 * Print default and non-default alternatives.
	 */

	insn_count = disas_alt_trim_nops(dalts, alt_count, insn_count);

	if (opts.wide)
		disas_alt_print_wide(alt_name, dalts, alt_count, insn_count);
	else
		disas_alt_print_compact(alt_name, dalts, alt_count, insn_count);

	last_insn = orig_insn->alt_group ? orig_insn->alt_group->last_insn :
		orig_insn;

done:
	for (i = 0; i < alt_count; i++) {
		free(dalts[i].name);
		for (j = 0; j < insn_count; j++)
			free(dalts[i].insn[j].str);
	}

	free(alt_name);

	return last_insn;
}

/*
 * Disassemble a function.
 */
static void disas_func(struct disas_context *dctx, struct symbol *func)
{
	struct instruction *insn_start;
	struct instruction *insn;

	printf("%s:\n", func->name);
	sym_for_each_insn(dctx->file, func, insn) {
		if (insn->alts) {
			insn_start = insn;
			insn = disas_alt(dctx, insn);
			if (insn)
				continue;
			/*
			 * There was an error with disassembling
			 * the alternative. Resume disassembling
			 * at the current instruction, this will
			 * disassemble the default alternative
			 * only and continue with the code after
			 * the alternative.
			 */
			insn = insn_start;
		}

		DISAS_PRINSN(dctx, insn, 0);
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

void disas_funcs(struct disas_context *dctx)
{
	bool disas_all = !strcmp(opts.disas, "*");
	struct section *sec;
	struct symbol *sym;

	for_each_sec(dctx->file->elf, sec) {

		if (!(sec->sh.sh_flags & SHF_EXECINSTR))
			continue;

		sec_for_each_sym(sec, sym) {
			/*
			 * If the function had a warning and the verbose
			 * option is used then the function was already
			 * disassemble.
			 */
			if (opts.verbose && sym->warned)
				continue;

			if (disas_all || fnmatch(opts.disas, sym->name, 0) == 0)
				disas_func(dctx, sym);
		}
	}
}
