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

#include <getopt.h>

#include "cpufreq.h"
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

static void print_speed(unsigned long speed)
{
	unsigned long tmp;

	if (speed > 1000000) {
		tmp = speed % 10000;
		if (tmp >= 5000)
			speed += 10000;
		printf("%u.%02u GHz", ((unsigned int) speed/1000000),
			((unsigned int) (speed%1000000)/10000));
	} else if (speed > 100000) {
		tmp = speed % 1000;
		if (tmp >= 500)
			speed += 1000;
		printf("%u MHz", ((unsigned int) speed / 1000));
	} else if (speed > 1000) {
		tmp = speed % 100;
		if (tmp >= 50)
			speed += 100;
		printf("%u.%01u MHz", ((unsigned int) speed/1000),
			((unsigned int) (speed%1000)/100));
	} else
		printf("%lu kHz", speed);

	return;
}

static void print_duration(unsigned long duration)
{
	unsigned long tmp;

	if (duration > 1000000) {
		tmp = duration % 10000;
		if (tmp >= 5000)
			duration += 10000;
		printf("%u.%02u ms", ((unsigned int) duration/1000000),
			((unsigned int) (duration%1000000)/10000));
	} else if (duration > 100000) {
		tmp = duration % 1000;
		if (tmp >= 500)
			duration += 1000;
		printf("%u us", ((unsigned int) duration / 1000));
	} else if (duration > 1000) {
		tmp = duration % 100;
		if (tmp >= 50)
			duration += 100;
		printf("%u.%01u us", ((unsigned int) duration/1000),
			((unsigned int) (duration%1000)/100));
	} else
		printf("%lu ns", duration);

	return;
}

/* --boost / -b */

static int get_boost_mode(unsigned int cpu)
{
	int support, active, b_states = 0, ret, pstate_no, i;
	/* ToDo: Make this more global */
	unsigned long pstates[MAX_HW_PSTATES] = {0,};

	if (cpupower_cpu_info.vendor != X86_VENDOR_AMD &&
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

	if (cpupower_cpu_info.vendor == X86_VENDOR_AMD &&
	    cpupower_cpu_info.family >= 0x10) {
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

static void debug_output_one(unsigned int cpu)
{
	char *driver;
	struct cpufreq_affected_cpus *cpus;
	struct cpufreq_available_frequencies *freqs;
	unsigned long min, max, freq_kernel, freq_hardware;
	unsigned long total_trans, latency;
	unsigned long long total_time;
	struct cpufreq_policy *policy;
	struct cpufreq_available_governors *governors;
	struct cpufreq_stats *stats;

	if (cpufreq_cpu_exists(cpu))
		return;

	freq_kernel = cpufreq_get_freq_kernel(cpu);
	freq_hardware = cpufreq_get_freq_hardware(cpu);

	driver = cpufreq_get_driver(cpu);
	if (!driver) {
		printf(_("  no or unknown cpufreq driver is active on this CPU\n"));
	} else {
		printf(_("  driver: %s\n"), driver);
		cpufreq_put_driver(driver);
	}

	cpus = cpufreq_get_related_cpus(cpu);
	if (cpus) {
		printf(_("  CPUs which run at the same hardware frequency: "));
		while (cpus->next) {
			printf("%d ", cpus->cpu);
			cpus = cpus->next;
		}
		printf("%d\n", cpus->cpu);
		cpufreq_put_related_cpus(cpus);
	}

	cpus = cpufreq_get_affected_cpus(cpu);
	if (cpus) {
		printf(_("  CPUs which need to have their frequency coordinated by software: "));
		while (cpus->next) {
			printf("%d ", cpus->cpu);
			cpus = cpus->next;
		}
		printf("%d\n", cpus->cpu);
		cpufreq_put_affected_cpus(cpus);
	}

	latency = cpufreq_get_transition_latency(cpu);
	if (latency) {
		printf(_("  maximum transition latency: "));
		print_duration(latency);
		printf(".\n");
	}

	if (!(cpufreq_get_hardware_limits(cpu, &min, &max))) {
		printf(_("  hardware limits: "));
		print_speed(min);
		printf(" - ");
		print_speed(max);
		printf("\n");
	}

	freqs = cpufreq_get_available_frequencies(cpu);
	if (freqs) {
		printf(_("  available frequency steps: "));
		while (freqs->next) {
			print_speed(freqs->frequency);
			printf(", ");
			freqs = freqs->next;
		}
		print_speed(freqs->frequency);
		printf("\n");
		cpufreq_put_available_frequencies(freqs);
	}

	governors = cpufreq_get_available_governors(cpu);
	if (governors) {
		printf(_("  available cpufreq governors: "));
		while (governors->next) {
			printf("%s, ", governors->governor);
			governors = governors->next;
		}
		printf("%s\n", governors->governor);
		cpufreq_put_available_governors(governors);
	}

	policy = cpufreq_get_policy(cpu);
	if (policy) {
		printf(_("  current policy: frequency should be within "));
		print_speed(policy->min);
		printf(_(" and "));
		print_speed(policy->max);

		printf(".\n                  ");
		printf(_("The governor \"%s\" may"
		       " decide which speed to use\n                  within this range.\n"),
		       policy->governor);
		cpufreq_put_policy(policy);
	}

	if (freq_kernel || freq_hardware) {
		printf(_("  current CPU frequency is "));
		if (freq_hardware) {
			print_speed(freq_hardware);
			printf(_(" (asserted by call to hardware)"));
		} else
			print_speed(freq_kernel);
		printf(".\n");
	}
	stats = cpufreq_get_stats(cpu, &total_time);
	if (stats) {
		printf(_("  cpufreq stats: "));
		while (stats) {
			print_speed(stats->frequency);
			printf(":%.2f%%", (100.0 * stats->time_in_state) / total_time);
			stats = stats->next;
			if (stats)
				printf(", ");
		}
		cpufreq_put_stats(stats);
		total_trans = cpufreq_get_transitions(cpu);
		if (total_trans)
			printf("  (%lu)\n", total_trans);
		else
			printf("\n");
	}
	get_boost_mode(cpu);

}

/* --freq / -f */

static int get_freq_kernel(unsigned int cpu, unsigned int human)
{
	unsigned long freq = cpufreq_get_freq_kernel(cpu);
	if (!freq)
		return -EINVAL;
	if (human) {
		print_speed(freq);
		printf("\n");
	} else
		printf("%lu\n", freq);
	return 0;
}


/* --hwfreq / -w */

static int get_freq_hardware(unsigned int cpu, unsigned int human)
{
	unsigned long freq = cpufreq_get_freq_hardware(cpu);
	if (!freq)
		return -EINVAL;
	if (human) {
		print_speed(freq);
		printf("\n");
	} else
		printf("%lu\n", freq);
	return 0;
}

/* --hwlimits / -l */

static int get_hardware_limits(unsigned int cpu)
{
	unsigned long min, max;
	if (cpufreq_get_hardware_limits(cpu, &min, &max))
		return -EINVAL;
	printf("%lu %lu\n", min, max);
	return 0;
}

/* --driver / -d */

static int get_driver(unsigned int cpu)
{
	char *driver = cpufreq_get_driver(cpu);
	if (!driver)
		return -EINVAL;
	printf("%s\n", driver);
	cpufreq_put_driver(driver);
	return 0;
}

/* --policy / -p */

static int get_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_get_policy(cpu);
	if (!policy)
		return -EINVAL;
	printf("%lu %lu %s\n", policy->min, policy->max, policy->governor);
	cpufreq_put_policy(policy);
	return 0;
}

/* --governors / -g */

static int get_available_governors(unsigned int cpu)
{
	struct cpufreq_available_governors *governors =
		cpufreq_get_available_governors(cpu);
	if (!governors)
		return -EINVAL;

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
	if (!cpus)
		return -EINVAL;

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
	if (!cpus)
		return -EINVAL;

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
	if (!latency)
		return -EINVAL;

	if (human) {
		print_duration(latency);
		printf("\n");
	} else
		printf("%lu\n", latency);
	return 0;
}

void freq_info_help(void)
{
	printf(_("Usage: cpupower freqinfo [options]\n"));
	printf(_("Options:\n"));
	printf(_("  -e, --debug          Prints out debug information [default]\n"));
	printf(_("  -f, --freq           Get frequency the CPU currently runs at, according\n"
	       "                       to the cpufreq core *\n"));
	printf(_("  -w, --hwfreq         Get frequency the CPU currently runs at, by reading\n"
	       "                       it from hardware (only available to root) *\n"));
	printf(_("  -l, --hwlimits       Determine the minimum and maximum CPU frequency allowed *\n"));
	printf(_("  -d, --driver         Determines the used cpufreq kernel driver *\n"));
	printf(_("  -p, --policy         Gets the currently used cpufreq policy *\n"));
	printf(_("  -g, --governors      Determines available cpufreq governors *\n"));
	printf(_("  -r, --related-cpus   Determines which CPUs run at the same hardware frequency *\n"));
	printf(_("  -a, --affected-cpus  Determines which CPUs need to have their frequency\n"
			"                       coordinated by software *\n"));
	printf(_("  -s, --stats          Shows cpufreq statistics if available\n"));
	printf(_("  -y, --latency        Determines the maximum latency on CPU frequency changes *\n"));
	printf(_("  -b, --boost          Checks for turbo or boost modes  *\n"));
	printf(_("  -o, --proc           Prints out information like provided by the /proc/cpufreq\n"
	       "                       interface in 2.4. and early 2.6. kernels\n"));
	printf(_("  -m, --human          human-readable output for the -f, -w, -s and -y parameters\n"));
	printf(_("  -h, --help           Prints out this screen\n"));

	printf("\n");
	printf(_("If no argument is given, full output about\n"
	       "cpufreq is printed which is useful e.g. for reporting bugs.\n\n"));
	printf(_("By default info of CPU 0 is shown which can be overridden\n"
		 "with the cpupower --cpu main command option.\n"));
}

static struct option info_opts[] = {
	{ .name = "debug",	.has_arg = no_argument,		.flag = NULL,	.val = 'e'},
	{ .name = "boost",	.has_arg = no_argument,		.flag = NULL,	.val = 'b'},
	{ .name = "freq",	.has_arg = no_argument,		.flag = NULL,	.val = 'f'},
	{ .name = "hwfreq",	.has_arg = no_argument,		.flag = NULL,	.val = 'w'},
	{ .name = "hwlimits",	.has_arg = no_argument,		.flag = NULL,	.val = 'l'},
	{ .name = "driver",	.has_arg = no_argument,		.flag = NULL,	.val = 'd'},
	{ .name = "policy",	.has_arg = no_argument,		.flag = NULL,	.val = 'p'},
	{ .name = "governors",	.has_arg = no_argument,		.flag = NULL,	.val = 'g'},
	{ .name = "related-cpus", .has_arg = no_argument,	.flag = NULL,	.val = 'r'},
	{ .name = "affected-cpus",.has_arg = no_argument,	.flag = NULL,	.val = 'a'},
	{ .name = "stats",	.has_arg = no_argument,		.flag = NULL,	.val = 's'},
	{ .name = "latency",	.has_arg = no_argument,		.flag = NULL,	.val = 'y'},
	{ .name = "proc",	.has_arg = no_argument,		.flag = NULL,	.val = 'o'},
	{ .name = "human",	.has_arg = no_argument,		.flag = NULL,	.val = 'm'},
	{ .name = "help",	.has_arg = no_argument,		.flag = NULL,	.val = 'h'},
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
		ret = getopt_long(argc, argv, "hoefwldpgrasmyb", info_opts, NULL);
		switch (ret) {
		case '?':
			output_param = '?';
			cont = 0;
			break;
		case 'h':
			output_param = 'h';
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
		freq_info_help();
		return -EINVAL;
	case 'h':
		freq_info_help();
		return EXIT_SUCCESS;
	case 'o':
		proc_cpufreq_output();
		return EXIT_SUCCESS;
	}

	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu))
			continue;
		if (cpufreq_cpu_exists(cpu)) {
			printf(_("couldn't analyze CPU %d as it doesn't seem to be present\n"), cpu);
			continue;
		}
		printf(_("analyzing CPU %d:\n"), cpu);

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
			ret = get_hardware_limits(cpu);
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
