// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/zalloc.h>
#include <subcmd/pager.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "debug.h"
#include "evsel.h"
#include "pmus.h"
#include "pmu.h"
#include "print-events.h"

/*
 * core_pmus:  A PMU belongs to core_pmus if it's name is "cpu" or it's sysfs
 *             directory contains "cpus" file. All PMUs belonging to core_pmus
 *             must have pmu->is_core=1. If there are more than one PMU in
 *             this list, perf interprets it as a heterogeneous platform.
 *             (FWIW, certain ARM platforms having heterogeneous cores uses
 *             homogeneous PMU, and thus they are treated as homogeneous
 *             platform by perf because core_pmus will have only one entry)
 * other_pmus: All other PMUs which are not part of core_pmus list. It doesn't
 *             matter whether PMU is present per SMT-thread or outside of the
 *             core in the hw. For e.g., an instance of AMD ibs_fetch// and
 *             ibs_op// PMUs is present in each hw SMT thread, however they
 *             are captured under other_pmus. PMUs belonging to other_pmus
 *             must have pmu->is_core=0 but pmu->is_uncore could be 0 or 1.
 */
static LIST_HEAD(core_pmus);
static LIST_HEAD(other_pmus);
static bool read_sysfs_core_pmus;
static bool read_sysfs_all_pmus;

void perf_pmus__destroy(void)
{
	struct perf_pmu *pmu, *tmp;

	list_for_each_entry_safe(pmu, tmp, &core_pmus, list) {
		list_del(&pmu->list);

		perf_pmu__delete(pmu);
	}
	list_for_each_entry_safe(pmu, tmp, &other_pmus, list) {
		list_del(&pmu->list);

		perf_pmu__delete(pmu);
	}
	read_sysfs_core_pmus = false;
	read_sysfs_all_pmus = false;
}

static struct perf_pmu *pmu_find(const char *name)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &core_pmus, list) {
		if (!strcmp(pmu->name, name) ||
		    (pmu->alias_name && !strcmp(pmu->alias_name, name)))
			return pmu;
	}
	list_for_each_entry(pmu, &other_pmus, list) {
		if (!strcmp(pmu->name, name) ||
		    (pmu->alias_name && !strcmp(pmu->alias_name, name)))
			return pmu;
	}

	return NULL;
}

struct perf_pmu *perf_pmus__find(const char *name)
{
	struct perf_pmu *pmu;
	int dirfd;
	bool core_pmu;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	if (read_sysfs_all_pmus)
		return NULL;

	core_pmu = is_pmu_core(name);
	if (core_pmu && read_sysfs_core_pmus)
		return NULL;

	dirfd = perf_pmu__event_source_devices_fd();
	pmu = perf_pmu__lookup(core_pmu ? &core_pmus : &other_pmus, dirfd, name);
	close(dirfd);

	return pmu;
}

static struct perf_pmu *perf_pmu__find2(int dirfd, const char *name)
{
	struct perf_pmu *pmu;
	bool core_pmu;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	if (read_sysfs_all_pmus)
		return NULL;

	core_pmu = is_pmu_core(name);
	if (core_pmu && read_sysfs_core_pmus)
		return NULL;

	return perf_pmu__lookup(core_pmu ? &core_pmus : &other_pmus, dirfd, name);
}

/* Add all pmus in sysfs to pmu list: */
static void pmu_read_sysfs(bool core_only)
{
	int fd;
	DIR *dir;
	struct dirent *dent;

	if (read_sysfs_all_pmus || (core_only && read_sysfs_core_pmus))
		return;

	fd = perf_pmu__event_source_devices_fd();
	if (fd < 0)
		return;

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return;
	}

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;
		if (core_only && !is_pmu_core(dent->d_name))
			continue;
		/* add to static LIST_HEAD(core_pmus) or LIST_HEAD(other_pmus): */
		perf_pmu__find2(fd, dent->d_name);
	}

	closedir(dir);
	if (core_only) {
		if (!list_empty(&core_pmus))
			read_sysfs_core_pmus = true;
		else {
			if (perf_pmu__create_placeholder_core_pmu(&core_pmus))
				read_sysfs_core_pmus = true;
		}
	} else {
		read_sysfs_core_pmus = true;
		read_sysfs_all_pmus = true;
	}
}

static struct perf_pmu *__perf_pmus__find_by_type(unsigned int type)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &core_pmus, list) {
		if (pmu->type == type)
			return pmu;
	}

	list_for_each_entry(pmu, &other_pmus, list) {
		if (pmu->type == type)
			return pmu;
	}
	return NULL;
}

struct perf_pmu *perf_pmus__find_by_type(unsigned int type)
{
	struct perf_pmu *pmu = __perf_pmus__find_by_type(type);

	if (pmu || read_sysfs_all_pmus)
		return pmu;

	pmu_read_sysfs(/*core_only=*/false);
	pmu = __perf_pmus__find_by_type(type);
	return pmu;
}

/*
 * pmu iterator: If pmu is NULL, we start at the begin, otherwise return the
 * next pmu. Returns NULL on end.
 */
struct perf_pmu *perf_pmus__scan(struct perf_pmu *pmu)
{
	bool use_core_pmus = !pmu || pmu->is_core;

	if (!pmu) {
		pmu_read_sysfs(/*core_only=*/false);
		pmu = list_prepare_entry(pmu, &core_pmus, list);
	}
	if (use_core_pmus) {
		list_for_each_entry_continue(pmu, &core_pmus, list)
			return pmu;

		pmu = NULL;
		pmu = list_prepare_entry(pmu, &other_pmus, list);
	}
	list_for_each_entry_continue(pmu, &other_pmus, list)
		return pmu;
	return NULL;
}

struct perf_pmu *perf_pmus__scan_core(struct perf_pmu *pmu)
{
	if (!pmu) {
		pmu_read_sysfs(/*core_only=*/true);
		pmu = list_prepare_entry(pmu, &core_pmus, list);
	}
	list_for_each_entry_continue(pmu, &core_pmus, list)
		return pmu;

	return NULL;
}

const struct perf_pmu *perf_pmus__pmu_for_pmu_filter(const char *str)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		if (!strcmp(pmu->name, str))
			return pmu;
		/* Ignore "uncore_" prefix. */
		if (!strncmp(pmu->name, "uncore_", 7)) {
			if (!strcmp(pmu->name + 7, str))
				return pmu;
		}
		/* Ignore "cpu_" prefix on Intel hybrid PMUs. */
		if (!strncmp(pmu->name, "cpu_", 4)) {
			if (!strcmp(pmu->name + 4, str))
				return pmu;
		}
	}
	return NULL;
}

int __weak perf_pmus__num_mem_pmus(void)
{
	/* All core PMUs are for mem events. */
	return perf_pmus__num_core_pmus();
}

/** Struct for ordering events as output in perf list. */
struct sevent {
	/** PMU for event. */
	const struct perf_pmu *pmu;
	/**
	 * Optional event for name, desc, etc. If not present then this is a
	 * selectable PMU and the event name is shown as "//".
	 */
	const struct perf_pmu_alias *event;
	/** Is the PMU for the CPU? */
	bool is_cpu;
};

static int cmp_sevent(const void *a, const void *b)
{
	const struct sevent *as = a;
	const struct sevent *bs = b;
	const char *a_pmu_name = NULL, *b_pmu_name = NULL;
	const char *a_name = "//", *a_desc = NULL, *a_topic = "";
	const char *b_name = "//", *b_desc = NULL, *b_topic = "";
	int ret;

	if (as->event) {
		a_name = as->event->name;
		a_desc = as->event->desc;
		a_topic = as->event->topic ?: "";
		a_pmu_name = as->event->pmu_name;
	}
	if (bs->event) {
		b_name = bs->event->name;
		b_desc = bs->event->desc;
		b_topic = bs->event->topic ?: "";
		b_pmu_name = bs->event->pmu_name;
	}
	/* Put extra events last. */
	if (!!a_desc != !!b_desc)
		return !!a_desc - !!b_desc;

	/* Order by topics. */
	ret = strcmp(a_topic, b_topic);
	if (ret)
		return ret;

	/* Order CPU core events to be first */
	if (as->is_cpu != bs->is_cpu)
		return as->is_cpu ? -1 : 1;

	/* Order by PMU name. */
	if (as->pmu != bs->pmu) {
		a_pmu_name = a_pmu_name ?: (as->pmu->name ?: "");
		b_pmu_name = b_pmu_name ?: (bs->pmu->name ?: "");
		ret = strcmp(a_pmu_name, b_pmu_name);
		if (ret)
			return ret;
	}

	/* Order by event name. */
	return strcmp(a_name, b_name);
}

static bool pmu_alias_is_duplicate(struct sevent *alias_a,
				   struct sevent *alias_b)
{
	const char *a_pmu_name = NULL, *b_pmu_name = NULL;
	const char *a_name = "//", *b_name = "//";


	if (alias_a->event) {
		a_name = alias_a->event->name;
		a_pmu_name = alias_a->event->pmu_name;
	}
	if (alias_b->event) {
		b_name = alias_b->event->name;
		b_pmu_name = alias_b->event->pmu_name;
	}

	/* Different names -> never duplicates */
	if (strcmp(a_name, b_name))
		return false;

	/* Don't remove duplicates for different PMUs */
	a_pmu_name = a_pmu_name ?: (alias_a->pmu->name ?: "");
	b_pmu_name = b_pmu_name ?: (alias_b->pmu->name ?: "");
	return strcmp(a_pmu_name, b_pmu_name) == 0;
}

static int sub_non_neg(int a, int b)
{
	if (b > a)
		return 0;
	return a - b;
}

static char *format_alias(char *buf, int len, const struct perf_pmu *pmu,
			  const struct perf_pmu_alias *alias)
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

void perf_pmus__print_pmu_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *event;
	char buf[1024];
	int printed = 0;
	int len, j;
	struct sevent *aliases;

	pmu = NULL;
	len = 0;
	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		list_for_each_entry(event, &pmu->aliases, list)
			len++;
		if (pmu->selectable)
			len++;
	}
	aliases = zalloc(sizeof(struct sevent) * len);
	if (!aliases) {
		pr_err("FATAL: not enough memory to print PMU events\n");
		return;
	}
	pmu = NULL;
	j = 0;
	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		bool is_cpu = pmu->is_core;

		list_for_each_entry(event, &pmu->aliases, list) {
			aliases[j].event = event;
			aliases[j].pmu = pmu;
			aliases[j].is_cpu = is_cpu;
			j++;
		}
		if (pmu->selectable) {
			aliases[j].event = NULL;
			aliases[j].pmu = pmu;
			aliases[j].is_cpu = is_cpu;
			j++;
		}
	}
	len = j;
	qsort(aliases, len, sizeof(struct sevent), cmp_sevent);
	for (j = 0; j < len; j++) {
		const char *name, *alias = NULL, *scale_unit = NULL,
			*desc = NULL, *long_desc = NULL,
			*encoding_desc = NULL, *topic = NULL,
			*pmu_name = NULL;
		bool deprecated = false;
		size_t buf_used;

		/* Skip duplicates */
		if (j > 0 && pmu_alias_is_duplicate(&aliases[j], &aliases[j - 1]))
			continue;

		if (!aliases[j].event) {
			/* A selectable event. */
			pmu_name = aliases[j].pmu->name;
			buf_used = snprintf(buf, sizeof(buf), "%s//", pmu_name) + 1;
			name = buf;
		} else {
			if (aliases[j].event->desc) {
				name = aliases[j].event->name;
				buf_used = 0;
			} else {
				name = format_alias(buf, sizeof(buf), aliases[j].pmu,
						    aliases[j].event);
				if (aliases[j].is_cpu) {
					alias = name;
					name = aliases[j].event->name;
				}
				buf_used = strlen(buf) + 1;
			}
			pmu_name = aliases[j].event->pmu_name ?: (aliases[j].pmu->name ?: "");
			if (strlen(aliases[j].event->unit) || aliases[j].event->scale != 1.0) {
				scale_unit = buf + buf_used;
				buf_used += snprintf(buf + buf_used, sizeof(buf) - buf_used,
						"%G%s", aliases[j].event->scale,
						aliases[j].event->unit) + 1;
			}
			desc = aliases[j].event->desc;
			long_desc = aliases[j].event->long_desc;
			topic = aliases[j].event->topic;
			encoding_desc = buf + buf_used;
			buf_used += snprintf(buf + buf_used, sizeof(buf) - buf_used,
					"%s/%s/", pmu_name, aliases[j].event->str) + 1;
			deprecated = aliases[j].event->deprecated;
		}
		print_cb->print_event(print_state,
				pmu_name,
				topic,
				name,
				alias,
				scale_unit,
				deprecated,
				"Kernel PMU event",
				desc,
				long_desc,
				encoding_desc);
	}
	if (printed && pager_in_use())
		printf("\n");

	zfree(&aliases);
}

bool perf_pmus__have_event(const char *pname, const char *name)
{
	struct perf_pmu *pmu = perf_pmus__find(pname);

	return pmu && perf_pmu__have_event(pmu, name);
}

int perf_pmus__num_core_pmus(void)
{
	static int count;

	if (!count) {
		struct perf_pmu *pmu = NULL;

		while ((pmu = perf_pmus__scan_core(pmu)) != NULL)
			count++;
	}
	return count;
}

static bool __perf_pmus__supports_extended_type(void)
{
	struct perf_pmu *pmu = NULL;

	if (perf_pmus__num_core_pmus() <= 1)
		return false;

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (!is_event_supported(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES | ((__u64)pmu->type << PERF_PMU_TYPE_SHIFT)))
			return false;
	}

	return true;
}

static bool perf_pmus__do_support_extended_type;

static void perf_pmus__init_supports_extended_type(void)
{
	perf_pmus__do_support_extended_type = __perf_pmus__supports_extended_type();
}

bool perf_pmus__supports_extended_type(void)
{
	static pthread_once_t extended_type_once = PTHREAD_ONCE_INIT;

	pthread_once(&extended_type_once, perf_pmus__init_supports_extended_type);

	return perf_pmus__do_support_extended_type;
}

char *perf_pmus__default_pmu_name(void)
{
	int fd;
	DIR *dir;
	struct dirent *dent;
	char *result = NULL;

	if (!list_empty(&core_pmus))
		return strdup(list_first_entry(&core_pmus, struct perf_pmu, list)->name);

	fd = perf_pmu__event_source_devices_fd();
	if (fd < 0)
		return strdup("cpu");

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return strdup("cpu");
	}

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;
		if (is_pmu_core(dent->d_name)) {
			result = strdup(dent->d_name);
			break;
		}
	}

	closedir(dir);
	return result ?: strdup("cpu");
}

struct perf_pmu *evsel__find_pmu(const struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel->pmu;

	if (!pmu) {
		pmu = perf_pmus__find_by_type(evsel->core.attr.type);
		((struct evsel *)evsel)->pmu = pmu;
	}
	return pmu;
}
