/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-annotate.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <errno.h>
#include <inttypes.h>
#include "util.h"
#include "ui/ui.h"
#include "sort.h"
#include "build-id.h"
#include "color.h"
#include "config.h"
#include "cache.h"
#include "symbol.h"
#include "units.h"
#include "debug.h"
#include "annotate.h"
#include "evsel.h"
#include "block-range.h"
#include "string2.h"
#include "arch/common.h"
#include <regex.h>
#include <pthread.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

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

#include "sane_ctype.h"

struct annotation_options annotation__default_options = {
	.use_offset     = true,
	.jump_arrows    = true,
};

const char 	*disassembler_style;
const char	*objdump_path;
static regex_t	 file_lineno;

static struct ins_ops *ins__find(struct arch *arch, const char *name);
static void ins__sort(struct arch *arch);
static int disasm_line__parse(char *line, const char **namep, char **rawp);

struct arch {
	const char	*name;
	struct ins	*instructions;
	size_t		nr_instructions;
	size_t		nr_instructions_allocated;
	struct ins_ops  *(*associate_instruction_ops)(struct arch *arch, const char *name);
	bool		sorted_instructions;
	bool		initialized;
	void		*priv;
	unsigned int	model;
	unsigned int	family;
	int		(*init)(struct arch *arch, char *cpuid);
	bool		(*ins_is_fused)(struct arch *arch, const char *ins1,
					const char *ins2);
	struct		{
		char comment_char;
		char skip_functions_char;
	} objdump;
};

static struct ins_ops call_ops;
static struct ins_ops dec_ops;
static struct ins_ops jump_ops;
static struct ins_ops mov_ops;
static struct ins_ops nop_ops;
static struct ins_ops lock_ops;
static struct ins_ops ret_ops;

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

#include "arch/arm/annotate/instructions.c"
#include "arch/arm64/annotate/instructions.c"
#include "arch/x86/annotate/instructions.c"
#include "arch/powerpc/annotate/instructions.c"
#include "arch/s390/annotate/instructions.c"

static struct arch architectures[] = {
	{
		.name = "arm",
		.init = arm__annotate_init,
	},
	{
		.name = "arm64",
		.init = arm64__annotate_init,
	},
	{
		.name = "x86",
		.init = x86__annotate_init,
		.instructions = x86__instructions,
		.nr_instructions = ARRAY_SIZE(x86__instructions),
		.ins_is_fused = x86__ins_is_fused,
		.objdump =  {
			.comment_char = '#',
		},
	},
	{
		.name = "powerpc",
		.init = powerpc__annotate_init,
	},
	{
		.name = "s390",
		.init = s390__annotate_init,
		.objdump =  {
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
			      struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6s %s", ins->name, ops->raw);
}

int ins__scnprintf(struct ins *ins, char *bf, size_t size,
		  struct ins_operands *ops)
{
	if (ins->ops->scnprintf)
		return ins->ops->scnprintf(ins, bf, size, ops);

	return ins__raw_scnprintf(ins, bf, size, ops);
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
		.map = map,
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

	if (map_groups__find_ams(&target) == 0 &&
	    map__rip_2objdump(target.map, map->map_ip(target.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.sym;

	return 0;

indirect_call:
	tok = strchr(endptr, '*');
	if (tok != NULL)
		ops->target.addr = strtoull(tok + 1, NULL, 16);
	goto find_target;
}

static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	if (ops->target.sym)
		return scnprintf(bf, size, "%-6s %s", ins->name, ops->target.sym->name);

	if (ops->target.addr == 0)
		return ins__raw_scnprintf(ins, bf, size, ops);

	if (ops->target.name)
		return scnprintf(bf, size, "%-6s %s", ins->name, ops->target.name);

	return scnprintf(bf, size, "%-6s *%" PRIx64, ins->name, ops->target.addr);
}

static struct ins_ops call_ops = {
	.parse	   = call__parse,
	.scnprintf = call__scnprintf,
};

bool ins__is_call(const struct ins *ins)
{
	return ins->ops == &call_ops || ins->ops == &s390_call_ops;
}

static int jump__parse(struct arch *arch __maybe_unused, struct ins_operands *ops, struct map_symbol *ms)
{
	struct map *map = ms->map;
	struct symbol *sym = ms->sym;
	struct addr_map_symbol target = {
		.map = map,
	};
	const char *c = strchr(ops->raw, ',');
	u64 start, end;
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
			if (c++ != NULL)
				ops->target.addr = strtoull(c, NULL, 16);
		}
	} else {
		ops->target.addr = strtoull(ops->raw, NULL, 16);
	}

	target.addr = map__objdump_2mem(map, ops->target.addr);
	start = map->unmap_ip(map, sym->start),
	end = map->unmap_ip(map, sym->end);

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
	if (map_groups__find_ams(&target) == 0 &&
	    map__rip_2objdump(target.map, map->map_ip(target.map, target.addr)) == ops->target.addr)
		ops->target.sym = target.sym;

	if (!ops->target.outside) {
		ops->target.offset = target.addr - start;
		ops->target.offset_avail = true;
	} else {
		ops->target.offset_avail = false;
	}

	return 0;
}

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	const char *c;

	if (!ops->target.addr || ops->target.offset < 0)
		return ins__raw_scnprintf(ins, bf, size, ops);

	if (ops->target.outside && ops->target.sym != NULL)
		return scnprintf(bf, size, "%-6s %s", ins->name, ops->target.sym->name);

	c = strchr(ops->raw, ',');
	if (c != NULL) {
		const char *c2 = strchr(c + 1, ',');

		/* check for 3-op insn */
		if (c2 != NULL)
			c = c2;
		c++;

		/* mirror arch objdump's space-after-comma style */
		if (*c == ' ')
			c++;
	}

	return scnprintf(bf, size, "%-6s %.*s%" PRIx64,
			 ins->name, c ? c - ops->raw : 0, ops->raw,
			 ops->target.offset);
}

static struct ins_ops jump_ops = {
	.parse	   = jump__parse,
	.scnprintf = jump__scnprintf,
};

bool ins__is_jump(const struct ins *ins)
{
	return ins->ops == &jump_ops;
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
			   struct ins_operands *ops)
{
	int printed;

	if (ops->locked.ins.ops == NULL)
		return ins__raw_scnprintf(ins, bf, size, ops);

	printed = scnprintf(bf, size, "%-6s ", ins->name);
	return printed + ins__scnprintf(&ops->locked.ins, bf + printed,
					size - printed, ops->locked.ops);
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

static int mov__parse(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms __maybe_unused)
{
	char *s = strchr(ops->raw, ','), *target, *comment, prev;

	if (s == NULL)
		return -1;

	*s = '\0';
	ops->source.raw = strdup(ops->raw);
	*s = ',';

	if (ops->source.raw == NULL)
		return -1;

	target = ++s;
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

	if (comment == NULL)
		return 0;

	comment = ltrim(comment);
	comment__symbol(ops->source.raw, comment + 1, &ops->source.addr, &ops->source.name);
	comment__symbol(ops->target.raw, comment + 1, &ops->target.addr, &ops->target.name);

	return 0;

out_free_source:
	zfree(&ops->source.raw);
	return -1;
}

static int mov__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6s %s,%s", ins->name,
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

	comment = ltrim(comment);
	comment__symbol(ops->target.raw, comment + 1, &ops->target.addr, &ops->target.name);

	return 0;
}

static int dec__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6s %s", ins->name,
			 ops->target.name ?: ops->target.raw);
}

static struct ins_ops dec_ops = {
	.parse	   = dec__parse,
	.scnprintf = dec__scnprintf,
};

static int nop__scnprintf(struct ins *ins __maybe_unused, char *bf, size_t size,
			  struct ins_operands *ops __maybe_unused)
{
	return scnprintf(bf, size, "%-6s", "nop");
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

int symbol__alloc_hist(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	size_t size = symbol__size(sym);
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
	if (sizeof_sym_hist > (SIZE_MAX - sizeof(*notes->src))
				/ symbol_conf.nr_events)
		return -1;

	notes->src = zalloc(sizeof(*notes->src) + symbol_conf.nr_events * sizeof_sym_hist);
	if (notes->src == NULL)
		return -1;
	notes->src->sizeof_sym_hist = sizeof_sym_hist;
	notes->src->nr_histograms   = symbol_conf.nr_events;
	INIT_LIST_HEAD(&notes->src->source);
	return 0;
}

/* The cycles histogram is lazily allocated. */
static int symbol__alloc_hist_cycles(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	const size_t size = symbol__size(sym);

	notes->src->cycles_hist = calloc(size, sizeof(struct cyc_hist));
	if (notes->src->cycles_hist == NULL)
		return -1;
	return 0;
}

void symbol__annotate_zero_histograms(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);

	pthread_mutex_lock(&notes->lock);
	if (notes->src != NULL) {
		memset(notes->src->histograms, 0,
		       notes->src->nr_histograms * notes->src->sizeof_sym_hist);
		if (notes->src->cycles_hist)
			memset(notes->src->cycles_hist, 0,
				symbol__size(sym) * sizeof(struct cyc_hist));
	}
	pthread_mutex_unlock(&notes->lock);
}

static int __symbol__account_cycles(struct annotation *notes,
				    u64 start,
				    unsigned offset, unsigned cycles,
				    unsigned have_start)
{
	struct cyc_hist *ch;

	ch = notes->src->cycles_hist;
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
	ch[offset].have_start = have_start;
	ch[offset].start = start;
	ch[offset].cycles += cycles;
	ch[offset].num++;
	return 0;
}

static int __symbol__inc_addr_samples(struct symbol *sym, struct map *map,
				      struct annotation *notes, int evidx, u64 addr,
				      struct perf_sample *sample)
{
	unsigned offset;
	struct sym_hist *h;

	pr_debug3("%s: addr=%#" PRIx64 "\n", __func__, map->unmap_ip(map, addr));

	if ((addr < sym->start || addr >= sym->end) &&
	    (addr != sym->end || sym->start != sym->end)) {
		pr_debug("%s(%d): ERANGE! sym->name=%s, start=%#" PRIx64 ", addr=%#" PRIx64 ", end=%#" PRIx64 "\n",
		       __func__, __LINE__, sym->name, sym->start, addr, sym->end);
		return -ERANGE;
	}

	offset = addr - sym->start;
	h = annotation__histogram(notes, evidx);
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

static struct annotation *symbol__get_annotation(struct symbol *sym, bool cycles)
{
	struct annotation *notes = symbol__annotation(sym);

	if (notes->src == NULL) {
		if (symbol__alloc_hist(sym) < 0)
			return NULL;
	}
	if (!notes->src->cycles_hist && cycles) {
		if (symbol__alloc_hist_cycles(sym) < 0)
			return NULL;
	}
	return notes;
}

static int symbol__inc_addr_samples(struct symbol *sym, struct map *map,
				    int evidx, u64 addr,
				    struct perf_sample *sample)
{
	struct annotation *notes;

	if (sym == NULL)
		return 0;
	notes = symbol__get_annotation(sym, false);
	if (notes == NULL)
		return -ENOMEM;
	return __symbol__inc_addr_samples(sym, map, notes, evidx, addr, sample);
}

static int symbol__account_cycles(u64 addr, u64 start,
				  struct symbol *sym, unsigned cycles)
{
	struct annotation *notes;
	unsigned offset;

	if (sym == NULL)
		return 0;
	notes = symbol__get_annotation(sym, true);
	if (notes == NULL)
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
	return __symbol__account_cycles(notes,
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
		(start->sym == ams->sym ||
		 (ams->sym &&
		   start->addr == ams->sym->start + ams->map->start)))
		saddr = start->al_addr;
	if (saddr == 0)
		pr_debug2("BB with bad start: addr %"PRIx64" start %"PRIx64" sym %"PRIx64" saddr %"PRIx64"\n",
			ams->addr,
			start ? start->addr : 0,
			ams->sym ? ams->sym->start + ams->map->start : 0,
			saddr);
	err = symbol__account_cycles(ams->al_addr, saddr, ams->sym, cycles);
	if (err)
		pr_debug2("account_cycles failed %d\n", err);
	return err;
}

static unsigned annotation__count_insn(struct annotation *notes, u64 start, u64 end)
{
	unsigned n_insn = 0;
	u64 offset;

	for (offset = start; offset <= end; offset++) {
		if (notes->offsets[offset])
			n_insn++;
	}
	return n_insn;
}

static void annotation__count_and_fill(struct annotation *notes, u64 start, u64 end, struct cyc_hist *ch)
{
	unsigned n_insn;
	u64 offset;

	n_insn = annotation__count_insn(notes, start, end);
	if (n_insn && ch->num && ch->cycles) {
		float ipc = n_insn / ((double)ch->cycles / (double)ch->num);

		/* Hide data when there are too many overlaps. */
		if (ch->reset >= 0x7fff || ch->reset >= ch->num / 2)
			return;

		for (offset = start; offset <= end; offset++) {
			struct annotation_line *al = notes->offsets[offset];

			if (al)
				al->ipc = ipc;
		}
	}
}

void annotation__compute_ipc(struct annotation *notes, size_t size)
{
	u64 offset;

	if (!notes->src || !notes->src->cycles_hist)
		return;

	pthread_mutex_lock(&notes->lock);
	for (offset = 0; offset < size; ++offset) {
		struct cyc_hist *ch;

		ch = &notes->src->cycles_hist[offset];
		if (ch && ch->cycles) {
			struct annotation_line *al;

			if (ch->have_start)
				annotation__count_and_fill(notes, ch->start, offset, ch);
			al = notes->offsets[offset];
			if (al && ch->num_aggr)
				al->cycles = ch->cycles_aggr / ch->num_aggr;
			notes->have_cycles = true;
		}
	}
	pthread_mutex_unlock(&notes->lock);
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 int evidx)
{
	return symbol__inc_addr_samples(ams->sym, ams->map, evidx, ams->al_addr, sample);
}

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 int evidx, u64 ip)
{
	return symbol__inc_addr_samples(he->ms.sym, he->ms.map, evidx, ip, sample);
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
	char tmp, *name = ltrim(line);

	if (name[0] == '\0')
		return -1;

	*rawp = name + 1;

	while ((*rawp)[0] != '\0' && !isspace((*rawp)[0]))
		++*rawp;

	tmp = (*rawp)[0];
	(*rawp)[0] = '\0';
	*namep = strdup(name);

	if (*namep == NULL)
		goto out_free_name;

	(*rawp)[0] = tmp;
	*rawp = ltrim(*rawp);

	return 0;

out_free_name:
	free((void *)namep);
	*namep = NULL;
	return -1;
}

struct annotate_args {
	size_t			 privsize;
	struct arch		*arch;
	struct map_symbol	 ms;
	struct perf_evsel	*evsel;
	s64			 offset;
	char			*line;
	int			 line_nr;
};

static void annotation_line__delete(struct annotation_line *al)
{
	void *ptr = (void *) al - al->privsize;

	free_srcline(al->path);
	zfree(&al->line);
	free(ptr);
}

/*
 * Allocating the annotation line data with following
 * structure:
 *
 *    --------------------------------------
 *    private space | struct annotation_line
 *    --------------------------------------
 *
 * Size of the private space is stored in 'struct annotation_line'.
 *
 */
static struct annotation_line *
annotation_line__new(struct annotate_args *args, size_t privsize)
{
	struct annotation_line *al;
	struct perf_evsel *evsel = args->evsel;
	size_t size = privsize + sizeof(*al);
	int nr = 1;

	if (perf_evsel__is_group_event(evsel))
		nr = evsel->nr_members;

	size += sizeof(al->samples[0]) * nr;

	al = zalloc(size);
	if (al) {
		al = (void *) al + privsize;
		al->privsize   = privsize;
		al->offset     = args->offset;
		al->line       = strdup(args->line);
		al->line_nr    = args->line_nr;
		al->samples_nr = nr;
	}

	return al;
}

/*
 * Allocating the disasm annotation line data with
 * following structure:
 *
 *    ------------------------------------------------------------
 *    privsize space | struct disasm_line | struct annotation_line
 *    ------------------------------------------------------------
 *
 * We have 'struct annotation_line' member as last member
 * of 'struct disasm_line' to have an easy access.
 *
 */
static struct disasm_line *disasm_line__new(struct annotate_args *args)
{
	struct disasm_line *dl = NULL;
	struct annotation_line *al;
	size_t privsize = args->privsize + offsetof(struct disasm_line, al);

	al = annotation_line__new(args, privsize);
	if (al != NULL) {
		dl = disasm_line(al);

		if (dl->al.line == NULL)
			goto out_delete;

		if (args->offset != -1) {
			if (disasm_line__parse(dl->al.line, &dl->ins.name, &dl->ops.raw) < 0)
				goto out_free_line;

			disasm_line__init_ins(dl, args->arch, &args->ms);
		}
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
	free((void *)dl->ins.name);
	dl->ins.name = NULL;
	annotation_line__delete(&dl->al);
}

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw)
{
	if (raw || !dl->ins.ops)
		return scnprintf(bf, size, "%-6s %s", dl->ins.name, dl->ops.raw);

	return ins__scnprintf(&dl->ins, bf, size, &dl->ops);
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
		       struct perf_evsel *evsel, u64 len, int min_pcnt, int printed,
		       int max_lines, struct annotation_line *queue, int addr_fmt_width)
{
	struct disasm_line *dl = container_of(al, struct disasm_line, al);
	static const char *prev_line;
	static const char *prev_color;

	if (al->offset != -1) {
		double max_percent = 0.0;
		int i, nr_percent = 1;
		const char *color;
		struct annotation *notes = symbol__annotation(sym);

		for (i = 0; i < al->samples_nr; i++) {
			struct annotation_data *sample = &al->samples[i];

			if (sample->percent > max_percent)
				max_percent = sample->percent;
		}

		if (max_percent < min_pcnt)
			return -1;

		if (max_lines && printed >= max_lines)
			return 1;

		if (queue != NULL) {
			list_for_each_entry_from(queue, &notes->src->source, node) {
				if (queue == al)
					break;
				annotation_line__print(queue, sym, start, evsel, len,
						       0, 0, 1, NULL, addr_fmt_width);
			}
		}

		color = get_percent_color(max_percent);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored addr with the same filename:line
		 */
		if (al->path) {
			if (!prev_line || strcmp(prev_line, al->path)
				       || color != prev_color) {
				color_fprintf(stdout, color, " %s", al->path);
				prev_line = al->path;
				prev_color = color;
			}
		}

		for (i = 0; i < nr_percent; i++) {
			struct annotation_data *sample = &al->samples[i];

			color = get_percent_color(sample->percent);

			if (symbol_conf.show_total_period)
				color_fprintf(stdout, color, " %11" PRIu64,
					      sample->he.period);
			else if (symbol_conf.show_nr_samples)
				color_fprintf(stdout, color, " %7" PRIu64,
					      sample->he.nr_samples);
			else
				color_fprintf(stdout, color, " %7.2f", sample->percent);
		}

		printf(" : ");

		disasm_line__print(dl, start, addr_fmt_width);
		printf("\n");
	} else if (max_lines && printed >= max_lines)
		return 1;
	else {
		int width = symbol_conf.show_total_period ? 12 : 8;

		if (queue)
			return -1;

		if (perf_evsel__is_group_event(evsel))
			width *= evsel->nr_members;

		if (!*al->line)
			printf(" %*s:\n", width, " ");
		else
			printf(" %*s:     %*s %s\n", width, " ", addr_fmt_width, " ", al->line);
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
static int symbol__parse_objdump_line(struct symbol *sym, FILE *file,
				      struct annotate_args *args,
				      int *line_nr)
{
	struct map *map = args->ms.map;
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *dl;
	char *line = NULL, *parsed_line, *tmp, *tmp2;
	size_t line_len;
	s64 line_ip, offset = -1;
	regmatch_t match[2];

	if (getline(&line, &line_len, file) < 0)
		return -1;

	if (!line)
		return -1;

	line_ip = -1;
	parsed_line = rtrim(line);

	/* /filename:linenr ? Save line number and ignore. */
	if (regexec(&file_lineno, parsed_line, 2, match, 0) == 0) {
		*line_nr = atoi(parsed_line + match[1].rm_so);
		return 0;
	}

	tmp = ltrim(parsed_line);
	if (*tmp) {
		/*
		 * Parse hexa addresses followed by ':'
		 */
		line_ip = strtoull(tmp, &tmp2, 16);
		if (*tmp2 != ':' || tmp == tmp2 || tmp2[1] == '\0')
			line_ip = -1;
	}

	if (line_ip != -1) {
		u64 start = map__rip_2objdump(map, sym->start),
		    end = map__rip_2objdump(map, sym->end);

		offset = line_ip - start;
		if ((u64)line_ip < start || (u64)line_ip >= end)
			offset = -1;
		else
			parsed_line = tmp2 + 1;
	}

	args->offset  = offset;
	args->line    = parsed_line;
	args->line_nr = *line_nr;
	args->ms.sym  = sym;

	dl = disasm_line__new(args);
	free(line);
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
			.map = map,
			.addr = dl->ops.target.addr,
		};

		if (!map_groups__find_ams(&target) &&
		    target.sym->start == target.al_addr)
			dl->ops.target.sym = target.sym;
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

		list_del(&dl->al.node);
		disasm_line__free(dl);
	}
}

int symbol__strerror_disassemble(struct symbol *sym __maybe_unused, struct map *map,
			      int errnum, char *buf, size_t buflen)
{
	struct dso *dso = map->dso;

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
			build_id__sprintf(dso->build_id,
					  sizeof(dso->build_id), bf + 15);
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
		return -1;

	/*
	 * old style build-id cache has name of XX/XXXXXXX.. while
	 * new style has XX/XXXXXXX../{elf,kallsyms,vdso}.
	 * extract the build-id part of dirname in the new style only.
	 */
	pos = strrchr(build_id_path, '/');
	if (pos && strlen(pos) < SBUILD_ID_SIZE - 2)
		dirname(build_id_path);

	if (dso__is_kcore(dso) ||
	    readlink(build_id_path, linkname, sizeof(linkname)) < 0 ||
	    strstr(linkname, DSO__NAME_KALLSYMS) ||
	    access(filename, R_OK)) {
fallback:
		/*
		 * If we don't have build-ids or the build-id file isn't in the
		 * cache, or is just a kallsyms file, well, lets hope that this
		 * DSO is the same as when 'perf record' ran.
		 */
		__symbol__join_symfs(filename, filename_size, dso->long_name);
	}

	free(build_id_path);
	return 0;
}

static int symbol__disassemble(struct symbol *sym, struct annotate_args *args)
{
	struct map *map = args->ms.map;
	struct dso *dso = map->dso;
	char *command;
	FILE *file;
	char symfs_filename[PATH_MAX];
	struct kcore_extract kce;
	bool delete_extract = false;
	int stdout_fd[2];
	int lineno = 0;
	int nline;
	pid_t pid;
	int err = dso__disassemble_filename(dso, symfs_filename, sizeof(symfs_filename));

	if (err)
		return err;

	pr_debug("%s: filename=%s, sym=%s, start=%#" PRIx64 ", end=%#" PRIx64 "\n", __func__,
		 symfs_filename, sym->name, map->unmap_ip(map, sym->start),
		 map->unmap_ip(map, sym->end));

	pr_debug("annotating [%p] %30s : [%p] %30s\n",
		 dso, dso->long_name, sym, sym->name);

	if (dso__is_kcore(dso)) {
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
			goto out;

		strcpy(symfs_filename, tmp);
	}

	err = asprintf(&command,
		 "%s %s%s --start-address=0x%016" PRIx64
		 " --stop-address=0x%016" PRIx64
		 " -l -d %s %s -C \"%s\" 2>/dev/null|grep -v \"%s:\"|expand",
		 objdump_path ? objdump_path : "objdump",
		 disassembler_style ? "-M " : "",
		 disassembler_style ? disassembler_style : "",
		 map__rip_2objdump(map, sym->start),
		 map__rip_2objdump(map, sym->end),
		 symbol_conf.annotate_asm_raw ? "" : "--no-show-raw",
		 symbol_conf.annotate_src ? "-S" : "",
		 symfs_filename, symfs_filename);

	if (err < 0) {
		pr_err("Failure allocating memory for the command to run\n");
		goto out_remove_tmp;
	}

	pr_debug("Executing: %s\n", command);

	err = -1;
	if (pipe(stdout_fd) < 0) {
		pr_err("Failure creating the pipe to run %s\n", command);
		goto out_free_command;
	}

	pid = fork();
	if (pid < 0) {
		pr_err("Failure forking to run %s\n", command);
		goto out_close_stdout;
	}

	if (pid == 0) {
		close(stdout_fd[0]);
		dup2(stdout_fd[1], 1);
		close(stdout_fd[1]);
		execl("/bin/sh", "sh", "-c", command, NULL);
		perror(command);
		exit(-1);
	}

	close(stdout_fd[1]);

	file = fdopen(stdout_fd[0], "r");
	if (!file) {
		pr_err("Failure creating FILE stream for %s\n", command);
		/*
		 * If we were using debug info should retry with
		 * original binary.
		 */
		goto out_free_command;
	}

	nline = 0;
	while (!feof(file)) {
		/*
		 * The source code line number (lineno) needs to be kept in
		 * accross calls to symbol__parse_objdump_line(), so that it
		 * can associate it with the instructions till the next one.
		 * See disasm_line__new() and struct disasm_line::line_nr.
		 */
		if (symbol__parse_objdump_line(sym, file, args, &lineno) < 0)
			break;
		nline++;
	}

	if (nline == 0)
		pr_err("No output from %s\n", command);

	/*
	 * kallsyms does not have symbol sizes so there may a nop at the end.
	 * Remove it.
	 */
	if (dso__is_kcore(dso))
		delete_last_nop(sym);

	fclose(file);
	err = 0;
out_free_command:
	free(command);
out_remove_tmp:
	close(stdout_fd[0]);

	if (dso__needs_decompress(dso))
		unlink(symfs_filename);

	if (delete_extract)
		kcore_extract__delete(&kce);
out:
	return err;

out_close_stdout:
	close(stdout_fd[1]);
	goto out_free_command;
}

static void calc_percent(struct sym_hist *hist,
			 struct annotation_data *sample,
			 s64 offset, s64 end)
{
	unsigned int hits = 0;
	u64 period = 0;

	while (offset < end) {
		hits   += hist->addr[offset].nr_samples;
		period += hist->addr[offset].period;
		++offset;
	}

	if (hist->nr_samples) {
		sample->he.period     = period;
		sample->he.nr_samples = hits;
		sample->percent = 100.0 * hits / hist->nr_samples;
	}
}

static void annotation__calc_percent(struct annotation *notes,
				     struct perf_evsel *evsel, s64 len)
{
	struct annotation_line *al, *next;

	list_for_each_entry(al, &notes->src->source, node) {
		s64 end;
		int i;

		if (al->offset == -1)
			continue;

		next = annotation_line__next(al, &notes->src->source);
		end  = next ? next->offset : len;

		for (i = 0; i < al->samples_nr; i++) {
			struct annotation_data *sample;
			struct sym_hist *hist;

			hist   = annotation__histogram(notes, evsel->idx + i);
			sample = &al->samples[i];

			calc_percent(hist, sample, al->offset, end);
		}
	}
}

void symbol__calc_percent(struct symbol *sym, struct perf_evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__calc_percent(notes, evsel, symbol__size(sym));
}

int symbol__annotate(struct symbol *sym, struct map *map,
		     struct perf_evsel *evsel, size_t privsize,
		     struct arch **parch)
{
	struct annotate_args args = {
		.privsize	= privsize,
		.evsel		= evsel,
	};
	struct perf_env *env = perf_evsel__env(evsel);
	const char *arch_name = perf_env__arch(env);
	struct arch *arch;
	int err;

	if (!arch_name)
		return -1;

	args.arch = arch = arch__find(arch_name);
	if (arch == NULL)
		return -ENOTSUP;

	if (parch)
		*parch = arch;

	if (arch->init) {
		err = arch->init(arch, env ? env->cpuid : NULL);
		if (err) {
			pr_err("%s: failed to initialize %s arch priv area\n", __func__, arch->name);
			return err;
		}
	}

	args.ms.map = map;
	args.ms.sym = sym;

	return symbol__disassemble(sym, &args);
}

static void insert_source_line(struct rb_root *root, struct annotation_line *al)
{
	struct annotation_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	int i, ret;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct annotation_line, rb_node);

		ret = strcmp(iter->path, al->path);
		if (ret == 0) {
			for (i = 0; i < al->samples_nr; i++)
				iter->samples[i].percent_sum += al->samples[i].percent;
			return;
		}

		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	for (i = 0; i < al->samples_nr; i++)
		al->samples[i].percent_sum = al->samples[i].percent;

	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static int cmp_source_line(struct annotation_line *a, struct annotation_line *b)
{
	int i;

	for (i = 0; i < a->samples_nr; i++) {
		if (a->samples[i].percent_sum == b->samples[i].percent_sum)
			continue;
		return a->samples[i].percent_sum > b->samples[i].percent_sum;
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
		for (i = 0; i < al->samples_nr; i++) {
			percent = al->samples[i].percent_sum;
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

static void symbol__annotate_hits(struct symbol *sym, struct perf_evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel->idx);
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

int symbol__annotate_printf(struct symbol *sym, struct map *map,
			    struct perf_evsel *evsel, bool full_paths,
			    int min_pcnt, int max_lines, int context)
{
	struct dso *dso = map->dso;
	char *filename;
	const char *d_filename;
	const char *evsel_name = perf_evsel__name(evsel);
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel->idx);
	struct annotation_line *pos, *queue = NULL;
	u64 start = map__rip_2objdump(map, sym->start);
	int printed = 2, queue_len = 0, addr_fmt_width;
	int more = 0;
	u64 len;
	int width = symbol_conf.show_total_period ? 12 : 8;
	int graph_dotted_len;

	filename = strdup(dso->long_name);
	if (!filename)
		return -ENOMEM;

	if (full_paths)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = symbol__size(sym);

	if (perf_evsel__is_group_event(evsel))
		width *= evsel->nr_members;

	graph_dotted_len = printf(" %-*.*s|	Source code & Disassembly of %s for %s (%" PRIu64 " samples)\n",
				  width, width, symbol_conf.show_total_period ? "Period" :
				  symbol_conf.show_nr_samples ? "Samples" : "Percent",
				  d_filename, evsel_name, h->nr_samples);

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
					     min_pcnt, printed, max_lines,
					     queue, addr_fmt_width);

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

int symbol__annotate_fprintf2(struct symbol *sym, FILE *fp)
{
	struct annotation *notes = symbol__annotation(sym);
	struct annotation_write_ops ops = {
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
		if (annotation_line__filter(al, notes))
			continue;
		annotation_line__write(al, notes, &ops);
		fputc('\n', fp);
		ops.first_line = false;
	}

	return 0;
}

int map_symbol__annotation_dump(struct map_symbol *ms, struct perf_evsel *evsel)
{
	const char *ev_name = perf_evsel__name(evsel);
	char buf[1024];
	char *filename;
	int err = -1;
	FILE *fp;

	if (asprintf(&filename, "%s.annotation", ms->sym->name) < 0)
		return -1;

	fp = fopen(filename, "w");
	if (fp == NULL)
		goto out_free_filename;

	if (perf_evsel__is_group_event(evsel)) {
		perf_evsel__group_desc(evsel, buf, sizeof(buf));
		ev_name = buf;
	}

	fprintf(fp, "%s() %s\nEvent: %s\n\n",
		ms->sym->name, ms->map->dso->long_name, ev_name);
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
		list_del(&al->node);
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
		struct annotation_line *al = notes->offsets[offset];
		struct disasm_line *dl;

		dl = disasm_line(al);

		if (!disasm_line__is_valid_local_jump(dl, sym))
			continue;

		al = notes->offsets[dl->ops.target.offset];

		/*
		 * FIXME: Oops, no jump target? Buggy disassembler? Or do we
		 * have to adjust to the previous offset?
		 */
		if (al == NULL)
			continue;

		if (++al->jump_sources > notes->max_jump_sources)
			notes->max_jump_sources = al->jump_sources;

		++notes->nr_jumps;
	}
}

void annotation__set_offsets(struct annotation *notes, s64 size)
{
	struct annotation_line *al;

	notes->max_line_len = 0;

	list_for_each_entry(al, &notes->src->source, node) {
		size_t line_len = strlen(al->line);

		if (notes->max_line_len < line_len)
			notes->max_line_len = line_len;
		al->idx = notes->nr_entries++;
		if (al->offset != -1) {
			al->idx_asm = notes->nr_asm_entries++;
			/*
			 * FIXME: short term bandaid to cope with assembly
			 * routines that comes with labels in the same column
			 * as the address in objdump, sigh.
			 *
			 * E.g. copy_user_generic_unrolled
 			 */
			if (al->offset < size)
				notes->offsets[al->offset] = al;
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

void annotation__init_column_widths(struct annotation *notes, struct symbol *sym)
{
	notes->widths.addr = notes->widths.target =
		notes->widths.min_addr = hex_width(symbol__size(sym));
	notes->widths.max_addr = hex_width(sym->end);
	notes->widths.jumps = width_jumps(notes->max_jump_sources);
}

void annotation__update_column_widths(struct annotation *notes)
{
	if (notes->options->use_offset)
		notes->widths.target = notes->widths.min_addr;
	else
		notes->widths.target = notes->widths.max_addr;

	notes->widths.addr = notes->widths.target;

	if (notes->options->show_nr_jumps)
		notes->widths.addr += notes->widths.jumps + 1;
}

static void annotation__calc_lines(struct annotation *notes, struct map *map,
				  struct rb_root *root)
{
	struct annotation_line *al;
	struct rb_root tmp_root = RB_ROOT;

	list_for_each_entry(al, &notes->src->source, node) {
		double percent_max = 0.0;
		int i;

		for (i = 0; i < al->samples_nr; i++) {
			struct annotation_data *sample;

			sample = &al->samples[i];

			if (sample->percent > percent_max)
				percent_max = sample->percent;
		}

		if (percent_max <= 0.5)
			continue;

		al->path = get_srcline(map->dso, notes->start + al->offset, NULL,
				       false, true, notes->start + al->offset);
		insert_source_line(&tmp_root, al);
	}

	resort_source_line(root, &tmp_root);
}

static void symbol__calc_lines(struct symbol *sym, struct map *map,
			      struct rb_root *root)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__calc_lines(notes, map, root);
}

int symbol__tty_annotate2(struct symbol *sym, struct map *map,
			  struct perf_evsel *evsel, bool print_lines,
			  bool full_paths)
{
	struct dso *dso = map->dso;
	struct rb_root source_line = RB_ROOT;
	struct annotation_options opts = annotation__default_options;
	struct annotation *notes = symbol__annotation(sym);
	char buf[1024];

	if (symbol__annotate2(sym, map, evsel, &opts, NULL) < 0)
		return -1;

	if (print_lines) {
		srcline_full_filename = full_paths;
		symbol__calc_lines(sym, map, &source_line);
		print_summary(&source_line, dso->long_name);
	}

	annotation__scnprintf_samples_period(notes, buf, sizeof(buf), evsel);
	fprintf(stdout, "%s\n%s() %s\n", buf, sym->name, dso->long_name);
	symbol__annotate_fprintf2(sym, stdout);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel, bool print_lines,
			 bool full_paths, int min_pcnt, int max_lines)
{
	struct dso *dso = map->dso;
	struct rb_root source_line = RB_ROOT;

	if (symbol__annotate(sym, map, evsel, 0, NULL) < 0)
		return -1;

	symbol__calc_percent(sym, evsel);

	if (print_lines) {
		srcline_full_filename = full_paths;
		symbol__calc_lines(sym, map, &source_line);
		print_summary(&source_line, dso->long_name);
	}

	symbol__annotate_printf(sym, map, evsel, full_paths,
				min_pcnt, max_lines, 0);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

bool ui__has_annotation(void)
{
	return use_browser == 1 && perf_hpp_list.sym;
}


double annotation_line__max_percent(struct annotation_line *al, struct annotation *notes)
{
	double percent_max = 0.0;
	int i;

	for (i = 0; i < notes->nr_events; i++) {
		if (al->samples[i].percent > percent_max)
			percent_max = al->samples[i].percent;
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

	disasm_line__scnprintf(dl, bf, size, !notes->options->use_offset);
}

static void __annotation_line__write(struct annotation_line *al, struct annotation *notes,
				     bool first_line, bool current_entry, bool change_color, int width,
				     void *obj,
				     int  (*obj__set_color)(void *obj, int color),
				     void (*obj__set_percent_color)(void *obj, double percent, bool current),
				     int  (*obj__set_jumps_percent_color)(void *obj, int nr, bool current),
				     void (*obj__printf)(void *obj, const char *fmt, ...),
				     void (*obj__write_graph)(void *obj, int graph))

{
	double percent_max = annotation_line__max_percent(al, notes);
	int pcnt_width = annotation__pcnt_width(notes),
	    cycles_width = annotation__cycles_width(notes);
	bool show_title = false;
	char bf[256];
	int printed;

	if (first_line && (al->offset == -1 || percent_max == 0.0)) {
		if (notes->have_cycles) {
			if (al->ipc == 0.0 && al->cycles == 0)
				show_title = true;
		} else
			show_title = true;
	}

	if (al->offset != -1 && percent_max != 0.0) {
		int i;

		for (i = 0; i < notes->nr_events; i++) {
			obj__set_percent_color(obj, al->samples[i].percent, current_entry);
			if (notes->options->show_total_period) {
				obj__printf(obj, "%11" PRIu64 " ", al->samples[i].he.period);
			} else if (notes->options->show_nr_samples) {
				obj__printf(obj, "%6" PRIu64 " ",
						   al->samples[i].he.nr_samples);
			} else {
				obj__printf(obj, "%6.2f ",
						   al->samples[i].percent);
			}
		}
	} else {
		obj__set_percent_color(obj, 0, current_entry);

		if (!show_title)
			obj__printf(obj, "%-*s", pcnt_width, " ");
		else {
			obj__printf(obj, "%-*s", pcnt_width,
					   notes->options->show_total_period ? "Period" :
					   notes->options->show_nr_samples ? "Samples" : "Percent");
		}
	}

	if (notes->have_cycles) {
		if (al->ipc)
			obj__printf(obj, "%*.2f ", ANNOTATION__IPC_WIDTH - 1, al->ipc);
		else if (!show_title)
			obj__printf(obj, "%*s", ANNOTATION__IPC_WIDTH, " ");
		else
			obj__printf(obj, "%*s ", ANNOTATION__IPC_WIDTH - 1, "IPC");

		if (al->cycles)
			obj__printf(obj, "%*" PRIu64 " ",
					   ANNOTATION__CYCLES_WIDTH - 1, al->cycles);
		else if (!show_title)
			obj__printf(obj, "%*s", ANNOTATION__CYCLES_WIDTH, " ");
		else
			obj__printf(obj, "%*s ", ANNOTATION__CYCLES_WIDTH - 1, "Cycle");
	}

	obj__printf(obj, " ");

	if (!*al->line)
		obj__printf(obj, "%-*s", width - pcnt_width - cycles_width, " ");
	else if (al->offset == -1) {
		if (al->line_nr && notes->options->show_linenr)
			printed = scnprintf(bf, sizeof(bf), "%-*d ", notes->widths.addr + 1, al->line_nr);
		else
			printed = scnprintf(bf, sizeof(bf), "%-*s  ", notes->widths.addr, " ");
		obj__printf(obj, bf);
		obj__printf(obj, "%-*s", width - printed - pcnt_width - cycles_width + 1, al->line);
	} else {
		u64 addr = al->offset;
		int color = -1;

		if (!notes->options->use_offset)
			addr += notes->start;

		if (!notes->options->use_offset) {
			printed = scnprintf(bf, sizeof(bf), "%" PRIx64 ": ", addr);
		} else {
			if (al->jump_sources) {
				if (notes->options->show_nr_jumps) {
					int prev;
					printed = scnprintf(bf, sizeof(bf), "%*d ",
							    notes->widths.jumps,
							    al->jump_sources);
					prev = obj__set_jumps_percent_color(obj, al->jump_sources,
									    current_entry);
					obj__printf(obj, bf);
					obj__set_color(obj, prev);
				}

				printed = scnprintf(bf, sizeof(bf), "%*" PRIx64 ": ",
						    notes->widths.target, addr);
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
			    struct annotation_write_ops *ops)
{
	__annotation_line__write(al, notes, ops->first_line, ops->current_entry,
				 ops->change_color, ops->width, ops->obj,
				 ops->set_color, ops->set_percent_color,
				 ops->set_jumps_percent_color, ops->printf,
				 ops->write_graph);
}

int symbol__annotate2(struct symbol *sym, struct map *map, struct perf_evsel *evsel,
		      struct annotation_options *options, struct arch **parch)
{
	struct annotation *notes = symbol__annotation(sym);
	size_t size = symbol__size(sym);
	int nr_pcnt = 1, err;

	notes->offsets = zalloc(size * sizeof(struct annotation_line *));
	if (notes->offsets == NULL)
		return -1;

	if (perf_evsel__is_group_event(evsel))
		nr_pcnt = evsel->nr_members;

	err = symbol__annotate(sym, map, evsel, 0, parch);
	if (err)
		goto out_free_offsets;

	notes->options = options;

	symbol__calc_percent(sym, evsel);

	notes->start = map__rip_2objdump(map, sym->start);

	annotation__set_offsets(notes, size);
	annotation__mark_jump_targets(notes, sym);
	annotation__compute_ipc(notes, size);
	annotation__init_column_widths(notes, sym);
	notes->nr_events = nr_pcnt;

	annotation__update_column_widths(notes);

	return 0;

out_free_offsets:
	zfree(&notes->offsets);
	return -1;
}

int __annotation__scnprintf_samples_period(struct annotation *notes,
					   char *bf, size_t size,
					   struct perf_evsel *evsel,
					   bool show_freq)
{
	const char *ev_name = perf_evsel__name(evsel);
	char buf[1024], ref[30] = " show reference callgraph, ";
	char sample_freq_str[64] = "";
	unsigned long nr_samples = 0;
	int nr_members = 1;
	bool enable_ref = false;
	u64 nr_events = 0;
	char unit;
	int i;

	if (perf_evsel__is_group_event(evsel)) {
		perf_evsel__group_desc(evsel, buf, sizeof(buf));
		ev_name = buf;
                nr_members = evsel->nr_members;
	}

	for (i = 0; i < nr_members; i++) {
		struct sym_hist *ah = annotation__histogram(notes, evsel->idx + i);

		nr_samples += ah->nr_samples;
		nr_events  += ah->period;
	}

	if (symbol_conf.show_ref_callgraph && strstr(ev_name, "call-graph=no"))
		enable_ref = true;

	if (show_freq)
		scnprintf(sample_freq_str, sizeof(sample_freq_str), " %d Hz,", evsel->attr.sample_freq);

	nr_samples = convert_unit(nr_samples, &unit);
	return scnprintf(bf, size, "Samples: %lu%c of event%s '%s',%s%sEvent count (approx.): %" PRIu64,
			 nr_samples, unit, evsel->nr_members > 1 ? "s" : "",
			 ev_name, sample_freq_str, enable_ref ? ref : " ", nr_events);
}

#define ANNOTATION__CFG(n) \
	{ .name = #n, .value = &annotation__default_options.n, }

/*
 * Keep the entries sorted, they are bsearch'ed
 */
static struct annotation_config {
	const char *name;
	bool *value;
} annotation__configs[] = {
	ANNOTATION__CFG(hide_src_code),
	ANNOTATION__CFG(jump_arrows),
	ANNOTATION__CFG(show_linenr),
	ANNOTATION__CFG(show_nr_jumps),
	ANNOTATION__CFG(show_nr_samples),
	ANNOTATION__CFG(show_total_period),
	ANNOTATION__CFG(use_offset),
};

#undef ANNOTATION__CFG

static int annotation_config__cmp(const void *name, const void *cfgp)
{
	const struct annotation_config *cfg = cfgp;

	return strcmp(name, cfg->name);
}

static int annotation__config(const char *var, const char *value,
			    void *data __maybe_unused)
{
	struct annotation_config *cfg;
	const char *name;

	if (!strstarts(var, "annotate."))
		return 0;

	name = var + 9;
	cfg = bsearch(name, annotation__configs, ARRAY_SIZE(annotation__configs),
		      sizeof(struct annotation_config), annotation_config__cmp);

	if (cfg == NULL)
		pr_debug("%s variable unknown, ignoring...", var);
	else
		*cfg->value = perf_config_bool(name, value);
	return 0;
}

void annotation_config__init(void)
{
	perf_config(annotation__config, NULL);

	annotation__default_options.show_total_period = symbol_conf.show_total_period;
	annotation__default_options.show_nr_samples   = symbol_conf.show_nr_samples;
}
