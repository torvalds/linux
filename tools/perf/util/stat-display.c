#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <math.h>
#include <perf/cpumap.h>
#include "color.h"
#include "counts.h"
#include "evlist.h"
#include "evsel.h"
#include "stat.h"
#include "top.h"
#include "thread_map.h"
#include "cpumap.h"
#include "string2.h"
#include <linux/ctype.h>
#include "cgroup.h"
#include <api/fs/fs.h>
#include "util.h"
#include "iostat.h"
#include "pmu.h"
#include "pmus.h"

#define CNTR_NOT_SUPPORTED	"<not supported>"
#define CNTR_NOT_COUNTED	"<not counted>"

#define MGROUP_LEN   50
#define METRIC_LEN   38
#define EVNAME_LEN   32
#define COUNTS_LEN   18
#define INTERVAL_LEN 16
#define CGROUP_LEN   16
#define COMM_LEN     16
#define PID_LEN       7
#define CPUS_LEN      4

static int aggr_header_lens[] = {
	[AGGR_CORE] 	= 18,
	[AGGR_CACHE]	= 22,
	[AGGR_DIE] 	= 12,
	[AGGR_SOCKET] 	= 6,
	[AGGR_NODE] 	= 6,
	[AGGR_NONE] 	= 6,
	[AGGR_THREAD] 	= 16,
	[AGGR_GLOBAL] 	= 0,
};

static const char *aggr_header_csv[] = {
	[AGGR_CORE] 	= 	"core,cpus,",
	[AGGR_CACHE]	= 	"cache,cpus,",
	[AGGR_DIE] 	= 	"die,cpus,",
	[AGGR_SOCKET] 	= 	"socket,cpus,",
	[AGGR_NONE] 	= 	"cpu,",
	[AGGR_THREAD] 	= 	"comm-pid,",
	[AGGR_NODE] 	= 	"node,",
	[AGGR_GLOBAL] 	=	""
};

static const char *aggr_header_std[] = {
	[AGGR_CORE] 	= 	"core",
	[AGGR_CACHE] 	= 	"cache",
	[AGGR_DIE] 	= 	"die",
	[AGGR_SOCKET] 	= 	"socket",
	[AGGR_NONE] 	= 	"cpu",
	[AGGR_THREAD] 	= 	"comm-pid",
	[AGGR_NODE] 	= 	"node",
	[AGGR_GLOBAL] 	=	""
};

static void print_running_std(struct perf_stat_config *config, u64 run, u64 ena)
{
	if (run != ena)
		fprintf(config->output, "  (%.2f%%)", 100.0 * run / ena);
}

static void print_running_csv(struct perf_stat_config *config, u64 run, u64 ena)
{
	double enabled_percent = 100;

	if (run != ena)
		enabled_percent = 100 * run / ena;
	fprintf(config->output, "%s%" PRIu64 "%s%.2f",
		config->csv_sep, run, config->csv_sep, enabled_percent);
}

static void print_running_json(struct perf_stat_config *config, u64 run, u64 ena)
{
	double enabled_percent = 100;

	if (run != ena)
		enabled_percent = 100 * run / ena;
	fprintf(config->output, "\"event-runtime\" : %" PRIu64 ", \"pcnt-running\" : %.2f, ",
		run, enabled_percent);
}

static void print_running(struct perf_stat_config *config,
			  u64 run, u64 ena, bool before_metric)
{
	if (config->json_output) {
		if (before_metric)
			print_running_json(config, run, ena);
	} else if (config->csv_output) {
		if (before_metric)
			print_running_csv(config, run, ena);
	} else {
		if (!before_metric)
			print_running_std(config, run, ena);
	}
}

static void print_noise_pct_std(struct perf_stat_config *config,
				double pct)
{
	if (pct)
		fprintf(config->output, "  ( +-%6.2f%% )", pct);
}

static void print_noise_pct_csv(struct perf_stat_config *config,
				double pct)
{
	fprintf(config->output, "%s%.2f%%", config->csv_sep, pct);
}

static void print_noise_pct_json(struct perf_stat_config *config,
				 double pct)
{
	fprintf(config->output, "\"variance\" : %.2f, ", pct);
}

static void print_noise_pct(struct perf_stat_config *config,
			    double total, double avg, bool before_metric)
{
	double pct = rel_stddev_stats(total, avg);

	if (config->json_output) {
		if (before_metric)
			print_noise_pct_json(config, pct);
	} else if (config->csv_output) {
		if (before_metric)
			print_noise_pct_csv(config, pct);
	} else {
		if (!before_metric)
			print_noise_pct_std(config, pct);
	}
}

static void print_noise(struct perf_stat_config *config,
			struct evsel *evsel, double avg, bool before_metric)
{
	struct perf_stat_evsel *ps;

	if (config->run_count == 1)
		return;

	ps = evsel->stats;
	print_noise_pct(config, stddev_stats(&ps->res_stats), avg, before_metric);
}

static void print_cgroup_std(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, " %-*s", CGROUP_LEN, cgrp_name);
}

static void print_cgroup_csv(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, "%s%s", config->csv_sep, cgrp_name);
}

static void print_cgroup_json(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, "\"cgroup\" : \"%s\", ", cgrp_name);
}

static void print_cgroup(struct perf_stat_config *config, struct cgroup *cgrp)
{
	if (nr_cgroups || config->cgroup_list) {
		const char *cgrp_name = cgrp ? cgrp->name  : "";

		if (config->json_output)
			print_cgroup_json(config, cgrp_name);
		else if (config->csv_output)
			print_cgroup_csv(config, cgrp_name);
		else
			print_cgroup_std(config, cgrp_name);
	}
}

static void print_aggr_id_std(struct perf_stat_config *config,
			      struct evsel *evsel, struct aggr_cpu_id id, int aggr_nr)
{
	FILE *output = config->output;
	int idx = config->aggr_mode;
	char buf[128];

	switch (config->aggr_mode) {
	case AGGR_CORE:
		snprintf(buf, sizeof(buf), "S%d-D%d-C%d", id.socket, id.die, id.core);
		break;
	case AGGR_CACHE:
		snprintf(buf, sizeof(buf), "S%d-D%d-L%d-ID%d",
			 id.socket, id.die, id.cache_lvl, id.cache);
		break;
	case AGGR_DIE:
		snprintf(buf, sizeof(buf), "S%d-D%d", id.socket, id.die);
		break;
	case AGGR_SOCKET:
		snprintf(buf, sizeof(buf), "S%d", id.socket);
		break;
	case AGGR_NODE:
		snprintf(buf, sizeof(buf), "N%d", id.node);
		break;
	case AGGR_NONE:
		if (evsel->percore && !config->percore_show_thread) {
			snprintf(buf, sizeof(buf), "S%d-D%d-C%d ",
				id.socket, id.die, id.core);
			fprintf(output, "%-*s ",
				aggr_header_lens[AGGR_CORE], buf);
		} else if (id.cpu.cpu > -1) {
			fprintf(output, "CPU%-*d ",
				aggr_header_lens[AGGR_NONE] - 3, id.cpu.cpu);
		}
		return;
	case AGGR_THREAD:
		fprintf(output, "%*s-%-*d ",
			COMM_LEN, perf_thread_map__comm(evsel->core.threads, id.thread_idx),
			PID_LEN, perf_thread_map__pid(evsel->core.threads, id.thread_idx));
		return;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		return;
	}

	fprintf(output, "%-*s %*d ", aggr_header_lens[idx], buf, 4, aggr_nr);
}

static void print_aggr_id_csv(struct perf_stat_config *config,
			      struct evsel *evsel, struct aggr_cpu_id id, int aggr_nr)
{
	FILE *output = config->output;
	const char *sep = config->csv_sep;

	switch (config->aggr_mode) {
	case AGGR_CORE:
		fprintf(output, "S%d-D%d-C%d%s%d%s",
			id.socket, id.die, id.core, sep, aggr_nr, sep);
		break;
	case AGGR_CACHE:
		fprintf(config->output, "S%d-D%d-L%d-ID%d%s%d%s",
			id.socket, id.die, id.cache_lvl, id.cache, sep, aggr_nr, sep);
		break;
	case AGGR_DIE:
		fprintf(output, "S%d-D%d%s%d%s",
			id.socket, id.die, sep, aggr_nr, sep);
		break;
	case AGGR_SOCKET:
		fprintf(output, "S%d%s%d%s",
			id.socket, sep, aggr_nr, sep);
		break;
	case AGGR_NODE:
		fprintf(output, "N%d%s%d%s",
			id.node, sep, aggr_nr, sep);
		break;
	case AGGR_NONE:
		if (evsel->percore && !config->percore_show_thread) {
			fprintf(output, "S%d-D%d-C%d%s",
				id.socket, id.die, id.core, sep);
		} else if (id.cpu.cpu > -1) {
			fprintf(output, "CPU%d%s",
				id.cpu.cpu, sep);
		}
		break;
	case AGGR_THREAD:
		fprintf(output, "%s-%d%s",
			perf_thread_map__comm(evsel->core.threads, id.thread_idx),
			perf_thread_map__pid(evsel->core.threads, id.thread_idx),
			sep);
		break;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		break;
	}
}

static void print_aggr_id_json(struct perf_stat_config *config,
			       struct evsel *evsel, struct aggr_cpu_id id, int aggr_nr)
{
	FILE *output = config->output;

	switch (config->aggr_mode) {
	case AGGR_CORE:
		fprintf(output, "\"core\" : \"S%d-D%d-C%d\", \"aggregate-number\" : %d, ",
			id.socket, id.die, id.core, aggr_nr);
		break;
	case AGGR_CACHE:
		fprintf(output, "\"cache\" : \"S%d-D%d-L%d-ID%d\", \"aggregate-number\" : %d, ",
			id.socket, id.die, id.cache_lvl, id.cache, aggr_nr);
		break;
	case AGGR_DIE:
		fprintf(output, "\"die\" : \"S%d-D%d\", \"aggregate-number\" : %d, ",
			id.socket, id.die, aggr_nr);
		break;
	case AGGR_SOCKET:
		fprintf(output, "\"socket\" : \"S%d\", \"aggregate-number\" : %d, ",
			id.socket, aggr_nr);
		break;
	case AGGR_NODE:
		fprintf(output, "\"node\" : \"N%d\", \"aggregate-number\" : %d, ",
			id.node, aggr_nr);
		break;
	case AGGR_NONE:
		if (evsel->percore && !config->percore_show_thread) {
			fprintf(output, "\"core\" : \"S%d-D%d-C%d\"",
				id.socket, id.die, id.core);
		} else if (id.cpu.cpu > -1) {
			fprintf(output, "\"cpu\" : \"%d\", ",
				id.cpu.cpu);
		}
		break;
	case AGGR_THREAD:
		fprintf(output, "\"thread\" : \"%s-%d\", ",
			perf_thread_map__comm(evsel->core.threads, id.thread_idx),
			perf_thread_map__pid(evsel->core.threads, id.thread_idx));
		break;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		break;
	}
}

static void aggr_printout(struct perf_stat_config *config,
			  struct evsel *evsel, struct aggr_cpu_id id, int aggr_nr)
{
	if (config->json_output)
		print_aggr_id_json(config, evsel, id, aggr_nr);
	else if (config->csv_output)
		print_aggr_id_csv(config, evsel, id, aggr_nr);
	else
		print_aggr_id_std(config, evsel, id, aggr_nr);
}

struct outstate {
	FILE *fh;
	bool newline;
	bool first;
	const char *prefix;
	int  nfields;
	int  aggr_nr;
	struct aggr_cpu_id id;
	struct evsel *evsel;
	struct cgroup *cgrp;
};

static void new_line_std(struct perf_stat_config *config __maybe_unused,
			 void *ctx)
{
	struct outstate *os = ctx;

	os->newline = true;
}

static inline void __new_line_std_csv(struct perf_stat_config *config,
				      struct outstate *os)
{
	fputc('\n', os->fh);
	if (os->prefix)
		fputs(os->prefix, os->fh);
	aggr_printout(config, os->evsel, os->id, os->aggr_nr);
}

static inline void __new_line_std(struct outstate *os)
{
	fprintf(os->fh, "                                                 ");
}

static void do_new_line_std(struct perf_stat_config *config,
			    struct outstate *os)
{
	__new_line_std_csv(config, os);
	if (config->aggr_mode == AGGR_NONE)
		fprintf(os->fh, "        ");
	__new_line_std(os);
}

static void print_metric_std(struct perf_stat_config *config,
			     void *ctx, const char *color, const char *fmt,
			     const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	int n;
	bool newline = os->newline;

	os->newline = false;

	if (unit == NULL || fmt == NULL) {
		fprintf(out, "%-*s", METRIC_LEN, "");
		return;
	}

	if (newline)
		do_new_line_std(config, os);

	n = fprintf(out, " # ");
	if (color)
		n += color_fprintf(out, color, fmt, val);
	else
		n += fprintf(out, fmt, val);
	fprintf(out, " %-*s", METRIC_LEN - n - 1, unit);
}

static void new_line_csv(struct perf_stat_config *config, void *ctx)
{
	struct outstate *os = ctx;
	int i;

	__new_line_std_csv(config, os);
	for (i = 0; i < os->nfields; i++)
		fputs(config->csv_sep, os->fh);
}

static void print_metric_csv(struct perf_stat_config *config __maybe_unused,
			     void *ctx,
			     const char *color __maybe_unused,
			     const char *fmt, const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[64], *vals, *ends;

	if (unit == NULL || fmt == NULL) {
		fprintf(out, "%s%s", config->csv_sep, config->csv_sep);
		return;
	}
	snprintf(buf, sizeof(buf), fmt, val);
	ends = vals = skip_spaces(buf);
	while (isdigit(*ends) || *ends == '.')
		ends++;
	*ends = 0;
	fprintf(out, "%s%s%s%s", config->csv_sep, vals, config->csv_sep, skip_spaces(unit));
}

static void print_metric_json(struct perf_stat_config *config __maybe_unused,
			     void *ctx,
			     const char *color __maybe_unused,
			     const char *fmt __maybe_unused,
			     const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;

	fprintf(out, "\"metric-value\" : \"%f\", ", val);
	fprintf(out, "\"metric-unit\" : \"%s\"", unit);
	if (!config->metric_only)
		fprintf(out, "}");
}

static void new_line_json(struct perf_stat_config *config, void *ctx)
{
	struct outstate *os = ctx;

	fputs("\n{", os->fh);
	if (os->prefix)
		fprintf(os->fh, "%s", os->prefix);
	aggr_printout(config, os->evsel, os->id, os->aggr_nr);
}

static void print_metricgroup_header_json(struct perf_stat_config *config,
					  void *ctx,
					  const char *metricgroup_name)
{
	if (!metricgroup_name)
		return;

	fprintf(config->output, "\"metricgroup\" : \"%s\"}", metricgroup_name);
	new_line_json(config, ctx);
}

static void print_metricgroup_header_csv(struct perf_stat_config *config,
					 void *ctx,
					 const char *metricgroup_name)
{
	struct outstate *os = ctx;
	int i;

	if (!metricgroup_name) {
		/* Leave space for running and enabling */
		for (i = 0; i < os->nfields - 2; i++)
			fputs(config->csv_sep, os->fh);
		return;
	}

	for (i = 0; i < os->nfields; i++)
		fputs(config->csv_sep, os->fh);
	fprintf(config->output, "%s", metricgroup_name);
	new_line_csv(config, ctx);
}

static void print_metricgroup_header_std(struct perf_stat_config *config,
					 void *ctx,
					 const char *metricgroup_name)
{
	struct outstate *os = ctx;
	int n;

	if (!metricgroup_name) {
		__new_line_std(os);
		return;
	}

	n = fprintf(config->output, " %*s", EVNAME_LEN, metricgroup_name);

	fprintf(config->output, "%*s", MGROUP_LEN - n - 1, "");
}

/* Filter out some columns that don't work well in metrics only mode */

static bool valid_only_metric(const char *unit)
{
	if (!unit)
		return false;
	if (strstr(unit, "/sec") ||
	    strstr(unit, "CPUs utilized"))
		return false;
	return true;
}

static const char *fixunit(char *buf, struct evsel *evsel,
			   const char *unit)
{
	if (!strncmp(unit, "of all", 6)) {
		snprintf(buf, 1024, "%s %s", evsel__name(evsel),
			 unit);
		return buf;
	}
	return unit;
}

static void print_metric_only(struct perf_stat_config *config,
			      void *ctx, const char *color, const char *fmt,
			      const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[1024], str[1024];
	unsigned mlen = config->metric_only_len;

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(buf, os->evsel, unit);
	if (mlen < strlen(unit))
		mlen = strlen(unit) + 1;

	if (color)
		mlen += strlen(color) + sizeof(PERF_COLOR_RESET) - 1;

	color_snprintf(str, sizeof(str), color ?: "", fmt, val);
	fprintf(out, "%*s ", mlen, str);
	os->first = false;
}

static void print_metric_only_csv(struct perf_stat_config *config __maybe_unused,
				  void *ctx, const char *color __maybe_unused,
				  const char *fmt,
				  const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[64], *vals, *ends;
	char tbuf[1024];

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);
	snprintf(buf, sizeof buf, fmt, val);
	ends = vals = skip_spaces(buf);
	while (isdigit(*ends) || *ends == '.')
		ends++;
	*ends = 0;
	fprintf(out, "%s%s", vals, config->csv_sep);
	os->first = false;
}

static void print_metric_only_json(struct perf_stat_config *config __maybe_unused,
				  void *ctx, const char *color __maybe_unused,
				  const char *fmt,
				  const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[64], *vals, *ends;
	char tbuf[1024];

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);
	snprintf(buf, sizeof(buf), fmt, val);
	ends = vals = skip_spaces(buf);
	while (isdigit(*ends) || *ends == '.')
		ends++;
	*ends = 0;
	if (!unit[0] || !vals[0])
		return;
	fprintf(out, "%s\"%s\" : \"%s\"", os->first ? "" : ", ", unit, vals);
	os->first = false;
}

static void new_line_metric(struct perf_stat_config *config __maybe_unused,
			    void *ctx __maybe_unused)
{
}

static void print_metric_header(struct perf_stat_config *config,
				void *ctx, const char *color __maybe_unused,
				const char *fmt __maybe_unused,
				const char *unit, double val __maybe_unused)
{
	struct outstate *os = ctx;
	char tbuf[1024];

	/* In case of iostat, print metric header for first root port only */
	if (config->iostat_run &&
	    os->evsel->priv != os->evsel->evlist->selected->priv)
		return;

	if (os->evsel->cgrp != os->cgrp)
		return;

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);

	if (config->json_output)
		return;
	else if (config->csv_output)
		fprintf(os->fh, "%s%s", unit, config->csv_sep);
	else
		fprintf(os->fh, "%*s ", config->metric_only_len, unit);
}

static void print_counter_value_std(struct perf_stat_config *config,
				    struct evsel *evsel, double avg, bool ok)
{
	FILE *output = config->output;
	double sc =  evsel->scale;
	const char *fmt;
	const char *bad_count = evsel->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED;

	if (config->big_num)
		fmt = floor(sc) != sc ? "%'*.2f " : "%'*.0f ";
	else
		fmt = floor(sc) != sc ? "%*.2f " : "%*.0f ";

	if (ok)
		fprintf(output, fmt, COUNTS_LEN, avg);
	else
		fprintf(output, "%*s ", COUNTS_LEN, bad_count);

	if (evsel->unit)
		fprintf(output, "%-*s ", config->unit_width, evsel->unit);

	fprintf(output, "%-*s", EVNAME_LEN, evsel__name(evsel));
}

static void print_counter_value_csv(struct perf_stat_config *config,
				    struct evsel *evsel, double avg, bool ok)
{
	FILE *output = config->output;
	double sc =  evsel->scale;
	const char *sep = config->csv_sep;
	const char *fmt = floor(sc) != sc ? "%.2f%s" : "%.0f%s";
	const char *bad_count = evsel->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED;

	if (ok)
		fprintf(output, fmt, avg, sep);
	else
		fprintf(output, "%s%s", bad_count, sep);

	if (evsel->unit)
		fprintf(output, "%s%s", evsel->unit, sep);

	fprintf(output, "%s", evsel__name(evsel));
}

static void print_counter_value_json(struct perf_stat_config *config,
				     struct evsel *evsel, double avg, bool ok)
{
	FILE *output = config->output;
	const char *bad_count = evsel->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED;

	if (ok)
		fprintf(output, "\"counter-value\" : \"%f\", ", avg);
	else
		fprintf(output, "\"counter-value\" : \"%s\", ", bad_count);

	if (evsel->unit)
		fprintf(output, "\"unit\" : \"%s\", ", evsel->unit);

	fprintf(output, "\"event\" : \"%s\", ", evsel__name(evsel));
}

static void print_counter_value(struct perf_stat_config *config,
				struct evsel *evsel, double avg, bool ok)
{
	if (config->json_output)
		print_counter_value_json(config, evsel, avg, ok);
	else if (config->csv_output)
		print_counter_value_csv(config, evsel, avg, ok);
	else
		print_counter_value_std(config, evsel, avg, ok);
}

static void abs_printout(struct perf_stat_config *config,
			 struct aggr_cpu_id id, int aggr_nr,
			 struct evsel *evsel, double avg, bool ok)
{
	aggr_printout(config, evsel, id, aggr_nr);
	print_counter_value(config, evsel, avg, ok);
	print_cgroup(config, evsel->cgrp);
}

static bool is_mixed_hw_group(struct evsel *counter)
{
	struct evlist *evlist = counter->evlist;
	u32 pmu_type = counter->core.attr.type;
	struct evsel *pos;

	if (counter->core.nr_members < 2)
		return false;

	evlist__for_each_entry(evlist, pos) {
		/* software events can be part of any hardware group */
		if (pos->core.attr.type == PERF_TYPE_SOFTWARE)
			continue;
		if (pmu_type == PERF_TYPE_SOFTWARE) {
			pmu_type = pos->core.attr.type;
			continue;
		}
		if (pmu_type != pos->core.attr.type)
			return true;
	}

	return false;
}

static bool evlist__has_hybrid(struct evlist *evlist)
{
	struct evsel *evsel;

	if (perf_pmus__num_core_pmus() == 1)
		return false;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.is_pmu_core)
			return true;
	}

	return false;
}

static void printout(struct perf_stat_config *config, struct outstate *os,
		     double uval, u64 run, u64 ena, double noise, int aggr_idx)
{
	struct perf_stat_output_ctx out;
	print_metric_t pm;
	new_line_t nl;
	print_metricgroup_header_t pmh;
	bool ok = true;
	struct evsel *counter = os->evsel;

	if (config->csv_output) {
		pm = config->metric_only ? print_metric_only_csv : print_metric_csv;
		nl = config->metric_only ? new_line_metric : new_line_csv;
		pmh = print_metricgroup_header_csv;
		os->nfields = 4 + (counter->cgrp ? 1 : 0);
	} else if (config->json_output) {
		pm = config->metric_only ? print_metric_only_json : print_metric_json;
		nl = config->metric_only ? new_line_metric : new_line_json;
		pmh = print_metricgroup_header_json;
	} else {
		pm = config->metric_only ? print_metric_only : print_metric_std;
		nl = config->metric_only ? new_line_metric : new_line_std;
		pmh = print_metricgroup_header_std;
	}

	if (run == 0 || ena == 0 || counter->counts->scaled == -1) {
		if (config->metric_only) {
			pm(config, os, NULL, "", "", 0);
			return;
		}

		ok = false;

		if (counter->supported) {
			if (!evlist__has_hybrid(counter->evlist)) {
				config->print_free_counters_hint = 1;
				if (is_mixed_hw_group(counter))
					config->print_mixed_hw_group_error = 1;
			}
		}
	}

	out.print_metric = pm;
	out.new_line = nl;
	out.print_metricgroup_header = pmh;
	out.ctx = os;
	out.force_header = false;

	if (!config->metric_only && !counter->default_metricgroup) {
		abs_printout(config, os->id, os->aggr_nr, counter, uval, ok);

		print_noise(config, counter, noise, /*before_metric=*/true);
		print_running(config, run, ena, /*before_metric=*/true);
	}

	if (ok) {
		if (!config->metric_only && counter->default_metricgroup) {
			void *from = NULL;

			aggr_printout(config, os->evsel, os->id, os->aggr_nr);
			/* Print out all the metricgroup with the same metric event. */
			do {
				int num = 0;

				/* Print out the new line for the next new metricgroup. */
				if (from) {
					if (config->json_output)
						new_line_json(config, (void *)os);
					else
						__new_line_std_csv(config, os);
				}

				print_noise(config, counter, noise, /*before_metric=*/true);
				print_running(config, run, ena, /*before_metric=*/true);
				from = perf_stat__print_shadow_stats_metricgroup(config, counter, aggr_idx,
										 &num, from, &out,
										 &config->metric_events);
			} while (from != NULL);
		} else
			perf_stat__print_shadow_stats(config, counter, uval, aggr_idx,
						      &out, &config->metric_events);
	} else {
		pm(config, os, /*color=*/NULL, /*format=*/NULL, /*unit=*/"", /*val=*/0);
	}

	if (!config->metric_only) {
		print_noise(config, counter, noise, /*before_metric=*/false);
		print_running(config, run, ena, /*before_metric=*/false);
	}
}

static void uniquify_event_name(struct evsel *counter)
{
	char *new_name;
	char *config;
	int ret = 0;

	if (counter->uniquified_name || counter->use_config_name ||
	    !counter->pmu_name || !strncmp(evsel__name(counter), counter->pmu_name,
					   strlen(counter->pmu_name)))
		return;

	config = strchr(counter->name, '/');
	if (config) {
		if (asprintf(&new_name,
			     "%s%s", counter->pmu_name, config) > 0) {
			free(counter->name);
			counter->name = new_name;
		}
	} else {
		if (evsel__is_hybrid(counter)) {
			ret = asprintf(&new_name, "%s/%s/",
				       counter->pmu_name, counter->name);
		} else {
			ret = asprintf(&new_name, "%s [%s]",
				       counter->name, counter->pmu_name);
		}

		if (ret) {
			free(counter->name);
			counter->name = new_name;
		}
	}

	counter->uniquified_name = true;
}

static bool hybrid_uniquify(struct evsel *evsel, struct perf_stat_config *config)
{
	return evsel__is_hybrid(evsel) && !config->hybrid_merge;
}

static void uniquify_counter(struct perf_stat_config *config, struct evsel *counter)
{
	if (config->no_merge || hybrid_uniquify(counter, config))
		uniquify_event_name(counter);
}

/**
 * should_skip_zero_count() - Check if the event should print 0 values.
 * @config: The perf stat configuration (including aggregation mode).
 * @counter: The evsel with its associated cpumap.
 * @id: The aggregation id that is being queried.
 *
 * Due to mismatch between the event cpumap or thread-map and the
 * aggregation mode, sometimes it'd iterate the counter with the map
 * which does not contain any values.
 *
 * For example, uncore events have dedicated CPUs to manage them,
 * result for other CPUs should be zero and skipped.
 *
 * Return: %true if the value should NOT be printed, %false if the value
 * needs to be printed like "<not counted>" or "<not supported>".
 */
static bool should_skip_zero_counter(struct perf_stat_config *config,
				     struct evsel *counter,
				     const struct aggr_cpu_id *id)
{
	struct perf_cpu cpu;
	int idx;

	/*
	 * Skip value 0 when enabling --per-thread globally,
	 * otherwise it will have too many 0 output.
	 */
	if (config->aggr_mode == AGGR_THREAD && config->system_wide)
		return true;

	/* Tool events have the software PMU but are only gathered on 1. */
	if (evsel__is_tool(counter))
		return true;

	/*
	 * Skip value 0 when it's an uncore event and the given aggr id
	 * does not belong to the PMU cpumask.
	 */
	if (!counter->pmu || !counter->pmu->is_uncore)
		return false;

	perf_cpu_map__for_each_cpu(cpu, idx, counter->pmu->cpus) {
		struct aggr_cpu_id own_id = config->aggr_get_id(config, cpu);

		if (aggr_cpu_id__equal(id, &own_id))
			return false;
	}
	return true;
}

static void print_counter_aggrdata(struct perf_stat_config *config,
				   struct evsel *counter, int aggr_idx,
				   struct outstate *os)
{
	FILE *output = config->output;
	u64 ena, run, val;
	double uval;
	struct perf_stat_evsel *ps = counter->stats;
	struct perf_stat_aggr *aggr = &ps->aggr[aggr_idx];
	struct aggr_cpu_id id = config->aggr_map->map[aggr_idx];
	double avg = aggr->counts.val;
	bool metric_only = config->metric_only;

	os->id = id;
	os->aggr_nr = aggr->nr;
	os->evsel = counter;

	/* Skip already merged uncore/hybrid events */
	if (counter->merged_stat)
		return;

	uniquify_counter(config, counter);

	val = aggr->counts.val;
	ena = aggr->counts.ena;
	run = aggr->counts.run;

	if (perf_stat__skip_metric_event(counter, &config->metric_events, ena, run))
		return;

	if (val == 0 && should_skip_zero_counter(config, counter, &id))
		return;

	if (!metric_only) {
		if (config->json_output)
			fputc('{', output);
		if (os->prefix)
			fprintf(output, "%s", os->prefix);
		else if (config->summary && config->csv_output &&
			 !config->no_csv_summary && !config->interval)
			fprintf(output, "%s%s", "summary", config->csv_sep);
	}

	uval = val * counter->scale;

	printout(config, os, uval, run, ena, avg, aggr_idx);

	if (!metric_only)
		fputc('\n', output);
}

static void print_metric_begin(struct perf_stat_config *config,
			       struct evlist *evlist,
			       struct outstate *os, int aggr_idx)
{
	struct perf_stat_aggr *aggr;
	struct aggr_cpu_id id;
	struct evsel *evsel;

	os->first = true;
	if (!config->metric_only)
		return;

	if (config->json_output)
		fputc('{', config->output);
	if (os->prefix)
		fprintf(config->output, "%s", os->prefix);

	evsel = evlist__first(evlist);
	id = config->aggr_map->map[aggr_idx];
	aggr = &evsel->stats->aggr[aggr_idx];
	aggr_printout(config, evsel, id, aggr->nr);

	print_cgroup(config, os->cgrp ? : evsel->cgrp);
}

static void print_metric_end(struct perf_stat_config *config, struct outstate *os)
{
	FILE *output = config->output;

	if (!config->metric_only)
		return;

	if (config->json_output) {
		if (os->first)
			fputs("\"metric-value\" : \"none\"", output);
		fputc('}', output);
	}
	fputc('\n', output);
}

static void print_aggr(struct perf_stat_config *config,
		       struct evlist *evlist,
		       struct outstate *os)
{
	struct evsel *counter;
	int aggr_idx;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	/*
	 * With metric_only everything is on a single line.
	 * Without each counter has its own line.
	 */
	cpu_aggr_map__for_each_idx(aggr_idx, config->aggr_map) {
		print_metric_begin(config, evlist, os, aggr_idx);

		evlist__for_each_entry(evlist, counter) {
			print_counter_aggrdata(config, counter, aggr_idx, os);
		}
		print_metric_end(config, os);
	}
}

static void print_aggr_cgroup(struct perf_stat_config *config,
			      struct evlist *evlist,
			      struct outstate *os)
{
	struct evsel *counter, *evsel;
	int aggr_idx;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	evlist__for_each_entry(evlist, evsel) {
		if (os->cgrp == evsel->cgrp)
			continue;

		os->cgrp = evsel->cgrp;

		cpu_aggr_map__for_each_idx(aggr_idx, config->aggr_map) {
			print_metric_begin(config, evlist, os, aggr_idx);

			evlist__for_each_entry(evlist, counter) {
				if (counter->cgrp != os->cgrp)
					continue;

				print_counter_aggrdata(config, counter, aggr_idx, os);
			}
			print_metric_end(config, os);
		}
	}
}

static void print_counter(struct perf_stat_config *config,
			  struct evsel *counter, struct outstate *os)
{
	int aggr_idx;

	/* AGGR_THREAD doesn't have config->aggr_get_id */
	if (!config->aggr_map)
		return;

	cpu_aggr_map__for_each_idx(aggr_idx, config->aggr_map) {
		print_counter_aggrdata(config, counter, aggr_idx, os);
	}
}

static void print_no_aggr_metric(struct perf_stat_config *config,
				 struct evlist *evlist,
				 struct outstate *os)
{
	int all_idx;
	struct perf_cpu cpu;

	perf_cpu_map__for_each_cpu(cpu, all_idx, evlist->core.user_requested_cpus) {
		struct evsel *counter;
		bool first = true;

		evlist__for_each_entry(evlist, counter) {
			u64 ena, run, val;
			double uval;
			struct perf_stat_evsel *ps = counter->stats;
			int aggr_idx = perf_cpu_map__idx(evsel__cpus(counter), cpu);

			if (aggr_idx < 0)
				continue;

			os->evsel = counter;
			os->id = aggr_cpu_id__cpu(cpu, /*data=*/NULL);
			if (first) {
				print_metric_begin(config, evlist, os, aggr_idx);
				first = false;
			}
			val = ps->aggr[aggr_idx].counts.val;
			ena = ps->aggr[aggr_idx].counts.ena;
			run = ps->aggr[aggr_idx].counts.run;

			uval = val * counter->scale;
			printout(config, os, uval, run, ena, 1.0, aggr_idx);
		}
		if (!first)
			print_metric_end(config, os);
	}
}

static void print_metric_headers_std(struct perf_stat_config *config,
				     bool no_indent)
{
	fputc(' ', config->output);

	if (!no_indent) {
		int len = aggr_header_lens[config->aggr_mode];

		if (nr_cgroups || config->cgroup_list)
			len += CGROUP_LEN + 1;

		fprintf(config->output, "%*s", len, "");
	}
}

static void print_metric_headers_csv(struct perf_stat_config *config,
				     bool no_indent __maybe_unused)
{
	if (config->interval)
		fputs("time,", config->output);
	if (!config->iostat_run)
		fputs(aggr_header_csv[config->aggr_mode], config->output);
}

static void print_metric_headers_json(struct perf_stat_config *config __maybe_unused,
				      bool no_indent __maybe_unused)
{
}

static void print_metric_headers(struct perf_stat_config *config,
				 struct evlist *evlist, bool no_indent)
{
	struct evsel *counter;
	struct outstate os = {
		.fh = config->output
	};
	struct perf_stat_output_ctx out = {
		.ctx = &os,
		.print_metric = print_metric_header,
		.new_line = new_line_metric,
		.force_header = true,
	};

	if (config->json_output)
		print_metric_headers_json(config, no_indent);
	else if (config->csv_output)
		print_metric_headers_csv(config, no_indent);
	else
		print_metric_headers_std(config, no_indent);

	if (config->iostat_run)
		iostat_print_header_prefix(config);

	if (config->cgroup_list)
		os.cgrp = evlist__first(evlist)->cgrp;

	/* Print metrics headers only */
	evlist__for_each_entry(evlist, counter) {
		os.evsel = counter;

		perf_stat__print_shadow_stats(config, counter, 0,
					      0,
					      &out,
					      &config->metric_events);
	}

	if (!config->json_output)
		fputc('\n', config->output);
}

static void prepare_interval(struct perf_stat_config *config,
			     char *prefix, size_t len, struct timespec *ts)
{
	if (config->iostat_run)
		return;

	if (config->json_output)
		scnprintf(prefix, len, "\"interval\" : %lu.%09lu, ",
			  (unsigned long) ts->tv_sec, ts->tv_nsec);
	else if (config->csv_output)
		scnprintf(prefix, len, "%lu.%09lu%s",
			  (unsigned long) ts->tv_sec, ts->tv_nsec, config->csv_sep);
	else
		scnprintf(prefix, len, "%6lu.%09lu ",
			  (unsigned long) ts->tv_sec, ts->tv_nsec);
}

static void print_header_interval_std(struct perf_stat_config *config,
				      struct target *_target __maybe_unused,
				      struct evlist *evlist,
				      int argc __maybe_unused,
				      const char **argv __maybe_unused)
{
	FILE *output = config->output;

	switch (config->aggr_mode) {
	case AGGR_NODE:
	case AGGR_SOCKET:
	case AGGR_DIE:
	case AGGR_CACHE:
	case AGGR_CORE:
		fprintf(output, "#%*s %-*s cpus",
			INTERVAL_LEN - 1, "time",
			aggr_header_lens[config->aggr_mode],
			aggr_header_std[config->aggr_mode]);
		break;
	case AGGR_NONE:
		fprintf(output, "#%*s %-*s",
			INTERVAL_LEN - 1, "time",
			aggr_header_lens[config->aggr_mode],
			aggr_header_std[config->aggr_mode]);
		break;
	case AGGR_THREAD:
		fprintf(output, "#%*s %*s-%-*s",
			INTERVAL_LEN - 1, "time",
			COMM_LEN, "comm", PID_LEN, "pid");
		break;
	case AGGR_GLOBAL:
	default:
		if (!config->iostat_run)
			fprintf(output, "#%*s",
				INTERVAL_LEN - 1, "time");
	case AGGR_UNSET:
	case AGGR_MAX:
		break;
	}

	if (config->metric_only)
		print_metric_headers(config, evlist, true);
	else
		fprintf(output, " %*s %*s events\n",
			COUNTS_LEN, "counts", config->unit_width, "unit");
}

static void print_header_std(struct perf_stat_config *config,
			     struct target *_target, struct evlist *evlist,
			     int argc, const char **argv)
{
	FILE *output = config->output;
	int i;

	fprintf(output, "\n");
	fprintf(output, " Performance counter stats for ");
	if (_target->bpf_str)
		fprintf(output, "\'BPF program(s) %s", _target->bpf_str);
	else if (_target->system_wide)
		fprintf(output, "\'system wide");
	else if (_target->cpu_list)
		fprintf(output, "\'CPU(s) %s", _target->cpu_list);
	else if (!target__has_task(_target)) {
		fprintf(output, "\'%s", argv ? argv[0] : "pipe");
		for (i = 1; argv && (i < argc); i++)
			fprintf(output, " %s", argv[i]);
	} else if (_target->pid)
		fprintf(output, "process id \'%s", _target->pid);
	else
		fprintf(output, "thread id \'%s", _target->tid);

	fprintf(output, "\'");
	if (config->run_count > 1)
		fprintf(output, " (%d runs)", config->run_count);
	fprintf(output, ":\n\n");

	if (config->metric_only)
		print_metric_headers(config, evlist, false);
}

static void print_header_csv(struct perf_stat_config *config,
			     struct target *_target __maybe_unused,
			     struct evlist *evlist,
			     int argc __maybe_unused,
			     const char **argv __maybe_unused)
{
	if (config->metric_only)
		print_metric_headers(config, evlist, true);
}
static void print_header_json(struct perf_stat_config *config,
			      struct target *_target __maybe_unused,
			      struct evlist *evlist,
			      int argc __maybe_unused,
			      const char **argv __maybe_unused)
{
	if (config->metric_only)
		print_metric_headers(config, evlist, true);
}

static void print_header(struct perf_stat_config *config,
			 struct target *_target,
			 struct evlist *evlist,
			 int argc, const char **argv)
{
	static int num_print_iv;

	fflush(stdout);

	if (config->interval_clear)
		puts(CONSOLE_CLEAR);

	if (num_print_iv == 0 || config->interval_clear) {
		if (config->json_output)
			print_header_json(config, _target, evlist, argc, argv);
		else if (config->csv_output)
			print_header_csv(config, _target, evlist, argc, argv);
		else if (config->interval)
			print_header_interval_std(config, _target, evlist, argc, argv);
		else
			print_header_std(config, _target, evlist, argc, argv);
	}

	if (num_print_iv++ == 25)
		num_print_iv = 0;
}

static int get_precision(double num)
{
	if (num > 1)
		return 0;

	return lround(ceil(-log10(num)));
}

static void print_table(struct perf_stat_config *config,
			FILE *output, int precision, double avg)
{
	char tmp[64];
	int idx, indent = 0;

	scnprintf(tmp, 64, " %17.*f", precision, avg);
	while (tmp[indent] == ' ')
		indent++;

	fprintf(output, "%*s# Table of individual measurements:\n", indent, "");

	for (idx = 0; idx < config->run_count; idx++) {
		double run = (double) config->walltime_run[idx] / NSEC_PER_SEC;
		int h, n = 1 + abs((int) (100.0 * (run - avg)/run) / 5);

		fprintf(output, " %17.*f (%+.*f) ",
			precision, run, precision, run - avg);

		for (h = 0; h < n; h++)
			fprintf(output, "#");

		fprintf(output, "\n");
	}

	fprintf(output, "\n%*s# Final result:\n", indent, "");
}

static double timeval2double(struct timeval *t)
{
	return t->tv_sec + (double) t->tv_usec/USEC_PER_SEC;
}

static void print_footer(struct perf_stat_config *config)
{
	double avg = avg_stats(config->walltime_nsecs_stats) / NSEC_PER_SEC;
	FILE *output = config->output;

	if (config->interval || config->csv_output || config->json_output)
		return;

	if (!config->null_run)
		fprintf(output, "\n");

	if (config->run_count == 1) {
		fprintf(output, " %17.9f seconds time elapsed", avg);

		if (config->ru_display) {
			double ru_utime = timeval2double(&config->ru_data.ru_utime);
			double ru_stime = timeval2double(&config->ru_data.ru_stime);

			fprintf(output, "\n\n");
			fprintf(output, " %17.9f seconds user\n", ru_utime);
			fprintf(output, " %17.9f seconds sys\n", ru_stime);
		}
	} else {
		double sd = stddev_stats(config->walltime_nsecs_stats) / NSEC_PER_SEC;
		/*
		 * Display at most 2 more significant
		 * digits than the stddev inaccuracy.
		 */
		int precision = get_precision(sd) + 2;

		if (config->walltime_run_table)
			print_table(config, output, precision, avg);

		fprintf(output, " %17.*f +- %.*f seconds time elapsed",
			precision, avg, precision, sd);

		print_noise_pct(config, sd, avg, /*before_metric=*/false);
	}
	fprintf(output, "\n\n");

	if (config->print_free_counters_hint && sysctl__nmi_watchdog_enabled())
		fprintf(output,
"Some events weren't counted. Try disabling the NMI watchdog:\n"
"	echo 0 > /proc/sys/kernel/nmi_watchdog\n"
"	perf stat ...\n"
"	echo 1 > /proc/sys/kernel/nmi_watchdog\n");

	if (config->print_mixed_hw_group_error)
		fprintf(output,
			"The events in group usually have to be from "
			"the same PMU. Try reorganizing the group.\n");
}

static void print_percore(struct perf_stat_config *config,
			  struct evsel *counter, struct outstate *os)
{
	bool metric_only = config->metric_only;
	FILE *output = config->output;
	struct cpu_aggr_map *core_map;
	int aggr_idx, core_map_len = 0;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	if (config->percore_show_thread)
		return print_counter(config, counter, os);

	/*
	 * core_map will hold the aggr_cpu_id for the cores that have been
	 * printed so that each core is printed just once.
	 */
	core_map = cpu_aggr_map__empty_new(config->aggr_map->nr);
	if (core_map == NULL) {
		fprintf(output, "Cannot allocate per-core aggr map for display\n");
		return;
	}

	cpu_aggr_map__for_each_idx(aggr_idx, config->aggr_map) {
		struct perf_cpu curr_cpu = config->aggr_map->map[aggr_idx].cpu;
		struct aggr_cpu_id core_id = aggr_cpu_id__core(curr_cpu, NULL);
		bool found = false;

		for (int i = 0; i < core_map_len; i++) {
			if (aggr_cpu_id__equal(&core_map->map[i], &core_id)) {
				found = true;
				break;
			}
		}
		if (found)
			continue;

		print_counter_aggrdata(config, counter, aggr_idx, os);

		core_map->map[core_map_len++] = core_id;
	}
	free(core_map);

	if (metric_only)
		fputc('\n', output);
}

static void print_cgroup_counter(struct perf_stat_config *config, struct evlist *evlist,
				 struct outstate *os)
{
	struct evsel *counter;

	evlist__for_each_entry(evlist, counter) {
		if (os->cgrp != counter->cgrp) {
			if (os->cgrp != NULL)
				print_metric_end(config, os);

			os->cgrp = counter->cgrp;
			print_metric_begin(config, evlist, os, /*aggr_idx=*/0);
		}

		print_counter(config, counter, os);
	}
	if (os->cgrp)
		print_metric_end(config, os);
}

void evlist__print_counters(struct evlist *evlist, struct perf_stat_config *config,
			    struct target *_target, struct timespec *ts,
			    int argc, const char **argv)
{
	bool metric_only = config->metric_only;
	int interval = config->interval;
	struct evsel *counter;
	char buf[64];
	struct outstate os = {
		.fh = config->output,
		.first = true,
	};

	if (config->iostat_run)
		evlist->selected = evlist__first(evlist);

	if (interval) {
		os.prefix = buf;
		prepare_interval(config, buf, sizeof(buf), ts);
	}

	print_header(config, _target, evlist, argc, argv);

	switch (config->aggr_mode) {
	case AGGR_CORE:
	case AGGR_CACHE:
	case AGGR_DIE:
	case AGGR_SOCKET:
	case AGGR_NODE:
		if (config->cgroup_list)
			print_aggr_cgroup(config, evlist, &os);
		else
			print_aggr(config, evlist, &os);
		break;
	case AGGR_THREAD:
	case AGGR_GLOBAL:
		if (config->iostat_run) {
			iostat_print_counters(evlist, config, ts, buf,
					      (iostat_print_counter_t)print_counter, &os);
		} else if (config->cgroup_list) {
			print_cgroup_counter(config, evlist, &os);
		} else {
			print_metric_begin(config, evlist, &os, /*aggr_idx=*/0);
			evlist__for_each_entry(evlist, counter) {
				print_counter(config, counter, &os);
			}
			print_metric_end(config, &os);
		}
		break;
	case AGGR_NONE:
		if (metric_only)
			print_no_aggr_metric(config, evlist, &os);
		else {
			evlist__for_each_entry(evlist, counter) {
				if (counter->percore)
					print_percore(config, counter, &os);
				else
					print_counter(config, counter, &os);
			}
		}
		break;
	case AGGR_MAX:
	case AGGR_UNSET:
	default:
		break;
	}

	print_footer(config);

	fflush(config->output);
}
