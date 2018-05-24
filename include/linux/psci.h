/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2015 ARM Limited
 */

#ifndef __LINUX_PSCI_H
#define __LINUX_PSCI_H

#include <linux/init.h>
#include <linux/types.h>

#define PSCI_POWER_STATE_TYPE_STANDBY		0
#define PSCI_POWER_STATE_TYPE_POWER_DOWN	1

bool psci_tos_resident_on(int cpu);

int psci_cpu_init_idle(unsigned int cpu);
int psci_cpu_suspend_enter(unsigned long index);

enum psci_conduit {
	PSCI_CONDUIT_NONE,
	PSCI_CONDUIT_SMC,
	PSCI_CONDUIT_HVC,
};

enum smccc_version {
	SMCCC_VERSION_1_0,
	SMCCC_VERSION_1_1,
};

struct psci_operations {
	u32 (*get_version)(void);
	int (*cpu_suspend)(u32 state, unsigned long entry_point);
	int (*cpu_off)(u32 state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
	int (*affinity_info)(unsigned long target_affinity,
			unsigned long lowest_affinity_level);
	int (*migrate_info_type)(void);
	enum psci_conduit conduit;
	enum smccc_version smccc_version;
};

extern struct psci_operations psci_ops;

#if defined(CONFIG_ARM_PSCI_FW)
int __init psci_dt_init(void);
#else
static inline int psci_dt_init(void) { return 0; }
#endif

#if defined(CONFIG_ARM_PSCI_FW) && defined(CONFIG_ACPI)
int __init psci_acpi_init(void);
bool __init acpi_psci_present(void);
bool acpi_psci_use_hvc(void);
#else
static inline int psci_acpi_init(void) { return 0; }
static inline bool acpi_psci_present(void) { return false; }
static inline bool acpi_psci_use_hvc(void) {return false; }
#endif

#endif /* __LINUX_PSCI_H */
