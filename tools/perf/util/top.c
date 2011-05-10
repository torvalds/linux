/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Refactored from builtin-top.c, see that files for further copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "cpumap.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "symbol.h"
#include "top.h"
#include <inttypes.h>

/*
 * Ordering weight: count-1 * count-2 * ... / count-n
 */
static double sym_weight(const struct sym_entry *sym, struct perf_top *top)
{
	double weight = sym->snap_count;
	int counter;

	if (!top->display_weighted)
		return weight;

	for (counter = 1; counter < top->evlist->nr_entries - 1; counter++)
		weight *= sym->count[counter];

	weight /= (sym->count[counter] + 1);

	return weight;
}

static void perf_top__remove_active_sym(struct perf_top *top, struct sym_entry *syme)
{
	pthread_mutex_lock(&top->active_symbols_lock);
	list_del_init(&syme->node);
	pthread_mutex_unlock(&top->active_symbols_lock);
}

static void rb_insert_active_sym(struct rb_root *tree, struct sym_entry *se)
{
	struct rb_node **p = &tree->rb_node;
	struct rb_node *parent = NULL;
	struct sym_entry *iter;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct sym_entry, rb_node);

		if (se->weight > iter->weight)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&se->rb_node, parent, p);
	rb_insert_color(&se->rb_node, tree);
}

#define SNPRINTF(buf, size, fmt, args...) \
({ \
	size_t r = snprintf(buf, size, fmt, ## args); \
	r > size ?  size : r; \
})

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size)
{
	struct perf_evsel *counter;
	float samples_per_sec = top->samples / top->delay_secs;
	float ksamples_per_sec = top->kernel_samples / top->delay_secs;
	float esamples_percent = (100.0 * top->exact_samples) / top->samples;
	size_t ret = 0;

	if (!perf_guest) {
		ret = SNPRINTF(bf, size,
			       "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%%"
			       "  exact: %4.1f%% [", samples_per_sec,
			       100.0 - (100.0 * ((samples_per_sec - ksamples_per_sec) /
					samples_per_sec)),
				esamples_percent);
	} else {
		float us_samples_per_sec = top->us_samples / top->delay_secs;
		float guest_kernel_samples_per_sec = top->guest_kernel_samples / top->delay_secs;
		float guest_us_samples_per_sec = top->guest_us_samples / top->delay_secs;

		ret = SNPRINTF(bf, size,
			       "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%% us:%4.1f%%"
			       " guest kernel:%4.1f%% guest us:%4.1f%%"
			       " exact: %4.1f%% [", samples_per_sec,
			       100.0 - (100.0 * ((samples_per_sec - ksamples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec - us_samples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec -
						  guest_kernel_samples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec -
						  guest_us_samples_per_sec) /
						 samples_per_sec)),
			       esamples_percent);
	}

	if (top->evlist->nr_entries == 1 || !top->display_weighted) {
		struct perf_evsel *first;
		first = list_entry(top->evlist->entries.next, struct perf_evsel, node);
		ret += SNPRINTF(bf + ret, size - ret, "%" PRIu64 "%s ",
				(uint64_t)first->attr.sample_period,
				top->freq ? "Hz" : "");
	}

	if (!top->display_weighted) {
		ret += SNPRINTF(bf + ret, size - ret, "%s",
				event_name(top->sym_evsel));
	} else {
		/*
		 * Don't let events eat all the space. Leaving 30 bytes
		 * for the rest should be enough.
		 */
		size_t last_pos = size - 30;

		list_for_each_entry(counter, &top->evlist->entries, node) {
			ret += SNPRINTF(bf + ret, size - ret, "%s%s",
					counter->idx ? "/" : "",
					event_name(counter));
			if (ret > last_pos) {
				sprintf(bf + last_pos - 3, "..");
				ret = last_pos - 1;
				break;
			}
		}
	}

	ret += SNPRINTF(bf + ret, size - ret, "], ");

	if (top->target_pid != -1)
		ret += SNPRINTF(bf + ret, size - ret, " (target_pid: %d",
				top->target_pid);
	else if (top->target_tid != -1)
		ret += SNPRINTF(bf + ret, size - ret, " (target_tid: %d",
				top->target_tid);
	else
		ret += SNPRINTF(bf + ret, size - ret, " (all");

	if (top->cpu_list)
		ret += SNPRINTF(bf + ret, size - ret, ", CPU%s: %s)",
				top->evlist->cpus->nr > 1 ? "s" : "", top->cpu_list);
	else {
		if (top->target_tid != -1)
			ret += SNPRINTF(bf + ret, size - ret, ")");
		else
			ret += SNPRINTF(bf + ret, size - ret, ", %d CPU%s)",
					top->evlist->cpus->nr,
					top->evlist->cpus->nr > 1 ? "s" : "");
	}

	return ret;
}

void perf_top__reset_sample_counters(struct perf_top *top)
{
	top->samples = top->us_samples = top->kernel_samples =
	top->exact_samples = top->guest_kernel_samples =
	top->guest_us_samples = 0;
}

float perf_top__decay_samples(struct perf_top *top, struct rb_root *root)
{
	struct sym_entry *syme, *n;
	float sum_ksamples = 0.0;
	int snap = !top->display_weighted ? top->sym_evsel->idx : 0, j;

	/* Sort the active symbols */
	pthread_mutex_lock(&top->active_symbols_lock);
	syme = list_entry(top->active_symbols.next, struct sym_entry, node);
	pthread_mutex_unlock(&top->active_symbols_lock);

	top->rb_entries = 0;
	list_for_each_entry_safe_from(syme, n, &top->active_symbols, node) {
		syme->snap_count = syme->count[snap];
		if (syme->snap_count != 0) {

			if ((top->hide_user_symbols &&
			     syme->map->dso->kernel == DSO_TYPE_USER) ||
			    (top->hide_kernel_symbols &&
			     syme->map->dso->kernel == DSO_TYPE_KERNEL)) {
				perf_top__remove_active_sym(top, syme);
				continue;
			}
			syme->weight = sym_weight(syme, top);

			if ((int)syme->snap_count >= top->count_filter) {
				rb_insert_active_sym(root, syme);
				++top->rb_entries;
			}
			sum_ksamples += syme->snap_count;

			for (j = 0; j < top->evlist->nr_entries; j++)
				syme->count[j] = top->zero ? 0 : syme->count[j] * 7 / 8;
		} else
			perf_top__remove_active_sym(top, syme);
	}

	return sum_ksamples;
}

/*
 * Find the longest symbol name that will be displayed
 */
void perf_top__find_widths(struct perf_top *top, struct rb_root *root,
			   int *dso_width, int *dso_short_width, int *sym_width)
{
	struct rb_node *nd;
	int printed = 0;

	*sym_width = *dso_width = *dso_short_width = 0;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct sym_entry *syme = rb_entry(nd, struct sym_entry, rb_node);
		struct symbol *sym = sym_entry__symbol(syme);

		if (++printed > top->print_entries ||
		    (int)syme->snap_count < top->count_filter)
			continue;

		if (syme->map->dso->long_name_len > *dso_width)
			*dso_width = syme->map->dso->long_name_len;

		if (syme->map->dso->short_name_len > *dso_short_width)
			*dso_short_width = syme->map->dso->short_name_len;

		if (sym->namelen > *sym_width)
			*sym_width = sym->namelen;
	}
}
