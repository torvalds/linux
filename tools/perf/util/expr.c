// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "metricgroup.h"
#include "cpumap.h"
#include "cputopo.h"
#include "debug.h"
#include "evlist.h"
#include "expr.h"
#include <util/expr-bison.h>
#include <util/expr-flex.h>
#include "util/hashmap.h"
#include "util/header.h"
#include "util/pmu.h"
#include "smt.h"
#include "tsc.h"
#include <api/fs/fs.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <ctype.h>
#include <math.h>
#include "pmu.h"

#ifdef PARSER_DEBUG
extern int expr_debug;
#endif

struct expr_id_data {
	union {
		struct {
			double val;
			int source_count;
		} val;
		struct {
			double val;
			const char *metric_name;
			const char *metric_expr;
		} ref;
	};

	enum {
		/* Holding a double value. */
		EXPR_ID_DATA__VALUE,
		/* Reference to another metric. */
		EXPR_ID_DATA__REF,
		/* A reference but the value has been computed. */
		EXPR_ID_DATA__REF_VALUE,
	} kind;
};

static size_t key_hash(long key, void *ctx __maybe_unused)
{
	const char *str = (const char *)key;
	size_t hash = 0;

	while (*str != '\0') {
		hash *= 31;
		hash += *str;
		str++;
	}
	return hash;
}

static bool key_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return !strcmp((const char *)key1, (const char *)key2);
}

struct hashmap *ids__new(void)
{
	struct hashmap *hash;

	hash = hashmap__new(key_hash, key_equal, NULL);
	if (IS_ERR(hash))
		return NULL;
	return hash;
}

void ids__free(struct hashmap *ids)
{
	struct hashmap_entry *cur;
	size_t bkt;

	if (ids == NULL)
		return;

	hashmap__for_each_entry(ids, cur, bkt) {
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}

	hashmap__free(ids);
}

int ids__insert(struct hashmap *ids, const char *id)
{
	struct expr_id_data *data_ptr = NULL, *old_data = NULL;
	char *old_key = NULL;
	int ret;

	ret = hashmap__set(ids, id, data_ptr, &old_key, &old_data);
	if (ret)
		free(data_ptr);
	free(old_key);
	free(old_data);
	return ret;
}

struct hashmap *ids__union(struct hashmap *ids1, struct hashmap *ids2)
{
	size_t bkt;
	struct hashmap_entry *cur;
	int ret;
	struct expr_id_data *old_data = NULL;
	char *old_key = NULL;

	if (!ids1)
		return ids2;

	if (!ids2)
		return ids1;

	if (hashmap__size(ids1) <  hashmap__size(ids2)) {
		struct hashmap *tmp = ids1;

		ids1 = ids2;
		ids2 = tmp;
	}
	hashmap__for_each_entry(ids2, cur, bkt) {
		ret = hashmap__set(ids1, cur->key, cur->value, &old_key, &old_data);
		free(old_key);
		free(old_data);

		if (ret) {
			hashmap__free(ids1);
			hashmap__free(ids2);
			return NULL;
		}
	}
	hashmap__free(ids2);
	return ids1;
}

/* Caller must make sure id is allocated */
int expr__add_id(struct expr_parse_ctx *ctx, const char *id)
{
	return ids__insert(ctx->ids, id);
}

/* Caller must make sure id is allocated */
int expr__add_id_val(struct expr_parse_ctx *ctx, const char *id, double val)
{
	return expr__add_id_val_source_count(ctx, id, val, /*source_count=*/1);
}

/* Caller must make sure id is allocated */
int expr__add_id_val_source_count(struct expr_parse_ctx *ctx, const char *id,
				  double val, int source_count)
{
	struct expr_id_data *data_ptr = NULL, *old_data = NULL;
	char *old_key = NULL;
	int ret;

	data_ptr = malloc(sizeof(*data_ptr));
	if (!data_ptr)
		return -ENOMEM;
	data_ptr->val.val = val;
	data_ptr->val.source_count = source_count;
	data_ptr->kind = EXPR_ID_DATA__VALUE;

	ret = hashmap__set(ctx->ids, id, data_ptr, &old_key, &old_data);
	if (ret)
		free(data_ptr);
	free(old_key);
	free(old_data);
	return ret;
}

int expr__add_ref(struct expr_parse_ctx *ctx, struct metric_ref *ref)
{
	struct expr_id_data *data_ptr = NULL, *old_data = NULL;
	char *old_key = NULL;
	char *name;
	int ret;

	data_ptr = zalloc(sizeof(*data_ptr));
	if (!data_ptr)
		return -ENOMEM;

	name = strdup(ref->metric_name);
	if (!name) {
		free(data_ptr);
		return -ENOMEM;
	}

	/*
	 * Intentionally passing just const char pointers,
	 * originally from 'struct pmu_event' object.
	 * We don't need to change them, so there's no
	 * need to create our own copy.
	 */
	data_ptr->ref.metric_name = ref->metric_name;
	data_ptr->ref.metric_expr = ref->metric_expr;
	data_ptr->kind = EXPR_ID_DATA__REF;

	ret = hashmap__set(ctx->ids, name, data_ptr, &old_key, &old_data);
	if (ret)
		free(data_ptr);

	pr_debug2("adding ref metric %s: %s\n",
		  ref->metric_name, ref->metric_expr);

	free(old_key);
	free(old_data);
	return ret;
}

int expr__get_id(struct expr_parse_ctx *ctx, const char *id,
		 struct expr_id_data **data)
{
	return hashmap__find(ctx->ids, id, data) ? 0 : -1;
}

bool expr__subset_of_ids(struct expr_parse_ctx *haystack,
			 struct expr_parse_ctx *needles)
{
	struct hashmap_entry *cur;
	size_t bkt;
	struct expr_id_data *data;

	hashmap__for_each_entry(needles->ids, cur, bkt) {
		if (expr__get_id(haystack, cur->pkey, &data))
			return false;
	}
	return true;
}


int expr__resolve_id(struct expr_parse_ctx *ctx, const char *id,
		     struct expr_id_data **datap)
{
	struct expr_id_data *data;

	if (expr__get_id(ctx, id, datap) || !*datap) {
		pr_debug("%s not found\n", id);
		return -1;
	}

	data = *datap;

	switch (data->kind) {
	case EXPR_ID_DATA__VALUE:
		pr_debug2("lookup(%s): val %f\n", id, data->val.val);
		break;
	case EXPR_ID_DATA__REF:
		pr_debug2("lookup(%s): ref metric name %s\n", id,
			data->ref.metric_name);
		pr_debug("processing metric: %s ENTRY\n", id);
		data->kind = EXPR_ID_DATA__REF_VALUE;
		if (expr__parse(&data->ref.val, ctx, data->ref.metric_expr)) {
			pr_debug("%s failed to count\n", id);
			return -1;
		}
		pr_debug("processing metric: %s EXIT: %f\n", id, data->ref.val);
		break;
	case EXPR_ID_DATA__REF_VALUE:
		pr_debug2("lookup(%s): ref val %f metric name %s\n", id,
			data->ref.val, data->ref.metric_name);
		break;
	default:
		assert(0);  /* Unreachable. */
	}

	return 0;
}

void expr__del_id(struct expr_parse_ctx *ctx, const char *id)
{
	struct expr_id_data *old_val = NULL;
	char *old_key = NULL;

	hashmap__delete(ctx->ids, id, &old_key, &old_val);
	free(old_key);
	free(old_val);
}

struct expr_parse_ctx *expr__ctx_new(void)
{
	struct expr_parse_ctx *ctx;

	ctx = malloc(sizeof(struct expr_parse_ctx));
	if (!ctx)
		return NULL;

	ctx->ids = hashmap__new(key_hash, key_equal, NULL);
	if (IS_ERR(ctx->ids)) {
		free(ctx);
		return NULL;
	}
	ctx->sctx.user_requested_cpu_list = NULL;
	ctx->sctx.runtime = 0;
	ctx->sctx.system_wide = false;

	return ctx;
}

void expr__ctx_clear(struct expr_parse_ctx *ctx)
{
	struct hashmap_entry *cur;
	size_t bkt;

	hashmap__for_each_entry(ctx->ids, cur, bkt) {
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}
	hashmap__clear(ctx->ids);
}

void expr__ctx_free(struct expr_parse_ctx *ctx)
{
	struct hashmap_entry *cur;
	size_t bkt;

	if (!ctx)
		return;

	zfree(&ctx->sctx.user_requested_cpu_list);
	hashmap__for_each_entry(ctx->ids, cur, bkt) {
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}
	hashmap__free(ctx->ids);
	free(ctx);
}

static int
__expr__parse(double *val, struct expr_parse_ctx *ctx, const char *expr,
	      bool compute_ids)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	pr_debug2("parsing metric: %s\n", expr);

	ret = expr_lex_init_extra(&ctx->sctx, &scanner);
	if (ret)
		return ret;

	buffer = expr__scan_string(expr, scanner);

#ifdef PARSER_DEBUG
	expr_debug = 1;
	expr_set_debug(1, scanner);
#endif

	ret = expr_parse(val, ctx, compute_ids, scanner);

	expr__flush_buffer(buffer, scanner);
	expr__delete_buffer(buffer, scanner);
	expr_lex_destroy(scanner);
	return ret;
}

int expr__parse(double *final_val, struct expr_parse_ctx *ctx,
		const char *expr)
{
	return __expr__parse(final_val, ctx, expr, /*compute_ids=*/false) ? -1 : 0;
}

int expr__find_ids(const char *expr, const char *one,
		   struct expr_parse_ctx *ctx)
{
	int ret = __expr__parse(NULL, ctx, expr, /*compute_ids=*/true);

	if (one)
		expr__del_id(ctx, one);

	return ret;
}

double expr_id_data__value(const struct expr_id_data *data)
{
	if (data->kind == EXPR_ID_DATA__VALUE)
		return data->val.val;
	assert(data->kind == EXPR_ID_DATA__REF_VALUE);
	return data->ref.val;
}

double expr_id_data__source_count(const struct expr_id_data *data)
{
	assert(data->kind == EXPR_ID_DATA__VALUE);
	return data->val.source_count;
}

#if !defined(__i386__) && !defined(__x86_64__)
double arch_get_tsc_freq(void)
{
	return 0.0;
}
#endif

static double has_pmem(void)
{
	static bool has_pmem, cached;
	const char *sysfs = sysfs__mountpoint();
	char path[PATH_MAX];

	if (!cached) {
		snprintf(path, sizeof(path), "%s/firmware/acpi/tables/NFIT", sysfs);
		has_pmem = access(path, F_OK) == 0;
		cached = true;
	}
	return has_pmem ? 1.0 : 0.0;
}

double expr__get_literal(const char *literal, const struct expr_scanner_ctx *ctx)
{
	const struct cpu_topology *topology;
	double result = NAN;

	if (!strcmp("#num_cpus", literal)) {
		result = cpu__max_present_cpu().cpu;
		goto out;
	}
	if (!strcmp("#num_cpus_online", literal)) {
		struct perf_cpu_map *online = cpu_map__online();

		if (online)
			result = perf_cpu_map__nr(online);
		goto out;
	}

	if (!strcasecmp("#system_tsc_freq", literal)) {
		result = arch_get_tsc_freq();
		goto out;
	}

	/*
	 * Assume that topology strings are consistent, such as CPUs "0-1"
	 * wouldn't be listed as "0,1", and so after deduplication the number of
	 * these strings gives an indication of the number of packages, dies,
	 * etc.
	 */
	if (!strcasecmp("#smt_on", literal)) {
		result = smt_on() ? 1.0 : 0.0;
		goto out;
	}
	if (!strcmp("#core_wide", literal)) {
		result = core_wide(ctx->system_wide, ctx->user_requested_cpu_list)
			? 1.0 : 0.0;
		goto out;
	}
	if (!strcmp("#num_packages", literal)) {
		topology = online_topology();
		result = topology->package_cpus_lists;
		goto out;
	}
	if (!strcmp("#num_dies", literal)) {
		topology = online_topology();
		result = topology->die_cpus_lists;
		goto out;
	}
	if (!strcmp("#num_cores", literal)) {
		topology = online_topology();
		result = topology->core_cpus_lists;
		goto out;
	}
	if (!strcmp("#slots", literal)) {
		result = perf_pmu__cpu_slots_per_cycle();
		goto out;
	}
	if (!strcmp("#has_pmem", literal)) {
		result = has_pmem();
		goto out;
	}

	pr_err("Unrecognized literal '%s'", literal);
out:
	pr_debug2("literal: %s = %f\n", literal, result);
	return result;
}

/* Does the event 'id' parse? Determine via ctx->ids if possible. */
double expr__has_event(const struct expr_parse_ctx *ctx, bool compute_ids, const char *id)
{
	struct evlist *tmp;
	double ret;

	if (hashmap__find(ctx->ids, id, /*value=*/NULL))
		return 1.0;

	if (!compute_ids)
		return 0.0;

	tmp = evlist__new();
	if (!tmp)
		return NAN;

	if (strchr(id, '@')) {
		char *tmp_id, *p;

		tmp_id = strdup(id);
		if (!tmp_id) {
			ret = NAN;
			goto out;
		}
		p = strchr(tmp_id, '@');
		*p = '/';
		p = strrchr(tmp_id, '@');
		*p = '/';
		ret = parse_event(tmp, tmp_id) ? 0 : 1;
		free(tmp_id);
	} else {
		ret = parse_event(tmp, id) ? 0 : 1;
	}
out:
	evlist__delete(tmp);
	return ret;
}

double expr__strcmp_cpuid_str(const struct expr_parse_ctx *ctx __maybe_unused,
		       bool compute_ids __maybe_unused, const char *test_id)
{
	double ret;
	struct perf_pmu *pmu = perf_pmus__find_core_pmu();
	char *cpuid = perf_pmu__getcpuid(pmu);

	if (!cpuid)
		return NAN;

	ret = !strcmp_cpuid_str(test_id, cpuid);

	free(cpuid);
	return ret;
}
