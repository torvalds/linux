/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Functions and macros to control the flowcontroller
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 */

#ifndef __SOC_TEGRA_FLOWCTRL_H__
#define __SOC_TEGRA_FLOWCTRL_H__

#define FLOW_CTRL_HALT_CPU0_EVENTS	0x0
#define FLOW_CTRL_WAITEVENT		(2 << 29)
#define FLOW_CTRL_WAIT_FOR_INTERRUPT	(4 << 29)
#define FLOW_CTRL_JTAG_RESUME		(1 << 28)
#define FLOW_CTRL_SCLK_RESUME		(1 << 27)
#define FLOW_CTRL_HALT_CPU_IRQ		(1 << 10)
#define	FLOW_CTRL_HALT_CPU_FIQ		(1 << 8)
#define FLOW_CTRL_HALT_LIC_IRQ		(1 << 11)
#define FLOW_CTRL_HALT_LIC_FIQ		(1 << 10)
#define FLOW_CTRL_HALT_GIC_IRQ		(1 << 9)
#define FLOW_CTRL_HALT_GIC_FIQ		(1 << 8)
#define FLOW_CTRL_CPU0_CSR		0x8
#define	FLOW_CTRL_CSR_INTR_FLAG		(1 << 15)
#define FLOW_CTRL_CSR_EVENT_FLAG	(1 << 14)
#define FLOW_CTRL_CSR_ENABLE_EXT_CRAIL	(1 << 13)
#define FLOW_CTRL_CSR_ENABLE_EXT_NCPU	(1 << 12)
#define FLOW_CTRL_CSR_ENABLE_EXT_MASK ( \
		FLOW_CTRL_CSR_ENABLE_EXT_NCPU | \
		FLOW_CTRL_CSR_ENABLE_EXT_CRAIL)
#define FLOW_CTRL_CSR_ENABLE		(1 << 0)
#define FLOW_CTRL_HALT_CPU1_EVENTS	0x14
#define FLOW_CTRL_CPU1_CSR		0x18

#define TEGRA20_FLOW_CTRL_CSR_WFE_CPU0		(1 << 4)
#define TEGRA20_FLOW_CTRL_CSR_WFE_BITMAP	(3 << 4)
#define TEGRA20_FLOW_CTRL_CSR_WFI_BITMAP	0

#define TEGRA30_FLOW_CTRL_CSR_WFI_CPU0		(1 << 8)
#define TEGRA30_FLOW_CTRL_CSR_WFE_BITMAP	(0xF << 4)
#define TEGRA30_FLOW_CTRL_CSR_WFI_BITMAP	(0xF << 8)

#ifndef __ASSEMBLY__
#ifdef CONFIG_SOC_TEGRA_FLOWCTRL
u32 flowctrl_read_cpu_csr(unsigned int cpuid);
void flowctrl_write_cpu_csr(unsigned int cpuid, u32 value);
void flowctrl_write_cpu_halt(unsigned int cpuid, u32 value);

void flowctrl_cpu_suspend_enter(unsigned int cpuid);
void flowctrl_cpu_suspend_exit(unsigned int cpuid);
#else
static inline u32 flowctrl_read_cpu_csr(unsigned int cpuid)
{
	return 0;
}

static inline void flowctrl_write_cpu_csr(unsigned int cpuid, u32 value)
{
}

static inline void flowctrl_write_cpu_halt(unsigned int cpuid, u32 value) {}

static inline void flowctrl_cpu_suspend_enter(unsigned int cpuid)
{
}

static inline void flowctrl_cpu_suspend_exit(unsigned int cpuid)
{
}
#endif /* CONFIG_SOC_TEGRA_FLOWCTRL */
#endif /* __ASSEMBLY */
#endif /* __SOC_TEGRA_FLOWCTRL_H__ */
