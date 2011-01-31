#ifndef __PERF_TOP_H
#define __PERF_TOP_H 1

#include "types.h"
#include "../perf.h"
#include <stddef.h>
#include <pthread.h>
#include <linux/list.h>
#include <linux/rbtree.h>

struct perf_evlist;
struct perf_evsel;

struct source_line {
	u64			eip;
	unsigned long		count[MAX_COUNTERS]; /* FIXME */
	char			*line;
	struct source_line	*next;
};

struct sym_entry_source {
	struct source_line	*source;
	struct source_line	*lines;
	struct source_line	**lines_tail;
	pthread_mutex_t		lock;
};

struct sym_entry {
	struct rb_node		rb_node;
	struct list_head	node;
	unsigned long		snap_count;
	double			weight;
	int			skip;
	u16			name_len;
	u8			origin;
	struct map		*map;
	struct sym_entry_source	*src;
	unsigned long		count[0];
};

struct perf_top {
	struct perf_evlist *evlist;
	/*
	 * Symbols will be added here in perf_event__process_sample and will
	 * get out after decayed.
	 */
	struct list_head   active_symbols;
	pthread_mutex_t	   active_symbols_lock;
	u64		   samples;
	u64		   kernel_samples, us_samples;
	u64		   exact_samples;
	u64		   guest_us_samples, guest_kernel_samples;
	int		   print_entries, count_filter, delay_secs;
	int		   display_weighted, freq;
	int		   sym_counter, target_pid, target_tid;
	bool		   hide_kernel_symbols, hide_user_symbols, zero;
	const char	   *cpu_list;
	struct perf_evsel  *sym_evsel;
};

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size);
void perf_top__reset_sample_counters(struct perf_top *top);
float perf_top__decay_samples(struct perf_top *top, struct rb_root *root);
void perf_top__find_widths(struct perf_top *top, struct rb_root *root,
			   int *dso_width, int *dso_short_width, int *sym_width);

#endif /* __PERF_TOP_H */
