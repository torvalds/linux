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
#include "build-id.h"
#include "debug.h"
#include "disasm.h"
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

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name);
static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name);

static void ins__sort(struct arch *arch);
static int disasm_line__parse(char *line, const char **namep, char **rawp);

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
		ins_ops__delete(ops->locked.ops);

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

struct ins_ops *ins__find(struct arch *arch, const char *name)
{
	struct ins_ops *ops = __ins__find(arch, name);

	if (!ops && arch->associate_instruction_ops)
		ops = arch->associate_instruction_ops(arch, name);

	return ops;
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
struct disasm_line *disasm_line__new(struct annotate_args *args)
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

#if defined(HAVE_LIBBFD_SUPPORT) && defined(HAVE_LIBBPF_SUPPORT)
#define PACKAGE "perf"
#include <bfd.h>
#include <dis-asm.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <linux/btf.h>
#include <tools/dis-asm-compat.h>

#include "bpf-event.h"
#include "bpf-utils.h"

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

#ifdef HAVE_LIBCAPSTONE_SUPPORT
#include <capstone/capstone.h>

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

static int symbol__disassemble_capstone(char *filename, struct symbol *sym,
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
	cs_insn *insn;
	char disasm_buf[512];
	struct disasm_line *dl;

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

	if (open_capstone_handle(args, is_64bit, &handle) < 0)
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

	count = cs_disasm(handle, buf, len, start, len, &insn);
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
