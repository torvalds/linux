/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_HIST_H
#define __PERF_HIST_H

#include <linux/rbtree.h>
#include <linux/types.h>
#include "callchain.h"
#include "color.h"
#include "events_stats.h"
#include "evsel.h"
#include "map_symbol.h"
#include "mem-events.h"
#include "mutex.h"
#include "sample.h"
#include "spark.h"
#include "stat.h"

struct addr_location;
struct mem_info;
struct kvm_info;
struct branch_info;
struct branch_stack;
struct block_info;
struct ui_progress;

enum hist_filter {
	HIST_FILTER__DSO,
	HIST_FILTER__THREAD,
	HIST_FILTER__PARENT,
	HIST_FILTER__SYMBOL,
	HIST_FILTER__GUEST,
	HIST_FILTER__HOST,
	HIST_FILTER__SOCKET,
	HIST_FILTER__C2C,
	HIST_FILTER__PARALLELISM,
};

typedef u16 filter_mask_t;

enum hist_column {
	HISTC_SYMBOL,
	HISTC_TIME,
	HISTC_DSO,
	HISTC_THREAD,
	HISTC_TGID,
	HISTC_COMM,
	HISTC_CGROUP_ID,
	HISTC_CGROUP,
	HISTC_PARENT,
	HISTC_PARALLELISM,
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
	HISTC_CODE_PAGE_SIZE,
	HISTC_MEM_DADDR_SYMBOL,
	HISTC_MEM_DADDR_DSO,
	HISTC_MEM_PHYS_DADDR,
	HISTC_MEM_DATA_PAGE_SIZE,
	HISTC_MEM_LOCKED,
	HISTC_MEM_TLB,
	HISTC_MEM_LVL,
	HISTC_MEM_SNOOP,
	HISTC_MEM_DCACHELINE,
	HISTC_MEM_IADDR_SYMBOL,
	HISTC_TRANSACTION,
	HISTC_CYCLES,
	HISTC_SRCLINE_FROM,
	HISTC_SRCLINE_TO,
	HISTC_TRACE,
	HISTC_SYM_SIZE,
	HISTC_DSO_SIZE,
	HISTC_SYMBOL_IPC,
	HISTC_MEM_BLOCKED,
	HISTC_LOCAL_INS_LAT,
	HISTC_GLOBAL_INS_LAT,
	HISTC_LOCAL_P_STAGE_CYC,
	HISTC_GLOBAL_P_STAGE_CYC,
	HISTC_ADDR_FROM,
	HISTC_ADDR_TO,
	HISTC_ADDR,
	HISTC_SIMD,
	HISTC_TYPE,
	HISTC_TYPE_OFFSET,
	HISTC_SYMBOL_OFFSET,
	HISTC_TYPE_CACHELINE,
	HISTC_CALLCHAIN_BRANCH_PREDICTED,
	HISTC_CALLCHAIN_BRANCH_ABORT,
	HISTC_CALLCHAIN_BRANCH_CYCLES,
	HISTC_NR_COLS, /* Last entry */
};

struct thread;
struct dso;

#define MEM_STAT_LEN  8

struct he_mem_stat {
	/* meaning of entries depends on enum mem_stat_type */
	u64			entries[MEM_STAT_LEN];
};

struct hists {
	struct rb_root_cached	entries_in_array[2];
	struct rb_root_cached	*entries_in;
	struct rb_root_cached	entries;
	struct rb_root_cached	entries_collapsed;
	u64			nr_entries;
	u64			nr_non_filtered_entries;
	u64			callchain_period;
	u64			callchain_non_filtered_period;
	u64			callchain_latency;
	u64			callchain_non_filtered_latency;
	struct thread		*thread_filter;
	const struct dso	*dso_filter;
	const char		*uid_filter_str;
	const char		*symbol_filter_str;
	unsigned long		*parallelism_filter;
	struct mutex		lock;
	struct hists_stats	stats;
	u64			event_stream;
	u16			col_len[HISTC_NR_COLS];
	bool			has_callchains;
	int			socket_filter;
	struct perf_hpp_list	*hpp_list;
	struct list_head	hpp_formats;
	int			nr_hpp_node;
	int			nr_mem_stats;
	enum mem_stat_type	*mem_stat_types;
	struct he_mem_stat	*mem_stat_total;
};

#define hists__has(__h, __f) (__h)->hpp_list->__f

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

	struct evsel *evsel;
	struct perf_sample *sample;
	struct hist_entry *he;
	struct symbol *parent;

	struct mem_info *mi;
	struct branch_info *bi;
	struct hist_entry **he_cache;

	const struct hist_iter_ops *ops;
	/* user-defined callback function (optional) */
	int (*add_entry_cb)(struct hist_entry_iter *iter,
			    struct addr_location *al, bool single, void *arg);
	bool hide_unresolved;
};

extern const struct hist_iter_ops hist_iter_normal;
extern const struct hist_iter_ops hist_iter_branch;
extern const struct hist_iter_ops hist_iter_mem;
extern const struct hist_iter_ops hist_iter_cumulative;

struct res_sample {
	u64 time;
	int cpu;
	int tid;
};

struct he_stat {
	u64			period;
	/*
	 * Period re-scaled from CPU time to wall-clock time (divided by the
	 * parallelism at the time of the sample). This represents effect of
	 * the event on latency rather than CPU consumption.
	 */
	u64			latency;
	u64			period_sys;
	u64			period_us;
	u64			period_guest_sys;
	u64			period_guest_us;
	u64			weight1;
	u64			weight2;
	u64			weight3;
	u32			nr_events;
};

struct namespace_id {
	u64			dev;
	u64			ino;
};

struct hist_entry_diff {
	bool	computed;
	union {
		/* PERF_HPP__DELTA */
		double	period_ratio_delta;

		/* PERF_HPP__RATIO */
		double	period_ratio;

		/* HISTC_WEIGHTED_DIFF */
		s64	wdiff;

		/* PERF_HPP_DIFF__CYCLES */
		s64	cycles;
	};
	struct stats	stats;
	unsigned long	svals[NUM_SPARKS];
};

struct hist_entry_ops {
	void	*(*new)(size_t size);
	void	(*free)(void *ptr);
};

/**
 * struct hist_entry - histogram entry
 *
 * @row_offset - offset from the first callchain expanded to appear on screen
 * @nr_rows - rows expanded in callchain, recalculated on folding/unfolding
 */
struct hist_entry {
	struct rb_node		rb_node_in;
	struct rb_node		rb_node;
	union {
		struct list_head node;
		struct list_head head;
	} pairs;
	struct he_stat		stat;
	struct he_stat		*stat_acc;
	struct he_mem_stat	*mem_stat;
	struct map_symbol	ms;
	struct thread		*thread;
	struct comm		*comm;
	struct namespace_id	cgroup_id;
	u64			cgroup;
	u64			ip;
	u64			transaction;
	u64			code_page_size;
	u64			weight;
	u64			ins_lat;
	/** @weight3: On x86 holds retire_lat, on powerpc holds p_stage_cyc. */
	u64			weight3;
	s32			socket;
	s32			cpu;
	int			parallelism;
	int			mem_type_off;
	u8			cpumode;
	u8			depth;
	struct simd_flags	simd_flags;

	/* We are added by hists__add_dummy_entry. */
	bool			dummy;
	bool			leaf;

	char			level;
	filter_mask_t		filtered;

	u16			callchain_size;
	union {
		/*
		 * Since perf diff only supports the stdio output, TUI
		 * fields are only accessed from perf report (or perf
		 * top).  So make it a union to reduce memory usage.
		 */
		struct hist_entry_diff	diff;
		struct /* for TUI */ {
			u16	row_offset;
			u16	nr_rows;
			bool	init_have_children;
			bool	unfolded;
			bool	has_children;
			bool	has_no_entry;
		};
	};
	char			*srcline;
	char			*srcfile;
	struct symbol		*parent;
	struct branch_info	*branch_info;
	long			time;
	struct hists		*hists;
	struct mem_info		*mem_info;
	struct block_info	*block_info;
	struct kvm_info		*kvm_info;
	void			*raw_data;
	u32			raw_size;
	int			num_res;
	struct res_sample	*res_samples;
	void			*trace_output;
	struct perf_hpp_list	*hpp_list;
	struct hist_entry	*parent_he;
	struct hist_entry_ops	*ops;
	struct annotated_data_type *mem_type;
	union {
		/* this is for hierarchical entry structure */
		struct {
			struct rb_root_cached	hroot_in;
			struct rb_root_cached   hroot_out;
		};				/* non-leaf entries */
		struct rb_root	sorted_chain;	/* leaf entry has callchains */
	};
	struct callchain_root	callchain[0]; /* must be last member */
};

static __pure inline bool hist_entry__has_callchains(struct hist_entry *he)
{
	return he->callchain_size != 0;
}

static inline bool hist_entry__has_pairs(struct hist_entry *he)
{
	return !list_empty(&he->pairs.node);
}

static inline struct hist_entry *hist_entry__next_pair(struct hist_entry *he)
{
	if (hist_entry__has_pairs(he))
		return list_entry(he->pairs.node.next, struct hist_entry, pairs.node);
	return NULL;
}

static inline void hist_entry__add_pair(struct hist_entry *pair,
					struct hist_entry *he)
{
	list_add_tail(&pair->pairs.node, &he->pairs.head);
}

struct hist_entry *hists__add_entry(struct hists *hists,
				    struct addr_location *al,
				    struct symbol *parent,
				    struct branch_info *bi,
				    struct mem_info *mi,
				    struct kvm_info *ki,
				    struct perf_sample *sample,
				    bool sample_self);

struct hist_entry *hists__add_entry_ops(struct hists *hists,
					struct hist_entry_ops *ops,
					struct addr_location *al,
					struct symbol *sym_parent,
					struct branch_info *bi,
					struct mem_info *mi,
					struct kvm_info *ki,
					struct perf_sample *sample,
					bool sample_self);

struct hist_entry *hists__add_entry_block(struct hists *hists,
					  struct addr_location *al,
					  struct block_info *bi);

int hist_entry_iter__add(struct hist_entry_iter *iter, struct addr_location *al,
			 int max_stack_depth, void *arg);

struct perf_hpp;
struct perf_hpp_fmt;

int hist_entry__transaction_len(void);
int hist_entry__sort_snprintf(struct hist_entry *he, char *bf, size_t size,
			      struct hists *hists);
int hist_entry__snprintf_alignment(struct hist_entry *he, struct perf_hpp *hpp,
				   struct perf_hpp_fmt *fmt, int printed);
int hist_entry__sym_snprintf(struct hist_entry *he, char *bf, size_t size,
			     unsigned int width);
void hist_entry__delete(struct hist_entry *he);

typedef int (*hists__resort_cb_t)(struct hist_entry *he, void *arg);

void evsel__output_resort_cb(struct evsel *evsel, struct ui_progress *prog,
			     hists__resort_cb_t cb, void *cb_arg);
void evsel__output_resort(struct evsel *evsel, struct ui_progress *prog);
void hists__output_resort(struct hists *hists, struct ui_progress *prog);
void hists__output_resort_cb(struct hists *hists, struct ui_progress *prog,
			     hists__resort_cb_t cb);
int hists__collapse_resort(struct hists *hists, struct ui_progress *prog);

void hists__decay_entries(struct hists *hists, bool zap_user, bool zap_kernel);
void hists__delete_entries(struct hists *hists);
void hists__output_recalc_col_len(struct hists *hists, int max_rows);

struct hist_entry *hists__get_entry(struct hists *hists, int idx);

u64 hists__total_period(struct hists *hists);
u64 hists__total_latency(struct hists *hists);
void hists__reset_stats(struct hists *hists);
void hists__inc_stats(struct hists *hists, struct hist_entry *h);
void hists__inc_nr_events(struct hists *hists);
void hists__inc_nr_samples(struct hists *hists, bool filtered);
void hists__inc_nr_lost_samples(struct hists *hists, u32 lost);
void hists__inc_nr_dropped_samples(struct hists *hists, u32 lost);

size_t hists__fprintf(struct hists *hists, bool show_header, int max_rows,
		      int max_cols, float min_pcnt, FILE *fp,
		      bool ignore_callchains);
size_t evlist__fprintf_nr_events(struct evlist *evlist, FILE *fp);

void hists__filter_by_dso(struct hists *hists);
void hists__filter_by_thread(struct hists *hists);
void hists__filter_by_symbol(struct hists *hists);
void hists__filter_by_socket(struct hists *hists);
void hists__filter_by_parallelism(struct hists *hists);

static inline bool hists__has_filter(struct hists *hists)
{
	return hists->thread_filter || hists->dso_filter ||
		hists->symbol_filter_str || (hists->socket_filter > -1) ||
		hists->parallelism_filter;
}

u16 hists__col_len(struct hists *hists, enum hist_column col);
void hists__set_col_len(struct hists *hists, enum hist_column col, u16 len);
bool hists__new_col_len(struct hists *hists, enum hist_column col, u16 len);
void hists__reset_col_len(struct hists *hists);
void hists__calc_col_len(struct hists *hists, struct hist_entry *he);

void hists__match(struct hists *leader, struct hists *other);
int hists__link(struct hists *leader, struct hists *other);
int hists__unlink(struct hists *hists);

static inline float hist_entry__get_percent_limit(struct hist_entry *he)
{
	u64 period = he->stat.period;
	u64 total_period = hists__total_period(he->hists);

	if (unlikely(total_period == 0))
		return 0;

	if (symbol_conf.cumulate_callchain)
		period = he->stat_acc->period;

	return period * 100.0 / total_period;
}

struct hists_evsel {
	struct evsel evsel;
	struct hists	  hists;
};

static inline struct evsel *hists_to_evsel(struct hists *hists)
{
	struct hists_evsel *hevsel = container_of(hists, struct hists_evsel, hists);
	return &hevsel->evsel;
}

static inline struct hists *evsel__hists(struct evsel *evsel)
{
	struct hists_evsel *hevsel = (struct hists_evsel *)evsel;
	return &hevsel->hists;
}

static __pure inline bool hists__has_callchains(struct hists *hists)
{
	return hists->has_callchains;
}

int hists__init(void);
int __hists__init(struct hists *hists, struct perf_hpp_list *hpp_list);

struct rb_root_cached *hists__get_rotate_entries_in(struct hists *hists);

struct perf_hpp {
	char *buf;
	size_t size;
	const char *sep;
	void *ptr;
	bool skip;
};

typedef int64_t (*perf_hpp_fmt_cmp_t)(
	struct perf_hpp_fmt *, struct hist_entry *, struct hist_entry *);

struct perf_hpp_fmt {
	const char *name;
	int (*header)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hists *hists, int line, int *span);
	int (*width)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hists *hists);
	void (*init)(struct perf_hpp_fmt *fmt, struct hist_entry *he);
	int (*color)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	int (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	perf_hpp_fmt_cmp_t cmp;
	perf_hpp_fmt_cmp_t collapse;
	perf_hpp_fmt_cmp_t sort;
	bool (*equal)(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b);
	void (*free)(struct perf_hpp_fmt *fmt);

	struct list_head list;
	struct list_head sort_list;
	bool elide;
	int len;
	int user_len;
	int idx;
	int level;
};

struct perf_hpp_list {
	struct list_head fields;
	struct list_head sorts;

	int nr_header_lines;
	int need_collapse;
	int parent;
	int sym;
	int dso;
	int socket;
	int thread;
	int comm;
};

extern struct perf_hpp_list perf_hpp_list;

struct perf_hpp_list_node {
	struct list_head	list;
	struct perf_hpp_list	hpp;
	int			level;
	bool			skip;
};

void perf_hpp_list__column_register(struct perf_hpp_list *list,
				    struct perf_hpp_fmt *format);
void perf_hpp_list__register_sort_field(struct perf_hpp_list *list,
					struct perf_hpp_fmt *format);
void perf_hpp_list__prepend_sort_field(struct perf_hpp_list *list,
				       struct perf_hpp_fmt *format);

static inline void perf_hpp__column_register(struct perf_hpp_fmt *format)
{
	perf_hpp_list__column_register(&perf_hpp_list, format);
}

static inline void perf_hpp__register_sort_field(struct perf_hpp_fmt *format)
{
	perf_hpp_list__register_sort_field(&perf_hpp_list, format);
}

static inline void perf_hpp__prepend_sort_field(struct perf_hpp_fmt *format)
{
	perf_hpp_list__prepend_sort_field(&perf_hpp_list, format);
}

#define perf_hpp_list__for_each_format(_list, format) \
	list_for_each_entry(format, &(_list)->fields, list)

#define perf_hpp_list__for_each_format_safe(_list, format, tmp)	\
	list_for_each_entry_safe(format, tmp, &(_list)->fields, list)

#define perf_hpp_list__for_each_sort_list(_list, format) \
	list_for_each_entry(format, &(_list)->sorts, sort_list)

#define perf_hpp_list__for_each_sort_list_safe(_list, format, tmp)	\
	list_for_each_entry_safe(format, tmp, &(_list)->sorts, sort_list)

#define hists__for_each_format(hists, format) \
	perf_hpp_list__for_each_format((hists)->hpp_list, format)

#define hists__for_each_sort_list(hists, format) \
	perf_hpp_list__for_each_sort_list((hists)->hpp_list, format)

extern struct perf_hpp_fmt perf_hpp__format[];

enum {
	/* Matches perf_hpp__format array. */
	PERF_HPP__OVERHEAD,
	PERF_HPP__LATENCY,
	PERF_HPP__OVERHEAD_SYS,
	PERF_HPP__OVERHEAD_US,
	PERF_HPP__OVERHEAD_GUEST_SYS,
	PERF_HPP__OVERHEAD_GUEST_US,
	PERF_HPP__OVERHEAD_ACC,
	PERF_HPP__LATENCY_ACC,
	PERF_HPP__SAMPLES,
	PERF_HPP__PERIOD,
	PERF_HPP__WEIGHT1,
	PERF_HPP__WEIGHT2,
	PERF_HPP__WEIGHT3,
	PERF_HPP__MEM_STAT_OP,
	PERF_HPP__MEM_STAT_CACHE,
	PERF_HPP__MEM_STAT_MEMORY,
	PERF_HPP__MEM_STAT_SNOOP,
	PERF_HPP__MEM_STAT_DTLB,

	PERF_HPP__MAX_INDEX
};

void perf_hpp__init(void);
void perf_hpp__cancel_cumulate(struct evlist *evlist);
void perf_hpp__cancel_latency(struct evlist *evlist);
void perf_hpp__setup_output_field(struct perf_hpp_list *list);
void perf_hpp__reset_output_field(struct perf_hpp_list *list);
void perf_hpp__append_sort_keys(struct perf_hpp_list *list);
int perf_hpp__setup_hists_formats(struct perf_hpp_list *list,
				  struct evlist *evlist);
int perf_hpp__alloc_mem_stats(struct perf_hpp_list *list,
			      struct evlist *evlist);


bool perf_hpp__is_sort_entry(struct perf_hpp_fmt *format);
bool perf_hpp__is_dynamic_entry(struct perf_hpp_fmt *format);
bool perf_hpp__defined_dynamic_entry(struct perf_hpp_fmt *fmt, struct hists *hists);
bool perf_hpp__is_trace_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_srcline_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_srcfile_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_thread_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_comm_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_dso_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_sym_entry(struct perf_hpp_fmt *fmt);
bool perf_hpp__is_parallelism_entry(struct perf_hpp_fmt *fmt);

struct perf_hpp_fmt *perf_hpp_fmt__dup(struct perf_hpp_fmt *fmt);

int hist_entry__filter(struct hist_entry *he, int type, const void *arg);

static inline bool perf_hpp__should_skip(struct perf_hpp_fmt *format,
					 struct hists *hists)
{
	if (format->elide)
		return true;

	if (perf_hpp__is_dynamic_entry(format) &&
	    !perf_hpp__defined_dynamic_entry(format, hists))
		return true;

	return false;
}

void perf_hpp__reset_width(struct perf_hpp_fmt *fmt, struct hists *hists);
void perf_hpp__reset_sort_width(struct perf_hpp_fmt *fmt, struct hists *hists);
void perf_hpp__set_user_width(const char *width_list_str);
void hists__reset_column_width(struct hists *hists);

enum perf_hpp_fmt_type {
	PERF_HPP_FMT_TYPE__RAW,
	PERF_HPP_FMT_TYPE__PERCENT,
	PERF_HPP_FMT_TYPE__LATENCY,
	PERF_HPP_FMT_TYPE__AVERAGE,
};

typedef u64 (*hpp_field_fn)(struct hist_entry *he);
typedef int (*hpp_callback_fn)(struct perf_hpp *hpp, bool front);
typedef int (*hpp_snprint_fn)(struct perf_hpp *hpp, const char *fmt, ...);

int hpp__fmt(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he, hpp_field_fn get_field,
	     const char *fmtstr, hpp_snprint_fn print_fn,
	     enum perf_hpp_fmt_type fmtype);
int hpp__fmt_acc(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		 struct hist_entry *he, hpp_field_fn get_field,
		 const char *fmtstr, hpp_snprint_fn print_fn,
		 enum perf_hpp_fmt_type fmtype);
int hpp__fmt_mem_stat(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hist_entry *he, enum mem_stat_type mst,
		      const char *fmtstr, hpp_snprint_fn print_fn);

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

struct evlist;

struct hist_browser_timer {
	void (*timer)(void *arg);
	void *arg;
	int refresh;
};

enum rstype {
	A_NORMAL,
	A_ASM,
	A_SOURCE
};

struct block_hist {
	struct hists		block_hists;
	struct perf_hpp_list	block_list;
	struct perf_hpp_fmt	block_fmt;
	int			block_idx;
	bool			valid;
	struct hist_entry	he;
};

#ifdef HAVE_SLANG_SUPPORT
#include "../ui/keysyms.h"
void attr_to_script(char *buf, struct perf_event_attr *attr);

int __hist_entry__tui_annotate(struct hist_entry *he, struct map_symbol *ms,
			       struct evsel *evsel,
			       struct hist_browser_timer *hbt);

int hist_entry__tui_annotate(struct hist_entry *he, struct evsel *evsel,
			     struct hist_browser_timer *hbt);

int evlist__tui_browse_hists(struct evlist *evlist, const char *help, struct hist_browser_timer *hbt,
			     float min_pcnt, struct perf_env *env, bool warn_lost_event);

int script_browse(const char *script_opt, struct evsel *evsel);

void run_script(char *cmd);
int res_sample_browse(struct res_sample *res_samples, int num_res,
		      struct evsel *evsel, enum rstype rstype);
void res_sample_init(void);

int block_hists_tui_browse(struct block_hist *bh, struct evsel *evsel,
			   float min_percent, struct perf_env *env);
#else
static inline
int evlist__tui_browse_hists(struct evlist *evlist __maybe_unused,
			     const char *help __maybe_unused,
			     struct hist_browser_timer *hbt __maybe_unused,
			     float min_pcnt __maybe_unused,
			     struct perf_env *env __maybe_unused,
			     bool warn_lost_event __maybe_unused)
{
	return 0;
}
static inline int __hist_entry__tui_annotate(struct hist_entry *he __maybe_unused,
					     struct map_symbol *ms __maybe_unused,
					     struct evsel *evsel __maybe_unused,
					     struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}

static inline int hist_entry__tui_annotate(struct hist_entry *he __maybe_unused,
					   struct evsel *evsel __maybe_unused,
					   struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}

static inline int script_browse(const char *script_opt __maybe_unused,
				struct evsel *evsel __maybe_unused)
{
	return 0;
}

static inline int res_sample_browse(struct res_sample *res_samples __maybe_unused,
				    int num_res __maybe_unused,
				    struct evsel *evsel __maybe_unused,
				    enum rstype rstype __maybe_unused)
{
	return 0;
}

static inline void res_sample_init(void) {}

static inline int block_hists_tui_browse(struct block_hist *bh __maybe_unused,
					 struct evsel *evsel __maybe_unused,
					 float min_percent __maybe_unused,
					 struct perf_env *env __maybe_unused)
{
	return 0;
}

#define K_LEFT  -1000
#define K_RIGHT -2000
#define K_SWITCH_INPUT_DATA -3000
#define K_RELOAD -4000
#endif

unsigned int hists__sort_list_width(struct hists *hists);
unsigned int hists__overhead_width(struct hists *hists);

void hist__account_cycles(struct branch_stack *bs, struct addr_location *al,
			  struct perf_sample *sample, bool nonany_branch_mode,
			  u64 *total_cycles, struct evsel *evsel);

struct option;
int parse_filter_percentage(const struct option *opt, const char *arg, int unset);
int perf_hist_config(const char *var, const char *value);

void perf_hpp_list__init(struct perf_hpp_list *list);

enum hierarchy_move_dir {
	HMD_NORMAL,
	HMD_FORCE_SIBLING,
	HMD_FORCE_CHILD,
};

struct rb_node *rb_hierarchy_last(struct rb_node *node);
struct rb_node *__rb_hierarchy_next(struct rb_node *node,
				    enum hierarchy_move_dir hmd);
struct rb_node *rb_hierarchy_prev(struct rb_node *node);

static inline struct rb_node *rb_hierarchy_next(struct rb_node *node)
{
	return __rb_hierarchy_next(node, HMD_NORMAL);
}

#define HIERARCHY_INDENT  3

bool hist_entry__has_hierarchy_children(struct hist_entry *he, float limit);
int hpp_color_scnprintf(struct perf_hpp *hpp, const char *fmt, ...);
int __hpp__slsmg_color_printf(struct perf_hpp *hpp, const char *fmt, ...);
int __hist_entry__snprintf(struct hist_entry *he, struct perf_hpp *hpp,
			   struct perf_hpp_list *hpp_list);
int hists__fprintf_headers(struct hists *hists, FILE *fp);
int __hists__scnprintf_title(struct hists *hists, char *bf, size_t size, bool show_freq);

static inline int hists__scnprintf_title(struct hists *hists, char *bf, size_t size)
{
	return __hists__scnprintf_title(hists, bf, size, true);
}

#endif	/* __PERF_HIST_H */
