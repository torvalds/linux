/*
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_CPUFEATURE_H
#define __LINUX_CPUFEATURE_H

#ifdef CONFIG_GENERIC_CPU_AUTOPROBE

#include <linux/mod_devicetable.h>
#include <asm/cpufeature.h>

/*
 * Macros imported from <asm/cpufeature.h>:
 * - cpu_feature(x)		ordinal value of feature called 'x'
 * - cpu_have_feature(u32 n)	whether feature #n is available
 * - MAX_CPU_FEATURES		upper bound for feature ordinal values
 * Optional:
 * - CPU_FEATURE_TYPEFMT	format string fragment for printing the cpu type
 * - CPU_FEATURE_TYPEVAL	set of values matching the format string above
 */

#ifndef CPU_FEATURE_TYPEFMT
#define CPU_FEATURE_TYPEFMT	"%s"
#endif

#ifndef CPU_FEATURE_TYPEVAL
#define CPU_FEATURE_TYPEVAL	ELF_PLATFORM
#endif

/*
 * Use module_cpu_feature_match(feature, module_init_function) to
 * declare that
 * a) the module shall be probed upon discovery of CPU feature 'feature'
 *    (typically at boot time using udev)
 * b) the module must not be loaded if CPU feature 'feature' is not present
 *    (not even by manual insmod).
 *
 * For a list of legal values for 'feature', please consult the file
 * 'asm/cpufeature.h' of your favorite architecture.
 */
#define module_cpu_feature_match(x, __init)			\
static struct cpu_feature const cpu_feature_match_ ## x[] =	\
	{ { .feature = cpu_feature(x) }, { } };			\
MODULE_DEVICE_TABLE(cpu, cpu_feature_match_ ## x);		\
								\
static int cpu_feature_match_ ## x ## _init(void)		\
{								\
	if (!cpu_have_feature(cpu_feature(x)))			\
		return -ENODEV;					\
	return __init();					\
}								\
module_init(cpu_feature_match_ ## x ## _init)

#endif
#endif
