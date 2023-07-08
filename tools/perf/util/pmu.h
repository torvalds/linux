/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMU_H
#define __PMU_H

#include <linux/bitmap.h>
#include <linux/compiler.h>
#include <linux/perf_event.h>
#include <linux/list.h>
#include <stdbool.h>
#include <stdio.h>
#include "parse-events.h"
#include "pmu-events/pmu-events.h"

struct evsel_config_term;
struct perf_cpu_map;
struct print_callbacks;

enum {
	PERF_PMU_FORMAT_VALUE_CONFIG,
	PERF_PMU_FORMAT_VALUE_CONFIG1,
	PERF_PMU_FORMAT_VALUE_CONFIG2,
	PERF_PMU_FORMAT_VALUE_CONFIG3,
	PERF_PMU_FORMAT_VALUE_CONFIG_END,
};

#define PERF_PMU_FORMAT_BITS 64
#define MAX_PMU_NAME_LEN 128

struct perf_event_attr;

struct perf_pmu_caps {
	char *name;
	char *value;
	struct list_head list;
};

/**
 * struct perf_pmu
 */
struct perf_pmu {
	/** @name: The name of the PMU such as "cpu". */
	char *name;
	/**
	 * @alias_name: Optional alternate name for the PMU determined in
	 * architecture specific code.
	 */
	char *alias_name;
	/**
	 * @id: Optional PMU identifier read from
	 * <sysfs>/bus/event_source/devices/<name>/identifier.
	 */
	char *id;
	/**
	 * @type: Perf event attributed type value, read from
	 * <sysfs>/bus/event_source/devices/<name>/type.
	 */
	__u32 type;
	/**
	 * @selectable: Can the PMU name be selected as if it were an event?
	 */
	bool selectable;
	/**
	 * @is_core: Is the PMU the core CPU PMU? Determined by the name being
	 * "cpu" or by the presence of
	 * <sysfs>/bus/event_source/devices/<name>/cpus. There may be >1 core
	 * PMU on systems like Intel hybrid.
	 */
	bool is_core;
	/**
	 * @is_uncore: Is the PMU not within the CPU core? Determined by the
	 * presence of <sysfs>/bus/event_source/devices/<name>/cpumask.
	 */
	bool is_uncore;
	/**
	 * @auxtrace: Are events auxiliary events? Determined in architecture
	 * specific code.
	 */
	bool auxtrace;
	/**
	 * @formats_checked: Only check PMU's formats are valid for
	 * perf_event_attr once.
	 */
	bool formats_checked;
	/** @config_masks_present: Are there config format values? */
	bool config_masks_present;
	/** @config_masks_computed: Set when masks are lazily computed. */
	bool config_masks_computed;
	/**
	 * @max_precise: Number of levels of :ppp precision supported by the
	 * PMU, read from
	 * <sysfs>/bus/event_source/devices/<name>/caps/max_precise.
	 */
	int max_precise;
	/**
	 * @default_config: Optional default perf_event_attr determined in
	 * architecture specific code.
	 */
	struct perf_event_attr *default_config;
	/**
	 * @cpus: Empty or the contents of either of:
	 * <sysfs>/bus/event_source/devices/<name>/cpumask.
	 * <sysfs>/bus/event_source/devices/<cpu>/cpus.
	 */
	struct perf_cpu_map *cpus;
	/**
	 * @format: Holds the contents of files read from
	 * <sysfs>/bus/event_source/devices/<name>/format/. The contents specify
	 * which event parameter changes what config, config1 or config2 bits.
	 */
	struct list_head format;
	/**
	 * @aliases: List of struct perf_pmu_alias. Each alias corresponds to an
	 * event read from <sysfs>/bus/event_source/devices/<name>/events/ or
	 * from json events in pmu-events.c.
	 */
	struct list_head aliases;
	/** @caps_initialized: Has the list caps been initialized? */
	bool caps_initialized;
	/** @nr_caps: The length of the list caps. */
	u32 nr_caps;
	/**
	 * @caps: Holds the contents of files read from
	 * <sysfs>/bus/event_source/devices/<name>/caps/.
	 *
	 * The contents are pairs of the filename with the value of its
	 * contents, for example, max_precise (see above) may have a value of 3.
	 */
	struct list_head caps;
	/** @list: Element on pmus list in pmu.c. */
	struct list_head list;

	/**
	 * @config_masks: Derived from the PMU's format data, bits that are
	 * valid within the config value.
	 */
	__u64 config_masks[PERF_PMU_FORMAT_VALUE_CONFIG_END];

	/**
	 * @missing_features: Features to inhibit when events on this PMU are
	 * opened.
	 */
	struct {
		/**
		 * @exclude_guest: Disables perf_event_attr exclude_guest and
		 * exclude_host.
		 */
		bool exclude_guest;
	} missing_features;
};

/** @perf_pmu__fake: A special global PMU used for testing. */
extern struct perf_pmu perf_pmu__fake;

struct perf_pmu_info {
	const char *unit;
	double scale;
	bool per_pkg;
	bool snapshot;
};

#define UNIT_MAX_LEN	31 /* max length for event unit name */

/**
 * struct perf_pmu_alias - An event either read from sysfs or builtin in
 * pmu-events.c, created by parsing the pmu-events json files.
 */
struct perf_pmu_alias {
	/** @name: Name of the event like "mem-loads". */
	char *name;
	/** @desc: Optional short description of the event. */
	char *desc;
	/** @long_desc: Optional long description. */
	char *long_desc;
	/**
	 * @topic: Optional topic such as cache or pipeline, particularly for
	 * json events.
	 */
	char *topic;
	/**
	 * @str: Comma separated parameter list like
	 * "event=0xcd,umask=0x1,ldlat=0x3".
	 */
	char *str;
	/** @terms: Owned list of the original parsed parameters. */
	struct list_head terms;
	/** @list: List element of struct perf_pmu aliases. */
	struct list_head list;
	/** @unit: Units for the event, such as bytes or cache lines. */
	char unit[UNIT_MAX_LEN+1];
	/** @scale: Value to scale read counter values by. */
	double scale;
	/**
	 * @per_pkg: Does the file
	 * <sysfs>/bus/event_source/devices/<pmu_name>/events/<name>.per-pkg or
	 * equivalent json value exist and have the value 1.
	 */
	bool per_pkg;
	/**
	 * @snapshot: Does the file
	 * <sysfs>/bus/event_source/devices/<pmu_name>/events/<name>.snapshot
	 * exist and have the value 1.
	 */
	bool snapshot;
	/**
	 * @deprecated: Is the event hidden and so not shown in perf list by
	 * default.
	 */
	bool deprecated;
	/**
	 * @pmu_name: The name copied from the json struct pmu_event. This can
	 * differ from the PMU name as it won't have suffixes.
	 */
	char *pmu_name;
};

void pmu_add_sys_aliases(struct list_head *head, struct perf_pmu *pmu);
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
void perf_pmu_error(struct list_head *list, char *name, void *scanner, char const *msg);

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits);
void perf_pmu__set_format(unsigned long *bits, long from, long to);
int perf_pmu__format_parse(int dirfd, struct list_head *head);
void perf_pmu__del_formats(struct list_head *formats);

bool is_pmu_core(const char *name);
bool perf_pmu__supports_legacy_cache(const struct perf_pmu *pmu);
bool perf_pmu__auto_merge_stats(const struct perf_pmu *pmu);
bool perf_pmu__have_event(const struct perf_pmu *pmu, const char *name);
/**
 * perf_pmu_is_software - is the PMU a software PMU as in it uses the
 *                        perf_sw_context in the kernel?
 */
bool perf_pmu__is_software(const struct perf_pmu *pmu);

FILE *perf_pmu__open_file(struct perf_pmu *pmu, const char *name);
FILE *perf_pmu__open_file_at(struct perf_pmu *pmu, int dirfd, const char *name);

int perf_pmu__scan_file(struct perf_pmu *pmu, const char *name, const char *fmt, ...) __scanf(3, 4);
int perf_pmu__scan_file_at(struct perf_pmu *pmu, int dirfd, const char *name,
			   const char *fmt, ...) __scanf(4, 5);

bool perf_pmu__file_exists(struct perf_pmu *pmu, const char *name);

int perf_pmu__test(void);

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu);
void pmu_add_cpu_aliases_table(struct list_head *head, struct perf_pmu *pmu,
			       const struct pmu_events_table *table);

char *perf_pmu__getcpuid(struct perf_pmu *pmu);
const struct pmu_events_table *pmu_events_table__find(void);
const struct pmu_metrics_table *pmu_metrics_table__find(void);
void perf_pmu_free_alias(struct perf_pmu_alias *alias);

int perf_pmu__convert_scale(const char *scale, char **end, double *sval);

int perf_pmu__caps_parse(struct perf_pmu *pmu);

void perf_pmu__warn_invalid_config(struct perf_pmu *pmu, __u64 config,
				   const char *name, int config_num,
				   const char *config_name);
void perf_pmu__warn_invalid_formats(struct perf_pmu *pmu);

int perf_pmu__match(char *pattern, char *name, char *tok);

char *pmu_find_real_name(const char *name);
char *pmu_find_alias_name(const char *name);
double perf_pmu__cpu_slots_per_cycle(void);
int perf_pmu__event_source_devices_scnprintf(char *pathname, size_t size);
int perf_pmu__pathname_scnprintf(char *buf, size_t size,
				 const char *pmu_name, const char *filename);
int perf_pmu__event_source_devices_fd(void);
int perf_pmu__pathname_fd(int dirfd, const char *pmu_name, const char *filename, int flags);

struct perf_pmu *perf_pmu__lookup(struct list_head *pmus, int dirfd, const char *lookup_name);
struct perf_pmu *perf_pmu__create_placeholder_core_pmu(struct list_head *core_pmus);
void perf_pmu__delete(struct perf_pmu *pmu);

#endif /* __PMU_H */
