/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <bpf/bpf.h>
#include <linux/err.h>
#include <internal/xyarray.h>

#include "util/debug.h"
#include "util/evsel.h"

#include "util/bpf-filter.h"
#include "util/bpf-filter-flex.h"
#include "util/bpf-filter-bison.h"

#include "bpf_skel/sample-filter.h"
#include "bpf_skel/sample_filter.skel.h"

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

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
			.flags = expr->sample_flags,
			.value = expr->val,
		};
		bpf_map_update_elem(fd, &i, &entry, BPF_ANY);
		i++;
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

struct perf_bpf_filter_expr *perf_bpf_filter_expr__new(unsigned long sample_flags,
						       enum perf_bpf_filter_op op,
						       unsigned long val)
{
	struct perf_bpf_filter_expr *expr;

	expr = malloc(sizeof(*expr));
	if (expr != NULL) {
		expr->sample_flags = sample_flags;
		expr->op = op;
		expr->val = val;
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
