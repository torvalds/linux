// SPDX-License-Identifier: GPL-2.0
#include "callchain.h"
#include "debug.h"
#include "dso.h"
#include "build-id.h"
#include "hist.h"
#include "kvm-stat.h"
#include "map.h"
#include "map_symbol.h"
#include "branch.h"
#include "mem-events.h"
#include "mem-info.h"
#include "session.h"
#include "namespaces.h"
#include "cgroup.h"
#include "sort.h"
#include "units.h"
#include "evlist.h"
#include "evsel.h"
#include "annotate.h"
#include "srcline.h"
#include "symbol.h"
#include "thread.h"
#include "block-info.h"
#include "ui/progress.h"
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <sys/param.h>
#include <linux/rbtree.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/zalloc.h>

static bool hists__filter_entry_by_dso(struct hists *hists,
				       struct hist_entry *he);
static bool hists__filter_entry_by_thread(struct hists *hists,
					  struct hist_entry *he);
static bool hists__filter_entry_by_symbol(struct hists *hists,
					  struct hist_entry *he);
static bool hists__filter_entry_by_socket(struct hists *hists,
					  struct hist_entry *he);

u16 hists__col_len(struct hists *hists, enum hist_column col)
{
	return hists->col_len[col];
}

void hists__set_col_len(struct hists *hists, enum hist_column col, u16 len)
{
	hists->col_len[col] = len;
}

bool hists__new_col_len(struct hists *hists, enum hist_column col, u16 len)
{
	if (len > hists__col_len(hists, col)) {
		hists__set_col_len(hists, col, len);
		return true;
	}
	return false;
}

void hists__reset_col_len(struct hists *hists)
{
	enum hist_column col;

	for (col = 0; col < HISTC_NR_COLS; ++col)
		hists__set_col_len(hists, col, 0);
}

static void hists__set_unres_dso_col_len(struct hists *hists, int dso)
{
	const unsigned int unresolved_col_width = BITS_PER_LONG / 4;

	if (hists__col_len(hists, dso) < unresolved_col_width &&
	    !symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
	    !symbol_conf.dso_list)
		hists__set_col_len(hists, dso, unresolved_col_width);
}

void hists__calc_col_len(struct hists *hists, struct hist_entry *h)
{
	const unsigned int unresolved_col_width = BITS_PER_LONG / 4;
	int symlen;
	u16 len;

	if (h->block_info)
		return;
	/*
	 * +4 accounts for '[x] ' priv level info
	 * +2 accounts for 0x prefix on raw addresses
	 * +3 accounts for ' y ' symtab origin info
	 */
	if (h->ms.sym) {
		symlen = h->ms.sym->namelen + 4;
		if (verbose > 0)
			symlen += BITS_PER_LONG / 4 + 2 + 3;
		hists__new_col_len(hists, HISTC_SYMBOL, symlen);
	} else {
		symlen = unresolved_col_width + 4 + 2;
		hists__new_col_len(hists, HISTC_SYMBOL, symlen);
		hists__set_unres_dso_col_len(hists, HISTC_DSO);
	}

	len = thread__comm_len(h->thread);
	if (hists__new_col_len(hists, HISTC_COMM, len))
		hists__set_col_len(hists, HISTC_THREAD, len + 8);

	if (h->ms.map) {
		len = dso__name_len(map__dso(h->ms.map));
		hists__new_col_len(hists, HISTC_DSO, len);
	}

	if (h->parent)
		hists__new_col_len(hists, HISTC_PARENT, h->parent->namelen);

	if (h->branch_info) {
		if (h->branch_info->from.ms.sym) {
			symlen = (int)h->branch_info->from.ms.sym->namelen + 4;
			if (verbose > 0)
				symlen += BITS_PER_LONG / 4 + 2 + 3;
			hists__new_col_len(hists, HISTC_SYMBOL_FROM, symlen);

			symlen = dso__name_len(map__dso(h->branch_info->from.ms.map));
			hists__new_col_len(hists, HISTC_DSO_FROM, symlen);
		} else {
			symlen = unresolved_col_width + 4 + 2;
			hists__new_col_len(hists, HISTC_SYMBOL_FROM, symlen);
			hists__new_col_len(hists, HISTC_ADDR_FROM, symlen);
			hists__set_unres_dso_col_len(hists, HISTC_DSO_FROM);
		}

		if (h->branch_info->to.ms.sym) {
			symlen = (int)h->branch_info->to.ms.sym->namelen + 4;
			if (verbose > 0)
				symlen += BITS_PER_LONG / 4 + 2 + 3;
			hists__new_col_len(hists, HISTC_SYMBOL_TO, symlen);

			symlen = dso__name_len(map__dso(h->branch_info->to.ms.map));
			hists__new_col_len(hists, HISTC_DSO_TO, symlen);
		} else {
			symlen = unresolved_col_width + 4 + 2;
			hists__new_col_len(hists, HISTC_SYMBOL_TO, symlen);
			hists__new_col_len(hists, HISTC_ADDR_TO, symlen);
			hists__set_unres_dso_col_len(hists, HISTC_DSO_TO);
		}

		if (h->branch_info->srcline_from)
			hists__new_col_len(hists, HISTC_SRCLINE_FROM,
					strlen(h->branch_info->srcline_from));
		if (h->branch_info->srcline_to)
			hists__new_col_len(hists, HISTC_SRCLINE_TO,
					strlen(h->branch_info->srcline_to));
	}

	if (h->mem_info) {
		if (mem_info__daddr(h->mem_info)->ms.sym) {
			symlen = (int)mem_info__daddr(h->mem_info)->ms.sym->namelen + 4
			       + unresolved_col_width + 2;
			hists__new_col_len(hists, HISTC_MEM_DADDR_SYMBOL,
					   symlen);
			hists__new_col_len(hists, HISTC_MEM_DCACHELINE,
					   symlen + 1);
		} else {
			symlen = unresolved_col_width + 4 + 2;
			hists__new_col_len(hists, HISTC_MEM_DADDR_SYMBOL,
					   symlen);
			hists__new_col_len(hists, HISTC_MEM_DCACHELINE,
					   symlen);
		}

		if (mem_info__iaddr(h->mem_info)->ms.sym) {
			symlen = (int)mem_info__iaddr(h->mem_info)->ms.sym->namelen + 4
			       + unresolved_col_width + 2;
			hists__new_col_len(hists, HISTC_MEM_IADDR_SYMBOL,
					   symlen);
		} else {
			symlen = unresolved_col_width + 4 + 2;
			hists__new_col_len(hists, HISTC_MEM_IADDR_SYMBOL,
					   symlen);
		}

		if (mem_info__daddr(h->mem_info)->ms.map) {
			symlen = dso__name_len(map__dso(mem_info__daddr(h->mem_info)->ms.map));
			hists__new_col_len(hists, HISTC_MEM_DADDR_DSO,
					   symlen);
		} else {
			symlen = unresolved_col_width + 4 + 2;
			hists__set_unres_dso_col_len(hists, HISTC_MEM_DADDR_DSO);
		}

		hists__new_col_len(hists, HISTC_MEM_PHYS_DADDR,
				   unresolved_col_width + 4 + 2);

		hists__new_col_len(hists, HISTC_MEM_DATA_PAGE_SIZE,
				   unresolved_col_width + 4 + 2);

	} else {
		symlen = unresolved_col_width + 4 + 2;
		hists__new_col_len(hists, HISTC_MEM_DADDR_SYMBOL, symlen);
		hists__new_col_len(hists, HISTC_MEM_IADDR_SYMBOL, symlen);
		hists__set_unres_dso_col_len(hists, HISTC_MEM_DADDR_DSO);
	}

	hists__new_col_len(hists, HISTC_CGROUP, 6);
	hists__new_col_len(hists, HISTC_CGROUP_ID, 20);
	hists__new_col_len(hists, HISTC_CPU, 3);
	hists__new_col_len(hists, HISTC_SOCKET, 6);
	hists__new_col_len(hists, HISTC_MEM_LOCKED, 6);
	hists__new_col_len(hists, HISTC_MEM_TLB, 22);
	hists__new_col_len(hists, HISTC_MEM_SNOOP, 12);
	hists__new_col_len(hists, HISTC_MEM_LVL, 36 + 3);
	hists__new_col_len(hists, HISTC_LOCAL_WEIGHT, 12);
	hists__new_col_len(hists, HISTC_GLOBAL_WEIGHT, 12);
	hists__new_col_len(hists, HISTC_MEM_BLOCKED, 10);
	hists__new_col_len(hists, HISTC_LOCAL_INS_LAT, 13);
	hists__new_col_len(hists, HISTC_GLOBAL_INS_LAT, 13);
	hists__new_col_len(hists, HISTC_LOCAL_P_STAGE_CYC, 13);
	hists__new_col_len(hists, HISTC_GLOBAL_P_STAGE_CYC, 13);
	hists__new_col_len(hists, HISTC_ADDR, BITS_PER_LONG / 4 + 2);

	if (symbol_conf.nanosecs)
		hists__new_col_len(hists, HISTC_TIME, 16);
	else
		hists__new_col_len(hists, HISTC_TIME, 12);
	hists__new_col_len(hists, HISTC_CODE_PAGE_SIZE, 6);

	if (h->srcline) {
		len = MAX(strlen(h->srcline), strlen(sort_srcline.se_header));
		hists__new_col_len(hists, HISTC_SRCLINE, len);
	}

	if (h->srcfile)
		hists__new_col_len(hists, HISTC_SRCFILE, strlen(h->srcfile));

	if (h->transaction)
		hists__new_col_len(hists, HISTC_TRANSACTION,
				   hist_entry__transaction_len());

	if (h->trace_output)
		hists__new_col_len(hists, HISTC_TRACE, strlen(h->trace_output));

	if (h->cgroup) {
		const char *cgrp_name = "unknown";
		struct cgroup *cgrp = cgroup__find(maps__machine(h->ms.maps)->env,
						   h->cgroup);
		if (cgrp != NULL)
			cgrp_name = cgrp->name;

		hists__new_col_len(hists, HISTC_CGROUP, strlen(cgrp_name));
	}
}

void hists__output_recalc_col_len(struct hists *hists, int max_rows)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	struct hist_entry *n;
	int row = 0;

	hists__reset_col_len(hists);

	while (next && row++ < max_rows) {
		n = rb_entry(next, struct hist_entry, rb_node);
		if (!n->filtered)
			hists__calc_col_len(hists, n);
		next = rb_next(&n->rb_node);
	}
}

static void he_stat__add_cpumode_period(struct he_stat *he_stat,
					unsigned int cpumode, u64 period)
{
	switch (cpumode) {
	case PERF_RECORD_MISC_KERNEL:
		he_stat->period_sys += period;
		break;
	case PERF_RECORD_MISC_USER:
		he_stat->period_us += period;
		break;
	case PERF_RECORD_MISC_GUEST_KERNEL:
		he_stat->period_guest_sys += period;
		break;
	case PERF_RECORD_MISC_GUEST_USER:
		he_stat->period_guest_us += period;
		break;
	default:
		break;
	}
}

static long hist_time(unsigned long htime)
{
	unsigned long time_quantum = symbol_conf.time_quantum;
	if (time_quantum)
		return (htime / time_quantum) * time_quantum;
	return htime;
}

static void he_stat__add_period(struct he_stat *he_stat, u64 period)
{
	he_stat->period		+= period;
	he_stat->nr_events	+= 1;
}

static void he_stat__add_stat(struct he_stat *dest, struct he_stat *src)
{
	dest->period		+= src->period;
	dest->period_sys	+= src->period_sys;
	dest->period_us		+= src->period_us;
	dest->period_guest_sys	+= src->period_guest_sys;
	dest->period_guest_us	+= src->period_guest_us;
	dest->weight1		+= src->weight1;
	dest->weight2		+= src->weight2;
	dest->weight3		+= src->weight3;
	dest->nr_events		+= src->nr_events;
}

static void he_stat__decay(struct he_stat *he_stat)
{
	he_stat->period = (he_stat->period * 7) / 8;
	he_stat->nr_events = (he_stat->nr_events * 7) / 8;
	he_stat->weight1 = (he_stat->weight1 * 7) / 8;
	he_stat->weight2 = (he_stat->weight2 * 7) / 8;
	he_stat->weight3 = (he_stat->weight3 * 7) / 8;
}

static void hists__delete_entry(struct hists *hists, struct hist_entry *he);

static bool hists__decay_entry(struct hists *hists, struct hist_entry *he)
{
	u64 prev_period = he->stat.period;
	u64 diff;

	if (prev_period == 0)
		return true;

	he_stat__decay(&he->stat);
	if (symbol_conf.cumulate_callchain)
		he_stat__decay(he->stat_acc);
	decay_callchain(he->callchain);

	diff = prev_period - he->stat.period;

	if (!he->depth) {
		hists->stats.total_period -= diff;
		if (!he->filtered)
			hists->stats.total_non_filtered_period -= diff;
	}

	if (!he->leaf) {
		struct hist_entry *child;
		struct rb_node *node = rb_first_cached(&he->hroot_out);
		while (node) {
			child = rb_entry(node, struct hist_entry, rb_node);
			node = rb_next(node);

			if (hists__decay_entry(hists, child))
				hists__delete_entry(hists, child);
		}
	}

	return he->stat.period == 0;
}

static void hists__delete_entry(struct hists *hists, struct hist_entry *he)
{
	struct rb_root_cached *root_in;
	struct rb_root_cached *root_out;

	if (he->parent_he) {
		root_in  = &he->parent_he->hroot_in;
		root_out = &he->parent_he->hroot_out;
	} else {
		if (hists__has(hists, need_collapse))
			root_in = &hists->entries_collapsed;
		else
			root_in = hists->entries_in;
		root_out = &hists->entries;
	}

	rb_erase_cached(&he->rb_node_in, root_in);
	rb_erase_cached(&he->rb_node, root_out);

	--hists->nr_entries;
	if (!he->filtered)
		--hists->nr_non_filtered_entries;

	hist_entry__delete(he);
}

void hists__decay_entries(struct hists *hists, bool zap_user, bool zap_kernel)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	struct hist_entry *n;

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);
		if (((zap_user && n->level == '.') ||
		     (zap_kernel && n->level != '.') ||
		     hists__decay_entry(hists, n))) {
			hists__delete_entry(hists, n);
		}
	}
}

void hists__delete_entries(struct hists *hists)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	struct hist_entry *n;

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		hists__delete_entry(hists, n);
	}
}

struct hist_entry *hists__get_entry(struct hists *hists, int idx)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	struct hist_entry *n;
	int i = 0;

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		if (i == idx)
			return n;

		next = rb_next(&n->rb_node);
		i++;
	}

	return NULL;
}

/*
 * histogram, sorted on item, collects periods
 */

static int hist_entry__init(struct hist_entry *he,
			    struct hist_entry *template,
			    bool sample_self,
			    size_t callchain_size)
{
	*he = *template;
	he->callchain_size = callchain_size;

	if (symbol_conf.cumulate_callchain) {
		he->stat_acc = malloc(sizeof(he->stat));
		if (he->stat_acc == NULL)
			return -ENOMEM;
		memcpy(he->stat_acc, &he->stat, sizeof(he->stat));
		if (!sample_self)
			memset(&he->stat, 0, sizeof(he->stat));
	}

	he->ms.maps = maps__get(he->ms.maps);
	he->ms.map = map__get(he->ms.map);

	if (he->branch_info) {
		/*
		 * This branch info is (a part of) allocated from
		 * sample__resolve_bstack() and will be freed after
		 * adding new entries.  So we need to save a copy.
		 */
		he->branch_info = malloc(sizeof(*he->branch_info));
		if (he->branch_info == NULL)
			goto err;

		memcpy(he->branch_info, template->branch_info,
		       sizeof(*he->branch_info));

		he->branch_info->from.ms.map = map__get(he->branch_info->from.ms.map);
		he->branch_info->to.ms.map = map__get(he->branch_info->to.ms.map);
	}

	if (hist_entry__has_callchains(he) && symbol_conf.use_callchain)
		callchain_init(he->callchain);

	if (he->raw_data) {
		he->raw_data = memdup(he->raw_data, he->raw_size);
		if (he->raw_data == NULL)
			goto err_infos;
	}

	if (he->srcline && he->srcline != SRCLINE_UNKNOWN) {
		he->srcline = strdup(he->srcline);
		if (he->srcline == NULL)
			goto err_rawdata;
	}

	if (symbol_conf.res_sample) {
		he->res_samples = calloc(symbol_conf.res_sample,
					sizeof(struct res_sample));
		if (!he->res_samples)
			goto err_srcline;
	}

	INIT_LIST_HEAD(&he->pairs.node);
	he->thread = thread__get(he->thread);
	he->hroot_in  = RB_ROOT_CACHED;
	he->hroot_out = RB_ROOT_CACHED;

	if (!symbol_conf.report_hierarchy)
		he->leaf = true;

	return 0;

err_srcline:
	zfree(&he->srcline);

err_rawdata:
	zfree(&he->raw_data);

err_infos:
	if (he->branch_info) {
		map_symbol__exit(&he->branch_info->from.ms);
		map_symbol__exit(&he->branch_info->to.ms);
		zfree(&he->branch_info);
	}
	if (he->mem_info) {
		map_symbol__exit(&mem_info__iaddr(he->mem_info)->ms);
		map_symbol__exit(&mem_info__daddr(he->mem_info)->ms);
	}
err:
	map_symbol__exit(&he->ms);
	zfree(&he->stat_acc);
	return -ENOMEM;
}

static void *hist_entry__zalloc(size_t size)
{
	return zalloc(size + sizeof(struct hist_entry));
}

static void hist_entry__free(void *ptr)
{
	free(ptr);
}

static struct hist_entry_ops default_ops = {
	.new	= hist_entry__zalloc,
	.free	= hist_entry__free,
};

static struct hist_entry *hist_entry__new(struct hist_entry *template,
					  bool sample_self)
{
	struct hist_entry_ops *ops = template->ops;
	size_t callchain_size = 0;
	struct hist_entry *he;
	int err = 0;

	if (!ops)
		ops = template->ops = &default_ops;

	if (symbol_conf.use_callchain)
		callchain_size = sizeof(struct callchain_root);

	he = ops->new(callchain_size);
	if (he) {
		err = hist_entry__init(he, template, sample_self, callchain_size);
		if (err) {
			ops->free(he);
			he = NULL;
		}
	}
	return he;
}

static u8 symbol__parent_filter(const struct symbol *parent)
{
	if (symbol_conf.exclude_other && parent == NULL)
		return 1 << HIST_FILTER__PARENT;
	return 0;
}

static void hist_entry__add_callchain_period(struct hist_entry *he, u64 period)
{
	if (!hist_entry__has_callchains(he) || !symbol_conf.use_callchain)
		return;

	he->hists->callchain_period += period;
	if (!he->filtered)
		he->hists->callchain_non_filtered_period += period;
}

static struct hist_entry *hists__findnew_entry(struct hists *hists,
					       struct hist_entry *entry,
					       const struct addr_location *al,
					       bool sample_self)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	int64_t cmp;
	u64 period = entry->stat.period;
	bool leftmost = true;

	p = &hists->entries_in->rb_root.rb_node;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		/*
		 * Make sure that it receives arguments in a same order as
		 * hist_entry__collapse() so that we can use an appropriate
		 * function when searching an entry regardless which sort
		 * keys were used.
		 */
		cmp = hist_entry__cmp(he, entry);
		if (!cmp) {
			if (sample_self) {
				he_stat__add_stat(&he->stat, &entry->stat);
				hist_entry__add_callchain_period(he, period);
			}
			if (symbol_conf.cumulate_callchain)
				he_stat__add_period(he->stat_acc, period);

			/*
			 * This mem info was allocated from sample__resolve_mem
			 * and will not be used anymore.
			 */
			mem_info__zput(entry->mem_info);

			block_info__delete(entry->block_info);

			kvm_info__zput(entry->kvm_info);

			/* If the map of an existing hist_entry has
			 * become out-of-date due to an exec() or
			 * similar, update it.  Otherwise we will
			 * mis-adjust symbol addresses when computing
			 * the history counter to increment.
			 */
			if (hists__has(hists, sym) && he->ms.map != entry->ms.map) {
				if (he->ms.sym) {
					u64 addr = he->ms.sym->start;
					he->ms.sym = map__find_symbol(entry->ms.map, addr);
				}

				map__put(he->ms.map);
				he->ms.map = map__get(entry->ms.map);
			}
			goto out;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}

	he = hist_entry__new(entry, sample_self);
	if (!he)
		return NULL;

	if (sample_self)
		hist_entry__add_callchain_period(he, period);
	hists->nr_entries++;

	rb_link_node(&he->rb_node_in, parent, p);
	rb_insert_color_cached(&he->rb_node_in, hists->entries_in, leftmost);
out:
	if (sample_self)
		he_stat__add_cpumode_period(&he->stat, al->cpumode, period);
	if (symbol_conf.cumulate_callchain)
		he_stat__add_cpumode_period(he->stat_acc, al->cpumode, period);
	return he;
}

static unsigned random_max(unsigned high)
{
	unsigned thresh = -high % high;
	for (;;) {
		unsigned r = random();
		if (r >= thresh)
			return r % high;
	}
}

static void hists__res_sample(struct hist_entry *he, struct perf_sample *sample)
{
	struct res_sample *r;
	int j;

	if (he->num_res < symbol_conf.res_sample) {
		j = he->num_res++;
	} else {
		j = random_max(symbol_conf.res_sample);
	}
	r = &he->res_samples[j];
	r->time = sample->time;
	r->cpu = sample->cpu;
	r->tid = sample->tid;
}

static struct hist_entry*
__hists__add_entry(struct hists *hists,
		   struct addr_location *al,
		   struct symbol *sym_parent,
		   struct branch_info *bi,
		   struct mem_info *mi,
		   struct kvm_info *ki,
		   struct block_info *block_info,
		   struct perf_sample *sample,
		   bool sample_self,
		   struct hist_entry_ops *ops)
{
	struct namespaces *ns = thread__namespaces(al->thread);
	struct hist_entry entry = {
		.thread	= al->thread,
		.comm = thread__comm(al->thread),
		.cgroup_id = {
			.dev = ns ? ns->link_info[CGROUP_NS_INDEX].dev : 0,
			.ino = ns ? ns->link_info[CGROUP_NS_INDEX].ino : 0,
		},
		.cgroup = sample->cgroup,
		.ms = {
			.maps	= al->maps,
			.map	= al->map,
			.sym	= al->sym,
		},
		.srcline = (char *) al->srcline,
		.socket	 = al->socket,
		.cpu	 = al->cpu,
		.cpumode = al->cpumode,
		.ip	 = al->addr,
		.level	 = al->level,
		.code_page_size = sample->code_page_size,
		.stat = {
			.nr_events = 1,
			.period	= sample->period,
			.weight1 = sample->weight,
			.weight2 = sample->ins_lat,
			.weight3 = sample->p_stage_cyc,
		},
		.parent = sym_parent,
		.filtered = symbol__parent_filter(sym_parent) | al->filtered,
		.hists	= hists,
		.branch_info = bi,
		.mem_info = mem_info__get(mi),
		.kvm_info = ki,
		.block_info = block_info,
		.transaction = sample->transaction,
		.raw_data = sample->raw_data,
		.raw_size = sample->raw_size,
		.ops = ops,
		.time = hist_time(sample->time),
		.weight = sample->weight,
		.ins_lat = sample->ins_lat,
		.p_stage_cyc = sample->p_stage_cyc,
		.simd_flags = sample->simd_flags,
	}, *he = hists__findnew_entry(hists, &entry, al, sample_self);

	if (!hists->has_callchains && he && he->callchain_size != 0)
		hists->has_callchains = true;
	if (he && symbol_conf.res_sample)
		hists__res_sample(he, sample);
	return he;
}

struct hist_entry *hists__add_entry(struct hists *hists,
				    struct addr_location *al,
				    struct symbol *sym_parent,
				    struct branch_info *bi,
				    struct mem_info *mi,
				    struct kvm_info *ki,
				    struct perf_sample *sample,
				    bool sample_self)
{
	return __hists__add_entry(hists, al, sym_parent, bi, mi, ki, NULL,
				  sample, sample_self, NULL);
}

struct hist_entry *hists__add_entry_ops(struct hists *hists,
					struct hist_entry_ops *ops,
					struct addr_location *al,
					struct symbol *sym_parent,
					struct branch_info *bi,
					struct mem_info *mi,
					struct kvm_info *ki,
					struct perf_sample *sample,
					bool sample_self)
{
	return __hists__add_entry(hists, al, sym_parent, bi, mi, ki, NULL,
				  sample, sample_self, ops);
}

struct hist_entry *hists__add_entry_block(struct hists *hists,
					  struct addr_location *al,
					  struct block_info *block_info)
{
	struct hist_entry entry = {
		.block_info = block_info,
		.hists = hists,
		.ms = {
			.maps = al->maps,
			.map = al->map,
			.sym = al->sym,
		},
	}, *he = hists__findnew_entry(hists, &entry, al, false);

	return he;
}

static int
iter_next_nop_entry(struct hist_entry_iter *iter __maybe_unused,
		    struct addr_location *al __maybe_unused)
{
	return 0;
}

static int
iter_add_next_nop_entry(struct hist_entry_iter *iter __maybe_unused,
			struct addr_location *al __maybe_unused)
{
	return 0;
}

static int
iter_prepare_mem_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	struct perf_sample *sample = iter->sample;
	struct mem_info *mi;

	mi = sample__resolve_mem(sample, al);
	if (mi == NULL)
		return -ENOMEM;

	iter->mi = mi;
	return 0;
}

static int
iter_add_single_mem_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	u64 cost;
	struct mem_info *mi = iter->mi;
	struct hists *hists = evsel__hists(iter->evsel);
	struct perf_sample *sample = iter->sample;
	struct hist_entry *he;

	if (mi == NULL)
		return -EINVAL;

	cost = sample->weight;
	if (!cost)
		cost = 1;

	/*
	 * must pass period=weight in order to get the correct
	 * sorting from hists__collapse_resort() which is solely
	 * based on periods. We want sorting be done on nr_events * weight
	 * and this is indirectly achieved by passing period=weight here
	 * and the he_stat__add_period() function.
	 */
	sample->period = cost;

	he = hists__add_entry(hists, al, iter->parent, NULL, mi, NULL,
			      sample, true);
	if (!he)
		return -ENOMEM;

	iter->he = he;
	return 0;
}

static int
iter_finish_mem_entry(struct hist_entry_iter *iter,
		      struct addr_location *al __maybe_unused)
{
	struct evsel *evsel = iter->evsel;
	struct hists *hists = evsel__hists(evsel);
	struct hist_entry *he = iter->he;
	int err = -EINVAL;

	if (he == NULL)
		goto out;

	hists__inc_nr_samples(hists, he->filtered);

	err = hist_entry__append_callchain(he, iter->sample);

out:
	mem_info__zput(iter->mi);

	iter->he = NULL;
	return err;
}

static int
iter_prepare_branch_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	struct branch_info *bi;
	struct perf_sample *sample = iter->sample;

	bi = sample__resolve_bstack(sample, al);
	if (!bi)
		return -ENOMEM;

	iter->curr = 0;
	iter->total = sample->branch_stack->nr;

	iter->bi = bi;
	return 0;
}

static int
iter_add_single_branch_entry(struct hist_entry_iter *iter __maybe_unused,
			     struct addr_location *al __maybe_unused)
{
	return 0;
}

static int
iter_next_branch_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	struct branch_info *bi = iter->bi;
	int i = iter->curr;

	if (bi == NULL)
		return 0;

	if (iter->curr >= iter->total)
		return 0;

	maps__put(al->maps);
	al->maps = maps__get(bi[i].to.ms.maps);
	map__put(al->map);
	al->map = map__get(bi[i].to.ms.map);
	al->sym = bi[i].to.ms.sym;
	al->addr = bi[i].to.addr;
	return 1;
}

static int
iter_add_next_branch_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	struct branch_info *bi;
	struct evsel *evsel = iter->evsel;
	struct hists *hists = evsel__hists(evsel);
	struct perf_sample *sample = iter->sample;
	struct hist_entry *he = NULL;
	int i = iter->curr;
	int err = 0;

	bi = iter->bi;

	if (iter->hide_unresolved && !(bi[i].from.ms.sym && bi[i].to.ms.sym))
		goto out;

	/*
	 * The report shows the percentage of total branches captured
	 * and not events sampled. Thus we use a pseudo period of 1.
	 */
	sample->period = 1;
	sample->weight = bi->flags.cycles ? bi->flags.cycles : 1;

	he = hists__add_entry(hists, al, iter->parent, &bi[i], NULL, NULL,
			      sample, true);
	if (he == NULL)
		return -ENOMEM;

	hists__inc_nr_samples(hists, he->filtered);

out:
	iter->he = he;
	iter->curr++;
	return err;
}

static int
iter_finish_branch_entry(struct hist_entry_iter *iter,
			 struct addr_location *al __maybe_unused)
{
	zfree(&iter->bi);
	iter->he = NULL;

	return iter->curr >= iter->total ? 0 : -1;
}

static int
iter_prepare_normal_entry(struct hist_entry_iter *iter __maybe_unused,
			  struct addr_location *al __maybe_unused)
{
	return 0;
}

static int
iter_add_single_normal_entry(struct hist_entry_iter *iter, struct addr_location *al)
{
	struct evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;
	struct hist_entry *he;

	he = hists__add_entry(evsel__hists(evsel), al, iter->parent, NULL, NULL,
			      NULL, sample, true);
	if (he == NULL)
		return -ENOMEM;

	iter->he = he;
	return 0;
}

static int
iter_finish_normal_entry(struct hist_entry_iter *iter,
			 struct addr_location *al __maybe_unused)
{
	struct hist_entry *he = iter->he;
	struct evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;

	if (he == NULL)
		return 0;

	iter->he = NULL;

	hists__inc_nr_samples(evsel__hists(evsel), he->filtered);

	return hist_entry__append_callchain(he, sample);
}

static int
iter_prepare_cumulative_entry(struct hist_entry_iter *iter,
			      struct addr_location *al __maybe_unused)
{
	struct hist_entry **he_cache;
	struct callchain_cursor *cursor = get_tls_callchain_cursor();

	if (cursor == NULL)
		return -ENOMEM;

	callchain_cursor_commit(cursor);

	/*
	 * This is for detecting cycles or recursions so that they're
	 * cumulated only one time to prevent entries more than 100%
	 * overhead.
	 */
	he_cache = malloc(sizeof(*he_cache) * (cursor->nr + 1));
	if (he_cache == NULL)
		return -ENOMEM;

	iter->he_cache = he_cache;
	iter->curr = 0;

	return 0;
}

static int
iter_add_single_cumulative_entry(struct hist_entry_iter *iter,
				 struct addr_location *al)
{
	struct evsel *evsel = iter->evsel;
	struct hists *hists = evsel__hists(evsel);
	struct perf_sample *sample = iter->sample;
	struct hist_entry **he_cache = iter->he_cache;
	struct hist_entry *he;
	int err = 0;

	he = hists__add_entry(hists, al, iter->parent, NULL, NULL, NULL,
			      sample, true);
	if (he == NULL)
		return -ENOMEM;

	iter->he = he;
	he_cache[iter->curr++] = he;

	hist_entry__append_callchain(he, sample);

	/*
	 * We need to re-initialize the cursor since callchain_append()
	 * advanced the cursor to the end.
	 */
	callchain_cursor_commit(get_tls_callchain_cursor());

	hists__inc_nr_samples(hists, he->filtered);

	return err;
}

static int
iter_next_cumulative_entry(struct hist_entry_iter *iter,
			   struct addr_location *al)
{
	struct callchain_cursor_node *node;

	node = callchain_cursor_current(get_tls_callchain_cursor());
	if (node == NULL)
		return 0;

	return fill_callchain_info(al, node, iter->hide_unresolved);
}

static bool
hist_entry__fast__sym_diff(struct hist_entry *left,
			   struct hist_entry *right)
{
	struct symbol *sym_l = left->ms.sym;
	struct symbol *sym_r = right->ms.sym;

	if (!sym_l && !sym_r)
		return left->ip != right->ip;

	return !!_sort__sym_cmp(sym_l, sym_r);
}


static int
iter_add_next_cumulative_entry(struct hist_entry_iter *iter,
			       struct addr_location *al)
{
	struct evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;
	struct hist_entry **he_cache = iter->he_cache;
	struct hist_entry *he;
	struct hist_entry he_tmp = {
		.hists = evsel__hists(evsel),
		.cpu = al->cpu,
		.thread = al->thread,
		.comm = thread__comm(al->thread),
		.ip = al->addr,
		.ms = {
			.maps = al->maps,
			.map = al->map,
			.sym = al->sym,
		},
		.srcline = (char *) al->srcline,
		.parent = iter->parent,
		.raw_data = sample->raw_data,
		.raw_size = sample->raw_size,
	};
	int i;
	struct callchain_cursor cursor, *tls_cursor = get_tls_callchain_cursor();
	bool fast = hists__has(he_tmp.hists, sym);

	if (tls_cursor == NULL)
		return -ENOMEM;

	callchain_cursor_snapshot(&cursor, tls_cursor);

	callchain_cursor_advance(tls_cursor);

	/*
	 * Check if there's duplicate entries in the callchain.
	 * It's possible that it has cycles or recursive calls.
	 */
	for (i = 0; i < iter->curr; i++) {
		/*
		 * For most cases, there are no duplicate entries in callchain.
		 * The symbols are usually different. Do a quick check for
		 * symbols first.
		 */
		if (fast && hist_entry__fast__sym_diff(he_cache[i], &he_tmp))
			continue;

		if (hist_entry__cmp(he_cache[i], &he_tmp) == 0) {
			/* to avoid calling callback function */
			iter->he = NULL;
			return 0;
		}
	}

	he = hists__add_entry(evsel__hists(evsel), al, iter->parent, NULL, NULL,
			      NULL, sample, false);
	if (he == NULL)
		return -ENOMEM;

	iter->he = he;
	he_cache[iter->curr++] = he;

	if (hist_entry__has_callchains(he) && symbol_conf.use_callchain)
		callchain_append(he->callchain, &cursor, sample->period);
	return 0;
}

static int
iter_finish_cumulative_entry(struct hist_entry_iter *iter,
			     struct addr_location *al __maybe_unused)
{
	mem_info__zput(iter->mi);
	zfree(&iter->bi);
	zfree(&iter->he_cache);
	iter->he = NULL;

	return 0;
}

const struct hist_iter_ops hist_iter_mem = {
	.prepare_entry 		= iter_prepare_mem_entry,
	.add_single_entry 	= iter_add_single_mem_entry,
	.next_entry 		= iter_next_nop_entry,
	.add_next_entry 	= iter_add_next_nop_entry,
	.finish_entry 		= iter_finish_mem_entry,
};

const struct hist_iter_ops hist_iter_branch = {
	.prepare_entry 		= iter_prepare_branch_entry,
	.add_single_entry 	= iter_add_single_branch_entry,
	.next_entry 		= iter_next_branch_entry,
	.add_next_entry 	= iter_add_next_branch_entry,
	.finish_entry 		= iter_finish_branch_entry,
};

const struct hist_iter_ops hist_iter_normal = {
	.prepare_entry 		= iter_prepare_normal_entry,
	.add_single_entry 	= iter_add_single_normal_entry,
	.next_entry 		= iter_next_nop_entry,
	.add_next_entry 	= iter_add_next_nop_entry,
	.finish_entry 		= iter_finish_normal_entry,
};

const struct hist_iter_ops hist_iter_cumulative = {
	.prepare_entry 		= iter_prepare_cumulative_entry,
	.add_single_entry 	= iter_add_single_cumulative_entry,
	.next_entry 		= iter_next_cumulative_entry,
	.add_next_entry 	= iter_add_next_cumulative_entry,
	.finish_entry 		= iter_finish_cumulative_entry,
};

int hist_entry_iter__add(struct hist_entry_iter *iter, struct addr_location *al,
			 int max_stack_depth, void *arg)
{
	int err, err2;
	struct map *alm = NULL;

	if (al)
		alm = map__get(al->map);

	err = sample__resolve_callchain(iter->sample, get_tls_callchain_cursor(), &iter->parent,
					iter->evsel, al, max_stack_depth);
	if (err) {
		map__put(alm);
		return err;
	}

	err = iter->ops->prepare_entry(iter, al);
	if (err)
		goto out;

	err = iter->ops->add_single_entry(iter, al);
	if (err)
		goto out;

	if (iter->he && iter->add_entry_cb) {
		err = iter->add_entry_cb(iter, al, true, arg);
		if (err)
			goto out;
	}

	while (iter->ops->next_entry(iter, al)) {
		err = iter->ops->add_next_entry(iter, al);
		if (err)
			break;

		if (iter->he && iter->add_entry_cb) {
			err = iter->add_entry_cb(iter, al, false, arg);
			if (err)
				goto out;
		}
	}

out:
	err2 = iter->ops->finish_entry(iter, al);
	if (!err)
		err = err2;

	map__put(alm);

	return err;
}

int64_t
hist_entry__cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct hists *hists = left->hists;
	struct perf_hpp_fmt *fmt;
	int64_t cmp = 0;

	hists__for_each_sort_list(hists, fmt) {
		if (perf_hpp__is_dynamic_entry(fmt) &&
		    !perf_hpp__defined_dynamic_entry(fmt, hists))
			continue;

		cmp = fmt->cmp(fmt, left, right);
		if (cmp)
			break;
	}

	return cmp;
}

int64_t
hist_entry__collapse(struct hist_entry *left, struct hist_entry *right)
{
	struct hists *hists = left->hists;
	struct perf_hpp_fmt *fmt;
	int64_t cmp = 0;

	hists__for_each_sort_list(hists, fmt) {
		if (perf_hpp__is_dynamic_entry(fmt) &&
		    !perf_hpp__defined_dynamic_entry(fmt, hists))
			continue;

		cmp = fmt->collapse(fmt, left, right);
		if (cmp)
			break;
	}

	return cmp;
}

void hist_entry__delete(struct hist_entry *he)
{
	struct hist_entry_ops *ops = he->ops;

	thread__zput(he->thread);
	map_symbol__exit(&he->ms);

	if (he->branch_info) {
		map_symbol__exit(&he->branch_info->from.ms);
		map_symbol__exit(&he->branch_info->to.ms);
		zfree_srcline(&he->branch_info->srcline_from);
		zfree_srcline(&he->branch_info->srcline_to);
		zfree(&he->branch_info);
	}

	if (he->mem_info) {
		map_symbol__exit(&mem_info__iaddr(he->mem_info)->ms);
		map_symbol__exit(&mem_info__daddr(he->mem_info)->ms);
		mem_info__zput(he->mem_info);
	}

	if (he->block_info)
		block_info__delete(he->block_info);

	if (he->kvm_info)
		kvm_info__zput(he->kvm_info);

	zfree(&he->res_samples);
	zfree(&he->stat_acc);
	zfree_srcline(&he->srcline);
	if (he->srcfile && he->srcfile[0])
		zfree(&he->srcfile);
	free_callchain(he->callchain);
	zfree(&he->trace_output);
	zfree(&he->raw_data);
	ops->free(he);
}

/*
 * If this is not the last column, then we need to pad it according to the
 * pre-calculated max length for this column, otherwise don't bother adding
 * spaces because that would break viewing this with, for instance, 'less',
 * that would show tons of trailing spaces when a long C++ demangled method
 * names is sampled.
*/
int hist_entry__snprintf_alignment(struct hist_entry *he, struct perf_hpp *hpp,
				   struct perf_hpp_fmt *fmt, int printed)
{
	if (!list_is_last(&fmt->list, &he->hists->hpp_list->fields)) {
		const int width = fmt->width(fmt, hpp, he->hists);
		if (printed < width) {
			advance_hpp(hpp, printed);
			printed = scnprintf(hpp->buf, hpp->size, "%-*s", width - printed, " ");
		}
	}

	return printed;
}

/*
 * collapse the histogram
 */

static void hists__apply_filters(struct hists *hists, struct hist_entry *he);
static void hists__remove_entry_filter(struct hists *hists, struct hist_entry *he,
				       enum hist_filter type);

typedef bool (*fmt_chk_fn)(struct perf_hpp_fmt *fmt);

static bool check_thread_entry(struct perf_hpp_fmt *fmt)
{
	return perf_hpp__is_thread_entry(fmt) || perf_hpp__is_comm_entry(fmt);
}

static void hist_entry__check_and_remove_filter(struct hist_entry *he,
						enum hist_filter type,
						fmt_chk_fn check)
{
	struct perf_hpp_fmt *fmt;
	bool type_match = false;
	struct hist_entry *parent = he->parent_he;

	switch (type) {
	case HIST_FILTER__THREAD:
		if (symbol_conf.comm_list == NULL &&
		    symbol_conf.pid_list == NULL &&
		    symbol_conf.tid_list == NULL)
			return;
		break;
	case HIST_FILTER__DSO:
		if (symbol_conf.dso_list == NULL)
			return;
		break;
	case HIST_FILTER__SYMBOL:
		if (symbol_conf.sym_list == NULL)
			return;
		break;
	case HIST_FILTER__PARENT:
	case HIST_FILTER__GUEST:
	case HIST_FILTER__HOST:
	case HIST_FILTER__SOCKET:
	case HIST_FILTER__C2C:
	default:
		return;
	}

	/* if it's filtered by own fmt, it has to have filter bits */
	perf_hpp_list__for_each_format(he->hpp_list, fmt) {
		if (check(fmt)) {
			type_match = true;
			break;
		}
	}

	if (type_match) {
		/*
		 * If the filter is for current level entry, propagate
		 * filter marker to parents.  The marker bit was
		 * already set by default so it only needs to clear
		 * non-filtered entries.
		 */
		if (!(he->filtered & (1 << type))) {
			while (parent) {
				parent->filtered &= ~(1 << type);
				parent = parent->parent_he;
			}
		}
	} else {
		/*
		 * If current entry doesn't have matching formats, set
		 * filter marker for upper level entries.  it will be
		 * cleared if its lower level entries is not filtered.
		 *
		 * For lower-level entries, it inherits parent's
		 * filter bit so that lower level entries of a
		 * non-filtered entry won't set the filter marker.
		 */
		if (parent == NULL)
			he->filtered |= (1 << type);
		else
			he->filtered |= (parent->filtered & (1 << type));
	}
}

static void hist_entry__apply_hierarchy_filters(struct hist_entry *he)
{
	hist_entry__check_and_remove_filter(he, HIST_FILTER__THREAD,
					    check_thread_entry);

	hist_entry__check_and_remove_filter(he, HIST_FILTER__DSO,
					    perf_hpp__is_dso_entry);

	hist_entry__check_and_remove_filter(he, HIST_FILTER__SYMBOL,
					    perf_hpp__is_sym_entry);

	hists__apply_filters(he->hists, he);
}

static struct hist_entry *hierarchy_insert_entry(struct hists *hists,
						 struct rb_root_cached *root,
						 struct hist_entry *he,
						 struct hist_entry *parent_he,
						 struct perf_hpp_list *hpp_list)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter, *new;
	struct perf_hpp_fmt *fmt;
	int64_t cmp;
	bool leftmost = true;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = 0;
		perf_hpp_list__for_each_sort_list(hpp_list, fmt) {
			cmp = fmt->collapse(fmt, iter, he);
			if (cmp)
				break;
		}

		if (!cmp) {
			he_stat__add_stat(&iter->stat, &he->stat);
			return iter;
		}

		if (cmp < 0)
			p = &parent->rb_left;
		else {
			p = &parent->rb_right;
			leftmost = false;
		}
	}

	new = hist_entry__new(he, true);
	if (new == NULL)
		return NULL;

	hists->nr_entries++;

	/* save related format list for output */
	new->hpp_list = hpp_list;
	new->parent_he = parent_he;

	hist_entry__apply_hierarchy_filters(new);

	/* some fields are now passed to 'new' */
	perf_hpp_list__for_each_sort_list(hpp_list, fmt) {
		if (perf_hpp__is_trace_entry(fmt) || perf_hpp__is_dynamic_entry(fmt))
			he->trace_output = NULL;
		else
			new->trace_output = NULL;

		if (perf_hpp__is_srcline_entry(fmt))
			he->srcline = NULL;
		else
			new->srcline = NULL;

		if (perf_hpp__is_srcfile_entry(fmt))
			he->srcfile = NULL;
		else
			new->srcfile = NULL;
	}

	rb_link_node(&new->rb_node_in, parent, p);
	rb_insert_color_cached(&new->rb_node_in, root, leftmost);
	return new;
}

static int hists__hierarchy_insert_entry(struct hists *hists,
					 struct rb_root_cached *root,
					 struct hist_entry *he)
{
	struct perf_hpp_list_node *node;
	struct hist_entry *new_he = NULL;
	struct hist_entry *parent = NULL;
	int depth = 0;
	int ret = 0;

	list_for_each_entry(node, &hists->hpp_formats, list) {
		/* skip period (overhead) and elided columns */
		if (node->level == 0 || node->skip)
			continue;

		/* insert copy of 'he' for each fmt into the hierarchy */
		new_he = hierarchy_insert_entry(hists, root, he, parent, &node->hpp);
		if (new_he == NULL) {
			ret = -1;
			break;
		}

		root = &new_he->hroot_in;
		new_he->depth = depth++;
		parent = new_he;
	}

	if (new_he) {
		new_he->leaf = true;

		if (hist_entry__has_callchains(new_he) &&
		    symbol_conf.use_callchain) {
			struct callchain_cursor *cursor = get_tls_callchain_cursor();

			if (cursor == NULL)
				return -1;

			callchain_cursor_reset(cursor);
			if (callchain_merge(cursor,
					    new_he->callchain,
					    he->callchain) < 0)
				ret = -1;
		}
	}

	/* 'he' is no longer used */
	hist_entry__delete(he);

	/* return 0 (or -1) since it already applied filters */
	return ret;
}

static int hists__collapse_insert_entry(struct hists *hists,
					struct rb_root_cached *root,
					struct hist_entry *he)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	int64_t cmp;
	bool leftmost = true;

	if (symbol_conf.report_hierarchy)
		return hists__hierarchy_insert_entry(hists, root, he);

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = hist_entry__collapse(iter, he);

		if (!cmp) {
			int ret = 0;

			he_stat__add_stat(&iter->stat, &he->stat);
			if (symbol_conf.cumulate_callchain)
				he_stat__add_stat(iter->stat_acc, he->stat_acc);

			if (hist_entry__has_callchains(he) && symbol_conf.use_callchain) {
				struct callchain_cursor *cursor = get_tls_callchain_cursor();

				if (cursor != NULL) {
					callchain_cursor_reset(cursor);
					if (callchain_merge(cursor, iter->callchain, he->callchain) < 0)
						ret = -1;
				} else {
					ret = 0;
				}
			}
			hist_entry__delete(he);
			return ret;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}
	hists->nr_entries++;

	rb_link_node(&he->rb_node_in, parent, p);
	rb_insert_color_cached(&he->rb_node_in, root, leftmost);
	return 1;
}

struct rb_root_cached *hists__get_rotate_entries_in(struct hists *hists)
{
	struct rb_root_cached *root;

	mutex_lock(&hists->lock);

	root = hists->entries_in;
	if (++hists->entries_in > &hists->entries_in_array[1])
		hists->entries_in = &hists->entries_in_array[0];

	mutex_unlock(&hists->lock);

	return root;
}

static void hists__apply_filters(struct hists *hists, struct hist_entry *he)
{
	hists__filter_entry_by_dso(hists, he);
	hists__filter_entry_by_thread(hists, he);
	hists__filter_entry_by_symbol(hists, he);
	hists__filter_entry_by_socket(hists, he);
}

int hists__collapse_resort(struct hists *hists, struct ui_progress *prog)
{
	struct rb_root_cached *root;
	struct rb_node *next;
	struct hist_entry *n;
	int ret;

	if (!hists__has(hists, need_collapse))
		return 0;

	hists->nr_entries = 0;

	root = hists__get_rotate_entries_in(hists);

	next = rb_first_cached(root);

	while (next) {
		if (session_done())
			break;
		n = rb_entry(next, struct hist_entry, rb_node_in);
		next = rb_next(&n->rb_node_in);

		rb_erase_cached(&n->rb_node_in, root);
		ret = hists__collapse_insert_entry(hists, &hists->entries_collapsed, n);
		if (ret < 0)
			return -1;

		if (ret) {
			/*
			 * If it wasn't combined with one of the entries already
			 * collapsed, we need to apply the filters that may have
			 * been set by, say, the hist_browser.
			 */
			hists__apply_filters(hists, n);
		}
		if (prog)
			ui_progress__update(prog, 1);
	}
	return 0;
}

static int64_t hist_entry__sort(struct hist_entry *a, struct hist_entry *b)
{
	struct hists *hists = a->hists;
	struct perf_hpp_fmt *fmt;
	int64_t cmp = 0;

	hists__for_each_sort_list(hists, fmt) {
		if (perf_hpp__should_skip(fmt, a->hists))
			continue;

		cmp = fmt->sort(fmt, a, b);
		if (cmp)
			break;
	}

	return cmp;
}

static void hists__reset_filter_stats(struct hists *hists)
{
	hists->nr_non_filtered_entries = 0;
	hists->stats.total_non_filtered_period = 0;
}

void hists__reset_stats(struct hists *hists)
{
	hists->nr_entries = 0;
	hists->stats.total_period = 0;

	hists__reset_filter_stats(hists);
}

static void hists__inc_filter_stats(struct hists *hists, struct hist_entry *h)
{
	hists->nr_non_filtered_entries++;
	hists->stats.total_non_filtered_period += h->stat.period;
}

void hists__inc_stats(struct hists *hists, struct hist_entry *h)
{
	if (!h->filtered)
		hists__inc_filter_stats(hists, h);

	hists->nr_entries++;
	hists->stats.total_period += h->stat.period;
}

static void hierarchy_recalc_total_periods(struct hists *hists)
{
	struct rb_node *node;
	struct hist_entry *he;

	node = rb_first_cached(&hists->entries);

	hists->stats.total_period = 0;
	hists->stats.total_non_filtered_period = 0;

	/*
	 * recalculate total period using top-level entries only
	 * since lower level entries only see non-filtered entries
	 * but upper level entries have sum of both entries.
	 */
	while (node) {
		he = rb_entry(node, struct hist_entry, rb_node);
		node = rb_next(node);

		hists->stats.total_period += he->stat.period;
		if (!he->filtered)
			hists->stats.total_non_filtered_period += he->stat.period;
	}
}

static void hierarchy_insert_output_entry(struct rb_root_cached *root,
					  struct hist_entry *he)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	struct perf_hpp_fmt *fmt;
	bool leftmost = true;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (hist_entry__sort(he, iter) > 0)
			p = &parent->rb_left;
		else {
			p = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color_cached(&he->rb_node, root, leftmost);

	/* update column width of dynamic entry */
	perf_hpp_list__for_each_sort_list(he->hpp_list, fmt) {
		if (fmt->init)
			fmt->init(fmt, he);
	}
}

static void hists__hierarchy_output_resort(struct hists *hists,
					   struct ui_progress *prog,
					   struct rb_root_cached *root_in,
					   struct rb_root_cached *root_out,
					   u64 min_callchain_hits,
					   bool use_callchain)
{
	struct rb_node *node;
	struct hist_entry *he;

	*root_out = RB_ROOT_CACHED;
	node = rb_first_cached(root_in);

	while (node) {
		he = rb_entry(node, struct hist_entry, rb_node_in);
		node = rb_next(node);

		hierarchy_insert_output_entry(root_out, he);

		if (prog)
			ui_progress__update(prog, 1);

		hists->nr_entries++;
		if (!he->filtered) {
			hists->nr_non_filtered_entries++;
			hists__calc_col_len(hists, he);
		}

		if (!he->leaf) {
			hists__hierarchy_output_resort(hists, prog,
						       &he->hroot_in,
						       &he->hroot_out,
						       min_callchain_hits,
						       use_callchain);
			continue;
		}

		if (!use_callchain)
			continue;

		if (callchain_param.mode == CHAIN_GRAPH_REL) {
			u64 total = he->stat.period;

			if (symbol_conf.cumulate_callchain)
				total = he->stat_acc->period;

			min_callchain_hits = total * (callchain_param.min_percent / 100);
		}

		callchain_param.sort(&he->sorted_chain, he->callchain,
				     min_callchain_hits, &callchain_param);
	}
}

static void __hists__insert_output_entry(struct rb_root_cached *entries,
					 struct hist_entry *he,
					 u64 min_callchain_hits,
					 bool use_callchain)
{
	struct rb_node **p = &entries->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	struct perf_hpp_fmt *fmt;
	bool leftmost = true;

	if (use_callchain) {
		if (callchain_param.mode == CHAIN_GRAPH_REL) {
			u64 total = he->stat.period;

			if (symbol_conf.cumulate_callchain)
				total = he->stat_acc->period;

			min_callchain_hits = total * (callchain_param.min_percent / 100);
		}
		callchain_param.sort(&he->sorted_chain, he->callchain,
				      min_callchain_hits, &callchain_param);
	}

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (hist_entry__sort(he, iter) > 0)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color_cached(&he->rb_node, entries, leftmost);

	/* update column width of dynamic entries */
	perf_hpp_list__for_each_sort_list(&perf_hpp_list, fmt) {
		if (fmt->init)
			fmt->init(fmt, he);
	}
}

static void output_resort(struct hists *hists, struct ui_progress *prog,
			  bool use_callchain, hists__resort_cb_t cb,
			  void *cb_arg)
{
	struct rb_root_cached *root;
	struct rb_node *next;
	struct hist_entry *n;
	u64 callchain_total;
	u64 min_callchain_hits;

	callchain_total = hists->callchain_period;
	if (symbol_conf.filter_relative)
		callchain_total = hists->callchain_non_filtered_period;

	min_callchain_hits = callchain_total * (callchain_param.min_percent / 100);

	hists__reset_stats(hists);
	hists__reset_col_len(hists);

	if (symbol_conf.report_hierarchy) {
		hists__hierarchy_output_resort(hists, prog,
					       &hists->entries_collapsed,
					       &hists->entries,
					       min_callchain_hits,
					       use_callchain);
		hierarchy_recalc_total_periods(hists);
		return;
	}

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	next = rb_first_cached(root);
	hists->entries = RB_ROOT_CACHED;

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node_in);
		next = rb_next(&n->rb_node_in);

		if (cb && cb(n, cb_arg))
			continue;

		__hists__insert_output_entry(&hists->entries, n, min_callchain_hits, use_callchain);
		hists__inc_stats(hists, n);

		if (!n->filtered)
			hists__calc_col_len(hists, n);

		if (prog)
			ui_progress__update(prog, 1);
	}
}

void evsel__output_resort_cb(struct evsel *evsel, struct ui_progress *prog,
			     hists__resort_cb_t cb, void *cb_arg)
{
	bool use_callchain;

	if (evsel && symbol_conf.use_callchain && !symbol_conf.show_ref_callgraph)
		use_callchain = evsel__has_callchain(evsel);
	else
		use_callchain = symbol_conf.use_callchain;

	use_callchain |= symbol_conf.show_branchflag_count;

	output_resort(evsel__hists(evsel), prog, use_callchain, cb, cb_arg);
}

void evsel__output_resort(struct evsel *evsel, struct ui_progress *prog)
{
	return evsel__output_resort_cb(evsel, prog, NULL, NULL);
}

void hists__output_resort(struct hists *hists, struct ui_progress *prog)
{
	output_resort(hists, prog, symbol_conf.use_callchain, NULL, NULL);
}

void hists__output_resort_cb(struct hists *hists, struct ui_progress *prog,
			     hists__resort_cb_t cb)
{
	output_resort(hists, prog, symbol_conf.use_callchain, cb, NULL);
}

static bool can_goto_child(struct hist_entry *he, enum hierarchy_move_dir hmd)
{
	if (he->leaf || hmd == HMD_FORCE_SIBLING)
		return false;

	if (he->unfolded || hmd == HMD_FORCE_CHILD)
		return true;

	return false;
}

struct rb_node *rb_hierarchy_last(struct rb_node *node)
{
	struct hist_entry *he = rb_entry(node, struct hist_entry, rb_node);

	while (can_goto_child(he, HMD_NORMAL)) {
		node = rb_last(&he->hroot_out.rb_root);
		he = rb_entry(node, struct hist_entry, rb_node);
	}
	return node;
}

struct rb_node *__rb_hierarchy_next(struct rb_node *node, enum hierarchy_move_dir hmd)
{
	struct hist_entry *he = rb_entry(node, struct hist_entry, rb_node);

	if (can_goto_child(he, hmd))
		node = rb_first_cached(&he->hroot_out);
	else
		node = rb_next(node);

	while (node == NULL) {
		he = he->parent_he;
		if (he == NULL)
			break;

		node = rb_next(&he->rb_node);
	}
	return node;
}

struct rb_node *rb_hierarchy_prev(struct rb_node *node)
{
	struct hist_entry *he = rb_entry(node, struct hist_entry, rb_node);

	node = rb_prev(node);
	if (node)
		return rb_hierarchy_last(node);

	he = he->parent_he;
	if (he == NULL)
		return NULL;

	return &he->rb_node;
}

bool hist_entry__has_hierarchy_children(struct hist_entry *he, float limit)
{
	struct rb_node *node;
	struct hist_entry *child;
	float percent;

	if (he->leaf)
		return false;

	node = rb_first_cached(&he->hroot_out);
	child = rb_entry(node, struct hist_entry, rb_node);

	while (node && child->filtered) {
		node = rb_next(node);
		child = rb_entry(node, struct hist_entry, rb_node);
	}

	if (node)
		percent = hist_entry__get_percent_limit(child);
	else
		percent = 0;

	return node && percent >= limit;
}

static void hists__remove_entry_filter(struct hists *hists, struct hist_entry *h,
				       enum hist_filter filter)
{
	h->filtered &= ~(1 << filter);

	if (symbol_conf.report_hierarchy) {
		struct hist_entry *parent = h->parent_he;

		while (parent) {
			he_stat__add_stat(&parent->stat, &h->stat);

			parent->filtered &= ~(1 << filter);

			if (parent->filtered)
				goto next;

			/* force fold unfiltered entry for simplicity */
			parent->unfolded = false;
			parent->has_no_entry = false;
			parent->row_offset = 0;
			parent->nr_rows = 0;
next:
			parent = parent->parent_he;
		}
	}

	if (h->filtered)
		return;

	/* force fold unfiltered entry for simplicity */
	h->unfolded = false;
	h->has_no_entry = false;
	h->row_offset = 0;
	h->nr_rows = 0;

	hists->stats.nr_non_filtered_samples += h->stat.nr_events;

	hists__inc_filter_stats(hists, h);
	hists__calc_col_len(hists, h);
}


static bool hists__filter_entry_by_dso(struct hists *hists,
				       struct hist_entry *he)
{
	if (hists->dso_filter != NULL &&
	    (he->ms.map == NULL || !RC_CHK_EQUAL(map__dso(he->ms.map), hists->dso_filter))) {
		he->filtered |= (1 << HIST_FILTER__DSO);
		return true;
	}

	return false;
}

static bool hists__filter_entry_by_thread(struct hists *hists,
					  struct hist_entry *he)
{
	if (hists->thread_filter != NULL &&
	    !RC_CHK_EQUAL(he->thread, hists->thread_filter)) {
		he->filtered |= (1 << HIST_FILTER__THREAD);
		return true;
	}

	return false;
}

static bool hists__filter_entry_by_symbol(struct hists *hists,
					  struct hist_entry *he)
{
	if (hists->symbol_filter_str != NULL &&
	    (!he->ms.sym || strstr(he->ms.sym->name,
				   hists->symbol_filter_str) == NULL)) {
		he->filtered |= (1 << HIST_FILTER__SYMBOL);
		return true;
	}

	return false;
}

static bool hists__filter_entry_by_socket(struct hists *hists,
					  struct hist_entry *he)
{
	if ((hists->socket_filter > -1) &&
	    (he->socket != hists->socket_filter)) {
		he->filtered |= (1 << HIST_FILTER__SOCKET);
		return true;
	}

	return false;
}

typedef bool (*filter_fn_t)(struct hists *hists, struct hist_entry *he);

static void hists__filter_by_type(struct hists *hists, int type, filter_fn_t filter)
{
	struct rb_node *nd;

	hists->stats.nr_non_filtered_samples = 0;

	hists__reset_filter_stats(hists);
	hists__reset_col_len(hists);

	for (nd = rb_first_cached(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		if (filter(hists, h))
			continue;

		hists__remove_entry_filter(hists, h, type);
	}
}

static void resort_filtered_entry(struct rb_root_cached *root,
				  struct hist_entry *he)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	struct rb_root_cached new_root = RB_ROOT_CACHED;
	struct rb_node *nd;
	bool leftmost = true;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (hist_entry__sort(he, iter) > 0)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color_cached(&he->rb_node, root, leftmost);

	if (he->leaf || he->filtered)
		return;

	nd = rb_first_cached(&he->hroot_out);
	while (nd) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		nd = rb_next(nd);
		rb_erase_cached(&h->rb_node, &he->hroot_out);

		resort_filtered_entry(&new_root, h);
	}

	he->hroot_out = new_root;
}

static void hists__filter_hierarchy(struct hists *hists, int type, const void *arg)
{
	struct rb_node *nd;
	struct rb_root_cached new_root = RB_ROOT_CACHED;

	hists->stats.nr_non_filtered_samples = 0;

	hists__reset_filter_stats(hists);
	hists__reset_col_len(hists);

	nd = rb_first_cached(&hists->entries);
	while (nd) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		int ret;

		ret = hist_entry__filter(h, type, arg);

		/*
		 * case 1. non-matching type
		 * zero out the period, set filter marker and move to child
		 */
		if (ret < 0) {
			memset(&h->stat, 0, sizeof(h->stat));
			h->filtered |= (1 << type);

			nd = __rb_hierarchy_next(&h->rb_node, HMD_FORCE_CHILD);
		}
		/*
		 * case 2. matched type (filter out)
		 * set filter marker and move to next
		 */
		else if (ret == 1) {
			h->filtered |= (1 << type);

			nd = __rb_hierarchy_next(&h->rb_node, HMD_FORCE_SIBLING);
		}
		/*
		 * case 3. ok (not filtered)
		 * add period to hists and parents, erase the filter marker
		 * and move to next sibling
		 */
		else {
			hists__remove_entry_filter(hists, h, type);

			nd = __rb_hierarchy_next(&h->rb_node, HMD_FORCE_SIBLING);
		}
	}

	hierarchy_recalc_total_periods(hists);

	/*
	 * resort output after applying a new filter since filter in a lower
	 * hierarchy can change periods in a upper hierarchy.
	 */
	nd = rb_first_cached(&hists->entries);
	while (nd) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);

		nd = rb_next(nd);
		rb_erase_cached(&h->rb_node, &hists->entries);

		resort_filtered_entry(&new_root, h);
	}

	hists->entries = new_root;
}

void hists__filter_by_thread(struct hists *hists)
{
	if (symbol_conf.report_hierarchy)
		hists__filter_hierarchy(hists, HIST_FILTER__THREAD,
					hists->thread_filter);
	else
		hists__filter_by_type(hists, HIST_FILTER__THREAD,
				      hists__filter_entry_by_thread);
}

void hists__filter_by_dso(struct hists *hists)
{
	if (symbol_conf.report_hierarchy)
		hists__filter_hierarchy(hists, HIST_FILTER__DSO,
					hists->dso_filter);
	else
		hists__filter_by_type(hists, HIST_FILTER__DSO,
				      hists__filter_entry_by_dso);
}

void hists__filter_by_symbol(struct hists *hists)
{
	if (symbol_conf.report_hierarchy)
		hists__filter_hierarchy(hists, HIST_FILTER__SYMBOL,
					hists->symbol_filter_str);
	else
		hists__filter_by_type(hists, HIST_FILTER__SYMBOL,
				      hists__filter_entry_by_symbol);
}

void hists__filter_by_socket(struct hists *hists)
{
	if (symbol_conf.report_hierarchy)
		hists__filter_hierarchy(hists, HIST_FILTER__SOCKET,
					&hists->socket_filter);
	else
		hists__filter_by_type(hists, HIST_FILTER__SOCKET,
				      hists__filter_entry_by_socket);
}

void events_stats__inc(struct events_stats *stats, u32 type)
{
	++stats->nr_events[0];
	++stats->nr_events[type];
}

static void hists_stats__inc(struct hists_stats *stats)
{
	++stats->nr_samples;
}

void hists__inc_nr_events(struct hists *hists)
{
	hists_stats__inc(&hists->stats);
}

void hists__inc_nr_samples(struct hists *hists, bool filtered)
{
	hists_stats__inc(&hists->stats);
	if (!filtered)
		hists->stats.nr_non_filtered_samples++;
}

void hists__inc_nr_lost_samples(struct hists *hists, u32 lost)
{
	hists->stats.nr_lost_samples += lost;
}

static struct hist_entry *hists__add_dummy_entry(struct hists *hists,
						 struct hist_entry *pair)
{
	struct rb_root_cached *root;
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	int64_t cmp;
	bool leftmost = true;

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	p = &root->rb_root.rb_node;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = hist_entry__collapse(he, pair);

		if (!cmp)
			goto out;

		if (cmp < 0)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}

	he = hist_entry__new(pair, true);
	if (he) {
		memset(&he->stat, 0, sizeof(he->stat));
		he->hists = hists;
		if (symbol_conf.cumulate_callchain)
			memset(he->stat_acc, 0, sizeof(he->stat));
		rb_link_node(&he->rb_node_in, parent, p);
		rb_insert_color_cached(&he->rb_node_in, root, leftmost);
		hists__inc_stats(hists, he);
		he->dummy = true;
	}
out:
	return he;
}

static struct hist_entry *add_dummy_hierarchy_entry(struct hists *hists,
						    struct rb_root_cached *root,
						    struct hist_entry *pair)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	struct perf_hpp_fmt *fmt;
	bool leftmost = true;

	p = &root->rb_root.rb_node;
	while (*p != NULL) {
		int64_t cmp = 0;

		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		perf_hpp_list__for_each_sort_list(he->hpp_list, fmt) {
			cmp = fmt->collapse(fmt, he, pair);
			if (cmp)
				break;
		}
		if (!cmp)
			goto out;

		if (cmp < 0)
			p = &parent->rb_left;
		else {
			p = &parent->rb_right;
			leftmost = false;
		}
	}

	he = hist_entry__new(pair, true);
	if (he) {
		rb_link_node(&he->rb_node_in, parent, p);
		rb_insert_color_cached(&he->rb_node_in, root, leftmost);

		he->dummy = true;
		he->hists = hists;
		memset(&he->stat, 0, sizeof(he->stat));
		hists__inc_stats(hists, he);
	}
out:
	return he;
}

static struct hist_entry *hists__find_entry(struct hists *hists,
					    struct hist_entry *he)
{
	struct rb_node *n;

	if (hists__has(hists, need_collapse))
		n = hists->entries_collapsed.rb_root.rb_node;
	else
		n = hists->entries_in->rb_root.rb_node;

	while (n) {
		struct hist_entry *iter = rb_entry(n, struct hist_entry, rb_node_in);
		int64_t cmp = hist_entry__collapse(iter, he);

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return iter;
	}

	return NULL;
}

static struct hist_entry *hists__find_hierarchy_entry(struct rb_root_cached *root,
						      struct hist_entry *he)
{
	struct rb_node *n = root->rb_root.rb_node;

	while (n) {
		struct hist_entry *iter;
		struct perf_hpp_fmt *fmt;
		int64_t cmp = 0;

		iter = rb_entry(n, struct hist_entry, rb_node_in);
		perf_hpp_list__for_each_sort_list(he->hpp_list, fmt) {
			cmp = fmt->collapse(fmt, iter, he);
			if (cmp)
				break;
		}

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return iter;
	}

	return NULL;
}

static void hists__match_hierarchy(struct rb_root_cached *leader_root,
				   struct rb_root_cached *other_root)
{
	struct rb_node *nd;
	struct hist_entry *pos, *pair;

	for (nd = rb_first_cached(leader_root); nd; nd = rb_next(nd)) {
		pos  = rb_entry(nd, struct hist_entry, rb_node_in);
		pair = hists__find_hierarchy_entry(other_root, pos);

		if (pair) {
			hist_entry__add_pair(pair, pos);
			hists__match_hierarchy(&pos->hroot_in, &pair->hroot_in);
		}
	}
}

/*
 * Look for pairs to link to the leader buckets (hist_entries):
 */
void hists__match(struct hists *leader, struct hists *other)
{
	struct rb_root_cached *root;
	struct rb_node *nd;
	struct hist_entry *pos, *pair;

	if (symbol_conf.report_hierarchy) {
		/* hierarchy report always collapses entries */
		return hists__match_hierarchy(&leader->entries_collapsed,
					      &other->entries_collapsed);
	}

	if (hists__has(leader, need_collapse))
		root = &leader->entries_collapsed;
	else
		root = leader->entries_in;

	for (nd = rb_first_cached(root); nd; nd = rb_next(nd)) {
		pos  = rb_entry(nd, struct hist_entry, rb_node_in);
		pair = hists__find_entry(other, pos);

		if (pair)
			hist_entry__add_pair(pair, pos);
	}
}

static int hists__link_hierarchy(struct hists *leader_hists,
				 struct hist_entry *parent,
				 struct rb_root_cached *leader_root,
				 struct rb_root_cached *other_root)
{
	struct rb_node *nd;
	struct hist_entry *pos, *leader;

	for (nd = rb_first_cached(other_root); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node_in);

		if (hist_entry__has_pairs(pos)) {
			bool found = false;

			list_for_each_entry(leader, &pos->pairs.head, pairs.node) {
				if (leader->hists == leader_hists) {
					found = true;
					break;
				}
			}
			if (!found)
				return -1;
		} else {
			leader = add_dummy_hierarchy_entry(leader_hists,
							   leader_root, pos);
			if (leader == NULL)
				return -1;

			/* do not point parent in the pos */
			leader->parent_he = parent;

			hist_entry__add_pair(pos, leader);
		}

		if (!pos->leaf) {
			if (hists__link_hierarchy(leader_hists, leader,
						  &leader->hroot_in,
						  &pos->hroot_in) < 0)
				return -1;
		}
	}
	return 0;
}

/*
 * Look for entries in the other hists that are not present in the leader, if
 * we find them, just add a dummy entry on the leader hists, with period=0,
 * nr_events=0, to serve as the list header.
 */
int hists__link(struct hists *leader, struct hists *other)
{
	struct rb_root_cached *root;
	struct rb_node *nd;
	struct hist_entry *pos, *pair;

	if (symbol_conf.report_hierarchy) {
		/* hierarchy report always collapses entries */
		return hists__link_hierarchy(leader, NULL,
					     &leader->entries_collapsed,
					     &other->entries_collapsed);
	}

	if (hists__has(other, need_collapse))
		root = &other->entries_collapsed;
	else
		root = other->entries_in;

	for (nd = rb_first_cached(root); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node_in);

		if (!hist_entry__has_pairs(pos)) {
			pair = hists__add_dummy_entry(leader, pos);
			if (pair == NULL)
				return -1;
			hist_entry__add_pair(pos, pair);
		}
	}

	return 0;
}

int hists__unlink(struct hists *hists)
{
	struct rb_root_cached *root;
	struct rb_node *nd;
	struct hist_entry *pos;

	if (hists__has(hists, need_collapse))
		root = &hists->entries_collapsed;
	else
		root = hists->entries_in;

	for (nd = rb_first_cached(root); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node_in);
		list_del_init(&pos->pairs.node);
	}

	return 0;
}

void hist__account_cycles(struct branch_stack *bs, struct addr_location *al,
			  struct perf_sample *sample, bool nonany_branch_mode,
			  u64 *total_cycles)
{
	struct branch_info *bi;
	struct branch_entry *entries = perf_sample__branch_entries(sample);

	/* If we have branch cycles always annotate them. */
	if (bs && bs->nr && entries[0].flags.cycles) {
		bi = sample__resolve_bstack(sample, al);
		if (bi) {
			struct addr_map_symbol *prev = NULL;

			/*
			 * Ignore errors, still want to process the
			 * other entries.
			 *
			 * For non standard branch modes always
			 * force no IPC (prev == NULL)
			 *
			 * Note that perf stores branches reversed from
			 * program order!
			 */
			for (int i = bs->nr - 1; i >= 0; i--) {
				addr_map_symbol__account_cycles(&bi[i].from,
					nonany_branch_mode ? NULL : prev,
					bi[i].flags.cycles);
				prev = &bi[i].to;

				if (total_cycles)
					*total_cycles += bi[i].flags.cycles;
			}
			for (unsigned int i = 0; i < bs->nr; i++) {
				map_symbol__exit(&bi[i].to.ms);
				map_symbol__exit(&bi[i].from.ms);
			}
			free(bi);
		}
	}
}

size_t evlist__fprintf_nr_events(struct evlist *evlist, FILE *fp)
{
	struct evsel *pos;
	size_t ret = 0;

	evlist__for_each_entry(evlist, pos) {
		struct hists *hists = evsel__hists(pos);

		if (symbol_conf.skip_empty && !hists->stats.nr_samples &&
		    !hists->stats.nr_lost_samples)
			continue;

		ret += fprintf(fp, "%s stats:\n", evsel__name(pos));
		if (hists->stats.nr_samples)
			ret += fprintf(fp, "%16s events: %10d\n",
				       "SAMPLE", hists->stats.nr_samples);
		if (hists->stats.nr_lost_samples)
			ret += fprintf(fp, "%16s events: %10d\n",
				       "LOST_SAMPLES", hists->stats.nr_lost_samples);
	}

	return ret;
}


u64 hists__total_period(struct hists *hists)
{
	return symbol_conf.filter_relative ? hists->stats.total_non_filtered_period :
		hists->stats.total_period;
}

int __hists__scnprintf_title(struct hists *hists, char *bf, size_t size, bool show_freq)
{
	char unit;
	int printed;
	const struct dso *dso = hists->dso_filter;
	struct thread *thread = hists->thread_filter;
	int socket_id = hists->socket_filter;
	unsigned long nr_samples = hists->stats.nr_samples;
	u64 nr_events = hists->stats.total_period;
	struct evsel *evsel = hists_to_evsel(hists);
	const char *ev_name = evsel__name(evsel);
	char buf[512], sample_freq_str[64] = "";
	size_t buflen = sizeof(buf);
	char ref[30] = " show reference callgraph, ";
	bool enable_ref = false;

	if (symbol_conf.filter_relative) {
		nr_samples = hists->stats.nr_non_filtered_samples;
		nr_events = hists->stats.total_non_filtered_period;
	}

	if (evsel__is_group_event(evsel)) {
		struct evsel *pos;

		evsel__group_desc(evsel, buf, buflen);
		ev_name = buf;

		for_each_group_member(pos, evsel) {
			struct hists *pos_hists = evsel__hists(pos);

			if (symbol_conf.filter_relative) {
				nr_samples += pos_hists->stats.nr_non_filtered_samples;
				nr_events += pos_hists->stats.total_non_filtered_period;
			} else {
				nr_samples += pos_hists->stats.nr_samples;
				nr_events += pos_hists->stats.total_period;
			}
		}
	}

	if (symbol_conf.show_ref_callgraph &&
	    strstr(ev_name, "call-graph=no"))
		enable_ref = true;

	if (show_freq)
		scnprintf(sample_freq_str, sizeof(sample_freq_str), " %d Hz,", evsel->core.attr.sample_freq);

	nr_samples = convert_unit(nr_samples, &unit);
	printed = scnprintf(bf, size,
			   "Samples: %lu%c of event%s '%s',%s%sEvent count (approx.): %" PRIu64,
			   nr_samples, unit, evsel->core.nr_members > 1 ? "s" : "",
			   ev_name, sample_freq_str, enable_ref ? ref : " ", nr_events);


	if (hists->uid_filter_str)
		printed += snprintf(bf + printed, size - printed,
				    ", UID: %s", hists->uid_filter_str);
	if (thread) {
		if (hists__has(hists, thread)) {
			printed += scnprintf(bf + printed, size - printed,
				    ", Thread: %s(%d)",
				    (thread__comm_set(thread) ? thread__comm_str(thread) : ""),
					thread__tid(thread));
		} else {
			printed += scnprintf(bf + printed, size - printed,
				    ", Thread: %s",
				    (thread__comm_set(thread) ? thread__comm_str(thread) : ""));
		}
	}
	if (dso)
		printed += scnprintf(bf + printed, size - printed,
				     ", DSO: %s", dso__short_name(dso));
	if (socket_id > -1)
		printed += scnprintf(bf + printed, size - printed,
				    ", Processor Socket: %d", socket_id);

	return printed;
}

int parse_filter_percentage(const struct option *opt __maybe_unused,
			    const char *arg, int unset __maybe_unused)
{
	if (!strcmp(arg, "relative"))
		symbol_conf.filter_relative = true;
	else if (!strcmp(arg, "absolute"))
		symbol_conf.filter_relative = false;
	else {
		pr_debug("Invalid percentage: %s\n", arg);
		return -1;
	}

	return 0;
}

int perf_hist_config(const char *var, const char *value)
{
	if (!strcmp(var, "hist.percentage"))
		return parse_filter_percentage(NULL, value, 0);

	return 0;
}

int __hists__init(struct hists *hists, struct perf_hpp_list *hpp_list)
{
	memset(hists, 0, sizeof(*hists));
	hists->entries_in_array[0] = hists->entries_in_array[1] = RB_ROOT_CACHED;
	hists->entries_in = &hists->entries_in_array[0];
	hists->entries_collapsed = RB_ROOT_CACHED;
	hists->entries = RB_ROOT_CACHED;
	mutex_init(&hists->lock);
	hists->socket_filter = -1;
	hists->hpp_list = hpp_list;
	INIT_LIST_HEAD(&hists->hpp_formats);
	return 0;
}

static void hists__delete_remaining_entries(struct rb_root_cached *root)
{
	struct rb_node *node;
	struct hist_entry *he;

	while (!RB_EMPTY_ROOT(&root->rb_root)) {
		node = rb_first_cached(root);
		rb_erase_cached(node, root);

		he = rb_entry(node, struct hist_entry, rb_node_in);
		hist_entry__delete(he);
	}
}

static void hists__delete_all_entries(struct hists *hists)
{
	hists__delete_entries(hists);
	hists__delete_remaining_entries(&hists->entries_in_array[0]);
	hists__delete_remaining_entries(&hists->entries_in_array[1]);
	hists__delete_remaining_entries(&hists->entries_collapsed);
}

static void hists_evsel__exit(struct evsel *evsel)
{
	struct hists *hists = evsel__hists(evsel);
	struct perf_hpp_fmt *fmt, *pos;
	struct perf_hpp_list_node *node, *tmp;

	hists__delete_all_entries(hists);

	list_for_each_entry_safe(node, tmp, &hists->hpp_formats, list) {
		perf_hpp_list__for_each_format_safe(&node->hpp, fmt, pos) {
			list_del_init(&fmt->list);
			free(fmt);
		}
		list_del_init(&node->list);
		free(node);
	}
}

static int hists_evsel__init(struct evsel *evsel)
{
	struct hists *hists = evsel__hists(evsel);

	__hists__init(hists, &perf_hpp_list);
	return 0;
}

/*
 * XXX We probably need a hists_evsel__exit() to free the hist_entries
 * stored in the rbtree...
 */

int hists__init(void)
{
	int err = evsel__object_config(sizeof(struct hists_evsel),
				       hists_evsel__init, hists_evsel__exit);
	if (err)
		fputs("FATAL ERROR: Couldn't setup hists class\n", stderr);

	return err;
}

void perf_hpp_list__init(struct perf_hpp_list *list)
{
	INIT_LIST_HEAD(&list->fields);
	INIT_LIST_HEAD(&list->sorts);
}
