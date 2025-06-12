// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 */

#if defined(__i386__) || defined(__x86_64__)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <cpufreq.h>

#include "helpers/helpers.h"
#include "idle_monitor/cpupower-monitor.h"

#define MSR_APERF	0xE8
#define MSR_MPERF	0xE7

#define RDPRU ".byte 0x0f, 0x01, 0xfd"
#define RDPRU_ECX_MPERF	0
#define RDPRU_ECX_APERF	1

#define MSR_TSC	0x10

#define MSR_AMD_HWCR 0xc0010015

enum mperf_id { C0 = 0, Cx, AVG_FREQ, MPERF_CSTATE_COUNT };

static int mperf_get_count_percent(unsigned int self_id, double *percent,
				   unsigned int cpu);
static int mperf_get_count_freq(unsigned int id, unsigned long long *count,
				unsigned int cpu);
static struct timespec *time_start, *time_end;

static cstate_t mperf_cstates[MPERF_CSTATE_COUNT] = {
	{
		.name			= "C0",
		.desc			= N_("Processor Core not idle"),
		.id			= C0,
		.range			= RANGE_THREAD,
		.get_count_percent	= mperf_get_count_percent,
	},
	{
		.name			= "Cx",
		.desc			= N_("Processor Core in an idle state"),
		.id			= Cx,
		.range			= RANGE_THREAD,
		.get_count_percent	= mperf_get_count_percent,
	},

	{
		.name			= "Freq",
		.desc			= N_("Average Frequency (including boost) in MHz"),
		.id			= AVG_FREQ,
		.range			= RANGE_THREAD,
		.get_count		= mperf_get_count_freq,
	},
};

enum MAX_FREQ_MODE { MAX_FREQ_SYSFS, MAX_FREQ_TSC_REF };
static int max_freq_mode;
/*
 * The max frequency mperf is ticking at (in C0), either retrieved via:
 *   1) calculated after measurements if we know TSC ticks at mperf/P0 frequency
 *   2) cpufreq /sys/devices/.../cpu0/cpufreq/cpuinfo_max_freq at init time
 * 1. Is preferred as it also works without cpufreq subsystem (e.g. on Xen)
 */
static unsigned long max_frequency;

static unsigned long long *tsc_at_measure_start;
static unsigned long long *tsc_at_measure_end;
static unsigned long long *mperf_previous_count;
static unsigned long long *aperf_previous_count;
static unsigned long long *mperf_current_count;
static unsigned long long *aperf_current_count;

/* valid flag for all CPUs. If a MSR read failed it will be zero */
static int *is_valid;

static int mperf_get_tsc(unsigned long long *tsc)
{
	int ret;

	ret = read_msr(base_cpu, MSR_TSC, tsc);
	if (ret)
		dprint("Reading TSC MSR failed, returning %llu\n", *tsc);
	return ret;
}

static int get_aperf_mperf(int cpu, unsigned long long *aval,
				    unsigned long long *mval)
{
	unsigned long low_a, high_a;
	unsigned long low_m, high_m;
	int ret;

	/*
	 * Running on the cpu from which we read the registers will
	 * prevent APERF/MPERF from going out of sync because of IPI
	 * latency introduced by read_msr()s.
	 */
	if (mperf_monitor.flags.per_cpu_schedule) {
		if (bind_cpu(cpu))
			return 1;
	}

	if (cpupower_cpu_info.caps & CPUPOWER_CAP_AMD_RDPRU) {
		asm volatile(RDPRU
			     : "=a" (low_a), "=d" (high_a)
			     : "c" (RDPRU_ECX_APERF));
		asm volatile(RDPRU
			     : "=a" (low_m), "=d" (high_m)
			     : "c" (RDPRU_ECX_MPERF));

		*aval = ((low_a) | (high_a) << 32);
		*mval = ((low_m) | (high_m) << 32);

		return 0;
	}

	ret  = read_msr(cpu, MSR_APERF, aval);
	ret |= read_msr(cpu, MSR_MPERF, mval);

	return ret;
}

static int mperf_init_stats(unsigned int cpu)
{
	unsigned long long aval, mval;
	int ret;

	ret = get_aperf_mperf(cpu, &aval, &mval);
	aperf_previous_count[cpu] = aval;
	mperf_previous_count[cpu] = mval;
	is_valid[cpu] = !ret;

	return 0;
}

static int mperf_measure_stats(unsigned int cpu)
{
	unsigned long long aval, mval;
	int ret;

	ret = get_aperf_mperf(cpu, &aval, &mval);
	aperf_current_count[cpu] = aval;
	mperf_current_count[cpu] = mval;
	is_valid[cpu] |= !ret;

	return 0;
}

static int mperf_get_count_percent(unsigned int id, double *percent,
				   unsigned int cpu)
{
	unsigned long long aperf_diff, mperf_diff, tsc_diff;
	unsigned long long timediff;

	if (!is_valid[cpu])
		return -1;

	if (id != C0 && id != Cx)
		return -1;

	mperf_diff = mperf_current_count[cpu] - mperf_previous_count[cpu];
	aperf_diff = aperf_current_count[cpu] - aperf_previous_count[cpu];

	if (max_freq_mode == MAX_FREQ_TSC_REF) {
		tsc_diff = tsc_at_measure_end[cpu] - tsc_at_measure_start[cpu];
		*percent = 100.0 * mperf_diff / tsc_diff;
		dprint("%s: TSC Ref - mperf_diff: %llu, tsc_diff: %llu\n",
		       mperf_cstates[id].name, mperf_diff, tsc_diff);
	} else if (max_freq_mode == MAX_FREQ_SYSFS) {
		timediff = max_frequency * timespec_diff_us(time_start[cpu], time_end[cpu]);
		*percent = 100.0 * mperf_diff / timediff;
		dprint("%s: MAXFREQ - mperf_diff: %llu, time_diff: %llu\n",
		       mperf_cstates[id].name, mperf_diff, timediff);
	} else
		return -1;

	if (id == Cx)
		*percent = 100.0 - *percent;

	dprint("%s: previous: %llu - current: %llu - (%u)\n",
		mperf_cstates[id].name, mperf_diff, aperf_diff, cpu);
	dprint("%s: %f\n", mperf_cstates[id].name, *percent);
	return 0;
}

static int mperf_get_count_freq(unsigned int id, unsigned long long *count,
				unsigned int cpu)
{
	unsigned long long aperf_diff, mperf_diff, time_diff, tsc_diff;

	if (id != AVG_FREQ)
		return 1;

	if (!is_valid[cpu])
		return -1;

	mperf_diff = mperf_current_count[cpu] - mperf_previous_count[cpu];
	aperf_diff = aperf_current_count[cpu] - aperf_previous_count[cpu];

	if (max_freq_mode == MAX_FREQ_TSC_REF) {
		/* Calculate max_freq from TSC count */
		tsc_diff = tsc_at_measure_end[cpu] - tsc_at_measure_start[cpu];
		time_diff = timespec_diff_us(time_start[cpu], time_end[cpu]);
		max_frequency = tsc_diff / time_diff;
	}

	*count = max_frequency * ((double)aperf_diff / mperf_diff);
	dprint("%s: Average freq based on %s maximum frequency:\n",
	       mperf_cstates[id].name,
	       (max_freq_mode == MAX_FREQ_TSC_REF) ? "TSC calculated" : "sysfs read");
	dprint("max_frequency: %lu\n", max_frequency);
	dprint("aperf_diff: %llu\n", aperf_diff);
	dprint("mperf_diff: %llu\n", mperf_diff);
	dprint("avg freq:   %llu\n", *count);
	return 0;
}

static int mperf_start(void)
{
	int cpu;

	for (cpu = 0; cpu < cpu_count; cpu++) {
		clock_gettime(CLOCK_REALTIME, &time_start[cpu]);
		mperf_get_tsc(&tsc_at_measure_start[cpu]);
		mperf_init_stats(cpu);
	}

	return 0;
}

static int mperf_stop(void)
{
	int cpu;

	for (cpu = 0; cpu < cpu_count; cpu++) {
		clock_gettime(CLOCK_REALTIME, &time_end[cpu]);
		mperf_get_tsc(&tsc_at_measure_end[cpu]);
		mperf_measure_stats(cpu);
	}

	return 0;
}

/*
 * Mperf register is defined to tick at P0 (maximum) frequency
 *
 * Instead of reading out P0 which can be tricky to read out from HW,
 * we use TSC counter if it reliably ticks at P0/mperf frequency.
 *
 * Still try to fall back to:
 * /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq
 * on older Intel HW without invariant TSC feature.
 * Or on AMD machines where TSC does not tick at P0 (do not exist yet, but
 * it's still double checked (MSR_AMD_HWCR)).
 *
 * On these machines the user would still get useful mperf
 * stats when acpi-cpufreq driver is loaded.
 */
static int init_maxfreq_mode(void)
{
	int ret;
	unsigned long long hwcr;
	unsigned long min;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_INV_TSC))
		goto use_sysfs;

	if (cpupower_cpu_info.vendor == X86_VENDOR_AMD ||
	    cpupower_cpu_info.vendor == X86_VENDOR_HYGON) {
		/* MSR_AMD_HWCR tells us whether TSC runs at P0/mperf
		 * freq.
		 * A test whether hwcr is accessable/available would be:
		 * (cpupower_cpu_info.family > 0x10 ||
		 *   cpupower_cpu_info.family == 0x10 &&
		 *   cpupower_cpu_info.model >= 0x2))
		 * This should be the case for all aperf/mperf
		 * capable AMD machines and is therefore safe to test here.
		 * Compare with Linus kernel git commit: acf01734b1747b1ec4
		 */
		ret = read_msr(0, MSR_AMD_HWCR, &hwcr);
		/*
		 * If the MSR read failed, assume a Xen system that did
		 * not explicitly provide access to it and assume TSC works
		*/
		if (ret != 0) {
			dprint("TSC read 0x%x failed - assume TSC working\n",
			       MSR_AMD_HWCR);
			return 0;
		} else if (1 & (hwcr >> 24)) {
			max_freq_mode = MAX_FREQ_TSC_REF;
			return 0;
		} else { /* Use sysfs max frequency if available */ }
	} else if (cpupower_cpu_info.vendor == X86_VENDOR_INTEL) {
		/*
		 * On Intel we assume mperf (in C0) is ticking at same
		 * rate than TSC
		 */
		max_freq_mode = MAX_FREQ_TSC_REF;
		return 0;
	}
use_sysfs:
	if (cpufreq_get_hardware_limits(0, &min, &max_frequency)) {
		dprint("Cannot retrieve max freq from cpufreq kernel "
		       "subsystem\n");
		return -1;
	}
	max_freq_mode = MAX_FREQ_SYSFS;
	max_frequency /= 1000; /* Default automatically to MHz value */
	return 0;
}

/*
 * This monitor provides:
 *
 * 1) Average frequency a CPU resided in
 *    This always works if the CPU has aperf/mperf capabilities
 *
 * 2) C0 and Cx (any sleep state) time a CPU resided in
 *    Works if mperf timer stops ticking in sleep states which
 *    seem to be the case on all current HW.
 * Both is directly retrieved from HW registers and is independent
 * from kernel statistics.
 */
struct cpuidle_monitor mperf_monitor;
struct cpuidle_monitor *mperf_register(void)
{
	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_APERF))
		return NULL;

	if (init_maxfreq_mode())
		return NULL;

	if (cpupower_cpu_info.vendor == X86_VENDOR_AMD)
		mperf_monitor.flags.per_cpu_schedule = 1;

	/* Free this at program termination */
	is_valid = calloc(cpu_count, sizeof(int));
	mperf_previous_count = calloc(cpu_count, sizeof(unsigned long long));
	aperf_previous_count = calloc(cpu_count, sizeof(unsigned long long));
	mperf_current_count = calloc(cpu_count, sizeof(unsigned long long));
	aperf_current_count = calloc(cpu_count, sizeof(unsigned long long));
	tsc_at_measure_start = calloc(cpu_count, sizeof(unsigned long long));
	tsc_at_measure_end = calloc(cpu_count, sizeof(unsigned long long));
	time_start = calloc(cpu_count, sizeof(struct timespec));
	time_end = calloc(cpu_count, sizeof(struct timespec));
	mperf_monitor.name_len = strlen(mperf_monitor.name);
	return &mperf_monitor;
}

void mperf_unregister(void)
{
	free(mperf_previous_count);
	free(aperf_previous_count);
	free(mperf_current_count);
	free(aperf_current_count);
	free(tsc_at_measure_start);
	free(tsc_at_measure_end);
	free(time_start);
	free(time_end);
	free(is_valid);
}

struct cpuidle_monitor mperf_monitor = {
	.name			= "Mperf",
	.hw_states_num		= MPERF_CSTATE_COUNT,
	.hw_states		= mperf_cstates,
	.start			= mperf_start,
	.stop			= mperf_stop,
	.do_register		= mperf_register,
	.unregister		= mperf_unregister,
	.flags.needs_root	= 1,
	.overflow_s		= 922000000 /* 922337203 seconds TSC overflow
					       at 20GHz */
};
#endif /* #if defined(__i386__) || defined(__x86_64__) */
