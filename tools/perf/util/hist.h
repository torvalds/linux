#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include "callchain.h"

extern struct callchain_param callchain_param;

struct hist_entry;
struct addr_location;
struct symbol;
struct rb_root;

struct events_stats {
	u64 total;
	u64 lost;
};

struct hists {
	struct rb_node		rb_node;
	struct rb_root		entries;
	u64			nr_entries;
	struct events_stats	stats;
	u64			config;
	u64			event_stream;
	u32			type;
	u32			max_sym_namelen;
};

struct hist_entry *__hists__add_entry(struct hists *self,
				      struct addr_location *al,
				      struct symbol *parent, u64 count);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
int hist_entry__fprintf(struct hist_entry *self, struct hists *pair_hists,
			bool show_displacement, long displacement, FILE *fp,
			u64 total);
int hist_entry__snprintf(struct hist_entry *self, char *bf, size_t size,
			 struct hists *pair_hists, bool show_displacement,
			 long displacement, bool color, u64 total);
void hist_entry__free(struct hist_entry *);

void hists__output_resort(struct hists *self);
void hists__collapse_resort(struct hists *self);
size_t hists__fprintf(struct hists *self, struct hists *pair,
		      bool show_displacement, FILE *fp);
#endif	/* __PERF_HIST_H */
