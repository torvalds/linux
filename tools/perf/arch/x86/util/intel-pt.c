// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pt.c: Intel Processor Trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 */

#include <errno.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/zalloc.h>
#include <linux/err.h>
#include <cpuid.h>

#include "../../../util/session.h"
#include "../../../util/event.h"
#include "../../../util/evlist.h"
#include "../../../util/evsel.h"
#include "../../../util/evsel_config.h"
#include "../../../util/cpumap.h"
#include "../../../util/mmap.h"
#include <subcmd/parse-options.h>
#include "../../../util/parse-events.h"
#include "../../../util/pmus.h"
#include "../../../util/debug.h"
#include "../../../util/auxtrace.h"
#include "../../../util/perf_api_probe.h"
#include "../../../util/record.h"
#include "../../../util/target.h"
#include "../../../util/tsc.h"
#include <internal/lib.h> // page_size
#include "../../../util/intel-pt.h"

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)
#define KiB_MASK(x) (KiB(x) - 1)
#define MiB_MASK(x) (MiB(x) - 1)

#define INTEL_PT_PSB_PERIOD_NEAR	256

struct intel_pt_snapshot_ref {
	void *ref_buf;
	size_t ref_offset;
	bool wrapped;
};

struct intel_pt_recording {
	struct auxtrace_record		itr;
	struct perf_pmu			*intel_pt_pmu;
	int				have_sched_switch;
	struct evlist		*evlist;
	bool				snapshot_mode;
	bool				snapshot_init_done;
	size_t				snapshot_size;
	size_t				snapshot_ref_buf_size;
	int				snapshot_ref_cnt;
	struct intel_pt_snapshot_ref	*snapshot_refs;
	size_t				priv_size;
};

static int intel_pt_parse_terms_with_default(const struct perf_pmu *pmu,
					     const char *str,
					     u64 *config)
{
	struct parse_events_terms terms;
	struct perf_event_attr attr = { .size = 0, };
	int err;

	parse_events_terms__init(&terms);
	err = parse_events_terms(&terms, str, /*input=*/ NULL);
	if (err)
		goto out_free;

	attr.config = *config;
	err = perf_pmu__config_terms(pmu, &attr, &terms, /*zero=*/true, /*err=*/NULL);
	if (err)
		goto out_free;

	*config = attr.config;
out_free:
	parse_events_terms__exit(&terms);
	return err;
}

static int intel_pt_parse_terms(const struct perf_pmu *pmu, const char *str, u64 *config)
{
	*config = 0;
	return intel_pt_parse_terms_with_default(pmu, str, config);
}

static u64 intel_pt_masked_bits(u64 mask, u64 bits)
{
	const u64 top_bit = 1ULL << 63;
	u64 res = 0;
	int i;

	for (i = 0; i < 64; i++) {
		if (mask & top_bit) {
			res <<= 1;
			if (bits & top_bit)
				res |= 1;
		}
		mask <<= 1;
		bits <<= 1;
	}

	return res;
}

static int intel_pt_read_config(struct perf_pmu *intel_pt_pmu, const char *str,
				struct evlist *evlist, u64 *res)
{
	struct evsel *evsel;
	u64 mask;

	*res = 0;

	mask = perf_pmu__format_bits(intel_pt_pmu, str);
	if (!mask)
		return -EINVAL;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == intel_pt_pmu->type) {
			*res = intel_pt_masked_bits(mask, evsel->core.attr.config);
			return 0;
		}
	}

	return -EINVAL;
}

static size_t intel_pt_psb_period(struct perf_pmu *intel_pt_pmu,
				  struct evlist *evlist)
{
	u64 val;
	int err, topa_multiple_entries;
	size_t psb_period;

	if (perf_pmu__scan_file(intel_pt_pmu, "caps/topa_multiple_entries",
				"%d", &topa_multiple_entries) != 1)
		topa_multiple_entries = 0;

	/*
	 * Use caps/topa_multiple_entries to indicate early hardware that had
	 * extra frequent PSBs.
	 */
	if (!topa_multiple_entries) {
		psb_period = 256;
		goto out;
	}

	err = intel_pt_read_config(intel_pt_pmu, "psb_period", evlist, &val);
	if (err)
		val = 0;

	psb_period = 1 << (val + 11);
out:
	pr_debug2("%s psb_period %zu\n", intel_pt_pmu->name, psb_period);
	return psb_period;
}

static int intel_pt_pick_bit(int bits, int target)
{
	int pos, pick = -1;

	for (pos = 0; bits; bits >>= 1, pos++) {
		if (bits & 1) {
			if (pos <= target || pick < 0)
				pick = pos;
			if (pos >= target)
				break;
		}
	}

	return pick;
}

static u64 intel_pt_default_config(const struct perf_pmu *intel_pt_pmu)
{
	char buf[256];
	int mtc, mtc_periods = 0, mtc_period;
	int psb_cyc, psb_periods, psb_period;
	int pos = 0;
	u64 config;
	char c;
	int dirfd;

	dirfd = perf_pmu__event_source_devices_fd();

	pos += scnprintf(buf + pos, sizeof(buf) - pos, "tsc");

	if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "caps/mtc", "%d",
				   &mtc) != 1)
		mtc = 1;

	if (mtc) {
		if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "caps/mtc_periods", "%x",
					   &mtc_periods) != 1)
			mtc_periods = 0;
		if (mtc_periods) {
			mtc_period = intel_pt_pick_bit(mtc_periods, 3);
			pos += scnprintf(buf + pos, sizeof(buf) - pos,
					 ",mtc,mtc_period=%d", mtc_period);
		}
	}

	if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "caps/psb_cyc", "%d",
				   &psb_cyc) != 1)
		psb_cyc = 1;

	if (psb_cyc && mtc_periods) {
		if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "caps/psb_periods", "%x",
					   &psb_periods) != 1)
			psb_periods = 0;
		if (psb_periods) {
			psb_period = intel_pt_pick_bit(psb_periods, 3);
			pos += scnprintf(buf + pos, sizeof(buf) - pos,
					 ",psb_period=%d", psb_period);
		}
	}

	if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "format/pt", "%c", &c) == 1 &&
	    perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "format/branch", "%c", &c) == 1)
		pos += scnprintf(buf + pos, sizeof(buf) - pos, ",pt,branch");

	pr_debug2("%s default config: %s\n", intel_pt_pmu->name, buf);

	intel_pt_parse_terms(intel_pt_pmu, buf, &config);

	close(dirfd);
	return config;
}

static int intel_pt_parse_snapshot_options(struct auxtrace_record *itr,
					   struct record_opts *opts,
					   const char *str)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	unsigned long long snapshot_size = 0;
	char *endptr;

	if (str) {
		snapshot_size = strtoull(str, &endptr, 0);
		if (*endptr || snapshot_size > SIZE_MAX)
			return -1;
	}

	opts->auxtrace_snapshot_mode = true;
	opts->auxtrace_snapshot_size = snapshot_size;

	ptr->snapshot_size = snapshot_size;

	return 0;
}

void intel_pt_pmu_default_config(const struct perf_pmu *intel_pt_pmu,
				 struct perf_event_attr *attr)
{
	static u64 config;
	static bool initialized;

	if (!initialized) {
		config = intel_pt_default_config(intel_pt_pmu);
		initialized = true;
	}
	attr->config = config;
}

static const char *intel_pt_find_filter(struct evlist *evlist,
					struct perf_pmu *intel_pt_pmu)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == intel_pt_pmu->type)
			return evsel->filter;
	}

	return NULL;
}

static size_t intel_pt_filter_bytes(const char *filter)
{
	size_t len = filter ? strlen(filter) : 0;

	return len ? roundup(len + 1, 8) : 0;
}

static size_t
intel_pt_info_priv_size(struct auxtrace_record *itr, struct evlist *evlist)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	const char *filter = intel_pt_find_filter(evlist, ptr->intel_pt_pmu);

	ptr->priv_size = (INTEL_PT_AUXTRACE_PRIV_MAX * sizeof(u64)) +
			 intel_pt_filter_bytes(filter);
	ptr->priv_size += sizeof(u64); /* Cap Event Trace */

	return ptr->priv_size;
}

static void intel_pt_tsc_ctc_ratio(u32 *n, u32 *d)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

	__get_cpuid(0x15, &eax, &ebx, &ecx, &edx);
	*n = ebx;
	*d = eax;
}

static int intel_pt_info_fill(struct auxtrace_record *itr,
			      struct perf_session *session,
			      struct perf_record_auxtrace_info *auxtrace_info,
			      size_t priv_size)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	struct perf_pmu *intel_pt_pmu = ptr->intel_pt_pmu;
	struct perf_event_mmap_page *pc;
	struct perf_tsc_conversion tc = { .time_mult = 0, };
	bool cap_user_time_zero = false, per_cpu_mmaps;
	u64 tsc_bit, mtc_bit, mtc_freq_bits, cyc_bit, noretcomp_bit;
	u32 tsc_ctc_ratio_n, tsc_ctc_ratio_d;
	unsigned long max_non_turbo_ratio;
	size_t filter_str_len;
	const char *filter;
	int event_trace;
	__u64 *info;
	int err;

	if (priv_size != ptr->priv_size)
		return -EINVAL;

	intel_pt_parse_terms(intel_pt_pmu, "tsc", &tsc_bit);
	intel_pt_parse_terms(intel_pt_pmu, "noretcomp", &noretcomp_bit);
	intel_pt_parse_terms(intel_pt_pmu, "mtc", &mtc_bit);
	mtc_freq_bits = perf_pmu__format_bits(intel_pt_pmu, "mtc_period");
	intel_pt_parse_terms(intel_pt_pmu, "cyc", &cyc_bit);

	intel_pt_tsc_ctc_ratio(&tsc_ctc_ratio_n, &tsc_ctc_ratio_d);

	if (perf_pmu__scan_file(intel_pt_pmu, "max_nonturbo_ratio",
				"%lu", &max_non_turbo_ratio) != 1)
		max_non_turbo_ratio = 0;
	if (perf_pmu__scan_file(intel_pt_pmu, "caps/event_trace",
				"%d", &event_trace) != 1)
		event_trace = 0;

	filter = intel_pt_find_filter(session->evlist, ptr->intel_pt_pmu);
	filter_str_len = filter ? strlen(filter) : 0;

	if (!session->evlist->core.nr_mmaps)
		return -EINVAL;

	pc = session->evlist->mmap[0].core.base;
	if (pc) {
		err = perf_read_tsc_conversion(pc, &tc);
		if (err) {
			if (err != -EOPNOTSUPP)
				return err;
		} else {
			cap_user_time_zero = tc.time_mult != 0;
		}
		if (!cap_user_time_zero)
			ui__warning("Intel Processor Trace: TSC not available\n");
	}

	per_cpu_mmaps = !perf_cpu_map__empty(session->evlist->core.user_requested_cpus);

	auxtrace_info->type = PERF_AUXTRACE_INTEL_PT;
	auxtrace_info->priv[INTEL_PT_PMU_TYPE] = intel_pt_pmu->type;
	auxtrace_info->priv[INTEL_PT_TIME_SHIFT] = tc.time_shift;
	auxtrace_info->priv[INTEL_PT_TIME_MULT] = tc.time_mult;
	auxtrace_info->priv[INTEL_PT_TIME_ZERO] = tc.time_zero;
	auxtrace_info->priv[INTEL_PT_CAP_USER_TIME_ZERO] = cap_user_time_zero;
	auxtrace_info->priv[INTEL_PT_TSC_BIT] = tsc_bit;
	auxtrace_info->priv[INTEL_PT_NORETCOMP_BIT] = noretcomp_bit;
	auxtrace_info->priv[INTEL_PT_HAVE_SCHED_SWITCH] = ptr->have_sched_switch;
	auxtrace_info->priv[INTEL_PT_SNAPSHOT_MODE] = ptr->snapshot_mode;
	auxtrace_info->priv[INTEL_PT_PER_CPU_MMAPS] = per_cpu_mmaps;
	auxtrace_info->priv[INTEL_PT_MTC_BIT] = mtc_bit;
	auxtrace_info->priv[INTEL_PT_MTC_FREQ_BITS] = mtc_freq_bits;
	auxtrace_info->priv[INTEL_PT_TSC_CTC_N] = tsc_ctc_ratio_n;
	auxtrace_info->priv[INTEL_PT_TSC_CTC_D] = tsc_ctc_ratio_d;
	auxtrace_info->priv[INTEL_PT_CYC_BIT] = cyc_bit;
	auxtrace_info->priv[INTEL_PT_MAX_NONTURBO_RATIO] = max_non_turbo_ratio;
	auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN] = filter_str_len;

	info = &auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN] + 1;

	if (filter_str_len) {
		size_t len = intel_pt_filter_bytes(filter);

		strncpy((char *)info, filter, len);
		info += len >> 3;
	}

	*info++ = event_trace;

	return 0;
}

#ifdef HAVE_LIBTRACEEVENT
static int intel_pt_track_switches(struct evlist *evlist)
{
	const char *sched_switch = "sched:sched_switch";
	struct evsel *evsel;
	int err;

	if (!evlist__can_select_event(evlist, sched_switch))
		return -EPERM;

	evsel = evlist__add_sched_switch(evlist, true);
	if (IS_ERR(evsel)) {
		err = PTR_ERR(evsel);
		pr_debug2("%s: failed to create %s, error = %d\n",
			  __func__, sched_switch, err);
		return err;
	}

	evsel->immediate = true;

	return 0;
}
#endif

static void intel_pt_valid_str(char *str, size_t len, u64 valid)
{
	unsigned int val, last = 0, state = 1;
	int p = 0;

	str[0] = '\0';

	for (val = 0; val <= 64; val++, valid >>= 1) {
		if (valid & 1) {
			last = val;
			switch (state) {
			case 0:
				p += scnprintf(str + p, len - p, ",");
				/* Fall through */
			case 1:
				p += scnprintf(str + p, len - p, "%u", val);
				state = 2;
				break;
			case 2:
				state = 3;
				break;
			case 3:
				state = 4;
				break;
			default:
				break;
			}
		} else {
			switch (state) {
			case 3:
				p += scnprintf(str + p, len - p, ",%u", last);
				state = 0;
				break;
			case 4:
				p += scnprintf(str + p, len - p, "-%u", last);
				state = 0;
				break;
			default:
				break;
			}
			if (state != 1)
				state = 0;
		}
	}
}

static int intel_pt_val_config_term(struct perf_pmu *intel_pt_pmu, int dirfd,
				    const char *caps, const char *name,
				    const char *supported, u64 config)
{
	char valid_str[256];
	unsigned int shift;
	unsigned long long valid;
	u64 bits;
	int ok;

	if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, caps, "%llx", &valid) != 1)
		valid = 0;

	if (supported &&
	    perf_pmu__scan_file_at(intel_pt_pmu, dirfd, supported, "%d", &ok) == 1 && !ok)
		valid = 0;

	valid |= 1;

	bits = perf_pmu__format_bits(intel_pt_pmu, name);

	config &= bits;

	for (shift = 0; bits && !(bits & 1); shift++)
		bits >>= 1;

	config >>= shift;

	if (config > 63)
		goto out_err;

	if (valid & (1 << config))
		return 0;
out_err:
	intel_pt_valid_str(valid_str, sizeof(valid_str), valid);
	pr_err("Invalid %s for %s. Valid values are: %s\n",
	       name, INTEL_PT_PMU_NAME, valid_str);
	return -EINVAL;
}

static int intel_pt_validate_config(struct perf_pmu *intel_pt_pmu,
				    struct evsel *evsel)
{
	int err, dirfd;
	char c;

	if (!evsel)
		return 0;

	dirfd = perf_pmu__event_source_devices_fd();
	if (dirfd < 0)
		return dirfd;

	/*
	 * If supported, force pass-through config term (pt=1) even if user
	 * sets pt=0, which avoids senseless kernel errors.
	 */
	if (perf_pmu__scan_file_at(intel_pt_pmu, dirfd, "format/pt", "%c", &c) == 1 &&
	    !(evsel->core.attr.config & 1)) {
		pr_warning("pt=0 doesn't make sense, forcing pt=1\n");
		evsel->core.attr.config |= 1;
	}

	err = intel_pt_val_config_term(intel_pt_pmu, dirfd, "caps/cycle_thresholds",
				       "cyc_thresh", "caps/psb_cyc",
				       evsel->core.attr.config);
	if (err)
		goto out;

	err = intel_pt_val_config_term(intel_pt_pmu, dirfd, "caps/mtc_periods",
				       "mtc_period", "caps/mtc",
				       evsel->core.attr.config);
	if (err)
		goto out;

	err = intel_pt_val_config_term(intel_pt_pmu, dirfd, "caps/psb_periods",
					"psb_period", "caps/psb_cyc",
					evsel->core.attr.config);

out:
	close(dirfd);
	return err;
}

static void intel_pt_min_max_sample_sz(struct evlist *evlist,
				       size_t *min_sz, size_t *max_sz)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		size_t sz = evsel->core.attr.aux_sample_size;

		if (!sz)
			continue;
		if (min_sz && (sz < *min_sz || !*min_sz))
			*min_sz = sz;
		if (max_sz && sz > *max_sz)
			*max_sz = sz;
	}
}

/*
 * Currently, there is not enough information to disambiguate different PEBS
 * events, so only allow one.
 */
static bool intel_pt_too_many_aux_output(struct evlist *evlist)
{
	struct evsel *evsel;
	int aux_output_cnt = 0;

	evlist__for_each_entry(evlist, evsel)
		aux_output_cnt += !!evsel->core.attr.aux_output;

	if (aux_output_cnt > 1) {
		pr_err(INTEL_PT_PMU_NAME " supports at most one event with aux-output\n");
		return true;
	}

	return false;
}

static int intel_pt_recording_options(struct auxtrace_record *itr,
				      struct evlist *evlist,
				      struct record_opts *opts)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	struct perf_pmu *intel_pt_pmu = ptr->intel_pt_pmu;
	bool have_timing_info, need_immediate = false;
	struct evsel *evsel, *intel_pt_evsel = NULL;
	const struct perf_cpu_map *cpus = evlist->core.user_requested_cpus;
	bool privileged = perf_event_paranoid_check(-1);
	u64 tsc_bit;
	int err;

	ptr->evlist = evlist;
	ptr->snapshot_mode = opts->auxtrace_snapshot_mode;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == intel_pt_pmu->type) {
			if (intel_pt_evsel) {
				pr_err("There may be only one " INTEL_PT_PMU_NAME " event\n");
				return -EINVAL;
			}
			evsel->core.attr.freq = 0;
			evsel->core.attr.sample_period = 1;
			evsel->no_aux_samples = true;
			evsel->needs_auxtrace_mmap = true;
			intel_pt_evsel = evsel;
			opts->full_auxtrace = true;
		}
	}

	if (opts->auxtrace_snapshot_mode && !opts->full_auxtrace) {
		pr_err("Snapshot mode (-S option) requires " INTEL_PT_PMU_NAME " PMU event (-e " INTEL_PT_PMU_NAME ")\n");
		return -EINVAL;
	}

	if (opts->auxtrace_snapshot_mode && opts->auxtrace_sample_mode) {
		pr_err("Snapshot mode (" INTEL_PT_PMU_NAME " PMU) and sample trace cannot be used together\n");
		return -EINVAL;
	}

	if (opts->use_clockid) {
		pr_err("Cannot use clockid (-k option) with " INTEL_PT_PMU_NAME "\n");
		return -EINVAL;
	}

	if (intel_pt_too_many_aux_output(evlist))
		return -EINVAL;

	if (!opts->full_auxtrace)
		return 0;

	if (opts->auxtrace_sample_mode)
		evsel__set_config_if_unset(intel_pt_pmu, intel_pt_evsel,
					   "psb_period", 0);

	err = intel_pt_validate_config(intel_pt_pmu, intel_pt_evsel);
	if (err)
		return err;

	/* Set default sizes for snapshot mode */
	if (opts->auxtrace_snapshot_mode) {
		size_t psb_period = intel_pt_psb_period(intel_pt_pmu, evlist);

		if (!opts->auxtrace_snapshot_size && !opts->auxtrace_mmap_pages) {
			if (privileged) {
				opts->auxtrace_mmap_pages = MiB(4) / page_size;
			} else {
				opts->auxtrace_mmap_pages = KiB(128) / page_size;
				if (opts->mmap_pages == UINT_MAX)
					opts->mmap_pages = KiB(256) / page_size;
			}
		} else if (!opts->auxtrace_mmap_pages && !privileged &&
			   opts->mmap_pages == UINT_MAX) {
			opts->mmap_pages = KiB(256) / page_size;
		}
		if (!opts->auxtrace_snapshot_size)
			opts->auxtrace_snapshot_size =
				opts->auxtrace_mmap_pages * (size_t)page_size;
		if (!opts->auxtrace_mmap_pages) {
			size_t sz = opts->auxtrace_snapshot_size;

			sz = round_up(sz, page_size) / page_size;
			opts->auxtrace_mmap_pages = roundup_pow_of_two(sz);
		}
		if (opts->auxtrace_snapshot_size >
				opts->auxtrace_mmap_pages * (size_t)page_size) {
			pr_err("Snapshot size %zu must not be greater than AUX area tracing mmap size %zu\n",
			       opts->auxtrace_snapshot_size,
			       opts->auxtrace_mmap_pages * (size_t)page_size);
			return -EINVAL;
		}
		if (!opts->auxtrace_snapshot_size || !opts->auxtrace_mmap_pages) {
			pr_err("Failed to calculate default snapshot size and/or AUX area tracing mmap pages\n");
			return -EINVAL;
		}
		pr_debug2("Intel PT snapshot size: %zu\n",
			  opts->auxtrace_snapshot_size);
		if (psb_period &&
		    opts->auxtrace_snapshot_size <= psb_period +
						  INTEL_PT_PSB_PERIOD_NEAR)
			ui__warning("Intel PT snapshot size (%zu) may be too small for PSB period (%zu)\n",
				    opts->auxtrace_snapshot_size, psb_period);
	}

	/* Set default sizes for sample mode */
	if (opts->auxtrace_sample_mode) {
		size_t psb_period = intel_pt_psb_period(intel_pt_pmu, evlist);
		size_t min_sz = 0, max_sz = 0;

		intel_pt_min_max_sample_sz(evlist, &min_sz, &max_sz);
		if (!opts->auxtrace_mmap_pages && !privileged &&
		    opts->mmap_pages == UINT_MAX)
			opts->mmap_pages = KiB(256) / page_size;
		if (!opts->auxtrace_mmap_pages) {
			size_t sz = round_up(max_sz, page_size) / page_size;

			opts->auxtrace_mmap_pages = roundup_pow_of_two(sz);
		}
		if (max_sz > opts->auxtrace_mmap_pages * (size_t)page_size) {
			pr_err("Sample size %zu must not be greater than AUX area tracing mmap size %zu\n",
			       max_sz,
			       opts->auxtrace_mmap_pages * (size_t)page_size);
			return -EINVAL;
		}
		pr_debug2("Intel PT min. sample size: %zu max. sample size: %zu\n",
			  min_sz, max_sz);
		if (psb_period &&
		    min_sz <= psb_period + INTEL_PT_PSB_PERIOD_NEAR)
			ui__warning("Intel PT sample size (%zu) may be too small for PSB period (%zu)\n",
				    min_sz, psb_period);
	}

	/* Set default sizes for full trace mode */
	if (opts->full_auxtrace && !opts->auxtrace_mmap_pages) {
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
		size_t min_sz;

		if (opts->auxtrace_snapshot_mode || opts->auxtrace_sample_mode)
			min_sz = KiB(4);
		else
			min_sz = KiB(8);

		if (sz < min_sz || !is_power_of_2(sz)) {
			pr_err("Invalid mmap size for Intel Processor Trace: must be at least %zuKiB and a power of 2\n",
			       min_sz / 1024);
			return -EINVAL;
		}
	}

	if (!opts->auxtrace_snapshot_mode && !opts->auxtrace_sample_mode) {
		u32 aux_watermark = opts->auxtrace_mmap_pages * page_size / 4;

		intel_pt_evsel->core.attr.aux_watermark = aux_watermark;
	}

	intel_pt_parse_terms(intel_pt_pmu, "tsc", &tsc_bit);

	if (opts->full_auxtrace && (intel_pt_evsel->core.attr.config & tsc_bit))
		have_timing_info = true;
	else
		have_timing_info = false;

	/*
	 * Per-cpu recording needs sched_switch events to distinguish different
	 * threads.
	 */
	if (have_timing_info && !perf_cpu_map__empty(cpus) &&
	    !record_opts__no_switch_events(opts)) {
		if (perf_can_record_switch_events()) {
			bool cpu_wide = !target__none(&opts->target) &&
					!target__has_task(&opts->target);

			if (!cpu_wide && perf_can_record_cpu_wide()) {
				struct evsel *switch_evsel;

				switch_evsel = evlist__add_dummy_on_all_cpus(evlist);
				if (!switch_evsel)
					return -ENOMEM;

				switch_evsel->core.attr.context_switch = 1;
				switch_evsel->immediate = true;

				evsel__set_sample_bit(switch_evsel, TID);
				evsel__set_sample_bit(switch_evsel, TIME);
				evsel__set_sample_bit(switch_evsel, CPU);
				evsel__reset_sample_bit(switch_evsel, BRANCH_STACK);

				opts->record_switch_events = false;
				ptr->have_sched_switch = 3;
			} else {
				opts->record_switch_events = true;
				need_immediate = true;
				if (cpu_wide)
					ptr->have_sched_switch = 3;
				else
					ptr->have_sched_switch = 2;
			}
		} else {
#ifdef HAVE_LIBTRACEEVENT
			err = intel_pt_track_switches(evlist);
			if (err == -EPERM)
				pr_debug2("Unable to select sched:sched_switch\n");
			else if (err)
				return err;
			else
				ptr->have_sched_switch = 1;
#endif
		}
	}

	if (have_timing_info && !intel_pt_evsel->core.attr.exclude_kernel &&
	    perf_can_record_text_poke_events() && perf_can_record_cpu_wide())
		opts->text_poke = true;

	if (intel_pt_evsel) {
		/*
		 * To obtain the auxtrace buffer file descriptor, the auxtrace
		 * event must come first.
		 */
		evlist__to_front(evlist, intel_pt_evsel);
		/*
		 * In the case of per-cpu mmaps, we need the CPU on the
		 * AUX event.
		 */
		if (!perf_cpu_map__empty(cpus))
			evsel__set_sample_bit(intel_pt_evsel, CPU);
	}

	/* Add dummy event to keep tracking */
	if (opts->full_auxtrace) {
		bool need_system_wide_tracking;
		struct evsel *tracking_evsel;

		/*
		 * User space tasks can migrate between CPUs, so when tracing
		 * selected CPUs, sideband for all CPUs is still needed.
		 */
		need_system_wide_tracking = opts->target.cpu_list &&
					    !intel_pt_evsel->core.attr.exclude_user;

		tracking_evsel = evlist__add_aux_dummy(evlist, need_system_wide_tracking);
		if (!tracking_evsel)
			return -ENOMEM;

		evlist__set_tracking_event(evlist, tracking_evsel);

		if (need_immediate)
			tracking_evsel->immediate = true;

		/* In per-cpu case, always need the time of mmap events etc */
		if (!perf_cpu_map__empty(cpus)) {
			evsel__set_sample_bit(tracking_evsel, TIME);
			/* And the CPU for switch events */
			evsel__set_sample_bit(tracking_evsel, CPU);
		}
		evsel__reset_sample_bit(tracking_evsel, BRANCH_STACK);
	}

	/*
	 * Warn the user when we do not have enough information to decode i.e.
	 * per-cpu with no sched_switch (except workload-only).
	 */
	if (!ptr->have_sched_switch && !perf_cpu_map__empty(cpus) &&
	    !target__none(&opts->target) &&
	    !intel_pt_evsel->core.attr.exclude_user)
		ui__warning("Intel Processor Trace decoding will not be possible except for kernel tracing!\n");

	return 0;
}

static int intel_pt_snapshot_start(struct auxtrace_record *itr)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->intel_pt_pmu->type)
			return evsel__disable(evsel);
	}
	return -EINVAL;
}

static int intel_pt_snapshot_finish(struct auxtrace_record *itr)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->intel_pt_pmu->type)
			return evsel__enable(evsel);
	}
	return -EINVAL;
}

static int intel_pt_alloc_snapshot_refs(struct intel_pt_recording *ptr, int idx)
{
	const size_t sz = sizeof(struct intel_pt_snapshot_ref);
	int cnt = ptr->snapshot_ref_cnt, new_cnt = cnt * 2;
	struct intel_pt_snapshot_ref *refs;

	if (!new_cnt)
		new_cnt = 16;

	while (new_cnt <= idx)
		new_cnt *= 2;

	refs = calloc(new_cnt, sz);
	if (!refs)
		return -ENOMEM;

	memcpy(refs, ptr->snapshot_refs, cnt * sz);

	ptr->snapshot_refs = refs;
	ptr->snapshot_ref_cnt = new_cnt;

	return 0;
}

static void intel_pt_free_snapshot_refs(struct intel_pt_recording *ptr)
{
	int i;

	for (i = 0; i < ptr->snapshot_ref_cnt; i++)
		zfree(&ptr->snapshot_refs[i].ref_buf);
	zfree(&ptr->snapshot_refs);
}

static void intel_pt_recording_free(struct auxtrace_record *itr)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);

	intel_pt_free_snapshot_refs(ptr);
	free(ptr);
}

static int intel_pt_alloc_snapshot_ref(struct intel_pt_recording *ptr, int idx,
				       size_t snapshot_buf_size)
{
	size_t ref_buf_size = ptr->snapshot_ref_buf_size;
	void *ref_buf;

	ref_buf = zalloc(ref_buf_size);
	if (!ref_buf)
		return -ENOMEM;

	ptr->snapshot_refs[idx].ref_buf = ref_buf;
	ptr->snapshot_refs[idx].ref_offset = snapshot_buf_size - ref_buf_size;

	return 0;
}

static size_t intel_pt_snapshot_ref_buf_size(struct intel_pt_recording *ptr,
					     size_t snapshot_buf_size)
{
	const size_t max_size = 256 * 1024;
	size_t buf_size = 0, psb_period;

	if (ptr->snapshot_size <= 64 * 1024)
		return 0;

	psb_period = intel_pt_psb_period(ptr->intel_pt_pmu, ptr->evlist);
	if (psb_period)
		buf_size = psb_period * 2;

	if (!buf_size || buf_size > max_size)
		buf_size = max_size;

	if (buf_size >= snapshot_buf_size)
		return 0;

	if (buf_size >= ptr->snapshot_size / 2)
		return 0;

	return buf_size;
}

static int intel_pt_snapshot_init(struct intel_pt_recording *ptr,
				  size_t snapshot_buf_size)
{
	if (ptr->snapshot_init_done)
		return 0;

	ptr->snapshot_init_done = true;

	ptr->snapshot_ref_buf_size = intel_pt_snapshot_ref_buf_size(ptr,
							snapshot_buf_size);

	return 0;
}

/**
 * intel_pt_compare_buffers - compare bytes in a buffer to a circular buffer.
 * @buf1: first buffer
 * @compare_size: number of bytes to compare
 * @buf2: second buffer (a circular buffer)
 * @offs2: offset in second buffer
 * @buf2_size: size of second buffer
 *
 * The comparison allows for the possibility that the bytes to compare in the
 * circular buffer are not contiguous.  It is assumed that @compare_size <=
 * @buf2_size.  This function returns %false if the bytes are identical, %true
 * otherwise.
 */
static bool intel_pt_compare_buffers(void *buf1, size_t compare_size,
				     void *buf2, size_t offs2, size_t buf2_size)
{
	size_t end2 = offs2 + compare_size, part_size;

	if (end2 <= buf2_size)
		return memcmp(buf1, buf2 + offs2, compare_size);

	part_size = end2 - buf2_size;
	if (memcmp(buf1, buf2 + offs2, part_size))
		return true;

	compare_size -= part_size;

	return memcmp(buf1 + part_size, buf2, compare_size);
}

static bool intel_pt_compare_ref(void *ref_buf, size_t ref_offset,
				 size_t ref_size, size_t buf_size,
				 void *data, size_t head)
{
	size_t ref_end = ref_offset + ref_size;

	if (ref_end > buf_size) {
		if (head > ref_offset || head < ref_end - buf_size)
			return true;
	} else if (head > ref_offset && head < ref_end) {
		return true;
	}

	return intel_pt_compare_buffers(ref_buf, ref_size, data, ref_offset,
					buf_size);
}

static void intel_pt_copy_ref(void *ref_buf, size_t ref_size, size_t buf_size,
			      void *data, size_t head)
{
	if (head >= ref_size) {
		memcpy(ref_buf, data + head - ref_size, ref_size);
	} else {
		memcpy(ref_buf, data, head);
		ref_size -= head;
		memcpy(ref_buf + head, data + buf_size - ref_size, ref_size);
	}
}

static bool intel_pt_wrapped(struct intel_pt_recording *ptr, int idx,
			     struct auxtrace_mmap *mm, unsigned char *data,
			     u64 head)
{
	struct intel_pt_snapshot_ref *ref = &ptr->snapshot_refs[idx];
	bool wrapped;

	wrapped = intel_pt_compare_ref(ref->ref_buf, ref->ref_offset,
				       ptr->snapshot_ref_buf_size, mm->len,
				       data, head);

	intel_pt_copy_ref(ref->ref_buf, ptr->snapshot_ref_buf_size, mm->len,
			  data, head);

	return wrapped;
}

static bool intel_pt_first_wrap(u64 *data, size_t buf_size)
{
	int i, a, b;

	b = buf_size >> 3;
	a = b - 512;
	if (a < 0)
		a = 0;

	for (i = a; i < b; i++) {
		if (data[i])
			return true;
	}

	return false;
}

static int intel_pt_find_snapshot(struct auxtrace_record *itr, int idx,
				  struct auxtrace_mmap *mm, unsigned char *data,
				  u64 *head, u64 *old)
{
	struct intel_pt_recording *ptr =
			container_of(itr, struct intel_pt_recording, itr);
	bool wrapped;
	int err;

	pr_debug3("%s: mmap index %d old head %zu new head %zu\n",
		  __func__, idx, (size_t)*old, (size_t)*head);

	err = intel_pt_snapshot_init(ptr, mm->len);
	if (err)
		goto out_err;

	if (idx >= ptr->snapshot_ref_cnt) {
		err = intel_pt_alloc_snapshot_refs(ptr, idx);
		if (err)
			goto out_err;
	}

	if (ptr->snapshot_ref_buf_size) {
		if (!ptr->snapshot_refs[idx].ref_buf) {
			err = intel_pt_alloc_snapshot_ref(ptr, idx, mm->len);
			if (err)
				goto out_err;
		}
		wrapped = intel_pt_wrapped(ptr, idx, mm, data, *head);
	} else {
		wrapped = ptr->snapshot_refs[idx].wrapped;
		if (!wrapped && intel_pt_first_wrap((u64 *)data, mm->len)) {
			ptr->snapshot_refs[idx].wrapped = true;
			wrapped = true;
		}
	}

	/*
	 * In full trace mode 'head' continually increases.  However in snapshot
	 * mode 'head' is an offset within the buffer.  Here 'old' and 'head'
	 * are adjusted to match the full trace case which expects that 'old' is
	 * always less than 'head'.
	 */
	if (wrapped) {
		*old = *head;
		*head += mm->len;
	} else {
		if (mm->mask)
			*old &= mm->mask;
		else
			*old %= mm->len;
		if (*old > *head)
			*head += mm->len;
	}

	pr_debug3("%s: wrap-around %sdetected, adjusted old head %zu adjusted new head %zu\n",
		  __func__, wrapped ? "" : "not ", (size_t)*old, (size_t)*head);

	return 0;

out_err:
	pr_err("%s: failed, error %d\n", __func__, err);
	return err;
}

static u64 intel_pt_reference(struct auxtrace_record *itr __maybe_unused)
{
	return rdtsc();
}

struct auxtrace_record *intel_pt_recording_init(int *err)
{
	struct perf_pmu *intel_pt_pmu = perf_pmus__find(INTEL_PT_PMU_NAME);
	struct intel_pt_recording *ptr;

	if (!intel_pt_pmu)
		return NULL;

	if (setenv("JITDUMP_USE_ARCH_TIMESTAMP", "1", 1)) {
		*err = -errno;
		return NULL;
	}

	ptr = zalloc(sizeof(struct intel_pt_recording));
	if (!ptr) {
		*err = -ENOMEM;
		return NULL;
	}

	ptr->intel_pt_pmu = intel_pt_pmu;
	ptr->itr.pmu = intel_pt_pmu;
	ptr->itr.recording_options = intel_pt_recording_options;
	ptr->itr.info_priv_size = intel_pt_info_priv_size;
	ptr->itr.info_fill = intel_pt_info_fill;
	ptr->itr.free = intel_pt_recording_free;
	ptr->itr.snapshot_start = intel_pt_snapshot_start;
	ptr->itr.snapshot_finish = intel_pt_snapshot_finish;
	ptr->itr.find_snapshot = intel_pt_find_snapshot;
	ptr->itr.parse_snapshot_options = intel_pt_parse_snapshot_options;
	ptr->itr.reference = intel_pt_reference;
	ptr->itr.read_finish = auxtrace_record__read_finish;
	/*
	 * Decoding starts at a PSB packet. Minimum PSB period is 2K so 4K
	 * should give at least 1 PSB per sample.
	 */
	ptr->itr.default_aux_sample_size = 4096;
	return &ptr->itr;
}
