#ifndef __PERF_SORT_H
#define __PERF_SORT_H
#include "../builtin.h"

#include "util.h"

#include "color.h"
#include <linux/list.h>
#include "cache.h"
#include <linux/rbtree.h>
#include "symbol.h"
#include "string.h"
#include "callchain.h"
#include "strlist.h"
#include "values.h"

#include "../perf.h"
#include "debug.h"
#include "header.h"

#include "parse-options.h"
#include "parse-events.h"

#include "thread.h"
#include "sort.h"

extern regex_t parent_regex;
extern char *sort_order;
extern char default_parent_pattern[];
extern char *parent_pattern;
extern char default_sort_order[];
extern int sort__need_collapse;
extern int sort__has_parent;
extern char *field_sep;
extern struct sort_entry sort_comm;
extern struct sort_entry sort_dso;
extern struct sort_entry sort_sym;
extern struct sort_entry sort_parent;
extern unsigned int dsos__col_width;
extern unsigned int comms__col_width;
extern unsigned int threads__col_width;
extern enum sort_type sort__first_dimension;

struct hist_entry {
	struct rb_node		rb_node;
	u64			count;
	struct thread		*thread;
	struct map		*map;
	struct symbol		*sym;
	u64			ip;
	char			level;
	struct symbol	  *parent;
	struct callchain_node	callchain;
	union {
		unsigned long	  position;
		struct hist_entry *pair;
		struct rb_root	  sorted_chain;
	};
};

enum sort_type {
	SORT_PID,
	SORT_COMM,
	SORT_DSO,
	SORT_SYM,
	SORT_PARENT
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	struct list_head list;

	const char *header;

	int64_t (*cmp)(struct hist_entry *, struct hist_entry *);
	int64_t (*collapse)(struct hist_entry *, struct hist_entry *);
	size_t	(*print)(FILE *fp, struct hist_entry *, unsigned int width);
	unsigned int *width;
	bool	elide;
};

extern struct sort_entry sort_thread;
extern struct list_head hist_entry__sort_list;

void setup_sorting(const char * const usagestr[], const struct option *opts);

extern int repsep_fprintf(FILE *fp, const char *fmt, ...);
extern size_t sort__thread_print(FILE *, struct hist_entry *, unsigned int);
extern size_t sort__comm_print(FILE *, struct hist_entry *, unsigned int);
extern size_t sort__dso_print(FILE *, struct hist_entry *, unsigned int);
extern size_t sort__sym_print(FILE *, struct hist_entry *, unsigned int __used);
extern int64_t cmp_null(void *, void *);
extern int64_t sort__thread_cmp(struct hist_entry *, struct hist_entry *);
extern int64_t sort__comm_cmp(struct hist_entry *, struct hist_entry *);
extern int64_t sort__comm_collapse(struct hist_entry *, struct hist_entry *);
extern int64_t sort__dso_cmp(struct hist_entry *, struct hist_entry *);
extern int64_t sort__sym_cmp(struct hist_entry *, struct hist_entry *);
extern int64_t sort__parent_cmp(struct hist_entry *, struct hist_entry *);
extern size_t sort__parent_print(FILE *, struct hist_entry *, unsigned int);
extern int sort_dimension__add(const char *);
void sort_entry__setup_elide(struct sort_entry *self, struct strlist *list,
			     const char *list_name, FILE *fp);

#endif	/* __PERF_SORT_H */
