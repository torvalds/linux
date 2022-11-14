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
#include "pmu-hybrid.h"
#include "evlist-hybrid.h"

#define CNTR_NOT_SUPPORTED	"<not supported>"
#define CNTR_NOT_COUNTED	"<not counted>"

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
			  u64 run, u64 ena)
{
	if (config->json_output)
		print_running_json(config, run, ena);
	else if (config->csv_output)
		print_running_csv(config, run, ena);
	else
		print_running_std(config, run, ena);
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
			    double total, double avg)
{
	double pct = rel_stddev_stats(total, avg);

	if (config->json_output)
		print_noise_pct_json(config, pct);
	else if (config->csv_output)
		print_noise_pct_csv(config, pct);
	else
		print_noise_pct_std(config, pct);
}

static void print_noise(struct perf_stat_config *config,
			struct evsel *evsel, double avg)
{
	struct perf_stat_evsel *ps;

	if (config->run_count == 1)
		return;

	ps = evsel->stats;
	print_noise_pct(config, stddev_stats(&ps->res_stats), avg);
}

static void print_cgroup_std(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, " %s", cgrp_name);
}

static void print_cgroup_csv(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, "%s%s", config->csv_sep, cgrp_name);
}

static void print_cgroup_json(struct perf_stat_config *config, const char *cgrp_name)
{
	fprintf(config->output, "\"cgroup\" : \"%s\", ", cgrp_name);
}

static void print_cgroup(struct perf_stat_config *config, struct evsel *evsel)
{
	if (nr_cgroups) {
		const char *cgrp_name = evsel->cgrp ? evsel->cgrp->name  : "";

		if (config->json_output)
			print_cgroup_json(config, cgrp_name);
		if (config->csv_output)
			print_cgroup_csv(config, cgrp_name);
		else
			print_cgroup_std(config, cgrp_name);
	}
}

static void print_aggr_id_std(struct perf_stat_config *config,
			      struct evsel *evsel, struct aggr_cpu_id id, int nr)
{
	FILE *output = config->output;

	switch (config->aggr_mode) {
	case AGGR_CORE:
		fprintf(output, "S%d-D%d-C%*d %*d ",
			id.socket, id.die, -8, id.core, 4, nr);
		break;
	case AGGR_DIE:
		fprintf(output, "S%d-D%*d %*d ",
			id.socket, -8, id.die, 4, nr);
		break;
	case AGGR_SOCKET:
		fprintf(output, "S%*d %*d ",
			-5, id.socket, 4, nr);
		break;
	case AGGR_NODE:
		fprintf(output, "N%*d %*d ",
			-5, id.node, 4, nr);
		break;
	case AGGR_NONE:
		if (evsel->percore && !config->percore_show_thread) {
			fprintf(output, "S%d-D%d-C%*d ",
				id.socket, id.die, -3, id.core);
		} else if (id.cpu.cpu > -1) {
			fprintf(output, "CPU%*d ",
				-7, id.cpu.cpu);
		}
		break;
	case AGGR_THREAD:
		fprintf(output, "%*s-%*d ",
			16, perf_thread_map__comm(evsel->core.threads, id.thread_idx),
			-8, perf_thread_map__pid(evsel->core.threads, id.thread_idx));
		break;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		break;
	}
}

static void print_aggr_id_csv(struct perf_stat_config *config,
			      struct evsel *evsel, struct aggr_cpu_id id, int nr)
{
	FILE *output = config->output;
	const char *sep = config->csv_sep;

	switch (config->aggr_mode) {
	case AGGR_CORE:
		fprintf(output, "S%d-D%d-C%d%s%d%s",
			id.socket, id.die, id.core, sep, nr, sep);
		break;
	case AGGR_DIE:
		fprintf(output, "S%d-D%d%s%d%s",
			id.socket, id.die, sep, nr, sep);
		break;
	case AGGR_SOCKET:
		fprintf(output, "S%d%s%d%s",
			id.socket, sep, nr, sep);
		break;
	case AGGR_NODE:
		fprintf(output, "N%d%s%d%s",
			id.node, sep, nr, sep);
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
			       struct evsel *evsel, struct aggr_cpu_id id, int nr)
{
	FILE *output = config->output;

	if (!config->interval)
		fputc('{', output);

	switch (config->aggr_mode) {
	case AGGR_CORE:
		fprintf(output, "\"core\" : \"S%d-D%d-C%d\", \"aggregate-number\" : %d, ",
			id.socket, id.die, id.core, nr);
		break;
	case AGGR_DIE:
		fprintf(output, "\"die\" : \"S%d-D%d\", \"aggregate-number\" : %d, ",
			id.socket, id.die, nr);
		break;
	case AGGR_SOCKET:
		fprintf(output, "\"socket\" : \"S%d\", \"aggregate-number\" : %d, ",
			id.socket, nr);
		break;
	case AGGR_NODE:
		fprintf(output, "\"node\" : \"N%d\", \"aggregate-number\" : %d, ",
			id.node, nr);
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
			  struct evsel *evsel, struct aggr_cpu_id id, int nr)
{
	if (config->json_output)
		print_aggr_id_json(config, evsel, id, nr);
	else if (config->csv_output)
		print_aggr_id_csv(config, evsel, id, nr);
	else
		print_aggr_id_std(config, evsel, id, nr);
}

struct outstate {
	FILE *fh;
	bool newline;
	const char *prefix;
	int  nfields;
	int  nr;
	struct aggr_cpu_id id;
	struct evsel *evsel;
};

#define METRIC_LEN  38

static void new_line_std(struct perf_stat_config *config __maybe_unused,
			 void *ctx)
{
	struct outstate *os = ctx;

	os->newline = true;
}

static void do_new_line_std(struct perf_stat_config *config,
			    struct outstate *os)
{
	fputc('\n', os->fh);
	fputs(os->prefix, os->fh);
	aggr_printout(config, os->evsel, os->id, os->nr);
	if (config->aggr_mode == AGGR_NONE)
		fprintf(os->fh, "        ");
	fprintf(os->fh, "                                                 ");
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

	fputc('\n', os->fh);
	if (os->prefix)
		fprintf(os->fh, "%s", os->prefix);
	aggr_printout(config, os->evsel, os->id, os->nr);
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

	fprintf(out, "\"metric-value\" : %f, ", val);
	fprintf(out, "\"metric-unit\" : \"%s\"", unit);
	if (!config->metric_only)
		fprintf(out, "}");
}

static void new_line_json(struct perf_stat_config *config, void *ctx)
{
	struct outstate *os = ctx;

	fputc('\n', os->fh);
	if (os->prefix)
		fprintf(os->fh, "%s", os->prefix);
	aggr_printout(config, os->evsel, os->id, os->nr);
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
	fprintf(out, "{\"metric-value\" : \"%s\"}", vals);
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

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);

	if (config->json_output)
		fprintf(os->fh, "{\"unit\" : \"%s\"}", unit);
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
		fmt = floor(sc) != sc ? "%'18.2f " : "%'18.0f ";
	else
		fmt = floor(sc) != sc ? "%18.2f " : "%18.0f ";

	if (ok)
		fprintf(output, fmt, avg);
	else
		fprintf(output, "%18s ", bad_count);

	if (evsel->unit)
		fprintf(output, "%-*s ", config->unit_width, evsel->unit);

	fprintf(output, "%-*s", 32, evsel__name(evsel));
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
			 struct aggr_cpu_id id, int nr,
			 struct evsel *evsel, double avg, bool ok)
{
	aggr_printout(config, evsel, id, nr);
	print_counter_value(config, evsel, avg, ok);
	print_cgroup(config, evsel);
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

static void printout(struct perf_stat_config *config, struct aggr_cpu_id id, int nr,
		     struct evsel *counter, double uval,
		     char *prefix, u64 run, u64 ena, double noise,
		     struct runtime_stat *st, int map_idx)
{
	struct perf_stat_output_ctx out;
	struct outstate os = {
		.fh = config->output,
		.prefix = prefix ? prefix : "",
		.id = id,
		.nr = nr,
		.evsel = counter,
	};
	print_metric_t pm;
	new_line_t nl;

	if (config->csv_output) {
		static const int aggr_fields[AGGR_MAX] = {
			[AGGR_NONE] = 1,
			[AGGR_GLOBAL] = 0,
			[AGGR_SOCKET] = 2,
			[AGGR_DIE] = 2,
			[AGGR_CORE] = 2,
			[AGGR_THREAD] = 1,
			[AGGR_UNSET] = 0,
			[AGGR_NODE] = 1,
		};

		pm = config->metric_only ? print_metric_only_csv : print_metric_csv;
		nl = config->metric_only ? new_line_metric : new_line_csv;
		os.nfields = 3 + aggr_fields[config->aggr_mode] + (counter->cgrp ? 1 : 0);
	} else if (config->json_output) {
		pm = config->metric_only ? print_metric_only_json : print_metric_json;
		nl = config->metric_only ? new_line_metric : new_line_json;
	} else {
		pm = config->metric_only ? print_metric_only : print_metric_std;
		nl = config->metric_only ? new_line_metric : new_line_std;
	}

	if (!config->no_csv_summary && config->csv_output &&
	    config->summary && !config->interval && !config->metric_only) {
		fprintf(config->output, "%16s%s", "summary", config->csv_sep);
	}

	if (run == 0 || ena == 0 || counter->counts->scaled == -1) {
		if (config->metric_only) {
			pm(config, &os, NULL, "", "", 0);
			return;
		}

		abs_printout(config, id, nr, counter, uval, /*ok=*/false);

		if (counter->supported) {
			if (!evlist__has_hybrid(counter->evlist)) {
				config->print_free_counters_hint = 1;
				if (is_mixed_hw_group(counter))
					config->print_mixed_hw_group_error = 1;
			}
		}

		if (!config->csv_output && !config->json_output)
			pm(config, &os, NULL, NULL, "", 0);
		print_noise(config, counter, noise);
		print_running(config, run, ena);
		if (config->csv_output || config->json_output)
			pm(config, &os, NULL, NULL, "", 0);
		return;
	}

	if (!config->metric_only)
		abs_printout(config, id, nr, counter, uval, /*ok=*/true);

	out.print_metric = pm;
	out.new_line = nl;
	out.ctx = &os;
	out.force_header = false;

	if (config->csv_output && !config->metric_only) {
		print_noise(config, counter, noise);
		print_running(config, run, ena);
	} else if (config->json_output && !config->metric_only) {
		print_noise(config, counter, noise);
		print_running(config, run, ena);
	}

	perf_stat__print_shadow_stats(config, counter, uval, map_idx,
				&out, &config->metric_events, st);
	if (!config->csv_output && !config->metric_only && !config->json_output) {
		print_noise(config, counter, noise);
		print_running(config, run, ena);
	}
}

static void uniquify_event_name(struct evsel *counter)
{
	char *new_name;
	char *config;
	int ret = 0;

	if (counter->uniquified_name || counter->use_config_name ||
	    !counter->pmu_name || !strncmp(counter->name, counter->pmu_name,
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

static void print_counter_aggrdata(struct perf_stat_config *config,
				   struct evsel *counter, int s,
				   char *prefix, bool metric_only,
				   bool *first)
{
	FILE *output = config->output;
	u64 ena, run, val;
	double uval;
	struct perf_stat_evsel *ps = counter->stats;
	struct perf_stat_aggr *aggr = &ps->aggr[s];
	struct aggr_cpu_id id = config->aggr_map->map[s];
	double avg = aggr->counts.val;

	if (counter->supported && aggr->nr == 0)
		return;

	uniquify_counter(config, counter);

	val = aggr->counts.val;
	ena = aggr->counts.ena;
	run = aggr->counts.run;

	if (*first && metric_only) {
		*first = false;
		aggr_printout(config, counter, id, aggr->nr);
	}
	if (prefix && !metric_only)
		fprintf(output, "%s", prefix);

	uval = val * counter->scale;

	printout(config, id, aggr->nr, counter, uval,
		 prefix, run, ena, avg, &rt_stat, s);

	if (!metric_only)
		fputc('\n', output);
}

static void print_aggr(struct perf_stat_config *config,
		       struct evlist *evlist,
		       char *prefix)
{
	bool metric_only = config->metric_only;
	FILE *output = config->output;
	struct evsel *counter;
	int s;
	bool first;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	/*
	 * With metric_only everything is on a single line.
	 * Without each counter has its own line.
	 */
	for (s = 0; s < config->aggr_map->nr; s++) {
		if (metric_only) {
			if (prefix)
				fprintf(output, "%s", prefix);
			else if (config->summary && !config->no_csv_summary &&
				 config->csv_output && !config->interval)
				fprintf(output, "%16s%s", "summary", config->csv_sep);
		}

		first = true;
		evlist__for_each_entry(evlist, counter) {
			if (counter->merged_stat)
				continue;

			print_counter_aggrdata(config, counter, s,
					       prefix, metric_only,
					       &first);
		}
		if (metric_only)
			fputc('\n', output);
	}
}

static void print_counter(struct perf_stat_config *config,
			  struct evsel *counter, char *prefix)
{
	bool metric_only = config->metric_only;
	bool first = false;
	int s;

	/* AGGR_THREAD doesn't have config->aggr_get_id */
	if (!config->aggr_map)
		return;

	if (counter->merged_stat)
		return;

	for (s = 0; s < config->aggr_map->nr; s++) {
		print_counter_aggrdata(config, counter, s,
				       prefix, metric_only,
				       &first);
	}
}

static void print_no_aggr_metric(struct perf_stat_config *config,
				 struct evlist *evlist,
				 char *prefix)
{
	int all_idx;
	struct perf_cpu cpu;

	perf_cpu_map__for_each_cpu(cpu, all_idx, evlist->core.user_requested_cpus) {
		struct evsel *counter;
		bool first = true;

		evlist__for_each_entry(evlist, counter) {
			u64 ena, run, val;
			double uval;
			struct aggr_cpu_id id;
			struct perf_stat_evsel *ps = counter->stats;
			int counter_idx = perf_cpu_map__idx(evsel__cpus(counter), cpu);

			if (counter_idx < 0)
				continue;

			id = aggr_cpu_id__cpu(cpu, /*data=*/NULL);
			if (first) {
				if (prefix)
					fputs(prefix, config->output);
				aggr_printout(config, counter, id, 0);
				first = false;
			}
			val = ps->aggr[counter_idx].counts.val;
			ena = ps->aggr[counter_idx].counts.ena;
			run = ps->aggr[counter_idx].counts.run;

			uval = val * counter->scale;
			printout(config, id, 0, counter, uval, prefix,
				 run, ena, 1.0, &rt_stat, counter_idx);
		}
		if (!first)
			fputc('\n', config->output);
	}
}

static int aggr_header_lens[] = {
	[AGGR_CORE] = 24,
	[AGGR_DIE] = 18,
	[AGGR_SOCKET] = 12,
	[AGGR_NONE] = 6,
	[AGGR_THREAD] = 24,
	[AGGR_NODE] = 6,
	[AGGR_GLOBAL] = 0,
};

static const char *aggr_header_csv[] = {
	[AGGR_CORE] 	= 	"core,cpus,",
	[AGGR_DIE] 	= 	"die,cpus,",
	[AGGR_SOCKET] 	= 	"socket,cpus,",
	[AGGR_NONE] 	= 	"cpu,",
	[AGGR_THREAD] 	= 	"comm-pid,",
	[AGGR_NODE] 	= 	"node,",
	[AGGR_GLOBAL] 	=	""
};

static void print_metric_headers(struct perf_stat_config *config,
				 struct evlist *evlist,
				 const char *prefix, bool no_indent)
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

	if (prefix && !config->json_output)
		fprintf(config->output, "%s", prefix);

	if (!config->csv_output && !config->json_output && !no_indent)
		fprintf(config->output, "%*s",
			aggr_header_lens[config->aggr_mode], "");
	if (config->csv_output) {
		if (config->interval)
			fputs("time,", config->output);
		if (!config->iostat_run)
			fputs(aggr_header_csv[config->aggr_mode], config->output);
	}
	if (config->json_output) {
		if (config->interval)
			fputs("{\"unit\" : \"sec\"}", config->output);
	}
	if (config->iostat_run)
		iostat_print_header_prefix(config);

	/* Print metrics headers only */
	evlist__for_each_entry(evlist, counter) {
		os.evsel = counter;

		perf_stat__print_shadow_stats(config, counter, 0,
					      0,
					      &out,
					      &config->metric_events,
					      &rt_stat);
	}
	fputc('\n', config->output);
}

static void print_interval(struct perf_stat_config *config,
			   struct evlist *evlist,
			   char *prefix, struct timespec *ts)
{
	bool metric_only = config->metric_only;
	unsigned int unit_width = config->unit_width;
	FILE *output = config->output;
	static int num_print_interval;

	if (config->interval_clear && isatty(fileno(output)))
		puts(CONSOLE_CLEAR);

	if (!config->iostat_run && !config->json_output)
		sprintf(prefix, "%6lu.%09lu%s", (unsigned long) ts->tv_sec,
				 ts->tv_nsec, config->csv_sep);
	if (!config->iostat_run && config->json_output && !config->metric_only)
		sprintf(prefix, "{\"interval\" : %lu.%09lu, ", (unsigned long)
				 ts->tv_sec, ts->tv_nsec);
	if (!config->iostat_run && config->json_output && config->metric_only)
		sprintf(prefix, "{\"interval\" : %lu.%09lu}", (unsigned long)
				 ts->tv_sec, ts->tv_nsec);

	if ((num_print_interval == 0 || config->interval_clear) &&
			!config->csv_output && !config->json_output) {
		switch (config->aggr_mode) {
		case AGGR_NODE:
			fprintf(output, "#           time node   cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_SOCKET:
			fprintf(output, "#           time socket cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_DIE:
			fprintf(output, "#           time die          cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_CORE:
			fprintf(output, "#           time core            cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_NONE:
			fprintf(output, "#           time CPU    ");
			if (!metric_only)
				fprintf(output, "                counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_THREAD:
			fprintf(output, "#           time             comm-pid");
			if (!metric_only)
				fprintf(output, "                  counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_GLOBAL:
		default:
			if (!config->iostat_run) {
				fprintf(output, "#           time");
				if (!metric_only)
					fprintf(output, "             counts %*s events\n", unit_width, "unit");
			}
		case AGGR_UNSET:
		case AGGR_MAX:
			break;
		}
	}

	if ((num_print_interval == 0 || config->interval_clear) && metric_only)
		print_metric_headers(config, evlist, " ", true);
	if (++num_print_interval == 25)
		num_print_interval = 0;
}

static void print_header(struct perf_stat_config *config,
			 struct target *_target,
			 int argc, const char **argv)
{
	FILE *output = config->output;
	int i;

	fflush(stdout);

	if (!config->csv_output && !config->json_output) {
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
	}
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

		print_noise_pct(config, sd, avg);
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
			  struct evsel *counter, char *prefix)
{
	bool metric_only = config->metric_only;
	FILE *output = config->output;
	struct cpu_aggr_map *core_map;
	int s, c, i;
	bool first = true;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	if (config->percore_show_thread)
		return print_counter(config, counter, prefix);

	core_map = cpu_aggr_map__empty_new(config->aggr_map->nr);
	if (core_map == NULL) {
		fprintf(output, "Cannot allocate per-core aggr map for display\n");
		return;
	}

	for (s = 0, c = 0; s < config->aggr_map->nr; s++) {
		struct perf_cpu curr_cpu = config->aggr_map->map[s].cpu;
		struct aggr_cpu_id core_id = aggr_cpu_id__core(curr_cpu, NULL);
		bool found = false;

		for (i = 0; i < c; i++) {
			if (aggr_cpu_id__equal(&core_map->map[i], &core_id)) {
				found = true;
				break;
			}
		}
		if (found)
			continue;

		if (prefix && metric_only)
			fprintf(output, "%s", prefix);

		print_counter_aggrdata(config, counter, s,
				       prefix, metric_only, &first);

		core_map->map[c++] = core_id;
	}
	free(core_map);

	if (metric_only)
		fputc('\n', output);
}

void evlist__print_counters(struct evlist *evlist, struct perf_stat_config *config,
			    struct target *_target, struct timespec *ts, int argc, const char **argv)
{
	bool metric_only = config->metric_only;
	int interval = config->interval;
	struct evsel *counter;
	char buf[64], *prefix = NULL;

	if (config->iostat_run)
		evlist->selected = evlist__first(evlist);

	if (interval)
		print_interval(config, evlist, prefix = buf, ts);
	else
		print_header(config, _target, argc, argv);

	if (metric_only) {
		static int num_print_iv;

		if (num_print_iv == 0 && !interval)
			print_metric_headers(config, evlist, prefix, false);
		if (num_print_iv++ == 25)
			num_print_iv = 0;
		if (config->aggr_mode == AGGR_GLOBAL && prefix && !config->iostat_run)
			fprintf(config->output, "%s", prefix);

		if (config->json_output && !config->metric_only)
			fprintf(config->output, "}");
	}

	switch (config->aggr_mode) {
	case AGGR_CORE:
	case AGGR_DIE:
	case AGGR_SOCKET:
	case AGGR_NODE:
		print_aggr(config, evlist, prefix);
		break;
	case AGGR_THREAD:
	case AGGR_GLOBAL:
		if (config->iostat_run)
			iostat_print_counters(evlist, config, ts, prefix = buf,
					      print_counter);
		else {
			evlist__for_each_entry(evlist, counter) {
				print_counter(config, counter, prefix);
			}
			if (metric_only)
				fputc('\n', config->output);
		}
		break;
	case AGGR_NONE:
		if (metric_only)
			print_no_aggr_metric(config, evlist, prefix);
		else {
			evlist__for_each_entry(evlist, counter) {
				if (counter->percore)
					print_percore(config, counter, prefix);
				else
					print_counter(config, counter, prefix);
			}
		}
		break;
	case AGGR_MAX:
	case AGGR_UNSET:
	default:
		break;
	}

	if (!interval && !config->csv_output && !config->json_output)
		print_footer(config);

	fflush(config->output);
}
