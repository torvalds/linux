/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_CPPC_H__
#define __ACPI_CPPC_H__

enum acpi_cppc_value {
	HIGHEST_PERF,
	LOWEST_PERF,
	NOMINAL_PERF,
	LOWEST_NONLINEAR_PERF,
	LOWEST_FREQ,
	NOMINAL_FREQ,
	REFERENCE_PERF,
	WRAPAROUND_TIME,
	MAX_CPPC_VALUE_FILES
};

unsigned long acpi_cppc_get_data(unsigned int cpu,
				 enum acpi_cppc_value which);

#endif /* _ACPI_CPPC_H */
