#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include "callchain.h"

extern struct callchain_param callchain_param;

struct hist_entry;
struct addr_location;
struct symbol;
struct rb_root;

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
	u64		ip[0];
};

struct sym_ext {
	struct rb_node	node;
	double		percent;
	char		*path;
};

struct sym_priv {
	struct sym_hist	*hist;
	struct sym_ext	*ext;
};

/*
 * The kernel collects the number of events it couldn't send in a stretch and
 * when possible sends this number in a PERF_RECORD_LOST event. The number of
 * such "chunks" of lost events is stored in .nr_events[PERF_EVENT_LOST] while
 * total_lost tells exactly how many events the kernel in fact lost, i.e. it is
 * the sum of all struct lost_event.lost fields reported.
 *
 * The total_period is needed because by default auto-freq is used, so
 * multipling nr_events[PERF_EVENT_SAMPLE] by a frequency isn't possible to get
 * the total number of low level events, it is necessary to to sum all struct
 * sample_event.period and stash the result in total_period.
 */
struct events_stats {
	u64 total_period;
	u64 total_lost;
	u32 nr_events[PERF_RECORD_HEADER_MAX];
	u32 nr_unknown_events;
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
				      struct symbol *parent, u64 period);
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

void hists__inc_nr_events(struct hists *self, u32 type);
size_t hists__fprintf_nr_events(struct hists *self, FILE *fp);

size_t hists__fprintf(struct hists *self, struct hists *pair,
		      bool show_displacement, FILE *fp);

int hist_entry__inc_addr_samples(struct hist_entry *self, u64 ip);
int hist_entry__annotate(struct hist_entry *self, struct list_head *head);

void hists__filter_by_dso(struct hists *self, const struct dso *dso);
void hists__filter_by_thread(struct hists *self, const struct thread *thread);

#ifdef NO_NEWT_SUPPORT
static inline int hists__browse(struct hists *self __used,
				const char *helpline __used,
				const char *ev_name __used)
{
	return 0;
}

static inline int hists__tui_browse_tree(struct rb_root *self __used,
					 const char *help __used)
{
	return 0;
}

static inline int hist_entry__tui_annotate(struct hist_entry *self __used)
{
	return 0;
}
#define KEY_LEFT -1
#define KEY_RIGHT -2
#else
#include <newt.h>
int hists__browse(struct hists *self, const char *helpline,
		  const char *ev_name);
int hist_entry__tui_annotate(struct hist_entry *self);

#define KEY_LEFT NEWT_KEY_LEFT
#define KEY_RIGHT NEWT_KEY_RIGHT

int hists__tui_browse_tree(struct rb_root *self, const char *help);
#endif
#endif	/* __PERF_HIST_H */
