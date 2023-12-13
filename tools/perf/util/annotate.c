// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-annotate.c, see those files for further
 * copyright notes.
 */

#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdlib.h>
#include "util.h" // hex_width()
#include "ui/ui.h"
#include "sort.h"
#include "build-id.h"
#include "color.h"
#include "config.h"
#include "dso.h"
#include "env.h"
#include "map.h"
#include "maps.h"
#include "symbol.h"
#include "srcline.h"
#include "units.h"
#include "debug.h"
#include "annotate.h"
#include "annotate-data.h"
#include "evsel.h"
#include "evlist.h"
#include "bpf-event.h"
#include "bpf-utils.h"
#include "block-range.h"
#include "string2.h"
#include "dwarf-regs.h"
#include "util/event.h"
#include "util/sharded_mutex.h"
#include "arch/common.h"
#include "namespaces.h"
#include <regex.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <subcmd/parse-options.h>
#include <subcmd/run-command.h>

/* FIXME: For the HE_COLORSET */
#include "ui/browser.h"

/*
 * FIXME: Using the same values as slang.h,
 * but that header may not be available everywhere
 */
#define LARROW_CHAR	((unsigned char)',')
#define RARROW_CHAR	((unsigned char)'+')
#define DARROW_CHAR	((unsigned char)'.')
#define UARROW_CHAR	((unsigned char)'-')

#include <linux/ctype.h>

/* global annotation options */
struct annotation_options annotate_opts;

static regex_t	 file_lineno;

static struct ins_ops *ins__find(struct arch *arch, const char *name);
static void ins__sort(struct arch *arch);
static int disasm_line__parse(char *line, const char **namep, char **rawp);
static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			  struct ins_operands *ops, int max_ins_name);
static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			  struct ins_operands *ops, int max_ins_name);

struct arch {
	const char	*name;
	struct ins	*instructions;
	size_t		nr_instructions;
	size_t		nr_instructions_allocated;
	struct ins_ops  *(*associate_instruction_ops)(struct arch *arch, const char *name);
	bool		sorted_instructions;
	bool		initialized;
	const char	*insn_suffix;
	void		*priv;
	unsigned int	model;
	unsigned int	family;
	int		(*init)(struct arch *arch, char *cpuid);
	bool		(*ins_is_fused)(struct arch *arch, const char *ins1,
					const char *ins2);
	struct		{
		char comment_char;
		char skip_functions_char;
		char register_char;
		char memory_ref_char;
	} objdump;
};

static struct ins_ops call_ops;
static struct ins_ops dec_ops;
static struct ins_ops jump_ops;
static struct ins_ops mov_ops;
static struct ins_ops nop_ops;
static struct ins_ops lock_ops;
static struct ins_ops ret_ops;

/* Data type collection debug statistics */
struct annotated_data_stat ann_data_stat;
LIST_HEAD(ann_insn_stat);

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
		},
	},
	{
		.name = "powerpc",
		.init = powerpc__annotate_init,
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

static void ins__delete(struct ins_operands *ops)
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

static int call__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms)
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

static int jump__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms)
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

static int lock__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms)
{
	ops->locked.ops = zalloc(sizeof(*ops->locked.ops));
	if (ops->locked.ops == NULL)
		return 0;

	if (disasm_line__parse(ops->raw, &ops->locked.ins.name, &ops->locked.ops->raw) < 0)
		goto out_free_ops;

	ops->locked.ins.ops = ins__find(arch, ops->locked.ins.name);

	if (ops->locked.ins.ops == NULL)
		goto out_free_ops;

	if (ops->locked.ins.ops->parse &&
	    ops->locked.ins.ops->parse(arch, ops->locked.ops, ms) < 0)
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
		ins__delete(ops->locked.ops);

	zfree(&ops->locked.ops);
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

static int mov__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms __maybe_unused)
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

static int dec__parse(struct arch *arch __maybe_unused, struct ins_operands *ops, struct map_symbol *ms __maybe_unused)
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

static struct ins_ops *__ins__find(struct arch *arch, const char *name)
{
	struct ins *ins;
	const int nmemb = arch->nr_instructions;

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

static struct ins_ops *ins__find(struct arch *arch, const char *name)
{
	struct ins_ops *ops = __ins__find(arch, name);

	if (!ops && arch->associate_instruction_ops)
		ops = arch->associate_instruction_ops(arch, name);

	return ops;
}

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

static struct arch *arch__find(const char *name)
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

static struct annotated_source *annotated_source__new(void)
{
	struct annotated_source *src = zalloc(sizeof(*src));

	if (src != NULL)
		INIT_LIST_HEAD(&src->source);

	return src;
}

static __maybe_unused void annotated_source__delete(struct annotated_source *src)
{
	if (src == NULL)
		return;
	zfree(&src->histograms);
	free(src);
}

static int annotated_source__alloc_histograms(struct annotated_source *src,
					      size_t size, int nr_hists)
{
	size_t sizeof_sym_hist;

	/*
	 * Add buffer of one element for zero length symbol.
	 * When sample is taken from first instruction of
	 * zero length symbol, perf still resolves it and
	 * shows symbol name in perf report and allows to
	 * annotate it.
	 */
	if (size == 0)
		size = 1;

	/* Check for overflow when calculating sizeof_sym_hist */
	if (size > (SIZE_MAX - sizeof(struct sym_hist)) / sizeof(struct sym_hist_entry))
		return -1;

	sizeof_sym_hist = (sizeof(struct sym_hist) + size * sizeof(struct sym_hist_entry));

	/* Check for overflow in zalloc argument */
	if (sizeof_sym_hist > SIZE_MAX / nr_hists)
		return -1;

	src->sizeof_sym_hist = sizeof_sym_hist;
	src->nr_histograms   = nr_hists;
	src->histograms	     = calloc(nr_hists, sizeof_sym_hist) ;
	return src->histograms ? 0 : -1;
}

void symbol__annotate_zero_histograms(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__lock(notes);
	if (notes->src != NULL) {
		memset(notes->src->histograms, 0,
		       notes->src->nr_histograms * notes->src->sizeof_sym_hist);
	}
	if (notes->branch && notes->branch->cycles_hist) {
		memset(notes->branch->cycles_hist, 0,
		       symbol__size(sym) * sizeof(struct cyc_hist));
	}
	annotation__unlock(notes);
}

static int __symbol__account_cycles(struct cyc_hist *ch,
				    u64 start,
				    unsigned offset, unsigned cycles,
				    unsigned have_start)
{
	/*
	 * For now we can only account one basic block per
	 * final jump. But multiple could be overlapping.
	 * Always account the longest one. So when
	 * a shorter one has been already seen throw it away.
	 *
	 * We separately always account the full cycles.
	 */
	ch[offset].num_aggr++;
	ch[offset].cycles_aggr += cycles;

	if (cycles > ch[offset].cycles_max)
		ch[offset].cycles_max = cycles;

	if (ch[offset].cycles_min) {
		if (cycles && cycles < ch[offset].cycles_min)
			ch[offset].cycles_min = cycles;
	} else
		ch[offset].cycles_min = cycles;

	if (!have_start && ch[offset].have_start)
		return 0;
	if (ch[offset].num) {
		if (have_start && (!ch[offset].have_start ||
				   ch[offset].start > start)) {
			ch[offset].have_start = 0;
			ch[offset].cycles = 0;
			ch[offset].num = 0;
			if (ch[offset].reset < 0xffff)
				ch[offset].reset++;
		} else if (have_start &&
			   ch[offset].start < start)
			return 0;
	}

	if (ch[offset].num < NUM_SPARKS)
		ch[offset].cycles_spark[ch[offset].num] = cycles;

	ch[offset].have_start = have_start;
	ch[offset].start = start;
	ch[offset].cycles += cycles;
	ch[offset].num++;
	return 0;
}

static int __symbol__inc_addr_samples(struct map_symbol *ms,
				      struct annotated_source *src, int evidx, u64 addr,
				      struct perf_sample *sample)
{
	struct symbol *sym = ms->sym;
	unsigned offset;
	struct sym_hist *h;

	pr_debug3("%s: addr=%#" PRIx64 "\n", __func__, map__unmap_ip(ms->map, addr));

	if ((addr < sym->start || addr >= sym->end) &&
	    (addr != sym->end || sym->start != sym->end)) {
		pr_debug("%s(%d): ERANGE! sym->name=%s, start=%#" PRIx64 ", addr=%#" PRIx64 ", end=%#" PRIx64 "\n",
		       __func__, __LINE__, sym->name, sym->start, addr, sym->end);
		return -ERANGE;
	}

	offset = addr - sym->start;
	h = annotated_source__histogram(src, evidx);
	if (h == NULL) {
		pr_debug("%s(%d): ENOMEM! sym->name=%s, start=%#" PRIx64 ", addr=%#" PRIx64 ", end=%#" PRIx64 ", func: %d\n",
			 __func__, __LINE__, sym->name, sym->start, addr, sym->end, sym->type == STT_FUNC);
		return -ENOMEM;
	}
	h->nr_samples++;
	h->addr[offset].nr_samples++;
	h->period += sample->period;
	h->addr[offset].period += sample->period;

	pr_debug3("%#" PRIx64 " %s: period++ [addr: %#" PRIx64 ", %#" PRIx64
		  ", evidx=%d] => nr_samples: %" PRIu64 ", period: %" PRIu64 "\n",
		  sym->start, sym->name, addr, addr - sym->start, evidx,
		  h->addr[offset].nr_samples, h->addr[offset].period);
	return 0;
}

struct annotated_branch *annotation__get_branch(struct annotation *notes)
{
	if (notes == NULL)
		return NULL;

	if (notes->branch == NULL)
		notes->branch = zalloc(sizeof(*notes->branch));

	return notes->branch;
}

static struct cyc_hist *symbol__cycles_hist(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	struct annotated_branch *branch;

	branch = annotation__get_branch(notes);
	if (branch == NULL)
		return NULL;

	if (branch->cycles_hist == NULL) {
		const size_t size = symbol__size(sym);

		branch->cycles_hist = calloc(size, sizeof(struct cyc_hist));
	}

	return branch->cycles_hist;
}

struct annotated_source *symbol__hists(struct symbol *sym, int nr_hists)
{
	struct annotation *notes = symbol__annotation(sym);

	if (notes->src == NULL) {
		notes->src = annotated_source__new();
		if (notes->src == NULL)
			return NULL;
		goto alloc_histograms;
	}

	if (notes->src->histograms == NULL) {
alloc_histograms:
		annotated_source__alloc_histograms(notes->src, symbol__size(sym),
						   nr_hists);
	}

	return notes->src;
}

static int symbol__inc_addr_samples(struct map_symbol *ms,
				    struct evsel *evsel, u64 addr,
				    struct perf_sample *sample)
{
	struct symbol *sym = ms->sym;
	struct annotated_source *src;

	if (sym == NULL)
		return 0;
	src = symbol__hists(sym, evsel->evlist->core.nr_entries);
	return src ? __symbol__inc_addr_samples(ms, src, evsel->core.idx, addr, sample) : 0;
}

static int symbol__account_cycles(u64 addr, u64 start,
				  struct symbol *sym, unsigned cycles)
{
	struct cyc_hist *cycles_hist;
	unsigned offset;

	if (sym == NULL)
		return 0;
	cycles_hist = symbol__cycles_hist(sym);
	if (cycles_hist == NULL)
		return -ENOMEM;
	if (addr < sym->start || addr >= sym->end)
		return -ERANGE;

	if (start) {
		if (start < sym->start || start >= sym->end)
			return -ERANGE;
		if (start >= addr)
			start = 0;
	}
	offset = addr - sym->start;
	return __symbol__account_cycles(cycles_hist,
					start ? start - sym->start : 0,
					offset, cycles,
					!!start);
}

int addr_map_symbol__account_cycles(struct addr_map_symbol *ams,
				    struct addr_map_symbol *start,
				    unsigned cycles)
{
	u64 saddr = 0;
	int err;

	if (!cycles)
		return 0;

	/*
	 * Only set start when IPC can be computed. We can only
	 * compute it when the basic block is completely in a single
	 * function.
	 * Special case the case when the jump is elsewhere, but
	 * it starts on the function start.
	 */
	if (start &&
		(start->ms.sym == ams->ms.sym ||
		 (ams->ms.sym &&
		  start->addr == ams->ms.sym->start + map__start(ams->ms.map))))
		saddr = start->al_addr;
	if (saddr == 0)
		pr_debug2("BB with bad start: addr %"PRIx64" start %"PRIx64" sym %"PRIx64" saddr %"PRIx64"\n",
			ams->addr,
			start ? start->addr : 0,
			ams->ms.sym ? ams->ms.sym->start + map__start(ams->ms.map) : 0,
			saddr);
	err = symbol__account_cycles(ams->al_addr, saddr, ams->ms.sym, cycles);
	if (err)
		pr_debug2("account_cycles failed %d\n", err);
	return err;
}

static unsigned annotation__count_insn(struct annotation *notes, u64 start, u64 end)
{
	unsigned n_insn = 0;
	u64 offset;

	for (offset = start; offset <= end; offset++) {
		if (notes->src->offsets[offset])
			n_insn++;
	}
	return n_insn;
}

static void annotated_branch__delete(struct annotated_branch *branch)
{
	if (branch) {
		zfree(&branch->cycles_hist);
		free(branch);
	}
}

static void annotation__count_and_fill(struct annotation *notes, u64 start, u64 end, struct cyc_hist *ch)
{
	unsigned n_insn;
	unsigned int cover_insn = 0;
	u64 offset;

	n_insn = annotation__count_insn(notes, start, end);
	if (n_insn && ch->num && ch->cycles) {
		struct annotated_branch *branch;
		float ipc = n_insn / ((double)ch->cycles / (double)ch->num);

		/* Hide data when there are too many overlaps. */
		if (ch->reset >= 0x7fff)
			return;

		for (offset = start; offset <= end; offset++) {
			struct annotation_line *al = notes->src->offsets[offset];

			if (al && al->cycles && al->cycles->ipc == 0.0) {
				al->cycles->ipc = ipc;
				cover_insn++;
			}
		}

		branch = annotation__get_branch(notes);
		if (cover_insn && branch) {
			branch->hit_cycles += ch->cycles;
			branch->hit_insn += n_insn * ch->num;
			branch->cover_insn += cover_insn;
		}
	}
}

static int annotation__compute_ipc(struct annotation *notes, size_t size)
{
	int err = 0;
	s64 offset;

	if (!notes->branch || !notes->branch->cycles_hist)
		return 0;

	notes->branch->total_insn = annotation__count_insn(notes, 0, size - 1);
	notes->branch->hit_cycles = 0;
	notes->branch->hit_insn = 0;
	notes->branch->cover_insn = 0;

	annotation__lock(notes);
	for (offset = size - 1; offset >= 0; --offset) {
		struct cyc_hist *ch;

		ch = &notes->branch->cycles_hist[offset];
		if (ch && ch->cycles) {
			struct annotation_line *al;

			al = notes->src->offsets[offset];
			if (al && al->cycles == NULL) {
				al->cycles = zalloc(sizeof(*al->cycles));
				if (al->cycles == NULL) {
					err = ENOMEM;
					break;
				}
			}
			if (ch->have_start)
				annotation__count_and_fill(notes, ch->start, offset, ch);
			if (al && ch->num_aggr) {
				al->cycles->avg = ch->cycles_aggr / ch->num_aggr;
				al->cycles->max = ch->cycles_max;
				al->cycles->min = ch->cycles_min;
			}
		}
	}

	if (err) {
		while (++offset < (s64)size) {
			struct cyc_hist *ch = &notes->branch->cycles_hist[offset];

			if (ch && ch->cycles) {
				struct annotation_line *al = notes->src->offsets[offset];
				if (al)
					zfree(&al->cycles);
			}
		}
	}

	annotation__unlock(notes);
	return 0;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 struct evsel *evsel)
{
	return symbol__inc_addr_samples(&ams->ms, evsel, ams->al_addr, sample);
}

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 struct evsel *evsel, u64 ip)
{
	return symbol__inc_addr_samples(&he->ms, evsel, ip, sample);
}

static void disasm_line__init_ins(struct disasm_line *dl, struct arch *arch, struct map_symbol *ms)
{
	dl->ins.ops = ins__find(arch, dl->ins.name);

	if (!dl->ins.ops)
		return;

	if (dl->ins.ops->parse && dl->ins.ops->parse(arch, &dl->ops, ms) < 0)
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

struct annotate_args {
	struct arch		  *arch;
	struct map_symbol	  ms;
	struct evsel		  *evsel;
	struct annotation_options *options;
	s64			  offset;
	char			  *line;
	int			  line_nr;
	char			  *fileloc;
};

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
static struct disasm_line *disasm_line__new(struct annotate_args *args)
{
	struct disasm_line *dl = NULL;
	int nr = 1;

	if (evsel__is_group_event(args->evsel))
		nr = args->evsel->core.nr_members;

	dl = zalloc(disasm_line_size(nr));
	if (!dl)
		return NULL;

	annotation_line__init(&dl->al, args, nr);
	if (dl->al.line == NULL)
		goto out_delete;

	if (args->offset != -1) {
		if (disasm_line__parse(dl->al.line, &dl->ins.name, &dl->ops.raw) < 0)
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
		ins__delete(&dl->ops);
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

void annotation__exit(struct annotation *notes)
{
	annotated_source__delete(notes->src);
	annotated_branch__delete(notes->branch);
}

static struct sharded_mutex *sharded_mutex;

static void annotation__init_sharded_mutex(void)
{
	/* As many mutexes as there are CPUs. */
	sharded_mutex = sharded_mutex__new(cpu__max_present_cpu().cpu);
}

static size_t annotation__hash(const struct annotation *notes)
{
	return (size_t)notes;
}

static struct mutex *annotation__get_mutex(const struct annotation *notes)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, annotation__init_sharded_mutex);
	if (!sharded_mutex)
		return NULL;

	return sharded_mutex__get_mutex(sharded_mutex, annotation__hash(notes));
}

void annotation__lock(struct annotation *notes)
	NO_THREAD_SAFETY_ANALYSIS
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (mutex)
		mutex_lock(mutex);
}

void annotation__unlock(struct annotation *notes)
	NO_THREAD_SAFETY_ANALYSIS
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (mutex)
		mutex_unlock(mutex);
}

bool annotation__trylock(struct annotation *notes)
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (!mutex)
		return false;

	return mutex_trylock(mutex);
}


static void annotation_line__add(struct annotation_line *al, struct list_head *head)
{
	list_add_tail(&al->node, head);
}

struct annotation_line *
annotation_line__next(struct annotation_line *pos, struct list_head *head)
{
	list_for_each_entry_continue(pos, head, node)
		if (pos->offset >= 0)
			return pos;

	return NULL;
}

static const char *annotate__address_color(struct block_range *br)
{
	double cov = block_range__coverage(br);

	if (cov >= 0) {
		/* mark red for >75% coverage */
		if (cov > 0.75)
			return PERF_COLOR_RED;

		/* mark dull for <1% coverage */
		if (cov < 0.01)
			return PERF_COLOR_NORMAL;
	}

	return PERF_COLOR_MAGENTA;
}

static const char *annotate__asm_color(struct block_range *br)
{
	double cov = block_range__coverage(br);

	if (cov >= 0) {
		/* mark dull for <1% coverage */
		if (cov < 0.01)
			return PERF_COLOR_NORMAL;
	}

	return PERF_COLOR_BLUE;
}

static void annotate__branch_printf(struct block_range *br, u64 addr)
{
	bool emit_comment = true;

	if (!br)
		return;

#if 1
	if (br->is_target && br->start == addr) {
		struct block_range *branch = br;
		double p;

		/*
		 * Find matching branch to our target.
		 */
		while (!branch->is_branch)
			branch = block_range__next(branch);

		p = 100 *(double)br->entry / branch->coverage;

		if (p > 0.1) {
			if (emit_comment) {
				emit_comment = false;
				printf("\t#");
			}

			/*
			 * The percentage of coverage joined at this target in relation
			 * to the next branch.
			 */
			printf(" +%.2f%%", p);
		}
	}
#endif
	if (br->is_branch && br->end == addr) {
		double p = 100*(double)br->taken / br->coverage;

		if (p > 0.1) {
			if (emit_comment) {
				emit_comment = false;
				printf("\t#");
			}

			/*
			 * The percentage of coverage leaving at this branch, and
			 * its prediction ratio.
			 */
			printf(" -%.2f%% (p:%.2f%%)", p, 100*(double)br->pred  / br->taken);
		}
	}
}

static int disasm_line__print(struct disasm_line *dl, u64 start, int addr_fmt_width)
{
	s64 offset = dl->al.offset;
	const u64 addr = start + offset;
	struct block_range *br;

	br = block_range__find(addr);
	color_fprintf(stdout, annotate__address_color(br), "  %*" PRIx64 ":", addr_fmt_width, addr);
	color_fprintf(stdout, annotate__asm_color(br), "%s", dl->al.line);
	annotate__branch_printf(br, addr);
	return 0;
}

static int
annotation_line__print(struct annotation_line *al, struct symbol *sym, u64 start,
		       struct evsel *evsel, u64 len, int min_pcnt, int printed,
		       int max_lines, struct annotation_line *queue, int addr_fmt_width,
		       int percent_type)
{
	struct disasm_line *dl = container_of(al, struct disasm_line, al);
	static const char *prev_line;

	if (al->offset != -1) {
		double max_percent = 0.0;
		int i, nr_percent = 1;
		const char *color;
		struct annotation *notes = symbol__annotation(sym);

		for (i = 0; i < al->data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i],
							   percent_type);

			if (percent > max_percent)
				max_percent = percent;
		}

		if (al->data_nr > nr_percent)
			nr_percent = al->data_nr;

		if (max_percent < min_pcnt)
			return -1;

		if (max_lines && printed >= max_lines)
			return 1;

		if (queue != NULL) {
			list_for_each_entry_from(queue, &notes->src->source, node) {
				if (queue == al)
					break;
				annotation_line__print(queue, sym, start, evsel, len,
						       0, 0, 1, NULL, addr_fmt_width,
						       percent_type);
			}
		}

		color = get_percent_color(max_percent);

		for (i = 0; i < nr_percent; i++) {
			struct annotation_data *data = &al->data[i];
			double percent;

			percent = annotation_data__percent(data, percent_type);
			color = get_percent_color(percent);

			if (symbol_conf.show_total_period)
				color_fprintf(stdout, color, " %11" PRIu64,
					      data->he.period);
			else if (symbol_conf.show_nr_samples)
				color_fprintf(stdout, color, " %7" PRIu64,
					      data->he.nr_samples);
			else
				color_fprintf(stdout, color, " %7.2f", percent);
		}

		printf(" : ");

		disasm_line__print(dl, start, addr_fmt_width);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored addr with the same filename:line
		 */
		if (al->path) {
			if (!prev_line || strcmp(prev_line, al->path)) {
				color_fprintf(stdout, color, " // %s", al->path);
				prev_line = al->path;
			}
		}

		printf("\n");
	} else if (max_lines && printed >= max_lines)
		return 1;
	else {
		int width = symbol_conf.show_total_period ? 12 : 8;

		if (queue)
			return -1;

		if (evsel__is_group_event(evsel))
			width *= evsel->core.nr_members;

		if (!*al->line)
			printf(" %*s:\n", width, " ");
		else
			printf(" %*s: %-*d %s\n", width, " ", addr_fmt_width, al->line_nr, al->line);
	}

	return 0;
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

static __attribute__((constructor)) void symbol__init_regexpr(void)
{
	regcomp(&file_lineno, "^/[^:]+:([0-9]+)", REG_EXTENDED);
}

static void delete_last_nop(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	struct list_head *list = &notes->src->source;
	struct disasm_line *dl;

	while (!list_empty(list)) {
		dl = list_entry(list->prev, struct disasm_line, al.node);

		if (dl->ins.ops) {
			if (dl->ins.ops != &nop_ops)
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

		if (dso->has_build_id) {
			build_id__sprintf(&dso->bid, bf + 15);
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
		scnprintf(buf, buflen, "Invalid BPF file: %s.", dso->long_name);
		break;
	case SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF:
		scnprintf(buf, buflen, "The %s BPF file has no BTF section, compile with -g or use pahole -J.",
			  dso->long_name);
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

	if (dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(dso))
		return SYMBOL_ANNOTATE_ERRNO__NO_VMLINUX;

	build_id_filename = dso__build_id_filename(dso, NULL, 0, false);
	if (build_id_filename) {
		__symbol__join_symfs(filename, filename_size, build_id_filename);
		free(build_id_filename);
	} else {
		if (dso->has_build_id)
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
		if (dso->kernel && dso->long_name[0] == '/')
			snprintf(filename, filename_size, "%s", dso->long_name);
		else
			__symbol__join_symfs(filename, filename_size, dso->long_name);

		mutex_lock(&dso->lock);
		if (access(filename, R_OK) && errno == ENOENT && dso->nsinfo) {
			char *new_name = dso__filename_with_chroot(dso, filename);
			if (new_name) {
				strlcpy(filename, new_name, filename_size);
				free(new_name);
			}
		}
		mutex_unlock(&dso->lock);
	}

	free(build_id_path);
	return 0;
}

#if defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)
#define PACKAGE "perf"
#include <bfd.h>
#include <dis-asm.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <linux/btf.h>
#include <tools/dis-asm-compat.h>

static int symbol__disassemble_bpf(struct symbol *sym,
				   struct annotate_args *args)
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

	if (dso->binary_type != DSO_BINARY_TYPE__BPF_PROG_INFO)
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

	info_node = perf_env__find_bpf_prog_info(dso->bpf_prog.env,
						 dso->bpf_prog.id);
	if (!info_node) {
		ret = SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF;
		goto out;
	}
	info_linear = info_node->info_linear;
	sub_id = dso->bpf_prog.sub_id;

	info.buffer = (void *)(uintptr_t)(info_linear->info.jited_prog_insns);
	info.buffer_length = info_linear->info.jited_prog_len;

	if (info_linear->info.nr_line_info)
		prog_linfo = bpf_prog_linfo__new(&info_linear->info);

	if (info_linear->info.btf_id) {
		struct btf_node *node;

		node = perf_env__find_btf(dso->bpf_prog.env,
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
static int symbol__disassemble_bpf(struct symbol *sym __maybe_unused,
				   struct annotate_args *args __maybe_unused)
{
	return SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF;
}
#endif // defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)

static int
symbol__disassemble_bpf_image(struct symbol *sym,
			      struct annotate_args *args)
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

static int symbol__disassemble(struct symbol *sym, struct annotate_args *args)
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
		 dso, dso->long_name, sym, sym->name);

	if (dso->binary_type == DSO_BINARY_TYPE__BPF_PROG_INFO) {
		return symbol__disassemble_bpf(sym, args);
	} else if (dso->binary_type == DSO_BINARY_TYPE__BPF_IMAGE) {
		return symbol__disassemble_bpf_image(sym, args);
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

static void calc_percent(struct sym_hist *sym_hist,
			 struct hists *hists,
			 struct annotation_data *data,
			 s64 offset, s64 end)
{
	unsigned int hits = 0;
	u64 period = 0;

	while (offset < end) {
		hits   += sym_hist->addr[offset].nr_samples;
		period += sym_hist->addr[offset].period;
		++offset;
	}

	if (sym_hist->nr_samples) {
		data->he.period     = period;
		data->he.nr_samples = hits;
		data->percent[PERCENT_HITS_LOCAL] = 100.0 * hits / sym_hist->nr_samples;
	}

	if (hists->stats.nr_non_filtered_samples)
		data->percent[PERCENT_HITS_GLOBAL] = 100.0 * hits / hists->stats.nr_non_filtered_samples;

	if (sym_hist->period)
		data->percent[PERCENT_PERIOD_LOCAL] = 100.0 * period / sym_hist->period;

	if (hists->stats.total_period)
		data->percent[PERCENT_PERIOD_GLOBAL] = 100.0 * period / hists->stats.total_period;
}

static void annotation__calc_percent(struct annotation *notes,
				     struct evsel *leader, s64 len)
{
	struct annotation_line *al, *next;
	struct evsel *evsel;

	list_for_each_entry(al, &notes->src->source, node) {
		s64 end;
		int i = 0;

		if (al->offset == -1)
			continue;

		next = annotation_line__next(al, &notes->src->source);
		end  = next ? next->offset : len;

		for_each_group_evsel(evsel, leader) {
			struct hists *hists = evsel__hists(evsel);
			struct annotation_data *data;
			struct sym_hist *sym_hist;

			BUG_ON(i >= al->data_nr);

			sym_hist = annotation__histogram(notes, evsel->core.idx);
			data = &al->data[i++];

			calc_percent(sym_hist, hists, data, al->offset, end);
		}
	}
}

void symbol__calc_percent(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__calc_percent(notes, evsel, symbol__size(sym));
}

static int evsel__get_arch(struct evsel *evsel, struct arch **parch)
{
	struct perf_env *env = evsel__env(evsel);
	const char *arch_name = perf_env__arch(env);
	struct arch *arch;
	int err;

	if (!arch_name)
		return errno;

	*parch = arch = arch__find(arch_name);
	if (arch == NULL) {
		pr_err("%s: unsupported arch %s\n", __func__, arch_name);
		return ENOTSUP;
	}

	if (arch->init) {
		err = arch->init(arch, env ? env->cpuid : NULL);
		if (err) {
			pr_err("%s: failed to initialize %s arch priv area\n",
			       __func__, arch->name);
			return err;
		}
	}
	return 0;
}

int symbol__annotate(struct map_symbol *ms, struct evsel *evsel,
		     struct arch **parch)
{
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct annotate_args args = {
		.evsel		= evsel,
		.options	= &annotate_opts,
	};
	struct arch *arch = NULL;
	int err;

	err = evsel__get_arch(evsel, &arch);
	if (err < 0)
		return err;

	if (parch)
		*parch = arch;

	args.arch = arch;
	args.ms = *ms;
	if (annotate_opts.full_addr)
		notes->start = map__objdump_2mem(ms->map, ms->sym->start);
	else
		notes->start = map__rip_2objdump(ms->map, ms->sym->start);

	return symbol__disassemble(sym, &args);
}

static void insert_source_line(struct rb_root *root, struct annotation_line *al)
{
	struct annotation_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	unsigned int percent_type = annotate_opts.percent_type;
	int i, ret;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct annotation_line, rb_node);

		ret = strcmp(iter->path, al->path);
		if (ret == 0) {
			for (i = 0; i < al->data_nr; i++) {
				iter->data[i].percent_sum += annotation_data__percent(&al->data[i],
										      percent_type);
			}
			return;
		}

		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	for (i = 0; i < al->data_nr; i++) {
		al->data[i].percent_sum = annotation_data__percent(&al->data[i],
								   percent_type);
	}

	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static int cmp_source_line(struct annotation_line *a, struct annotation_line *b)
{
	int i;

	for (i = 0; i < a->data_nr; i++) {
		if (a->data[i].percent_sum == b->data[i].percent_sum)
			continue;
		return a->data[i].percent_sum > b->data[i].percent_sum;
	}

	return 0;
}

static void __resort_source_line(struct rb_root *root, struct annotation_line *al)
{
	struct annotation_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct annotation_line, rb_node);

		if (cmp_source_line(al, iter))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static void resort_source_line(struct rb_root *dest_root, struct rb_root *src_root)
{
	struct annotation_line *al;
	struct rb_node *node;

	node = rb_first(src_root);
	while (node) {
		struct rb_node *next;

		al = rb_entry(node, struct annotation_line, rb_node);
		next = rb_next(node);
		rb_erase(node, src_root);

		__resort_source_line(dest_root, al);
		node = next;
	}
}

static void print_summary(struct rb_root *root, const char *filename)
{
	struct annotation_line *al;
	struct rb_node *node;

	printf("\nSorted summary for file %s\n", filename);
	printf("----------------------------------------------\n\n");

	if (RB_EMPTY_ROOT(root)) {
		printf(" Nothing higher than %1.1f%%\n", MIN_GREEN);
		return;
	}

	node = rb_first(root);
	while (node) {
		double percent, percent_max = 0.0;
		const char *color;
		char *path;
		int i;

		al = rb_entry(node, struct annotation_line, rb_node);
		for (i = 0; i < al->data_nr; i++) {
			percent = al->data[i].percent_sum;
			color = get_percent_color(percent);
			color_fprintf(stdout, color, " %7.2f", percent);

			if (percent > percent_max)
				percent_max = percent;
		}

		path = al->path;
		color = get_percent_color(percent_max);
		color_fprintf(stdout, color, " %s\n", path);

		node = rb_next(node);
	}
}

static void symbol__annotate_hits(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel->core.idx);
	u64 len = symbol__size(sym), offset;

	for (offset = 0; offset < len; ++offset)
		if (h->addr[offset].nr_samples != 0)
			printf("%*" PRIx64 ": %" PRIu64 "\n", BITS_PER_LONG / 2,
			       sym->start + offset, h->addr[offset].nr_samples);
	printf("%*s: %" PRIu64 "\n", BITS_PER_LONG / 2, "h->nr_samples", h->nr_samples);
}

static int annotated_source__addr_fmt_width(struct list_head *lines, u64 start)
{
	char bf[32];
	struct annotation_line *line;

	list_for_each_entry_reverse(line, lines, node) {
		if (line->offset != -1)
			return scnprintf(bf, sizeof(bf), "%" PRIx64, start + line->offset);
	}

	return 0;
}

int symbol__annotate_printf(struct map_symbol *ms, struct evsel *evsel)
{
	struct map *map = ms->map;
	struct symbol *sym = ms->sym;
	struct dso *dso = map__dso(map);
	char *filename;
	const char *d_filename;
	const char *evsel_name = evsel__name(evsel);
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel->core.idx);
	struct annotation_line *pos, *queue = NULL;
	struct annotation_options *opts = &annotate_opts;
	u64 start = map__rip_2objdump(map, sym->start);
	int printed = 2, queue_len = 0, addr_fmt_width;
	int more = 0;
	bool context = opts->context;
	u64 len;
	int width = symbol_conf.show_total_period ? 12 : 8;
	int graph_dotted_len;
	char buf[512];

	filename = strdup(dso->long_name);
	if (!filename)
		return -ENOMEM;

	if (opts->full_path)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = symbol__size(sym);

	if (evsel__is_group_event(evsel)) {
		width *= evsel->core.nr_members;
		evsel__group_desc(evsel, buf, sizeof(buf));
		evsel_name = buf;
	}

	graph_dotted_len = printf(" %-*.*s|	Source code & Disassembly of %s for %s (%" PRIu64 " samples, "
				  "percent: %s)\n",
				  width, width, symbol_conf.show_total_period ? "Period" :
				  symbol_conf.show_nr_samples ? "Samples" : "Percent",
				  d_filename, evsel_name, h->nr_samples,
				  percent_type_str(opts->percent_type));

	printf("%-*.*s----\n",
	       graph_dotted_len, graph_dotted_len, graph_dotted_line);

	if (verbose > 0)
		symbol__annotate_hits(sym, evsel);

	addr_fmt_width = annotated_source__addr_fmt_width(&notes->src->source, start);

	list_for_each_entry(pos, &notes->src->source, node) {
		int err;

		if (context && queue == NULL) {
			queue = pos;
			queue_len = 0;
		}

		err = annotation_line__print(pos, sym, start, evsel, len,
					     opts->min_pcnt, printed, opts->max_lines,
					     queue, addr_fmt_width, opts->percent_type);

		switch (err) {
		case 0:
			++printed;
			if (context) {
				printed += queue_len;
				queue = NULL;
				queue_len = 0;
			}
			break;
		case 1:
			/* filtered by max_lines */
			++more;
			break;
		case -1:
		default:
			/*
			 * Filtered by min_pcnt or non IP lines when
			 * context != 0
			 */
			if (!context)
				break;
			if (queue_len == context)
				queue = list_entry(queue->node.next, typeof(*queue), node);
			else
				++queue_len;
			break;
		}
	}

	free(filename);

	return more;
}

static void FILE__set_percent_color(void *fp __maybe_unused,
				    double percent __maybe_unused,
				    bool current __maybe_unused)
{
}

static int FILE__set_jumps_percent_color(void *fp __maybe_unused,
					 int nr __maybe_unused, bool current __maybe_unused)
{
	return 0;
}

static int FILE__set_color(void *fp __maybe_unused, int color __maybe_unused)
{
	return 0;
}

static void FILE__printf(void *fp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(fp, fmt, args);
	va_end(args);
}

static void FILE__write_graph(void *fp, int graph)
{
	const char *s;
	switch (graph) {

	case DARROW_CHAR: s = "↓"; break;
	case UARROW_CHAR: s = "↑"; break;
	case LARROW_CHAR: s = "←"; break;
	case RARROW_CHAR: s = "→"; break;
	default:		s = "?"; break;
	}

	fputs(s, fp);
}

static int symbol__annotate_fprintf2(struct symbol *sym, FILE *fp)
{
	struct annotation *notes = symbol__annotation(sym);
	struct annotation_write_ops wops = {
		.first_line		 = true,
		.obj			 = fp,
		.set_color		 = FILE__set_color,
		.set_percent_color	 = FILE__set_percent_color,
		.set_jumps_percent_color = FILE__set_jumps_percent_color,
		.printf			 = FILE__printf,
		.write_graph		 = FILE__write_graph,
	};
	struct annotation_line *al;

	list_for_each_entry(al, &notes->src->source, node) {
		if (annotation_line__filter(al))
			continue;
		annotation_line__write(al, notes, &wops);
		fputc('\n', fp);
		wops.first_line = false;
	}

	return 0;
}

int map_symbol__annotation_dump(struct map_symbol *ms, struct evsel *evsel)
{
	const char *ev_name = evsel__name(evsel);
	char buf[1024];
	char *filename;
	int err = -1;
	FILE *fp;

	if (asprintf(&filename, "%s.annotation", ms->sym->name) < 0)
		return -1;

	fp = fopen(filename, "w");
	if (fp == NULL)
		goto out_free_filename;

	if (evsel__is_group_event(evsel)) {
		evsel__group_desc(evsel, buf, sizeof(buf));
		ev_name = buf;
	}

	fprintf(fp, "%s() %s\nEvent: %s\n\n",
		ms->sym->name, map__dso(ms->map)->long_name, ev_name);
	symbol__annotate_fprintf2(ms->sym, fp);

	fclose(fp);
	err = 0;
out_free_filename:
	free(filename);
	return err;
}

void symbol__annotate_zero_histogram(struct symbol *sym, int evidx)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evidx);

	memset(h, 0, notes->src->sizeof_sym_hist);
}

void symbol__annotate_decay_histogram(struct symbol *sym, int evidx)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evidx);
	int len = symbol__size(sym), offset;

	h->nr_samples = 0;
	for (offset = 0; offset < len; ++offset) {
		h->addr[offset].nr_samples = h->addr[offset].nr_samples * 7 / 8;
		h->nr_samples += h->addr[offset].nr_samples;
	}
}

void annotated_source__purge(struct annotated_source *as)
{
	struct annotation_line *al, *n;

	list_for_each_entry_safe(al, n, &as->source, node) {
		list_del_init(&al->node);
		disasm_line__free(disasm_line(al));
	}
}

static size_t disasm_line__fprintf(struct disasm_line *dl, FILE *fp)
{
	size_t printed;

	if (dl->al.offset == -1)
		return fprintf(fp, "%s\n", dl->al.line);

	printed = fprintf(fp, "%#" PRIx64 " %s", dl->al.offset, dl->ins.name);

	if (dl->ops.raw[0] != '\0') {
		printed += fprintf(fp, "%.*s %s\n", 6 - (int)printed, " ",
				   dl->ops.raw);
	}

	return printed + fprintf(fp, "\n");
}

size_t disasm__fprintf(struct list_head *head, FILE *fp)
{
	struct disasm_line *pos;
	size_t printed = 0;

	list_for_each_entry(pos, head, al.node)
		printed += disasm_line__fprintf(pos, fp);

	return printed;
}

bool disasm_line__is_valid_local_jump(struct disasm_line *dl, struct symbol *sym)
{
	if (!dl || !dl->ins.ops || !ins__is_jump(&dl->ins) ||
	    !disasm_line__has_local_offset(dl) || dl->ops.target.offset < 0 ||
	    dl->ops.target.offset >= (s64)symbol__size(sym))
		return false;

	return true;
}

void annotation__mark_jump_targets(struct annotation *notes, struct symbol *sym)
{
	u64 offset, size = symbol__size(sym);

	/* PLT symbols contain external offsets */
	if (strstr(sym->name, "@plt"))
		return;

	for (offset = 0; offset < size; ++offset) {
		struct annotation_line *al = notes->src->offsets[offset];
		struct disasm_line *dl;

		dl = disasm_line(al);

		if (!disasm_line__is_valid_local_jump(dl, sym))
			continue;

		al = notes->src->offsets[dl->ops.target.offset];

		/*
		 * FIXME: Oops, no jump target? Buggy disassembler? Or do we
		 * have to adjust to the previous offset?
		 */
		if (al == NULL)
			continue;

		if (++al->jump_sources > notes->max_jump_sources)
			notes->max_jump_sources = al->jump_sources;
	}
}

void annotation__set_offsets(struct annotation *notes, s64 size)
{
	struct annotation_line *al;
	struct annotated_source *src = notes->src;

	src->max_line_len = 0;
	src->nr_entries = 0;
	src->nr_asm_entries = 0;

	list_for_each_entry(al, &src->source, node) {
		size_t line_len = strlen(al->line);

		if (src->max_line_len < line_len)
			src->max_line_len = line_len;
		al->idx = src->nr_entries++;
		if (al->offset != -1) {
			al->idx_asm = src->nr_asm_entries++;
			/*
			 * FIXME: short term bandaid to cope with assembly
			 * routines that comes with labels in the same column
			 * as the address in objdump, sigh.
			 *
			 * E.g. copy_user_generic_unrolled
 			 */
			if (al->offset < size)
				notes->src->offsets[al->offset] = al;
		} else
			al->idx_asm = -1;
	}
}

static inline int width_jumps(int n)
{
	if (n >= 100)
		return 5;
	if (n / 10)
		return 2;
	return 1;
}

static int annotation__max_ins_name(struct annotation *notes)
{
	int max_name = 0, len;
	struct annotation_line *al;

        list_for_each_entry(al, &notes->src->source, node) {
		if (al->offset == -1)
			continue;

		len = strlen(disasm_line(al)->ins.name);
		if (max_name < len)
			max_name = len;
	}

	return max_name;
}

void annotation__init_column_widths(struct annotation *notes, struct symbol *sym)
{
	notes->widths.addr = notes->widths.target =
		notes->widths.min_addr = hex_width(symbol__size(sym));
	notes->widths.max_addr = hex_width(sym->end);
	notes->widths.jumps = width_jumps(notes->max_jump_sources);
	notes->widths.max_ins_name = annotation__max_ins_name(notes);
}

void annotation__update_column_widths(struct annotation *notes)
{
	if (annotate_opts.use_offset)
		notes->widths.target = notes->widths.min_addr;
	else if (annotate_opts.full_addr)
		notes->widths.target = BITS_PER_LONG / 4;
	else
		notes->widths.target = notes->widths.max_addr;

	notes->widths.addr = notes->widths.target;

	if (annotate_opts.show_nr_jumps)
		notes->widths.addr += notes->widths.jumps + 1;
}

void annotation__toggle_full_addr(struct annotation *notes, struct map_symbol *ms)
{
	annotate_opts.full_addr = !annotate_opts.full_addr;

	if (annotate_opts.full_addr)
		notes->start = map__objdump_2mem(ms->map, ms->sym->start);
	else
		notes->start = map__rip_2objdump(ms->map, ms->sym->start);

	annotation__update_column_widths(notes);
}

static void annotation__calc_lines(struct annotation *notes, struct map *map,
				   struct rb_root *root)
{
	struct annotation_line *al;
	struct rb_root tmp_root = RB_ROOT;

	list_for_each_entry(al, &notes->src->source, node) {
		double percent_max = 0.0;
		int i;

		for (i = 0; i < al->data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i],
							   annotate_opts.percent_type);

			if (percent > percent_max)
				percent_max = percent;
		}

		if (percent_max <= 0.5)
			continue;

		al->path = get_srcline(map__dso(map), notes->start + al->offset, NULL,
				       false, true, notes->start + al->offset);
		insert_source_line(&tmp_root, al);
	}

	resort_source_line(root, &tmp_root);
}

static void symbol__calc_lines(struct map_symbol *ms, struct rb_root *root)
{
	struct annotation *notes = symbol__annotation(ms->sym);

	annotation__calc_lines(notes, ms->map, root);
}

int symbol__tty_annotate2(struct map_symbol *ms, struct evsel *evsel)
{
	struct dso *dso = map__dso(ms->map);
	struct symbol *sym = ms->sym;
	struct rb_root source_line = RB_ROOT;
	struct hists *hists = evsel__hists(evsel);
	char buf[1024];
	int err;

	err = symbol__annotate2(ms, evsel, NULL);
	if (err) {
		char msg[BUFSIZ];

		dso->annotate_warned = true;
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s:\n%s", sym->name, msg);
		return -1;
	}

	if (annotate_opts.print_lines) {
		srcline_full_filename = annotate_opts.full_path;
		symbol__calc_lines(ms, &source_line);
		print_summary(&source_line, dso->long_name);
	}

	hists__scnprintf_title(hists, buf, sizeof(buf));
	fprintf(stdout, "%s, [percent: %s]\n%s() %s\n",
		buf, percent_type_str(annotate_opts.percent_type), sym->name,
		dso->long_name);
	symbol__annotate_fprintf2(sym, stdout);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

int symbol__tty_annotate(struct map_symbol *ms, struct evsel *evsel)
{
	struct dso *dso = map__dso(ms->map);
	struct symbol *sym = ms->sym;
	struct rb_root source_line = RB_ROOT;
	int err;

	err = symbol__annotate(ms, evsel, NULL);
	if (err) {
		char msg[BUFSIZ];

		dso->annotate_warned = true;
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s:\n%s", sym->name, msg);
		return -1;
	}

	symbol__calc_percent(sym, evsel);

	if (annotate_opts.print_lines) {
		srcline_full_filename = annotate_opts.full_path;
		symbol__calc_lines(ms, &source_line);
		print_summary(&source_line, dso->long_name);
	}

	symbol__annotate_printf(ms, evsel);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

bool ui__has_annotation(void)
{
	return use_browser == 1 && perf_hpp_list.sym;
}


static double annotation_line__max_percent(struct annotation_line *al,
					   struct annotation *notes,
					   unsigned int percent_type)
{
	double percent_max = 0.0;
	int i;

	for (i = 0; i < notes->nr_events; i++) {
		double percent;

		percent = annotation_data__percent(&al->data[i],
						   percent_type);

		if (percent > percent_max)
			percent_max = percent;
	}

	return percent_max;
}

static void disasm_line__write(struct disasm_line *dl, struct annotation *notes,
			       void *obj, char *bf, size_t size,
			       void (*obj__printf)(void *obj, const char *fmt, ...),
			       void (*obj__write_graph)(void *obj, int graph))
{
	if (dl->ins.ops && dl->ins.ops->scnprintf) {
		if (ins__is_jump(&dl->ins)) {
			bool fwd;

			if (dl->ops.target.outside)
				goto call_like;
			fwd = dl->ops.target.offset > dl->al.offset;
			obj__write_graph(obj, fwd ? DARROW_CHAR : UARROW_CHAR);
			obj__printf(obj, " ");
		} else if (ins__is_call(&dl->ins)) {
call_like:
			obj__write_graph(obj, RARROW_CHAR);
			obj__printf(obj, " ");
		} else if (ins__is_ret(&dl->ins)) {
			obj__write_graph(obj, LARROW_CHAR);
			obj__printf(obj, " ");
		} else {
			obj__printf(obj, "  ");
		}
	} else {
		obj__printf(obj, "  ");
	}

	disasm_line__scnprintf(dl, bf, size, !annotate_opts.use_offset, notes->widths.max_ins_name);
}

static void ipc_coverage_string(char *bf, int size, struct annotation *notes)
{
	double ipc = 0.0, coverage = 0.0;
	struct annotated_branch *branch = annotation__get_branch(notes);

	if (branch && branch->hit_cycles)
		ipc = branch->hit_insn / ((double)branch->hit_cycles);

	if (branch && branch->total_insn) {
		coverage = branch->cover_insn * 100.0 /
			((double)branch->total_insn);
	}

	scnprintf(bf, size, "(Average IPC: %.2f, IPC Coverage: %.1f%%)",
		  ipc, coverage);
}

static void __annotation_line__write(struct annotation_line *al, struct annotation *notes,
				     bool first_line, bool current_entry, bool change_color, int width,
				     void *obj, unsigned int percent_type,
				     int  (*obj__set_color)(void *obj, int color),
				     void (*obj__set_percent_color)(void *obj, double percent, bool current),
				     int  (*obj__set_jumps_percent_color)(void *obj, int nr, bool current),
				     void (*obj__printf)(void *obj, const char *fmt, ...),
				     void (*obj__write_graph)(void *obj, int graph))

{
	double percent_max = annotation_line__max_percent(al, notes, percent_type);
	int pcnt_width = annotation__pcnt_width(notes),
	    cycles_width = annotation__cycles_width(notes);
	bool show_title = false;
	char bf[256];
	int printed;

	if (first_line && (al->offset == -1 || percent_max == 0.0)) {
		if (notes->branch && al->cycles) {
			if (al->cycles->ipc == 0.0 && al->cycles->avg == 0)
				show_title = true;
		} else
			show_title = true;
	}

	if (al->offset != -1 && percent_max != 0.0) {
		int i;

		for (i = 0; i < notes->nr_events; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i], percent_type);

			obj__set_percent_color(obj, percent, current_entry);
			if (symbol_conf.show_total_period) {
				obj__printf(obj, "%11" PRIu64 " ", al->data[i].he.period);
			} else if (symbol_conf.show_nr_samples) {
				obj__printf(obj, "%6" PRIu64 " ",
						   al->data[i].he.nr_samples);
			} else {
				obj__printf(obj, "%6.2f ", percent);
			}
		}
	} else {
		obj__set_percent_color(obj, 0, current_entry);

		if (!show_title)
			obj__printf(obj, "%-*s", pcnt_width, " ");
		else {
			obj__printf(obj, "%-*s", pcnt_width,
					   symbol_conf.show_total_period ? "Period" :
					   symbol_conf.show_nr_samples ? "Samples" : "Percent");
		}
	}

	if (notes->branch) {
		if (al->cycles && al->cycles->ipc)
			obj__printf(obj, "%*.2f ", ANNOTATION__IPC_WIDTH - 1, al->cycles->ipc);
		else if (!show_title)
			obj__printf(obj, "%*s", ANNOTATION__IPC_WIDTH, " ");
		else
			obj__printf(obj, "%*s ", ANNOTATION__IPC_WIDTH - 1, "IPC");

		if (!annotate_opts.show_minmax_cycle) {
			if (al->cycles && al->cycles->avg)
				obj__printf(obj, "%*" PRIu64 " ",
					   ANNOTATION__CYCLES_WIDTH - 1, al->cycles->avg);
			else if (!show_title)
				obj__printf(obj, "%*s",
					    ANNOTATION__CYCLES_WIDTH, " ");
			else
				obj__printf(obj, "%*s ",
					    ANNOTATION__CYCLES_WIDTH - 1,
					    "Cycle");
		} else {
			if (al->cycles) {
				char str[32];

				scnprintf(str, sizeof(str),
					"%" PRIu64 "(%" PRIu64 "/%" PRIu64 ")",
					al->cycles->avg, al->cycles->min,
					al->cycles->max);

				obj__printf(obj, "%*s ",
					    ANNOTATION__MINMAX_CYCLES_WIDTH - 1,
					    str);
			} else if (!show_title)
				obj__printf(obj, "%*s",
					    ANNOTATION__MINMAX_CYCLES_WIDTH,
					    " ");
			else
				obj__printf(obj, "%*s ",
					    ANNOTATION__MINMAX_CYCLES_WIDTH - 1,
					    "Cycle(min/max)");
		}

		if (show_title && !*al->line) {
			ipc_coverage_string(bf, sizeof(bf), notes);
			obj__printf(obj, "%*s", ANNOTATION__AVG_IPC_WIDTH, bf);
		}
	}

	obj__printf(obj, " ");

	if (!*al->line)
		obj__printf(obj, "%-*s", width - pcnt_width - cycles_width, " ");
	else if (al->offset == -1) {
		if (al->line_nr && annotate_opts.show_linenr)
			printed = scnprintf(bf, sizeof(bf), "%-*d ", notes->widths.addr + 1, al->line_nr);
		else
			printed = scnprintf(bf, sizeof(bf), "%-*s  ", notes->widths.addr, " ");
		obj__printf(obj, bf);
		obj__printf(obj, "%-*s", width - printed - pcnt_width - cycles_width + 1, al->line);
	} else {
		u64 addr = al->offset;
		int color = -1;

		if (!annotate_opts.use_offset)
			addr += notes->start;

		if (!annotate_opts.use_offset) {
			printed = scnprintf(bf, sizeof(bf), "%" PRIx64 ": ", addr);
		} else {
			if (al->jump_sources &&
			    annotate_opts.offset_level >= ANNOTATION__OFFSET_JUMP_TARGETS) {
				if (annotate_opts.show_nr_jumps) {
					int prev;
					printed = scnprintf(bf, sizeof(bf), "%*d ",
							    notes->widths.jumps,
							    al->jump_sources);
					prev = obj__set_jumps_percent_color(obj, al->jump_sources,
									    current_entry);
					obj__printf(obj, bf);
					obj__set_color(obj, prev);
				}
print_addr:
				printed = scnprintf(bf, sizeof(bf), "%*" PRIx64 ": ",
						    notes->widths.target, addr);
			} else if (ins__is_call(&disasm_line(al)->ins) &&
				   annotate_opts.offset_level >= ANNOTATION__OFFSET_CALL) {
				goto print_addr;
			} else if (annotate_opts.offset_level == ANNOTATION__MAX_OFFSET_LEVEL) {
				goto print_addr;
			} else {
				printed = scnprintf(bf, sizeof(bf), "%-*s  ",
						    notes->widths.addr, " ");
			}
		}

		if (change_color)
			color = obj__set_color(obj, HE_COLORSET_ADDR);
		obj__printf(obj, bf);
		if (change_color)
			obj__set_color(obj, color);

		disasm_line__write(disasm_line(al), notes, obj, bf, sizeof(bf), obj__printf, obj__write_graph);

		obj__printf(obj, "%-*s", width - pcnt_width - cycles_width - 3 - printed, bf);
	}

}

void annotation_line__write(struct annotation_line *al, struct annotation *notes,
			    struct annotation_write_ops *wops)
{
	__annotation_line__write(al, notes, wops->first_line, wops->current_entry,
				 wops->change_color, wops->width, wops->obj,
				 annotate_opts.percent_type,
				 wops->set_color, wops->set_percent_color,
				 wops->set_jumps_percent_color, wops->printf,
				 wops->write_graph);
}

int symbol__annotate2(struct map_symbol *ms, struct evsel *evsel,
		      struct arch **parch)
{
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	size_t size = symbol__size(sym);
	int nr_pcnt = 1, err;

	notes->src->offsets = zalloc(size * sizeof(struct annotation_line *));
	if (notes->src->offsets == NULL)
		return ENOMEM;

	if (evsel__is_group_event(evsel))
		nr_pcnt = evsel->core.nr_members;

	err = symbol__annotate(ms, evsel, parch);
	if (err)
		goto out_free_offsets;

	symbol__calc_percent(sym, evsel);

	annotation__set_offsets(notes, size);
	annotation__mark_jump_targets(notes, sym);

	err = annotation__compute_ipc(notes, size);
	if (err)
		goto out_free_offsets;

	annotation__init_column_widths(notes, sym);
	notes->nr_events = nr_pcnt;

	annotation__update_column_widths(notes);
	sym->annotate2 = 1;

	return 0;

out_free_offsets:
	zfree(&notes->src->offsets);
	return err;
}

static int annotation__config(const char *var, const char *value, void *data)
{
	struct annotation_options *opt = data;

	if (!strstarts(var, "annotate."))
		return 0;

	if (!strcmp(var, "annotate.offset_level")) {
		perf_config_u8(&opt->offset_level, "offset_level", value);

		if (opt->offset_level > ANNOTATION__MAX_OFFSET_LEVEL)
			opt->offset_level = ANNOTATION__MAX_OFFSET_LEVEL;
		else if (opt->offset_level < ANNOTATION__MIN_OFFSET_LEVEL)
			opt->offset_level = ANNOTATION__MIN_OFFSET_LEVEL;
	} else if (!strcmp(var, "annotate.hide_src_code")) {
		opt->hide_src_code = perf_config_bool("hide_src_code", value);
	} else if (!strcmp(var, "annotate.jump_arrows")) {
		opt->jump_arrows = perf_config_bool("jump_arrows", value);
	} else if (!strcmp(var, "annotate.show_linenr")) {
		opt->show_linenr = perf_config_bool("show_linenr", value);
	} else if (!strcmp(var, "annotate.show_nr_jumps")) {
		opt->show_nr_jumps = perf_config_bool("show_nr_jumps", value);
	} else if (!strcmp(var, "annotate.show_nr_samples")) {
		symbol_conf.show_nr_samples = perf_config_bool("show_nr_samples",
								value);
	} else if (!strcmp(var, "annotate.show_total_period")) {
		symbol_conf.show_total_period = perf_config_bool("show_total_period",
								value);
	} else if (!strcmp(var, "annotate.use_offset")) {
		opt->use_offset = perf_config_bool("use_offset", value);
	} else if (!strcmp(var, "annotate.disassembler_style")) {
		opt->disassembler_style = strdup(value);
		if (!opt->disassembler_style) {
			pr_err("Not enough memory for annotate.disassembler_style\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.objdump")) {
		opt->objdump_path = strdup(value);
		if (!opt->objdump_path) {
			pr_err("Not enough memory for annotate.objdump\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.addr2line")) {
		symbol_conf.addr2line_path = strdup(value);
		if (!symbol_conf.addr2line_path) {
			pr_err("Not enough memory for annotate.addr2line\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.demangle")) {
		symbol_conf.demangle = perf_config_bool("demangle", value);
	} else if (!strcmp(var, "annotate.demangle_kernel")) {
		symbol_conf.demangle_kernel = perf_config_bool("demangle_kernel", value);
	} else {
		pr_debug("%s variable unknown, ignoring...", var);
	}

	return 0;
}

void annotation_options__init(void)
{
	struct annotation_options *opt = &annotate_opts;

	memset(opt, 0, sizeof(*opt));

	/* Default values. */
	opt->use_offset = true;
	opt->jump_arrows = true;
	opt->annotate_src = true;
	opt->offset_level = ANNOTATION__OFFSET_JUMP_TARGETS;
	opt->percent_type = PERCENT_PERIOD_LOCAL;
}

void annotation_options__exit(void)
{
	zfree(&annotate_opts.disassembler_style);
	zfree(&annotate_opts.objdump_path);
}

void annotation_config__init(void)
{
	perf_config(annotation__config, &annotate_opts);
}

static unsigned int parse_percent_type(char *str1, char *str2)
{
	unsigned int type = (unsigned int) -1;

	if (!strcmp("period", str1)) {
		if (!strcmp("local", str2))
			type = PERCENT_PERIOD_LOCAL;
		else if (!strcmp("global", str2))
			type = PERCENT_PERIOD_GLOBAL;
	}

	if (!strcmp("hits", str1)) {
		if (!strcmp("local", str2))
			type = PERCENT_HITS_LOCAL;
		else if (!strcmp("global", str2))
			type = PERCENT_HITS_GLOBAL;
	}

	return type;
}

int annotate_parse_percent_type(const struct option *opt __maybe_unused, const char *_str,
				int unset __maybe_unused)
{
	unsigned int type;
	char *str1, *str2;
	int err = -1;

	str1 = strdup(_str);
	if (!str1)
		return -ENOMEM;

	str2 = strchr(str1, '-');
	if (!str2)
		goto out;

	*str2++ = 0;

	type = parse_percent_type(str1, str2);
	if (type == (unsigned int) -1)
		type = parse_percent_type(str2, str1);
	if (type != (unsigned int) -1) {
		annotate_opts.percent_type = type;
		err = 0;
	}

out:
	free(str1);
	return err;
}

int annotate_check_args(void)
{
	struct annotation_options *args = &annotate_opts;

	if (args->prefix_strip && !args->prefix) {
		pr_err("--prefix-strip requires --prefix\n");
		return -1;
	}
	return 0;
}

/*
 * Get register number and access offset from the given instruction.
 * It assumes AT&T x86 asm format like OFFSET(REG).  Maybe it needs
 * to revisit the format when it handles different architecture.
 * Fills @reg and @offset when return 0.
 */
static int extract_reg_offset(struct arch *arch, const char *str,
			      struct annotated_op_loc *op_loc)
{
	char *p;
	char *regname;

	if (arch->objdump.register_char == 0)
		return -1;

	/*
	 * It should start from offset, but it's possible to skip 0
	 * in the asm.  So 0(%rax) should be same as (%rax).
	 *
	 * However, it also start with a segment select register like
	 * %gs:0x18(%rbx).  In that case it should skip the part.
	 */
	if (*str == arch->objdump.register_char) {
		while (*str && !isdigit(*str) &&
		       *str != arch->objdump.memory_ref_char)
			str++;
	}

	op_loc->offset = strtol(str, &p, 0);

	p = strchr(p, arch->objdump.register_char);
	if (p == NULL)
		return -1;

	regname = strdup(p);
	if (regname == NULL)
		return -1;

	op_loc->reg = get_dwarf_regnum(regname, 0);
	free(regname);
	return 0;
}

/**
 * annotate_get_insn_location - Get location of instruction
 * @arch: the architecture info
 * @dl: the target instruction
 * @loc: a buffer to save the data
 *
 * Get detailed location info (register and offset) in the instruction.
 * It needs both source and target operand and whether it accesses a
 * memory location.  The offset field is meaningful only when the
 * corresponding mem flag is set.
 *
 * Some examples on x86:
 *
 *   mov  (%rax), %rcx   # src_reg = rax, src_mem = 1, src_offset = 0
 *                       # dst_reg = rcx, dst_mem = 0
 *
 *   mov  0x18, %r8      # src_reg = -1, dst_reg = r8
 */
int annotate_get_insn_location(struct arch *arch, struct disasm_line *dl,
			       struct annotated_insn_loc *loc)
{
	struct ins_operands *ops;
	struct annotated_op_loc *op_loc;
	int i;

	if (!strcmp(dl->ins.name, "lock"))
		ops = dl->ops.locked.ops;
	else
		ops = &dl->ops;

	if (ops == NULL)
		return -1;

	memset(loc, 0, sizeof(*loc));

	for_each_insn_op_loc(loc, i, op_loc) {
		const char *insn_str = ops->source.raw;

		if (i == INSN_OP_TARGET)
			insn_str = ops->target.raw;

		/* Invalidate the register by default */
		op_loc->reg = -1;

		if (insn_str == NULL)
			continue;

		if (strchr(insn_str, arch->objdump.memory_ref_char)) {
			op_loc->mem_ref = true;
			extract_reg_offset(arch, insn_str, op_loc);
		} else {
			char *s = strdup(insn_str);

			if (s) {
				op_loc->reg = get_dwarf_regnum(s, 0);
				free(s);
			}
		}
	}

	return 0;
}

static void symbol__ensure_annotate(struct map_symbol *ms, struct evsel *evsel)
{
	struct disasm_line *dl, *tmp_dl;
	struct annotation *notes;

	notes = symbol__annotation(ms->sym);
	if (!list_empty(&notes->src->source))
		return;

	if (symbol__annotate(ms, evsel, NULL) < 0)
		return;

	/* remove non-insn disasm lines for simplicity */
	list_for_each_entry_safe(dl, tmp_dl, &notes->src->source, al.node) {
		if (dl->al.offset == -1) {
			list_del(&dl->al.node);
			free(dl);
		}
	}
}

static struct disasm_line *find_disasm_line(struct symbol *sym, u64 ip)
{
	struct disasm_line *dl;
	struct annotation *notes;

	notes = symbol__annotation(sym);

	list_for_each_entry(dl, &notes->src->source, al.node) {
		if (sym->start + dl->al.offset == ip)
			return dl;
	}
	return NULL;
}

static struct annotated_item_stat *annotate_data_stat(struct list_head *head,
						      const char *name)
{
	struct annotated_item_stat *istat;

	list_for_each_entry(istat, head, list) {
		if (!strcmp(istat->name, name))
			return istat;
	}

	istat = zalloc(sizeof(*istat));
	if (istat == NULL)
		return NULL;

	istat->name = strdup(name);
	if (istat->name == NULL) {
		free(istat);
		return NULL;
	}

	list_add_tail(&istat->list, head);
	return istat;
}

/**
 * hist_entry__get_data_type - find data type for given hist entry
 * @he: hist entry
 *
 * This function first annotates the instruction at @he->ip and extracts
 * register and offset info from it.  Then it searches the DWARF debug
 * info to get a variable and type information using the address, register,
 * and offset.
 */
struct annotated_data_type *hist_entry__get_data_type(struct hist_entry *he)
{
	struct map_symbol *ms = &he->ms;
	struct evsel *evsel = hists_to_evsel(he->hists);
	struct arch *arch;
	struct disasm_line *dl;
	struct annotated_insn_loc loc;
	struct annotated_op_loc *op_loc;
	struct annotated_data_type *mem_type;
	struct annotated_item_stat *istat;
	u64 ip = he->ip;
	int i;

	ann_data_stat.total++;

	if (ms->map == NULL || ms->sym == NULL) {
		ann_data_stat.no_sym++;
		return NULL;
	}

	if (!symbol_conf.init_annotation) {
		ann_data_stat.no_sym++;
		return NULL;
	}

	if (evsel__get_arch(evsel, &arch) < 0) {
		ann_data_stat.no_insn++;
		return NULL;
	}

	/* Make sure it runs objdump to get disasm of the function */
	symbol__ensure_annotate(ms, evsel);

	/*
	 * Get a disasm to extract the location from the insn.
	 * This is too slow...
	 */
	dl = find_disasm_line(ms->sym, ip);
	if (dl == NULL) {
		ann_data_stat.no_insn++;
		return NULL;
	}

	istat = annotate_data_stat(&ann_insn_stat, dl->ins.name);
	if (istat == NULL) {
		ann_data_stat.no_insn++;
		return NULL;
	}

	if (annotate_get_insn_location(arch, dl, &loc) < 0) {
		ann_data_stat.no_insn_ops++;
		istat->bad++;
		return NULL;
	}

	for_each_insn_op_loc(&loc, i, op_loc) {
		if (!op_loc->mem_ref)
			continue;

		mem_type = find_data_type(ms, ip, op_loc->reg, op_loc->offset);
		if (mem_type)
			istat->good++;
		else
			istat->bad++;

		if (symbol_conf.annotate_data_sample) {
			annotated_data_type__update_samples(mem_type, evsel,
							    op_loc->offset,
							    he->stat.nr_events,
							    he->stat.period);
		}
		he->mem_type_off = op_loc->offset;
		return mem_type;
	}

	ann_data_stat.no_mem_ops++;
	istat->bad++;
	return NULL;
}
