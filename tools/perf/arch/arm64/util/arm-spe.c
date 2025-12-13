// SPDX-License-Identifier: GPL-2.0
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/string.h>
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
#include "../../../util/header.h"
#include "../../../util/arm-spe.h"
#include <tools/libc_compat.h> // reallocarray

#define ARM_SPE_CPU_MAGIC		0x1010101010101010ULL

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

struct arm_spe_recording {
	struct auxtrace_record		itr;
	struct perf_pmu			*arm_spe_pmu;
	struct evlist		*evlist;
	int			wrapped_cnt;
	bool			*wrapped;
};

/* Iterate config list to detect if the "freq" parameter is set */
static bool arm_spe_is_set_freq(struct evsel *evsel)
{
	struct evsel_config_term *term;

	list_for_each_entry(term, &evsel->config_terms, list) {
		if (term->type == EVSEL__CONFIG_TERM_FREQ)
			return true;
	}

	return false;
}

/*
 * arm_spe_find_cpus() returns a new cpu map, and the caller should invoke
 * perf_cpu_map__put() to release the map after use.
 */
static struct perf_cpu_map *arm_spe_find_cpus(struct evlist *evlist)
{
	struct perf_cpu_map *event_cpus = evlist->core.user_requested_cpus;
	struct perf_cpu_map *online_cpus = perf_cpu_map__new_online_cpus();
	struct perf_cpu_map *intersect_cpus;

	/* cpu map is not "any" CPU , we have specific CPUs to work with */
	if (!perf_cpu_map__has_any_cpu(event_cpus)) {
		intersect_cpus = perf_cpu_map__intersect(event_cpus, online_cpus);
		perf_cpu_map__put(online_cpus);
	/* Event can be "any" CPU so count all CPUs. */
	} else {
		intersect_cpus = online_cpus;
	}

	return intersect_cpus;
}

static size_t
arm_spe_info_priv_size(struct auxtrace_record *itr __maybe_unused,
		       struct evlist *evlist)
{
	struct perf_cpu_map *cpu_map = arm_spe_find_cpus(evlist);
	size_t size;

	if (!cpu_map)
		return 0;

	size = ARM_SPE_AUXTRACE_PRIV_MAX +
	       ARM_SPE_CPU_PRIV_MAX * perf_cpu_map__nr(cpu_map);
	size *= sizeof(u64);

	perf_cpu_map__put(cpu_map);
	return size;
}

static int arm_spe_save_cpu_header(struct auxtrace_record *itr,
				   struct perf_cpu cpu, __u64 data[])
{
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);
	struct perf_pmu *pmu = NULL;
	char *cpuid = NULL;
	u64 val;

	/* Read CPU MIDR */
	cpuid = get_cpuid_allow_env_override(cpu);
	if (!cpuid)
		return -ENOMEM;
	val = strtol(cpuid, NULL, 16);

	data[ARM_SPE_MAGIC] = ARM_SPE_CPU_MAGIC;
	data[ARM_SPE_CPU] = cpu.cpu;
	data[ARM_SPE_CPU_NR_PARAMS] = ARM_SPE_CPU_PRIV_MAX - ARM_SPE_CPU_MIDR;
	data[ARM_SPE_CPU_MIDR] = val;

	/* Find the associate Arm SPE PMU for the CPU */
	if (perf_cpu_map__has(sper->arm_spe_pmu->cpus, cpu))
		pmu = sper->arm_spe_pmu;

	if (!pmu) {
		/* No Arm SPE PMU is found */
		data[ARM_SPE_CPU_PMU_TYPE] = ULLONG_MAX;
		data[ARM_SPE_CAP_MIN_IVAL] = 0;
		data[ARM_SPE_CAP_EVENT_FILTER] = 0;
	} else {
		data[ARM_SPE_CPU_PMU_TYPE] = pmu->type;

		if (perf_pmu__scan_file(pmu, "caps/min_interval", "%lu", &val) != 1)
			val = 0;
		data[ARM_SPE_CAP_MIN_IVAL] = val;

		if (perf_pmu__scan_file(pmu, "caps/event_filter", "%lx", &val) != 1)
			val = 0;
		data[ARM_SPE_CAP_EVENT_FILTER] = val;
	}

	free(cpuid);
	return ARM_SPE_CPU_PRIV_MAX;
}

static int arm_spe_info_fill(struct auxtrace_record *itr,
			     struct perf_session *session,
			     struct perf_record_auxtrace_info *auxtrace_info,
			     size_t priv_size)
{
	int i, ret;
	size_t offset;
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);
	struct perf_pmu *arm_spe_pmu = sper->arm_spe_pmu;
	struct perf_cpu_map *cpu_map;
	struct perf_cpu cpu;
	__u64 *data;

	if (priv_size != arm_spe_info_priv_size(itr, session->evlist))
		return -EINVAL;

	if (!session->evlist->core.nr_mmaps)
		return -EINVAL;

	cpu_map = arm_spe_find_cpus(session->evlist);
	if (!cpu_map)
		return -EINVAL;

	auxtrace_info->type = PERF_AUXTRACE_ARM_SPE;
	auxtrace_info->priv[ARM_SPE_HEADER_VERSION] = ARM_SPE_HEADER_CURRENT_VERSION;
	auxtrace_info->priv[ARM_SPE_HEADER_SIZE] =
		ARM_SPE_AUXTRACE_PRIV_MAX - ARM_SPE_HEADER_VERSION;
	auxtrace_info->priv[ARM_SPE_PMU_TYPE_V2] = arm_spe_pmu->type;
	auxtrace_info->priv[ARM_SPE_CPUS_NUM] = perf_cpu_map__nr(cpu_map);

	offset = ARM_SPE_AUXTRACE_PRIV_MAX;
	perf_cpu_map__for_each_cpu(cpu, i, cpu_map) {
		assert(offset < priv_size);
		data = &auxtrace_info->priv[offset];
		ret = arm_spe_save_cpu_header(itr, cpu, data);
		if (ret < 0)
			goto out;
		offset += ret;
	}

	ret = 0;
out:
	perf_cpu_map__put(cpu_map);
	return ret;
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

static __u64 arm_spe_pmu__sample_period(const struct perf_pmu *arm_spe_pmu)
{
	static __u64 sample_period;

	if (sample_period)
		return sample_period;

	/*
	 * If kernel driver doesn't advertise a minimum,
	 * use max allowable by PMSIDR_EL1.INTERVAL
	 */
	if (perf_pmu__scan_file(arm_spe_pmu, "caps/min_interval", "%llu",
				&sample_period) != 1) {
		pr_debug("arm_spe driver doesn't advertise a min. interval. Using 4096\n");
		sample_period = 4096;
	}
	return sample_period;
}

static void arm_spe_setup_evsel(struct evsel *evsel, struct perf_cpu_map *cpus)
{
	u64 bit;

	evsel->core.attr.freq = 0;
	evsel->core.attr.sample_period = arm_spe_pmu__sample_period(evsel->pmu);
	evsel->needs_auxtrace_mmap = true;

	/*
	 * To obtain the auxtrace buffer file descriptor, the auxtrace event
	 * must come first.
	 */
	evlist__to_front(evsel->evlist, evsel);

	/*
	 * In the case of per-cpu mmaps, sample CPU for AUX event;
	 * also enable the timestamp tracing for samples correlation.
	 */
	if (!perf_cpu_map__is_any_cpu_or_is_empty(cpus)) {
		evsel__set_sample_bit(evsel, CPU);
		evsel__set_config_if_unset(evsel->pmu, evsel, "ts_enable", 1);
	}

	/*
	 * Set this only so that perf report knows that SPE generates memory info. It has no effect
	 * on the opening of the event or the SPE data produced.
	 */
	evsel__set_sample_bit(evsel, DATA_SRC);

	/*
	 * The PHYS_ADDR flag does not affect the driver behaviour, it is used to
	 * inform that the resulting output's SPE samples contain physical addresses
	 * where applicable.
	 */
	bit = perf_pmu__format_bits(evsel->pmu, "pa_enable");
	if (evsel->core.attr.config & bit)
		evsel__set_sample_bit(evsel, PHYS_ADDR);
}

static int arm_spe_setup_aux_buffer(struct record_opts *opts)
{
	bool privileged = perf_event_paranoid_check(-1);

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

		pr_debug2("%sx snapshot size: %zu\n", ARM_SPE_PMU_NAME,
			  opts->auxtrace_snapshot_size);
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

	return 0;
}

static int arm_spe_setup_tracking_event(struct evlist *evlist,
					struct record_opts *opts)
{
	int err;
	struct evsel *tracking_evsel;
	struct perf_cpu_map *cpus = evlist->core.user_requested_cpus;

	/* Add dummy event to keep tracking */
	err = parse_event(evlist, "dummy:u");
	if (err)
		return err;

	tracking_evsel = evlist__last(evlist);
	evlist__set_tracking_event(evlist, tracking_evsel);

	tracking_evsel->core.attr.freq = 0;
	tracking_evsel->core.attr.sample_period = 1;

	/* In per-cpu case, always need the time of mmap events etc */
	if (!perf_cpu_map__is_any_cpu_or_is_empty(cpus)) {
		evsel__set_sample_bit(tracking_evsel, TIME);
		evsel__set_sample_bit(tracking_evsel, CPU);

		/* also track task context switch */
		if (!record_opts__no_switch_events(opts))
			tracking_evsel->core.attr.context_switch = 1;
	}

	return 0;
}

static int arm_spe_recording_options(struct auxtrace_record *itr,
				     struct evlist *evlist,
				     struct record_opts *opts)
{
	struct arm_spe_recording *sper =
			container_of(itr, struct arm_spe_recording, itr);
	struct evsel *evsel, *tmp;
	struct perf_cpu_map *cpus = evlist->core.user_requested_cpus;
	bool discard = false;
	int err;

	sper->evlist = evlist;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel__is_aux_event(evsel)) {
			if (!strstarts(evsel->pmu->name, ARM_SPE_PMU_NAME)) {
				pr_err("Found unexpected auxtrace event: %s\n",
				       evsel->pmu->name);
				return -EINVAL;
			}
			opts->full_auxtrace = true;

			if (opts->user_freq != UINT_MAX ||
			    arm_spe_is_set_freq(evsel)) {
				pr_err("Arm SPE: Frequency is not supported. "
				       "Set period with -c option or PMU parameter (-e %s/period=NUM/).\n",
				       evsel->pmu->name);
				return -EINVAL;
			}
		}
	}

	if (!opts->full_auxtrace)
		return 0;

	evlist__for_each_entry_safe(evlist, tmp, evsel) {
		if (evsel__is_aux_event(evsel)) {
			arm_spe_setup_evsel(evsel, cpus);
			if (evsel->core.attr.config &
			    perf_pmu__format_bits(evsel->pmu, "discard"))
				discard = true;
		}
	}

	if (discard)
		return 0;

	err = arm_spe_setup_aux_buffer(opts);
	if (err)
		return err;

	return arm_spe_setup_tracking_event(evlist, opts);
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
	int ret = -EINVAL;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel__is_aux_event(evsel)) {
			ret = evsel__disable(evsel);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

static int arm_spe_snapshot_finish(struct auxtrace_record *itr)
{
	struct arm_spe_recording *ptr =
			container_of(itr, struct arm_spe_recording, itr);
	struct evsel *evsel;
	int ret = -EINVAL;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel__is_aux_event(evsel)) {
			ret = evsel__enable(evsel);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

static int arm_spe_alloc_wrapped_array(struct arm_spe_recording *ptr, int idx)
{
	bool *wrapped;
	int cnt = ptr->wrapped_cnt, new_cnt, i;

	/*
	 * No need to allocate, so return early.
	 */
	if (idx < cnt)
		return 0;

	/*
	 * Make ptr->wrapped as big as idx.
	 */
	new_cnt = idx + 1;

	/*
	 * Free'ed in arm_spe_recording_free().
	 */
	wrapped = reallocarray(ptr->wrapped, new_cnt, sizeof(bool));
	if (!wrapped)
		return -ENOMEM;

	/*
	 * init new allocated values.
	 */
	for (i = cnt; i < new_cnt; i++)
		wrapped[i] = false;

	ptr->wrapped_cnt = new_cnt;
	ptr->wrapped = wrapped;

	return 0;
}

static bool arm_spe_buffer_has_wrapped(unsigned char *buffer,
				      size_t buffer_size, u64 head)
{
	u64 i, watermark;
	u64 *buf = (u64 *)buffer;
	size_t buf_size = buffer_size;

	/*
	 * Defensively handle the case where head might be continually increasing - if its value is
	 * equal or greater than the size of the ring buffer, then we can safely determine it has
	 * wrapped around. Otherwise, continue to detect if head might have wrapped.
	 */
	if (head >= buffer_size)
		return true;

	/*
	 * We want to look the very last 512 byte (chosen arbitrarily) in the ring buffer.
	 */
	watermark = buf_size - 512;

	/*
	 * The value of head is somewhere within the size of the ring buffer. This can be that there
	 * hasn't been enough data to fill the ring buffer yet or the trace time was so long that
	 * head has numerically wrapped around.  To find we need to check if we have data at the
	 * very end of the ring buffer.  We can reliably do this because mmap'ed pages are zeroed
	 * out and there is a fresh mapping with every new session.
	 */

	/*
	 * head is less than 512 byte from the end of the ring buffer.
	 */
	if (head > watermark)
		watermark = head;

	/*
	 * Speed things up by using 64 bit transactions (see "u64 *buf" above)
	 */
	watermark /= sizeof(u64);
	buf_size /= sizeof(u64);

	/*
	 * If we find trace data at the end of the ring buffer, head has been there and has
	 * numerically wrapped around at least once.
	 */
	for (i = watermark; i < buf_size; i++)
		if (buf[i])
			return true;

	return false;
}

static int arm_spe_find_snapshot(struct auxtrace_record *itr, int idx,
				  struct auxtrace_mmap *mm, unsigned char *data,
				  u64 *head, u64 *old)
{
	int err;
	bool wrapped;
	struct arm_spe_recording *ptr =
			container_of(itr, struct arm_spe_recording, itr);

	/*
	 * Allocate memory to keep track of wrapping if this is the first
	 * time we deal with this *mm.
	 */
	if (idx >= ptr->wrapped_cnt) {
		err = arm_spe_alloc_wrapped_array(ptr, idx);
		if (err)
			return err;
	}

	/*
	 * Check to see if *head has wrapped around.  If it hasn't only the
	 * amount of data between *head and *old is snapshot'ed to avoid
	 * bloating the perf.data file with zeros.  But as soon as *head has
	 * wrapped around the entire size of the AUX ring buffer it taken.
	 */
	wrapped = ptr->wrapped[idx];
	if (!wrapped && arm_spe_buffer_has_wrapped(data, mm->len, *head)) {
		wrapped = true;
		ptr->wrapped[idx] = true;
	}

	pr_debug3("%s: mmap index %d old head %zu new head %zu size %zu\n",
		  __func__, idx, (size_t)*old, (size_t)*head, mm->len);

	/*
	 * No wrap has occurred, we can just use *head and *old.
	 */
	if (!wrapped)
		return 0;

	/*
	 * *head has wrapped around - adjust *head and *old to pickup the
	 * entire content of the AUX buffer.
	 */
	if (*head >= mm->len) {
		*old = *head - mm->len;
	} else {
		*head += mm->len;
		*old = *head - mm->len;
	}

	return 0;
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

	zfree(&sper->wrapped);
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
	sper->itr.snapshot_start = arm_spe_snapshot_start;
	sper->itr.snapshot_finish = arm_spe_snapshot_finish;
	sper->itr.find_snapshot = arm_spe_find_snapshot;
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

void
arm_spe_pmu_default_config(const struct perf_pmu *arm_spe_pmu, struct perf_event_attr *attr)
{
	attr->sample_period = arm_spe_pmu__sample_period(arm_spe_pmu);
}
