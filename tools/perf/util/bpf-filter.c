/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <bpf/bpf.h>
#include <linux/err.h>
#include <internal/xyarray.h>

#include "util/debug.h"
#include "util/evsel.h"

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

int perf_bpf_filter__prepare(struct evsel *evsel)
{
	int i, x, y, fd;
	struct sample_filter_bpf *skel;
	struct bpf_program *prog;
	struct bpf_link *link;
	struct perf_bpf_filter_expr *expr;

	skel = sample_filter_bpf__open_and_load();
	if (!skel) {
		pr_err("Failed to load perf sample-filter BPF skeleton\n");
		return -1;
	}

	i = 0;
	fd = bpf_map__fd(skel->maps.filters);
	list_for_each_entry(expr, &evsel->bpf_filters, list) {
		struct perf_bpf_filter_entry entry = {
			.op = expr->op,
			.part = expr->part,
			.term = expr->term,
			.value = expr->val,
		};

		if (check_sample_flags(evsel, expr) < 0)
			return -1;

		bpf_map_update_elem(fd, &i, &entry, BPF_ANY);
		i++;

		if (expr->op == PBF_OP_GROUP_BEGIN) {
			struct perf_bpf_filter_expr *group;

			list_for_each_entry(group, &expr->groups, list) {
				struct perf_bpf_filter_entry group_entry = {
					.op = group->op,
					.part = group->part,
					.term = group->term,
					.value = group->val,
				};
				bpf_map_update_elem(fd, &i, &group_entry, BPF_ANY);
				i++;
			}

			memset(&entry, 0, sizeof(entry));
			entry.op = PBF_OP_GROUP_END;
			bpf_map_update_elem(fd, &i, &entry, BPF_ANY);
			i++;
		}
	}

	if (i > MAX_FILTERS) {
		pr_err("Too many filters: %d (max = %d)\n", i, MAX_FILTERS);
		return -1;
	}
	prog = skel->progs.perf_sample_filter;
	for (x = 0; x < xyarray__max_x(evsel->core.fd); x++) {
		for (y = 0; y < xyarray__max_y(evsel->core.fd); y++) {
			link = bpf_program__attach_perf_event(prog, FD(evsel, x, y));
			if (IS_ERR(link)) {
				pr_err("Failed to attach perf sample-filter program\n");
				return PTR_ERR(link);
			}
		}
	}
	evsel->bpf_skel = skel;
	return 0;
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
