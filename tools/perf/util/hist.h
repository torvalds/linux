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
extern struct rb_root collapse_hists;
extern struct rb_root output_hists;
extern int callchain;
extern struct callchain_param callchain_param;
extern unsigned long total;
extern unsigned long total_mmap;
extern unsigned long total_comm;
extern unsigned long total_fork;
extern unsigned long total_unknown;
extern unsigned long total_lost;

struct hist_entry *__hist_entry__add(struct thread *thread, struct map *map,
				     struct symbol *sym, struct symbol *parent,
				     u64 ip, u64 count, char level, bool *hit);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
extern void hist_entry__free(struct hist_entry *);
extern void collapse__insert_entry(struct hist_entry *);
extern void collapse__resort(void);
extern void output__insert_entry(struct hist_entry *, u64);
extern void output__resort(u64);

#endif	/* __PERF_HIST_H */
