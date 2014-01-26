#ifndef __PERF_ANNOTATE_H
#define __PERF_ANNOTATE_H

#include <stdbool.h>
#include <stdint.h>
#include "types.h"
#include "symbol.h"
#include "hist.h"
#include "sort.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include <pthread.h>

struct ins;

struct ins_operands {
	char	*raw;
	struct {
		char	*raw;
		char	*name;
		u64	addr;
		u64	offset;
	} target;
	union {
		struct {
			char	*raw;
			char	*name;
			u64	addr;
		} source;
		struct {
			struct ins *ins;
			struct ins_operands *ops;
		} locked;
	};
};

struct ins_ops {
	void (*free)(struct ins_operands *ops);
	int (*parse)(struct ins_operands *ops);
	int (*scnprintf)(struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops);
};

struct ins {
	const char     *name;
	struct ins_ops *ops;
};

bool ins__is_jump(const struct ins *ins);
bool ins__is_call(const struct ins *ins);
int ins__scnprintf(struct ins *ins, char *bf, size_t size, struct ins_operands *ops);

struct annotation;

struct disasm_line {
	struct list_head    node;
	s64		    offset;
	char		    *line;
	char		    *name;
	struct ins	    *ins;
	struct ins_operands ops;
};

static inline bool disasm_line__has_offset(const struct disasm_line *dl)
{
	return dl->ops.target.offset != UINT64_MAX;
}

void disasm_line__free(struct disasm_line *dl);
struct disasm_line *disasm__get_next_ip_line(struct list_head *head, struct disasm_line *pos);
int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw);
size_t disasm__fprintf(struct list_head *head, FILE *fp);
double disasm__calc_percent(struct annotation *notes, int evidx, s64 offset,
			    s64 end, const char **path);

struct sym_hist {
	u64		sum;
	u64		addr[0];
};

struct source_line_percent {
	double		percent;
	double		percent_sum;
};

struct source_line {
	struct rb_node	node;
	char		*path;
	int		nr_pcnt;
	struct source_line_percent p[1];
};

/** struct annotated_source - symbols with hits have this attached as in sannotation
 *
 * @histogram: Array of addr hit histograms per event being monitored
 * @lines: If 'print_lines' is specified, per source code line percentages
 * @source: source parsed from a disassembler like objdump -dS
 *
 * lines is allocated, percentages calculated and all sorted by percentage
 * when the annotation is about to be presented, so the percentages are for
 * one of the entries in the histogram array, i.e. for the event/counter being
 * presented. It is deallocated right after symbol__{tui,tty,etc}_annotate
 * returns.
 */
struct annotated_source {
	struct list_head   source;
	struct source_line *lines;
	int    		   nr_histograms;
	int    		   sizeof_sym_hist;
	struct sym_hist	   histograms[0];
};

struct annotation {
	pthread_mutex_t		lock;
	struct annotated_source *src;
};

struct sannotation {
	struct annotation annotation;
	struct symbol	  symbol;
};

static inline struct sym_hist *annotation__histogram(struct annotation *notes, int idx)
{
	return (((void *)&notes->src->histograms) +
	 	(notes->src->sizeof_sym_hist * idx));
}

static inline struct annotation *symbol__annotation(struct symbol *sym)
{
	struct sannotation *a = container_of(sym, struct sannotation, symbol);
	return &a->annotation;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, int evidx);

int hist_entry__inc_addr_samples(struct hist_entry *he, int evidx, u64 addr);

int symbol__alloc_hist(struct symbol *sym);
void symbol__annotate_zero_histograms(struct symbol *sym);

int symbol__annotate(struct symbol *sym, struct map *map, size_t privsize);

int hist_entry__annotate(struct hist_entry *he, size_t privsize);

int symbol__annotate_init(struct map *map __maybe_unused, struct symbol *sym);
int symbol__annotate_printf(struct symbol *sym, struct map *map,
			    struct perf_evsel *evsel, bool full_paths,
			    int min_pcnt, int max_lines, int context);
void symbol__annotate_zero_histogram(struct symbol *sym, int evidx);
void symbol__annotate_decay_histogram(struct symbol *sym, int evidx);
void disasm__purge(struct list_head *head);

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel, bool print_lines,
			 bool full_paths, int min_pcnt, int max_lines);

#ifdef HAVE_SLANG_SUPPORT
int symbol__tui_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel,
			 struct hist_browser_timer *hbt);
#else
static inline int symbol__tui_annotate(struct symbol *sym __maybe_unused,
				struct map *map __maybe_unused,
				struct perf_evsel *evsel  __maybe_unused,
				struct hist_browser_timer *hbt
				__maybe_unused)
{
	return 0;
}
#endif

extern const char	*disassembler_style;

#endif	/* __PERF_ANNOTATE_H */
