/*
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/*
 * Support for the Trusted Foundations secure monitor.
 *
 * Trusted Foundation comes active on some ARM consumer devices (most
 * Tegra-based devices sold on the market are concerned). Such devices can only
 * perform some basic operations, like setting the CPU reset vector, through
 * SMC calls to the secure monitor. The calls are completely specific to
 * Trusted Foundations, and do *not* follow the SMC calling convention or the
 * PSCI standard.
 */

#ifndef __FIRMWARE_TRUSTED_FOUNDATIONS_H
#define __FIRMWARE_TRUSTED_FOUNDATIONS_H

#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/outercache.h>

#define TF_PM_MODE_LP0			0
#define TF_PM_MODE_LP1			1
#define TF_PM_MODE_LP1_NO_MC_CLK	2
#define TF_PM_MODE_LP2			3
#define TF_PM_MODE_LP2_NOFLUSH_L2	4

struct trusted_foundations_platform_data {
	unsigned int version_major;
	unsigned int version_minor;
};

#if IS_ENABLED(CONFIG_TRUSTED_FOUNDATIONS)

void register_trusted_foundations(struct trusted_foundations_platform_data *pd);
void of_register_trusted_foundations(void);
bool trusted_foundations_registered(void);

#else /* CONFIG_TRUSTED_FOUNDATIONS */
static inline void tf_dummy_write_sec(unsigned long val, unsigned int reg)
{
}

static inline void register_trusted_foundations(
				   struct trusted_foundations_platform_data *pd)
{
	/*
	 * If the system requires TF and we cannot provide it, continue booting
	 * but disable features that cannot be provided.
	 */
	pr_err("No support for Trusted Foundations, continuing in degraded mode.\n");
	pr_err("Secondary processors as well as CPU PM will be disabled.\n");
#if IS_ENABLED(CONFIG_CACHE_L2X0)
	pr_err("L2X0 cache will be kept disabled.\n");
	outer_cache.write_sec = tf_dummy_write_sec;
#endif
#if IS_ENABLED(CONFIG_SMP)
	setup_max_cpus = 0;
#endif
	cpu_idle_poll_ctrl(true);
}

static inline void of_register_trusted_foundations(void)
{
	/*
	 * If we find the target should enable TF but does not support it,
	 * fail as the system won't be able to do much anyway
	 */
	if (of_find_compatible_node(NULL, NULL, "tlm,trusted-foundations"))
		register_trusted_foundations(NULL);
}

static inline bool trusted_foundations_registered(void)
{
	return false;
}
#endif /* CONFIG_TRUSTED_FOUNDATIONS */

#endif
