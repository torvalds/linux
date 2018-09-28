/*
 * Copyright (c) 2010 Google, Inc
 * Copyright (c) 2014 NVIDIA Corporation
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SOC_TEGRA_PMC_H__
#define __SOC_TEGRA_PMC_H__

#include <linux/reboot.h>

#include <soc/tegra/pm.h>

struct clk;
struct reset_control;

#ifdef CONFIG_SMP
bool tegra_pmc_cpu_is_powered(unsigned int cpuid);
int tegra_pmc_cpu_power_on(unsigned int cpuid);
int tegra_pmc_cpu_remove_clamping(unsigned int cpuid);
#endif /* CONFIG_SMP */

/*
 * powergate and I/O rail APIs
 */

#define TEGRA_POWERGATE_CPU	0
#define TEGRA_POWERGATE_3D	1
#define TEGRA_POWERGATE_VENC	2
#define TEGRA_POWERGATE_PCIE	3
#define TEGRA_POWERGATE_VDEC	4
#define TEGRA_POWERGATE_L2	5
#define TEGRA_POWERGATE_MPE	6
#define TEGRA_POWERGATE_HEG	7
#define TEGRA_POWERGATE_SATA	8
#define TEGRA_POWERGATE_CPU1	9
#define TEGRA_POWERGATE_CPU2	10
#define TEGRA_POWERGATE_CPU3	11
#define TEGRA_POWERGATE_CELP	12
#define TEGRA_POWERGATE_3D1	13
#define TEGRA_POWERGATE_CPU0	14
#define TEGRA_POWERGATE_C0NC	15
#define TEGRA_POWERGATE_C1NC	16
#define TEGRA_POWERGATE_SOR	17
#define TEGRA_POWERGATE_DIS	18
#define TEGRA_POWERGATE_DISB	19
#define TEGRA_POWERGATE_XUSBA	20
#define TEGRA_POWERGATE_XUSBB	21
#define TEGRA_POWERGATE_XUSBC	22
#define TEGRA_POWERGATE_VIC	23
#define TEGRA_POWERGATE_IRAM	24
#define TEGRA_POWERGATE_NVDEC	25
#define TEGRA_POWERGATE_NVJPG	26
#define TEGRA_POWERGATE_AUD	27
#define TEGRA_POWERGATE_DFD	28
#define TEGRA_POWERGATE_VE2	29
#define TEGRA_POWERGATE_MAX	TEGRA_POWERGATE_VE2

#define TEGRA_POWERGATE_3D0	TEGRA_POWERGATE_3D

/**
 * enum tegra_io_pad - I/O pad group identifier
 *
 * I/O pins on Tegra SoCs are grouped into so-called I/O pads. Each such pad
 * can be used to control the common voltage signal level and power state of
 * the pins of the given pad.
 */
enum tegra_io_pad {
	TEGRA_IO_PAD_AUDIO,
	TEGRA_IO_PAD_AUDIO_HV,
	TEGRA_IO_PAD_BB,
	TEGRA_IO_PAD_CAM,
	TEGRA_IO_PAD_COMP,
	TEGRA_IO_PAD_CONN,
	TEGRA_IO_PAD_CSIA,
	TEGRA_IO_PAD_CSIB,
	TEGRA_IO_PAD_CSIC,
	TEGRA_IO_PAD_CSID,
	TEGRA_IO_PAD_CSIE,
	TEGRA_IO_PAD_CSIF,
	TEGRA_IO_PAD_DBG,
	TEGRA_IO_PAD_DEBUG_NONAO,
	TEGRA_IO_PAD_DMIC,
	TEGRA_IO_PAD_DMIC_HV,
	TEGRA_IO_PAD_DP,
	TEGRA_IO_PAD_DSI,
	TEGRA_IO_PAD_DSIB,
	TEGRA_IO_PAD_DSIC,
	TEGRA_IO_PAD_DSID,
	TEGRA_IO_PAD_EDP,
	TEGRA_IO_PAD_EMMC,
	TEGRA_IO_PAD_EMMC2,
	TEGRA_IO_PAD_GPIO,
	TEGRA_IO_PAD_HDMI,
	TEGRA_IO_PAD_HDMI_DP0,
	TEGRA_IO_PAD_HDMI_DP1,
	TEGRA_IO_PAD_HSIC,
	TEGRA_IO_PAD_HV,
	TEGRA_IO_PAD_LVDS,
	TEGRA_IO_PAD_MIPI_BIAS,
	TEGRA_IO_PAD_NAND,
	TEGRA_IO_PAD_PEX_BIAS,
	TEGRA_IO_PAD_PEX_CLK_BIAS,
	TEGRA_IO_PAD_PEX_CLK1,
	TEGRA_IO_PAD_PEX_CLK2,
	TEGRA_IO_PAD_PEX_CLK3,
	TEGRA_IO_PAD_PEX_CNTRL,
	TEGRA_IO_PAD_SDMMC1,
	TEGRA_IO_PAD_SDMMC1_HV,
	TEGRA_IO_PAD_SDMMC2,
	TEGRA_IO_PAD_SDMMC2_HV,
	TEGRA_IO_PAD_SDMMC3,
	TEGRA_IO_PAD_SDMMC3_HV,
	TEGRA_IO_PAD_SDMMC4,
	TEGRA_IO_PAD_SPI,
	TEGRA_IO_PAD_SPI_HV,
	TEGRA_IO_PAD_SYS_DDC,
	TEGRA_IO_PAD_UART,
	TEGRA_IO_PAD_UFS,
	TEGRA_IO_PAD_USB0,
	TEGRA_IO_PAD_USB1,
	TEGRA_IO_PAD_USB2,
	TEGRA_IO_PAD_USB3,
	TEGRA_IO_PAD_USB_BIAS,
	TEGRA_IO_PAD_AO_HV,
};

/* deprecated, use TEGRA_IO_PAD_{HDMI,LVDS} instead */
#define TEGRA_IO_RAIL_HDMI	TEGRA_IO_PAD_HDMI
#define TEGRA_IO_RAIL_LVDS	TEGRA_IO_PAD_LVDS

#ifdef CONFIG_SOC_TEGRA_PMC
int tegra_powergate_is_powered(unsigned int id);
int tegra_powergate_power_on(unsigned int id);
int tegra_powergate_power_off(unsigned int id);
int tegra_powergate_remove_clamping(unsigned int id);

/* Must be called with clk disabled, and returns with clk enabled */
int tegra_powergate_sequence_power_up(unsigned int id, struct clk *clk,
				      struct reset_control *rst);

int tegra_io_pad_power_enable(enum tegra_io_pad id);
int tegra_io_pad_power_disable(enum tegra_io_pad id);

/* deprecated, use tegra_io_pad_power_{enable,disable}() instead */
int tegra_io_rail_power_on(unsigned int id);
int tegra_io_rail_power_off(unsigned int id);

enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void);
void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode);
void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode);

#else
static inline int tegra_powergate_is_powered(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_power_on(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_power_off(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_remove_clamping(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_sequence_power_up(unsigned int id,
						    struct clk *clk,
						    struct reset_control *rst)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_power_enable(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_power_disable(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_get_voltage(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_on(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_off(unsigned int id)
{
	return -ENOSYS;
}

static inline enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void)
{
	return TEGRA_SUSPEND_NONE;
}

static inline void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode)
{
}

static inline void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode)
{
}

#endif /* CONFIG_SOC_TEGRA_PMC */

#endif /* __SOC_TEGRA_PMC_H__ */
