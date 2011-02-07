#ifndef __PERF_ANNOTATE_H
#define __PERF_ANNOTATE_H

#include <stdbool.h>
#include "types.h"
#include "symbol.h"
#include <linux/list.h>
#include <linux/rbtree.h>

struct objdump_line {
	struct list_head node;
	s64		 offset;
	char		 *line;
};

void objdump_line__free(struct objdump_line *self);
struct objdump_line *objdump__get_next_ip_line(struct list_head *head,
					       struct objdump_line *pos);

struct sym_hist {
	u64		sum;
	u64		addr[0];
};

struct source_line {
	struct rb_node	node;
	double		percent;
	char		*path;
};

/** struct annotation - symbols with hits have this attached as in sannotation
 *
 * @histogram: Array of addr hit histograms per event being monitored
 * @src_line: If 'print_lines' is specified, per source code line percentages
 *
 * src_line is allocated, percentages calculated and all sorted by percentage
 * when the annotation is about to be presented, so the percentages are for
 * one of the entries in the histogram array, i.e. for the event/counter being
 * presented. It is deallocated right after symbol__{tui,tty,etc}_annotate
 * returns.
 */
struct annotation {
	struct source_line *src_line;
	struct sym_hist	   *histograms;
	int    		   nr_histograms;
	int    		   sizeof_sym_hist;
};

struct sannotation {
	struct annotation annotation;
	struct symbol	  symbol;
};

static inline struct sym_hist *annotation__histogram(struct annotation *notes, int idx)
{
	return ((void *)notes->histograms) + (notes->sizeof_sym_hist * idx);
}

static inline struct annotation *symbol__annotation(struct symbol *sym)
{
	struct sannotation *a = container_of(sym, struct sannotation, symbol);
	return &a->annotation;
}

int symbol__inc_addr_samples(struct symbol *sym, struct map *map,
			     int evidx, u64 addr);
int symbol__alloc_hist(struct symbol *sym, int nevents);
void symbol__annotate_zero_histograms(struct symbol *sym);

int symbol__annotate(struct symbol *sym, struct map *map,
		     struct list_head *head, size_t privsize);
int symbol__annotate_printf(struct symbol *sym, struct map *map,
			    struct list_head *head, int evidx, bool full_paths,
			    int min_pcnt, int max_lines);
void symbol__annotate_zero_histogram(struct symbol *sym, int evidx);
void symbol__annotate_decay_histogram(struct symbol *sym,
				      struct list_head *head, int evidx);
void objdump_line_list__purge(struct list_head *head);

int symbol__tty_annotate(struct symbol *sym, struct map *map, int evidx,
			 bool print_lines, bool full_paths, int min_pcnt,
			 int max_lines);

#ifdef NO_NEWT_SUPPORT
static inline int symbol__tui_annotate(struct symbol *sym __used,
				       struct map *map __used, int evidx __used)
{
	return 0;
}
#else
int symbol__tui_annotate(struct symbol *sym, struct map *map, int evidx);
#endif

#endif	/* __PERF_ANNOTATE_H */
