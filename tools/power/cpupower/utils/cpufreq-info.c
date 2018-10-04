/*
 *  (C) 2004-2009  Dominik Brodowski <linux@dominikbrodowski.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */


#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <getopt.h>

#include "cpufreq.h"
#include "helpers/sysfs.h"
#include "helpers/helpers.h"
#include "helpers/bitmask.h"

#define LINE_LEN 10

static unsigned int count_cpus(void)
{
	FILE *fp;
	char value[LINE_LEN];
	unsigned int ret = 0;
	unsigned int cpunr = 0;

	fp = fopen("/proc/stat", "r");
	if (!fp) {
		printf(_("Couldn't count the number of CPUs (%s: %s), assuming 1\n"), "/proc/stat", strerror(errno));
		return 1;
	}

	while (!feof(fp)) {
		if (!fgets(value, LINE_LEN, fp))
			continue;
		value[LINE_LEN - 1] = '\0';
		if (strlen(value) < (LINE_LEN - 2))
			continue;
		if (strstr(value, "cpu "))
			continue;
		if (sscanf(value, "cpu%d ", &cpunr) != 1)
			continue;
		if (cpunr > ret)
			ret = cpunr;
	}
	fclose(fp);

	/* cpu count starts from 0, on error return 1 (UP) */
	return ret + 1;
}


static void proc_cpufreq_output(void)
{
	unsigned int cpu, nr_cpus;
	struct cpufreq_policy *policy;
	unsigned int min_pctg = 0;
	unsigned int max_pctg = 0;
	unsigned long min, max;

	printf(_("          minimum CPU frequency  -  maximum CPU frequency  -  governor\n"));

	nr_cpus = count_cpus();
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		policy = cpufreq_get_policy(cpu);
		if (!policy)
			continue;

		if (cpufreq_get_hardware_limits(cpu, &min, &max)) {
			max = 0;
		} else {
			min_pctg = (policy->min * 100) / max;
			max_pctg = (policy->max * 100) / max;
		}
		printf("CPU%3d    %9lu kHz (%3d %%)  -  %9lu kHz (%3d %%)  -  %s\n",
			cpu , policy->min, max ? min_pctg : 0, policy->max,
			max ? max_pctg : 0, policy->governor);

		cpufreq_put_policy(policy);
	}
}

static int no_rounding;
static void print_speed(unsigned long speed)
{
	unsigned long tmp;

	if (no_rounding) {
		if (speed > 1000000)
			printf("%u.%06u GHz", ((unsigned int) speed/1000000),
				((unsigned int) speed%1000000));
		else if (speed > 1000)
			printf("%u.%03u MHz", ((unsigned int) speed/1000),
				(unsigned int) (speed%1000));
		else
			printf("%lu kHz", speed);
	} else {
		if (speed > 1000000) {
			tmp = speed%10000;
			if (tmp >= 5000)
				speed += 10000;
			printf("%u.%02u GHz", ((unsigned int) speed/1000000),
				((unsigned int) (speed%1000000)/10000));
		} else if (speed > 100000) {
			tmp = speed%1000;
			if (tmp >= 500)
				speed += 1000;
			printf("%u MHz", ((unsigned int) speed/1000));
		} else if (speed > 1000) {
			tmp = speed%100;
			if (tmp >= 50)
				speed += 100;
			printf("%u.%01u MHz", ((unsigned int) speed/1000),
				((unsigned int) (speed%1000)/100));
		}
	}

	return;
}

static void print_duration(unsigned long duration)
{
	unsigned long tmp;

	if (no_rounding) {
		if (duration > 1000000)
			printf("%u.%06u ms", ((unsigned int) duration/1000000),
				((unsigned int) duration%1000000));
		else if (duration > 100000)
			printf("%u us", ((unsigned int) duration/1000));
		else if (duration > 1000)
			printf("%u.%03u us", ((unsigned int) duration/1000),
				((unsigned int) duration%1000));
		else
			printf("%lu ns", duration);
	} else {
		if (duration > 1000000) {
			tmp = duration%10000;
			if (tmp >= 5000)
				duration += 10000;
			printf("%u.%02u ms", ((unsigned int) duration/1000000),
				((unsigned int) (duration%1000000)/10000));
		} else if (duration > 100000) {
			tmp = duration%1000;
			if (tmp >= 500)
				duration += 1000;
			printf("%u us", ((unsigned int) duration / 1000));
		} else if (duration > 1000) {
			tmp = duration%100;
			if (tmp >= 50)
				duration += 100;
			printf("%u.%01u us", ((unsigned int) duration/1000),
				((unsigned int) (duration%1000)/100));
		} else
			printf("%lu ns", duration);
	}
	return;
}

/* --boost / -b */

static int get_boost_mode(unsigned int cpu)
{
	int support, active, b_states = 0, ret, pstate_no, i;
	/* ToDo: Make this more global */
	unsigned long pstates[MAX_HW_PSTATES] = {0,};

	if (cpupower_cpu_info.vendor != X86_VENDOR_AMD &&
	    cpupower_cpu_info.vendor != X86_VENDOR_HYGON &&
	    cpupower_cpu_info.vendor != X86_VENDOR_INTEL)
		return 0;

	ret = cpufreq_has_boost_support(cpu, &support, &active, &b_states);
	if (ret) {
		printf(_("Error while evaluating Boost Capabilities"
				" on CPU %d -- are you root?\n"), cpu);
		return ret;
	}
	/* P state changes via MSR are identified via cpuid 80000007
	   on Intel and AMD, but we assume boost capable machines can do that
	   if (cpuid_eax(0x80000000) >= 0x80000007
	   && (cpuid_edx(0x80000007) & (1 << 7)))
	*/

	printf(_("  boost state support:\n"));

	printf(_("    Supported: %s\n"), support ? _("yes") : _("no"));
	printf(_("    Active: %s\n"), active ? _("yes") : _("no"));

	if ((cpupower_cpu_info.vendor == X86_VENDOR_AMD &&
	     cpupower_cpu_info.family >= 0x10) ||
	     cpupower_cpu_info.vendor == X86_VENDOR_HYGON) {
		ret = decode_pstates(cpu, cpupower_cpu_info.family, b_states,
				     pstates, &pstate_no);
		if (ret)
			return ret;

		printf(_("    Boost States: %d\n"), b_states);
		printf(_("    Total States: %d\n"), pstate_no);
		for (i = 0; i < pstate_no; i++) {
			if (i < b_states)
				printf(_("    Pstate-Pb%d: %luMHz (boost state)"
					 "\n"), i, pstates[i]);
			else
				printf(_("    Pstate-P%d:  %luMHz\n"),
				       i - b_states, pstates[i]);
		}
	} else if (cpupower_cpu_info.caps & CPUPOWER_CAP_HAS_TURBO_RATIO) {
		double bclk;
		unsigned long long intel_turbo_ratio = 0;
		unsigned int ratio;

		/* Any way to autodetect this ? */
		if (cpupower_cpu_info.caps & CPUPOWER_CAP_IS_SNB)
			bclk = 100.00;
		else
			bclk = 133.33;
		intel_turbo_ratio = msr_intel_get_turbo_ratio(cpu);
		dprint ("    Ratio: 0x%llx - bclk: %f\n",
			intel_turbo_ratio, bclk);

		ratio = (intel_turbo_ratio >> 24) & 0xFF;
		if (ratio)
			printf(_("    %.0f MHz max turbo 4 active cores\n"),
			       ratio * bclk);

		ratio = (intel_turbo_ratio >> 16) & 0xFF;
		if (ratio)
			printf(_("    %.0f MHz max turbo 3 active cores\n"),
			       ratio * bclk);

		ratio = (intel_turbo_ratio >> 8) & 0xFF;
		if (ratio)
			printf(_("    %.0f MHz max turbo 2 active cores\n"),
			       ratio * bclk);

		ratio = (intel_turbo_ratio >> 0) & 0xFF;
		if (ratio)
			printf(_("    %.0f MHz max turbo 1 active cores\n"),
			       ratio * bclk);
	}
	return 0;
}

/* --freq / -f */

static int get_freq_kernel(unsigned int cpu, unsigned int human)
{
	unsigned long freq = cpufreq_get_freq_kernel(cpu);
	printf(_("  current CPU frequency: "));
	if (!freq) {
		printf(_(" Unable to call to kernel\n"));
		return -EINVAL;
	}
	if (human) {
		print_speed(freq);
	} else
		printf("%lu", freq);
	printf(_(" (asserted by call to kernel)\n"));
	return 0;
}


/* --hwfreq / -w */

static int get_freq_hardware(unsigned int cpu, unsigned int human)
{
	unsigned long freq = cpufreq_get_freq_hardware(cpu);
	printf(_("  current CPU frequency: "));
	if (!freq) {
		printf("Unable to call hardware\n");
		return -EINVAL;
	}
	if (human) {
		print_speed(freq);
	} else
		printf("%lu", freq);
	printf(_(" (asserted by call to hardware)\n"));
	return 0;
}

/* --hwlimits / -l */

static int get_hardware_limits(unsigned int cpu, unsigned int human)
{
	unsigned long min, max;

	if (cpufreq_get_hardware_limits(cpu, &min, &max)) {
		printf(_("Not Available\n"));
		return -EINVAL;
	}

	if (human) {
		printf(_("  hardware limits: "));
		print_speed(min);
		printf(" - ");
		print_speed(max);
		printf("\n");
	} else {
		printf("%lu %lu\n", min, max);
	}
	return 0;
}

/* --driver / -d */

static int get_driver(unsigned int cpu)
{
	char *driver = cpufreq_get_driver(cpu);
	if (!driver) {
		printf(_("  no or unknown cpufreq driver is active on this CPU\n"));
		return -EINVAL;
	}
	printf("  driver: %s\n", driver);
	cpufreq_put_driver(driver);
	return 0;
}

/* --policy / -p */

static int get_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_get_policy(cpu);
	if (!policy) {
		printf(_("  Unable to determine current policy\n"));
		return -EINVAL;
	}
	printf(_("  current policy: frequency should be within "));
	print_speed(policy->min);
	printf(_(" and "));
	print_speed(policy->max);

	printf(".\n                  ");
	printf(_("The governor \"%s\" may decide which speed to use\n"
	       "                  within this range.\n"),
	       policy->governor);
	cpufreq_put_policy(policy);
	return 0;
}

/* --governors / -g */

static int get_available_governors(unsigned int cpu)
{
	struct cpufreq_available_governors *governors =
		cpufreq_get_available_governors(cpu);

	printf(_("  available cpufreq governors: "));
	if (!governors) {
		printf(_("Not Available\n"));
		return -EINVAL;
	}

	while (governors->next) {
		printf("%s ", governors->governor);
		governors = governors->next;
	}
	printf("%s\n", governors->governor);
	cpufreq_put_available_governors(governors);
	return 0;
}


/* --affected-cpus  / -a */

static int get_affected_cpus(unsigned int cpu)
{
	struct cpufreq_affected_cpus *cpus = cpufreq_get_affected_cpus(cpu);

	printf(_("  CPUs which need to have their frequency coordinated by software: "));
	if (!cpus) {
		printf(_("Not Available\n"));
		return -EINVAL;
	}

	while (cpus->next) {
		printf("%d ", cpus->cpu);
		cpus = cpus->next;
	}
	printf("%d\n", cpus->cpu);
	cpufreq_put_affected_cpus(cpus);
	return 0;
}

/* --related-cpus  / -r */

static int get_related_cpus(unsigned int cpu)
{
	struct cpufreq_affected_cpus *cpus = cpufreq_get_related_cpus(cpu);

	printf(_("  CPUs which run at the same hardware frequency: "));
	if (!cpus) {
		printf(_("Not Available\n"));
		return -EINVAL;
	}

	while (cpus->next) {
		printf("%d ", cpus->cpu);
		cpus = cpus->next;
	}
	printf("%d\n", cpus->cpu);
	cpufreq_put_related_cpus(cpus);
	return 0;
}

/* --stats / -s */

static int get_freq_stats(unsigned int cpu, unsigned int human)
{
	unsigned long total_trans = cpufreq_get_transitions(cpu);
	unsigned long long total_time;
	struct cpufreq_stats *stats = cpufreq_get_stats(cpu, &total_time);
	while (stats) {
		if (human) {
			print_speed(stats->frequency);
			printf(":%.2f%%",
				(100.0 * stats->time_in_state) / total_time);
		} else
			printf("%lu:%llu",
				stats->frequency, stats->time_in_state);
		stats = stats->next;
		if (stats)
			printf(", ");
	}
	cpufreq_put_stats(stats);
	if (total_trans)
		printf("  (%lu)\n", total_trans);
	return 0;
}

/* --latency / -y */

static int get_latency(unsigned int cpu, unsigned int human)
{
	unsigned long latency = cpufreq_get_transition_latency(cpu);

	printf(_("  maximum transition latency: "));
	if (!latency || latency == UINT_MAX) {
		printf(_(" Cannot determine or is not supported.\n"));
		return -EINVAL;
	}

	if (human) {
		print_duration(latency);
		printf("\n");
	} else
		printf("%lu\n", latency);
	return 0;
}

static void debug_output_one(unsigned int cpu)
{
	struct cpufreq_available_frequencies *freqs;

	get_driver(cpu);
	get_related_cpus(cpu);
	get_affected_cpus(cpu);
	get_latency(cpu, 1);
	get_hardware_limits(cpu, 1);

	freqs = cpufreq_get_available_frequencies(cpu);
	if (freqs) {
		printf(_("  available frequency steps:  "));
		while (freqs->next) {
			print_speed(freqs->frequency);
			printf(", ");
			freqs = freqs->next;
		}
		print_speed(freqs->frequency);
		printf("\n");
		cpufreq_put_available_frequencies(freqs);
	}

	get_available_governors(cpu);
	get_policy(cpu);
	if (get_freq_hardware(cpu, 1) < 0)
		get_freq_kernel(cpu, 1);
	get_boost_mode(cpu);
}

static struct option info_opts[] = {
	{"debug",	 no_argument,		 NULL,	 'e'},
	{"boost",	 no_argument,		 NULL,	 'b'},
	{"freq",	 no_argument,		 NULL,	 'f'},
	{"hwfreq",	 no_argument,		 NULL,	 'w'},
	{"hwlimits",	 no_argument,		 NULL,	 'l'},
	{"driver",	 no_argument,		 NULL,	 'd'},
	{"policy",	 no_argument,		 NULL,	 'p'},
	{"governors",	 no_argument,		 NULL,	 'g'},
	{"related-cpus",  no_argument,	 NULL,	 'r'},
	{"affected-cpus", no_argument,	 NULL,	 'a'},
	{"stats",	 no_argument,		 NULL,	 's'},
	{"latency",	 no_argument,		 NULL,	 'y'},
	{"proc",	 no_argument,		 NULL,	 'o'},
	{"human",	 no_argument,		 NULL,	 'm'},
	{"no-rounding", no_argument,	 NULL,	 'n'},
	{ },
};

int cmd_freq_info(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0, cont = 1;
	unsigned int cpu = 0;
	unsigned int human = 0;
	int output_param = 0;

	do {
		ret = getopt_long(argc, argv, "oefwldpgrasmybn", info_opts,
				  NULL);
		switch (ret) {
		case '?':
			output_param = '?';
			cont = 0;
			break;
		case -1:
			cont = 0;
			break;
		case 'b':
		case 'o':
		case 'a':
		case 'r':
		case 'g':
		case 'p':
		case 'd':
		case 'l':
		case 'w':
		case 'f':
		case 'e':
		case 's':
		case 'y':
			if (output_param) {
				output_param = -1;
				cont = 0;
				break;
			}
			output_param = ret;
			break;
		case 'm':
			if (human) {
				output_param = -1;
				cont = 0;
				break;
			}
			human = 1;
			break;
		case 'n':
			no_rounding = 1;
			break;
		default:
			fprintf(stderr, "invalid or unknown argument\n");
			return EXIT_FAILURE;
		}
	} while (cont);

	switch (output_param) {
	case 'o':
		if (!bitmask_isallclear(cpus_chosen)) {
			printf(_("The argument passed to this tool can't be "
				 "combined with passing a --cpu argument\n"));
			return -EINVAL;
		}
		break;
	case 0:
		output_param = 'e';
	}

	ret = 0;

	/* Default is: show output of CPU 0 only */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setbit(cpus_chosen, 0);

	switch (output_param) {
	case -1:
		printf(_("You can't specify more than one --cpu parameter and/or\n"
		       "more than one output-specific argument\n"));
		return -EINVAL;
	case '?':
		printf(_("invalid or unknown argument\n"));
		return -EINVAL;
	case 'o':
		proc_cpufreq_output();
		return EXIT_SUCCESS;
	}

	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu))
			continue;

		printf(_("analyzing CPU %d:\n"), cpu);

		if (sysfs_is_cpu_online(cpu) != 1) {
			printf(_(" *is offline\n"));
			printf("\n");
			continue;
		}

		switch (output_param) {
		case 'b':
			get_boost_mode(cpu);
			break;
		case 'e':
			debug_output_one(cpu);
			break;
		case 'a':
			ret = get_affected_cpus(cpu);
			break;
		case 'r':
			ret = get_related_cpus(cpu);
			break;
		case 'g':
			ret = get_available_governors(cpu);
			break;
		case 'p':
			ret = get_policy(cpu);
			break;
		case 'd':
			ret = get_driver(cpu);
			break;
		case 'l':
			ret = get_hardware_limits(cpu, human);
			break;
		case 'w':
			ret = get_freq_hardware(cpu, human);
			break;
		case 'f':
			ret = get_freq_kernel(cpu, human);
			break;
		case 's':
			ret = get_freq_stats(cpu, human);
			break;
		case 'y':
			ret = get_latency(cpu, human);
			break;
		}
		if (ret)
			return ret;
	}
	return ret;
}
