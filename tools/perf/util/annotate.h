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

struct annotation {
	struct sym_hist	   *histogram;
	struct source_line *src_line;
};

struct sannotation {
	struct annotation annotation;
	struct symbol	  symbol;
};

static inline struct annotation *symbol__annotation(struct symbol *sym)
{
	struct sannotation *a = container_of(sym, struct sannotation, symbol);
	return &a->annotation;
}

int symbol__inc_addr_samples(struct symbol *sym, struct map *map, u64 addr);

int symbol__annotate(struct symbol *sym, struct map *map,
		     struct list_head *head, size_t privsize);

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 bool print_lines, bool full_paths);

#ifdef NO_NEWT_SUPPORT
static inline int symbol__tui_annotate(symbol *sym __used,
				       struct map *map __used)
{
	return 0;
}
#else
int symbol__tui_annotate(struct symbol *sym, struct map *map);
#endif

#endif	/* __PERF_ANNOTATE_H */
