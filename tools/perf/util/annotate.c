/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-annotate.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

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
#include <regex.h>
#include <pthread.h>
#include <linux/bitops.h>

const char 	*disassembler_style;
const char	*objdump_path;
static regex_t	 file_lineno;

static struct ins *ins__find(const char *name);
static int disasm_line__parse(char *line, char **namep, char **rawp);

static void ins__delete(struct ins_operands *ops)
{
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

static int call__parse(struct ins_operands *ops)
{
	char *endptr, *tok, *name;

	ops->target.addr = strtoull(ops->raw, &endptr, 16);

	name = strchr(endptr, '<');
	if (name == NULL)
		goto indirect_call;

	name++;

	tok = strchr(name, '>');
	if (tok == NULL)
		return -1;

	*tok = '\0';
	ops->target.name = strdup(name);
	*tok = '>';

	return ops->target.name == NULL ? -1 : 0;

indirect_call:
	tok = strchr(endptr, '(');
	if (tok != NULL) {
		ops->target.addr = 0;
		return 0;
	}

	tok = strchr(endptr, '*');
	if (tok == NULL)
		return -1;

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

static int jump__parse(struct ins_operands *ops)
{
	const char *s = strchr(ops->raw, '+');

	ops->target.addr = strtoull(ops->raw, NULL, 16);

	if (s++ != NULL)
		ops->target.offset = strtoull(s, NULL, 16);
	else
		ops->target.offset = UINT64_MAX;

	return 0;
}

static int jump__scnprintf(struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops)
{
	return scnprintf(bf, size, "%-6.6s %" PRIx64, ins->name, ops->target.offset);
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

static int lock__parse(struct ins_operands *ops)
{
	char *name;

	ops->locked.ops = zalloc(sizeof(*ops->locked.ops));
	if (ops->locked.ops == NULL)
		return 0;

	if (disasm_line__parse(ops->raw, &name, &ops->locked.ops->raw) < 0)
		goto out_free_ops;

	ops->locked.ins = ins__find(name);
	free(name);

	if (ops->locked.ins == NULL)
		goto out_free_ops;

	if (!ops->locked.ins->ops)
		return 0;

	if (ops->locked.ins->ops->parse &&
	    ops->locked.ins->ops->parse(ops->locked.ops) < 0)
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

	if (ops->locked.ins == NULL)
		return ins__raw_scnprintf(ins, bf, size, ops);

	printed = scnprintf(bf, size, "%-6.6s ", ins->name);
	return printed + ins__scnprintf(ops->locked.ins, bf + printed,
					size - printed, ops->locked.ops);
}

static void lock__delete(struct ins_operands *ops)
{
	struct ins *ins = ops->locked.ins;

	if (ins && ins->ops->free)
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

static int mov__parse(struct ins_operands *ops)
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
	comment = strchr(s, '#');

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

	while (comment[0] != '\0' && isspace(comment[0]))
		++comment;

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

static int dec__parse(struct ins_operands *ops)
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

	comment = strchr(s, '#');
	if (comment == NULL)
		return 0;

	while (comment[0] != '\0' && isspace(comment[0]))
		++comment;

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

/*
 * Must be sorted by name!
 */
static struct ins instructions[] = {
	{ .name = "add",   .ops  = &mov_ops, },
	{ .name = "addl",  .ops  = &mov_ops, },
	{ .name = "addq",  .ops  = &mov_ops, },
	{ .name = "addw",  .ops  = &mov_ops, },
	{ .name = "and",   .ops  = &mov_ops, },
	{ .name = "bts",   .ops  = &mov_ops, },
	{ .name = "call",  .ops  = &call_ops, },
	{ .name = "callq", .ops  = &call_ops, },
	{ .name = "cmp",   .ops  = &mov_ops, },
	{ .name = "cmpb",  .ops  = &mov_ops, },
	{ .name = "cmpl",  .ops  = &mov_ops, },
	{ .name = "cmpq",  .ops  = &mov_ops, },
	{ .name = "cmpw",  .ops  = &mov_ops, },
	{ .name = "cmpxch", .ops  = &mov_ops, },
	{ .name = "dec",   .ops  = &dec_ops, },
	{ .name = "decl",  .ops  = &dec_ops, },
	{ .name = "imul",  .ops  = &mov_ops, },
	{ .name = "inc",   .ops  = &dec_ops, },
	{ .name = "incl",  .ops  = &dec_ops, },
	{ .name = "ja",	   .ops  = &jump_ops, },
	{ .name = "jae",   .ops  = &jump_ops, },
	{ .name = "jb",	   .ops  = &jump_ops, },
	{ .name = "jbe",   .ops  = &jump_ops, },
	{ .name = "jc",	   .ops  = &jump_ops, },
	{ .name = "jcxz",  .ops  = &jump_ops, },
	{ .name = "je",	   .ops  = &jump_ops, },
	{ .name = "jecxz", .ops  = &jump_ops, },
	{ .name = "jg",	   .ops  = &jump_ops, },
	{ .name = "jge",   .ops  = &jump_ops, },
	{ .name = "jl",    .ops  = &jump_ops, },
	{ .name = "jle",   .ops  = &jump_ops, },
	{ .name = "jmp",   .ops  = &jump_ops, },
	{ .name = "jmpq",  .ops  = &jump_ops, },
	{ .name = "jna",   .ops  = &jump_ops, },
	{ .name = "jnae",  .ops  = &jump_ops, },
	{ .name = "jnb",   .ops  = &jump_ops, },
	{ .name = "jnbe",  .ops  = &jump_ops, },
	{ .name = "jnc",   .ops  = &jump_ops, },
	{ .name = "jne",   .ops  = &jump_ops, },
	{ .name = "jng",   .ops  = &jump_ops, },
	{ .name = "jnge",  .ops  = &jump_ops, },
	{ .name = "jnl",   .ops  = &jump_ops, },
	{ .name = "jnle",  .ops  = &jump_ops, },
	{ .name = "jno",   .ops  = &jump_ops, },
	{ .name = "jnp",   .ops  = &jump_ops, },
	{ .name = "jns",   .ops  = &jump_ops, },
	{ .name = "jnz",   .ops  = &jump_ops, },
	{ .name = "jo",	   .ops  = &jump_ops, },
	{ .name = "jp",	   .ops  = &jump_ops, },
	{ .name = "jpe",   .ops  = &jump_ops, },
	{ .name = "jpo",   .ops  = &jump_ops, },
	{ .name = "jrcxz", .ops  = &jump_ops, },
	{ .name = "js",	   .ops  = &jump_ops, },
	{ .name = "jz",	   .ops  = &jump_ops, },
	{ .name = "lea",   .ops  = &mov_ops, },
	{ .name = "lock",  .ops  = &lock_ops, },
	{ .name = "mov",   .ops  = &mov_ops, },
	{ .name = "movb",  .ops  = &mov_ops, },
	{ .name = "movdqa",.ops  = &mov_ops, },
	{ .name = "movl",  .ops  = &mov_ops, },
	{ .name = "movq",  .ops  = &mov_ops, },
	{ .name = "movslq", .ops  = &mov_ops, },
	{ .name = "movzbl", .ops  = &mov_ops, },
	{ .name = "movzwl", .ops  = &mov_ops, },
	{ .name = "nop",   .ops  = &nop_ops, },
	{ .name = "nopl",  .ops  = &nop_ops, },
	{ .name = "nopw",  .ops  = &nop_ops, },
	{ .name = "or",    .ops  = &mov_ops, },
	{ .name = "orl",   .ops  = &mov_ops, },
	{ .name = "test",  .ops  = &mov_ops, },
	{ .name = "testb", .ops  = &mov_ops, },
	{ .name = "testl", .ops  = &mov_ops, },
	{ .name = "xadd",  .ops  = &mov_ops, },
	{ .name = "xbeginl", .ops  = &jump_ops, },
	{ .name = "xbeginq", .ops  = &jump_ops, },
};

static int ins__cmp(const void *name, const void *insp)
{
	const struct ins *ins = insp;

	return strcmp(name, ins->name);
}

static struct ins *ins__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(instructions);

	return bsearch(name, instructions, nmemb, sizeof(struct ins), ins__cmp);
}

int symbol__annotate_init(struct map *map __maybe_unused, struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);
	pthread_mutex_init(&notes->lock, NULL);
	return 0;
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

void symbol__annotate_zero_histograms(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);

	pthread_mutex_lock(&notes->lock);
	if (notes->src != NULL)
		memset(notes->src->histograms, 0,
		       notes->src->nr_histograms * notes->src->sizeof_sym_hist);
	pthread_mutex_unlock(&notes->lock);
}

static int __symbol__inc_addr_samples(struct symbol *sym, struct map *map,
				      struct annotation *notes, int evidx, u64 addr)
{
	unsigned offset;
	struct sym_hist *h;

	pr_debug3("%s: addr=%#" PRIx64 "\n", __func__, map->unmap_ip(map, addr));

	if (addr < sym->start || addr >= sym->end)
		return -ERANGE;

	offset = addr - sym->start;
	h = annotation__histogram(notes, evidx);
	h->sum++;
	h->addr[offset]++;

	pr_debug3("%#" PRIx64 " %s: period++ [addr: %#" PRIx64 ", %#" PRIx64
		  ", evidx=%d] => %" PRIu64 "\n", sym->start, sym->name,
		  addr, addr - sym->start, evidx, h->addr[offset]);
	return 0;
}

static int symbol__inc_addr_samples(struct symbol *sym, struct map *map,
				    int evidx, u64 addr)
{
	struct annotation *notes;

	if (sym == NULL)
		return 0;

	notes = symbol__annotation(sym);
	if (notes->src == NULL) {
		if (symbol__alloc_hist(sym) < 0)
			return -ENOMEM;
	}

	return __symbol__inc_addr_samples(sym, map, notes, evidx, addr);
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, int evidx)
{
	return symbol__inc_addr_samples(ams->sym, ams->map, evidx, ams->al_addr);
}

int hist_entry__inc_addr_samples(struct hist_entry *he, int evidx, u64 ip)
{
	return symbol__inc_addr_samples(he->ms.sym, he->ms.map, evidx, ip);
}

static void disasm_line__init_ins(struct disasm_line *dl)
{
	dl->ins = ins__find(dl->name);

	if (dl->ins == NULL)
		return;

	if (!dl->ins->ops)
		return;

	if (dl->ins->ops->parse && dl->ins->ops->parse(&dl->ops) < 0)
		dl->ins = NULL;
}

static int disasm_line__parse(char *line, char **namep, char **rawp)
{
	char *name = line, tmp;

	while (isspace(name[0]))
		++name;

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

	if ((*rawp)[0] != '\0') {
		(*rawp)++;
		while (isspace((*rawp)[0]))
			++(*rawp);
	}

	return 0;

out_free_name:
	zfree(namep);
	return -1;
}

static struct disasm_line *disasm_line__new(s64 offset, char *line,
					size_t privsize, int line_nr)
{
	struct disasm_line *dl = zalloc(sizeof(*dl) + privsize);

	if (dl != NULL) {
		dl->offset = offset;
		dl->line = strdup(line);
		dl->line_nr = line_nr;
		if (dl->line == NULL)
			goto out_delete;

		if (offset != -1) {
			if (disasm_line__parse(dl->line, &dl->name, &dl->ops.raw) < 0)
				goto out_free_line;

			disasm_line__init_ins(dl);
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
	zfree(&dl->name);
	if (dl->ins && dl->ins->ops->free)
		dl->ins->ops->free(&dl->ops);
	else
		ins__delete(&dl->ops);
	free(dl);
}

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw)
{
	if (raw || !dl->ins)
		return scnprintf(bf, size, "%-6.6s %s", dl->name, dl->ops.raw);

	return ins__scnprintf(dl->ins, bf, size, &dl->ops);
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
			    s64 end, const char **path)
{
	struct source_line *src_line = notes->src->lines;
	double percent = 0.0;

	if (src_line) {
		size_t sizeof_src_line = sizeof(*src_line) +
				sizeof(src_line->p) * (src_line->nr_pcnt - 1);

		while (offset < end) {
			src_line = (void *)notes->src->lines +
					(sizeof_src_line * offset);

			if (*path == NULL)
				*path = src_line->path;

			percent += src_line->p[evidx].percent;
			offset++;
		}
	} else {
		struct sym_hist *h = annotation__histogram(notes, evidx);
		unsigned int hits = 0;

		while (offset < end)
			hits += h->addr[offset++];

		if (h->sum)
			percent = 100.0 * hits / h->sum;
	}

	return percent;
}

static int disasm_line__print(struct disasm_line *dl, struct symbol *sym, u64 start,
		      struct perf_evsel *evsel, u64 len, int min_pcnt, int printed,
		      int max_lines, struct disasm_line *queue)
{
	static const char *prev_line;
	static const char *prev_color;

	if (dl->offset != -1) {
		const char *path = NULL;
		double percent, max_percent = 0.0;
		double *ppercents = &percent;
		int i, nr_percent = 1;
		const char *color;
		struct annotation *notes = symbol__annotation(sym);
		s64 offset = dl->offset;
		const u64 addr = start + offset;
		struct disasm_line *next;

		next = disasm__get_next_ip_line(&notes->src->source, dl);

		if (perf_evsel__is_group_event(evsel)) {
			nr_percent = evsel->nr_members;
			ppercents = calloc(nr_percent, sizeof(double));
			if (ppercents == NULL)
				return -1;
		}

		for (i = 0; i < nr_percent; i++) {
			percent = disasm__calc_percent(notes,
					notes->src->lines ? i : evsel->idx + i,
					offset,
					next ? next->offset : (s64) len,
					&path);

			ppercents[i] = percent;
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
			color = get_percent_color(percent);
			color_fprintf(stdout, color, " %7.2f", percent);
		}

		printf(" :	");
		color_fprintf(stdout, PERF_COLOR_MAGENTA, "  %" PRIx64 ":", addr);
		color_fprintf(stdout, PERF_COLOR_BLUE, "%s\n", dl->line);

		if (ppercents != &percent)
			free(ppercents);

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
				      FILE *file, size_t privsize,
				      int *line_nr)
{
	struct annotation *notes = symbol__annotation(sym);
	struct disasm_line *dl;
	char *line = NULL, *parsed_line, *tmp, *tmp2, *c;
	size_t line_len;
	s64 line_ip, offset = -1;
	regmatch_t match[2];

	if (getline(&line, &line_len, file) < 0)
		return -1;

	if (!line)
		return -1;

	while (line_len != 0 && isspace(line[line_len - 1]))
		line[--line_len] = '\0';

	c = strchr(line, '\n');
	if (c)
		*c = 0;

	line_ip = -1;
	parsed_line = line;

	/* /filename:linenr ? Save line number and ignore. */
	if (regexec(&file_lineno, line, 2, match, 0) == 0) {
		*line_nr = atoi(line + match[1].rm_so);
		return 0;
	}

	/*
	 * Strip leading spaces:
	 */
	tmp = line;
	while (*tmp) {
		if (*tmp != ' ')
			break;
		tmp++;
	}

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

	dl = disasm_line__new(offset, parsed_line, privsize, *line_nr);
	free(line);
	(*line_nr)++;

	if (dl == NULL)
		return -1;

	if (dl->ops.target.offset == UINT64_MAX)
		dl->ops.target.offset = dl->ops.target.addr -
					map__rip_2objdump(map, sym->start);

	/* kcore has no symbols, so add the call target name */
	if (dl->ins && ins__is_call(dl->ins) && !dl->ops.target.name) {
		struct addr_map_symbol target = {
			.map = map,
			.addr = dl->ops.target.addr,
		};

		if (!map_groups__find_ams(&target, NULL) &&
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

		if (dl->ins && dl->ins->ops) {
			if (dl->ins->ops != &nop_ops)
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

int symbol__annotate(struct symbol *sym, struct map *map, size_t privsize)
{
	struct dso *dso = map->dso;
	char *filename = dso__build_id_filename(dso, NULL, 0);
	bool free_filename = true;
	char command[PATH_MAX * 2];
	FILE *file;
	int err = 0;
	char symfs_filename[PATH_MAX];
	struct kcore_extract kce;
	bool delete_extract = false;
	int lineno = 0;

	if (filename)
		symbol__join_symfs(symfs_filename, filename);

	if (filename == NULL) {
		if (dso->has_build_id) {
			pr_err("Can't annotate %s: not enough memory\n",
			       sym->name);
			return -ENOMEM;
		}
		goto fallback;
	} else if (dso__is_kcore(dso)) {
		goto fallback;
	} else if (readlink(symfs_filename, command, sizeof(command)) < 0 ||
		   strstr(command, "[kernel.kallsyms]") ||
		   access(symfs_filename, R_OK)) {
		free(filename);
fallback:
		/*
		 * If we don't have build-ids or the build-id file isn't in the
		 * cache, or is just a kallsyms file, well, lets hope that this
		 * DSO is the same as when 'perf record' ran.
		 */
		filename = (char *)dso->long_name;
		symbol__join_symfs(symfs_filename, filename);
		free_filename = false;
	}

	if (dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(dso)) {
		char bf[BUILD_ID_SIZE * 2 + 16] = " with build id ";
		char *build_id_msg = NULL;

		if (dso->annotate_warned)
			goto out_free_filename;

		if (dso->has_build_id) {
			build_id__sprintf(dso->build_id,
					  sizeof(dso->build_id), bf + 15);
			build_id_msg = bf;
		}
		err = -ENOENT;
		dso->annotate_warned = 1;
		pr_err("Can't annotate %s:\n\n"
		       "No vmlinux file%s\nwas found in the path.\n\n"
		       "Please use:\n\n"
		       "  perf buildid-cache -vu vmlinux\n\n"
		       "or:\n\n"
		       "  --vmlinux vmlinux\n",
		       sym->name, build_id_msg ?: "");
		goto out_free_filename;
	}

	pr_debug("%s: filename=%s, sym=%s, start=%#" PRIx64 ", end=%#" PRIx64 "\n", __func__,
		 filename, sym->name, map->unmap_ip(map, sym->start),
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
			if (free_filename) {
				free(filename);
				free_filename = false;
			}
			filename = symfs_filename;
		}
	} else if (dso__needs_decompress(dso)) {
		char tmp[PATH_MAX];
		struct kmod_path m;
		int fd;
		bool ret;

		if (kmod_path__parse_ext(&m, symfs_filename))
			goto out_free_filename;

		snprintf(tmp, PATH_MAX, "/tmp/perf-kmod-XXXXXX");

		fd = mkstemp(tmp);
		if (fd < 0) {
			free(m.ext);
			goto out_free_filename;
		}

		ret = decompress_to_file(m.ext, symfs_filename, fd);

		free(m.ext);
		close(fd);

		if (!ret)
			goto out_free_filename;

		strcpy(symfs_filename, tmp);
	}

	snprintf(command, sizeof(command),
		 "%s %s%s --start-address=0x%016" PRIx64
		 " --stop-address=0x%016" PRIx64
		 " -l -d %s %s -C %s 2>/dev/null|grep -v %s|expand",
		 objdump_path ? objdump_path : "objdump",
		 disassembler_style ? "-M " : "",
		 disassembler_style ? disassembler_style : "",
		 map__rip_2objdump(map, sym->start),
		 map__rip_2objdump(map, sym->end),
		 symbol_conf.annotate_asm_raw ? "" : "--no-show-raw",
		 symbol_conf.annotate_src ? "-S" : "",
		 symfs_filename, filename);

	pr_debug("Executing: %s\n", command);

	file = popen(command, "r");
	if (!file)
		goto out_remove_tmp;

	while (!feof(file))
		if (symbol__parse_objdump_line(sym, map, file, privsize,
			    &lineno) < 0)
			break;

	/*
	 * kallsyms does not have symbol sizes so there may a nop at the end.
	 * Remove it.
	 */
	if (dso__is_kcore(dso))
		delete_last_nop(sym);

	pclose(file);

out_remove_tmp:
	if (dso__needs_decompress(dso))
		unlink(symfs_filename);
out_free_filename:
	if (delete_extract)
		kcore_extract__delete(&kce);
	if (free_filename)
		free(filename);
	return err;
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
				iter->p[i].percent_sum += src_line->p[i].percent;
			return;
		}

		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	for (i = 0; i < src_line->nr_pcnt; i++)
		src_line->p[i].percent_sum = src_line->p[i].percent;

	rb_link_node(&src_line->node, parent, p);
	rb_insert_color(&src_line->node, root);
}

static int cmp_source_line(struct source_line *a, struct source_line *b)
{
	int i;

	for (i = 0; i < a->nr_pcnt; i++) {
		if (a->p[i].percent_sum == b->p[i].percent_sum)
			continue;
		return a->p[i].percent_sum > b->p[i].percent_sum;
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
			  (sizeof(src_line->p) * (src_line->nr_pcnt - 1));

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
		sizeof_src_line += (nr_pcnt - 1) * sizeof(src_line->p);
	}

	if (!h_sum)
		return 0;

	src_line = notes->src->lines = calloc(len, sizeof_src_line);
	if (!notes->src->lines)
		return -1;

	start = map__rip_2objdump(map, sym->start);

	for (i = 0; i < len; i++) {
		u64 offset;
		double percent_max = 0.0;

		src_line->nr_pcnt = nr_pcnt;

		for (k = 0; k < nr_pcnt; k++) {
			h = annotation__histogram(notes, evidx + k);
			src_line->p[k].percent = 100.0 * h->addr[i] / h->sum;

			if (src_line->p[k].percent > percent_max)
				percent_max = src_line->p[k].percent;
		}

		if (percent_max <= 0.5)
			goto next;

		offset = start + i;
		src_line->path = get_srcline(map->dso, offset, NULL, false);
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
			percent = src_line->p[i].percent_sum;
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
	struct disasm_line *pos, *queue = NULL;
	u64 start = map__rip_2objdump(map, sym->start);
	int printed = 2, queue_len = 0;
	int more = 0;
	u64 len;
	int width = 8;
	int namelen, evsel_name_len, graph_dotted_len;

	filename = strdup(dso->long_name);
	if (!filename)
		return -ENOMEM;

	if (full_paths)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = symbol__size(sym);
	namelen = strlen(d_filename);
	evsel_name_len = strlen(evsel_name);

	if (perf_evsel__is_group_event(evsel))
		width *= evsel->nr_members;

	printf(" %-*.*s|	Source code & Disassembly of %s for %s\n",
	       width, width, "Percent", d_filename, evsel_name);

	graph_dotted_len = width + namelen + evsel_name_len;
	printf("-%-*.*s-----------------------------------------\n",
	       graph_dotted_len, graph_dotted_len, graph_dotted_line);

	if (verbose)
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

	printed = fprintf(fp, "%#" PRIx64 " %s", dl->offset, dl->name);

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

	if (symbol__annotate(sym, map, 0) < 0)
		return -1;

	len = symbol__size(sym);

	if (print_lines) {
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

int hist_entry__annotate(struct hist_entry *he, size_t privsize)
{
	return symbol__annotate(he->ms.sym, he->ms.map, privsize);
}

bool ui__has_annotation(void)
{
	return use_browser == 1 && sort__has_sym;
}
