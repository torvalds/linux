// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <linux/ctype.h>
#include <subcmd/pager.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dirent.h>
#include <api/fs/fs.h>
#include <locale.h>
#include <regex.h>
#include <perf/cpumap.h>
#include <fnmatch.h>
#include "debug.h"
#include "evsel.h"
#include "pmu.h"
#include "parse-events.h"
#include "header.h"
#include "string2.h"
#include "strbuf.h"
#include "fncache.h"
#include "pmu-hybrid.h"

struct perf_pmu perf_pmu__fake;

struct perf_pmu_format {
	char *name;
	int value;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	struct list_head list;
};

int perf_pmu_parse(struct list_head *list, char *name);
extern FILE *perf_pmu_in;

static LIST_HEAD(pmus);
static bool hybrid_scanned;

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
int perf_pmu__format_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *format_dir;
	int ret = 0;

	format_dir = opendir(dir);
	if (!format_dir)
		return -EINVAL;

	while (!ret && (evt_ent = readdir(format_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", dir, name);

		ret = -EINVAL;
		file = fopen(path, "r");
		if (!file)
			break;

		perf_pmu_in = file;
		ret = perf_pmu_parse(head, name);
		fclose(file);
	}

	closedir(format_dir);
	return ret;
}

/*
 * Reading/parsing the default pmu format definition, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/format as sysfs group attributes.
 */
static int pmu_format(const char *name, struct list_head *format)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/format", sysfs, name);

	if (!file_available(path))
		return 0;

	if (perf_pmu__format_parse(path, format))
		return -1;

	return 0;
}

int perf_pmu__convert_scale(const char *scale, char **end, double *sval)
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

static int perf_pmu__parse_scale(struct perf_pmu_alias *alias, char *dir, char *name)
{
	struct stat st;
	ssize_t sret;
	char scale[128];
	int fd, ret = -1;
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/%s.scale", dir, name);

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

static int perf_pmu__parse_unit(struct perf_pmu_alias *alias, char *dir, char *name)
{
	char path[PATH_MAX];
	ssize_t sret;
	int fd;

	scnprintf(path, PATH_MAX, "%s/%s.unit", dir, name);

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

static int
perf_pmu__parse_per_pkg(struct perf_pmu_alias *alias, char *dir, char *name)
{
	char path[PATH_MAX];
	int fd;

	scnprintf(path, PATH_MAX, "%s/%s.per-pkg", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	close(fd);

	alias->per_pkg = true;
	return 0;
}

static int perf_pmu__parse_snapshot(struct perf_pmu_alias *alias,
				    char *dir, char *name)
{
	char path[PATH_MAX];
	int fd;

	scnprintf(path, PATH_MAX, "%s/%s.snapshot", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	alias->snapshot = true;
	close(fd);
	return 0;
}

static void perf_pmu_assign_str(char *name, const char *field, char **old_str,
				char **new_str)
{
	if (!*old_str)
		goto set_new;

	if (*new_str) {	/* Have new string, check with old */
		if (strcasecmp(*old_str, *new_str))
			pr_debug("alias %s differs in field '%s'\n",
				 name, field);
		zfree(old_str);
	} else		/* Nothing new --> keep old string */
		return;
set_new:
	*old_str = *new_str;
	*new_str = NULL;
}

static void perf_pmu_update_alias(struct perf_pmu_alias *old,
				  struct perf_pmu_alias *newalias)
{
	perf_pmu_assign_str(old->name, "desc", &old->desc, &newalias->desc);
	perf_pmu_assign_str(old->name, "long_desc", &old->long_desc,
			    &newalias->long_desc);
	perf_pmu_assign_str(old->name, "topic", &old->topic, &newalias->topic);
	perf_pmu_assign_str(old->name, "metric_expr", &old->metric_expr,
			    &newalias->metric_expr);
	perf_pmu_assign_str(old->name, "metric_name", &old->metric_name,
			    &newalias->metric_name);
	perf_pmu_assign_str(old->name, "value", &old->str, &newalias->str);
	old->scale = newalias->scale;
	old->per_pkg = newalias->per_pkg;
	old->snapshot = newalias->snapshot;
	memcpy(old->unit, newalias->unit, sizeof(old->unit));
}

/* Delete an alias entry. */
void perf_pmu_free_alias(struct perf_pmu_alias *newalias)
{
	zfree(&newalias->name);
	zfree(&newalias->desc);
	zfree(&newalias->long_desc);
	zfree(&newalias->topic);
	zfree(&newalias->str);
	zfree(&newalias->metric_expr);
	zfree(&newalias->metric_name);
	zfree(&newalias->pmu_name);
	parse_events_terms__purge(&newalias->terms);
	free(newalias);
}

/* Merge an alias, search in alias list. If this name is already
 * present merge both of them to combine all information.
 */
static bool perf_pmu_merge_alias(struct perf_pmu_alias *newalias,
				 struct list_head *alist)
{
	struct perf_pmu_alias *a;

	list_for_each_entry(a, alist, list) {
		if (!strcasecmp(newalias->name, a->name)) {
			if (newalias->pmu_name && a->pmu_name &&
			    !strcasecmp(newalias->pmu_name, a->pmu_name)) {
				continue;
			}
			perf_pmu_update_alias(a, newalias);
			perf_pmu_free_alias(newalias);
			return true;
		}
	}
	return false;
}

static int __perf_pmu__new_alias(struct list_head *list, char *dir, char *name,
				 char *desc, char *val, const struct pmu_event *pe)
{
	struct parse_events_term *term;
	struct perf_pmu_alias *alias;
	int ret;
	int num;
	char newval[256];
	char *long_desc = NULL, *topic = NULL, *unit = NULL, *perpkg = NULL,
	     *metric_expr = NULL, *metric_name = NULL, *deprecated = NULL,
	     *pmu_name = NULL;

	if (pe) {
		long_desc = (char *)pe->long_desc;
		topic = (char *)pe->topic;
		unit = (char *)pe->unit;
		perpkg = (char *)pe->perpkg;
		metric_expr = (char *)pe->metric_expr;
		metric_name = (char *)pe->metric_name;
		deprecated = (char *)pe->deprecated;
		pmu_name = (char *)pe->pmu;
	}

	alias = malloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;

	INIT_LIST_HEAD(&alias->terms);
	alias->scale = 1.0;
	alias->unit[0] = '\0';
	alias->per_pkg = false;
	alias->snapshot = false;
	alias->deprecated = false;

	ret = parse_events_terms(&alias->terms, val);
	if (ret) {
		pr_err("Cannot parse alias %s: %d\n", val, ret);
		free(alias);
		return ret;
	}

	/* Scan event and remove leading zeroes, spaces, newlines, some
	 * platforms have terms specified as
	 * event=0x0091 (read from files ../<PMU>/events/<FILE>
	 * and terms specified as event=0x91 (read from JSON files).
	 *
	 * Rebuild string to make alias->str member comparable.
	 */
	memset(newval, 0, sizeof(newval));
	ret = 0;
	list_for_each_entry(term, &alias->terms, list) {
		if (ret)
			ret += scnprintf(newval + ret, sizeof(newval) - ret,
					 ",");
		if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
			ret += scnprintf(newval + ret, sizeof(newval) - ret,
					 "%s=%#x", term->config, term->val.num);
		else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR)
			ret += scnprintf(newval + ret, sizeof(newval) - ret,
					 "%s=%s", term->config, term->val.str);
	}

	alias->name = strdup(name);
	if (dir) {
		/*
		 * load unit name and scale if available
		 */
		perf_pmu__parse_unit(alias, dir, name);
		perf_pmu__parse_scale(alias, dir, name);
		perf_pmu__parse_per_pkg(alias, dir, name);
		perf_pmu__parse_snapshot(alias, dir, name);
	}

	alias->metric_expr = metric_expr ? strdup(metric_expr) : NULL;
	alias->metric_name = metric_name ? strdup(metric_name): NULL;
	alias->desc = desc ? strdup(desc) : NULL;
	alias->long_desc = long_desc ? strdup(long_desc) :
				desc ? strdup(desc) : NULL;
	alias->topic = topic ? strdup(topic) : NULL;
	if (unit) {
		if (perf_pmu__convert_scale(unit, &unit, &alias->scale) < 0)
			return -1;
		snprintf(alias->unit, sizeof(alias->unit), "%s", unit);
	}
	alias->per_pkg = perpkg && sscanf(perpkg, "%d", &num) == 1 && num == 1;
	alias->str = strdup(newval);
	alias->pmu_name = pmu_name ? strdup(pmu_name) : NULL;

	if (deprecated)
		alias->deprecated = true;

	if (!perf_pmu_merge_alias(alias, list))
		list_add_tail(&alias->list, list);

	return 0;
}

static int perf_pmu__new_alias(struct list_head *list, char *dir, char *name, FILE *file)
{
	char buf[256];
	int ret;

	ret = fread(buf, 1, sizeof(buf), file);
	if (ret == 0)
		return -EINVAL;

	buf[ret] = 0;

	/* Remove trailing newline from sysfs file */
	strim(buf);

	return __perf_pmu__new_alias(list, dir, name, NULL, buf, NULL);
}

static inline bool pmu_alias_info_file(char *name)
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
 * Process all the sysfs attributes located under the directory
 * specified in 'dir' parameter.
 */
static int pmu_aliases_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *event_dir;

	event_dir = opendir(dir);
	if (!event_dir)
		return -EINVAL;

	while ((evt_ent = readdir(event_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/*
		 * skip info files parsed in perf_pmu__new_alias()
		 */
		if (pmu_alias_info_file(name))
			continue;

		scnprintf(path, PATH_MAX, "%s/%s", dir, name);

		file = fopen(path, "r");
		if (!file) {
			pr_debug("Cannot open %s\n", path);
			continue;
		}

		if (perf_pmu__new_alias(head, dir, name, file) < 0)
			pr_debug("Cannot set up %s\n", name);
		fclose(file);
	}

	closedir(event_dir);
	return 0;
}

/*
 * Reading the pmu event aliases definition, which should be located at:
 * /sys/bus/event_source/devices/<dev>/events as sysfs group attributes.
 */
static int pmu_aliases(const char *name, struct list_head *head)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/events", sysfs, name);

	if (!file_available(path))
		return 0;

	if (pmu_aliases_parse(path, head))
		return -1;

	return 0;
}

static int pmu_alias_terms(struct perf_pmu_alias *alias,
			   struct list_head *terms)
{
	struct parse_events_term *term, *cloned;
	LIST_HEAD(list);
	int ret;

	list_for_each_entry(term, &alias->terms, list) {
		ret = parse_events_term__clone(&cloned, term);
		if (ret) {
			parse_events_terms__purge(&list);
			return ret;
		}
		/*
		 * Weak terms don't override command line options,
		 * which we don't want for implicit terms in aliases.
		 */
		cloned->weak = true;
		list_add_tail(&cloned->list, &list);
	}
	list_splice(&list, terms);
	return 0;
}

/*
 * Reading/parsing the default pmu type value, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/type as sysfs attribute.
 */
static int pmu_type(const char *name, __u32 *type)
{
	char path[PATH_MAX];
	FILE *file;
	int ret = 0;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/type", sysfs, name);

	if (access(path, R_OK) < 0)
		return -1;

	file = fopen(path, "r");
	if (!file)
		return -EINVAL;

	if (1 != fscanf(file, "%u", type))
		ret = -1;

	fclose(file);
	return ret;
}

/* Add all pmus in sysfs to pmu list: */
static void pmu_read_sysfs(void)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;
		/* add to static LIST_HEAD(pmus): */
		perf_pmu__find(dent->d_name);
	}

	closedir(dir);
}

static struct perf_cpu_map *__pmu_cpumask(const char *path)
{
	FILE *file;
	struct perf_cpu_map *cpus;

	file = fopen(path, "r");
	if (!file)
		return NULL;

	cpus = perf_cpu_map__read(file);
	fclose(file);
	return cpus;
}

/*
 * Uncore PMUs have a "cpumask" file under sysfs. CPU PMUs (e.g. on arm/arm64)
 * may have a "cpus" file.
 */
#define SYS_TEMPLATE_ID	"./bus/event_source/devices/%s/identifier"
#define CPUS_TEMPLATE_UNCORE	"%s/bus/event_source/devices/%s/cpumask"

static struct perf_cpu_map *pmu_cpumask(const char *name)
{
	char path[PATH_MAX];
	struct perf_cpu_map *cpus;
	const char *sysfs = sysfs__mountpoint();
	const char *templates[] = {
		CPUS_TEMPLATE_UNCORE,
		CPUS_TEMPLATE_CPU,
		NULL
	};
	const char **template;

	if (!sysfs)
		return NULL;

	for (template = templates; *template; template++) {
		snprintf(path, PATH_MAX, *template, sysfs, name);
		cpus = __pmu_cpumask(path);
		if (cpus)
			return cpus;
	}

	return NULL;
}

static bool pmu_is_uncore(const char *name)
{
	char path[PATH_MAX];
	const char *sysfs;

	if (perf_pmu__hybrid_mounted(name))
		return false;

	sysfs = sysfs__mountpoint();
	snprintf(path, PATH_MAX, CPUS_TEMPLATE_UNCORE, sysfs, name);
	return file_available(path);
}

static char *pmu_id(const char *name)
{
	char path[PATH_MAX], *str;
	size_t len;

	snprintf(path, PATH_MAX, SYS_TEMPLATE_ID, name);

	if (sysfs__read_str(path, &str, &len) < 0)
		return NULL;

	str[len - 1] = 0; /* remove line feed */

	return str;
}

/*
 *  PMU CORE devices have different name other than cpu in sysfs on some
 *  platforms.
 *  Looking for possible sysfs files to identify the arm core device.
 */
static int is_arm_pmu_core(const char *name)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return 0;

	/* Look for cpu sysfs (specific to arm) */
	scnprintf(path, PATH_MAX, "%s/bus/event_source/devices/%s/cpus",
				sysfs, name);
	return file_available(path);
}

char *perf_pmu__getcpuid(struct perf_pmu *pmu)
{
	char *cpuid;
	static bool printed;

	cpuid = getenv("PERF_CPUID");
	if (cpuid)
		cpuid = strdup(cpuid);
	if (!cpuid)
		cpuid = get_cpuid_str(pmu);
	if (!cpuid)
		return NULL;

	if (!printed) {
		pr_debug("Using CPUID %s\n", cpuid);
		printed = true;
	}
	return cpuid;
}

__weak const struct pmu_events_table *pmu_events_table__find(void)
{
	return perf_pmu__find_table(NULL);
}

/*
 * Suffix must be in form tok_{digits}, or tok{digits}, or same as pmu_name
 * to be valid.
 */
static bool perf_pmu__valid_suffix(const char *pmu_name, char *tok)
{
	const char *p;

	if (strncmp(pmu_name, tok, strlen(tok)))
		return false;

	p = pmu_name + strlen(tok);
	if (*p == 0)
		return true;

	if (*p == '_')
		++p;

	/* Ensure we end in a number */
	while (1) {
		if (!isdigit(*p))
			return false;
		if (*(++p) == 0)
			break;
	}

	return true;
}

bool pmu_uncore_alias_match(const char *pmu_name, const char *name)
{
	char *tmp = NULL, *tok, *str;
	bool res;

	str = strdup(pmu_name);
	if (!str)
		return false;

	/*
	 * uncore alias may be from different PMU with common prefix
	 */
	tok = strtok_r(str, ",", &tmp);
	if (strncmp(pmu_name, tok, strlen(tok))) {
		res = false;
		goto out;
	}

	/*
	 * Match more complex aliases where the alias name is a comma-delimited
	 * list of tokens, orderly contained in the matching PMU name.
	 *
	 * Example: For alias "socket,pmuname" and PMU "socketX_pmunameY", we
	 *	    match "socket" in "socketX_pmunameY" and then "pmuname" in
	 *	    "pmunameY".
	 */
	while (1) {
		char *next_tok = strtok_r(NULL, ",", &tmp);

		name = strstr(name, tok);
		if (!name ||
		    (!next_tok && !perf_pmu__valid_suffix(name, tok))) {
			res = false;
			goto out;
		}
		if (!next_tok)
			break;
		tok = next_tok;
		name += strlen(tok);
	}

	res = true;
out:
	free(str);
	return res;
}

struct pmu_add_cpu_aliases_map_data {
	struct list_head *head;
	const char *name;
	const char *cpu_name;
	struct perf_pmu *pmu;
};

static int pmu_add_cpu_aliases_map_callback(const struct pmu_event *pe,
					const struct pmu_events_table *table __maybe_unused,
					void *vdata)
{
	struct pmu_add_cpu_aliases_map_data *data = vdata;
	const char *pname = pe->pmu ? pe->pmu : data->cpu_name;

	if (!pe->name)
		return 0;

	if (data->pmu->is_uncore && pmu_uncore_alias_match(pname, data->name))
		goto new_alias;

	if (strcmp(pname, data->name))
		return 0;

new_alias:
	/* need type casts to override 'const' */
	__perf_pmu__new_alias(data->head, NULL, (char *)pe->name, (char *)pe->desc,
			      (char *)pe->event, pe);
	return 0;
}

/*
 * From the pmu_events_map, find the table of PMU events that corresponds
 * to the current running CPU. Then, add all PMU events from that table
 * as aliases.
 */
void pmu_add_cpu_aliases_table(struct list_head *head, struct perf_pmu *pmu,
			       const struct pmu_events_table *table)
{
	struct pmu_add_cpu_aliases_map_data data = {
		.head = head,
		.name = pmu->name,
		.cpu_name = is_arm_pmu_core(pmu->name) ? pmu->name : "cpu",
		.pmu = pmu,
	};

	pmu_events_table_for_each_event(table, pmu_add_cpu_aliases_map_callback, &data);
}

static void pmu_add_cpu_aliases(struct list_head *head, struct perf_pmu *pmu)
{
	const struct pmu_events_table *table;

	table = perf_pmu__find_table(pmu);
	if (!table)
		return;

	pmu_add_cpu_aliases_table(head, pmu, table);
}

struct pmu_sys_event_iter_data {
	struct list_head *head;
	struct perf_pmu *pmu;
};

static int pmu_add_sys_aliases_iter_fn(const struct pmu_event *pe,
				       const struct pmu_events_table *table __maybe_unused,
				       void *data)
{
	struct pmu_sys_event_iter_data *idata = data;
	struct perf_pmu *pmu = idata->pmu;

	if (!pe->name) {
		if (pe->metric_group || pe->metric_name)
			return 0;
		return -EINVAL;
	}

	if (!pe->compat || !pe->pmu)
		return 0;

	if (!strcmp(pmu->id, pe->compat) &&
	    pmu_uncore_alias_match(pe->pmu, pmu->name)) {
		__perf_pmu__new_alias(idata->head, NULL,
				      (char *)pe->name,
				      (char *)pe->desc,
				      (char *)pe->event,
				      pe);
	}

	return 0;
}

void pmu_add_sys_aliases(struct list_head *head, struct perf_pmu *pmu)
{
	struct pmu_sys_event_iter_data idata = {
		.head = head,
		.pmu = pmu,
	};

	if (!pmu->id)
		return;

	pmu_for_each_sys_event(pmu_add_sys_aliases_iter_fn, &idata);
}

struct perf_event_attr * __weak
perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
	return NULL;
}

char * __weak
pmu_find_real_name(const char *name)
{
	return (char *)name;
}

char * __weak
pmu_find_alias_name(const char *name __maybe_unused)
{
	return NULL;
}

static int pmu_max_precise(const char *name)
{
	char path[PATH_MAX];
	int max_precise = -1;

	scnprintf(path, PATH_MAX,
		 "bus/event_source/devices/%s/caps/max_precise",
		 name);

	sysfs__read_int(path, &max_precise);
	return max_precise;
}

static struct perf_pmu *pmu_lookup(const char *lookup_name)
{
	struct perf_pmu *pmu;
	LIST_HEAD(format);
	LIST_HEAD(aliases);
	__u32 type;
	char *name = pmu_find_real_name(lookup_name);
	bool is_hybrid = perf_pmu__hybrid_mounted(name);
	char *alias_name;

	/*
	 * Check pmu name for hybrid and the pmu may be invalid in sysfs
	 */
	if (!strncmp(name, "cpu_", 4) && !is_hybrid)
		return NULL;

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(name, &format))
		return NULL;

	/*
	 * Check the type first to avoid unnecessary work.
	 */
	if (pmu_type(name, &type))
		return NULL;

	if (pmu_aliases(name, &aliases))
		return NULL;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	pmu->cpus = pmu_cpumask(name);
	pmu->name = strdup(name);
	if (!pmu->name)
		goto err;

	alias_name = pmu_find_alias_name(name);
	if (alias_name) {
		pmu->alias_name = strdup(alias_name);
		if (!pmu->alias_name)
			goto err;
	}

	pmu->type = type;
	pmu->is_uncore = pmu_is_uncore(name);
	if (pmu->is_uncore)
		pmu->id = pmu_id(name);
	pmu->is_hybrid = is_hybrid;
	pmu->max_precise = pmu_max_precise(name);
	pmu_add_cpu_aliases(&aliases, pmu);
	pmu_add_sys_aliases(&aliases, pmu);

	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->aliases);
	INIT_LIST_HEAD(&pmu->caps);
	list_splice(&format, &pmu->format);
	list_splice(&aliases, &pmu->aliases);
	list_add_tail(&pmu->list, &pmus);

	if (pmu->is_hybrid)
		list_add_tail(&pmu->hybrid_list, &perf_pmu__hybrid_pmus);

	pmu->default_config = perf_pmu__get_default_config(pmu);

	return pmu;
err:
	if (pmu->name)
		free(pmu->name);
	free(pmu);
	return NULL;
}

void perf_pmu__warn_invalid_formats(struct perf_pmu *pmu)
{
	struct perf_pmu_format *format;

	/* fake pmu doesn't have format list */
	if (pmu == &perf_pmu__fake)
		return;

	list_for_each_entry(format, &pmu->format, list)
		if (format->value >= PERF_PMU_FORMAT_VALUE_CONFIG_END) {
			pr_warning("WARNING: '%s' format '%s' requires 'perf_event_attr::config%d'"
				   "which is not supported by this version of perf!\n",
				   pmu->name, format->name, format->value);
			return;
		}
}

static struct perf_pmu *pmu_find(const char *name)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &pmus, list) {
		if (!strcmp(pmu->name, name) ||
		    (pmu->alias_name && !strcmp(pmu->alias_name, name)))
			return pmu;
	}

	return NULL;
}

struct perf_pmu *perf_pmu__find_by_type(unsigned int type)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &pmus, list)
		if (pmu->type == type)
			return pmu;

	return NULL;
}

struct perf_pmu *perf_pmu__scan(struct perf_pmu *pmu)
{
	/*
	 * pmu iterator: If pmu is NULL, we start at the begin,
	 * otherwise return the next pmu. Returns NULL on end.
	 */
	if (!pmu) {
		pmu_read_sysfs();
		pmu = list_prepare_entry(pmu, &pmus, list);
	}
	list_for_each_entry_continue(pmu, &pmus, list)
		return pmu;
	return NULL;
}

struct perf_pmu *evsel__find_pmu(struct evsel *evsel)
{
	struct perf_pmu *pmu = NULL;

	if (evsel->pmu)
		return evsel->pmu;

	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (pmu->type == evsel->core.attr.type)
			break;
	}

	evsel->pmu = pmu;
	return pmu;
}

bool evsel__is_aux_event(struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel__find_pmu(evsel);

	return pmu && pmu->auxtrace;
}

struct perf_pmu *perf_pmu__find(const char *name)
{
	struct perf_pmu *pmu;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	return pmu_lookup(name);
}

static struct perf_pmu_format *
pmu_find_format(struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format;

	list_for_each_entry(format, formats, list)
		if (!strcmp(format->name, name))
			return format;

	return NULL;
}

__u64 perf_pmu__format_bits(struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format = pmu_find_format(formats, name);
	__u64 bits = 0;
	int fbit;

	if (!format)
		return 0;

	for_each_set_bit(fbit, format->bits, PERF_PMU_FORMAT_BITS)
		bits |= 1ULL << fbit;

	return bits;
}

int perf_pmu__format_type(struct list_head *formats, const char *name)
{
	struct perf_pmu_format *format = pmu_find_format(formats, name);

	if (!format)
		return -1;

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
				  struct list_head *head_terms,
				  __u64 *value)
{
	struct parse_events_term *t;

	list_for_each_entry(t, head_terms, list) {
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

static char *pmu_formats_string(struct list_head *formats)
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
static int pmu_config_term(const char *pmu_name,
			   struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct list_head *head_terms,
			   bool zero, struct parse_events_error *err)
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
	 * Hardcoded terms should be already in, so nothing
	 * to be done for them.
	 */
	if (parse_events__is_hardcoded_term(term))
		return 0;

	format = pmu_find_format(formats, term->config);
	if (!format) {
		char *pmu_term = pmu_formats_string(formats);
		char *unknown_term;
		char *help_msg;

		if (asprintf(&unknown_term,
				"unknown term '%s' for pmu '%s'",
				term->config, pmu_name) < 0)
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

			parse_events_error__handle(err, term->err_val,
				asprintf(&err_str,
				    "value too big for format, maximum is %llu",
				    (unsigned long long)max_val) < 0
				    ? strdup("value too big for format")
				    : err_str,
				    NULL);
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

int perf_pmu__config_terms(const char *pmu_name, struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct list_head *head_terms,
			   bool zero, struct parse_events_error *err)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head_terms, list) {
		if (pmu_config_term(pmu_name, formats, attr, term, head_terms,
				    zero, err))
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
		     struct list_head *head_terms,
		     struct parse_events_error *err)
{
	bool zero = !!pmu->default_config;

	attr->type = pmu->type;
	return perf_pmu__config_terms(pmu->name, &pmu->format, attr,
				      head_terms, zero, err);
}

static struct perf_pmu_alias *pmu_find_alias(struct perf_pmu *pmu,
					     struct parse_events_term *term)
{
	struct perf_pmu_alias *alias;
	char *name;

	if (parse_events__is_hardcoded_term(term))
		return NULL;

	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
		if (term->val.num != 1)
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

	list_for_each_entry(alias, &pmu->aliases, list) {
		if (!strcasecmp(alias->name, name))
			return alias;
	}
	return NULL;
}


static int check_info_data(struct perf_pmu_alias *alias,
			   struct perf_pmu_info *info)
{
	/*
	 * Only one term in event definition can
	 * define unit, scale and snapshot, fail
	 * if there's more than one.
	 */
	if ((info->unit && alias->unit[0]) ||
	    (info->scale && alias->scale) ||
	    (info->snapshot && alias->snapshot))
		return -EINVAL;

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
int perf_pmu__check_alias(struct perf_pmu *pmu, struct list_head *head_terms,
			  struct perf_pmu_info *info)
{
	struct parse_events_term *term, *h;
	struct perf_pmu_alias *alias;
	int ret;

	info->per_pkg = false;

	/*
	 * Mark unit and scale as not set
	 * (different from default values, see below)
	 */
	info->unit     = NULL;
	info->scale    = 0.0;
	info->snapshot = false;
	info->metric_expr = NULL;
	info->metric_name = NULL;

	list_for_each_entry_safe(term, h, head_terms, list) {
		alias = pmu_find_alias(pmu, term);
		if (!alias)
			continue;
		ret = pmu_alias_terms(alias, &term->list);
		if (ret)
			return ret;

		ret = check_info_data(alias, info);
		if (ret)
			return ret;

		if (alias->per_pkg)
			info->per_pkg = true;
		info->metric_expr = alias->metric_expr;
		info->metric_name = alias->metric_name;

		list_del_init(&term->list);
		parse_events_term__delete(term);
	}

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

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits)
{
	struct perf_pmu_format *format;

	format = zalloc(sizeof(*format));
	if (!format)
		return -ENOMEM;

	format->name = strdup(name);
	format->value = config;
	memcpy(format->bits, bits, sizeof(format->bits));

	list_add_tail(&format->list, list);
	return 0;
}

void perf_pmu__set_format(unsigned long *bits, long from, long to)
{
	long b;

	if (!to)
		to = from;

	memset(bits, 0, BITS_TO_BYTES(PERF_PMU_FORMAT_BITS));
	for (b = from; b <= to; b++)
		set_bit(b, bits);
}

void perf_pmu__del_formats(struct list_head *formats)
{
	struct perf_pmu_format *fmt, *tmp;

	list_for_each_entry_safe(fmt, tmp, formats, list) {
		list_del(&fmt->list);
		free(fmt->name);
		free(fmt);
	}
}

static int sub_non_neg(int a, int b)
{
	if (b > a)
		return 0;
	return a - b;
}

static char *format_alias(char *buf, int len, struct perf_pmu *pmu,
			  struct perf_pmu_alias *alias)
{
	struct parse_events_term *term;
	int used = snprintf(buf, len, "%s/%s", pmu->name, alias->name);

	list_for_each_entry(term, &alias->terms, list) {
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

static char *format_alias_or(char *buf, int len, struct perf_pmu *pmu,
			     struct perf_pmu_alias *alias)
{
	snprintf(buf, len, "%s OR %s/%s/", alias->name, pmu->name, alias->name);
	return buf;
}

struct sevent {
	char *name;
	char *desc;
	char *topic;
	char *str;
	char *pmu;
	char *metric_expr;
	char *metric_name;
	int is_cpu;
};

static int cmp_sevent(const void *a, const void *b)
{
	const struct sevent *as = a;
	const struct sevent *bs = b;
	int ret;

	/* Put extra events last */
	if (!!as->desc != !!bs->desc)
		return !!as->desc - !!bs->desc;
	if (as->topic && bs->topic) {
		int n = strcmp(as->topic, bs->topic);

		if (n)
			return n;
	}

	/* Order CPU core events to be first */
	if (as->is_cpu != bs->is_cpu)
		return bs->is_cpu - as->is_cpu;

	ret = strcmp(as->name, bs->name);
	if (!ret) {
		if (as->pmu && bs->pmu)
			return strcmp(as->pmu, bs->pmu);
	}

	return ret;
}

static void wordwrap(char *s, int start, int max, int corr)
{
	int column = start;
	int n;

	while (*s) {
		int wlen = strcspn(s, " \t");

		if (column + wlen >= max && column > start) {
			printf("\n%*s", start, "");
			column = start + corr;
		}
		n = printf("%s%.*s", column > start ? " " : "", wlen, s);
		if (n <= 0)
			break;
		s += wlen;
		column += n;
		s = skip_spaces(s);
	}
}

bool is_pmu_core(const char *name)
{
	return !strcmp(name, "cpu") || is_arm_pmu_core(name);
}

static bool pmu_alias_is_duplicate(struct sevent *alias_a,
				   struct sevent *alias_b)
{
	/* Different names -> never duplicates */
	if (strcmp(alias_a->name, alias_b->name))
		return false;

	/* Don't remove duplicates for hybrid PMUs */
	if (perf_pmu__is_hybrid(alias_a->pmu) &&
	    perf_pmu__is_hybrid(alias_b->pmu))
		return false;

	return true;
}

void print_pmu_events(const char *event_glob, bool name_only, bool quiet_flag,
			bool long_desc, bool details_flag, bool deprecated,
			const char *pmu_name)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;
	char buf[1024];
	int printed = 0;
	int len, j;
	struct sevent *aliases;
	int numdesc = 0;
	int columns = pager_get_columns();
	char *topic = NULL;

	pmu = NULL;
	len = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		list_for_each_entry(alias, &pmu->aliases, list)
			len++;
		if (pmu->selectable)
			len++;
	}
	aliases = zalloc(sizeof(struct sevent) * len);
	if (!aliases)
		goto out_enomem;
	pmu = NULL;
	j = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (pmu_name && perf_pmu__is_hybrid(pmu->name) &&
		    strcmp(pmu_name, pmu->name)) {
			continue;
		}

		list_for_each_entry(alias, &pmu->aliases, list) {
			char *name = alias->desc ? alias->name :
				format_alias(buf, sizeof(buf), pmu, alias);
			bool is_cpu = is_pmu_core(pmu->name) ||
				      perf_pmu__is_hybrid(pmu->name);

			if (alias->deprecated && !deprecated)
				continue;

			if (event_glob != NULL &&
			    !(strglobmatch_nocase(name, event_glob) ||
			      (!is_cpu && strglobmatch_nocase(alias->name,
						       event_glob)) ||
			      (alias->topic &&
			       strglobmatch_nocase(alias->topic, event_glob))))
				continue;

			if (is_cpu && !name_only && !alias->desc)
				name = format_alias_or(buf, sizeof(buf), pmu, alias);

			aliases[j].name = name;
			if (is_cpu && !name_only && !alias->desc)
				aliases[j].name = format_alias_or(buf,
								  sizeof(buf),
								  pmu, alias);
			aliases[j].name = strdup(aliases[j].name);
			if (!aliases[j].name)
				goto out_enomem;

			aliases[j].desc = long_desc ? alias->long_desc :
						alias->desc;
			aliases[j].topic = alias->topic;
			aliases[j].str = alias->str;
			aliases[j].pmu = pmu->name;
			aliases[j].metric_expr = alias->metric_expr;
			aliases[j].metric_name = alias->metric_name;
			aliases[j].is_cpu = is_cpu;
			j++;
		}
		if (pmu->selectable &&
		    (event_glob == NULL || strglobmatch(pmu->name, event_glob))) {
			char *s;
			if (asprintf(&s, "%s//", pmu->name) < 0)
				goto out_enomem;
			aliases[j].name = s;
			j++;
		}
	}
	len = j;
	qsort(aliases, len, sizeof(struct sevent), cmp_sevent);
	for (j = 0; j < len; j++) {
		/* Skip duplicates */
		if (j > 0 && pmu_alias_is_duplicate(&aliases[j], &aliases[j - 1]))
			continue;

		if (name_only) {
			printf("%s ", aliases[j].name);
			continue;
		}
		if (aliases[j].desc && !quiet_flag) {
			if (numdesc++ == 0)
				printf("\n");
			if (aliases[j].topic && (!topic ||
					strcmp(topic, aliases[j].topic))) {
				printf("%s%s:\n", topic ? "\n" : "",
						aliases[j].topic);
				topic = aliases[j].topic;
			}
			printf("  %-50s\n", aliases[j].name);
			printf("%*s", 8, "[");
			wordwrap(aliases[j].desc, 8, columns, 0);
			printf("]\n");
			if (details_flag) {
				printf("%*s%s/%s/ ", 8, "", aliases[j].pmu, aliases[j].str);
				if (aliases[j].metric_name)
					printf(" MetricName: %s", aliases[j].metric_name);
				if (aliases[j].metric_expr)
					printf(" MetricExpr: %s", aliases[j].metric_expr);
				putchar('\n');
			}
		} else
			printf("  %-50s [Kernel PMU event]\n", aliases[j].name);
		printed++;
	}
	if (printed && pager_in_use())
		printf("\n");
out_free:
	for (j = 0; j < len; j++)
		zfree(&aliases[j].name);
	zfree(&aliases);
	return;

out_enomem:
	printf("FATAL: not enough memory to print PMU events\n");
	if (aliases)
		goto out_free;
}

bool pmu_have_event(const char *pname, const char *name)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;

	pmu = NULL;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (strcmp(pname, pmu->name))
			continue;
		list_for_each_entry(alias, &pmu->aliases, list)
			if (!strcmp(alias->name, name))
				return true;
	}
	return false;
}

static FILE *perf_pmu__open_file(struct perf_pmu *pmu, const char *name)
{
	char path[PATH_MAX];
	const char *sysfs;

	sysfs = sysfs__mountpoint();
	if (!sysfs)
		return NULL;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/%s", sysfs, pmu->name, name);
	if (!file_available(path))
		return NULL;
	return fopen(path, "r");
}

int perf_pmu__scan_file(struct perf_pmu *pmu, const char *name, const char *fmt,
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
	zfree(caps->name);
free_caps:
	free(caps);

	return -ENOMEM;
}

/*
 * Reading/parsing the given pmu capabilities, which should be located at:
 * /sys/bus/event_source/devices/<dev>/caps as sysfs group attributes.
 * Return the number of capabilities
 */
int perf_pmu__caps_parse(struct perf_pmu *pmu)
{
	struct stat st;
	char caps_path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();
	DIR *caps_dir;
	struct dirent *evt_ent;

	if (pmu->caps_initialized)
		return pmu->nr_caps;

	pmu->nr_caps = 0;

	if (!sysfs)
		return -1;

	snprintf(caps_path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/caps", sysfs, pmu->name);

	if (stat(caps_path, &st) < 0) {
		pmu->caps_initialized = true;
		return 0;	/* no error if caps does not exist */
	}

	caps_dir = opendir(caps_path);
	if (!caps_dir)
		return -EINVAL;

	while ((evt_ent = readdir(caps_dir)) != NULL) {
		char path[PATH_MAX + NAME_MAX + 1];
		char *name = evt_ent->d_name;
		char value[128];
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		snprintf(path, sizeof(path), "%s/%s", caps_path, name);

		file = fopen(path, "r");
		if (!file)
			continue;

		if (!fgets(value, sizeof(value), file) ||
		    (perf_pmu__new_caps(&pmu->caps, name, value) < 0)) {
			fclose(file);
			continue;
		}

		pmu->nr_caps++;
		fclose(file);
	}

	closedir(caps_dir);

	pmu->caps_initialized = true;
	return pmu->nr_caps;
}

void perf_pmu__warn_invalid_config(struct perf_pmu *pmu, __u64 config,
				   const char *name)
{
	struct perf_pmu_format *format;
	__u64 masks = 0, bits;
	char buf[100];
	unsigned int i;

	list_for_each_entry(format, &pmu->format, list)	{
		if (format->value != PERF_PMU_FORMAT_VALUE_CONFIG)
			continue;

		for_each_set_bit(i, format->bits, PERF_PMU_FORMAT_BITS)
			masks |= 1ULL << i;
	}

	/*
	 * Kernel doesn't export any valid format bits.
	 */
	if (masks == 0)
		return;

	bits = config & ~masks;
	if (bits == 0)
		return;

	bitmap_scnprintf((unsigned long *)&bits, sizeof(bits) * 8, buf, sizeof(buf));

	pr_warning("WARNING: event '%s' not valid (bits %s of config "
		   "'%llx' not supported by kernel)!\n",
		   name ?: "N/A", buf, config);
}

bool perf_pmu__has_hybrid(void)
{
	if (!hybrid_scanned) {
		hybrid_scanned = true;
		perf_pmu__scan(NULL);
	}

	return !list_empty(&perf_pmu__hybrid_pmus);
}

int perf_pmu__match(char *pattern, char *name, char *tok)
{
	if (!name)
		return -1;

	if (fnmatch(pattern, name, 0))
		return -1;

	if (tok && !perf_pmu__valid_suffix(name, tok))
		return -1;

	return 0;
}

int perf_pmu__cpus_match(struct perf_pmu *pmu, struct perf_cpu_map *cpus,
			 struct perf_cpu_map **mcpus_ptr,
			 struct perf_cpu_map **ucpus_ptr)
{
	struct perf_cpu_map *pmu_cpus = pmu->cpus;
	struct perf_cpu_map *matched_cpus, *unmatched_cpus;
	struct perf_cpu cpu;
	int i, matched_nr = 0, unmatched_nr = 0;

	matched_cpus = perf_cpu_map__default_new();
	if (!matched_cpus)
		return -1;

	unmatched_cpus = perf_cpu_map__default_new();
	if (!unmatched_cpus) {
		perf_cpu_map__put(matched_cpus);
		return -1;
	}

	perf_cpu_map__for_each_cpu(cpu, i, cpus) {
		if (!perf_cpu_map__has(pmu_cpus, cpu))
			unmatched_cpus->map[unmatched_nr++] = cpu;
		else
			matched_cpus->map[matched_nr++] = cpu;
	}

	unmatched_cpus->nr = unmatched_nr;
	matched_cpus->nr = matched_nr;
	*mcpus_ptr = matched_cpus;
	*ucpus_ptr = unmatched_cpus;
	return 0;
}
