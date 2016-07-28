/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LINUX_CLK_TEGRA_H_
#define __LINUX_CLK_TEGRA_H_

#include <linux/types.h>
#include <linux/bug.h>

/*
 * Tegra CPU clock and reset control ops
 *
 * wait_for_reset:
 *	keep waiting until the CPU in reset state
 * put_in_reset:
 *	put the CPU in reset state
 * out_of_reset:
 *	release the CPU from reset state
 * enable_clock:
 *	CPU clock un-gate
 * disable_clock:
 *	CPU clock gate
 * rail_off_ready:
 *	CPU is ready for rail off
 * suspend:
 *	save the clock settings when CPU go into low-power state
 * resume:
 *	restore the clock settings when CPU exit low-power state
 */
struct tegra_cpu_car_ops {
	void (*wait_for_reset)(u32 cpu);
	void (*put_in_reset)(u32 cpu);
	void (*out_of_reset)(u32 cpu);
	void (*enable_clock)(u32 cpu);
	void (*disable_clock)(u32 cpu);
#ifdef CONFIG_PM_SLEEP
	bool (*rail_off_ready)(void);
	void (*suspend)(void);
	void (*resume)(void);
#endif
};

extern struct tegra_cpu_car_ops *tegra_cpu_car_ops;

static inline void tegra_wait_cpu_in_reset(u32 cpu)
{
	if (WARN_ON(!tegra_cpu_car_ops->wait_for_reset))
		return;

	tegra_cpu_car_ops->wait_for_reset(cpu);
}

static inline void tegra_put_cpu_in_reset(u32 cpu)
{
	if (WARN_ON(!tegra_cpu_car_ops->put_in_reset))
		return;

	tegra_cpu_car_ops->put_in_reset(cpu);
}

static inline void tegra_cpu_out_of_reset(u32 cpu)
{
	if (WARN_ON(!tegra_cpu_car_ops->out_of_reset))
		return;

	tegra_cpu_car_ops->out_of_reset(cpu);
}

static inline void tegra_enable_cpu_clock(u32 cpu)
{
	if (WARN_ON(!tegra_cpu_car_ops->enable_clock))
		return;

	tegra_cpu_car_ops->enable_clock(cpu);
}

static inline void tegra_disable_cpu_clock(u32 cpu)
{
	if (WARN_ON(!tegra_cpu_car_ops->disable_clock))
		return;

	tegra_cpu_car_ops->disable_clock(cpu);
}

#ifdef CONFIG_PM_SLEEP
static inline bool tegra_cpu_rail_off_ready(void)
{
	if (WARN_ON(!tegra_cpu_car_ops->rail_off_ready))
		return false;

	return tegra_cpu_car_ops->rail_off_ready();
}

static inline void tegra_cpu_clock_suspend(void)
{
	if (WARN_ON(!tegra_cpu_car_ops->suspend))
		return;

	tegra_cpu_car_ops->suspend();
}

static inline void tegra_cpu_clock_resume(void)
{
	if (WARN_ON(!tegra_cpu_car_ops->resume))
		return;

	tegra_cpu_car_ops->resume();
}
#endif

extern void tegra210_xusb_pll_hw_control_enable(void);
extern void tegra210_xusb_pll_hw_sequence_start(void);
extern void tegra210_sata_pll_hw_control_enable(void);
extern void tegra210_sata_pll_hw_sequence_start(void);

#endif /* __LINUX_CLK_TEGRA_H_ */
