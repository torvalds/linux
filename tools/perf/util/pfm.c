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
#include "util/pmus.h"
#include "util/pfm.h"
#include "util/strbuf.h"
#include "util/thread_map.h"

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

		pmu = perf_pmus__find_by_type((unsigned int)attr.type);
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

static bool is_libpfm_event_supported(const char *name, struct perf_cpu_map *cpus,
				      struct perf_thread_map *threads)
{
	struct perf_pmu *pmu;
	struct evsel *evsel;
	struct perf_event_attr attr = {};
	bool result = true;
	int ret;

	ret = pfm_get_perf_event_encoding(name, PFM_PLM0|PFM_PLM3,
					  &attr, NULL, NULL);
	if (ret != PFM_SUCCESS)
		return false;

	pmu = perf_pmus__find_by_type((unsigned int)attr.type);
	evsel = parse_events__add_event(0, &attr, name, /*metric_id=*/NULL, pmu);
	if (evsel == NULL)
		return false;

	evsel->is_libpfm_event = true;

	ret = evsel__open(evsel, cpus, threads);
	if (ret == -EACCES) {
		/*
		 * This happens if the paranoid value
		 * /proc/sys/kernel/perf_event_paranoid is set to 2
		 * Re-run with exclude_kernel set; we don't do that
		 * by default as some ARM machines do not support it.
		 *
		 */
		evsel->core.attr.exclude_kernel = 1;
		ret = evsel__open(evsel, cpus, threads);

	}
	if (ret < 0)
		result = false;

	evsel__close(evsel);
	evsel__delete(evsel);

	return result;
}

static const char *srcs[PFM_ATTR_CTRL_MAX] = {
	[PFM_ATTR_CTRL_UNKNOWN] = "???",
	[PFM_ATTR_CTRL_PMU] = "PMU",
	[PFM_ATTR_CTRL_PERF_EVENT] = "perf_event",
};

static void
print_attr_flags(struct strbuf *buf, const pfm_event_attr_info_t *info)
{
	if (info->is_dfl)
		strbuf_addf(buf, "[default] ");

	if (info->is_precise)
		strbuf_addf(buf, "[precise] ");
}

static void
print_libpfm_event(const struct print_callbacks *print_cb, void *print_state,
		const pfm_pmu_info_t *pinfo, const pfm_event_info_t *info,
		struct strbuf *buf)
{
	int j, ret;
	char topic[80], name[80];
	struct perf_cpu_map *cpus = perf_cpu_map__empty_new(1);
	struct perf_thread_map *threads = thread_map__new_by_tid(0);

	strbuf_setlen(buf, 0);
	snprintf(topic, sizeof(topic), "pfm %s", pinfo->name);

	snprintf(name, sizeof(name), "%s::%s", pinfo->name, info->name);
	strbuf_addf(buf, "Code: 0x%"PRIx64"\n", info->code);

	pfm_for_each_event_attr(j, info) {
		pfm_event_attr_info_t ainfo;
		const char *src;

		ainfo.size = sizeof(ainfo);
		ret = pfm_get_event_attr_info(info->idx, j, PFM_OS_PERF_EVENT_EXT, &ainfo);
		if (ret != PFM_SUCCESS)
			continue;

		if (ainfo.ctrl >= PFM_ATTR_CTRL_MAX)
			ainfo.ctrl = PFM_ATTR_CTRL_UNKNOWN;

		src = srcs[ainfo.ctrl];
		switch (ainfo.type) {
		case PFM_ATTR_UMASK: /* Ignore for now */
			break;
		case PFM_ATTR_MOD_BOOL:
			strbuf_addf(buf, " Modif: %s: [%s] : %s (boolean)\n", src,
				    ainfo.name, ainfo.desc);
			break;
		case PFM_ATTR_MOD_INTEGER:
			strbuf_addf(buf, " Modif: %s: [%s] : %s (integer)\n", src,
				    ainfo.name, ainfo.desc);
			break;
		case PFM_ATTR_NONE:
		case PFM_ATTR_RAW_UMASK:
		case PFM_ATTR_MAX:
		default:
			strbuf_addf(buf, " Attr: %s: [%s] : %s\n", src,
				    ainfo.name, ainfo.desc);
		}
	}

	if (is_libpfm_event_supported(name, cpus, threads)) {
		print_cb->print_event(print_state, topic, pinfo->name,
				      /*pmu_type=*/PERF_TYPE_RAW,
				      name, info->equiv,
				      /*scale_unit=*/NULL,
				      /*deprecated=*/NULL, "PFM event",
				      info->desc, /*long_desc=*/NULL,
				      /*encoding_desc=*/buf->buf);
	}

	pfm_for_each_event_attr(j, info) {
		pfm_event_attr_info_t ainfo;
		const char *src;

		strbuf_setlen(buf, 0);

		ainfo.size = sizeof(ainfo);
		ret = pfm_get_event_attr_info(info->idx, j, PFM_OS_PERF_EVENT_EXT, &ainfo);
		if (ret != PFM_SUCCESS)
			continue;

		if (ainfo.ctrl >= PFM_ATTR_CTRL_MAX)
			ainfo.ctrl = PFM_ATTR_CTRL_UNKNOWN;

		src = srcs[ainfo.ctrl];
		if (ainfo.type == PFM_ATTR_UMASK) {
			strbuf_addf(buf, "Umask: 0x%02"PRIx64" : %s: ",
				ainfo.code, src);
			print_attr_flags(buf, &ainfo);
			snprintf(name, sizeof(name), "%s::%s:%s",
				 pinfo->name, info->name, ainfo.name);

			if (!is_libpfm_event_supported(name, cpus, threads))
				continue;

			print_cb->print_event(print_state,
					topic,
					pinfo->name,
					/*pmu_type=*/PERF_TYPE_RAW,
					name, /*alias=*/NULL,
					/*scale_unit=*/NULL,
					/*deprecated=*/NULL, "PFM event",
					ainfo.desc, /*long_desc=*/NULL,
					/*encoding_desc=*/buf->buf);
		}
	}

	perf_cpu_map__put(cpus);
	perf_thread_map__put(threads);
}

void print_libpfm_events(const struct print_callbacks *print_cb, void *print_state)
{
	pfm_event_info_t info;
	pfm_pmu_info_t pinfo;
	int p, ret;
	struct strbuf storage;

	libpfm_initialize();

	/* initialize to zero to indicate ABI version */
	info.size  = sizeof(info);
	pinfo.size = sizeof(pinfo);

	strbuf_init(&storage, 2048);

	pfm_for_all_pmus(p) {
		ret = pfm_get_pmu_info(p, &pinfo);
		if (ret != PFM_SUCCESS)
			continue;

		/* only print events that are supported by host HW */
		if (!pinfo.is_present)
			continue;

		/* handled by perf directly */
		if (pinfo.pmu == PFM_PMU_PERF_EVENT)
			continue;

		for (int i = pinfo.first_event; i != -1; i = pfm_get_event_next(i)) {
			ret = pfm_get_event_info(i, PFM_OS_PERF_EVENT_EXT,
						&info);
			if (ret != PFM_SUCCESS)
				continue;

			print_libpfm_event(print_cb, print_state, &pinfo, &info, &storage);
		}
	}
	strbuf_release(&storage);
}
