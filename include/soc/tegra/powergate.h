/*
 * Copyright (c) 2010 Google, Inc
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

#ifndef __SOC_TEGRA_POWERGATE_H__
#define __SOC_TEGRA_POWERGATE_H__

struct clk;
struct reset_control;

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

#define TEGRA_POWERGATE_3D0	TEGRA_POWERGATE_3D

#define TEGRA_IO_RAIL_CSIA	0
#define TEGRA_IO_RAIL_CSIB	1
#define TEGRA_IO_RAIL_DSI	2
#define TEGRA_IO_RAIL_MIPI_BIAS	3
#define TEGRA_IO_RAIL_PEX_BIAS	4
#define TEGRA_IO_RAIL_PEX_CLK1	5
#define TEGRA_IO_RAIL_PEX_CLK2	6
#define TEGRA_IO_RAIL_USB0	9
#define TEGRA_IO_RAIL_USB1	10
#define TEGRA_IO_RAIL_USB2	11
#define TEGRA_IO_RAIL_USB_BIAS	12
#define TEGRA_IO_RAIL_NAND	13
#define TEGRA_IO_RAIL_UART	14
#define TEGRA_IO_RAIL_BB	15
#define TEGRA_IO_RAIL_AUDIO	17
#define TEGRA_IO_RAIL_HSIC	19
#define TEGRA_IO_RAIL_COMP	22
#define TEGRA_IO_RAIL_HDMI	28
#define TEGRA_IO_RAIL_PEX_CNTRL	32
#define TEGRA_IO_RAIL_SDMMC1	33
#define TEGRA_IO_RAIL_SDMMC3	34
#define TEGRA_IO_RAIL_SDMMC4	35
#define TEGRA_IO_RAIL_CAM	36
#define TEGRA_IO_RAIL_RES	37
#define TEGRA_IO_RAIL_HV	38
#define TEGRA_IO_RAIL_DSIB	39
#define TEGRA_IO_RAIL_DSIC	40
#define TEGRA_IO_RAIL_DSID	41
#define TEGRA_IO_RAIL_CSIE	44
#define TEGRA_IO_RAIL_LVDS	57
#define TEGRA_IO_RAIL_SYS_DDC	58

#ifdef CONFIG_ARCH_TEGRA
int tegra_powergate_is_powered(int id);
int tegra_powergate_power_on(int id);
int tegra_powergate_power_off(int id);
int tegra_powergate_remove_clamping(int id);

/* Must be called with clk disabled, and returns with clk enabled */
int tegra_powergate_sequence_power_up(int id, struct clk *clk,
				      struct reset_control *rst);

int tegra_io_rail_power_on(int id);
int tegra_io_rail_power_off(int id);
#else
static inline int tegra_powergate_is_powered(int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_power_on(int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_power_off(int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_remove_clamping(int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_sequence_power_up(int id, struct clk *clk,
						    struct reset_control *rst)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_on(int id)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_off(int id)
{
	return -ENOSYS;
}
#endif

#endif /* __SOC_TEGRA_POWERGATE_H__ */
