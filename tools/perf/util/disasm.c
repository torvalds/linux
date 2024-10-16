// SPDX-License-Identifier: GPL-2.0-only
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <regex.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/string.h>
#include <subcmd/run-command.h>

#include "annotate.h"
#include "annotate-data.h"
#include "build-id.h"
#include "debug.h"
#include "disasm.h"
#include "disasm_bpf.h"
#include "dso.h"
#include "env.h"
#include "evsel.h"
#include "map.h"
#include "maps.h"
#include "namespaces.h"
#include "srcline.h"
#include "symbol.h"
#include "util.h"

static regex_t	 file_lineno;

/* These can be referred from the arch-dependent code */
static struct ins_ops call_ops;
static struct ins_ops dec_ops;
static struct ins_ops jump_ops;
static struct ins_ops mov_ops;
static struct ins_ops nop_ops;
static struct ins_ops lock_ops;
static struct ins_ops ret_ops;
static struct ins_ops load_store_ops;
static struct ins_ops arithmetic_ops;

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name);
static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name);

static void ins__sort(struct arch *arch);
static int disasm_line__parse(char *line, const char **namep, char **rawp);
static int disasm_line__parse_powerpc(struct disasm_line *dl);
static char *expand_tabs(char *line, char **storage, size_t *storage_len);

static __attribute__((constructor)) void symbol__init_regexpr(void)
{
	regcomp(&file_lineno, "^/[^:]+:([0-9]+)", REG_EXTENDED);
}

static int arch__grow_instructions(struct arch *arch)
{
	struct ins *new_instructions;
	size_t new_nr_allocated;

	if (arch->nr_instructions_allocated == 0 && arch->instructions)
		goto grow_from_non_allocated_table;

	new_nr_allocated = arch->nr_instructions_allocated + 128;
	new_instructions = realloc(arch->instructions, new_nr_allocated * sizeof(struct ins));
	if (new_instructions == NULL)
		return -1;

out_update_instructions:
	arch->instructions = new_instructions;
	arch->nr_instructions_allocated = new_nr_allocated;
	return 0;

grow_from_non_allocated_table:
	new_nr_allocated = arch->nr_instructions + 128;
	new_instructions = calloc(new_nr_allocated, sizeof(struct ins));
	if (new_instructions == NULL)
		return -1;

	memcpy(new_instructions, arch->instructions, arch->nr_instructions);
	goto out_update_instructions;
}

static int arch__associate_ins_ops(struct arch* arch, const char *name, struct ins_ops *ops)
{
	struct ins *ins;

	if (arch->nr_instructions == arch->nr_instructions_allocated &&
	    arch__grow_instructions(arch))
		return -1;

	ins = &arch->instructions[arch->nr_instructions];
	ins->name = strdup(name);
	if (!ins->name)
		return -1;

	ins->ops  = ops;
	arch->nr_instructions++;

	ins__sort(arch);
	return 0;
}

#include "arch/arc/annotate/instructions.c"
#include "arch/arm/annotate/instructions.c"
#include "arch/arm64/annotate/instructions.c"
#include "arch/csky/annotate/instructions.c"
#include "arch/loongarch/annotate/instructions.c"
#include "arch/mips/annotate/instructions.c"
#include "arch/x86/annotate/instructions.c"
#include "arch/powerpc/annotate/instructions.c"
#include "arch/riscv64/annotate/instructions.c"
#include "arch/s390/annotate/instructions.c"
#include "arch/sparc/annotate/instructions.c"

static struct arch architectures[] = {
	{
		.name = "arc",
		.init = arc__annotate_init,
	},
	{
		.name = "arm",
		.init = arm__annotate_init,
	},
	{
		.name = "arm64",
		.init = arm64__annotate_init,
	},
	{
		.name = "csky",
		.init = csky__annotate_init,
	},
	{
		.name = "mips",
		.init = mips__annotate_init,
		.objdump = {
			.comment_char = '#',
		},
	},
	{
		.name = "x86",
		.init = x86__annotate_init,
		.instructions = x86__instructions,
		.nr_instructions = ARRAY_SIZE(x86__instructions),
		.insn_suffix = "bwlq",
		.objdump =  {
			.comment_char = '#',
			.register_char = '%',
			.memory_ref_char = '(',
			.imm_char = '$',
		},
#ifdef HAVE_DWARF_SUPPORT
		.update_insn_state = update_insn_state_x86,
#endif
	},
	{
		.name = "powerpc",
		.init = powerpc__annotate_init,
#ifdef HAVE_DWARF_SUPPORT
		.update_insn_state = update_insn_state_powerpc,
#endif
	},
	{
		.name = "riscv64",
		.init = riscv64__annotate_init,
	},
	{
		.name = "s390",
		.init = s390__annotate_init,
		.objdump =  {
			.comment_char = '#',
		},
	},
	{
		.name = "sparc",
		.init = sparc__annotate_init,
		.objdump = {
			.comment_char = '#',
		},
	},
	{
		.name = "loongarch",
		.init = loongarch__annotate_init,
		.objdump = {
			.comment_char = '#',
		},
	},
};

static int arch__key_cmp(const void *name, const void *archp)
{
	const struct arch *arch = archp;

	return strcmp(name, arch->name);
}

static int arch__cmp(const void *a, const void *b)
{
	const struct arch *aa = a;
	const struct arch *ab = b;

	return strcmp(aa->name, ab->name);
}

static void arch__sort(void)
{
	const int nmemb = ARRAY_SIZE(architectures);

	qsort(architectures, nmemb, sizeof(struct arch), arch__cmp);
}

struct arch *arch__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(architectures);
	static bool sorted;

	if (!sorted) {
		arch__sort();
		sorted = true;
	}

	return bsearch(name, architectures, nmemb, sizeof(struct arch), arch__key_cmp);
}

bool arch__is(struct arch *arch, const char *name)
{
	return !strcmp(arch->name, name);
}

static void ins_ops__delete(struct ins_operands *ops)
{
	if (ops == NULL)
		return;
	zfree(&ops->source.raw);
	zfree(&ops->source.name);
	zfree(&ops->target.raw);
	zfree(&ops->target.name);
}

static int ins__raw_scnprintf(struct ins *ins, char *bf, size_t size,
			      struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name, ops->raw);
}

int ins__scnprintf(struct ins *ins, char *bf, size_t size,
		   struct ins_operands *ops, int max_ins_name)
{
	if (ins->ops->scnprintf)
		return ins->ops->scnprintf(ins, bf, size, ops, max_ins_name);

	return ins__raw_scnprintf(ins, bf, size, ops, max_ins_name);
}

bool ins__is_fused(struct arch *arch, const char *ins1, const char *ins2)
{
	if (!arch || !arch->ins_is_fused)
		return false;

	return arch->ins_is_fused(arch, ins1, ins2);
}

static int call__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms,
		struct disasm_line *dl __maybe_unused)
{
	char *endptr, *tok, *name;
	struct map *map = ms->map;
	struct addr_map_symbol target = {
		.ms = { .map = map, },
	};

	ops->target.addr = strtoull(ops->raw, &endptr, 16);

	name = strchr(endptr, '<');
	if (name == NULL)
		goto indirect_call;

	name++;

	if (arch->objdump.skip_functions_char &&
	    strchr(name, arch->objdump.skip_functions_char))
		return -1;

	tok = strchr(name, '>');
	if (tok == NULL)
		return -1;

	*tok = '\0';
	ops->target.name = strdup(name);
	*tok = '>';

	if (ops->target.name == NULL)
		return -1;
find_target:
	target.addr = map__objdump_2mem(map, ops->target.addr);

	if (maps__find_ams(ms->maps, &target) == 0 &&
	    map__rip_2objdump(target.ms.map, map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	return 0;

indirect_call:
	tok = strchr(endptr, '*');
	if (tok != NULL) {
		endptr++;

		/* Indirect call can use a non-rip register and offset: callq  *0x8(%rbx).
		 * Do not parse such instruction.  */
		if (strstr(endptr, "(%r") == NULL)
			ops->target.addr = strtoull(endptr, NULL, 16);
	}
	goto find_target;
}

static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name)
{
	if (ops->target.sym)
		return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name, ops->target.sym->name);

	if (ops->target.addr == 0)
		return ins__raw_scnprintf(ins, bf, size, ops, max_ins_name);

	if (ops->target.name)
		return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name, ops->target.name);

	return scnprintf(bf, size, "%-*s *%" PRIx64, max_ins_name, ins->name, ops->target.addr);
}

static struct ins_ops call_ops = {
	.parse	   = call__parse,
	.scnprintf = call__scnprintf,
};

bool ins__is_call(const struct ins *ins)
{
	return ins->ops == &call_ops || ins->ops == &s390_call_ops || ins->ops == &loongarch_call_ops;
}

/*
 * Prevents from matching commas in the comment section, e.g.:
 * ffff200008446e70:       b.cs    ffff2000084470f4 <generic_exec_single+0x314>  // b.hs, b.nlast
 *
 * and skip comma as part of function arguments, e.g.:
 * 1d8b4ac <linemap_lookup(line_maps const*, unsigned int)+0xcc>
 */
static inline const char *validate_comma(const char *c, struct ins_operands *ops)
{
	if (ops->jump.raw_comment && c > ops->jump.raw_comment)
		return NULL;

	if (ops->jump.raw_func_start && c > ops->jump.raw_func_start)
		return NULL;

	return c;
}

static int jump__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms,
		struct disasm_line *dl __maybe_unused)
{
	struct map *map = ms->map;
	struct symbol *sym = ms->sym;
	struct addr_map_symbol target = {
		.ms = { .map = map, },
	};
	const char *c = strchr(ops->raw, ',');
	u64 start, end;

	ops->jump.raw_comment = strchr(ops->raw, arch->objdump.comment_char);
	ops->jump.raw_func_start = strchr(ops->raw, '<');

	c = validate_comma(c, ops);

	/*
	 * Examples of lines to parse for the _cpp_lex_token@@Base
	 * function:
	 *
	 * 1159e6c: jne    115aa32 <_cpp_lex_token@@Base+0xf92>
	 * 1159e8b: jne    c469be <cpp_named_operator2name@@Base+0xa72>
	 *
	 * The first is a jump to an offset inside the same function,
	 * the second is to another function, i.e. that 0xa72 is an
	 * offset in the cpp_named_operator2name@@base function.
	 */
	/*
	 * skip over possible up to 2 operands to get to address, e.g.:
	 * tbnz	 w0, #26, ffff0000083cd190 <security_file_permission+0xd0>
	 */
	if (c++ != NULL) {
		ops->target.addr = strtoull(c, NULL, 16);
		if (!ops->target.addr) {
			c = strchr(c, ',');
			c = validate_comma(c, ops);
			if (c++ != NULL)
				ops->target.addr = strtoull(c, NULL, 16);
		}
	} else {
		ops->target.addr = strtoull(ops->raw, NULL, 16);
	}

	target.addr = map__objdump_2mem(map, ops->target.addr);
	start = map__unmap_ip(map, sym->start);
	end = map__unmap_ip(map, sym->end);

	ops->target.outside = target.addr < start || target.addr > end;

	/*
	 * FIXME: things like this in _cpp_lex_token (gcc's cc1 program):

		cpp_named_operator2name@@Base+0xa72

	 * Point to a place that is after the cpp_named_operator2name
	 * boundaries, i.e.  in the ELF symbol table for cc1
	 * cpp_named_operator2name is marked as being 32-bytes long, but it in
	 * fact is much larger than that, so we seem to need a symbols__find()
	 * routine that looks for >= current->start and  < next_symbol->start,
	 * possibly just for C++ objects?
	 *
	 * For now lets just make some progress by marking jumps to outside the
	 * current function as call like.
	 *
	 * Actual navigation will come next, with further understanding of how
	 * the symbol searching and disassembly should be done.
	 */
	if (maps__find_ams(ms->maps, &target) == 0 &&
	    map__rip_2objdump(target.ms.map, map__map_ip(target.ms.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.ms.sym;

	if (!ops->target.outside) {
		ops->target.offset = target.addr - start;
		ops->target.offset_avail = true;
	} else {
		ops->target.offset_avail = false;
	}

	return 0;
}

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name)
{
	const char *c;

	if (!ops->target.addr || ops->target.offset < 0)
		return ins__raw_scnprintf(ins, bf, size, ops, max_ins_name);

	if (ops->target.outside && ops->target.sym != NULL)
		return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name, ops->target.sym->name);

	c = strchr(ops->raw, ',');
	c = validate_comma(c, ops);

	if (c != NULL) {
		const char *c2 = strchr(c + 1, ',');

		c2 = validate_comma(c2, ops);
		/* check for 3-op insn */
		if (c2 != NULL)
			c = c2;
		c++;

		/* mirror arch objdump's space-after-comma style */
		if (*c == ' ')
			c++;
	}

	return scnprintf(bf, size, "%-*s %.*s%" PRIx64, max_ins_name,
			 ins->name, c ? c - ops->raw : 0, ops->raw,
			 ops->target.offset);
}

static void jump__delete(struct ins_operands *ops __maybe_unused)
{
	/*
	 * The ops->jump.raw_comment and ops->jump.raw_func_start belong to the
	 * raw string, don't free them.
	 */
}

static struct ins_ops jump_ops = {
	.free	   = jump__delete,
	.parse	   = jump__parse,
	.scnprintf = jump__scnprintf,
};

bool ins__is_jump(const struct ins *ins)
{
	return ins->ops == &jump_ops || ins->ops == &loongarch_jump_ops;
}

static int comment__symbol(char *raw, char *comment, u64 *addrp, char **namep)
{
	char *endptr, *name, *t;

	if (strstr(raw, "(%rip)") == NULL)
		return 0;

	*addrp = strtoull(comment, &endptr, 16);
	if (endptr == comment)
		return 0;
	name = strchr(endptr, '<');
	if (name == NULL)
		return -1;

	name++;

	t = strchr(name, '>');
	if (t == NULL)
		return 0;

	*t = '\0';
	*namep = strdup(name);
	*t = '>';

	return 0;
}

static int lock__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms,
		struct disasm_line *dl __maybe_unused)
{
	ops->locked.ops = zalloc(sizeof(*ops->locked.ops));
	if (ops->locked.ops == NULL)
		return 0;

	if (disasm_line__parse(ops->raw, &ops->locked.ins.name, &ops->locked.ops->raw) < 0)
		goto out_free_ops;

	ops->locked.ins.ops = ins__find(arch, ops->locked.ins.name, 0);

	if (ops->locked.ins.ops == NULL)
		goto out_free_ops;

	if (ops->locked.ins.ops->parse &&
	    ops->locked.ins.ops->parse(arch, ops->locked.ops, ms, NULL) < 0)
		goto out_free_ops;

	return 0;

out_free_ops:
	zfree(&ops->locked.ops);
	return 0;
}

static int lock__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name)
{
	int printed;

	if (ops->locked.ins.ops == NULL)
		return ins__raw_scnprintf(ins, bf, size, ops, max_ins_name);

	printed = scnprintf(bf, size, "%-*s ", max_ins_name, ins->name);
	return printed + ins__scnprintf(&ops->locked.ins, bf + printed,
					size - printed, ops->locked.ops, max_ins_name);
}

static void lock__delete(struct ins_operands *ops)
{
	struct ins *ins = &ops->locked.ins;

	if (ins->ops && ins->ops->free)
		ins->ops->free(ops->locked.ops);
	else
		ins_ops__delete(ops->locked.ops);

	zfree(&ops->locked.ops);
	zfree(&ops->locked.ins.name);
	zfree(&ops->target.raw);
	zfree(&ops->target.name);
}

static struct ins_ops lock_ops = {
	.free	   = lock__delete,
	.parse	   = lock__parse,
	.scnprintf = lock__scnprintf,
};

/*
 * Check if the operand has more than one registers like x86 SIB addressing:
 *   0x1234(%rax, %rbx, 8)
 *
 * But it doesn't care segment selectors like %gs:0x5678(%rcx), so just check
 * the input string after 'memory_ref_char' if exists.
 */
static bool check_multi_regs(struct arch *arch, const char *op)
{
	int count = 0;

	if (arch->objdump.register_char == 0)
		return false;

	if (arch->objdump.memory_ref_char) {
		op = strchr(op, arch->objdump.memory_ref_char);
		if (op == NULL)
			return false;
	}

	while ((op = strchr(op, arch->objdump.register_char)) != NULL) {
		count++;
		op++;
	}

	return count > 1;
}

static int mov__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms __maybe_unused,
		struct disasm_line *dl __maybe_unused)
{
	char *s = strchr(ops->raw, ','), *target, *comment, prev;

	if (s == NULL)
		return -1;

	*s = '\0';

	/*
	 * x86 SIB addressing has something like 0x8(%rax, %rcx, 1)
	 * then it needs to have the closing parenthesis.
	 */
	if (strchr(ops->raw, '(')) {
		*s = ',';
		s = strchr(ops->raw, ')');
		if (s == NULL || s[1] != ',')
			return -1;
		*++s = '\0';
	}

	ops->source.raw = strdup(ops->raw);
	*s = ',';

	if (ops->source.raw == NULL)
		return -1;

	ops->source.multi_regs = check_multi_regs(arch, ops->source.raw);

	target = skip_spaces(++s);
	comment = strchr(s, arch->objdump.comment_char);

	if (comment != NULL)
		s = comment - 1;
	else
		s = strchr(s, '\0') - 1;

	while (s > target && isspace(s[0]))
		--s;
	s++;
	prev = *s;
	*s = '\0';

	ops->target.raw = strdup(target);
	*s = prev;

	if (ops->target.raw == NULL)
		goto out_free_source;

	ops->target.multi_regs = check_multi_regs(arch, ops->target.raw);

	if (comment == NULL)
		return 0;

	comment = skip_spaces(comment);
	comment__symbol(ops->source.raw, comment + 1, &ops->source.addr, &ops->source.name);
	comment__symbol(ops->target.raw, comment + 1, &ops->target.addr, &ops->target.name);

	return 0;

out_free_source:
	zfree(&ops->source.raw);
	return -1;
}

static int mov__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s,%s", max_ins_name, ins->name,
			 ops->source.name ?: ops->source.raw,
			 ops->target.name ?: ops->target.raw);
}

static struct ins_ops mov_ops = {
	.parse	   = mov__parse,
	.scnprintf = mov__scnprintf,
};

#define PPC_22_30(R)    (((R) >> 1) & 0x1ff)
#define MINUS_EXT_XO_FORM	234
#define SUB_EXT_XO_FORM		232
#define	ADD_ZERO_EXT_XO_FORM	202
#define	SUB_ZERO_EXT_XO_FORM	200

static int arithmetic__scnprintf(struct ins *ins, char *bf, size_t size,
		struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name,
			ops->raw);
}

/*
 * Sets the fields: multi_regs and "mem_ref".
 * "mem_ref" is set for ops->source which is later used to
 * fill the objdump->memory_ref-char field. This ops is currently
 * used by powerpc and since binary instruction code is used to
 * extract opcode, regs and offset, no other parsing is needed here.
 *
 * Dont set multi regs for 4 cases since it has only one operand
 * for source:
 * - Add to Minus One Extended XO-form ( Ex: addme, addmeo )
 * - Subtract From Minus One Extended XO-form ( Ex: subfme )
 * - Add to Zero Extended XO-form ( Ex: addze, addzeo )
 * - Subtract From Zero Extended XO-form ( Ex: subfze )
 */
static int arithmetic__parse(struct arch *arch __maybe_unused, struct ins_operands *ops,
		struct map_symbol *ms __maybe_unused, struct disasm_line *dl)
{
	int opcode = PPC_OP(dl->raw.raw_insn);

	ops->source.mem_ref = false;
	if (opcode == 31) {
		if ((opcode != MINUS_EXT_XO_FORM) && (opcode != SUB_EXT_XO_FORM) \
				&& (opcode != ADD_ZERO_EXT_XO_FORM) && (opcode != SUB_ZERO_EXT_XO_FORM))
			ops->source.multi_regs = true;
	}

	ops->target.mem_ref = false;
	ops->target.multi_regs = false;

	return 0;
}

static struct ins_ops arithmetic_ops = {
	.parse     = arithmetic__parse,
	.scnprintf = arithmetic__scnprintf,
};

static int load_store__scnprintf(struct ins *ins, char *bf, size_t size,
		struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name,
			ops->raw);
}

/*
 * Sets the fields: multi_regs and "mem_ref".
 * "mem_ref" is set for ops->source which is later used to
 * fill the objdump->memory_ref-char field. This ops is currently
 * used by powerpc and since binary instruction code is used to
 * extract opcode, regs and offset, no other parsing is needed here
 */
static int load_store__parse(struct arch *arch __maybe_unused, struct ins_operands *ops,
		struct map_symbol *ms __maybe_unused, struct disasm_line *dl __maybe_unused)
{
	ops->source.mem_ref = true;
	ops->source.multi_regs = false;
	/* opcode 31 is of X form */
	if (PPC_OP(dl->raw.raw_insn) == 31)
		ops->source.multi_regs = true;

	ops->target.mem_ref = false;
	ops->target.multi_regs = false;

	return 0;
}

static struct ins_ops load_store_ops = {
	.parse     = load_store__parse,
	.scnprintf = load_store__scnprintf,
};

static int dec__parse(struct arch *arch __maybe_unused, struct ins_operands *ops, struct map_symbol *ms __maybe_unused,
		struct disasm_line *dl __maybe_unused)
{
	char *target, *comment, *s, prev;

	target = s = ops->raw;

	while (s[0] != '\0' && !isspace(s[0]))
		++s;
	prev = *s;
	*s = '\0';

	ops->target.raw = strdup(target);
	*s = prev;

	if (ops->target.raw == NULL)
		return -1;

	comment = strchr(s, arch->objdump.comment_char);
	if (comment == NULL)
		return 0;

	comment = skip_spaces(comment);
	comment__symbol(ops->target.raw, comment + 1, &ops->target.addr, &ops->target.name);

	return 0;
}

static int dec__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s %s", max_ins_name, ins->name,
			 ops->target.name ?: ops->target.raw);
}

static struct ins_ops dec_ops = {
	.parse	   = dec__parse,
	.scnprintf = dec__scnprintf,
};

static int nop__scnprintf(struct ins *ins __maybe_unused, char *bf, size_t size,
			  struct ins_operands *ops __maybe_unused, int max_ins_name)
{
	return scnprintf(bf, size, "%-*s", max_ins_name, "nop");
}

static struct ins_ops nop_ops = {
	.scnprintf = nop__scnprintf,
};

static struct ins_ops ret_ops = {
	.scnprintf = ins__raw_scnprintf,
};

bool ins__is_nop(const struct ins *ins)
{
	return ins->ops == &nop_ops;
}

bool ins__is_ret(const struct ins *ins)
{
	return ins->ops == &ret_ops;
}

bool ins__is_lock(const struct ins *ins)
{
	return ins->ops == &lock_ops;
}

static int ins__key_cmp(const void *name, const void *insp)
{
	const struct ins *ins = insp;

	return strcmp(name, ins->name);
}

static int ins__cmp(const void *a, const void *b)
{
	const struct ins *ia = a;
	const struct ins *ib = b;

	return strcmp(ia->name, ib->name);
}

static void ins__sort(struct arch *arch)
{
	const int nmemb = arch->nr_instructions;

	qsort(arch->instructions, nmemb, sizeof(struct ins), ins__cmp);
}

static struct ins_ops *__ins__find(struct arch *arch, const char *name, struct disasm_line *dl)
{
	struct ins *ins;
	const int nmemb = arch->nr_instructions;

	if (arch__is(arch, "powerpc")) {
		/*
		 * For powerpc, identify the instruction ops
		 * from the opcode using raw_insn.
		 */
		struct ins_ops *ops;

		ops = check_ppc_insn(dl);
		if (ops)
			return ops;
	}

	if (!arch->sorted_instructions) {
		ins__sort(arch);
		arch->sorted_instructions = true;
	}

	ins = bsearch(name, arch->instructions, nmemb, sizeof(struct ins), ins__key_cmp);
	if (ins)
		return ins->ops;

	if (arch->insn_suffix) {
		char tmp[32];
		char suffix;
		size_t len = strlen(name);

		if (len == 0 || len >= sizeof(tmp))
			return NULL;

		suffix = name[len - 1];
		if (strchr(arch->insn_suffix, suffix) == NULL)
			return NULL;

		strcpy(tmp, name);
		tmp[len - 1] = '\0'; /* remove the suffix and check again */

		ins = bsearch(tmp, arch->instructions, nmemb, sizeof(struct ins), ins__key_cmp);
	}
	return ins ? ins->ops : NULL;
}

struct ins_ops *ins__find(struct arch *arch, const char *name, struct disasm_line *dl)
{
	struct ins_ops *ops = __ins__find(arch, name, dl);

	if (!ops && arch->associate_instruction_ops)
		ops = arch->associate_instruction_ops(arch, name);

	return ops;
}

static void disasm_line__init_ins(struct disasm_line *dl, struct arch *arch, struct map_symbol *ms)
{
	dl->ins.ops = ins__find(arch, dl->ins.name, dl);

	if (!dl->ins.ops)
		return;

	if (dl->ins.ops->parse && dl->ins.ops->parse(arch, &dl->ops, ms, dl) < 0)
		dl->ins.ops = NULL;
}

static int disasm_line__parse(char *line, const char **namep, char **rawp)
{
	char tmp, *name = skip_spaces(line);

	if (name[0] == '\0')
		return -1;

	*rawp = name + 1;

	while ((*rawp)[0] != '\0' && !isspace((*rawp)[0]))
		++*rawp;

	tmp = (*rawp)[0];
	(*rawp)[0] = '\0';
	*namep = strdup(name);

	if (*namep == NULL)
		goto out;

	(*rawp)[0] = tmp;
	*rawp = strim(*rawp);

	return 0;

out:
	return -1;
}

/*
 * Parses the result captured from symbol__disassemble_*
 * Example, line read from DSO file in powerpc:
 * line:    38 01 81 e8
 * opcode: fetched from arch specific get_opcode_insn
 * rawp_insn: e8810138
 *
 * rawp_insn is used later to extract the reg/offset fields
 */
#define	PPC_OP(op)	(((op) >> 26) & 0x3F)
#define	RAW_BYTES	11

static int disasm_line__parse_powerpc(struct disasm_line *dl)
{
	char *line = dl->al.line;
	const char **namep = &dl->ins.name;
	char **rawp = &dl->ops.raw;
	char *tmp_raw_insn, *name_raw_insn = skip_spaces(line);
	char *name = skip_spaces(name_raw_insn + RAW_BYTES);
	int objdump = 0;

	if (strlen(line) > RAW_BYTES)
		objdump = 1;

	if (name_raw_insn[0] == '\0')
		return -1;

	if (objdump) {
		disasm_line__parse(name, namep, rawp);
	} else
		*namep = "";

	tmp_raw_insn = strndup(name_raw_insn, 11);
	if (tmp_raw_insn == NULL)
		return -1;

	remove_spaces(tmp_raw_insn);

	sscanf(tmp_raw_insn, "%x", &dl->raw.raw_insn);
	if (objdump)
		dl->raw.raw_insn = be32_to_cpu(dl->raw.raw_insn);

	return 0;
}

static void annotation_line__init(struct annotation_line *al,
				  struct annotate_args *args,
				  int nr)
{
	al->offset = args->offset;
	al->line = strdup(args->line);
	al->line_nr = args->line_nr;
	al->fileloc = args->fileloc;
	al->data_nr = nr;
}

static void annotation_line__exit(struct annotation_line *al)
{
	zfree_srcline(&al->path);
	zfree(&al->line);
	zfree(&al->cycles);
	zfree(&al->br_cntr);
}

static size_t disasm_line_size(int nr)
{
	struct annotation_line *al;

	return (sizeof(struct disasm_line) + (sizeof(al->data[0]) * nr));
}

/*
 * Allocating the disasm annotation line data with
 * following structure:
 *
 *    -------------------------------------------
 *    struct disasm_line | struct annotation_line
 *    -------------------------------------------
 *
 * We have 'struct annotation_line' member as last member
 * of 'struct disasm_line' to have an easy access.
 */
struct disasm_line *disasm_line__new(struct annotate_args *args)
{
	struct disasm_line *dl = NULL;
	struct annotation *notes = symbol__annotation(args->ms.sym);
	int nr = notes->src->nr_events;

	dl = zalloc(disasm_line_size(nr));
	if (!dl)
		return NULL;

	annotation_line__init(&dl->al, args, nr);
	if (dl->al.line == NULL)
		goto out_delete;

	if (args->offset != -1) {
		if (arch__is(args->arch, "powerpc")) {
			if (disasm_line__parse_powerpc(dl) < 0)
				goto out_free_line;
		} else if (disasm_line__parse(dl->al.line, &dl->ins.name, &dl->ops.raw) < 0)
			goto out_free_line;

		disasm_line__init_ins(dl, args->arch, &args->ms);
	}

	return dl;

out_free_line:
	zfree(&dl->al.line);
out_delete:
	free(dl);
	return NULL;
}

void disasm_line__free(struct disasm_line *dl)
{
	if (dl->ins.ops && dl->ins.ops->free)
		dl->ins.ops->free(&dl->ops);
	else
		ins_ops__delete(&dl->ops);
	zfree(&dl->ins.name);
	annotation_line__exit(&dl->al);
	free(dl);
}

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw, int max_ins_name)
{
	if (raw || !dl->ins.ops)
		return scnprintf(bf, size, "%-*s %s", max_ins_name, dl->ins.name, dl->ops.raw);

	return ins__scnprintf(&dl->ins, bf, size, &dl->ops, max_ins_name);
}

/*
 * symbol__parse_objdump_line() parses objdump output (with -d --no-show-raw)
 * which looks like following
 *
 *  0000000000415500 <_init>:
 *    415500:       sub    $0x8,%rsp
 *    415504:       mov    0x2f5ad5(%rip),%rax        # 70afe0 <_DYNAMIC+0x2f8>
 *    41550b:       test   %rax,%rax
 *    41550e:       je     415515 <_init+0x15>
 *    415510:       callq  416e70 <__gmon_start__@plt>
 *    415515:       add    $0x8,%rsp
 *    415519:       retq
 *
 * it will be parsed and saved into struct disasm_line as
 *  <offset>       <name>  <ops.raw>
 *
 * The offset will be a relative offset from the start of the symbol and -1
 * means that it's not a disassembly line so should be treated differently.
 * The ops.raw part will be parsed further according to type of the instruction.
 */
static int symbol__parse_objdump_line(struct symbol *sym,
				      struct annotate_args *args,
				      char *parsed_line, int *line_nr, char **fileloc)
{
	struct map *map = args->ms.map;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *dl;
	char *tmp;
	s64 line_ip, offset = -1;
	regmatch_t match[2];

	/* /filename:linenr ? Save line number and ignore. */
	if (regexec(&file_lineno, parsed_line, 2, match, 0) == 0) {
		*line_nr = atoi(parsed_line + match[1].rm_so);
		free(*fileloc);
		*fileloc = strdup(parsed_line);
		return 0;
	}

	/* Process hex address followed by ':'. */
	line_ip = strtoull(parsed_line, &tmp, 16);
	if (parsed_line != tmp && tmp[0] == ':' && tmp[1] != '\0') {
		u64 start = map__rip_2objdump(map, sym->start),
		    end = map__rip_2objdump(map, sym->end);

		offset = line_ip - start;
		if ((u64)line_ip < start || (u64)line_ip >= end)
			offset = -1;
		else
			parsed_line = tmp + 1;
	}

	args->offset  = offset;
	args->line    = parsed_line;
	args->line_nr = *line_nr;
	args->fileloc = *fileloc;
	args->ms.sym  = sym;

	dl = disasm_line__new(args);
	(*line_nr)++;

	if (dl == NULL)
		return -1;

	if (!disasm_line__has_local_offset(dl)) {
		dl->ops.target.offset = dl->ops.target.addr -
					map__rip_2objdump(map, sym->start);
		dl->ops.target.offset_avail = true;
	}

	/* kcore has no symbols, so add the call target symbol */
	if (dl->ins.ops && ins__is_call(&dl->ins) && !dl->ops.target.sym) {
		struct addr_map_symbol target = {
			.addr = dl->ops.target.addr,
			.ms = { .map = map, },
		};

		if (!maps__find_ams(args->ms.maps, &target) &&
		    target.ms.sym->start == target.al_addr)
			dl->ops.target.sym = target.ms.sym;
	}

	annotation_line__add(&dl->al, &notes->src->source);
	return 0;
}

static void delete_last_nop(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	struct list_head *list = &notes->src->source;
	struct disasm_line *dl;

	while (!list_empty(list)) {
		dl = list_entry(list->prev, struct disasm_line, al.node);

		if (dl->ins.ops) {
			if (!ins__is_nop(&dl->ins))
				return;
		} else {
			if (!strstr(dl->al.line, " nop ") &&
			    !strstr(dl->al.line, " nopl ") &&
			    !strstr(dl->al.line, " nopw "))
				return;
		}

		list_del_init(&dl->al.node);
		disasm_line__free(dl);
	}
}

int symbol__strerror_disassemble(struct map_symbol *ms, int errnum, char *buf, size_t buflen)
{
	struct dso *dso = map__dso(ms->map);

	BUG_ON(buflen == 0);

	if (errnum >= 0) {
		str_error_r(errnum, buf, buflen);
		return 0;
	}

	switch (errnum) {
	case SYMBOL_ANNOTATE_ERRNO__NO_VMLINUX: {
		char bf[SBUILD_ID_SIZE + 15] = " with build id ";
		char *build_id_msg = NULL;

		if (dso__has_build_id(dso)) {
			build_id__sprintf(dso__bid(dso), bf + 15);
			build_id_msg = bf;
		}
		scnprintf(buf, buflen,
			  "No vmlinux file%s\nwas found in the path.\n\n"
			  "Note that annotation using /proc/kcore requires CAP_SYS_RAWIO capability.\n\n"
			  "Please use:\n\n"
			  "  perf buildid-cache -vu vmlinux\n\n"
			  "or:\n\n"
			  "  --vmlinux vmlinux\n", build_id_msg ?: "");
	}
		break;
	case SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF:
		scnprintf(buf, buflen, "Please link with binutils's libopcode to enable BPF annotation");
		break;
	case SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_REGEXP:
		scnprintf(buf, buflen, "Problems with arch specific instruction name regular expressions.");
		break;
	case SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_CPUID_PARSING:
		scnprintf(buf, buflen, "Problems while parsing the CPUID in the arch specific initialization.");
		break;
	case SYMBOL_ANNOTATE_ERRNO__BPF_INVALID_FILE:
		scnprintf(buf, buflen, "Invalid BPF file: %s.", dso__long_name(dso));
		break;
	case SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF:
		scnprintf(buf, buflen, "The %s BPF file has no BTF section, compile with -g or use pahole -J.",
			  dso__long_name(dso));
		break;
	default:
		scnprintf(buf, buflen, "Internal error: Invalid %d error code\n", errnum);
		break;
	}

	return 0;
}

static int dso__disassemble_filename(struct dso *dso, char *filename, size_t filename_size)
{
	char linkname[PATH_MAX];
	char *build_id_filename;
	char *build_id_path = NULL;
	char *pos;
	int len;

	if (dso__symtab_type(dso) == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(dso))
		return SYMBOL_ANNOTATE_ERRNO__NO_VMLINUX;

	build_id_filename = dso__build_id_filename(dso, NULL, 0, false);
	if (build_id_filename) {
		__symbol__join_symfs(filename, filename_size, build_id_filename);
		free(build_id_filename);
	} else {
		if (dso__has_build_id(dso))
			return ENOMEM;
		goto fallback;
	}

	build_id_path = strdup(filename);
	if (!build_id_path)
		return ENOMEM;

	/*
	 * old style build-id cache has name of XX/XXXXXXX.. while
	 * new style has XX/XXXXXXX../{elf,kallsyms,vdso}.
	 * extract the build-id part of dirname in the new style only.
	 */
	pos = strrchr(build_id_path, '/');
	if (pos && strlen(pos) < SBUILD_ID_SIZE - 2)
		dirname(build_id_path);

	if (dso__is_kcore(dso))
		goto fallback;

	len = readlink(build_id_path, linkname, sizeof(linkname) - 1);
	if (len < 0)
		goto fallback;

	linkname[len] = '\0';
	if (strstr(linkname, DSO__NAME_KALLSYMS) ||
		access(filename, R_OK)) {
fallback:
		/*
		 * If we don't have build-ids or the build-id file isn't in the
		 * cache, or is just a kallsyms file, well, lets hope that this
		 * DSO is the same as when 'perf record' ran.
		 */
		if (dso__kernel(dso) && dso__long_name(dso)[0] == '/')
			snprintf(filename, filename_size, "%s", dso__long_name(dso));
		else
			__symbol__join_symfs(filename, filename_size, dso__long_name(dso));

		mutex_lock(dso__lock(dso));
		if (access(filename, R_OK) && errno == ENOENT && dso__nsinfo(dso)) {
			char *new_name = dso__filename_with_chroot(dso, filename);
			if (new_name) {
				strlcpy(filename, new_name, filename_size);
				free(new_name);
			}
		}
		mutex_unlock(dso__lock(dso));
	} else if (dso__binary_type(dso) == DSO_BINARY_TYPE__NOT_FOUND) {
		dso__set_binary_type(dso, DSO_BINARY_TYPE__BUILD_ID_CACHE);
	}

	free(build_id_path);
	return 0;
}

#ifdef HAVE_LIBCAPSTONE_SUPPORT
#include <capstone/capstone.h>

int capstone_init(struct machine *machine, csh *cs_handle, bool is64, bool disassembler_style);

static int open_capstone_handle(struct annotate_args *args, bool is_64bit,
				csh *handle)
{
	struct annotation_options *opt = args->options;
	cs_mode mode = is_64bit ? CS_MODE_64 : CS_MODE_32;

	/* TODO: support more architectures */
	if (!arch__is(args->arch, "x86"))
		return -1;

	if (cs_open(CS_ARCH_X86, mode, handle) != CS_ERR_OK)
		return -1;

	if (!opt->disassembler_style ||
	    !strcmp(opt->disassembler_style, "att"))
		cs_option(*handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);

	/*
	 * Resolving address operands to symbols is implemented
	 * on x86 by investigating instruction details.
	 */
	cs_option(*handle, CS_OPT_DETAIL, CS_OPT_ON);

	return 0;
}
#endif

#if defined(HAVE_LIBCAPSTONE_SUPPORT) || defined(HAVE_LIBLLVM_SUPPORT)
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

#ifdef HAVE_LIBCAPSTONE_SUPPORT
static void print_capstone_detail(cs_insn *insn, char *buf, size_t len,
				  struct annotate_args *args, u64 addr)
{
	int i;
	struct map *map = args->ms.map;
	struct symbol *sym;

	/* TODO: support more architectures */
	if (!arch__is(args->arch, "x86"))
		return;

	if (insn->detail == NULL)
		return;

	for (i = 0; i < insn->detail->x86.op_count; i++) {
		cs_x86_op *op = &insn->detail->x86.operands[i];
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
			 * The kernel maps can be splitted into sections,
			 * let's find the map first and the search the symbol.
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

static int symbol__disassemble_capstone_powerpc(char *filename, struct symbol *sym,
					struct annotate_args *args)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms.map;
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

	if (capstone_init(maps__machine(args->ms.maps), &handle, is_64bit, disassembler_style) < 0)
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
	args->ms.sym = sym;

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
			goto err;

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
		cs_close(&handle);
	free(buf);
	return count < 0 ? count : 0;

err:
	if (fd >= 0)
		close(fd);
	if (needs_cs_close) {
		struct disasm_line *tmp;

		/*
		 * It probably failed in the middle of the above loop.
		 * Release any resources it might add.
		 */
		list_for_each_entry_safe(dl, tmp, &notes->src->source, al.node) {
			list_del(&dl->al.node);
			free(dl);
		}
	}
	count = -1;
	goto out;
}

static int symbol__disassemble_capstone(char *filename, struct symbol *sym,
					struct annotate_args *args)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms.map;
	u64 start = map__rip_2objdump(map, sym->start);
	u64 len;
	u64 offset;
	int i, count, free_count;
	bool is_64bit = false;
	bool needs_cs_close = false;
	u8 *buf = NULL;
	csh handle;
	cs_insn *insn = NULL;
	char disasm_buf[512];
	struct disasm_line *dl;

	if (args->options->objdump_path)
		return -1;

	buf = read_symbol(filename, map, sym, &len, &is_64bit);
	if (buf == NULL)
		return -1;

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

	if (open_capstone_handle(args, is_64bit, &handle) < 0)
		goto err;

	needs_cs_close = true;

	free_count = count = cs_disasm(handle, buf, len, start, len, &insn);
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
	if (needs_cs_close) {
		cs_close(&handle);
		if (free_count > 0)
			cs_free(insn, free_count);
	}
	free(buf);
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
			free(dl);
		}
	}
	count = -1;
	goto out;
}
#endif

static int symbol__disassemble_raw(char *filename, struct symbol *sym,
					struct annotate_args *args)
{
	struct annotation *notes = symbol__annotation(sym);
	struct map *map = args->ms.map;
	struct dso *dso = map__dso(map);
	u64 start = map__rip_2objdump(map, sym->start);
	u64 end = map__rip_2objdump(map, sym->end);
	u64 len = end - start;
	u64 offset;
	int i, count;
	u8 *buf = NULL;
	char disasm_buf[512];
	struct disasm_line *dl;
	u32 *line;

	/* Return if objdump is specified explicitly */
	if (args->options->objdump_path)
		return -1;

	pr_debug("Reading raw instruction from : %s using dso__data_read_offset\n", filename);

	buf = malloc(len);
	if (buf == NULL)
		goto err;

	count = dso__data_read_offset(dso, NULL, sym->start, buf, len);

	line = (u32 *)buf;

	if ((u64)count != len)
		goto err;

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

	/* Each raw instruction is 4 byte */
	count = len/4;

	for (i = 0, offset = 0; i < count; i++) {
		args->offset = offset;
		sprintf(args->line, "%x", line[i]);
		dl = disasm_line__new(args);
		if (dl == NULL)
			goto err;

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
	free(buf);
	return count < 0 ? count : 0;

err:
	count = -1;
	goto out;
}

#ifdef HAVE_LIBLLVM_SUPPORT
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#include "util/llvm-c-helpers.h"

struct symbol_lookup_storage {
	u64 branch_addr;
	u64 pcrel_load_addr;
};

/*
 * Whenever LLVM wants to resolve an address into a symbol, it calls this
 * callback. We don't ever actually _return_ anything (in particular, because
 * it puts quotation marks around what we return), but we use this as a hint
 * that there is a branch or PC-relative address in the expression that we
 * should add some textual annotation for after the instruction. The caller
 * will use this information to add the actual annotation.
 */
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

static int symbol__disassemble_llvm(char *filename, struct symbol *sym,
				    struct annotate_args *args)
{
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
}
#endif

/*
 * Possibly create a new version of line with tabs expanded. Returns the
 * existing or new line, storage is updated if a new line is allocated. If
 * allocation fails then NULL is returned.
 */
static char *expand_tabs(char *line, char **storage, size_t *storage_len)
{
	size_t i, src, dst, len, new_storage_len, num_tabs;
	char *new_line;
	size_t line_len = strlen(line);

	for (num_tabs = 0, i = 0; i < line_len; i++)
		if (line[i] == '\t')
			num_tabs++;

	if (num_tabs == 0)
		return line;

	/*
	 * Space for the line and '\0', less the leading and trailing
	 * spaces. Each tab may introduce 7 additional spaces.
	 */
	new_storage_len = line_len + 1 + (num_tabs * 7);

	new_line = malloc(new_storage_len);
	if (new_line == NULL) {
		pr_err("Failure allocating memory for tab expansion\n");
		return NULL;
	}

	/*
	 * Copy regions starting at src and expand tabs. If there are two
	 * adjacent tabs then 'src == i', the memcpy is of size 0 and the spaces
	 * are inserted.
	 */
	for (i = 0, src = 0, dst = 0; i < line_len && num_tabs; i++) {
		if (line[i] == '\t') {
			len = i - src;
			memcpy(&new_line[dst], &line[src], len);
			dst += len;
			new_line[dst++] = ' ';
			while (dst % 8 != 0)
				new_line[dst++] = ' ';
			src = i + 1;
			num_tabs--;
		}
	}

	/* Expand the last region. */
	len = line_len - src;
	memcpy(&new_line[dst], &line[src], len);
	dst += len;
	new_line[dst] = '\0';

	free(*storage);
	*storage = new_line;
	*storage_len = new_storage_len;
	return new_line;
}

int symbol__disassemble(struct symbol *sym, struct annotate_args *args)
{
	struct annotation_options *opts = &annotate_opts;
	struct map *map = args->ms.map;
	struct dso *dso = map__dso(map);
	char *command;
	FILE *file;
	char symfs_filename[PATH_MAX];
	struct kcore_extract kce;
	bool delete_extract = false;
	bool decomp = false;
	int lineno = 0;
	char *fileloc = NULL;
	int nline;
	char *line;
	size_t line_len;
	const char *objdump_argv[] = {
		"/bin/sh",
		"-c",
		NULL, /* Will be the objdump command to run. */
		"--",
		NULL, /* Will be the symfs path. */
		NULL,
	};
	struct child_process objdump_process;
	int err = dso__disassemble_filename(dso, symfs_filename, sizeof(symfs_filename));

	if (err)
		return err;

	pr_debug("%s: filename=%s, sym=%s, start=%#" PRIx64 ", end=%#" PRIx64 "\n", __func__,
		 symfs_filename, sym->name, map__unmap_ip(map, sym->start),
		 map__unmap_ip(map, sym->end));

	pr_debug("annotating [%p] %30s : [%p] %30s\n",
		 dso, dso__long_name(dso), sym, sym->name);

	if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_PROG_INFO) {
		return symbol__disassemble_bpf(sym, args);
	} else if (dso__binary_type(dso) == DSO_BINARY_TYPE__BPF_IMAGE) {
		return symbol__disassemble_bpf_image(sym, args);
	} else if (dso__binary_type(dso) == DSO_BINARY_TYPE__NOT_FOUND) {
		return -1;
	} else if (dso__is_kcore(dso)) {
		kce.kcore_filename = symfs_filename;
		kce.addr = map__rip_2objdump(map, sym->start);
		kce.offs = sym->start;
		kce.len = sym->end - sym->start;
		if (!kcore_extract__create(&kce)) {
			delete_extract = true;
			strlcpy(symfs_filename, kce.extract_filename,
				sizeof(symfs_filename));
		}
	} else if (dso__needs_decompress(dso)) {
		char tmp[KMOD_DECOMP_LEN];

		if (dso__decompress_kmodule_path(dso, symfs_filename,
						 tmp, sizeof(tmp)) < 0)
			return -1;

		decomp = true;
		strcpy(symfs_filename, tmp);
	}

	/*
	 * For powerpc data type profiling, use the dso__data_read_offset
	 * to read raw instruction directly and interpret the binary code
	 * to understand instructions and register fields. For sort keys as
	 * type and typeoff, disassemble to mnemonic notation is
	 * not required in case of powerpc.
	 */
	if (arch__is(args->arch, "powerpc")) {
		extern const char *sort_order;

		if (sort_order && !strstr(sort_order, "sym")) {
			err = symbol__disassemble_raw(symfs_filename, sym, args);
			if (err == 0)
				goto out_remove_tmp;
#ifdef HAVE_LIBCAPSTONE_SUPPORT
			err = symbol__disassemble_capstone_powerpc(symfs_filename, sym, args);
			if (err == 0)
				goto out_remove_tmp;
#endif
		}
	}

#ifdef HAVE_LIBLLVM_SUPPORT
	err = symbol__disassemble_llvm(symfs_filename, sym, args);
	if (err == 0)
		goto out_remove_tmp;
#endif
#ifdef HAVE_LIBCAPSTONE_SUPPORT
	err = symbol__disassemble_capstone(symfs_filename, sym, args);
	if (err == 0)
		goto out_remove_tmp;
#endif

	err = asprintf(&command,
		 "%s %s%s --start-address=0x%016" PRIx64
		 " --stop-address=0x%016" PRIx64
		 " %s -d %s %s %s %c%s%c %s%s -C \"$1\"",
		 opts->objdump_path ?: "objdump",
		 opts->disassembler_style ? "-M " : "",
		 opts->disassembler_style ?: "",
		 map__rip_2objdump(map, sym->start),
		 map__rip_2objdump(map, sym->end),
		 opts->show_linenr ? "-l" : "",
		 opts->show_asm_raw ? "" : "--no-show-raw-insn",
		 opts->annotate_src ? "-S" : "",
		 opts->prefix ? "--prefix " : "",
		 opts->prefix ? '"' : ' ',
		 opts->prefix ?: "",
		 opts->prefix ? '"' : ' ',
		 opts->prefix_strip ? "--prefix-strip=" : "",
		 opts->prefix_strip ?: "");

	if (err < 0) {
		pr_err("Failure allocating memory for the command to run\n");
		goto out_remove_tmp;
	}

	pr_debug("Executing: %s\n", command);

	objdump_argv[2] = command;
	objdump_argv[4] = symfs_filename;

	/* Create a pipe to read from for stdout */
	memset(&objdump_process, 0, sizeof(objdump_process));
	objdump_process.argv = objdump_argv;
	objdump_process.out = -1;
	objdump_process.err = -1;
	objdump_process.no_stderr = 1;
	if (start_command(&objdump_process)) {
		pr_err("Failure starting to run %s\n", command);
		err = -1;
		goto out_free_command;
	}

	file = fdopen(objdump_process.out, "r");
	if (!file) {
		pr_err("Failure creating FILE stream for %s\n", command);
		/*
		 * If we were using debug info should retry with
		 * original binary.
		 */
		err = -1;
		goto out_close_stdout;
	}

	/* Storage for getline. */
	line = NULL;
	line_len = 0;

	nline = 0;
	while (!feof(file)) {
		const char *match;
		char *expanded_line;

		if (getline(&line, &line_len, file) < 0 || !line)
			break;

		/* Skip lines containing "filename:" */
		match = strstr(line, symfs_filename);
		if (match && match[strlen(symfs_filename)] == ':')
			continue;

		expanded_line = strim(line);
		expanded_line = expand_tabs(expanded_line, &line, &line_len);
		if (!expanded_line)
			break;

		/*
		 * The source code line number (lineno) needs to be kept in
		 * across calls to symbol__parse_objdump_line(), so that it
		 * can associate it with the instructions till the next one.
		 * See disasm_line__new() and struct disasm_line::line_nr.
		 */
		if (symbol__parse_objdump_line(sym, args, expanded_line,
					       &lineno, &fileloc) < 0)
			break;
		nline++;
	}
	free(line);
	free(fileloc);

	err = finish_command(&objdump_process);
	if (err)
		pr_err("Error running %s\n", command);

	if (nline == 0) {
		err = -1;
		pr_err("No output from %s\n", command);
	}

	/*
	 * kallsyms does not have symbol sizes so there may a nop at the end.
	 * Remove it.
	 */
	if (dso__is_kcore(dso))
		delete_last_nop(sym);

	fclose(file);

out_close_stdout:
	close(objdump_process.out);

out_free_command:
	free(command);

out_remove_tmp:
	if (decomp)
		unlink(symfs_filename);

	if (delete_extract)
		kcore_extract__delete(&kce);

	return err;
}
