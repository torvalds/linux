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
#include <locale.h>
#include <fnmatch.h>
#include <math.h>
#include "debug.h"
#include "evsel.h"
#include "pmu.h"
#include "pmus.h"
#include "pmu-bison.h"
#include "pmu-flex.h"
#include "parse-events.h"
#include "print-events.h"
#include "header.h"
#include "string2.h"
#include "strbuf.h"
#include "fncache.h"
#include "util/evsel_config.h"

struct perf_pmu perf_pmu__fake;

/**
 * struct perf_pmu_format - Values from a format file read from
 * <sysfs>/devices/cpu/format/ held in struct perf_pmu.
 *
 * For example, the contents of <sysfs>/devices/cpu/format/event may be
 * "config:0-7" and will be represented here as name="event",
 * value=PERF_PMU_FORMAT_VALUE_CONFIG and bits 0 to 7 will be set.
 */
struct perf_pmu_format {
	/** @name: The modifier/file name. */
	char *name;
	/**
	 * @value : Which config value the format relates to. Supported values
	 * are from PERF_PMU_FORMAT_VALUE_CONFIG to
	 * PERF_PMU_FORMAT_VALUE_CONFIG_END.
	 */
	int value;
	/** @bits: Which config bits are set by this format value. */
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	/** @list: Element on list within struct perf_pmu. */
	struct list_head list;
};

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
int perf_pmu__format_parse(int dirfd, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *format_dir;
	int ret = 0;

	format_dir = fdopendir(dirfd);
	if (!format_dir)
		return -EINVAL;

	while (!ret && (evt_ent = readdir(format_dir))) {
		char *name = evt_ent->d_name;
		int fd;
		void *scanner;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;


		ret = -EINVAL;
		fd = openat(dirfd, name, O_RDONLY);
		if (fd < 0)
			break;

		file = fdopen(fd, "r");
		if (!file) {
			close(fd);
			break;
		}

		ret = perf_pmu_lex_init(&scanner);
		if (ret) {
			fclose(file);
			break;
		}

		perf_pmu_set_in(file, scanner);
		ret = perf_pmu_parse(head, name, scanner);
		perf_pmu_lex_destroy(scanner);
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
static int pmu_format(int dirfd, const char *name, struct list_head *format)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, name, "format", O_DIRECTORY);
	if (fd < 0)
		return 0;

	/* it'll close the fd */
	if (perf_pmu__format_parse(fd, format))
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

static int perf_pmu__parse_scale(struct perf_pmu_alias *alias, int dirfd, char *name)
{
	struct stat st;
	ssize_t sret;
	char scale[128];
	int fd, ret = -1;
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s.scale", name);

	fd = openat(dirfd, path, O_RDONLY);
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

static int perf_pmu__parse_unit(struct perf_pmu_alias *alias, int dirfd, char *name)
{
	char path[PATH_MAX];
	ssize_t sret;
	int fd;

	scnprintf(path, PATH_MAX, "%s.unit", name);

	fd = openat(dirfd, path, O_RDONLY);
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
perf_pmu__parse_per_pkg(struct perf_pmu_alias *alias, int dirfd, char *name)
{
	char path[PATH_MAX];
	int fd;

	scnprintf(path, PATH_MAX, "%s.per-pkg", name);

	fd = openat(dirfd, path, O_RDONLY);
	if (fd == -1)
		return -1;

	close(fd);

	alias->per_pkg = true;
	return 0;
}

static int perf_pmu__parse_snapshot(struct perf_pmu_alias *alias,
				    int dirfd, char *name)
{
	char path[PATH_MAX];
	int fd;

	scnprintf(path, PATH_MAX, "%s.snapshot", name);

	fd = openat(dirfd, path, O_RDONLY);
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
	zfree(&newalias->pmu_name);
	parse_events_terms__purge(&newalias->terms);
	free(newalias);
}

static void perf_pmu__del_aliases(struct perf_pmu *pmu)
{
	struct perf_pmu_alias *alias, *tmp;

	list_for_each_entry_safe(alias, tmp, &pmu->aliases, list) {
		list_del(&alias->list);
		perf_pmu_free_alias(alias);
	}
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

static int __perf_pmu__new_alias(struct list_head *list, int dirfd, char *name,
				 char *desc, char *val, const struct pmu_event *pe)
{
	struct parse_events_term *term;
	struct perf_pmu_alias *alias;
	int ret;
	char newval[256];
	const char *long_desc = NULL, *topic = NULL, *unit = NULL, *pmu_name = NULL;
	bool deprecated = false, perpkg = false;

	if (pe) {
		long_desc = pe->long_desc;
		topic = pe->topic;
		unit = pe->unit;
		perpkg = pe->perpkg;
		deprecated = pe->deprecated;
		pmu_name = pe->pmu;
	}

	alias = malloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;

	INIT_LIST_HEAD(&alias->terms);
	alias->scale = 1.0;
	alias->unit[0] = '\0';
	alias->per_pkg = perpkg;
	alias->snapshot = false;
	alias->deprecated = deprecated;

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
	if (dirfd >= 0) {
		/*
		 * load unit name and scale if available
		 */
		perf_pmu__parse_unit(alias, dirfd, name);
		perf_pmu__parse_scale(alias, dirfd, name);
		perf_pmu__parse_per_pkg(alias, dirfd, name);
		perf_pmu__parse_snapshot(alias, dirfd, name);
	}

	alias->desc = desc ? strdup(desc) : NULL;
	alias->long_desc = long_desc ? strdup(long_desc) :
				desc ? strdup(desc) : NULL;
	alias->topic = topic ? strdup(topic) : NULL;
	if (unit) {
		if (perf_pmu__convert_scale(unit, (char **)&unit, &alias->scale) < 0)
			return -1;
		snprintf(alias->unit, sizeof(alias->unit), "%s", unit);
	}
	alias->str = strdup(newval);
	alias->pmu_name = pmu_name ? strdup(pmu_name) : NULL;

	if (!perf_pmu_merge_alias(alias, list))
		list_add_tail(&alias->list, list);

	return 0;
}

static int perf_pmu__new_alias(struct list_head *list, int dirfd, char *name, FILE *file)
{
	char buf[256];
	int ret;

	ret = fread(buf, 1, sizeof(buf), file);
	if (ret == 0)
		return -EINVAL;

	buf[ret] = 0;

	/* Remove trailing newline from sysfs file */
	strim(buf);

	return __perf_pmu__new_alias(list, dirfd, name, NULL, buf, NULL);
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
static int pmu_aliases_parse(int dirfd, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *event_dir;
	int fd;

	event_dir = fdopendir(dirfd);
	if (!event_dir)
		return -EINVAL;

	while ((evt_ent = readdir(event_dir))) {
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/*
		 * skip info files parsed in perf_pmu__new_alias()
		 */
		if (pmu_alias_info_file(name))
			continue;

		fd = openat(dirfd, name, O_RDONLY);
		if (fd == -1) {
			pr_debug("Cannot open %s\n", name);
			continue;
		}
		file = fdopen(fd, "r");
		if (!file) {
			close(fd);
			continue;
		}

		if (perf_pmu__new_alias(head, dirfd, name, file) < 0)
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
static int pmu_aliases(int dirfd, const char *name, struct list_head *head)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, name, "events", O_DIRECTORY);
	if (fd < 0)
		return 0;

	/* it'll close the fd */
	if (pmu_aliases_parse(fd, head))
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
 * Uncore PMUs have a "cpumask" file under sysfs. CPU PMUs (e.g. on arm/arm64)
 * may have a "cpus" file.
 */
static struct perf_cpu_map *pmu_cpumask(int dirfd, const char *name, bool is_core)
{
	struct perf_cpu_map *cpus;
	const char *templates[] = {
		"cpumask",
		"cpus",
		NULL
	};
	const char **template;
	char pmu_name[PATH_MAX];
	struct perf_pmu pmu = {.name = pmu_name};
	FILE *file;

	strlcpy(pmu_name, name, sizeof(pmu_name));
	for (template = templates; *template; template++) {
		file = perf_pmu__open_file_at(&pmu, dirfd, *template);
		if (!file)
			continue;
		cpus = perf_cpu_map__read(file);
		fclose(file);
		if (cpus)
			return cpus;
	}

	/* Nothing found, for core PMUs assume this means all CPUs. */
	return is_core ? perf_cpu_map__get(cpu_map__online()) : NULL;
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
	return perf_pmu__find_events_table(NULL);
}

__weak const struct pmu_metrics_table *pmu_metrics_table__find(void)
{
	return perf_pmu__find_metrics_table(NULL);
}

/**
 * perf_pmu__match_ignoring_suffix - Does the pmu_name match tok ignoring any
 *                                   trailing suffix? The Suffix must be in form
 *                                   tok_{digits}, or tok{digits}.
 * @pmu_name: The pmu_name with possible suffix.
 * @tok: The possible match to pmu_name without suffix.
 */
static bool perf_pmu__match_ignoring_suffix(const char *pmu_name, const char *tok)
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

/**
 * pmu_uncore_alias_match - does name match the PMU name?
 * @pmu_name: the json struct pmu_event name. This may lack a suffix (which
 *            matches) or be of the form "socket,pmuname" which will match
 *            "socketX_pmunameY".
 * @name: a real full PMU name as from sysfs.
 */
static bool pmu_uncore_alias_match(const char *pmu_name, const char *name)
{
	char *tmp = NULL, *tok, *str;
	bool res;

	if (strchr(pmu_name, ',') == NULL)
		return perf_pmu__match_ignoring_suffix(name, pmu_name);

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
		    (!next_tok && !perf_pmu__match_ignoring_suffix(name, tok))) {
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
	/* List being added to. */
	struct list_head *head;
	/* If a pmu_event lacks a given PMU the default used. */
	char *default_pmu_name;
	/* The PMU that we're searching for events for. */
	struct perf_pmu *pmu;
};

static int pmu_add_cpu_aliases_map_callback(const struct pmu_event *pe,
					const struct pmu_events_table *table __maybe_unused,
					void *vdata)
{
	struct pmu_add_cpu_aliases_map_data *data = vdata;
	const char *pname = pe->pmu ?: data->default_pmu_name;

	if (!strcmp(pname, data->pmu->name) ||
	    (data->pmu->is_uncore && pmu_uncore_alias_match(pname, data->pmu->name))) {
		/* need type casts to override 'const' */
		__perf_pmu__new_alias(data->head, -1, (char *)pe->name, (char *)pe->desc,
				      (char *)pe->event, pe);
	}
	return 0;
}

/*
 * From the pmu_events_table, find the events that correspond to the given
 * PMU and add them to the list 'head'.
 */
void pmu_add_cpu_aliases_table(struct list_head *head, struct perf_pmu *pmu,
			const struct pmu_events_table *table)
{
	struct pmu_add_cpu_aliases_map_data data = {
		.head = head,
		.default_pmu_name = perf_pmus__default_pmu_name(),
		.pmu = pmu,
	};

	pmu_events_table_for_each_event(table, pmu_add_cpu_aliases_map_callback, &data);
	free(data.default_pmu_name);
}

static void pmu_add_cpu_aliases(struct list_head *head, struct perf_pmu *pmu)
{
	const struct pmu_events_table *table;

	table = perf_pmu__find_events_table(pmu);
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

	if (!pe->compat || !pe->pmu)
		return 0;

	if (!strcmp(pmu->id, pe->compat) &&
	    pmu_uncore_alias_match(pe->pmu, pmu->name)) {
		__perf_pmu__new_alias(idata->head, -1,
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

static int pmu_max_precise(int dirfd, struct perf_pmu *pmu)
{
	int max_precise = -1;

	perf_pmu__scan_file_at(pmu, dirfd, "caps/max_precise", "%d", &max_precise);
	return max_precise;
}

struct perf_pmu *perf_pmu__lookup(struct list_head *pmus, int dirfd, const char *lookup_name)
{
	struct perf_pmu *pmu;
	LIST_HEAD(format);
	LIST_HEAD(aliases);
	__u32 type;
	char *name = pmu_find_real_name(lookup_name);
	char *alias_name;

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(dirfd, name, &format))
		return NULL;

	/*
	 * Check the aliases first to avoid unnecessary work.
	 */
	if (pmu_aliases(dirfd, name, &aliases))
		return NULL;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	pmu->is_core = is_pmu_core(name);
	pmu->cpus = pmu_cpumask(dirfd, name, pmu->is_core);
	pmu->name = strdup(name);
	if (!pmu->name)
		goto err;

	/* Read type, and ensure that type value is successfully assigned (return 1) */
	if (perf_pmu__scan_file_at(pmu, dirfd, "type", "%u", &type) != 1)
		goto err;

	alias_name = pmu_find_alias_name(name);
	if (alias_name) {
		pmu->alias_name = strdup(alias_name);
		if (!pmu->alias_name)
			goto err;
	}

	pmu->type = type;
	pmu->is_uncore = pmu_is_uncore(dirfd, name);
	if (pmu->is_uncore)
		pmu->id = pmu_id(name);
	pmu->max_precise = pmu_max_precise(dirfd, pmu);
	pmu_add_cpu_aliases(&aliases, pmu);
	pmu_add_sys_aliases(&aliases, pmu);

	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->aliases);
	INIT_LIST_HEAD(&pmu->caps);
	list_splice(&format, &pmu->format);
	list_splice(&aliases, &pmu->aliases);
	list_add_tail(&pmu->list, pmus);

	pmu->default_config = perf_pmu__get_default_config(pmu);

	return pmu;
err:
	zfree(&pmu->name);
	free(pmu);
	return NULL;
}

void perf_pmu__warn_invalid_formats(struct perf_pmu *pmu)
{
	struct perf_pmu_format *format;

	if (pmu->formats_checked)
		return;

	pmu->formats_checked = true;

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

bool evsel__is_aux_event(const struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel__find_pmu(evsel);

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

	bits = perf_pmu__format_bits(&pmu->format, config_name);

	/* Do nothing if the user changed the value */
	if (bits & user_bits)
		return;

	/* Otherwise replace it */
	evsel->core.attr.config &= ~bits;
	evsel->core.attr.config |= field_prep(bits, val);
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
		__set_bit(b, bits);
}

void perf_pmu__del_formats(struct list_head *formats)
{
	struct perf_pmu_format *fmt, *tmp;

	list_for_each_entry_safe(fmt, tmp, formats, list) {
		list_del(&fmt->list);
		zfree(&fmt->name);
		free(fmt);
	}
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

bool perf_pmu__have_event(const struct perf_pmu *pmu, const char *name)
{
	struct perf_pmu_alias *alias;

	list_for_each_entry(alias, &pmu->aliases, list) {
		if (!strcmp(alias->name, name))
			return true;
	}
	return false;
}

bool perf_pmu__is_software(const struct perf_pmu *pmu)
{
	if (pmu->is_core || pmu->is_uncore || pmu->auxtrace)
		return false;
	switch (pmu->type) {
	case PERF_TYPE_HARDWARE:	return false;
	case PERF_TYPE_SOFTWARE:	return true;
	case PERF_TYPE_TRACEPOINT:	return true;
	case PERF_TYPE_HW_CACHE:	return false;
	case PERF_TYPE_RAW:		return false;
	case PERF_TYPE_BREAKPOINT:	return true;
	default: break;
	}
	return !strcmp(pmu->name, "kprobe") || !strcmp(pmu->name, "uprobe");
}

FILE *perf_pmu__open_file(struct perf_pmu *pmu, const char *name)
{
	char path[PATH_MAX];

	if (!perf_pmu__pathname_scnprintf(path, sizeof(path), pmu->name, name) ||
	    !file_available(path))
		return NULL;

	return fopen(path, "r");
}

FILE *perf_pmu__open_file_at(struct perf_pmu *pmu, int dirfd, const char *name)
{
	int fd;

	fd = perf_pmu__pathname_fd(dirfd, pmu->name, name, O_RDONLY);
	if (fd < 0)
		return NULL;

	return fdopen(fd, "r");
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

int perf_pmu__scan_file_at(struct perf_pmu *pmu, int dirfd, const char *name,
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

bool perf_pmu__file_exists(struct perf_pmu *pmu, const char *name)
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

/*
 * Reading/parsing the given pmu capabilities, which should be located at:
 * /sys/bus/event_source/devices/<dev>/caps as sysfs group attributes.
 * Return the number of capabilities
 */
int perf_pmu__caps_parse(struct perf_pmu *pmu)
{
	struct stat st;
	char caps_path[PATH_MAX];
	DIR *caps_dir;
	struct dirent *evt_ent;
	int caps_fd;

	if (pmu->caps_initialized)
		return pmu->nr_caps;

	pmu->nr_caps = 0;

	if (!perf_pmu__pathname_scnprintf(caps_path, sizeof(caps_path), pmu->name, "caps"))
		return -1;

	if (stat(caps_path, &st) < 0) {
		pmu->caps_initialized = true;
		return 0;	/* no error if caps does not exist */
	}

	caps_dir = opendir(caps_path);
	if (!caps_dir)
		return -EINVAL;

	caps_fd = dirfd(caps_dir);

	while ((evt_ent = readdir(caps_dir)) != NULL) {
		char *name = evt_ent->d_name;
		char value[128];
		FILE *file;
		int fd;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
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

	closedir(caps_dir);

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

int perf_pmu__match(char *pattern, char *name, char *tok)
{
	if (!name)
		return -1;

	if (fnmatch(pattern, name, 0))
		return -1;

	if (tok && !perf_pmu__match_ignoring_suffix(name, tok))
		return -1;

	return 0;
}

double __weak perf_pmu__cpu_slots_per_cycle(void)
{
	return NAN;
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
 * Return 0 if the sysfs mountpoint couldn't be found or if no
 * characters were written.
 */
int perf_pmu__pathname_scnprintf(char *buf, size_t size,
				 const char *pmu_name, const char *filename)
{
	char base_path[PATH_MAX];

	if (!perf_pmu__event_source_devices_scnprintf(base_path, sizeof(base_path)))
		return 0;
	return scnprintf(buf, size, "%s%s/%s", base_path, pmu_name, filename);
}

int perf_pmu__pathname_fd(int dirfd, const char *pmu_name, const char *filename, int flags)
{
	char path[PATH_MAX];

	scnprintf(path, sizeof(path), "%s/%s", pmu_name, filename);
	return openat(dirfd, path, flags);
}

void perf_pmu__delete(struct perf_pmu *pmu)
{
	perf_pmu__del_formats(&pmu->format);
	perf_pmu__del_aliases(pmu);
	perf_pmu__del_caps(pmu);

	perf_cpu_map__put(pmu->cpus);

	zfree(&pmu->default_config);
	zfree(&pmu->name);
	zfree(&pmu->alias_name);
	free(pmu);
}
