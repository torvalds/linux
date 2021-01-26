/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMU_H
#define __PMU_H

#include <linux/bitmap.h>
#include <linux/compiler.h>
#include <linux/perf_event.h>
#include <stdbool.h>
#include "parse-events.h"
#include "pmu-events/pmu-events.h"

struct evsel_config_term;

enum {
	PERF_PMU_FORMAT_VALUE_CONFIG,
	PERF_PMU_FORMAT_VALUE_CONFIG1,
	PERF_PMU_FORMAT_VALUE_CONFIG2,
};

#define PERF_PMU_FORMAT_BITS 64
#define EVENT_SOURCE_DEVICE_PATH "/bus/event_source/devices/"

struct perf_event_attr;

struct perf_pmu_caps {
	char *name;
	char *value;
	struct list_head list;
};

struct perf_pmu {
	char *name;
	__u32 type;
	bool selectable;
	bool is_uncore;
	bool auxtrace;
	int max_precise;
	struct perf_event_attr *default_config;
	struct perf_cpu_map *cpus;
	struct list_head format;  /* HEAD struct perf_pmu_format -> list */
	struct list_head aliases; /* HEAD struct perf_pmu_alias -> list */
	struct list_head caps;    /* HEAD struct perf_pmu_caps -> list */
	struct list_head list;    /* ELEM */
};

extern struct perf_pmu perf_pmu__fake;

struct perf_pmu_info {
	const char *unit;
	const char *metric_expr;
	const char *metric_name;
	double scale;
	bool per_pkg;
	bool snapshot;
};

#define UNIT_MAX_LEN	31 /* max length for event unit name */

struct perf_pmu_alias {
	char *name;
	char *desc;
	char *long_desc;
	char *topic;
	char *str;
	struct list_head terms; /* HEAD struct parse_events_term -> list */
	struct list_head list;  /* ELEM */
	char unit[UNIT_MAX_LEN+1];
	double scale;
	bool per_pkg;
	bool snapshot;
	bool deprecated;
	char *metric_expr;
	char *metric_name;
};

struct perf_pmu *perf_pmu__find(const char *name);
struct perf_pmu *perf_pmu__find_by_type(unsigned int type);
int perf_pmu__config(struct perf_pmu *pmu, struct perf_event_attr *attr,
		     struct list_head *head_terms,
		     struct parse_events_error *error);
int perf_pmu__config_terms(const char *pmu_name, struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct list_head *head_terms,
			   bool zero, struct parse_events_error *error);
__u64 perf_pmu__format_bits(struct list_head *formats, const char *name);
int perf_pmu__format_type(struct list_head *formats, const char *name);
int perf_pmu__check_alias(struct perf_pmu *pmu, struct list_head *head_terms,
			  struct perf_pmu_info *info);
struct list_head *perf_pmu__alias(struct perf_pmu *pmu,
				  struct list_head *head_terms);
void perf_pmu_error(struct list_head *list, char *name, char const *msg);

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits);
void perf_pmu__set_format(unsigned long *bits, long from, long to);
int perf_pmu__format_parse(char *dir, struct list_head *head);
void perf_pmu__del_formats(struct list_head *formats);

struct perf_pmu *perf_pmu__scan(struct perf_pmu *pmu);

bool is_pmu_core(const char *name);
void print_pmu_events(const char *event_glob, bool name_only, bool quiet,
		      bool long_desc, bool details_flag,
		      bool deprecated);
bool pmu_have_event(const char *pname, const char *name);

int perf_pmu__scan_file(struct perf_pmu *pmu, const char *name, const char *fmt, ...) __scanf(3, 4);

int perf_pmu__test(void);

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu);
void pmu_add_cpu_aliases_map(struct list_head *head, struct perf_pmu *pmu,
			     struct pmu_events_map *map);

struct pmu_events_map *perf_pmu__find_map(struct perf_pmu *pmu);
bool pmu_uncore_alias_match(const char *pmu_name, const char *name);
void perf_pmu_free_alias(struct perf_pmu_alias *alias);

int perf_pmu__convert_scale(const char *scale, char **end, double *sval);

int perf_pmu__caps_parse(struct perf_pmu *pmu);

#endif /* __PMU_H */
