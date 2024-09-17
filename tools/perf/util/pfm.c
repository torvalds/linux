// SPDX-License-Identifier: GPL-2.0
/*
 * Support for libpfm4 event encoding.
 *
 * Copyright 2020 Google LLC.
 */
#include "util/cpumap.h"
#include "util/debug.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/parse-events.h"
#include "util/pmu.h"
#include "util/pfm.h"

#include <string.h>
#include <linux/kernel.h>
#include <perfmon/pfmlib_perf_event.h>

static void libpfm_initialize(void)
{
	int ret;

	ret = pfm_initialize();
	if (ret != PFM_SUCCESS) {
		ui__warning("libpfm failed to initialize: %s\n",
			pfm_strerror(ret));
	}
}

int parse_libpfm_events_option(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	struct evlist *evlist = *(struct evlist **)opt->value;
	struct perf_event_attr attr;
	struct perf_pmu *pmu;
	struct evsel *evsel, *grp_leader = NULL;
	char *p, *q, *p_orig;
	const char *sep;
	int grp_evt = -1;
	int ret;

	libpfm_initialize();

	p_orig = p = strdup(str);
	if (!p)
		return -1;
	/*
	 * force loading of the PMU list
	 */
	perf_pmu__scan(NULL);

	for (q = p; strsep(&p, ",{}"); q = p) {
		sep = p ? str + (p - p_orig - 1) : "";
		if (*sep == '{') {
			if (grp_evt > -1) {
				ui__error(
					"nested event groups not supported\n");
				goto error;
			}
			grp_evt++;
		}

		/* no event */
		if (*q == '\0') {
			if (*sep == '}') {
				if (grp_evt < 0) {
					ui__error("cannot close a non-existing event group\n");
					goto error;
				}
				grp_evt--;
			}
			continue;
		}

		memset(&attr, 0, sizeof(attr));
		event_attr_init(&attr);

		ret = pfm_get_perf_event_encoding(q, PFM_PLM0|PFM_PLM3,
						&attr, NULL, NULL);

		if (ret != PFM_SUCCESS) {
			ui__error("failed to parse event %s : %s\n", str,
				  pfm_strerror(ret));
			goto error;
		}

		pmu = perf_pmu__find_by_type((unsigned int)attr.type);
		evsel = parse_events__add_event(evlist->core.nr_entries,
						&attr, q, /*metric_id=*/NULL,
						pmu);
		if (evsel == NULL)
			goto error;

		evsel->is_libpfm_event = true;

		evlist__add(evlist, evsel);

		if (grp_evt == 0)
			grp_leader = evsel;

		if (grp_evt > -1) {
			evsel__set_leader(evsel, grp_leader);
			grp_leader->core.nr_members++;
			grp_evt++;
		}

		if (*sep == '}') {
			if (grp_evt < 0) {
				ui__error(
				   "cannot close a non-existing event group\n");
				goto error;
			}
			evlist->core.nr_groups++;
			grp_leader = NULL;
			grp_evt = -1;
		}
	}
	free(p_orig);
	return 0;
error:
	free(p_orig);
	return -1;
}

static const char *srcs[PFM_ATTR_CTRL_MAX] = {
	[PFM_ATTR_CTRL_UNKNOWN] = "???",
	[PFM_ATTR_CTRL_PMU] = "PMU",
	[PFM_ATTR_CTRL_PERF_EVENT] = "perf_event",
};

static void
print_attr_flags(pfm_event_attr_info_t *info)
{
	int n = 0;

	if (info->is_dfl) {
		printf("[default] ");
		n++;
	}

	if (info->is_precise) {
		printf("[precise] ");
		n++;
	}

	if (!n)
		printf("- ");
}

static void
print_libpfm_events_detailed(pfm_event_info_t *info, bool long_desc)
{
	pfm_event_attr_info_t ainfo;
	const char *src;
	int j, ret;

	ainfo.size = sizeof(ainfo);

	printf("  %s\n", info->name);
	printf("    [%s]\n", info->desc);
	if (long_desc) {
		if (info->equiv)
			printf("      Equiv: %s\n", info->equiv);

		printf("      Code  : 0x%"PRIx64"\n", info->code);
	}
	pfm_for_each_event_attr(j, info) {
		ret = pfm_get_event_attr_info(info->idx, j,
					      PFM_OS_PERF_EVENT_EXT, &ainfo);
		if (ret != PFM_SUCCESS)
			continue;

		if (ainfo.type == PFM_ATTR_UMASK) {
			printf("      %s:%s\n", info->name, ainfo.name);
			printf("        [%s]\n", ainfo.desc);
		}

		if (!long_desc)
			continue;

		if (ainfo.ctrl >= PFM_ATTR_CTRL_MAX)
			ainfo.ctrl = PFM_ATTR_CTRL_UNKNOWN;

		src = srcs[ainfo.ctrl];
		switch (ainfo.type) {
		case PFM_ATTR_UMASK:
			printf("        Umask : 0x%02"PRIx64" : %s: ",
				ainfo.code, src);
			print_attr_flags(&ainfo);
			putchar('\n');
			break;
		case PFM_ATTR_MOD_BOOL:
			printf("      Modif : %s: [%s] : %s (boolean)\n", src,
				ainfo.name, ainfo.desc);
			break;
		case PFM_ATTR_MOD_INTEGER:
			printf("      Modif : %s: [%s] : %s (integer)\n", src,
				ainfo.name, ainfo.desc);
			break;
		case PFM_ATTR_NONE:
		case PFM_ATTR_RAW_UMASK:
		case PFM_ATTR_MAX:
		default:
			printf("      Attr  : %s: [%s] : %s\n", src,
				ainfo.name, ainfo.desc);
		}
	}
}

/*
 * list all pmu::event:umask, pmu::event
 * printed events may not be all valid combinations of umask for an event
 */
static void
print_libpfm_events_raw(pfm_pmu_info_t *pinfo, pfm_event_info_t *info)
{
	pfm_event_attr_info_t ainfo;
	int j, ret;
	bool has_umask = false;

	ainfo.size = sizeof(ainfo);

	pfm_for_each_event_attr(j, info) {
		ret = pfm_get_event_attr_info(info->idx, j,
					      PFM_OS_PERF_EVENT_EXT, &ainfo);
		if (ret != PFM_SUCCESS)
			continue;

		if (ainfo.type != PFM_ATTR_UMASK)
			continue;

		printf("%s::%s:%s\n", pinfo->name, info->name, ainfo.name);
		has_umask = true;
	}
	if (!has_umask)
		printf("%s::%s\n", pinfo->name, info->name);
}

void print_libpfm_events(bool name_only, bool long_desc)
{
	pfm_event_info_t info;
	pfm_pmu_info_t pinfo;
	int i, p, ret;

	libpfm_initialize();

	/* initialize to zero to indicate ABI version */
	info.size  = sizeof(info);
	pinfo.size = sizeof(pinfo);

	if (!name_only)
		puts("\nList of pre-defined events (to be used in --pfm-events):\n");

	pfm_for_all_pmus(p) {
		bool printed_pmu = false;

		ret = pfm_get_pmu_info(p, &pinfo);
		if (ret != PFM_SUCCESS)
			continue;

		/* only print events that are supported by host HW */
		if (!pinfo.is_present)
			continue;

		/* handled by perf directly */
		if (pinfo.pmu == PFM_PMU_PERF_EVENT)
			continue;

		for (i = pinfo.first_event; i != -1;
		     i = pfm_get_event_next(i)) {

			ret = pfm_get_event_info(i, PFM_OS_PERF_EVENT_EXT,
						&info);
			if (ret != PFM_SUCCESS)
				continue;

			if (!name_only && !printed_pmu) {
				printf("%s:\n", pinfo.name);
				printed_pmu = true;
			}

			if (!name_only)
				print_libpfm_events_detailed(&info, long_desc);
			else
				print_libpfm_events_raw(&pinfo, &info);
		}
		if (!name_only && printed_pmu)
			putchar('\n');
	}
}
