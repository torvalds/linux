// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "helpers/helpers.h"
#include "cpufreq.h"
#include "acpi_cppc.h"

#define cppc_to_frequency(perf) (roundf(slope * (perf) + intercept))

void cppc_show_perf_and_freq(unsigned int cpu, int no_rounding)
{
	int64_t nominal = acpi_cppc_get_data(cpu, NOMINAL_PERF);
	int64_t nominal_freq = acpi_cppc_get_data(cpu, NOMINAL_FREQ) * 1000;
	int64_t lowest = acpi_cppc_get_data(cpu, LOWEST_PERF);
	int64_t lowest_freq = acpi_cppc_get_data(cpu, LOWEST_FREQ) * 1000;
	unsigned long non_linear = acpi_cppc_get_data(cpu, LOWEST_NONLINEAR_PERF);
	unsigned long highest = acpi_cppc_get_data(cpu, HIGHEST_PERF);
	float slope, intercept;

	/* do the optional freq fields look invalid? */
	if (!nominal_freq || !lowest_freq || nominal == lowest)
		return;

	slope = (float)(nominal_freq - lowest_freq) / (nominal - lowest);
	intercept = lowest_freq - slope * lowest;

	printf(_("  CPPC limits:\n"));
	printf(_("    Highest Performance: %lu. Maximum Frequency: "),
	       highest);
	/*
	 * If boost isn't active, the cpuinfo_max doesn't indicate real max
	 * frequency.
	 */
	print_speed(cppc_to_frequency(highest), no_rounding);
	printf(".\n");

	printf(_("    Nominal Performance: %lu. Nominal Frequency: "),
	       acpi_cppc_get_data(cpu, NOMINAL_PERF));
	print_speed(nominal_freq,  no_rounding);
	printf(".\n");

	printf(_("    Lowest Non-linear Performance: %lu. Lowest Non-linear Frequency: "),
	       non_linear);
	print_speed(cppc_to_frequency(non_linear), no_rounding);
	printf(".\n");

	printf(_("    Lowest Performance: %lu. Lowest Frequency: "),
	       acpi_cppc_get_data(cpu, LOWEST_PERF));
	print_speed(lowest_freq, no_rounding);
	printf(".\n");
}
