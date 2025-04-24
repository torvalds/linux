// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <api/io_dir.h>
#include <subcmd/pager.h>
#include <sys/types.h>
#include <ctype.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "cpumap.h"
#include "debug.h"
#include "evsel.h"
#include "pmus.h"
#include "pmu.h"
#include "hwmon_pmu.h"
#include "tool_pmu.h"
#include "print-events.h"
#include "strbuf.h"

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
enum perf_tool_pmu_type {
	PERF_TOOL_PMU_TYPE_PE_CORE,
	PERF_TOOL_PMU_TYPE_PE_OTHER,
	PERF_TOOL_PMU_TYPE_TOOL,
	PERF_TOOL_PMU_TYPE_HWMON,

#define PERF_TOOL_PMU_TYPE_PE_CORE_MASK (1 << PERF_TOOL_PMU_TYPE_PE_CORE)
#define PERF_TOOL_PMU_TYPE_PE_OTHER_MASK (1 << PERF_TOOL_PMU_TYPE_PE_OTHER)
#define PERF_TOOL_PMU_TYPE_TOOL_MASK (1 << PERF_TOOL_PMU_TYPE_TOOL)
#define PERF_TOOL_PMU_TYPE_HWMON_MASK (1 << PERF_TOOL_PMU_TYPE_HWMON)

#define PERF_TOOL_PMU_TYPE_ALL_MASK (PERF_TOOL_PMU_TYPE_PE_CORE_MASK |	\
					PERF_TOOL_PMU_TYPE_PE_OTHER_MASK | \
					PERF_TOOL_PMU_TYPE_TOOL_MASK |	\
					PERF_TOOL_PMU_TYPE_HWMON_MASK)
};
static unsigned int read_pmu_types;

static void pmu_read_sysfs(unsigned int to_read_pmus);

size_t pmu_name_len_no_suffix(const char *str)
{
	int orig_len, len;
	bool has_hex_digits = false;

	orig_len = len = strlen(str);

	/* Count trailing digits. */
	while (len > 0 && isxdigit(str[len - 1])) {
		if (!isdigit(str[len - 1]))
			has_hex_digits = true;
		len--;
	}

	if (len > 0 && len != orig_len && str[len - 1] == '_') {
		/*
		 * There is a '_{num}' suffix. For decimal suffixes any length
		 * will do, for hexadecimal ensure more than 2 hex digits so
		 * that S390's cpum_cf PMU doesn't match.
		 */
		if (!has_hex_digits || (orig_len - len) > 2)
			return len - 1;
	}
	/* Use the full length. */
	return orig_len;
}

int pmu_name_cmp(const char *lhs_pmu_name, const char *rhs_pmu_name)
{
	unsigned long long lhs_num = 0, rhs_num = 0;
	size_t lhs_pmu_name_len = pmu_name_len_no_suffix(lhs_pmu_name);
	size_t rhs_pmu_name_len = pmu_name_len_no_suffix(rhs_pmu_name);
	int ret = strncmp(lhs_pmu_name, rhs_pmu_name,
			lhs_pmu_name_len < rhs_pmu_name_len ? lhs_pmu_name_len : rhs_pmu_name_len);

	if (lhs_pmu_name_len != rhs_pmu_name_len || ret != 0 || lhs_pmu_name_len == 0)
		return ret;

	if (lhs_pmu_name_len + 1 < strlen(lhs_pmu_name))
		lhs_num = strtoull(&lhs_pmu_name[lhs_pmu_name_len + 1], NULL, 16);
	if (rhs_pmu_name_len + 1 < strlen(rhs_pmu_name))
		rhs_num = strtoull(&rhs_pmu_name[rhs_pmu_name_len + 1], NULL, 16);

	return lhs_num < rhs_num ? -1 : (lhs_num > rhs_num ? 1 : 0);
}

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
	read_pmu_types = 0;
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
	unsigned int to_read_pmus = 0;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	if (read_pmu_types == PERF_TOOL_PMU_TYPE_ALL_MASK)
		return NULL;

	core_pmu = is_pmu_core(name);
	if (core_pmu && (read_pmu_types & PERF_TOOL_PMU_TYPE_PE_CORE_MASK))
		return NULL;

	dirfd = perf_pmu__event_source_devices_fd();
	pmu = perf_pmu__lookup(core_pmu ? &core_pmus : &other_pmus, dirfd, name,
			       /*eager_load=*/false);
	close(dirfd);

	if (pmu)
		return pmu;

	/* Looking up an individual perf event PMU failed, check if a tool PMU should be read. */
	if (!strncmp(name, "hwmon_", 6))
		to_read_pmus |= PERF_TOOL_PMU_TYPE_HWMON_MASK;
	else if (!strcmp(name, "tool"))
		to_read_pmus |= PERF_TOOL_PMU_TYPE_TOOL_MASK;

	if (to_read_pmus) {
		pmu_read_sysfs(to_read_pmus);
		pmu = pmu_find(name);
		if (pmu)
			return pmu;
	}
	/* Read all necessary PMUs from sysfs and see if the PMU is found. */
	to_read_pmus = PERF_TOOL_PMU_TYPE_PE_CORE_MASK;
	if (!core_pmu)
		to_read_pmus |= PERF_TOOL_PMU_TYPE_PE_OTHER_MASK;
	pmu_read_sysfs(to_read_pmus);
	return pmu_find(name);
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

	if (read_pmu_types == PERF_TOOL_PMU_TYPE_ALL_MASK)
		return NULL;

	core_pmu = is_pmu_core(name);
	if (core_pmu && (read_pmu_types & PERF_TOOL_PMU_TYPE_PE_CORE_MASK))
		return NULL;

	return perf_pmu__lookup(core_pmu ? &core_pmus : &other_pmus, dirfd, name,
				/*eager_load=*/false);
}

static int pmus_cmp(void *priv __maybe_unused,
		    const struct list_head *lhs, const struct list_head *rhs)
{
	struct perf_pmu *lhs_pmu = container_of(lhs, struct perf_pmu, list);
	struct perf_pmu *rhs_pmu = container_of(rhs, struct perf_pmu, list);

	return pmu_name_cmp(lhs_pmu->name ?: "", rhs_pmu->name ?: "");
}

/* Add all pmus in sysfs to pmu list: */
static void pmu_read_sysfs(unsigned int to_read_types)
{
	struct perf_pmu *tool_pmu;

	if ((read_pmu_types & to_read_types) == to_read_types) {
		/* All requested PMU types have been read. */
		return;
	}

	if (to_read_types & (PERF_TOOL_PMU_TYPE_PE_CORE_MASK | PERF_TOOL_PMU_TYPE_PE_OTHER_MASK)) {
		int fd = perf_pmu__event_source_devices_fd();
		struct io_dir dir;
		struct io_dirent64 *dent;
		bool core_only = (to_read_types & PERF_TOOL_PMU_TYPE_PE_OTHER_MASK) == 0;

		if (fd < 0)
			goto skip_pe_pmus;

		io_dir__init(&dir, fd);

		while ((dent = io_dir__readdir(&dir)) != NULL) {
			if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
				continue;
			if (core_only && !is_pmu_core(dent->d_name))
				continue;
			/* add to static LIST_HEAD(core_pmus) or LIST_HEAD(other_pmus): */
			perf_pmu__find2(fd, dent->d_name);
		}

		close(fd);
	}
skip_pe_pmus:
	if ((to_read_types & PERF_TOOL_PMU_TYPE_PE_CORE_MASK) && list_empty(&core_pmus)) {
		if (!perf_pmu__create_placeholder_core_pmu(&core_pmus))
			pr_err("Failure to set up any core PMUs\n");
	}
	list_sort(NULL, &core_pmus, pmus_cmp);

	if ((to_read_types & PERF_TOOL_PMU_TYPE_TOOL_MASK) != 0 &&
	    (read_pmu_types & PERF_TOOL_PMU_TYPE_TOOL_MASK) == 0) {
		tool_pmu = tool_pmu__new();
		if (tool_pmu)
			list_add_tail(&tool_pmu->list, &other_pmus);
	}
	if ((to_read_types & PERF_TOOL_PMU_TYPE_HWMON_MASK) != 0 &&
	    (read_pmu_types & PERF_TOOL_PMU_TYPE_HWMON_MASK) == 0)
		perf_pmus__read_hwmon_pmus(&other_pmus);

	list_sort(NULL, &other_pmus, pmus_cmp);

	read_pmu_types |= to_read_types;
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
	unsigned int to_read_pmus;
	struct perf_pmu *pmu = __perf_pmus__find_by_type(type);

	if (pmu || (read_pmu_types == PERF_TOOL_PMU_TYPE_ALL_MASK))
		return pmu;

	if (type >= PERF_PMU_TYPE_PE_START && type <= PERF_PMU_TYPE_PE_END) {
		to_read_pmus = PERF_TOOL_PMU_TYPE_PE_CORE_MASK |
			PERF_TOOL_PMU_TYPE_PE_OTHER_MASK;
	} else if (type >= PERF_PMU_TYPE_HWMON_START && type <= PERF_PMU_TYPE_HWMON_END) {
		to_read_pmus = PERF_TOOL_PMU_TYPE_HWMON_MASK;
	} else {
		to_read_pmus = PERF_TOOL_PMU_TYPE_TOOL_MASK;
	}
	pmu_read_sysfs(to_read_pmus);
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
		pmu_read_sysfs(PERF_TOOL_PMU_TYPE_ALL_MASK);
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
		pmu_read_sysfs(PERF_TOOL_PMU_TYPE_PE_CORE_MASK);
		return list_first_entry_or_null(&core_pmus, typeof(*pmu), list);
	}
	list_for_each_entry_continue(pmu, &core_pmus, list)
		return pmu;

	return NULL;
}

static struct perf_pmu *perf_pmus__scan_skip_duplicates(struct perf_pmu *pmu)
{
	bool use_core_pmus = !pmu || pmu->is_core;
	int last_pmu_name_len = 0;
	const char *last_pmu_name = (pmu && pmu->name) ? pmu->name : "";

	if (!pmu) {
		pmu_read_sysfs(PERF_TOOL_PMU_TYPE_ALL_MASK);
		pmu = list_prepare_entry(pmu, &core_pmus, list);
	} else
		last_pmu_name_len = pmu_name_len_no_suffix(pmu->name ?: "");

	if (use_core_pmus) {
		list_for_each_entry_continue(pmu, &core_pmus, list) {
			int pmu_name_len = pmu_name_len_no_suffix(pmu->name ?: "");

			if (last_pmu_name_len == pmu_name_len &&
			    !strncmp(last_pmu_name, pmu->name ?: "", pmu_name_len))
				continue;

			return pmu;
		}
		pmu = NULL;
		pmu = list_prepare_entry(pmu, &other_pmus, list);
	}
	list_for_each_entry_continue(pmu, &other_pmus, list) {
		int pmu_name_len = pmu_name_len_no_suffix(pmu->name ?: "");

		if (last_pmu_name_len == pmu_name_len &&
		    !strncmp(last_pmu_name, pmu->name ?: "", pmu_name_len))
			continue;

		return pmu;
	}
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

/** Struct for ordering events as output in perf list. */
struct sevent {
	/** PMU for event. */
	const struct perf_pmu *pmu;
	const char *name;
	const char* alias;
	const char *scale_unit;
	const char *desc;
	const char *long_desc;
	const char *encoding_desc;
	const char *topic;
	const char *pmu_name;
	const char *event_type_desc;
	bool deprecated;
};

static int cmp_sevent(const void *a, const void *b)
{
	const struct sevent *as = a;
	const struct sevent *bs = b;
	bool a_iscpu, b_iscpu;
	int ret;

	/* Put extra events last. */
	if (!!as->desc != !!bs->desc)
		return !!as->desc - !!bs->desc;

	/* Order by topics. */
	ret = strcmp(as->topic ?: "", bs->topic ?: "");
	if (ret)
		return ret;

	/* Order CPU core events to be first */
	a_iscpu = as->pmu ? as->pmu->is_core : true;
	b_iscpu = bs->pmu ? bs->pmu->is_core : true;
	if (a_iscpu != b_iscpu)
		return a_iscpu ? -1 : 1;

	/* Order by PMU name. */
	if (as->pmu != bs->pmu) {
		ret = strcmp(as->pmu_name ?: "", bs->pmu_name ?: "");
		if (ret)
			return ret;
	}

	/* Order by event name. */
	return strcmp(as->name, bs->name);
}

static bool pmu_alias_is_duplicate(struct sevent *a, struct sevent *b)
{
	/* Different names -> never duplicates */
	if (strcmp(a->name ?: "//", b->name ?: "//"))
		return false;

	/* Don't remove duplicates for different PMUs */
	return strcmp(a->pmu_name, b->pmu_name) == 0;
}

struct events_callback_state {
	struct sevent *aliases;
	size_t aliases_len;
	size_t index;
};

static int perf_pmus__print_pmu_events__callback(void *vstate,
						struct pmu_event_info *info)
{
	struct events_callback_state *state = vstate;
	struct sevent *s;

	if (state->index >= state->aliases_len) {
		pr_err("Unexpected event %s/%s/\n", info->pmu->name, info->name);
		return 1;
	}
	assert(info->pmu != NULL || info->name != NULL);
	s = &state->aliases[state->index];
	s->pmu = info->pmu;
#define COPY_STR(str) s->str = info->str ? strdup(info->str) : NULL
	COPY_STR(name);
	COPY_STR(alias);
	COPY_STR(scale_unit);
	COPY_STR(desc);
	COPY_STR(long_desc);
	COPY_STR(encoding_desc);
	COPY_STR(topic);
	COPY_STR(pmu_name);
	COPY_STR(event_type_desc);
#undef COPY_STR
	s->deprecated = info->deprecated;
	state->index++;
	return 0;
}

void perf_pmus__print_pmu_events(const struct print_callbacks *print_cb, void *print_state)
{
	struct perf_pmu *pmu;
	int printed = 0;
	int len;
	struct sevent *aliases;
	struct events_callback_state state;
	bool skip_duplicate_pmus = print_cb->skip_duplicate_pmus(print_state);
	struct perf_pmu *(*scan_fn)(struct perf_pmu *);

	if (skip_duplicate_pmus)
		scan_fn = perf_pmus__scan_skip_duplicates;
	else
		scan_fn = perf_pmus__scan;

	pmu = NULL;
	len = 0;
	while ((pmu = scan_fn(pmu)) != NULL)
		len += perf_pmu__num_events(pmu);

	aliases = zalloc(sizeof(struct sevent) * len);
	if (!aliases) {
		pr_err("FATAL: not enough memory to print PMU events\n");
		return;
	}
	pmu = NULL;
	state = (struct events_callback_state) {
		.aliases = aliases,
		.aliases_len = len,
		.index = 0,
	};
	while ((pmu = scan_fn(pmu)) != NULL) {
		perf_pmu__for_each_event(pmu, skip_duplicate_pmus, &state,
					 perf_pmus__print_pmu_events__callback);
	}
	qsort(aliases, len, sizeof(struct sevent), cmp_sevent);
	for (int j = 0; j < len; j++) {
		/* Skip duplicates */
		if (j < len - 1 && pmu_alias_is_duplicate(&aliases[j], &aliases[j + 1]))
			goto free;

		print_cb->print_event(print_state,
				aliases[j].topic,
				aliases[j].pmu_name,
				aliases[j].name,
				aliases[j].alias,
				aliases[j].scale_unit,
				aliases[j].deprecated,
				aliases[j].event_type_desc,
				aliases[j].desc,
				aliases[j].long_desc,
				aliases[j].encoding_desc);
free:
		zfree(&aliases[j].name);
		zfree(&aliases[j].alias);
		zfree(&aliases[j].scale_unit);
		zfree(&aliases[j].desc);
		zfree(&aliases[j].long_desc);
		zfree(&aliases[j].encoding_desc);
		zfree(&aliases[j].topic);
		zfree(&aliases[j].pmu_name);
		zfree(&aliases[j].event_type_desc);
	}
	if (printed && pager_in_use())
		printf("\n");

	zfree(&aliases);
}

struct build_format_string_args {
	struct strbuf short_string;
	struct strbuf long_string;
	int num_formats;
};

static int build_format_string(void *state, const char *name, int config,
			       const unsigned long *bits)
{
	struct build_format_string_args *args = state;
	unsigned int num_bits;
	int ret1, ret2 = 0;

	(void)config;
	args->num_formats++;
	if (args->num_formats > 1) {
		strbuf_addch(&args->long_string, ',');
		if (args->num_formats < 4)
			strbuf_addch(&args->short_string, ',');
	}
	num_bits = bits ? bitmap_weight(bits, PERF_PMU_FORMAT_BITS) : 0;
	if (num_bits <= 1) {
		ret1 = strbuf_addf(&args->long_string, "%s", name);
		if (args->num_formats < 4)
			ret2 = strbuf_addf(&args->short_string, "%s", name);
	} else if (num_bits > 8) {
		ret1 = strbuf_addf(&args->long_string, "%s=0..0x%llx", name,
				   ULLONG_MAX >> (64 - num_bits));
		if (args->num_formats < 4) {
			ret2 = strbuf_addf(&args->short_string, "%s=0..0x%llx", name,
					   ULLONG_MAX >> (64 - num_bits));
		}
	} else {
		ret1 = strbuf_addf(&args->long_string, "%s=0..%llu", name,
				  ULLONG_MAX >> (64 - num_bits));
		if (args->num_formats < 4) {
			ret2 = strbuf_addf(&args->short_string, "%s=0..%llu", name,
					   ULLONG_MAX >> (64 - num_bits));
		}
	}
	return ret1 < 0 ? ret1 : (ret2 < 0 ? ret2 : 0);
}

void perf_pmus__print_raw_pmu_events(const struct print_callbacks *print_cb, void *print_state)
{
	bool skip_duplicate_pmus = print_cb->skip_duplicate_pmus(print_state);
	struct perf_pmu *(*scan_fn)(struct perf_pmu *);
	struct perf_pmu *pmu = NULL;

	if (skip_duplicate_pmus)
		scan_fn = perf_pmus__scan_skip_duplicates;
	else
		scan_fn = perf_pmus__scan;

	while ((pmu = scan_fn(pmu)) != NULL) {
		struct build_format_string_args format_args = {
			.short_string = STRBUF_INIT,
			.long_string = STRBUF_INIT,
			.num_formats = 0,
		};
		int len = pmu_name_len_no_suffix(pmu->name);
		const char *desc = "(see 'man perf-list' or 'man perf-record' on how to encode it)";

		if (!pmu->is_core)
			desc = NULL;

		strbuf_addf(&format_args.short_string, "%.*s/", len, pmu->name);
		strbuf_addf(&format_args.long_string, "%.*s/", len, pmu->name);
		perf_pmu__for_each_format(pmu, &format_args, build_format_string);

		if (format_args.num_formats > 3)
			strbuf_addf(&format_args.short_string, ",.../modifier");
		else
			strbuf_addf(&format_args.short_string, "/modifier");

		strbuf_addf(&format_args.long_string, "/modifier");
		print_cb->print_event(print_state,
				/*topic=*/NULL,
				/*pmu_name=*/NULL,
				format_args.short_string.buf,
				/*event_alias=*/NULL,
				/*scale_unit=*/NULL,
				/*deprecated=*/false,
				"Raw event descriptor",
				desc,
				/*long_desc=*/NULL,
				format_args.long_string.buf);

		strbuf_release(&format_args.short_string);
		strbuf_release(&format_args.long_string);
	}
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

struct perf_pmu *evsel__find_pmu(const struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel->pmu;
	bool legacy_core_type;

	if (pmu)
		return pmu;

	pmu = perf_pmus__find_by_type(evsel->core.attr.type);
	legacy_core_type =
		evsel->core.attr.type == PERF_TYPE_HARDWARE ||
		evsel->core.attr.type == PERF_TYPE_HW_CACHE;
	if (!pmu && legacy_core_type) {
		if (perf_pmus__supports_extended_type()) {
			u32 type = evsel->core.attr.config >> PERF_PMU_TYPE_SHIFT;

			pmu = perf_pmus__find_by_type(type);
		} else {
			pmu = perf_pmus__find_core_pmu();
		}
	}
	((struct evsel *)evsel)->pmu = pmu;
	return pmu;
}

struct perf_pmu *perf_pmus__find_core_pmu(void)
{
	return perf_pmus__scan_core(NULL);
}

struct perf_pmu *perf_pmus__add_test_pmu(int test_sysfs_dirfd, const char *name)
{
	/*
	 * Some PMU functions read from the sysfs mount point, so care is
	 * needed, hence passing the eager_load flag to load things like the
	 * format files.
	 */
	return perf_pmu__lookup(&other_pmus, test_sysfs_dirfd, name, /*eager_load=*/true);
}

struct perf_pmu *perf_pmus__add_test_hwmon_pmu(int hwmon_dir,
					       const char *sysfs_name,
					       const char *name)
{
	return hwmon_pmu__new(&other_pmus, hwmon_dir, sysfs_name, name);
}

struct perf_pmu *perf_pmus__fake_pmu(void)
{
	static struct perf_pmu fake = {
		.name = "fake",
		.type = PERF_PMU_TYPE_FAKE,
		.format = LIST_HEAD_INIT(fake.format),
	};

	return &fake;
}
