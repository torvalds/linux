#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/types.h>
#include <pthread.h>
#include "callchain.h"
#include "evsel.h"
#include "header.h"
#include "color.h"
#include "ui/progress.h"

struct hist_entry;
struct addr_location;
struct symbol;

enum hist_filter {
	HIST_FILTER__DSO,
	HIST_FILTER__THREAD,
	HIST_FILTER__PARENT,
	HIST_FILTER__SYMBOL,
	HIST_FILTER__GUEST,
	HIST_FILTER__HOST,
	HIST_FILTER__SOCKET,
};

enum hist_column {
	HISTC_SYMBOL,
	HISTC_DSO,
	HISTC_THREAD,
	HISTC_COMM,
	HISTC_PARENT,
	HISTC_CPU,
	HISTC_SOCKET,
	HISTC_SRCLINE,
	HISTC_SRCFILE,
	HISTC_MISPREDICT,
	HISTC_IN_TX,
	HISTC_ABORT,
	HISTC_SYMBOL_FROM,
	HISTC_SYMBOL_TO,
	HISTC_DSO_FROM,
	HISTC_DSO_TO,
	HISTC_LOCAL_WEIGHT,
	HISTC_GLOBAL_WEIGHT,
	HISTC_MEM_DADDR_SYMBOL,
	HISTC_MEM_DADDR_DSO,
	HISTC_MEM_LOCKED,
	HISTC_MEM_TLB,
	HISTC_MEM_LVL,
	HISTC_MEM_SNOOP,
	HISTC_MEM_DCACHELINE,
	HISTC_MEM_IADDR_SYMBOL,
	HISTC_TRANSACTION,
	HISTC_CYCLES,
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
	u64			nr_non_filtered_entries;
	struct thread		*thread_filter;
	const struct dso	*dso_filter;
	const char		*uid_filter_str;
	const char		*symbol_filter_str;
	pthread_mutex_t		lock;
	struct events_stats	stats;
	u64			event_stream;
	u16			col_len[HISTC_NR_COLS];
	int			socket_filter;
};

struct hist_entry_iter;

struct hist_iter_ops {
	int (*prepare_entry)(struct hist_entry_iter *, struct addr_location *);
	int (*add_single_entry)(struct hist_entry_iter *, struct addr_location *);
	int (*next_entry)(struct hist_entry_iter *, struct addr_location *);
	int (*add_next_entry)(struct hist_entry_iter *, struct addr_location *);
	int (*finish_entry)(struct hist_entry_iter *, struct addr_location *);
};

struct hist_entry_iter {
	int total;
	int curr;

	bool hide_unresolved;
	int max_stack;

	struct perf_evsel *evsel;
	struct perf_sample *sample;
	struct hist_entry *he;
	struct symbol *parent;
	void *priv;

	const struct hist_iter_ops *ops;
	/* user-defined callback function (optional) */
	int (*add_entry_cb)(struct hist_entry_iter *iter,
			    struct addr_location *al, bool single, void *arg);
};

extern const struct hist_iter_ops hist_iter_normal;
extern const struct hist_iter_ops hist_iter_branch;
extern const struct hist_iter_ops hist_iter_mem;
extern const struct hist_iter_ops hist_iter_cumulative;

struct hist_entry *__hists__add_entry(struct hists *hists,
				      struct addr_location *al,
				      struct symbol *parent,
				      struct branch_info *bi,
				      struct mem_info *mi, u64 period,
				      u64 weight, u64 transaction,
				      bool sample_self);
int hist_entry_iter__add(struct hist_entry_iter *iter, struct addr_location *al,
			 int max_stack_depth, void *arg);

int64_t hist_entry__cmp(struct hist_entry *left, struct hist_entry *right);
int64_t hist_entry__collapse(struct hist_entry *left, struct hist_entry *right);
int hist_entry__transaction_len(void);
int hist_entry__sort_snprintf(struct hist_entry *he, char *bf, size_t size,
			      struct hists *hists);
void hist_entry__delete(struct hist_entry *he);

void hists__output_resort(struct hists *hists, struct ui_progress *prog);
void hists__collapse_resort(struct hists *hists, struct ui_progress *prog);

void hists__decay_entries(struct hists *hists, bool zap_user, bool zap_kernel);
void hists__delete_entries(struct hists *hists);
void hists__output_recalc_col_len(struct hists *hists, int max_rows);

u64 hists__total_period(struct hists *hists);
void hists__reset_stats(struct hists *hists);
void hists__inc_stats(struct hists *hists, struct hist_entry *h);
void hists__inc_nr_events(struct hists *hists, u32 type);
void hists__inc_nr_samples(struct hists *hists, bool filtered);
void events_stats__inc(struct events_stats *stats, u32 type);
size_t events_stats__fprintf(struct events_stats *stats, FILE *fp);

size_t hists__fprintf(struct hists *hists, bool show_header, int max_rows,
		      int max_cols, float min_pcnt, FILE *fp);
size_t perf_evlist__fprintf_nr_events(struct perf_evlist *evlist, FILE *fp);

void hists__filter_by_dso(struct hists *hists);
void hists__filter_by_thread(struct hists *hists);
void hists__filter_by_symbol(struct hists *hists);
void hists__filter_by_socket(struct hists *hists);

static inline bool hists__has_filter(struct hists *hists)
{
	return hists->thread_filter || hists->dso_filter ||
		hists->symbol_filter_str || (hists->socket_filter > -1);
}

u16 hists__col_len(struct hists *hists, enum hist_column col);
void hists__set_col_len(struct hists *hists, enum hist_column col, u16 len);
bool hists__new_col_len(struct hists *hists, enum hist_column col, u16 len);
void hists__reset_col_len(struct hists *hists);
void hists__calc_col_len(struct hists *hists, struct hist_entry *he);

void hists__match(struct hists *leader, struct hists *other);
int hists__link(struct hists *leader, struct hists *other);

struct hists_evsel {
	struct perf_evsel evsel;
	struct hists	  hists;
};

static inline struct perf_evsel *hists_to_evsel(struct hists *hists)
{
	struct hists_evsel *hevsel = container_of(hists, struct hists_evsel, hists);
	return &hevsel->evsel;
}

static inline struct hists *evsel__hists(struct perf_evsel *evsel)
{
	struct hists_evsel *hevsel = (struct hists_evsel *)evsel;
	return &hevsel->hists;
}

int hists__init(void);

struct perf_hpp {
	char *buf;
	size_t size;
	const char *sep;
	void *ptr;
};

struct perf_hpp_fmt {
	const char *name;
	int (*header)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct perf_evsel *evsel);
	int (*width)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct perf_evsel *evsel);
	int (*color)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	int (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	int64_t (*cmp)(struct perf_hpp_fmt *fmt,
		       struct hist_entry *a, struct hist_entry *b);
	int64_t (*collapse)(struct perf_hpp_fmt *fmt,
			    struct hist_entry *a, struct hist_entry *b);
	int64_t (*sort)(struct perf_hpp_fmt *fmt,
			struct hist_entry *a, struct hist_entry *b);

	struct list_head list;
	struct list_head sort_list;
	bool elide;
	int len;
	int user_len;
};

extern struct list_head perf_hpp__list;
extern struct list_head perf_hpp__sort_list;

#define perf_hpp__for_each_format(format) \
	list_for_each_entry(format, &perf_hpp__list, list)

#define perf_hpp__for_each_format_safe(format, tmp)	\
	list_for_each_entry_safe(format, tmp, &perf_hpp__list, list)

#define perf_hpp__for_each_sort_list(format) \
	list_for_each_entry(format, &perf_hpp__sort_list, sort_list)

#define perf_hpp__for_each_sort_list_safe(format, tmp)	\
	list_for_each_entry_safe(format, tmp, &perf_hpp__sort_list, sort_list)

extern struct perf_hpp_fmt perf_hpp__format[];

enum {
	/* Matches perf_hpp__format array. */
	PERF_HPP__OVERHEAD,
	PERF_HPP__OVERHEAD_SYS,
	PERF_HPP__OVERHEAD_US,
	PERF_HPP__OVERHEAD_GUEST_SYS,
	PERF_HPP__OVERHEAD_GUEST_US,
	PERF_HPP__OVERHEAD_ACC,
	PERF_HPP__SAMPLES,
	PERF_HPP__PERIOD,

	PERF_HPP__MAX_INDEX
};

void perf_hpp__init(void);
void perf_hpp__column_register(struct perf_hpp_fmt *format);
void perf_hpp__column_unregister(struct perf_hpp_fmt *format);
void perf_hpp__column_enable(unsigned col);
void perf_hpp__column_disable(unsigned col);
void perf_hpp__cancel_cumulate(void);

void perf_hpp__register_sort_field(struct perf_hpp_fmt *format);
void perf_hpp__setup_output_field(void);
void perf_hpp__reset_output_field(void);
void perf_hpp__append_sort_keys(void);

bool perf_hpp__is_sort_entry(struct perf_hpp_fmt *format);
bool perf_hpp__same_sort_entry(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b);

static inline bool perf_hpp__should_skip(struct perf_hpp_fmt *format)
{
	return format->elide;
}

void perf_hpp__reset_width(struct perf_hpp_fmt *fmt, struct hists *hists);
void perf_hpp__reset_sort_width(struct perf_hpp_fmt *fmt, struct hists *hists);
void perf_hpp__set_user_width(const char *width_list_str);

typedef u64 (*hpp_field_fn)(struct hist_entry *he);
typedef int (*hpp_callback_fn)(struct perf_hpp *hpp, bool front);
typedef int (*hpp_snprint_fn)(struct perf_hpp *hpp, const char *fmt, ...);

int hpp__fmt(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he, hpp_field_fn get_field,
	     const char *fmtstr, hpp_snprint_fn print_fn, bool fmt_percent);
int hpp__fmt_acc(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		 struct hist_entry *he, hpp_field_fn get_field,
		 const char *fmtstr, hpp_snprint_fn print_fn, bool fmt_percent);

static inline void advance_hpp(struct perf_hpp *hpp, int inc)
{
	hpp->buf  += inc;
	hpp->size -= inc;
}

static inline size_t perf_hpp__use_color(void)
{
	return !symbol_conf.field_sep;
}

static inline size_t perf_hpp__color_overhead(void)
{
	return perf_hpp__use_color() ?
	       (COLOR_MAXLEN + sizeof(PERF_COLOR_RESET)) * PERF_HPP__MAX_INDEX
	       : 0;
}

struct perf_evlist;

struct hist_browser_timer {
	void (*timer)(void *arg);
	void *arg;
	int refresh;
};

#ifdef HAVE_SLANG_SUPPORT
#include "../ui/keysyms.h"
int map_symbol__tui_annotate(struct map_symbol *ms, struct perf_evsel *evsel,
			     struct hist_browser_timer *hbt);

int hist_entry__tui_annotate(struct hist_entry *he, struct perf_evsel *evsel,
			     struct hist_browser_timer *hbt);

int perf_evlist__tui_browse_hists(struct perf_evlist *evlist, const char *help,
				  struct hist_browser_timer *hbt,
				  float min_pcnt,
				  struct perf_env *env);
int script_browse(const char *script_opt);
#else
static inline
int perf_evlist__tui_browse_hists(struct perf_evlist *evlist __maybe_unused,
				  const char *help __maybe_unused,
				  struct hist_browser_timer *hbt __maybe_unused,
				  float min_pcnt __maybe_unused,
				  struct perf_env *env __maybe_unused)
{
	return 0;
}
static inline int map_symbol__tui_annotate(struct map_symbol *ms __maybe_unused,
					   struct perf_evsel *evsel __maybe_unused,
					   struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}

static inline int hist_entry__tui_annotate(struct hist_entry *he __maybe_unused,
					   struct perf_evsel *evsel __maybe_unused,
					   struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}

static inline int script_browse(const char *script_opt __maybe_unused)
{
	return 0;
}

#define K_LEFT  -1000
#define K_RIGHT -2000
#define K_SWITCH_INPUT_DATA -3000
#endif

unsigned int hists__sort_list_width(struct hists *hists);

void hist__account_cycles(struct branch_stack *bs, struct addr_location *al,
			  struct perf_sample *sample, bool nonany_branch_mode);

struct option;
int parse_filter_percentage(const struct option *opt __maybe_unused,
			    const char *arg, int unset __maybe_unused);
int perf_hist_config(const char *var, const char *value);

#endif	/* __PERF_HIST_H */
