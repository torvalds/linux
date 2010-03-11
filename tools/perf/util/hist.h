#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include "callchain.h"

extern struct callchain_param callchain_param;

struct perf_session;
struct hist_entry;
struct addr_location;
struct symbol;
struct rb_root;

struct hist_entry *__perf_session__add_hist_entry(struct rb_root *hists,
						  struct addr_location *al,
						  struct symbol *parent,
						  u64 count, bool *hit);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
size_t hist_entry__fprintf(struct hist_entry *self,
			   struct perf_session *pair_session,
			   bool show_displacement,
			   long displacement, FILE *fp,
			   u64 session_total);
void hist_entry__free(struct hist_entry *);

void perf_session__output_resort(struct rb_root *hists, u64 total_samples);
void perf_session__collapse_resort(struct rb_root *hists);
size_t perf_session__fprintf_hists(struct rb_root *hists,
				   struct perf_session *pair,
				   bool show_displacement, FILE *fp,
				   u64 session_total);
#endif	/* __PERF_HIST_H */
