/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include "util/bpf-filter.h"
#include "util/bpf-filter-flex.h"
#include "util/bpf-filter-bison.h"

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
