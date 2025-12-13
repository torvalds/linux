// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <linux/ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <dirent.h>
#include <api/fs/fs.h>
#include <api/io.h>
#include <api/io_dir.h>
#include <locale.h>
#include <fnmatch.h>
#include <math.h>
#include "debug.h"
#include "evsel.h"
#include "pmu.h"
#include "drm_pmu.h"
#include "hwmon_pmu.h"
#include "pmus.h"
#include "tool_pmu.h"
#include "tp_pmu.h"
#include <util/pmu-bison.h>
#include <util/pmu-flex.h>
#include "parse-events.h"
#include "print-events.h"
#include "hashmap.h"
#include "header.h"
#include "string2.h"
#include "strbuf.h"
#include "fncache.h"
#include "util/evsel_config.h"
#include <regex.h>

#define UNIT_MAX_LEN	31 /* max length for event unit name */

enum event_source {
	/* An event loaded from /sys/bus/event_source/devices/<pmu>/events. */
	EVENT_SRC_SYSFS,
	/* An event loaded from a CPUID matched json file. */
	EVENT_SRC_CPU_JSON,
	/*
	 * An event loaded from a /sys/bus/event_source/devices/<pmu>/identifier matched json
	 * file.
	 */
	EVENT_SRC_SYS_JSON,
};

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
	/** @terms: Owned list of the original parsed parameters. */
	struct parse_events_terms terms;
	/**
	 * @pmu_name: The name copied from the json struct pmu_event. This can
	 * differ from the PMU name as it won't have suffixes.
	 */
	char *pmu_name;
	/** @unit: Units for the event, such as bytes or cache lines. */
	char unit[UNIT_MAX_LEN+1];
	/** @scale: Value to scale read counter values by. */
	double scale;
	/** @retirement_latency_mean: Value to be given for unsampled retirement latency mean. */
	double retirement_latency_mean;
	/** @retirement_latency_min: Value to be given for unsampled retirement latency min. */
	double retirement_latency_min;
	/** @retirement_latency_max: Value to be given for unsampled retirement latency max. */
	double retirement_latency_max;
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
	/** @from_sysfs: Was the alias from sysfs or a json event? */
	bool from_sysfs;
	/** @info_loaded: Have the scale, unit and other values been read from disk? */
	bool info_loaded;
};

/**
 * struct perf_pmu_format - Values from a format file read from
 * <sysfs>/devices/cpu/format/ held in struct perf_pmu.
 *
 * For example, the contents of <sysfs>/devices/cpu/format/event may be
 * "config:0-7" and will be represented here as name="event",
 * value=PERF_PMU_FORMAT_VALUE_CONFIG and bits 0 to 7 will be set.
 */
struct perf_pmu_format {
	/** @list: Element on list within struct perf_pmu. */
	struct list_head list;
	/** @bits: Which config bits are set by this format value. */
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	/** @name: The modifier/file name. */
	char *name;
	/**
	 * @value : Which config value the format relates to. Supported values
	 * are from PERF_PMU_FORMAT_VALUE_CONFIG to
	 * PERF_PMU_FORMAT_VALUE_CONFIG_END.
	 */
	u16 value;
	/** @loaded: Has the contents been loaded/parsed. */
	bool loaded;
};

static int pmu_aliases_parse(struct perf_pmu *pmu);

static struct perf_pmu_format *perf_pmu__new_format(struct list_head *list, char *name)
{
	struct perf_pmu_format *format;

	format = zalloc(sizeof(*format));
	if (!format)
		return NULL;

	format->name = strdup(name);
	if (!format->name) {
		free(format);
		return NULL;
	}
	list_add_tail(&format->list, list);
	return format;
}

/* Called at the end of parsing a format. */
void perf_pmu_format__set_value(void *vformat, int config, unsigned long *bits)
{
	struct perf_pmu_format *format = vformat;

	format->value = config;
	memcpy(format->bits, bits, sizeof(format->bits));
}

static void __perf_pmu_format__load(struct perf_pmu_format *format, FILE *file)
{
	void *scanner;
	int ret;

	ret = perf_pmu_lex_init(&scanner);
	if (ret)
		return;

	perf_pmu_set_in(file, scanner);
	ret = perf_pmu_parse(format, scanner);
	perf_pmu_lex_destroy(scanner);
	format->loaded = true;
}

static void perf_pmu_format__load(const struct perf_pmu *pmu, struct perf_pmu_format *format)
{
	char path[PATH_MAX];
	FILE *file = NULL;

	if (format->loaded)
		return;

	if (!perf_pmu__pathname_scnprintf(path, sizeof(path), pmu->name, "format"))
		return;

	assert(strlen(path) + strlen(format->name) + 2 < sizeof(path));
	strcat(path, "/");
	strcat(path, format->name);

	file = fopen(path, "r");
	if (!file)
		return;
	__perf_pmu_format__load(format, file);
	fclose(file);
}

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
static int perf_pmu__format_parse(struct perf_pmu *pmu, int dirfd, bool eager_load)
{
	struct io_dirent64 *evt_ent;
	struct io_dir format_dir;
	int ret = 0;

	io_dir__init(&format_dir, dirfd);

	while ((evt_ent = io_dir__readdir(&format_dir)) != NULL) {
		struct perf_pmu_format *format;
		char *name = evt_ent->d_name;

		if (io_dir__is_dir(&format_dir, evt_ent))
			continue;

		format = perf_pmu__new_format(&pmu->format, name);
		if (!format) {
			ret = -ENOMEM;
			break;
		}

		if (eager_load) {
			FILE *file;
			int fd = openat(dirfd, name, O_RDONLY);

			if (fd < 0) {
				ret = -errno;
				break;
			}
			file = fdopen(fd, "r");
			if (!file) {
				close(fd);
				break;
			}
			__perf_pmu_format__load(format, file);
			fclose(file);
		}
	}

	close(format_dir.dirfd);
	return ret;
}

/*
 * Reading/parsing the default pmu format definition, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/format as sysfs group attributes.
 */
static int pmu_format(struct perf_pmu *pmu, int dirfd, const char *name, bool eager_load)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, name, "format", O_DIRECTORY);
	if (fd < 0)
		return 0;

	/* it'll close the fd */
	if (perf_pmu__format_parse(pmu, fd, eager_load))
		return -1;

	return 0;
}

static int parse_double(const char *scale, char **end, double *sval)
{
	char *lc;
	int ret = 0;

	/*
	 * save current locale
	 */
	lc = setlocale(LC_NUMERIC, NULL);

	/*
	 * The lc string may be allocated in static storage,
	 * so get a dynamic copy to make it survive setlocale
	 * call below.
	 */
	lc = strdup(lc);
	if (!lc) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * force to C locale to ensure kernel
	 * scale string is converted correctly.
	 * kernel uses default C locale.
	 */
	setlocale(LC_NUMERIC, "C");

	*sval = strtod(scale, end);

out:
	/* restore locale */
	setlocale(LC_NUMERIC, lc);
	free(lc);
	return ret;
}

int perf_pmu__convert_scale(const char *scale, char **end, double *sval)
{
	return parse_double(scale, end, sval);
}

static int perf_pmu__parse_scale(struct perf_pmu *pmu, struct perf_pmu_alias *alias)
{
	struct stat st;
	ssize_t sret;
	size_t len;
	char scale[128];
	int fd, ret = -1;
	char path[PATH_MAX];

	len = perf_pmu__event_source_devices_scnprintf(path, sizeof(path));
	if (!len)
		return 0;
	scnprintf(path + len, sizeof(path) - len, "%s/events/%s.scale", pmu->name, alias->name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	if (fstat(fd, &st) < 0)
		goto error;

	sret = read(fd, scale, sizeof(scale)-1);
	if (sret < 0)
		goto error;

	if (scale[sret - 1] == '\n')
		scale[sret - 1] = '\0';
	else
		scale[sret] = '\0';

	ret = perf_pmu__convert_scale(scale, NULL, &alias->scale);
error:
	close(fd);
	return ret;
}

static int perf_pmu__parse_unit(struct perf_pmu *pmu, struct perf_pmu_alias *alias)
{
	char path[PATH_MAX];
	size_t len;
	ssize_t sret;
	int fd;


	len = perf_pmu__event_source_devices_scnprintf(path, sizeof(path));
	if (!len)
		return 0;
	scnprintf(path + len, sizeof(path) - len, "%s/events/%s.unit", pmu->name, alias->name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	sret = read(fd, alias->unit, UNIT_MAX_LEN);
	if (sret < 0)
		goto error;

	close(fd);

	if (alias->unit[sret - 1] == '\n')
		alias->unit[sret - 1] = '\0';
	else
		alias->unit[sret] = '\0';

	return 0;
error:
	close(fd);
	alias->unit[0] = '\0';
	return -1;
}

static bool perf_pmu__parse_event_source_bool(const char *pmu_name, const char *event_name,
					      const char *suffix)
{
	char path[PATH_MAX];
	size_t len;
	int fd;

	len = perf_pmu__event_source_devices_scnprintf(path, sizeof(path));
	if (!len)
		return false;

	scnprintf(path + len, sizeof(path) - len, "%s/events/%s.%s", pmu_name, event_name, suffix);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return false;

#ifndef NDEBUG
	{
		char buf[8];

		len = read(fd, buf, sizeof(buf));
		assert(len == 1 || len == 2);
		assert(buf[0] == '1');
	}
#endif

	close(fd);
	return true;
}

static void perf_pmu__parse_per_pkg(struct perf_pmu *pmu, struct perf_pmu_alias *alias)
{
	alias->per_pkg = perf_pmu__parse_event_source_bool(pmu->name, alias->name, "per-pkg");
}

static void perf_pmu__parse_snapshot(struct perf_pmu *pmu, struct perf_pmu_alias *alias)
{
	alias->snapshot = perf_pmu__parse_event_source_bool(pmu->name, alias->name, "snapshot");
}

/* Delete an alias entry. */
static void perf_pmu_free_alias(struct perf_pmu_alias *alias)
{
	if (!alias)
		return;

	zfree(&alias->name);
	zfree(&alias->desc);
	zfree(&alias->long_desc);
	zfree(&alias->topic);
	zfree(&alias->pmu_name);
	parse_events_terms__exit(&alias->terms);
	free(alias);
}

static void perf_pmu__del_aliases(struct perf_pmu *pmu)
{
	struct hashmap_entry *entry;
	size_t bkt;

	if (!pmu->aliases)
		return;

	hashmap__for_each_entry(pmu->aliases, entry, bkt)
		perf_pmu_free_alias(entry->pvalue);

	hashmap__free(pmu->aliases);
	pmu->aliases = NULL;
}

static struct perf_pmu_alias *perf_pmu__find_alias(struct perf_pmu *pmu,
						   const char *name,
						   bool load)
{
	struct perf_pmu_alias *alias;
	bool has_sysfs_event;
	char event_file_name[NAME_MAX + 8];

	if (hashmap__find(pmu->aliases, name, &alias))
		return alias;

	if (!load || pmu->sysfs_aliases_loaded)
		return NULL;

	/*
	 * Test if alias/event 'name' exists in the PMU's sysfs/events
	 * directory. If not skip parsing the sysfs aliases. Sysfs event
	 * name must be all lower or all upper case.
	 */
	scnprintf(event_file_name, sizeof(event_file_name), "events/%s", name);
	for (size_t i = 7, n = 7 + strlen(name); i < n; i++)
		event_file_name[i] = tolower(event_file_name[i]);

	has_sysfs_event = perf_pmu__file_exists(pmu, event_file_name);
	if (!has_sysfs_event) {
		for (size_t i = 7, n = 7 + strlen(name); i < n; i++)
			event_file_name[i] = toupper(event_file_name[i]);

		has_sysfs_event = perf_pmu__file_exists(pmu, event_file_name);
	}
	if (has_sysfs_event) {
		pmu_aliases_parse(pmu);
		if (hashmap__find(pmu->aliases, name, &alias))
			return alias;
	}

	return NULL;
}

static bool assign_str(const char *name, const char *field, char **old_str,
				const char *new_str)
{
	if (!*old_str && new_str) {
		*old_str = strdup(new_str);
		return true;
	}

	if (!new_str || !strcasecmp(*old_str, new_str))
		return false; /* Nothing to update. */

	pr_debug("alias %s differs in field '%s' ('%s' != '%s')\n",
		name, field, *old_str, new_str);
	zfree(old_str);
	*old_str = strdup(new_str);
	return true;
}

static void read_alias_info(struct perf_pmu *pmu, struct perf_pmu_alias *alias)
{
	if (!alias->from_sysfs || alias->info_loaded)
		return;

	/*
	 * load unit name and scale if available
	 */
	perf_pmu__parse_unit(pmu, alias);
	perf_pmu__parse_scale(pmu, alias);
	perf_pmu__parse_per_pkg(pmu, alias);
	perf_pmu__parse_snapshot(pmu, alias);
}

struct update_alias_data {
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;
};

static int update_alias(const struct pmu_event *pe,
			const struct pmu_events_table *table __maybe_unused,
			void *vdata)
{
	struct update_alias_data *data = vdata;
	int ret = 0;

	read_alias_info(data->pmu, data->alias);
	assign_str(pe->name, "desc", &data->alias->desc, pe->desc);
	assign_str(pe->name, "long_desc", &data->alias->long_desc, pe->long_desc);
	assign_str(pe->name, "topic", &data->alias->topic, pe->topic);
	data->alias->per_pkg = pe->perpkg;
	if (pe->event) {
		parse_events_terms__exit(&data->alias->terms);
		ret = parse_events_terms(&data->alias->terms, pe->event, /*input=*/NULL);
	}
	if (!ret && pe->unit) {
		char *unit;

		ret = perf_pmu__convert_scale(pe->unit, &unit, &data->alias->scale);
		if (!ret)
			snprintf(data->alias->unit, sizeof(data->alias->unit), "%s", unit);
	}
	if (!ret && pe->retirement_latency_mean) {
		ret = parse_double(pe->retirement_latency_mean, NULL,
					      &data->alias->retirement_latency_mean);
	}
	if (!ret && pe->retirement_latency_min) {
		ret = parse_double(pe->retirement_latency_min, NULL,
					      &data->alias->retirement_latency_min);
	}
	if (!ret && pe->retirement_latency_max) {
		ret = parse_double(pe->retirement_latency_max, NULL,
					      &data->alias->retirement_latency_max);
	}
	return ret;
}

static int perf_pmu__new_alias(struct perf_pmu *pmu, const char *name,
				const char *desc, const char *val, FILE *val_fd,
			        const struct pmu_event *pe, enum event_source src)
{
	struct perf_pmu_alias *alias, *old_alias;
	int ret = 0;
	const char *long_desc = NULL, *topic = NULL, *unit = NULL, *pmu_name = NULL;
	bool deprecated = false, perpkg = false;

	if (perf_pmu__find_alias(pmu, name, /*load=*/ false)) {
		/* Alias was already created/loaded. */
		return 0;
	}

	if (pe) {
		long_desc = pe->long_desc;
		topic = pe->topic;
		unit = pe->unit;
		perpkg = pe->perpkg;
		deprecated = pe->deprecated;
		if (pe->pmu && strcmp(pe->pmu, "default_core"))
			pmu_name = pe->pmu;
	}

	alias = zalloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;

	parse_events_terms__init(&alias->terms);
	alias->scale = 1.0;
	alias->unit[0] = '\0';
	alias->per_pkg = perpkg;
	alias->snapshot = false;
	alias->deprecated = deprecated;
	alias->retirement_latency_mean = 0.0;
	alias->retirement_latency_min = 0.0;
	alias->retirement_latency_max = 0.0;

	if (!ret && pe && pe->retirement_latency_mean) {
		ret = parse_double(pe->retirement_latency_mean, NULL,
				   &alias->retirement_latency_mean);
	}
	if (!ret && pe && pe->retirement_latency_min) {
		ret = parse_double(pe->retirement_latency_min, NULL,
				   &alias->retirement_latency_min);
	}
	if (!ret && pe && pe->retirement_latency_max) {
		ret = parse_double(pe->retirement_latency_max, NULL,
				   &alias->retirement_latency_max);
	}
	if (ret)
		return ret;

	ret = parse_events_terms(&alias->terms, val, val_fd);
	if (ret) {
		pr_err("Cannot parse alias %s: %d\n", val, ret);
		free(alias);
		return ret;
	}

	alias->name = strdup(name);
	alias->desc = desc ? strdup(desc) : NULL;
	alias->long_desc = long_desc ? strdup(long_desc) : NULL;
	alias->topic = topic ? strdup(topic) : NULL;
	alias->pmu_name = pmu_name ? strdup(pmu_name) : NULL;
	if (unit) {
		if (perf_pmu__convert_scale(unit, (char **)&unit, &alias->scale) < 0) {
			perf_pmu_free_alias(alias);
			return -1;
		}
		snprintf(alias->unit, sizeof(alias->unit), "%s", unit);
	}
	switch (src) {
	default:
	case EVENT_SRC_SYSFS:
		alias->from_sysfs = true;
		if (pmu->events_table) {
			/* Update an event from sysfs with json data. */
			struct update_alias_data data = {
				.pmu = pmu,
				.alias = alias,
			};
			if (pmu_events_table__find_event(pmu->events_table, pmu, name,
							 update_alias, &data) == 0)
				pmu->cpu_common_json_aliases++;
		}
		pmu->sysfs_aliases++;
		break;
	case  EVENT_SRC_CPU_JSON:
		pmu->cpu_json_aliases++;
		break;
	case  EVENT_SRC_SYS_JSON:
		pmu->sys_json_aliases++;
		break;

	}
	hashmap__set(pmu->aliases, alias->name, alias, /*old_key=*/ NULL, &old_alias);
	perf_pmu_free_alias(old_alias);
	return 0;
}

static inline bool pmu_alias_info_file(const char *name)
{
	size_t len;

	len = strlen(name);
	if (len > 5 && !strcmp(name + len - 5, ".unit"))
		return true;
	if (len > 6 && !strcmp(name + len - 6, ".scale"))
		return true;
	if (len > 8 && !strcmp(name + len - 8, ".per-pkg"))
		return true;
	if (len > 9 && !strcmp(name + len - 9, ".snapshot"))
		return true;

	return false;
}

/*
 * Reading the pmu event aliases definition, which should be located at:
 * /sys/bus/event_source/devices/<dev>/events as sysfs group attributes.
 */
static int __pmu_aliases_parse(struct perf_pmu *pmu, int events_dir_fd)
{
	struct io_dirent64 *evt_ent;
	struct io_dir event_dir;

	io_dir__init(&event_dir, events_dir_fd);

	while ((evt_ent = io_dir__readdir(&event_dir))) {
		char *name = evt_ent->d_name;
		int fd;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/*
		 * skip info files parsed in perf_pmu__new_alias()
		 */
		if (pmu_alias_info_file(name))
			continue;

		fd = openat(events_dir_fd, name, O_RDONLY);
		if (fd == -1) {
			pr_debug("Cannot open %s\n", name);
			continue;
		}
		file = fdopen(fd, "r");
		if (!file) {
			close(fd);
			continue;
		}

		if (perf_pmu__new_alias(pmu, name, /*desc=*/ NULL,
					/*val=*/ NULL, file, /*pe=*/ NULL,
					EVENT_SRC_SYSFS) < 0)
			pr_debug("Cannot set up %s\n", name);
		fclose(file);
	}

	pmu->sysfs_aliases_loaded = true;
	return 0;
}

static int pmu_aliases_parse(struct perf_pmu *pmu)
{
	char path[PATH_MAX];
	size_t len;
	int events_dir_fd, ret;

	if (pmu->sysfs_aliases_loaded)
		return 0;

	len = perf_pmu__event_source_devices_scnprintf(path, sizeof(path));
	if (!len)
		return 0;
	scnprintf(path + len, sizeof(path) - len, "%s/events", pmu->name);

	events_dir_fd = open(path, O_DIRECTORY);
	if (events_dir_fd == -1) {
		pmu->sysfs_aliases_loaded = true;
		return 0;
	}
	ret = __pmu_aliases_parse(pmu, events_dir_fd);
	close(events_dir_fd);
	return ret;
}

static int pmu_aliases_parse_eager(struct perf_pmu *pmu, int sysfs_fd)
{
	char path[NAME_MAX + 8];
	int ret, events_dir_fd;

	scnprintf(path, sizeof(path), "%s/events", pmu->name);
	events_dir_fd = openat(sysfs_fd, path, O_DIRECTORY, 0);
	if (events_dir_fd == -1) {
		pmu->sysfs_aliases_loaded = true;
		return 0;
	}
	ret = __pmu_aliases_parse(pmu, events_dir_fd);
	close(events_dir_fd);
	return ret;
}

static int pmu_alias_terms(struct perf_pmu_alias *alias, int err_loc, struct list_head *terms)
{
	struct parse_events_term *term, *cloned;
	struct parse_events_terms clone_terms;

	parse_events_terms__init(&clone_terms);
	list_for_each_entry(term, &alias->terms.terms, list) {
		int ret = parse_events_term__clone(&cloned, term);

		if (ret) {
			parse_events_terms__exit(&clone_terms);
			return ret;
		}
		/*
		 * Weak terms don't override command line options,
		 * which we don't want for implicit terms in aliases.
		 */
		cloned->weak = true;
		cloned->err_term = cloned->err_val = err_loc;
		list_add_tail(&cloned->list, &clone_terms.terms);
	}
	list_splice_init(&clone_terms.terms, terms);
	parse_events_terms__exit(&clone_terms);
	return 0;
}

/*
 * Uncore PMUs have a "cpumask" file under sysfs. CPU PMUs (e.g. on arm/arm64)
 * may have a "cpus" file.
 */
static struct perf_cpu_map *pmu_cpumask(int dirfd, const char *pmu_name, bool is_core)
{
	const char *templates[] = {
		"cpumask",
		"cpus",
		NULL
	};
	const char **template;

	for (template = templates; *template; template++) {
		struct io io;
		char buf[128];
		char *cpumask = NULL;
		size_t cpumask_len;
		ssize_t ret;
		struct perf_cpu_map *cpus;

		io.fd = perf_pmu__pathname_fd(dirfd, pmu_name, *template, O_RDONLY);
		if (io.fd < 0)
			continue;

		io__init(&io, io.fd, buf, sizeof(buf));
		ret = io__getline(&io, &cpumask, &cpumask_len);
		close(io.fd);
		if (ret < 0)
			continue;

		cpus = perf_cpu_map__new(cpumask);
		free(cpumask);
		if (cpus)
			return cpus;
	}

	/* Nothing found, for core PMUs assume this means all CPUs. */
	return is_core ? cpu_map__online() : NULL;
}

static bool pmu_is_uncore(int dirfd, const char *name)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, name, "cpumask", O_PATH);
	if (fd < 0)
		return false;

	close(fd);
	return true;
}

static char *pmu_id(const char *name)
{
	char path[PATH_MAX], *str;
	size_t len;

	perf_pmu__pathname_scnprintf(path, sizeof(path), name, "identifier");

	if (filename__read_str(path, &str, &len) < 0)
		return NULL;

	str[len - 1] = 0; /* remove line feed */

	return str;
}

/**
 * is_sysfs_pmu_core() - PMU CORE devices have different name other than cpu in
 *         sysfs on some platforms like ARM or Intel hybrid. Looking for
 *         possible the cpus file in sysfs files to identify whether this is a
 *         core device.
 * @name: The PMU name such as "cpu_atom".
 */
static int is_sysfs_pmu_core(const char *name)
{
	char path[PATH_MAX];

	if (!perf_pmu__pathname_scnprintf(path, sizeof(path), name, "cpus"))
		return 0;
	return file_available(path);
}

/**
 * Return the length of the PMU name not including the suffix for uncore PMUs.
 *
 * We want to deduplicate many similar uncore PMUs by stripping their suffixes,
 * but there are never going to be too many core PMUs and the suffixes might be
 * interesting. "arm_cortex_a53" vs "arm_cortex_a57" or "cpum_cf" for example.
 *
 * @skip_duplicate_pmus: False in verbose mode so all uncore PMUs are visible
 */
static size_t pmu_deduped_name_len(const struct perf_pmu *pmu, const char *name,
				   bool skip_duplicate_pmus)
{
	return skip_duplicate_pmus && !pmu->is_core
		? pmu_name_len_no_suffix(name)
		: strlen(name);
}

/**
 * perf_pmu__match_wildcard - Does the pmu_name start with tok and is then only
 *                            followed by nothing or a suffix? tok may contain
 *                            part of a suffix.
 * @pmu_name: The pmu_name with possible suffix.
 * @tok: The wildcard argument to match.
 */
static bool perf_pmu__match_wildcard(const char *pmu_name, const char *tok)
{
	const char *p, *suffix;
	bool has_hex = false;
	size_t tok_len = strlen(tok);

	/* Check start of pmu_name for equality. */
	if (strncmp(pmu_name, tok, tok_len))
		return false;

	suffix = p = pmu_name + tok_len;
	if (*p == 0)
		return true;

	if (*p == '_') {
		++p;
		++suffix;
	}

	/* Ensure we end in a number */
	while (1) {
		if (!isxdigit(*p))
			return false;
		if (!has_hex)
			has_hex = !isdigit(*p);
		if (*(++p) == 0)
			break;
	}

	if (has_hex)
		return (p - suffix) > 2;

	return true;
}

/**
 * perf_pmu__match_ignoring_suffix_uncore - Does the pmu_name match tok ignoring
 *                                          any trailing suffix on pmu_name and
 *                                          tok?  The Suffix must be in form
 *                                          tok_{digits}, or tok{digits}.
 * @pmu_name: The pmu_name with possible suffix.
 * @tok: The possible match to pmu_name.
 */
static bool perf_pmu__match_ignoring_suffix_uncore(const char *pmu_name, const char *tok)
{
	size_t pmu_name_len, tok_len;

	/* For robustness, check for NULL. */
	if (pmu_name == NULL)
		return tok == NULL;

	/* uncore_ prefixes are ignored. */
	if (!strncmp(pmu_name, "uncore_", 7))
		pmu_name += 7;
	if (!strncmp(tok, "uncore_", 7))
		tok += 7;

	pmu_name_len = pmu_name_len_no_suffix(pmu_name);
	tok_len = pmu_name_len_no_suffix(tok);
	if (pmu_name_len != tok_len)
		return false;

	return strncmp(pmu_name, tok, pmu_name_len) == 0;
}


/**
 * perf_pmu__match_wildcard_uncore - does to_match match the PMU's name?
 * @pmu_name: The pmu->name or pmu->alias to match against.
 * @to_match: the json struct pmu_event name. This may lack a suffix (which
 *            matches) or be of the form "socket,pmuname" which will match
 *            "socketX_pmunameY".
 */
static bool perf_pmu__match_wildcard_uncore(const char *pmu_name, const char *to_match)
{
	char *mutable_to_match, *tok, *tmp;

	if (!pmu_name)
		return false;

	/* uncore_ prefixes are ignored. */
	if (!strncmp(pmu_name, "uncore_", 7))
		pmu_name += 7;
	if (!strncmp(to_match, "uncore_", 7))
		to_match += 7;

	if (strchr(to_match, ',') == NULL)
		return perf_pmu__match_wildcard(pmu_name, to_match);

	/* Process comma separated list of PMU name components. */
	mutable_to_match = strdup(to_match);
	if (!mutable_to_match)
		return false;

	tok = strtok_r(mutable_to_match, ",", &tmp);
	while (tok) {
		size_t tok_len = strlen(tok);

		if (strncmp(pmu_name, tok, tok_len)) {
			/* Mismatch between part of pmu_name and tok. */
			free(mutable_to_match);
			return false;
		}
		/* Move pmu_name forward over tok and suffix. */
		pmu_name += tok_len;
		while (*pmu_name != '\0' && isdigit(*pmu_name))
			pmu_name++;
		if (*pmu_name == '_')
			pmu_name++;

		tok = strtok_r(NULL, ",", &tmp);
	}
	free(mutable_to_match);
	return *pmu_name == '\0';
}

bool pmu_uncore_identifier_match(const char *compat, const char *id)
{
	regex_t re;
	regmatch_t pmatch[1];
	int match;

	if (regcomp(&re, compat, REG_EXTENDED) != 0) {
		/* Warn unable to generate match particular string. */
		pr_info("Invalid regular expression %s\n", compat);
		return false;
	}

	match = !regexec(&re, id, 1, pmatch, 0);
	if (match) {
		/* Ensure a full match. */
		match = pmatch[0].rm_so == 0 && (size_t)pmatch[0].rm_eo == strlen(id);
	}
	regfree(&re);

	return match;
}

static int pmu_add_cpu_aliases_map_callback(const struct pmu_event *pe,
					const struct pmu_events_table *table __maybe_unused,
					void *vdata)
{
	struct perf_pmu *pmu = vdata;

	perf_pmu__new_alias(pmu, pe->name, pe->desc, pe->event, /*val_fd=*/ NULL,
			    pe, EVENT_SRC_CPU_JSON);
	return 0;
}

/*
 * From the pmu_events_table, find the events that correspond to the given
 * PMU and add them to the list 'head'.
 */
void pmu_add_cpu_aliases_table(struct perf_pmu *pmu, const struct pmu_events_table *table)
{
	pmu_events_table__for_each_event(table, pmu, pmu_add_cpu_aliases_map_callback, pmu);
}

static void pmu_add_cpu_aliases(struct perf_pmu *pmu)
{
	if (!pmu->events_table)
		return;

	if (pmu->cpu_aliases_added)
		return;

	pmu_add_cpu_aliases_table(pmu, pmu->events_table);
	pmu->cpu_aliases_added = true;
}

static int pmu_add_sys_aliases_iter_fn(const struct pmu_event *pe,
				       const struct pmu_events_table *table __maybe_unused,
				       void *vdata)
{
	struct perf_pmu *pmu = vdata;

	if (!pe->compat || !pe->pmu) {
		/* No data to match. */
		return 0;
	}

	if (!perf_pmu__match_wildcard_uncore(pmu->name, pe->pmu) &&
	    !perf_pmu__match_wildcard_uncore(pmu->alias_name, pe->pmu)) {
		/* PMU name/alias_name don't match. */
		return 0;
	}

	if (pmu_uncore_identifier_match(pe->compat, pmu->id)) {
		/* Id matched. */
		perf_pmu__new_alias(pmu,
				pe->name,
				pe->desc,
				pe->event,
				/*val_fd=*/ NULL,
				pe,
				EVENT_SRC_SYS_JSON);
	}
	return 0;
}

void pmu_add_sys_aliases(struct perf_pmu *pmu)
{
	if (!pmu->id)
		return;

	pmu_for_each_sys_event(pmu_add_sys_aliases_iter_fn, pmu);
}

static char *pmu_find_alias_name(struct perf_pmu *pmu, int dirfd)
{
	FILE *file = perf_pmu__open_file_at(pmu, dirfd, "alias");
	char *line = NULL;
	size_t line_len = 0;
	ssize_t ret;

	if (!file)
		return NULL;

	ret = getline(&line, &line_len, file);
	if (ret < 0) {
		fclose(file);
		return NULL;
	}
	/* Remove trailing newline. */
	if (ret > 0 && line[ret - 1] == '\n')
		line[--ret] = '\0';

	fclose(file);
	return line;
}

static int pmu_max_precise(int dirfd, struct perf_pmu *pmu)
{
	int max_precise = -1;

	perf_pmu__scan_file_at(pmu, dirfd, "caps/max_precise", "%d", &max_precise);
	return max_precise;
}

void __weak
perf_pmu__arch_init(struct perf_pmu *pmu)
{
	if (pmu->is_core)
		pmu->mem_events = perf_mem_events;
}

/* Variant of str_hash that does tolower on each character. */
static size_t aliases__hash(long key, void *ctx __maybe_unused)
{
	const char *s = (const char *)key;
	size_t h = 0;

	while (*s) {
		h = h * 31 + tolower(*s);
		s++;
	}
	return h;
}

static bool aliases__equal(long key1, long key2, void *ctx __maybe_unused)
{
	return strcasecmp((const char *)key1, (const char *)key2) == 0;
}

int perf_pmu__init(struct perf_pmu *pmu, __u32 type, const char *name)
{
	pmu->type = type;
	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->caps);

	pmu->name = strdup(name);
	if (!pmu->name)
		return -ENOMEM;

	pmu->aliases = hashmap__new(aliases__hash, aliases__equal, /*ctx=*/ NULL);
	if (!pmu->aliases)
		return -ENOMEM;

	return 0;
}

static __u32 wellknown_pmu_type(const char *pmu_name)
{
	struct {
		const char *pmu_name;
		__u32 type;
	} wellknown_pmus[] = {
		{
			"software",
			PERF_TYPE_SOFTWARE
		},
		{
			"tracepoint",
			PERF_TYPE_TRACEPOINT
		},
		{
			"breakpoint",
			PERF_TYPE_BREAKPOINT
		},
	};
	for (size_t i = 0; i < ARRAY_SIZE(wellknown_pmus); i++) {
		if (!strcmp(wellknown_pmus[i].pmu_name, pmu_name))
			return wellknown_pmus[i].type;
	}
	return PERF_TYPE_MAX;
}

struct perf_pmu *perf_pmu__lookup(struct list_head *pmus, int dirfd, const char *name,
				  bool eager_load)
{
	struct perf_pmu *pmu;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	if (perf_pmu__init(pmu, PERF_PMU_TYPE_FAKE, name) != 0) {
		perf_pmu__delete(pmu);
		return NULL;
	}

	/*
	 * Read type early to fail fast if a lookup name isn't a PMU. Ensure
	 * that type value is successfully assigned (return 1).
	 */
	if (perf_pmu__scan_file_at(pmu, dirfd, "type", "%u", &pmu->type) != 1) {
		/* Double check the PMU's name isn't wellknown. */
		pmu->type = wellknown_pmu_type(name);
		if (pmu->type == PERF_TYPE_MAX) {
			perf_pmu__delete(pmu);
			return NULL;
		}
	}

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(pmu, dirfd, name, eager_load)) {
		perf_pmu__delete(pmu);
		return NULL;
	}

	pmu->is_core = is_pmu_core(name);
	pmu->cpus = pmu_cpumask(dirfd, name, pmu->is_core);

	pmu->is_uncore = pmu_is_uncore(dirfd, name);
	if (pmu->is_uncore)
		pmu->id = pmu_id(name);
	pmu->max_precise = pmu_max_precise(dirfd, pmu);
	pmu->alias_name = pmu_find_alias_name(pmu, dirfd);
	pmu->events_table = perf_pmu__find_events_table(pmu);
	/*
	 * Load the sys json events/aliases when loading the PMU as each event
	 * may have a different compat regular expression. We therefore can't
	 * know the number of sys json events/aliases without computing the
	 * regular expressions for them all.
	 */
	pmu_add_sys_aliases(pmu);
	list_add_tail(&pmu->list, pmus);

	perf_pmu__arch_init(pmu);

	if (eager_load)
		pmu_aliases_parse_eager(pmu, dirfd);

	return pmu;
}

/* Creates the PMU when sysfs scanning fails. */
struct perf_pmu *perf_pmu__create_placeholder_core_pmu(struct list_head *core_pmus)
{
	struct perf_pmu *pmu = zalloc(sizeof(*pmu));

	if (!pmu)
		return NULL;

	pmu->name = strdup("cpu");
	if (!pmu->name) {
		free(pmu);
		return NULL;
	}

	pmu->is_core = true;
	pmu->type = PERF_TYPE_RAW;
	pmu->cpus = cpu_map__online();

	INIT_LIST_HEAD(&pmu->format);
	pmu->aliases = hashmap__new(aliases__hash, aliases__equal, /*ctx=*/ NULL);
	INIT_LIST_HEAD(&pmu->caps);
	list_add_tail(&pmu->list, core_pmus);
	return pmu;
}

bool perf_pmu__is_fake(const struct perf_pmu *pmu)
{
	return pmu->type == PERF_PMU_TYPE_FAKE;
}

void perf_pmu__warn_invalid_formats(struct perf_pmu *pmu)
{
	struct perf_pmu_format *format;

	if (pmu->formats_checked)
		return;

	pmu->formats_checked = true;

	/* fake pmu doesn't have format list */
	if (perf_pmu__is_fake(pmu))
		return;

	list_for_each_entry(format, &pmu->format, list) {
		perf_pmu_format__load(pmu, format);
		if (format->value >= PERF_PMU_FORMAT_VALUE_CONFIG_END) {
			pr_warning("WARNING: '%s' format '%s' requires 'perf_event_attr::config%d'"
				   "which is not supported by this version of perf!\n",
				   pmu->name, format->name, format->value);
			return;
		}
	}
}

bool evsel__is_aux_event(const struct evsel *evsel)
{
	struct perf_pmu *pmu;

	if (evsel->needs_auxtrace_mmap)
		return true;

	pmu = evsel__find_pmu(evsel);
	return pmu && pmu->auxtrace;
}

/*
 * Set @config_name to @val as long as the user hasn't already set or cleared it
 * by passing a config term on the command line.
 *
 * @val is the value to put into the bits specified by @config_name rather than
 * the bit pattern. It is shifted into position by this function, so to set
 * something to true, pass 1 for val rather than a pre shifted value.
 */
#define field_prep(_mask, _val) (((_val) << (ffsll(_mask) - 1)) & (_mask))
void evsel__set_config_if_unset(struct perf_pmu *pmu, struct evsel *evsel,
				const char *config_name, u64 val)
{
	u64 user_bits = 0, bits;
	struct evsel_config_term *term = evsel__get_config_term(evsel, CFG_CHG);

	if (term)
		user_bits = term->val.cfg_chg;

	bits = perf_pmu__format_bits(pmu, config_name);

	/* Do nothing if the user changed the value */
	if (bits & user_bits)
		return;

	/* Otherwise replace it */
	evsel->core.attr.config &= ~bits;
	evsel->core.attr.config |= field_prep(bits, val);
}

static struct perf_pmu_format *
pmu_find_format(const struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format;

	list_for_each_entry(format, formats, list)
		if (!strcmp(format->name, name))
			return format;

	return NULL;
}

__u64 perf_pmu__format_bits(struct perf_pmu *pmu, const char *name)
{
	struct perf_pmu_format *format = pmu_find_format(&pmu->format, name);
	__u64 bits = 0;
	int fbit;

	if (!format)
		return 0;

	for_each_set_bit(fbit, format->bits, PERF_PMU_FORMAT_BITS)
		bits |= 1ULL << fbit;

	return bits;
}

int perf_pmu__format_type(struct perf_pmu *pmu, const char *name)
{
	struct perf_pmu_format *format = pmu_find_format(&pmu->format, name);

	if (!format)
		return -1;

	perf_pmu_format__load(pmu, format);
	return format->value;
}

/*
 * Sets value based on the format definition (format parameter)
 * and unformatted value (value parameter).
 */
static void pmu_format_value(unsigned long *format, __u64 value, __u64 *v,
			     bool zero)
{
	unsigned long fbit, vbit;

	for (fbit = 0, vbit = 0; fbit < PERF_PMU_FORMAT_BITS; fbit++) {

		if (!test_bit(fbit, format))
			continue;

		if (value & (1llu << vbit++))
			*v |= (1llu << fbit);
		else if (zero)
			*v &= ~(1llu << fbit);
	}
}

static __u64 pmu_format_max_value(const unsigned long *format)
{
	int w;

	w = bitmap_weight(format, PERF_PMU_FORMAT_BITS);
	if (!w)
		return 0;
	if (w < 64)
		return (1ULL << w) - 1;
	return -1;
}

/*
 * Term is a string term, and might be a param-term. Try to look up it's value
 * in the remaining terms.
 * - We have a term like "base-or-format-term=param-term",
 * - We need to find the value supplied for "param-term" (with param-term named
 *   in a config string) later on in the term list.
 */
static int pmu_resolve_param_term(struct parse_events_term *term,
				  struct parse_events_terms *head_terms,
				  __u64 *value)
{
	struct parse_events_term *t;

	list_for_each_entry(t, &head_terms->terms, list) {
		if (t->type_val == PARSE_EVENTS__TERM_TYPE_NUM &&
		    t->config && !strcmp(t->config, term->config)) {
			t->used = true;
			*value = t->val.num;
			return 0;
		}
	}

	if (verbose > 0)
		printf("Required parameter '%s' not specified\n", term->config);

	return -1;
}

static char *pmu_formats_string(const struct list_head *formats)
{
	struct perf_pmu_format *format;
	char *str = NULL;
	struct strbuf buf = STRBUF_INIT;
	unsigned int i = 0;

	if (!formats)
		return NULL;

	/* sysfs exported terms */
	list_for_each_entry(format, formats, list)
		if (strbuf_addf(&buf, i++ ? ",%s" : "%s", format->name) < 0)
			goto error;

	str = strbuf_detach(&buf, NULL);
error:
	strbuf_release(&buf);

	return str;
}

/*
 * Setup one of config[12] attr members based on the
 * user input data - term parameter.
 */
static int pmu_config_term(const struct perf_pmu *pmu,
			   struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct parse_events_terms *head_terms,
			   bool zero, bool apply_hardcoded,
			   struct parse_events_error *err)
{
	struct perf_pmu_format *format;
	__u64 *vp;
	__u64 val, max_val;

	/*
	 * If this is a parameter we've already used for parameterized-eval,
	 * skip it in normal eval.
	 */
	if (term->used)
		return 0;

	/*
	 * Hardcoded terms are generally handled in event parsing, which
	 * traditionally have had to handle not having a PMU. An alias may
	 * have hard coded config values, optionally apply them below.
	 */
	if (parse_events__is_hardcoded_term(term)) {
		/* Config terms set all bits in the config. */
		DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);

		if (!apply_hardcoded)
			return 0;

		bitmap_fill(bits, PERF_PMU_FORMAT_BITS);

		switch (term->type_term) {
		case PARSE_EVENTS__TERM_TYPE_CONFIG:
			assert(term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
			pmu_format_value(bits, term->val.num, &attr->config, zero);
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG1:
			assert(term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
			pmu_format_value(bits, term->val.num, &attr->config1, zero);
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG2:
			assert(term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
			pmu_format_value(bits, term->val.num, &attr->config2, zero);
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG3:
			assert(term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
			pmu_format_value(bits, term->val.num, &attr->config3, zero);
			break;
		case PARSE_EVENTS__TERM_TYPE_USER: /* Not hardcoded. */
			return -EINVAL;
		case PARSE_EVENTS__TERM_TYPE_NAME ... PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
			/* Skip non-config terms. */
			break;
		default:
			break;
		}
		return 0;
	}

	format = pmu_find_format(&pmu->format, term->config);
	if (!format) {
		char *pmu_term = pmu_formats_string(&pmu->format);
		char *unknown_term;
		char *help_msg;

		if (asprintf(&unknown_term,
				"unknown term '%s' for pmu '%s'",
				term->config, pmu->name) < 0)
			unknown_term = NULL;
		help_msg = parse_events_formats_error_string(pmu_term);
		if (err) {
			parse_events_error__handle(err, term->err_term,
						   unknown_term,
						   help_msg);
		} else {
			pr_debug("%s (%s)\n", unknown_term, help_msg);
			free(unknown_term);
		}
		free(pmu_term);
		return -EINVAL;
	}
	perf_pmu_format__load(pmu, format);
	switch (format->value) {
	case PERF_PMU_FORMAT_VALUE_CONFIG:
		vp = &attr->config;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG1:
		vp = &attr->config1;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG2:
		vp = &attr->config2;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG3:
		vp = &attr->config3;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Either directly use a numeric term, or try to translate string terms
	 * using event parameters.
	 */
	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
		if (term->no_value &&
		    bitmap_weight(format->bits, PERF_PMU_FORMAT_BITS) > 1) {
			if (err) {
				parse_events_error__handle(err, term->err_val,
					   strdup("no value assigned for term"),
					   NULL);
			}
			return -EINVAL;
		}

		val = term->val.num;
	} else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
		if (strcmp(term->val.str, "?")) {
			if (verbose > 0) {
				pr_info("Invalid sysfs entry %s=%s\n",
						term->config, term->val.str);
			}
			if (err) {
				parse_events_error__handle(err, term->err_val,
					strdup("expected numeric value"),
					NULL);
			}
			return -EINVAL;
		}

		if (pmu_resolve_param_term(term, head_terms, &val))
			return -EINVAL;
	} else
		return -EINVAL;

	max_val = pmu_format_max_value(format->bits);
	if (val > max_val) {
		if (err) {
			char *err_str;

			if (asprintf(&err_str,
				     "value too big for format (%s), maximum is %llu",
				     format->name, (unsigned long long)max_val) < 0) {
				err_str = strdup("value too big for format");
			}
			parse_events_error__handle(err, term->err_val, err_str, /*help=*/NULL);
			return -EINVAL;
		}
		/*
		 * Assume we don't care if !err, in which case the value will be
		 * silently truncated.
		 */
	}

	pmu_format_value(format->bits, val, vp, zero);
	return 0;
}

int perf_pmu__config_terms(const struct perf_pmu *pmu,
			   struct perf_event_attr *attr,
			   struct parse_events_terms *terms,
			   bool zero, bool apply_hardcoded,
			   struct parse_events_error *err)
{
	struct parse_events_term *term;

	if (perf_pmu__is_hwmon(pmu))
		return hwmon_pmu__config_terms(pmu, attr, terms, err);
	if (perf_pmu__is_drm(pmu))
		return drm_pmu__config_terms(pmu, attr, terms, err);

	list_for_each_entry(term, &terms->terms, list) {
		if (pmu_config_term(pmu, attr, term, terms, zero, apply_hardcoded, err))
			return -EINVAL;
	}

	return 0;
}

/*
 * Configures event's 'attr' parameter based on the:
 * 1) users input - specified in terms parameter
 * 2) pmu format definitions - specified by pmu parameter
 */
int perf_pmu__config(struct perf_pmu *pmu, struct perf_event_attr *attr,
		     struct parse_events_terms *head_terms,
		     bool apply_hardcoded,
		     struct parse_events_error *err)
{
	bool zero = !!pmu->perf_event_attr_init_default;

	/* Fake PMU doesn't have proper terms so nothing to configure in attr. */
	if (perf_pmu__is_fake(pmu))
		return 0;

	return perf_pmu__config_terms(pmu, attr, head_terms, zero, apply_hardcoded, err);
}

static struct perf_pmu_alias *pmu_find_alias(struct perf_pmu *pmu,
					     struct parse_events_term *term)
{
	struct perf_pmu_alias *alias;
	const char *name;

	if (parse_events__is_hardcoded_term(term))
		return NULL;

	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
		if (!term->no_value)
			return NULL;
		if (pmu_find_format(&pmu->format, term->config))
			return NULL;
		name = term->config;

	} else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
		if (strcasecmp(term->config, "event"))
			return NULL;
		name = term->val.str;
	} else {
		return NULL;
	}

	alias = perf_pmu__find_alias(pmu, name, /*load=*/ true);
	if (alias || pmu->cpu_aliases_added)
		return alias;

	/* Alias doesn't exist, try to get it from the json events. */
	if (pmu->events_table &&
	    pmu_events_table__find_event(pmu->events_table, pmu, name,
				         pmu_add_cpu_aliases_map_callback,
				         pmu) == 0) {
		alias = perf_pmu__find_alias(pmu, name, /*load=*/ false);
	}
	return alias;
}


static int check_info_data(struct perf_pmu *pmu,
			   struct perf_pmu_alias *alias,
			   struct perf_pmu_info *info,
			   struct parse_events_error *err,
			   int column)
{
	read_alias_info(pmu, alias);
	/*
	 * Only one term in event definition can
	 * define unit, scale and snapshot, fail
	 * if there's more than one.
	 */
	if (info->unit && alias->unit[0]) {
		parse_events_error__handle(err, column,
					strdup("Attempt to set event's unit twice"),
					NULL);
		return -EINVAL;
	}
	if (info->scale && alias->scale) {
		parse_events_error__handle(err, column,
					strdup("Attempt to set event's scale twice"),
					NULL);
		return -EINVAL;
	}
	if (info->snapshot && alias->snapshot) {
		parse_events_error__handle(err, column,
					strdup("Attempt to set event snapshot twice"),
					NULL);
		return -EINVAL;
	}

	if (alias->unit[0])
		info->unit = alias->unit;

	if (alias->scale)
		info->scale = alias->scale;

	if (alias->snapshot)
		info->snapshot = alias->snapshot;

	return 0;
}

/*
 * Find alias in the terms list and replace it with the terms
 * defined for the alias
 */
int perf_pmu__check_alias(struct perf_pmu *pmu, struct parse_events_terms *head_terms,
			  struct perf_pmu_info *info, bool *rewrote_terms,
			  u64 *alternate_hw_config, struct parse_events_error *err)
{
	struct parse_events_term *term, *h;
	struct perf_pmu_alias *alias;
	int ret;

	*rewrote_terms = false;
	info->per_pkg = false;

	/*
	 * Mark unit and scale as not set
	 * (different from default values, see below)
	 */
	info->unit     = NULL;
	info->scale    = 0.0;
	info->snapshot = false;
	info->retirement_latency_mean = 0.0;
	info->retirement_latency_min = 0.0;
	info->retirement_latency_max = 0.0;

	if (perf_pmu__is_hwmon(pmu)) {
		ret = hwmon_pmu__check_alias(head_terms, info, err);
		goto out;
	}
	if (perf_pmu__is_drm(pmu)) {
		ret = drm_pmu__check_alias(pmu, head_terms, info, err);
		goto out;
	}

	/* Fake PMU doesn't rewrite terms. */
	if (perf_pmu__is_fake(pmu))
		goto out;

	list_for_each_entry_safe(term, h, &head_terms->terms, list) {
		alias = pmu_find_alias(pmu, term);
		if (!alias)
			continue;
		ret = pmu_alias_terms(alias, term->err_term, &term->list);
		if (ret) {
			parse_events_error__handle(err, term->err_term,
						strdup("Failure to duplicate terms"),
						NULL);
			return ret;
		}

		*rewrote_terms = true;
		ret = check_info_data(pmu, alias, info, err, term->err_term);
		if (ret)
			return ret;

		if (alias->per_pkg)
			info->per_pkg = true;

		if (term->alternate_hw_config)
			*alternate_hw_config = term->val.num;

		info->retirement_latency_mean = alias->retirement_latency_mean;
		info->retirement_latency_min = alias->retirement_latency_min;
		info->retirement_latency_max = alias->retirement_latency_max;

		list_del_init(&term->list);
		parse_events_term__delete(term);
	}
out:
	/*
	 * if no unit or scale found in aliases, then
	 * set defaults as for evsel
	 * unit cannot left to NULL
	 */
	if (info->unit == NULL)
		info->unit   = "";

	if (info->scale == 0.0)
		info->scale  = 1.0;

	return 0;
}

struct find_event_args {
	const char *event;
	void *state;
	pmu_event_callback cb;
};

static int find_event_callback(void *state, struct pmu_event_info *info)
{
	struct find_event_args *args = state;

	if (!strcmp(args->event, info->name))
		return args->cb(args->state, info);

	return 0;
}

int perf_pmu__find_event(struct perf_pmu *pmu, const char *event, void *state, pmu_event_callback cb)
{
	struct find_event_args args = {
		.event = event,
		.state = state,
		.cb = cb,
	};

	/* Sub-optimal, but function is only used by tests. */
	return perf_pmu__for_each_event(pmu, /*skip_duplicate_pmus=*/ false,
					&args, find_event_callback);
}

static void perf_pmu__del_formats(struct list_head *formats)
{
	struct perf_pmu_format *fmt, *tmp;

	list_for_each_entry_safe(fmt, tmp, formats, list) {
		list_del(&fmt->list);
		zfree(&fmt->name);
		free(fmt);
	}
}

bool perf_pmu__has_format(const struct perf_pmu *pmu, const char *name)
{
	struct perf_pmu_format *format;

	list_for_each_entry(format, &pmu->format, list) {
		if (!strcmp(format->name, name))
			return true;
	}
	return false;
}

int perf_pmu__for_each_format(struct perf_pmu *pmu, void *state, pmu_format_callback cb)
{
	static const char *const terms[] = {
		"config=0..0xffffffffffffffff",
		"config1=0..0xffffffffffffffff",
		"config2=0..0xffffffffffffffff",
		"config3=0..0xffffffffffffffff",
		"name=string",
		"period=number",
		"freq=number",
		"branch_type=(u|k|hv|any|...)",
		"time",
		"call-graph=(fp|dwarf|lbr)",
		"stack-size=number",
		"max-stack=number",
		"nr=number",
		"inherit",
		"no-inherit",
		"overwrite",
		"no-overwrite",
		"percore",
		"aux-output",
		"aux-action=(pause|resume|start-paused)",
		"aux-sample-size=number",
		"cpu=number",
		"ratio-to-prev=string",
	};
	struct perf_pmu_format *format;
	int ret;

	/*
	 * max-events and driver-config are missing above as are the internal
	 * types user, metric-id, raw, legacy cache and hardware. Assert against
	 * the enum parse_events__term_type so they are kept in sync.
	 */
	_Static_assert(ARRAY_SIZE(terms) == __PARSE_EVENTS__TERM_TYPE_NR - 6,
		       "perf_pmu__for_each_format()'s terms must be kept in sync with enum parse_events__term_type");
	list_for_each_entry(format, &pmu->format, list) {
		perf_pmu_format__load(pmu, format);
		ret = cb(state, format->name, (int)format->value, format->bits);
		if (ret)
			return ret;
	}
	if (!pmu->is_core)
		return 0;

	for (size_t i = 0; i < ARRAY_SIZE(terms); i++) {
		int config = PERF_PMU_FORMAT_VALUE_CONFIG;

		if (i < PERF_PMU_FORMAT_VALUE_CONFIG_END)
			config = i;

		ret = cb(state, terms[i], config, /*bits=*/NULL);
		if (ret)
			return ret;
	}
	return 0;
}

bool is_pmu_core(const char *name)
{
	return !strcmp(name, "cpu") || !strcmp(name, "cpum_cf") || is_sysfs_pmu_core(name);
}

bool perf_pmu__supports_legacy_cache(const struct perf_pmu *pmu)
{
	return pmu->is_core;
}

bool perf_pmu__auto_merge_stats(const struct perf_pmu *pmu)
{
	return !pmu->is_core || perf_pmus__num_core_pmus() == 1;
}

bool perf_pmu__have_event(struct perf_pmu *pmu, const char *name)
{
	if (!name)
		return false;
	if (perf_pmu__is_tool(pmu) && tool_pmu__skip_event(name))
		return false;
	if (perf_pmu__is_tracepoint(pmu))
		return tp_pmu__have_event(pmu, name);
	if (perf_pmu__is_hwmon(pmu))
		return hwmon_pmu__have_event(pmu, name);
	if (perf_pmu__is_drm(pmu))
		return drm_pmu__have_event(pmu, name);
	if (perf_pmu__find_alias(pmu, name, /*load=*/ true) != NULL)
		return true;
	if (pmu->cpu_aliases_added || !pmu->events_table)
		return false;
	return pmu_events_table__find_event(pmu->events_table, pmu, name, NULL, NULL) == 0;
}

size_t perf_pmu__num_events(struct perf_pmu *pmu)
{
	size_t nr;

	if (perf_pmu__is_tracepoint(pmu))
		return tp_pmu__num_events(pmu);
	if (perf_pmu__is_hwmon(pmu))
		return hwmon_pmu__num_events(pmu);
	if (perf_pmu__is_drm(pmu))
		return drm_pmu__num_events(pmu);

	pmu_aliases_parse(pmu);
	nr = pmu->sysfs_aliases + pmu->sys_json_aliases;

	if (pmu->cpu_aliases_added)
		 nr += pmu->cpu_json_aliases;
	else if (pmu->events_table)
		nr += pmu_events_table__num_events(pmu->events_table, pmu) -
			pmu->cpu_common_json_aliases;
	else
		assert(pmu->cpu_json_aliases == 0 && pmu->cpu_common_json_aliases == 0);

	if (perf_pmu__is_tool(pmu))
		nr -= tool_pmu__num_skip_events();

	return pmu->selectable ? nr + 1 : nr;
}

static int sub_non_neg(int a, int b)
{
	if (b > a)
		return 0;
	return a - b;
}

static char *format_alias(char *buf, int len, const struct perf_pmu *pmu,
			  const struct perf_pmu_alias *alias, bool skip_duplicate_pmus)
{
	struct parse_events_term *term;
	size_t pmu_name_len = pmu_deduped_name_len(pmu, pmu->name,
						   skip_duplicate_pmus);
	int used = snprintf(buf, len, "%.*s/%s", (int)pmu_name_len, pmu->name, alias->name);

	list_for_each_entry(term, &alias->terms.terms, list) {
		if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR)
			used += snprintf(buf + used, sub_non_neg(len, used),
					",%s=%s", term->config,
					term->val.str);
	}

	if (sub_non_neg(len, used) > 0) {
		buf[used] = '/';
		used++;
	}
	if (sub_non_neg(len, used) > 0) {
		buf[used] = '\0';
		used++;
	} else
		buf[len - 1] = '\0';

	return buf;
}

int perf_pmu__for_each_event(struct perf_pmu *pmu, bool skip_duplicate_pmus,
			     void *state, pmu_event_callback cb)
{
	char buf[1024];
	struct pmu_event_info info = {
		.pmu = pmu,
		.event_type_desc = "Kernel PMU event",
	};
	int ret = 0;
	struct strbuf sb;
	struct hashmap_entry *entry;
	size_t bkt;

	if (perf_pmu__is_tracepoint(pmu))
		return tp_pmu__for_each_event(pmu, state, cb);
	if (perf_pmu__is_hwmon(pmu))
		return hwmon_pmu__for_each_event(pmu, state, cb);
	if (perf_pmu__is_drm(pmu))
		return drm_pmu__for_each_event(pmu, state, cb);

	strbuf_init(&sb, /*hint=*/ 0);
	pmu_aliases_parse(pmu);
	pmu_add_cpu_aliases(pmu);
	hashmap__for_each_entry(pmu->aliases, entry, bkt) {
		struct perf_pmu_alias *event = entry->pvalue;
		size_t buf_used, pmu_name_len;

		if (perf_pmu__is_tool(pmu) && tool_pmu__skip_event(event->name))
			continue;

		info.pmu_name = event->pmu_name ?: pmu->name;
		pmu_name_len = pmu_deduped_name_len(pmu, info.pmu_name,
						    skip_duplicate_pmus);
		info.alias = NULL;
		if (event->desc) {
			info.name = event->name;
			buf_used = 0;
		} else {
			info.name = format_alias(buf, sizeof(buf), pmu, event,
						 skip_duplicate_pmus);
			if (pmu->is_core) {
				info.alias = info.name;
				info.name = event->name;
			}
			buf_used = strlen(buf) + 1;
		}
		info.scale_unit = NULL;
		if (strlen(event->unit) || event->scale != 1.0) {
			info.scale_unit = buf + buf_used;
			buf_used += snprintf(buf + buf_used, sizeof(buf) - buf_used,
					"%G%s", event->scale, event->unit) + 1;
		}
		info.desc = event->desc;
		info.long_desc = event->long_desc;
		info.encoding_desc = buf + buf_used;
		parse_events_terms__to_strbuf(&event->terms, &sb);
		buf_used += snprintf(buf + buf_used, sizeof(buf) - buf_used,
				"%.*s/%s/", (int)pmu_name_len, info.pmu_name, sb.buf) + 1;
		info.topic = event->topic;
		info.str = sb.buf;
		info.deprecated = event->deprecated;
		ret = cb(state, &info);
		if (ret)
			goto out;
		strbuf_setlen(&sb, /*len=*/ 0);
	}
	if (pmu->selectable) {
		info.name = buf;
		snprintf(buf, sizeof(buf), "%s//", pmu->name);
		info.alias = NULL;
		info.scale_unit = NULL;
		info.desc = NULL;
		info.long_desc = NULL;
		info.encoding_desc = NULL;
		info.topic = NULL;
		info.pmu_name = pmu->name;
		info.deprecated = false;
		ret = cb(state, &info);
	}
out:
	strbuf_release(&sb);
	return ret;
}

static bool perf_pmu___name_match(const struct perf_pmu *pmu, const char *to_match, bool wildcard)
{
	const char *names[2] = {
		pmu->name,
		pmu->alias_name,
	};
	if (pmu->is_core) {
		for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
			const char *name = names[i];

			if (!name)
				continue;

			if (!strcmp(name, to_match)) {
				/* Exact name match. */
				return true;
			}
		}
		if (!strcmp(to_match, "default_core")) {
			/*
			 * jevents and tests use default_core as a marker for any core
			 * PMU as the PMU name varies across architectures.
			 */
			return true;
		}
		return false;
	}
	if (!pmu->is_uncore) {
		/*
		 * PMU isn't core or uncore, some kind of broken CPU mask
		 * situation. Only match exact name.
		 */
		for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
			const char *name = names[i];

			if (!name)
				continue;

			if (!strcmp(name, to_match)) {
				/* Exact name match. */
				return true;
			}
		}
		return false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const char *name = names[i];

		if (!name)
			continue;

		if (wildcard && perf_pmu__match_wildcard_uncore(name, to_match))
			return true;
		if (!wildcard && perf_pmu__match_ignoring_suffix_uncore(name, to_match))
			return true;
	}
	return false;
}

/**
 * perf_pmu__name_wildcard_match - Called by the jevents generated code to see
 *                                 if pmu matches the json to_match string.
 * @pmu: The pmu whose name/alias to match.
 * @to_match: The possible match to pmu_name.
 */
bool perf_pmu__name_wildcard_match(const struct perf_pmu *pmu, const char *to_match)
{
	return perf_pmu___name_match(pmu, to_match, /*wildcard=*/true);
}

/**
 * perf_pmu__name_no_suffix_match - Does pmu's name match to_match ignoring any
 *                                  trailing suffix on the pmu_name and/or tok?
 * @pmu: The pmu whose name/alias to match.
 * @to_match: The possible match to pmu_name.
 */
bool perf_pmu__name_no_suffix_match(const struct perf_pmu *pmu, const char *to_match)
{
	return perf_pmu___name_match(pmu, to_match, /*wildcard=*/false);
}

bool perf_pmu__is_software(const struct perf_pmu *pmu)
{
	const char *known_sw_pmus[] = {
		"kprobe",
		"msr",
		"uprobe",
	};

	if (pmu->is_core || pmu->is_uncore || pmu->auxtrace)
		return false;
	switch (pmu->type) {
	case PERF_TYPE_HARDWARE:	return false;
	case PERF_TYPE_SOFTWARE:	return true;
	case PERF_TYPE_TRACEPOINT:	return true;
	case PERF_TYPE_HW_CACHE:	return false;
	case PERF_TYPE_RAW:		return false;
	case PERF_TYPE_BREAKPOINT:	return true;
	case PERF_PMU_TYPE_TOOL:	return true;
	default: break;
	}
	for (size_t i = 0; i < ARRAY_SIZE(known_sw_pmus); i++) {
		if (!strcmp(pmu->name, known_sw_pmus[i]))
			return true;
	}
	return false;
}

FILE *perf_pmu__open_file(const struct perf_pmu *pmu, const char *name)
{
	char path[PATH_MAX];

	if (!perf_pmu__pathname_scnprintf(path, sizeof(path), pmu->name, name) ||
	    !file_available(path))
		return NULL;

	return fopen(path, "r");
}

FILE *perf_pmu__open_file_at(const struct perf_pmu *pmu, int dirfd, const char *name)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, pmu->name, name, O_RDONLY);
	if (fd < 0)
		return NULL;

	return fdopen(fd, "r");
}

int perf_pmu__scan_file(const struct perf_pmu *pmu, const char *name, const char *fmt,
			...)
{
	va_list args;
	FILE *file;
	int ret = EOF;

	va_start(args, fmt);
	file = perf_pmu__open_file(pmu, name);
	if (file) {
		ret = vfscanf(file, fmt, args);
		fclose(file);
	}
	va_end(args);
	return ret;
}

int perf_pmu__scan_file_at(const struct perf_pmu *pmu, int dirfd, const char *name,
			   const char *fmt, ...)
{
	va_list args;
	FILE *file;
	int ret = EOF;

	va_start(args, fmt);
	file = perf_pmu__open_file_at(pmu, dirfd, name);
	if (file) {
		ret = vfscanf(file, fmt, args);
		fclose(file);
	}
	va_end(args);
	return ret;
}

bool perf_pmu__file_exists(const struct perf_pmu *pmu, const char *name)
{
	char path[PATH_MAX];

	if (!perf_pmu__pathname_scnprintf(path, sizeof(path), pmu->name, name))
		return false;

	return file_available(path);
}

static int perf_pmu__new_caps(struct list_head *list, char *name, char *value)
{
	struct perf_pmu_caps *caps = zalloc(sizeof(*caps));

	if (!caps)
		return -ENOMEM;

	caps->name = strdup(name);
	if (!caps->name)
		goto free_caps;
	caps->value = strndup(value, strlen(value) - 1);
	if (!caps->value)
		goto free_name;
	list_add_tail(&caps->list, list);
	return 0;

free_name:
	zfree(&caps->name);
free_caps:
	free(caps);

	return -ENOMEM;
}

static void perf_pmu__del_caps(struct perf_pmu *pmu)
{
	struct perf_pmu_caps *caps, *tmp;

	list_for_each_entry_safe(caps, tmp, &pmu->caps, list) {
		list_del(&caps->list);
		zfree(&caps->name);
		zfree(&caps->value);
		free(caps);
	}
}

struct perf_pmu_caps *perf_pmu__get_cap(struct perf_pmu *pmu, const char *name)
{
	struct perf_pmu_caps *caps;

	list_for_each_entry(caps, &pmu->caps, list) {
		if (!strcmp(caps->name, name))
			return caps;
	}
	return NULL;
}

/*
 * Reading/parsing the given pmu capabilities, which should be located at:
 * /sys/bus/event_source/devices/<dev>/caps as sysfs group attributes.
 * Return the number of capabilities
 */
int perf_pmu__caps_parse(struct perf_pmu *pmu)
{
	char caps_path[PATH_MAX];
	struct io_dir caps_dir;
	struct io_dirent64 *evt_ent;
	int caps_fd;

	if (pmu->caps_initialized)
		return pmu->nr_caps;

	pmu->nr_caps = 0;

	if (!perf_pmu__pathname_scnprintf(caps_path, sizeof(caps_path), pmu->name, "caps"))
		return -1;

	caps_fd = open(caps_path, O_CLOEXEC | O_DIRECTORY | O_RDONLY);
	if (caps_fd == -1) {
		pmu->caps_initialized = true;
		return 0;	/* no error if caps does not exist */
	}

	io_dir__init(&caps_dir, caps_fd);

	while ((evt_ent = io_dir__readdir(&caps_dir)) != NULL) {
		char *name = evt_ent->d_name;
		char value[128];
		FILE *file;
		int fd;

		if (io_dir__is_dir(&caps_dir, evt_ent))
			continue;

		fd = openat(caps_fd, name, O_RDONLY);
		if (fd == -1)
			continue;
		file = fdopen(fd, "r");
		if (!file) {
			close(fd);
			continue;
		}

		if (!fgets(value, sizeof(value), file) ||
		    (perf_pmu__new_caps(&pmu->caps, name, value) < 0)) {
			fclose(file);
			continue;
		}

		pmu->nr_caps++;
		fclose(file);
	}

	close(caps_fd);

	pmu->caps_initialized = true;
	return pmu->nr_caps;
}

static void perf_pmu__compute_config_masks(struct perf_pmu *pmu)
{
	struct perf_pmu_format *format;

	if (pmu->config_masks_computed)
		return;

	list_for_each_entry(format, &pmu->format, list)	{
		unsigned int i;
		__u64 *mask;

		if (format->value >= PERF_PMU_FORMAT_VALUE_CONFIG_END)
			continue;

		pmu->config_masks_present = true;
		mask = &pmu->config_masks[format->value];

		for_each_set_bit(i, format->bits, PERF_PMU_FORMAT_BITS)
			*mask |= 1ULL << i;
	}
	pmu->config_masks_computed = true;
}

void perf_pmu__warn_invalid_config(struct perf_pmu *pmu, __u64 config,
				   const char *name, int config_num,
				   const char *config_name)
{
	__u64 bits;
	char buf[100];

	perf_pmu__compute_config_masks(pmu);

	/*
	 * Kernel doesn't export any valid format bits.
	 */
	if (!pmu->config_masks_present)
		return;

	bits = config & ~pmu->config_masks[config_num];
	if (bits == 0)
		return;

	bitmap_scnprintf((unsigned long *)&bits, sizeof(bits) * 8, buf, sizeof(buf));

	pr_warning("WARNING: event '%s' not valid (bits %s of %s "
		   "'%llx' not supported by kernel)!\n",
		   name ?: "N/A", buf, config_name, config);
}

bool perf_pmu__wildcard_match(const struct perf_pmu *pmu, const char *wildcard_to_match)
{
	const char *names[2] = {
		pmu->name,
		pmu->alias_name,
	};
	bool need_fnmatch = strisglob(wildcard_to_match);

	if (!strncmp(wildcard_to_match, "uncore_", 7))
		wildcard_to_match += 7;

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const char *pmu_name = names[i];

		if (!pmu_name)
			continue;

		if (!strncmp(pmu_name, "uncore_", 7))
			pmu_name += 7;

		if (perf_pmu__match_wildcard(pmu_name, wildcard_to_match) ||
		    (need_fnmatch && !fnmatch(wildcard_to_match, pmu_name, 0)))
			return true;
	}
	return false;
}

int perf_pmu__event_source_devices_scnprintf(char *pathname, size_t size)
{
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return 0;
	return scnprintf(pathname, size, "%s/bus/event_source/devices/", sysfs);
}

int perf_pmu__event_source_devices_fd(void)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	scnprintf(path, sizeof(path), "%s/bus/event_source/devices/", sysfs);
	return open(path, O_DIRECTORY);
}

/*
 * Fill 'buf' with the path to a file or folder in 'pmu_name' in
 * sysfs. For example if pmu_name = "cs_etm" and 'filename' = "format"
 * then pathname will be filled with
 * "/sys/bus/event_source/devices/cs_etm/format"
 *
 * Return 0 if the sysfs mountpoint couldn't be found, if no characters were
 * written or if the buffer size is exceeded.
 */
int perf_pmu__pathname_scnprintf(char *buf, size_t size,
				 const char *pmu_name, const char *filename)
{
	size_t len;

	len = perf_pmu__event_source_devices_scnprintf(buf, size);
	if (!len || (len + strlen(pmu_name) + strlen(filename) + 1)  >= size)
		return 0;

	return scnprintf(buf + len, size - len, "%s/%s", pmu_name, filename);
}

int perf_pmu__pathname_fd(int dirfd, const char *pmu_name, const char *filename, int flags)
{
	char path[PATH_MAX];

	scnprintf(path, sizeof(path), "%s/%s", pmu_name, filename);
	return openat(dirfd, path, flags);
}

void perf_pmu__delete(struct perf_pmu *pmu)
{
	if (!pmu)
		return;

	if (perf_pmu__is_hwmon(pmu))
		hwmon_pmu__exit(pmu);
	else if (perf_pmu__is_drm(pmu))
		drm_pmu__exit(pmu);

	perf_pmu__del_formats(&pmu->format);
	perf_pmu__del_aliases(pmu);
	perf_pmu__del_caps(pmu);

	perf_cpu_map__put(pmu->cpus);

	zfree(&pmu->name);
	zfree(&pmu->alias_name);
	zfree(&pmu->id);
	free(pmu);
}

const char *perf_pmu__name_from_config(struct perf_pmu *pmu, u64 config)
{
	struct hashmap_entry *entry;
	size_t bkt;

	if (!pmu)
		return NULL;

	pmu_aliases_parse(pmu);
	pmu_add_cpu_aliases(pmu);
	hashmap__for_each_entry(pmu->aliases, entry, bkt) {
		struct perf_pmu_alias *event = entry->pvalue;
		struct perf_event_attr attr = {.config = 0,};

		int ret = perf_pmu__config(pmu, &attr, &event->terms, /*apply_hardcoded=*/true,
					   /*err=*/NULL);

		if (ret == 0 && config == attr.config)
			return event->name;
	}
	return NULL;
}
