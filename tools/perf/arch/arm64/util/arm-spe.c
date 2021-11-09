// SPDX-License-Identifier: GPL-2.0
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/zalloc.h>
#include <time.h>

#include "../../../util/cpumap.h"
#include "../../../util/event.h"
#include "../../../util/evsel.h"
#include "../../../util/evsel_config.h"
#include "../../../util/evlist.h"
#include "../../../util/session.h"
#include <internal/lib.h> // page_size
#include "../../../util/pmu.h"
#include "../../../util/debug.h"
#include "../../../util/auxtrace.h"
#include "../../../util/record.h"
#include "../../../util/arm-spe.h"

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

struct arm_spe_recording {
	struct auxtrace_record		itr;
	struct perf_pmu			*arm_spe_pmu;
	struct evlist		*evlist;
};

static void arm_spe_set_timestamp(struct auxtrace_record *itr,
				  struct evsel *evsel)
{
	struct arm_spe_recording *ptr;
	struct perf_pmu *arm_spe_pmu;
	struct evsel_config_term *term = evsel__get_config_term(evsel, CFG_CHG);
	u64 user_bits = 0, bit;

	ptr = container_of(itr, struct arm_spe_recording, itr);
	arm_spe_pmu = ptr->arm_spe_pmu;

	if (term)
		user_bits = term->val.cfg_chg;

	bit = perf_pmu__format_bits(&arm_spe_pmu->format, "ts_enable");

	/* Skip if user has set it */
	if (bit & user_bits)
		return;

	evsel->core.attr.config |= bit;
}

static size_t
arm_spe_info_priv_size(struct auxtrace_record *itr __maybe_unused,
		       struct evlist *evlist __maybe_unused)
{
	return ARM_SPE_AUXTRACE_PRIV_SIZE;
}

static int arm_spe_info_fill(struct auxtrace_record *itr,
			     struct perf_session *session,
			     struct perf_record_auxtrace_info *auxtrace_info,
			     size_t priv_size)
{
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);
	struct perf_pmu *arm_spe_pmu = sper->arm_spe_pmu;

	if (priv_size != ARM_SPE_AUXTRACE_PRIV_SIZE)
		return -EINVAL;

	if (!session->evlist->core.nr_mmaps)
		return -EINVAL;

	auxtrace_info->type = PERF_AUXTRACE_ARM_SPE;
	auxtrace_info->priv[ARM_SPE_PMU_TYPE] = arm_spe_pmu->type;

	return 0;
}

static void
arm_spe_snapshot_resolve_auxtrace_defaults(struct record_opts *opts,
					   bool privileged)
{
	/*
	 * The default snapshot size is the auxtrace mmap size. If neither auxtrace mmap size nor
	 * snapshot size is specified, then the default is 4MiB for privileged users, 128KiB for
	 * unprivileged users.
	 *
	 * The default auxtrace mmap size is 4MiB/page_size for privileged users, 128KiB for
	 * unprivileged users. If an unprivileged user does not specify mmap pages, the mmap pages
	 * will be reduced from the default 512KiB/page_size to 256KiB/page_size, otherwise the
	 * user is likely to get an error as they exceed their mlock limmit.
	 */

	/*
	 * No size were given to '-S' or '-m,', so go with the default
	 */
	if (!opts->auxtrace_snapshot_size && !opts->auxtrace_mmap_pages) {
		if (privileged) {
			opts->auxtrace_mmap_pages = MiB(4) / page_size;
		} else {
			opts->auxtrace_mmap_pages = KiB(128) / page_size;
			if (opts->mmap_pages == UINT_MAX)
				opts->mmap_pages = KiB(256) / page_size;
		}
	} else if (!opts->auxtrace_mmap_pages && !privileged && opts->mmap_pages == UINT_MAX) {
		opts->mmap_pages = KiB(256) / page_size;
	}

	/*
	 * '-m,xyz' was specified but no snapshot size, so make the snapshot size as big as the
	 * auxtrace mmap area.
	 */
	if (!opts->auxtrace_snapshot_size)
		opts->auxtrace_snapshot_size = opts->auxtrace_mmap_pages * (size_t)page_size;

	/*
	 * '-Sxyz' was specified but no auxtrace mmap area, so make the auxtrace mmap area big
	 * enough to fit the requested snapshot size.
	 */
	if (!opts->auxtrace_mmap_pages) {
		size_t sz = opts->auxtrace_snapshot_size;

		sz = round_up(sz, page_size) / page_size;
		opts->auxtrace_mmap_pages = roundup_pow_of_two(sz);
	}
}

static int arm_spe_recording_options(struct auxtrace_record *itr,
				     struct evlist *evlist,
				     struct record_opts *opts)
{
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);
	struct perf_pmu *arm_spe_pmu = sper->arm_spe_pmu;
	struct evsel *evsel, *arm_spe_evsel = NULL;
	struct perf_cpu_map *cpus = evlist->core.cpus;
	bool privileged = perf_event_paranoid_check(-1);
	struct evsel *tracking_evsel;
	int err;

	sper->evlist = evlist;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == arm_spe_pmu->type) {
			if (arm_spe_evsel) {
				pr_err("There may be only one " ARM_SPE_PMU_NAME "x event\n");
				return -EINVAL;
			}
			evsel->core.attr.freq = 0;
			evsel->core.attr.sample_period = 1;
			arm_spe_evsel = evsel;
			opts->full_auxtrace = true;
		}
	}

	if (!opts->full_auxtrace)
		return 0;

	/*
	 * we are in snapshot mode.
	 */
	if (opts->auxtrace_snapshot_mode) {
		/*
		 * Command arguments '-Sxyz' and/or '-m,xyz' are missing, so fill those in with
		 * default values.
		 */
		if (!opts->auxtrace_snapshot_size || !opts->auxtrace_mmap_pages)
			arm_spe_snapshot_resolve_auxtrace_defaults(opts, privileged);

		/*
		 * Snapshot size can't be bigger than the auxtrace area.
		 */
		if (opts->auxtrace_snapshot_size > opts->auxtrace_mmap_pages * (size_t)page_size) {
			pr_err("Snapshot size %zu must not be greater than AUX area tracing mmap size %zu\n",
			       opts->auxtrace_snapshot_size,
			       opts->auxtrace_mmap_pages * (size_t)page_size);
			return -EINVAL;
		}

		/*
		 * Something went wrong somewhere - this shouldn't happen.
		 */
		if (!opts->auxtrace_snapshot_size || !opts->auxtrace_mmap_pages) {
			pr_err("Failed to calculate default snapshot size and/or AUX area tracing mmap pages\n");
			return -EINVAL;
		}
	}

	/* We are in full trace mode but '-m,xyz' wasn't specified */
	if (!opts->auxtrace_mmap_pages) {
		if (privileged) {
			opts->auxtrace_mmap_pages = MiB(4) / page_size;
		} else {
			opts->auxtrace_mmap_pages = KiB(128) / page_size;
			if (opts->mmap_pages == UINT_MAX)
				opts->mmap_pages = KiB(256) / page_size;
		}
	}

	/* Validate auxtrace_mmap_pages */
	if (opts->auxtrace_mmap_pages) {
		size_t sz = opts->auxtrace_mmap_pages * (size_t)page_size;
		size_t min_sz = KiB(8);

		if (sz < min_sz || !is_power_of_2(sz)) {
			pr_err("Invalid mmap size for ARM SPE: must be at least %zuKiB and a power of 2\n",
			       min_sz / 1024);
			return -EINVAL;
		}
	}

	if (opts->auxtrace_snapshot_mode)
		pr_debug2("%sx snapshot size: %zu\n", ARM_SPE_PMU_NAME,
			  opts->auxtrace_snapshot_size);

	/*
	 * To obtain the auxtrace buffer file descriptor, the auxtrace event
	 * must come first.
	 */
	evlist__to_front(evlist, arm_spe_evsel);

	/*
	 * In the case of per-cpu mmaps, sample CPU for AUX event;
	 * also enable the timestamp tracing for samples correlation.
	 */
	if (!perf_cpu_map__empty(cpus)) {
		evsel__set_sample_bit(arm_spe_evsel, CPU);
		arm_spe_set_timestamp(itr, arm_spe_evsel);
	}

	/* Add dummy event to keep tracking */
	err = parse_events(evlist, "dummy:u", NULL);
	if (err)
		return err;

	tracking_evsel = evlist__last(evlist);
	evlist__set_tracking_event(evlist, tracking_evsel);

	tracking_evsel->core.attr.freq = 0;
	tracking_evsel->core.attr.sample_period = 1;

	/* In per-cpu case, always need the time of mmap events etc */
	if (!perf_cpu_map__empty(cpus))
		evsel__set_sample_bit(tracking_evsel, TIME);

	return 0;
}

static int arm_spe_parse_snapshot_options(struct auxtrace_record *itr __maybe_unused,
					 struct record_opts *opts,
					 const char *str)
{
	unsigned long long snapshot_size = 0;
	char *endptr;

	if (str) {
		snapshot_size = strtoull(str, &endptr, 0);
		if (*endptr || snapshot_size > SIZE_MAX)
			return -1;
	}

	opts->auxtrace_snapshot_mode = true;
	opts->auxtrace_snapshot_size = snapshot_size;

	return 0;
}

static int arm_spe_snapshot_start(struct auxtrace_record *itr)
{
	struct arm_spe_recording *ptr =
			container_of(itr, struct arm_spe_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->arm_spe_pmu->type)
			return evsel__disable(evsel);
	}
	return -EINVAL;
}

static int arm_spe_snapshot_finish(struct auxtrace_record *itr)
{
	struct arm_spe_recording *ptr =
			container_of(itr, struct arm_spe_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->arm_spe_pmu->type)
			return evsel__enable(evsel);
	}
	return -EINVAL;
}

static u64 arm_spe_reference(struct auxtrace_record *itr __maybe_unused)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

	return ts.tv_sec ^ ts.tv_nsec;
}

static void arm_spe_recording_free(struct auxtrace_record *itr)
{
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);

	free(sper);
}

struct auxtrace_record *arm_spe_recording_init(int *err,
					       struct perf_pmu *arm_spe_pmu)
{
	struct arm_spe_recording *sper;

	if (!arm_spe_pmu) {
		*err = -ENODEV;
		return NULL;
	}

	sper = zalloc(sizeof(struct arm_spe_recording));
	if (!sper) {
		*err = -ENOMEM;
		return NULL;
	}

	sper->arm_spe_pmu = arm_spe_pmu;
	sper->itr.pmu = arm_spe_pmu;
	sper->itr.snapshot_start = arm_spe_snapshot_start;
	sper->itr.snapshot_finish = arm_spe_snapshot_finish;
	sper->itr.parse_snapshot_options = arm_spe_parse_snapshot_options;
	sper->itr.recording_options = arm_spe_recording_options;
	sper->itr.info_priv_size = arm_spe_info_priv_size;
	sper->itr.info_fill = arm_spe_info_fill;
	sper->itr.free = arm_spe_recording_free;
	sper->itr.reference = arm_spe_reference;
	sper->itr.read_finish = auxtrace_record__read_finish;
	sper->itr.alignment = 0;

	*err = 0;
	return &sper->itr;
}

struct perf_event_attr
*arm_spe_pmu_default_config(struct perf_pmu *arm_spe_pmu)
{
	struct perf_event_attr *attr;

	attr = zalloc(sizeof(struct perf_event_attr));
	if (!attr) {
		pr_err("arm_spe default config cannot allocate a perf_event_attr\n");
		return NULL;
	}

	/*
	 * If kernel driver doesn't advertise a minimum,
	 * use max allowable by PMSIDR_EL1.INTERVAL
	 */
	if (perf_pmu__scan_file(arm_spe_pmu, "caps/min_interval", "%llu",
				  &attr->sample_period) != 1) {
		pr_debug("arm_spe driver doesn't advertise a min. interval. Using 4096\n");
		attr->sample_period = 4096;
	}

	arm_spe_pmu->selectable = true;
	arm_spe_pmu->is_uncore = false;

	return attr;
}
