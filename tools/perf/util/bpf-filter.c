/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <bpf/bpf.h>
#include <linux/err.h>
#include <api/fs/fs.h>
#include <internal/xyarray.h>
#include <perf/threadmap.h>

#include "util/debug.h"
#include "util/evsel.h"
#include "util/target.h"

#include "util/bpf-filter.h"
#include <util/bpf-filter-flex.h>
#include <util/bpf-filter-bison.h>

#include "bpf_skel/sample-filter.h"
#include "bpf_skel/sample_filter.skel.h"

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

#define __PERF_SAMPLE_TYPE(tt, st, opt)	{ tt, #st, opt }
#define PERF_SAMPLE_TYPE(_st, opt)	__PERF_SAMPLE_TYPE(PBF_TERM_##_st, PERF_SAMPLE_##_st, opt)

static const struct perf_sample_info {
	enum perf_bpf_filter_term type;
	const char *name;
	const char *option;
} sample_table[] = {
	/* default sample flags */
	PERF_SAMPLE_TYPE(IP, NULL),
	PERF_SAMPLE_TYPE(TID, NULL),
	PERF_SAMPLE_TYPE(PERIOD, NULL),
	/* flags mostly set by default, but still have options */
	PERF_SAMPLE_TYPE(ID, "--sample-identifier"),
	PERF_SAMPLE_TYPE(CPU, "--sample-cpu"),
	PERF_SAMPLE_TYPE(TIME, "-T"),
	/* optional sample flags */
	PERF_SAMPLE_TYPE(ADDR, "-d"),
	PERF_SAMPLE_TYPE(DATA_SRC, "-d"),
	PERF_SAMPLE_TYPE(PHYS_ADDR, "--phys-data"),
	PERF_SAMPLE_TYPE(WEIGHT, "-W"),
	PERF_SAMPLE_TYPE(WEIGHT_STRUCT, "-W"),
	PERF_SAMPLE_TYPE(TRANSACTION, "--transaction"),
	PERF_SAMPLE_TYPE(CODE_PAGE_SIZE, "--code-page-size"),
	PERF_SAMPLE_TYPE(DATA_PAGE_SIZE, "--data-page-size"),
};

static const struct perf_sample_info *get_sample_info(enum perf_bpf_filter_term type)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sample_table); i++) {
		if (sample_table[i].type == type)
			return &sample_table[i];
	}
	return NULL;
}

static int check_sample_flags(struct evsel *evsel, struct perf_bpf_filter_expr *expr)
{
	const struct perf_sample_info *info;

	if (expr->term >= PBF_TERM_SAMPLE_START && expr->term <= PBF_TERM_SAMPLE_END &&
	    (evsel->core.attr.sample_type & (1 << (expr->term - PBF_TERM_SAMPLE_START))))
		return 0;

	if (expr->term == PBF_TERM_UID || expr->term == PBF_TERM_GID) {
		/* Not dependent on the sample_type as computed from a BPF helper. */
		return 0;
	}

	if (expr->op == PBF_OP_GROUP_BEGIN) {
		struct perf_bpf_filter_expr *group;

		list_for_each_entry(group, &expr->groups, list) {
			if (check_sample_flags(evsel, group) < 0)
				return -1;
		}
		return 0;
	}

	info = get_sample_info(expr->term);
	if (info == NULL) {
		pr_err("Error: %s event does not have sample flags %d\n",
		       evsel__name(evsel), expr->term);
		return -1;
	}

	pr_err("Error: %s event does not have %s\n", evsel__name(evsel), info->name);
	if (info->option)
		pr_err(" Hint: please add %s option to perf record\n", info->option);
	return -1;
}

static int get_filter_entries(struct evsel *evsel, struct perf_bpf_filter_entry *entry)
{
	int i = 0;
	struct perf_bpf_filter_expr *expr;

	list_for_each_entry(expr, &evsel->bpf_filters, list) {
		if (check_sample_flags(evsel, expr) < 0)
			return -EINVAL;

		if (i == MAX_FILTERS)
			return -E2BIG;

		entry[i].op = expr->op;
		entry[i].part = expr->part;
		entry[i].term = expr->term;
		entry[i].value = expr->val;
		i++;

		if (expr->op == PBF_OP_GROUP_BEGIN) {
			struct perf_bpf_filter_expr *group;

			list_for_each_entry(group, &expr->groups, list) {
				if (i == MAX_FILTERS)
					return -E2BIG;

				entry[i].op = group->op;
				entry[i].part = group->part;
				entry[i].term = group->term;
				entry[i].value = group->val;
				i++;
			}

			if (i == MAX_FILTERS)
				return -E2BIG;

			entry[i].op = PBF_OP_GROUP_END;
			i++;
		}
	}

	if (i < MAX_FILTERS) {
		/* to terminate the loop early */
		entry[i].op = PBF_OP_DONE;
		i++;
	}
	return 0;
}

static int convert_to_tgid(int tid)
{
	char path[128];
	char *buf, *p, *q;
	int tgid;
	size_t len;

	scnprintf(path, sizeof(path), "%d/status", tid);
	if (procfs__read_str(path, &buf, &len) < 0)
		return -1;

	p = strstr(buf, "Tgid:");
	if (p == NULL) {
		free(buf);
		return -1;
	}

	tgid = strtol(p + 6, &q, 0);
	free(buf);
	if (*q != '\n')
		return -1;

	return tgid;
}

static int update_pid_hash(struct sample_filter_bpf *skel, struct evsel *evsel,
			   struct perf_bpf_filter_entry *entry)
{
	int filter_idx;
	int nr, last;
	int fd = bpf_map__fd(skel->maps.filters);
	struct perf_thread_map *threads;

	/* Find the first available entry in the filters map */
	for (filter_idx = 0; filter_idx < MAX_FILTERS; filter_idx++) {
		if (bpf_map_update_elem(fd, &filter_idx, entry, BPF_NOEXIST) == 0)
			break;
	}

	if (filter_idx == MAX_FILTERS) {
		pr_err("Too many users for the filter map\n");
		return -EBUSY;
	}

	threads = perf_evsel__threads(&evsel->core);
	if (threads == NULL) {
		pr_err("Cannot get the thread list of the event\n");
		return -EINVAL;
	}

	/* save the index to a hash map */
	fd = bpf_map__fd(skel->maps.pid_hash);

	last = -1;
	nr = perf_thread_map__nr(threads);
	for (int i = 0; i < nr; i++) {
		int pid = perf_thread_map__pid(threads, i);
		int tgid;

		/* it actually needs tgid, let's get tgid from /proc. */
		tgid = convert_to_tgid(pid);
		if (tgid < 0) {
			/* the thread may be dead, ignore. */
			continue;
		}

		if (tgid == last)
			continue;
		last = tgid;

		if (bpf_map_update_elem(fd, &tgid, &filter_idx, BPF_ANY) < 0) {
			pr_err("Failed to update the pid hash\n");
			return -errno;
		}
		pr_debug("pid hash: %d -> %d\n", tgid, filter_idx);
	}
	return 0;
}

int perf_bpf_filter__prepare(struct evsel *evsel, struct target *target)
{
	int i, x, y, fd, ret;
	struct sample_filter_bpf *skel = NULL;
	struct bpf_program *prog;
	struct bpf_link *link;
	struct perf_bpf_filter_entry *entry;
	bool needs_pid_hash = !target__has_cpu(target) && !target->uid_str;

	entry = calloc(MAX_FILTERS, sizeof(*entry));
	if (entry == NULL)
		return -1;

	ret = get_filter_entries(evsel, entry);
	if (ret < 0) {
		pr_err("Failed to process filter entries\n");
		goto err;
	}

	skel = sample_filter_bpf__open();
	if (!skel) {
		pr_err("Failed to open perf sample-filter BPF skeleton\n");
		ret = -EPERM;
		goto err;
	}

	if (needs_pid_hash) {
		bpf_map__set_max_entries(skel->maps.filters, MAX_FILTERS);
		bpf_map__set_max_entries(skel->maps.pid_hash, MAX_PIDS);
		skel->rodata->use_pid_hash = 1;
	}

	if (sample_filter_bpf__load(skel) < 0) {
		pr_err("Failed to load perf sample-filter BPF skeleton\n");
		ret = -EPERM;
		goto err;
	}

	if (needs_pid_hash) {
		/* The filters map is shared among other processes  */
		ret = update_pid_hash(skel, evsel, entry);
		if (ret < 0)
			goto err;
	} else {
		i = 0;
		fd = bpf_map__fd(skel->maps.filters);

		/* The filters map has only one entry in this case */
		if (bpf_map_update_elem(fd, &i, entry, BPF_ANY) < 0) {
			ret = -errno;
			pr_err("Failed to update the filter map\n");
			goto err;
		}
	}

	prog = skel->progs.perf_sample_filter;
	for (x = 0; x < xyarray__max_x(evsel->core.fd); x++) {
		for (y = 0; y < xyarray__max_y(evsel->core.fd); y++) {
			link = bpf_program__attach_perf_event(prog, FD(evsel, x, y));
			if (IS_ERR(link)) {
				pr_err("Failed to attach perf sample-filter program\n");
				ret = PTR_ERR(link);
				goto err;
			}
		}
	}
	free(entry);
	evsel->bpf_skel = skel;
	return 0;

err:
	free(entry);
	sample_filter_bpf__destroy(skel);
	return ret;
}

int perf_bpf_filter__destroy(struct evsel *evsel)
{
	struct perf_bpf_filter_expr *expr, *tmp;

	list_for_each_entry_safe(expr, tmp, &evsel->bpf_filters, list) {
		list_del(&expr->list);
		free(expr);
	}
	sample_filter_bpf__destroy(evsel->bpf_skel);
	return 0;
}

u64 perf_bpf_filter__lost_count(struct evsel *evsel)
{
	struct sample_filter_bpf *skel = evsel->bpf_skel;

	return skel ? skel->bss->dropped : 0;
}

struct perf_bpf_filter_expr *perf_bpf_filter_expr__new(enum perf_bpf_filter_term term,
						       int part,
						       enum perf_bpf_filter_op op,
						       unsigned long val)
{
	struct perf_bpf_filter_expr *expr;

	expr = malloc(sizeof(*expr));
	if (expr != NULL) {
		expr->term = term;
		expr->part = part;
		expr->op = op;
		expr->val = val;
		INIT_LIST_HEAD(&expr->groups);
	}
	return expr;
}

int perf_bpf_filter__parse(struct list_head *expr_head, const char *str)
{
	YY_BUFFER_STATE buffer;
	int ret;

	buffer = perf_bpf_filter__scan_string(str);

	ret = perf_bpf_filter_parse(expr_head);

	perf_bpf_filter__flush_buffer(buffer);
	perf_bpf_filter__delete_buffer(buffer);
	perf_bpf_filter_lex_destroy();

	return ret;
}
