#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include <pthread.h>
#include "callchain.h"

extern struct callchain_param callchain_param;

struct hist_entry;
struct addr_location;
struct symbol;

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
	u64 total_invalid_chains;
	u32 nr_events[PERF_RECORD_HEADER_MAX];
	u32 nr_lost_warned;
	u32 nr_unknown_events;
	u32 nr_invalid_chains;
	u32 nr_unknown_id;
};

enum hist_column {
	HISTC_SYMBOL,
	HISTC_DSO,
	HISTC_THREAD,
	HISTC_COMM,
	HISTC_PARENT,
	HISTC_CPU,
	HISTC_NR_COLS, /* Last entry */
};

struct thread;
struct dso;

struct hists {
	struct rb_root		entries_in_array[2];
	struct rb_root		*entries_in;
	struct rb_root		entries;
	struct rb_root		entries_collapsed;
	u64			nr_entries;
	const struct thread	*thread_filter;
	const struct dso	*dso_filter;
	pthread_mutex_t		lock;
	struct events_stats	stats;
	u64			event_stream;
	u16			col_len[HISTC_NR_COLS];
	/* Best would be to reuse the session callchain cursor */
	struct callchain_cursor	callchain_cursor;
};

struct hist_entry *__hists__add_entry(struct hists *self,
				      struct addr_location *al,
				      struct symbol *parent, u64 period);
extern int64_t hist_entry__cmp(struct hist_entry *, struct hist_entry *);
extern int64_t hist_entry__collapse(struct hist_entry *, struct hist_entry *);
int hist_entry__fprintf(struct hist_entry *he, size_t size, struct hists *hists,
			struct hists *pair_hists, bool show_displacement,
			long displacement, FILE *fp, u64 session_total);
int hist_entry__snprintf(struct hist_entry *self, char *bf, size_t size,
			 struct hists *hists);
void hist_entry__free(struct hist_entry *);

void hists__output_resort(struct hists *self);
void hists__output_resort_threaded(struct hists *hists);
void hists__collapse_resort(struct hists *self);
void hists__collapse_resort_threaded(struct hists *hists);

void hists__decay_entries(struct hists *hists, bool zap_user, bool zap_kernel);
void hists__decay_entries_threaded(struct hists *hists, bool zap_user,
				   bool zap_kernel);
void hists__output_recalc_col_len(struct hists *hists, int max_rows);

void hists__inc_nr_events(struct hists *self, u32 type);
size_t hists__fprintf_nr_events(struct hists *self, FILE *fp);

size_t hists__fprintf(struct hists *self, struct hists *pair,
		      bool show_displacement, bool show_header,
		      int max_rows, int max_cols, FILE *fp);

int hist_entry__inc_addr_samples(struct hist_entry *self, int evidx, u64 addr);
int hist_entry__annotate(struct hist_entry *self, size_t privsize);

void hists__filter_by_dso(struct hists *hists);
void hists__filter_by_thread(struct hists *hists);

u16 hists__col_len(struct hists *self, enum hist_column col);
void hists__set_col_len(struct hists *self, enum hist_column col, u16 len);
bool hists__new_col_len(struct hists *self, enum hist_column col, u16 len);

struct perf_evlist;

#ifdef NO_NEWT_SUPPORT
static inline
int perf_evlist__tui_browse_hists(struct perf_evlist *evlist __used,
				  const char *help __used,
				  void(*timer)(void *arg) __used,
				  void *arg __used,
				  int refresh __used)
{
	return 0;
}

static inline int hist_entry__tui_annotate(struct hist_entry *self __used,
					   int evidx __used,
					   int nr_events __used,
					   void(*timer)(void *arg) __used,
					   void *arg __used,
					   int delay_secs __used)
{
	return 0;
}
#define K_LEFT -1
#define K_RIGHT -2
#else
#include "ui/keysyms.h"
int hist_entry__tui_annotate(struct hist_entry *he, int evidx, int nr_events,
			     void(*timer)(void *arg), void *arg, int delay_secs);

int perf_evlist__tui_browse_hists(struct perf_evlist *evlist, const char *help,
				  void(*timer)(void *arg), void *arg,
				  int refresh);
#endif

unsigned int hists__sort_list_width(struct hists *self);

#endif	/* __PERF_HIST_H */
