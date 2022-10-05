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

static void print_running(struct perf_stat_config *config,
			  u64 run, u64 ena)
{

	double enabled_percent = 100;

	if (run != ena)
		enabled_percent = 100 * run / ena;
	if (config->json_output)
		fprintf(config->output,
			"\"event-runtime\" : %" PRIu64 ", \"pcnt-running\" : %.2f, ",
			run, enabled_percent);
	else if (config->csv_output)
		fprintf(config->output,
			"%s%" PRIu64 "%s%.2f", config->csv_sep,
			run, config->csv_sep, enabled_percent);
	else if (run != ena)
		fprintf(config->output, "  (%.2f%%)", 100.0 * run / ena);
}

static void print_noise_pct(struct perf_stat_config *config,
			    double total, double avg)
{
	double pct = rel_stddev_stats(total, avg);

	if (config->json_output)
		fprintf(config->output, "\"variance\" : %.2f, ", pct);
	else if (config->csv_output)
		fprintf(config->output, "%s%.2f%%", config->csv_sep, pct);
	else if (pct)
		fprintf(config->output, "  ( +-%6.2f%% )", pct);
}

static void print_noise(struct perf_stat_config *config,
			struct evsel *evsel, double avg)
{
	struct perf_stat_evsel *ps;

	if (config->run_count == 1)
		return;

	ps = evsel->stats;
	print_noise_pct(config, stddev_stats(&ps->res_stats[0]), avg);
}

static void print_cgroup(struct perf_stat_config *config, struct evsel *evsel)
{
	if (nr_cgroups) {
		const char *cgrp_name = evsel->cgrp ? evsel->cgrp->name  : "";

		if (config->json_output)
			fprintf(config->output, "\"cgroup\" : \"%s\", ", cgrp_name);
		else
			fprintf(config->output, "%s%s", config->csv_sep, cgrp_name);
	}
}


static void aggr_printout(struct perf_stat_config *config,
			  struct evsel *evsel, struct aggr_cpu_id id, int nr)
{


	if (config->json_output && !config->interval)
		fprintf(config->output, "{");

	switch (config->aggr_mode) {
	case AGGR_CORE:
		if (config->json_output) {
			fprintf(config->output,
				"\"core\" : \"S%d-D%d-C%d\", \"aggregate-number\" : %d, ",
				id.socket,
				id.die,
				id.core,
				nr);
		} else {
			fprintf(config->output, "S%d-D%d-C%*d%s%*d%s",
				id.socket,
				id.die,
				config->csv_output ? 0 : -8,
				id.core,
				config->csv_sep,
				config->csv_output ? 0 : 4,
				nr,
				config->csv_sep);
		}
		break;
	case AGGR_DIE:
		if (config->json_output) {
			fprintf(config->output,
				"\"die\" : \"S%d-D%d\", \"aggregate-number\" : %d, ",
				id.socket,
				id.die,
				nr);
		} else {
			fprintf(config->output, "S%d-D%*d%s%*d%s",
				id.socket,
				config->csv_output ? 0 : -8,
				id.die,
				config->csv_sep,
				config->csv_output ? 0 : 4,
				nr,
				config->csv_sep);
		}
		break;
	case AGGR_SOCKET:
		if (config->json_output) {
			fprintf(config->output,
				"\"socket\" : \"S%d\", \"aggregate-number\" : %d, ",
				id.socket,
				nr);
		} else {
			fprintf(config->output, "S%*d%s%*d%s",
				config->csv_output ? 0 : -5,
				id.socket,
				config->csv_sep,
				config->csv_output ? 0 : 4,
				nr,
				config->csv_sep);
		}
		break;
	case AGGR_NODE:
		if (config->json_output) {
			fprintf(config->output, "\"node\" : \"N%d\", \"aggregate-number\" : %d, ",
				id.node,
				nr);
		} else {
			fprintf(config->output, "N%*d%s%*d%s",
				config->csv_output ? 0 : -5,
				id.node,
				config->csv_sep,
				config->csv_output ? 0 : 4,
				nr,
				config->csv_sep);
		}
		break;
	case AGGR_NONE:
		if (config->json_output) {
			if (evsel->percore && !config->percore_show_thread) {
				fprintf(config->output, "\"core\" : \"S%d-D%d-C%d\"",
					id.socket,
					id.die,
					id.core);
			} else if (id.core > -1) {
				fprintf(config->output, "\"cpu\" : \"%d\", ",
					id.cpu.cpu);
			}
		} else {
			if (evsel->percore && !config->percore_show_thread) {
				fprintf(config->output, "S%d-D%d-C%*d%s",
					id.socket,
					id.die,
					config->csv_output ? 0 : -3,
					id.core, config->csv_sep);
			} else if (id.core > -1) {
				fprintf(config->output, "CPU%*d%s",
					config->csv_output ? 0 : -7,
					id.cpu.cpu, config->csv_sep);
			}
		}
		break;
	case AGGR_THREAD:
		if (config->json_output) {
			fprintf(config->output, "\"thread\" : \"%s-%d\", ",
				perf_thread_map__comm(evsel->core.threads, id.thread),
				perf_thread_map__pid(evsel->core.threads, id.thread));
		} else {
			fprintf(config->output, "%*s-%*d%s",
				config->csv_output ? 0 : 16,
				perf_thread_map__comm(evsel->core.threads, id.thread),
				config->csv_output ? 0 : -8,
				perf_thread_map__pid(evsel->core.threads, id.thread),
				config->csv_sep);
		}
		break;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		break;
	}
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

#define METRIC_LEN  35

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
		fprintf(os->fh, "%s%s", os->prefix, config->csv_sep);
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

	if (!valid_only_metric(unit) && !config->json_output)
		return;
	unit = fixunit(tbuf, os->evsel, unit);

	if (config->json_output)
		fprintf(os->fh, "\"unit\" : \"%s\"", unit);
	else if (config->csv_output)
		fprintf(os->fh, "%s%s", unit, config->csv_sep);
	else
		fprintf(os->fh, "%*s ", config->metric_only_len, unit);
}

static int first_shadow_cpu_map_idx(struct perf_stat_config *config,
				struct evsel *evsel, const struct aggr_cpu_id *id)
{
	struct perf_cpu_map *cpus = evsel__cpus(evsel);
	struct perf_cpu cpu;
	int idx;

	if (config->aggr_mode == AGGR_NONE)
		return perf_cpu_map__idx(cpus, id->cpu);

	if (!config->aggr_get_id)
		return 0;

	perf_cpu_map__for_each_cpu(cpu, idx, cpus) {
		struct aggr_cpu_id cpu_id = config->aggr_get_id(config, cpu);

		if (aggr_cpu_id__equal(&cpu_id, id))
			return idx;
	}
	return 0;
}

static void abs_printout(struct perf_stat_config *config,
			 struct aggr_cpu_id id, int nr, struct evsel *evsel, double avg)
{
	FILE *output = config->output;
	double sc =  evsel->scale;
	const char *fmt;

	if (config->csv_output) {
		fmt = floor(sc) != sc ?  "%.2f%s" : "%.0f%s";
	} else {
		if (config->big_num)
			fmt = floor(sc) != sc ? "%'18.2f%s" : "%'18.0f%s";
		else
			fmt = floor(sc) != sc ? "%18.2f%s" : "%18.0f%s";
	}

	aggr_printout(config, evsel, id, nr);

	if (config->json_output)
		fprintf(output, "\"counter-value\" : \"%f\", ", avg);
	else
		fprintf(output, fmt, avg, config->csv_sep);

	if (config->json_output) {
		if (evsel->unit) {
			fprintf(output, "\"unit\" : \"%s\", ",
				evsel->unit);
		}
	} else {
		if (evsel->unit)
			fprintf(output, "%-*s%s",
				config->csv_output ? 0 : config->unit_width,
				evsel->unit, config->csv_sep);
	}

	if (config->json_output)
		fprintf(output, "\"event\" : \"%s\", ", evsel__name(evsel));
	else
		fprintf(output, "%-*s", config->csv_output ? 0 : 32, evsel__name(evsel));

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
		     struct runtime_stat *st)
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
			[AGGR_NODE] = 0,
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
	    config->summary && !config->interval) {
		fprintf(config->output, "%16s%s", "summary", config->csv_sep);
	}

	if (run == 0 || ena == 0 || counter->counts->scaled == -1) {
		if (config->metric_only) {
			pm(config, &os, NULL, "", "", 0);
			return;
		}
		aggr_printout(config, counter, id, nr);

		if (config->json_output) {
			fprintf(config->output, "\"counter-value\" : \"%s\", ",
					counter->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED);
		} else {
			fprintf(config->output, "%*s%s",
				config->csv_output ? 0 : 18,
				counter->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED,
				config->csv_sep);
		}

		if (counter->supported) {
			if (!evlist__has_hybrid(counter->evlist)) {
				config->print_free_counters_hint = 1;
				if (is_mixed_hw_group(counter))
					config->print_mixed_hw_group_error = 1;
			}
		}

		if (config->json_output) {
			fprintf(config->output, "\"unit\" : \"%s\", ", counter->unit);
		} else {
			fprintf(config->output, "%-*s%s",
				config->csv_output ? 0 : config->unit_width,
				counter->unit, config->csv_sep);
		}

		if (config->json_output) {
			fprintf(config->output, "\"event\" : \"%s\", ",
				evsel__name(counter));
		} else {
			fprintf(config->output, "%*s",
				 config->csv_output ? 0 : -25, evsel__name(counter));
		}

		print_cgroup(config, counter);

		if (!config->csv_output && !config->json_output)
			pm(config, &os, NULL, NULL, "", 0);
		print_noise(config, counter, noise);
		print_running(config, run, ena);
		if (config->csv_output)
			pm(config, &os, NULL, NULL, "", 0);
		else if (config->json_output)
			pm(config, &os, NULL, NULL, "", 0);
		return;
	}

	if (!config->metric_only)
		abs_printout(config, id, nr, counter, uval);

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

	perf_stat__print_shadow_stats(config, counter, uval,
				first_shadow_cpu_map_idx(config, counter, &id),
				&out, &config->metric_events, st);
	if (!config->csv_output && !config->metric_only && !config->json_output) {
		print_noise(config, counter, noise);
		print_running(config, run, ena);
	}
}

static void aggr_update_shadow(struct perf_stat_config *config,
			       struct evlist *evlist)
{
	int idx, s;
	struct perf_cpu cpu;
	struct aggr_cpu_id s2, id;
	u64 val;
	struct evsel *counter;
	struct perf_cpu_map *cpus;

	for (s = 0; s < config->aggr_map->nr; s++) {
		id = config->aggr_map->map[s];
		evlist__for_each_entry(evlist, counter) {
			cpus = evsel__cpus(counter);
			val = 0;
			perf_cpu_map__for_each_cpu(cpu, idx, cpus) {
				s2 = config->aggr_get_id(config, cpu);
				if (!aggr_cpu_id__equal(&s2, &id))
					continue;
				val += perf_counts(counter->counts, idx, 0)->val;
			}
			perf_stat__update_shadow_stats(counter, val,
					first_shadow_cpu_map_idx(config, counter, &id),
					&rt_stat);
		}
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
		if (perf_pmu__has_hybrid()) {
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

static void collect_all_aliases(struct perf_stat_config *config, struct evsel *counter,
			    void (*cb)(struct perf_stat_config *config, struct evsel *counter, void *data,
				       bool first),
			    void *data)
{
	struct evlist *evlist = counter->evlist;
	struct evsel *alias;

	alias = list_prepare_entry(counter, &(evlist->core.entries), core.node);
	list_for_each_entry_continue (alias, &evlist->core.entries, core.node) {
		/* Merge events with the same name, etc. but on different PMUs. */
		if (!strcmp(evsel__name(alias), evsel__name(counter)) &&
			alias->scale == counter->scale &&
			alias->cgrp == counter->cgrp &&
			!strcmp(alias->unit, counter->unit) &&
			evsel__is_clock(alias) == evsel__is_clock(counter) &&
			strcmp(alias->pmu_name, counter->pmu_name)) {
			alias->merged_stat = true;
			cb(config, alias, data, false);
		}
	}
}

static bool is_uncore(struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel__find_pmu(evsel);

	return pmu && pmu->is_uncore;
}

static bool hybrid_uniquify(struct evsel *evsel)
{
	return perf_pmu__has_hybrid() && !is_uncore(evsel);
}

static bool hybrid_merge(struct evsel *counter, struct perf_stat_config *config,
			 bool check)
{
	if (hybrid_uniquify(counter)) {
		if (check)
			return config && config->hybrid_merge;
		else
			return config && !config->hybrid_merge;
	}

	return false;
}

static bool collect_data(struct perf_stat_config *config, struct evsel *counter,
			    void (*cb)(struct perf_stat_config *config, struct evsel *counter, void *data,
				       bool first),
			    void *data)
{
	if (counter->merged_stat)
		return false;
	cb(config, counter, data, true);
	if (config->no_merge || hybrid_merge(counter, config, false))
		uniquify_event_name(counter);
	else if (counter->auto_merge_stats || hybrid_merge(counter, config, true))
		collect_all_aliases(config, counter, cb, data);
	return true;
}

struct aggr_data {
	u64 ena, run, val;
	struct aggr_cpu_id id;
	int nr;
	int cpu_map_idx;
};

static void aggr_cb(struct perf_stat_config *config,
		    struct evsel *counter, void *data, bool first)
{
	struct aggr_data *ad = data;
	int idx;
	struct perf_cpu cpu;
	struct perf_cpu_map *cpus;
	struct aggr_cpu_id s2;

	cpus = evsel__cpus(counter);
	perf_cpu_map__for_each_cpu(cpu, idx, cpus) {
		struct perf_counts_values *counts;

		s2 = config->aggr_get_id(config, cpu);
		if (!aggr_cpu_id__equal(&s2, &ad->id))
			continue;
		if (first)
			ad->nr++;
		counts = perf_counts(counter->counts, idx, 0);
		/*
		 * When any result is bad, make them all to give
		 * consistent output in interval mode.
		 */
		if (counts->ena == 0 || counts->run == 0 ||
		    counter->counts->scaled == -1) {
			ad->ena = 0;
			ad->run = 0;
			break;
		}
		ad->val += counts->val;
		ad->ena += counts->ena;
		ad->run += counts->run;
	}
}

static void print_counter_aggrdata(struct perf_stat_config *config,
				   struct evsel *counter, int s,
				   char *prefix, bool metric_only,
				   bool *first, struct perf_cpu cpu)
{
	struct aggr_data ad;
	FILE *output = config->output;
	u64 ena, run, val;
	int nr;
	struct aggr_cpu_id id;
	double uval;

	ad.id = id = config->aggr_map->map[s];
	ad.val = ad.ena = ad.run = 0;
	ad.nr = 0;
	if (!collect_data(config, counter, aggr_cb, &ad))
		return;

	if (perf_pmu__has_hybrid() && ad.ena == 0)
		return;

	nr = ad.nr;
	ena = ad.ena;
	run = ad.run;
	val = ad.val;
	if (*first && metric_only) {
		*first = false;
		aggr_printout(config, counter, id, nr);
	}
	if (prefix && !metric_only)
		fprintf(output, "%s", prefix);

	uval = val * counter->scale;
	if (cpu.cpu != -1)
		id = aggr_cpu_id__cpu(cpu, /*data=*/NULL);

	printout(config, id, nr, counter, uval,
		 prefix, run, ena, 1.0, &rt_stat);
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

	aggr_update_shadow(config, evlist);

	/*
	 * With metric_only everything is on a single line.
	 * Without each counter has its own line.
	 */
	for (s = 0; s < config->aggr_map->nr; s++) {
		if (prefix && metric_only)
			fprintf(output, "%s", prefix);

		first = true;
		evlist__for_each_entry(evlist, counter) {
			print_counter_aggrdata(config, counter, s,
					prefix, metric_only,
					&first, (struct perf_cpu){ .cpu = -1 });
		}
		if (metric_only)
			fputc('\n', output);
	}
}

static int cmp_val(const void *a, const void *b)
{
	return ((struct perf_aggr_thread_value *)b)->val -
		((struct perf_aggr_thread_value *)a)->val;
}

static struct perf_aggr_thread_value *sort_aggr_thread(
					struct evsel *counter,
					int *ret,
					struct target *_target)
{
	int nthreads = perf_thread_map__nr(counter->core.threads);
	int i = 0;
	double uval;
	struct perf_aggr_thread_value *buf;

	buf = calloc(nthreads, sizeof(struct perf_aggr_thread_value));
	if (!buf)
		return NULL;

	for (int thread = 0; thread < nthreads; thread++) {
		int idx;
		u64 ena = 0, run = 0, val = 0;

		perf_cpu_map__for_each_idx(idx, evsel__cpus(counter)) {
			struct perf_counts_values *counts =
				perf_counts(counter->counts, idx, thread);

			val += counts->val;
			ena += counts->ena;
			run += counts->run;
		}

		uval = val * counter->scale;

		/*
		 * Skip value 0 when enabling --per-thread globally,
		 * otherwise too many 0 output.
		 */
		if (uval == 0.0 && target__has_per_thread(_target))
			continue;

		buf[i].counter = counter;
		buf[i].id = aggr_cpu_id__empty();
		buf[i].id.thread = thread;
		buf[i].uval = uval;
		buf[i].val = val;
		buf[i].run = run;
		buf[i].ena = ena;
		i++;
	}

	qsort(buf, i, sizeof(struct perf_aggr_thread_value), cmp_val);

	if (ret)
		*ret = i;

	return buf;
}

static void print_aggr_thread(struct perf_stat_config *config,
			      struct target *_target,
			      struct evsel *counter, char *prefix)
{
	FILE *output = config->output;
	int thread, sorted_threads;
	struct aggr_cpu_id id;
	struct perf_aggr_thread_value *buf;

	buf = sort_aggr_thread(counter, &sorted_threads, _target);
	if (!buf) {
		perror("cannot sort aggr thread");
		return;
	}

	for (thread = 0; thread < sorted_threads; thread++) {
		if (prefix)
			fprintf(output, "%s", prefix);

		id = buf[thread].id;
		if (config->stats)
			printout(config, id, 0, buf[thread].counter, buf[thread].uval,
				 prefix, buf[thread].run, buf[thread].ena, 1.0,
				 &config->stats[id.thread]);
		else
			printout(config, id, 0, buf[thread].counter, buf[thread].uval,
				 prefix, buf[thread].run, buf[thread].ena, 1.0,
				 &rt_stat);
		fputc('\n', output);
	}

	free(buf);
}

struct caggr_data {
	double avg, avg_enabled, avg_running;
};

static void counter_aggr_cb(struct perf_stat_config *config __maybe_unused,
			    struct evsel *counter, void *data,
			    bool first __maybe_unused)
{
	struct caggr_data *cd = data;
	struct perf_counts_values *aggr = &counter->counts->aggr;

	cd->avg += aggr->val;
	cd->avg_enabled += aggr->ena;
	cd->avg_running += aggr->run;
}

/*
 * Print out the results of a single counter:
 * aggregated counts in system-wide mode
 */
static void print_counter_aggr(struct perf_stat_config *config,
			       struct evsel *counter, char *prefix)
{
	bool metric_only = config->metric_only;
	FILE *output = config->output;
	double uval;
	struct caggr_data cd = { .avg = 0.0 };

	if (!collect_data(config, counter, counter_aggr_cb, &cd))
		return;

	if (prefix && !metric_only)
		fprintf(output, "%s", prefix);

	uval = cd.avg * counter->scale;
	printout(config, aggr_cpu_id__empty(), 0, counter, uval, prefix, cd.avg_running,
		 cd.avg_enabled, cd.avg, &rt_stat);
	if (!metric_only)
		fprintf(output, "\n");
}

static void counter_cb(struct perf_stat_config *config __maybe_unused,
		       struct evsel *counter, void *data,
		       bool first __maybe_unused)
{
	struct aggr_data *ad = data;

	ad->val += perf_counts(counter->counts, ad->cpu_map_idx, 0)->val;
	ad->ena += perf_counts(counter->counts, ad->cpu_map_idx, 0)->ena;
	ad->run += perf_counts(counter->counts, ad->cpu_map_idx, 0)->run;
}

/*
 * Print out the results of a single counter:
 * does not use aggregated count in system-wide
 */
static void print_counter(struct perf_stat_config *config,
			  struct evsel *counter, char *prefix)
{
	FILE *output = config->output;
	u64 ena, run, val;
	double uval;
	int idx;
	struct perf_cpu cpu;
	struct aggr_cpu_id id;

	perf_cpu_map__for_each_cpu(cpu, idx, evsel__cpus(counter)) {
		struct aggr_data ad = { .cpu_map_idx = idx };

		if (!collect_data(config, counter, counter_cb, &ad))
			return;
		val = ad.val;
		ena = ad.ena;
		run = ad.run;

		if (prefix)
			fprintf(output, "%s", prefix);

		uval = val * counter->scale;
		id = aggr_cpu_id__cpu(cpu, /*data=*/NULL);
		printout(config, id, 0, counter, uval, prefix,
			 run, ena, 1.0, &rt_stat);

		fputc('\n', output);
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
			val = perf_counts(counter->counts, counter_idx, 0)->val;
			ena = perf_counts(counter->counts, counter_idx, 0)->ena;
			run = perf_counts(counter->counts, counter_idx, 0)->run;

			uval = val * counter->scale;
			printout(config, id, 0, counter, uval, prefix,
				 run, ena, 1.0, &rt_stat);
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
	[AGGR_GLOBAL] = 0,
};

static const char *aggr_header_csv[] = {
	[AGGR_CORE] 	= 	"core,cpus,",
	[AGGR_DIE] 	= 	"die,cpus",
	[AGGR_SOCKET] 	= 	"socket,cpus",
	[AGGR_NONE] 	= 	"cpu,",
	[AGGR_THREAD] 	= 	"comm-pid,",
	[AGGR_GLOBAL] 	=	""
};

static void print_metric_headers(struct perf_stat_config *config,
				 struct evlist *evlist,
				 const char *prefix, bool no_indent)
{
	struct perf_stat_output_ctx out;
	struct evsel *counter;
	struct outstate os = {
		.fh = config->output
	};
	bool first = true;

		if (config->json_output && !config->interval)
			fprintf(config->output, "{");

	if (prefix && !config->json_output)
		fprintf(config->output, "%s", prefix);

	if (!config->csv_output && !no_indent)
		fprintf(config->output, "%*s",
			aggr_header_lens[config->aggr_mode], "");
	if (config->csv_output) {
		if (config->interval)
			fputs("time,", config->output);
		if (!config->iostat_run)
			fputs(aggr_header_csv[config->aggr_mode], config->output);
	}
	if (config->iostat_run)
		iostat_print_header_prefix(config);

	/* Print metrics headers only */
	evlist__for_each_entry(evlist, counter) {
		os.evsel = counter;
		out.ctx = &os;
		out.print_metric = print_metric_header;
		if (!first && config->json_output)
			fprintf(config->output, ", ");
		first = false;
		out.new_line = new_line_metric;
		out.force_header = true;
		perf_stat__print_shadow_stats(config, counter, 0,
					      0,
					      &out,
					      &config->metric_events,
					      &rt_stat);
	}
	if (config->json_output)
		fprintf(config->output, "}");
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

	if (config->interval_clear)
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

	if ((num_print_interval == 0 && !config->csv_output && !config->json_output)
			 || config->interval_clear) {
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

	if ((num_print_interval == 0 || config->interval_clear)
			 && metric_only && !config->json_output)
		print_metric_headers(config, evlist, " ", true);
	if ((num_print_interval == 0 || config->interval_clear)
			 && metric_only && config->json_output) {
		fprintf(output, "{");
		print_metric_headers(config, evlist, " ", true);
	}
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

static void print_percore_thread(struct perf_stat_config *config,
				 struct evsel *counter, char *prefix)
{
	int s;
	struct aggr_cpu_id s2, id;
	struct perf_cpu_map *cpus;
	bool first = true;
	int idx;
	struct perf_cpu cpu;

	cpus = evsel__cpus(counter);
	perf_cpu_map__for_each_cpu(cpu, idx, cpus) {
		s2 = config->aggr_get_id(config, cpu);
		for (s = 0; s < config->aggr_map->nr; s++) {
			id = config->aggr_map->map[s];
			if (aggr_cpu_id__equal(&s2, &id))
				break;
		}

		print_counter_aggrdata(config, counter, s,
				       prefix, false,
				       &first, cpu);
	}
}

static void print_percore(struct perf_stat_config *config,
			  struct evsel *counter, char *prefix)
{
	bool metric_only = config->metric_only;
	FILE *output = config->output;
	int s;
	bool first = true;

	if (!config->aggr_map || !config->aggr_get_id)
		return;

	if (config->percore_show_thread)
		return print_percore_thread(config, counter, prefix);

	for (s = 0; s < config->aggr_map->nr; s++) {
		if (prefix && metric_only)
			fprintf(output, "%s", prefix);

		print_counter_aggrdata(config, counter, s,
				prefix, metric_only,
				&first, (struct perf_cpu){ .cpu = -1 });
	}

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
		evlist__for_each_entry(evlist, counter) {
			print_aggr_thread(config, _target, counter, prefix);
		}
		break;
	case AGGR_GLOBAL:
		if (config->iostat_run)
			iostat_print_counters(evlist, config, ts, prefix = buf,
					      print_counter_aggr);
		else {
			evlist__for_each_entry(evlist, counter) {
				print_counter_aggr(config, counter, prefix);
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
