#ifndef __PERF_HIST_H
#define __PERF_HIST_H
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

extern struct rb_root hist;
extern int callchain;
extern struct callchain_param callchain_param;

struct hist_entry *__hist_entry__add(struct addr_location *al,
				     struct symbol *parent,
				     u64 count, bool *hit);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
extern void hist_entry__free(struct hist_entry *);
extern void collapse__resort(void);
extern void output__resort(u64);

#endif	/* __PERF_HIST_H */
