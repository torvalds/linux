#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include "callchain.h"

extern struct callchain_param callchain_param;

struct perf_session;
struct hist_entry;
struct addr_location;
struct symbol;

struct hist_entry *__perf_session__add_hist_entry(struct perf_session *self,
						  struct addr_location *al,
						  struct symbol *parent,
						  u64 count, bool *hit);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
void hist_entry__free(struct hist_entry *);

void perf_session__output_resort(struct perf_session *self, u64 total_samples);
void perf_session__collapse_resort(struct perf_session *self);
size_t perf_session__fprintf_hists(struct perf_session *self,
				   struct perf_session *pair,
				   bool show_displacement, FILE *fp);
#endif	/* __PERF_HIST_H */
