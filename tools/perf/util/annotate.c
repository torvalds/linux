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
#include "cache.h"
#include "symbol.h"
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
#include <sys/utsname.h>

#include "sane_ctype.h"

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
	int		(*init)(struct arch *arch);
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
		.instructions = x86__instructions,
		.nr_instructions = ARRAY_SIZE(x86__instructions),
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
	return scnprintf(bf, size, "%-6.6s %s", ins->name, ops->raw);
}

int ins__scnprintf(struct ins *ins, char *bf, size_t size,
		  struct ins_operands *ops)
{
	if (ins->ops->scnprintf)
		return ins->ops->scnprintf(ins, bf, size, ops);

	return ins__raw_scnprintf(ins, bf, size, ops);
}

static int call__parse(struct arch *arch, struct ins_operands *ops, struct map *map)
{
	char *endptr, *tok, *name;

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

	return ops->target.name == NULL ? -1 : 0;

indirect_call:
	tok = strchr(endptr, '*');
	if (tok == NULL) {
		struct symbol *sym = map__find_symbol(map, map->map_ip(map, ops->target.addr));
		if (sym != NULL)
			ops->target.name = strdup(sym->name);
		else
			ops->target.addr = 0;
		return 0;
	}

	ops->target.addr = strtoull(tok + 1, NULL, 16);
	return 0;
}

static int call__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	if (ops->target.name)
		return scnprintf(bf, size, "%-6.6s %s", ins->name, ops->target.name);

	if (ops->target.addr == 0)
		return ins__raw_scnprintf(ins, bf, size, ops);

	return scnprintf(bf, size, "%-6.6s *%" PRIx64, ins->name, ops->target.addr);
}

static struct ins_ops call_ops = {
	.parse	   = call__parse,
	.scnprintf = call__scnprintf,
};

bool ins__is_call(const struct ins *ins)
{
	return ins->ops == &call_ops;
}

static int jump__parse(struct arch *arch __maybe_unused, struct ins_operands *ops, struct map *map __maybe_unused)
{
	const char *s = strchr(ops->raw, '+');
	const char *c = strchr(ops->raw, ',');

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

	if (s++ != NULL) {
		ops->target.offset = strtoull(s, NULL, 16);
		ops->target.offset_avail = true;
	} else {
		ops->target.offset_avail = false;
	}

	return 0;
}

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	const char *c = strchr(ops->raw, ',');

	if (!ops->target.addr || ops->target.offset < 0)
		return ins__raw_scnprintf(ins, bf, size, ops);

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

	return scnprintf(bf, size, "%-6.6s %.*s%" PRIx64,
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

static int lock__parse(struct arch *arch, struct ins_operands *ops, struct map *map)
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
	    ops->locked.ins.ops->parse(arch, ops->locked.ops, map) < 0)
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

	printed = scnprintf(bf, size, "%-6.6s ", ins->name);
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

static int mov__parse(struct arch *arch, struct ins_operands *ops, struct map *map __maybe_unused)
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
	comment__symbol(ops->source.raw, comment, &ops->source.addr, &ops->source.name);
	comment__symbol(ops->target.raw, comment, &ops->target.addr, &ops->target.name);

	return 0;

out_free_source:
	zfree(&ops->source.raw);
	return -1;
}

static int mov__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6.6s %s,%s", ins->name,
			 ops->source.name ?: ops->source.raw,
			 ops->target.name ?: ops->target.raw);
}

static struct ins_ops mov_ops = {
	.parse	   = mov__parse,
	.scnprintf = mov__scnprintf,
};

static int dec__parse(struct arch *arch __maybe_unused, struct ins_operands *ops, struct map *map __maybe_unused)
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
	comment__symbol(ops->target.raw, comment, &ops->target.addr, &ops->target.name);

	return 0;
}

static int dec__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6.6s %s", ins->name,
			 ops->target.name ?: ops->target.raw);
}

static struct ins_ops dec_ops = {
	.parse	   = dec__parse,
	.scnprintf = dec__scnprintf,
};

static int nop__scnprintf(struct ins *ins __maybe_unused, char *bf, size_t size,
			  struct ins_operands *ops __maybe_unused)
{
	return scnprintf(bf, size, "%-6.6s", "nop");
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
	const size_t size = symbol__size(sym);
	size_t sizeof_sym_hist;

	/* Check for overflow when calculating sizeof_sym_hist */
	if (size > (SIZE_MAX - sizeof(struct sym_hist)) / sizeof(u64))
		return -1;

	sizeof_sym_hist = (sizeof(struct sym_hist) + size * sizeof(u64));

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
				      struct annotation *notes, int evidx, u64 addr)
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
	h->sum++;
	h->addr[offset]++;

	pr_debug3("%#" PRIx64 " %s: period++ [addr: %#" PRIx64 ", %#" PRIx64
		  ", evidx=%d] => %" PRIu64 "\n", sym->start, sym->name,
		  addr, addr - sym->start, evidx, h->addr[offset]);
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
				    int evidx, u64 addr)
{
	struct annotation *notes;

	if (sym == NULL)
		return 0;
	notes = symbol__get_annotation(sym, false);
	if (notes == NULL)
		return -ENOMEM;
	return __symbol__inc_addr_samples(sym, map, notes, evidx, addr);
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

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, int evidx)
{
	return symbol__inc_addr_samples(ams->sym, ams->map, evidx, ams->al_addr);
}

int hist_entry__inc_addr_samples(struct hist_entry *he, int evidx, u64 ip)
{
	return symbol__inc_addr_samples(he->ms.sym, he->ms.map, evidx, ip);
}

static void disasm_line__init_ins(struct disasm_line *dl, struct arch *arch, struct map *map)
{
	dl->ins.ops = ins__find(arch, dl->ins.name);

	if (!dl->ins.ops)
		return;

	if (dl->ins.ops->parse && dl->ins.ops->parse(arch, &dl->ops, map) < 0)
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

static struct disasm_line *disasm_line__new(s64 offset, char *line,
					    size_t privsize, int line_nr,
					    struct arch *arch,
					    struct map *map)
{
	struct disasm_line *dl = zalloc(sizeof(*dl) + privsize);

	if (dl != NULL) {
		dl->offset = offset;
		dl->line = strdup(line);
		dl->line_nr = line_nr;
		if (dl->line == NULL)
			goto out_delete;

		if (offset != -1) {
			if (disasm_line__parse(dl->line, &dl->ins.name, &dl->ops.raw) < 0)
				goto out_free_line;

			disasm_line__init_ins(dl, arch, map);
		}
	}

	return dl;

out_free_line:
	zfree(&dl->line);
out_delete:
	free(dl);
	return NULL;
}

void disasm_line__free(struct disasm_line *dl)
{
	zfree(&dl->line);
	if (dl->ins.ops && dl->ins.ops->free)
		dl->ins.ops->free(&dl->ops);
	else
		ins__delete(&dl->ops);
	free((void *)dl->ins.name);
	dl->ins.name = NULL;
	free(dl);
}

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw)
{
	if (raw || !dl->ins.ops)
		return scnprintf(bf, size, "%-6.6s %s", dl->ins.name, dl->ops.raw);

	return ins__scnprintf(&dl->ins, bf, size, &dl->ops);
}

static void disasm__add(struct list_head *head, struct disasm_line *line)
{
	list_add_tail(&line->node, head);
}

struct disasm_line *disasm__get_next_ip_line(struct list_head *head, struct disasm_line *pos)
{
	list_for_each_entry_continue(pos, head, node)
		if (pos->offset >= 0)
			return pos;

	return NULL;
}

double disasm__calc_percent(struct annotation *notes, int evidx, s64 offset,
			    s64 end, const char **path, u64 *nr_samples)
{
	struct source_line *src_line = notes->src->lines;
	double percent = 0.0;
	*nr_samples = 0;

	if (src_line) {
		size_t sizeof_src_line = sizeof(*src_line) +
				sizeof(src_line->samples) * (src_line->nr_pcnt - 1);

		while (offset < end) {
			src_line = (void *)notes->src->lines +
					(sizeof_src_line * offset);

			if (*path == NULL)
				*path = src_line->path;

			percent += src_line->samples[evidx].percent;
			*nr_samples += src_line->samples[evidx].nr;
			offset++;
		}
	} else {
		struct sym_hist *h = annotation__histogram(notes, evidx);
		unsigned int hits = 0;

		while (offset < end)
			hits += h->addr[offset++];

		if (h->sum) {
			*nr_samples = hits;
			percent = 100.0 * hits / h->sum;
		}
	}

	return percent;
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


static int disasm_line__print(struct disasm_line *dl, struct symbol *sym, u64 start,
		      struct perf_evsel *evsel, u64 len, int min_pcnt, int printed,
		      int max_lines, struct disasm_line *queue)
{
	static const char *prev_line;
	static const char *prev_color;

	if (dl->offset != -1) {
		const char *path = NULL;
		u64 nr_samples;
		double percent, max_percent = 0.0;
		double *ppercents = &percent;
		u64 *psamples = &nr_samples;
		int i, nr_percent = 1;
		const char *color;
		struct annotation *notes = symbol__annotation(sym);
		s64 offset = dl->offset;
		const u64 addr = start + offset;
		struct disasm_line *next;
		struct block_range *br;

		next = disasm__get_next_ip_line(&notes->src->source, dl);

		if (perf_evsel__is_group_event(evsel)) {
			nr_percent = evsel->nr_members;
			ppercents = calloc(nr_percent, sizeof(double));
			psamples = calloc(nr_percent, sizeof(u64));
			if (ppercents == NULL || psamples == NULL) {
				return -1;
			}
		}

		for (i = 0; i < nr_percent; i++) {
			percent = disasm__calc_percent(notes,
					notes->src->lines ? i : evsel->idx + i,
					offset,
					next ? next->offset : (s64) len,
					&path, &nr_samples);

			ppercents[i] = percent;
			psamples[i] = nr_samples;
			if (percent > max_percent)
				max_percent = percent;
		}

		if (max_percent < min_pcnt)
			return -1;

		if (max_lines && printed >= max_lines)
			return 1;

		if (queue != NULL) {
			list_for_each_entry_from(queue, &notes->src->source, node) {
				if (queue == dl)
					break;
				disasm_line__print(queue, sym, start, evsel, len,
						    0, 0, 1, NULL);
			}
		}

		color = get_percent_color(max_percent);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored addr with the same filename:line
		 */
		if (path) {
			if (!prev_line || strcmp(prev_line, path)
				       || color != prev_color) {
				color_fprintf(stdout, color, " %s", path);
				prev_line = path;
				prev_color = color;
			}
		}

		for (i = 0; i < nr_percent; i++) {
			percent = ppercents[i];
			nr_samples = psamples[i];
			color = get_percent_color(percent);

			if (symbol_conf.show_total_period)
				color_fprintf(stdout, color, " %7" PRIu64,
					      nr_samples);
			else
				color_fprintf(stdout, color, " %7.2f", percent);
		}

		printf(" :	");

		br = block_range__find(addr);
		color_fprintf(stdout, annotate__address_color(br), "  %" PRIx64 ":", addr);
		color_fprintf(stdout, annotate__asm_color(br), "%s", dl->line);
		annotate__branch_printf(br, addr);
		printf("\n");

		if (ppercents != &percent)
			free(ppercents);

		if (psamples != &nr_samples)
			free(psamples);

	} else if (max_lines && printed >= max_lines)
		return 1;
	else {
		int width = 8;

		if (queue)
			return -1;

		if (perf_evsel__is_group_event(evsel))
			width *= evsel->nr_members;

		if (!*dl->line)
			printf(" %*s:\n", width, " ");
		else
			printf(" %*s:	%s\n", width, " ", dl->line);
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
static int symbol__parse_objdump_line(struct symbol *sym, struct map *map,
				      struct arch *arch,
				      FILE *file, size_t privsize,
				      int *line_nr)
{
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

	dl = disasm_line__new(offset, parsed_line, privsize, *line_nr, arch, map);
	free(line);
	(*line_nr)++;

	if (dl == NULL)
		return -1;

	if (!disasm_line__has_offset(dl)) {
		dl->ops.target.offset = dl->ops.target.addr -
					map__rip_2objdump(map, sym->start);
		dl->ops.target.offset_avail = true;
	}

	/* kcore has no symbols, so add the call target name */
	if (dl->ins.ops && ins__is_call(&dl->ins) && !dl->ops.target.name) {
		struct addr_map_symbol target = {
			.map = map,
			.addr = dl->ops.target.addr,
		};

		if (!map_groups__find_ams(&target) &&
		    target.sym->start == target.al_addr)
			dl->ops.target.name = strdup(target.sym->name);
	}

	disasm__add(&notes->src->source, dl);

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
		dl = list_entry(list->prev, struct disasm_line, node);

		if (dl->ins.ops) {
			if (dl->ins.ops != &nop_ops)
				return;
		} else {
			if (!strstr(dl->line, " nop ") &&
			    !strstr(dl->line, " nopl ") &&
			    !strstr(dl->line, " nopw "))
				return;
		}

		list_del(&dl->node);
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

	if (dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(dso))
		return SYMBOL_ANNOTATE_ERRNO__NO_VMLINUX;

	build_id_filename = dso__build_id_filename(dso, NULL, 0);
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

static const char *annotate__norm_arch(const char *arch_name)
{
	struct utsname uts;

	if (!arch_name) { /* Assume we are annotating locally. */
		if (uname(&uts) < 0)
			return NULL;
		arch_name = uts.machine;
	}
	return normalize_arch((char *)arch_name);
}

int symbol__disassemble(struct symbol *sym, struct map *map, const char *arch_name, size_t privsize)
{
	struct dso *dso = map->dso;
	char command[PATH_MAX * 2];
	struct arch *arch = NULL;
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

	arch_name = annotate__norm_arch(arch_name);
	if (!arch_name)
		return -1;

	arch = arch__find(arch_name);
	if (arch == NULL)
		return -ENOTSUP;

	if (arch->init) {
		err = arch->init(arch);
		if (err) {
			pr_err("%s: failed to initialize %s arch priv area\n", __func__, arch->name);
			return err;
		}
	}

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
		char tmp[PATH_MAX];
		struct kmod_path m;
		int fd;
		bool ret;

		if (kmod_path__parse_ext(&m, symfs_filename))
			goto out;

		snprintf(tmp, PATH_MAX, "/tmp/perf-kmod-XXXXXX");

		fd = mkstemp(tmp);
		if (fd < 0) {
			free(m.ext);
			goto out;
		}

		ret = decompress_to_file(m.ext, symfs_filename, fd);

		if (ret)
			pr_err("Cannot decompress %s %s\n", m.ext, symfs_filename);

		free(m.ext);
		close(fd);

		if (!ret)
			goto out;

		strcpy(symfs_filename, tmp);
	}

	snprintf(command, sizeof(command),
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

	pr_debug("Executing: %s\n", command);

	err = -1;
	if (pipe(stdout_fd) < 0) {
		pr_err("Failure creating the pipe to run %s\n", command);
		goto out_remove_tmp;
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
		goto out_remove_tmp;
	}

	nline = 0;
	while (!feof(file)) {
		/*
		 * The source code line number (lineno) needs to be kept in
		 * accross calls to symbol__parse_objdump_line(), so that it
		 * can associate it with the instructions till the next one.
		 * See disasm_line__new() and struct disasm_line::line_nr.
		 */
		if (symbol__parse_objdump_line(sym, map, arch, file, privsize,
			    &lineno) < 0)
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
	goto out_remove_tmp;
}

static void insert_source_line(struct rb_root *root, struct source_line *src_line)
{
	struct source_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	int i, ret;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct source_line, node);

		ret = strcmp(iter->path, src_line->path);
		if (ret == 0) {
			for (i = 0; i < src_line->nr_pcnt; i++)
				iter->samples[i].percent_sum += src_line->samples[i].percent;
			return;
		}

		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	for (i = 0; i < src_line->nr_pcnt; i++)
		src_line->samples[i].percent_sum = src_line->samples[i].percent;

	rb_link_node(&src_line->node, parent, p);
	rb_insert_color(&src_line->node, root);
}

static int cmp_source_line(struct source_line *a, struct source_line *b)
{
	int i;

	for (i = 0; i < a->nr_pcnt; i++) {
		if (a->samples[i].percent_sum == b->samples[i].percent_sum)
			continue;
		return a->samples[i].percent_sum > b->samples[i].percent_sum;
	}

	return 0;
}

static void __resort_source_line(struct rb_root *root, struct source_line *src_line)
{
	struct source_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct source_line, node);

		if (cmp_source_line(src_line, iter))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&src_line->node, parent, p);
	rb_insert_color(&src_line->node, root);
}

static void resort_source_line(struct rb_root *dest_root, struct rb_root *src_root)
{
	struct source_line *src_line;
	struct rb_node *node;

	node = rb_first(src_root);
	while (node) {
		struct rb_node *next;

		src_line = rb_entry(node, struct source_line, node);
		next = rb_next(node);
		rb_erase(node, src_root);

		__resort_source_line(dest_root, src_line);
		node = next;
	}
}

static void symbol__free_source_line(struct symbol *sym, int len)
{
	struct annotation *notes = symbol__annotation(sym);
	struct source_line *src_line = notes->src->lines;
	size_t sizeof_src_line;
	int i;

	sizeof_src_line = sizeof(*src_line) +
			  (sizeof(src_line->samples) * (src_line->nr_pcnt - 1));

	for (i = 0; i < len; i++) {
		free_srcline(src_line->path);
		src_line = (void *)src_line + sizeof_src_line;
	}

	zfree(&notes->src->lines);
}

/* Get the filename:line for the colored entries */
static int symbol__get_source_line(struct symbol *sym, struct map *map,
				   struct perf_evsel *evsel,
				   struct rb_root *root, int len)
{
	u64 start;
	int i, k;
	int evidx = evsel->idx;
	struct source_line *src_line;
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evidx);
	struct rb_root tmp_root = RB_ROOT;
	int nr_pcnt = 1;
	u64 h_sum = h->sum;
	size_t sizeof_src_line = sizeof(struct source_line);

	if (perf_evsel__is_group_event(evsel)) {
		for (i = 1; i < evsel->nr_members; i++) {
			h = annotation__histogram(notes, evidx + i);
			h_sum += h->sum;
		}
		nr_pcnt = evsel->nr_members;
		sizeof_src_line += (nr_pcnt - 1) * sizeof(src_line->samples);
	}

	if (!h_sum)
		return 0;

	src_line = notes->src->lines = calloc(len, sizeof_src_line);
	if (!notes->src->lines)
		return -1;

	start = map__rip_2objdump(map, sym->start);

	for (i = 0; i < len; i++) {
		u64 offset, nr_samples;
		double percent_max = 0.0;

		src_line->nr_pcnt = nr_pcnt;

		for (k = 0; k < nr_pcnt; k++) {
			double percent = 0.0;

			h = annotation__histogram(notes, evidx + k);
			nr_samples = h->addr[i];
			if (h->sum)
				percent = 100.0 * nr_samples / h->sum;

			if (percent > percent_max)
				percent_max = percent;
			src_line->samples[k].percent = percent;
			src_line->samples[k].nr = nr_samples;
		}

		if (percent_max <= 0.5)
			goto next;

		offset = start + i;
		src_line->path = get_srcline(map->dso, offset, NULL,
					     false, true);
		insert_source_line(&tmp_root, src_line);

	next:
		src_line = (void *)src_line + sizeof_src_line;
	}

	resort_source_line(root, &tmp_root);
	return 0;
}

static void print_summary(struct rb_root *root, const char *filename)
{
	struct source_line *src_line;
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

		src_line = rb_entry(node, struct source_line, node);
		for (i = 0; i < src_line->nr_pcnt; i++) {
			percent = src_line->samples[i].percent_sum;
			color = get_percent_color(percent);
			color_fprintf(stdout, color, " %7.2f", percent);

			if (percent > percent_max)
				percent_max = percent;
		}

		path = src_line->path;
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
		if (h->addr[offset] != 0)
			printf("%*" PRIx64 ": %" PRIu64 "\n", BITS_PER_LONG / 2,
			       sym->start + offset, h->addr[offset]);
	printf("%*s: %" PRIu64 "\n", BITS_PER_LONG / 2, "h->sum", h->sum);
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
	struct disasm_line *pos, *queue = NULL;
	u64 start = map__rip_2objdump(map, sym->start);
	int printed = 2, queue_len = 0;
	int more = 0;
	u64 len;
	int width = 8;
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
	       width, width, "Percent", d_filename, evsel_name, h->sum);

	printf("%-*.*s----\n",
	       graph_dotted_len, graph_dotted_len, graph_dotted_line);

	if (verbose > 0)
		symbol__annotate_hits(sym, evsel);

	list_for_each_entry(pos, &notes->src->source, node) {
		if (context && queue == NULL) {
			queue = pos;
			queue_len = 0;
		}

		switch (disasm_line__print(pos, sym, start, evsel, len,
					    min_pcnt, printed, max_lines,
					    queue)) {
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

	h->sum = 0;
	for (offset = 0; offset < len; ++offset) {
		h->addr[offset] = h->addr[offset] * 7 / 8;
		h->sum += h->addr[offset];
	}
}

void disasm__purge(struct list_head *head)
{
	struct disasm_line *pos, *n;

	list_for_each_entry_safe(pos, n, head, node) {
		list_del(&pos->node);
		disasm_line__free(pos);
	}
}

static size_t disasm_line__fprintf(struct disasm_line *dl, FILE *fp)
{
	size_t printed;

	if (dl->offset == -1)
		return fprintf(fp, "%s\n", dl->line);

	printed = fprintf(fp, "%#" PRIx64 " %s", dl->offset, dl->ins.name);

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

	list_for_each_entry(pos, head, node)
		printed += disasm_line__fprintf(pos, fp);

	return printed;
}

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel, bool print_lines,
			 bool full_paths, int min_pcnt, int max_lines)
{
	struct dso *dso = map->dso;
	struct rb_root source_line = RB_ROOT;
	u64 len;

	if (symbol__disassemble(sym, map, perf_evsel__env_arch(evsel), 0) < 0)
		return -1;

	len = symbol__size(sym);

	if (print_lines) {
		srcline_full_filename = full_paths;
		symbol__get_source_line(sym, map, evsel, &source_line, len);
		print_summary(&source_line, dso->long_name);
	}

	symbol__annotate_printf(sym, map, evsel, full_paths,
				min_pcnt, max_lines, 0);
	if (print_lines)
		symbol__free_source_line(sym, len);

	disasm__purge(&symbol__annotation(sym)->src->source);

	return 0;
}

bool ui__has_annotation(void)
{
	return use_browser == 1 && perf_hpp_list.sym;
}
