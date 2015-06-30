#ifndef __PERF_EVSEL_H
#define __PERF_EVSEL_H 1

#include <linux/list.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/perf_event.h>
#include <linux/types.h>
#include "xyarray.h"
#include "symbol.h"
#include "cpumap.h"
#include "stat.h"

struct perf_evsel;

/*
 * Per fd, to map back from PERF_SAMPLE_ID to evsel, only used when there are
 * more than one entry in the evlist.
 */
struct perf_sample_id {
	struct hlist_node 	node;
	u64		 	id;
	struct perf_evsel	*evsel;
	int			idx;
	int			cpu;
	pid_t			tid;

	/* Holds total ID period value for PERF_SAMPLE_READ processing. */
	u64			period;
};

struct cgroup_sel;

/** struct perf_evsel - event selector
 *
 * @name - Can be set to retain the original event name passed by the user,
 *         so that when showing results in tools such as 'perf stat', we
 *         show the name used, not some alias.
 * @id_pos: the position of the event id (PERF_SAMPLE_ID or
 *          PERF_SAMPLE_IDENTIFIER) in a sample event i.e. in the array of
 *          struct sample_event
 * @is_pos: the position (counting backwards) of the event id (PERF_SAMPLE_ID or
 *          PERF_SAMPLE_IDENTIFIER) in a non-sample event i.e. if sample_id_all
 *          is used there is an id sample appended to non-sample events
 * @priv:   And what is in its containing unnamed union are tool specific
 */
struct perf_evsel {
	struct list_head	node;
	struct perf_event_attr	attr;
	char			*filter;
	struct xyarray		*fd;
	struct xyarray		*sample_id;
	u64			*id;
	struct perf_counts	*counts;
	struct perf_counts	*prev_raw_counts;
	int			idx;
	u32			ids;
	char			*name;
	double			scale;
	const char		*unit;
	struct event_format	*tp_format;
	union {
		void		*priv;
		off_t		id_offset;
		u64		db_id;
	};
	struct cgroup_sel	*cgrp;
	void			*handler;
	struct cpu_map		*cpus;
	struct thread_map	*threads;
	unsigned int		sample_size;
	int			id_pos;
	int			is_pos;
	bool			snapshot;
	bool 			supported;
	bool 			needs_swap;
	bool			no_aux_samples;
	bool			immediate;
	bool			system_wide;
	bool			tracking;
	bool			per_pkg;
	/* parse modifier helper */
	int			exclude_GH;
	int			nr_members;
	int			sample_read;
	unsigned long		*per_pkg_mask;
	struct perf_evsel	*leader;
	char			*group_name;
};

union u64_swap {
	u64 val64;
	u32 val32[2];
};

struct cpu_map;
struct target;
struct thread_map;
struct perf_evlist;
struct record_opts;

static inline struct cpu_map *perf_evsel__cpus(struct perf_evsel *evsel)
{
	return evsel->cpus;
}

static inline int perf_evsel__nr_cpus(struct perf_evsel *evsel)
{
	return perf_evsel__cpus(evsel)->nr;
}

void perf_counts_values__scale(struct perf_counts_values *count,
			       bool scale, s8 *pscaled);

void perf_evsel__compute_deltas(struct perf_evsel *evsel, int cpu, int thread,
				struct perf_counts_values *count);

int perf_evsel__object_config(size_t object_size,
			      int (*init)(struct perf_evsel *evsel),
			      void (*fini)(struct perf_evsel *evsel));

struct perf_evsel *perf_evsel__new_idx(struct perf_event_attr *attr, int idx);

static inline struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr)
{
	return perf_evsel__new_idx(attr, 0);
}

struct perf_evsel *perf_evsel__newtp_idx(const char *sys, const char *name, int idx);

static inline struct perf_evsel *perf_evsel__newtp(const char *sys, const char *name)
{
	return perf_evsel__newtp_idx(sys, name, 0);
}

struct event_format *event_format__new(const char *sys, const char *name);

void perf_evsel__init(struct perf_evsel *evsel,
		      struct perf_event_attr *attr, int idx);
void perf_evsel__exit(struct perf_evsel *evsel);
void perf_evsel__delete(struct perf_evsel *evsel);

void perf_evsel__config(struct perf_evsel *evsel,
			struct record_opts *opts);

int __perf_evsel__sample_size(u64 sample_type);
void perf_evsel__calc_id_pos(struct perf_evsel *evsel);

bool perf_evsel__is_cache_op_valid(u8 type, u8 op);

#define PERF_EVSEL__MAX_ALIASES 8

extern const char *perf_evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX]
					      [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_names[PERF_COUNT_HW_MAX];
extern const char *perf_evsel__sw_names[PERF_COUNT_SW_MAX];
int __perf_evsel__hw_cache_type_op_res_name(u8 type, u8 op, u8 result,
					    char *bf, size_t size);
const char *perf_evsel__name(struct perf_evsel *evsel);

const char *perf_evsel__group_name(struct perf_evsel *evsel);
int perf_evsel__group_desc(struct perf_evsel *evsel, char *buf, size_t size);

int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads);
void perf_evsel__close_fd(struct perf_evsel *evsel, int ncpus, int nthreads);

void __perf_evsel__set_sample_bit(struct perf_evsel *evsel,
				  enum perf_event_sample_format bit);
void __perf_evsel__reset_sample_bit(struct perf_evsel *evsel,
				    enum perf_event_sample_format bit);

#define perf_evsel__set_sample_bit(evsel, bit) \
	__perf_evsel__set_sample_bit(evsel, PERF_SAMPLE_##bit)

#define perf_evsel__reset_sample_bit(evsel, bit) \
	__perf_evsel__reset_sample_bit(evsel, PERF_SAMPLE_##bit)

void perf_evsel__set_sample_id(struct perf_evsel *evsel,
			       bool use_sample_identifier);

int perf_evsel__set_filter(struct perf_evsel *evsel, int ncpus, int nthreads,
			   const char *filter);
int perf_evsel__enable(struct perf_evsel *evsel, int ncpus, int nthreads);

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus);
int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads);
int perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads);
void perf_evsel__close(struct perf_evsel *evsel, int ncpus, int nthreads);

struct perf_sample;

void *perf_evsel__rawptr(struct perf_evsel *evsel, struct perf_sample *sample,
			 const char *name);
u64 perf_evsel__intval(struct perf_evsel *evsel, struct perf_sample *sample,
		       const char *name);

static inline char *perf_evsel__strval(struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       const char *name)
{
	return perf_evsel__rawptr(evsel, sample, name);
}

struct format_field;

struct format_field *perf_evsel__field(struct perf_evsel *evsel, const char *name);

#define perf_evsel__match(evsel, t, c)		\
	(evsel->attr.type == PERF_TYPE_##t &&	\
	 evsel->attr.config == PERF_COUNT_##c)

static inline bool perf_evsel__match2(struct perf_evsel *e1,
				      struct perf_evsel *e2)
{
	return (e1->attr.type == e2->attr.type) &&
	       (e1->attr.config == e2->attr.config);
}

#define perf_evsel__cmp(a, b)			\
	((a) &&					\
	 (b) &&					\
	 (a)->attr.type == (b)->attr.type &&	\
	 (a)->attr.config == (b)->attr.config)

int perf_evsel__read(struct perf_evsel *evsel, int cpu, int thread,
		     struct perf_counts_values *count);

int __perf_evsel__read_on_cpu(struct perf_evsel *evsel,
			      int cpu, int thread, bool scale);

/**
 * perf_evsel__read_on_cpu - Read out the results on a CPU and thread
 *
 * @evsel - event selector to read value
 * @cpu - CPU of interest
 * @thread - thread of interest
 */
static inline int perf_evsel__read_on_cpu(struct perf_evsel *evsel,
					  int cpu, int thread)
{
	return __perf_evsel__read_on_cpu(evsel, cpu, thread, false);
}

/**
 * perf_evsel__read_on_cpu_scaled - Read out the results on a CPU and thread, scaled
 *
 * @evsel - event selector to read value
 * @cpu - CPU of interest
 * @thread - thread of interest
 */
static inline int perf_evsel__read_on_cpu_scaled(struct perf_evsel *evsel,
						 int cpu, int thread)
{
	return __perf_evsel__read_on_cpu(evsel, cpu, thread, true);
}

int perf_evsel__parse_sample(struct perf_evsel *evsel, union perf_event *event,
			     struct perf_sample *sample);

static inline struct perf_evsel *perf_evsel__next(struct perf_evsel *evsel)
{
	return list_entry(evsel->node.next, struct perf_evsel, node);
}

static inline struct perf_evsel *perf_evsel__prev(struct perf_evsel *evsel)
{
	return list_entry(evsel->node.prev, struct perf_evsel, node);
}

/**
 * perf_evsel__is_group_leader - Return whether given evsel is a leader event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true if @evsel is a group leader or a stand-alone event
 */
static inline bool perf_evsel__is_group_leader(const struct perf_evsel *evsel)
{
	return evsel->leader == evsel;
}

/**
 * perf_evsel__is_group_event - Return whether given evsel is a group event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true iff event group view is enabled and @evsel is a actual group
 * leader which has other members in the group
 */
static inline bool perf_evsel__is_group_event(struct perf_evsel *evsel)
{
	if (!symbol_conf.event_group)
		return false;

	return perf_evsel__is_group_leader(evsel) && evsel->nr_members > 1;
}

/**
 * perf_evsel__is_function_event - Return whether given evsel is a function
 * trace event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true if event is function trace event
 */
static inline bool perf_evsel__is_function_event(struct perf_evsel *evsel)
{
#define FUNCTION_EVENT "ftrace:function"

	return evsel->name &&
	       !strncmp(FUNCTION_EVENT, evsel->name, sizeof(FUNCTION_EVENT));

#undef FUNCTION_EVENT
}

struct perf_attr_details {
	bool freq;
	bool verbose;
	bool event_group;
	bool force;
};

int perf_evsel__fprintf(struct perf_evsel *evsel,
			struct perf_attr_details *details, FILE *fp);

bool perf_evsel__fallback(struct perf_evsel *evsel, int err,
			  char *msg, size_t msgsize);
int perf_evsel__open_strerror(struct perf_evsel *evsel, struct target *target,
			      int err, char *msg, size_t size);

static inline int perf_evsel__group_idx(struct perf_evsel *evsel)
{
	return evsel->idx - evsel->leader->idx;
}

#define for_each_group_member(_evsel, _leader) 					\
for ((_evsel) = list_entry((_leader)->node.next, struct perf_evsel, node); 	\
     (_evsel) && (_evsel)->leader == (_leader);					\
     (_evsel) = list_entry((_evsel)->node.next, struct perf_evsel, node))

static inline bool has_branch_callstack(struct perf_evsel *evsel)
{
	return evsel->attr.branch_sample_type & PERF_SAMPLE_BRANCH_CALL_STACK;
}

typedef int (*attr__fprintf_f)(FILE *, const char *, const char *, void *);

int perf_event_attr__fprintf(FILE *fp, struct perf_event_attr *attr,
			     attr__fprintf_f attr__fprintf, void *priv);

#endif /* __PERF_EVSEL_H */
