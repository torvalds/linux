/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _TEGRA_PMC_H_
#define	_TEGRA_PMC_H_

enum tegra_suspend_mode {
      TEGRA_SUSPEND_NONE = 0,
      TEGRA_SUSPEND_LP2, 	/* CPU voltage off */
      TEGRA_SUSPEND_LP1, 	/* CPU voltage off, DRAM self-refresh */
      TEGRA_SUSPEND_LP0, 	/* CPU + core voltage off, DRAM self-refresh */
};

/* PARTIDs for powergate */
enum tegra_powergate_id {
	TEGRA_POWERGATE_CRAIL	= 0,
	TEGRA_POWERGATE_TD	= 1,
	TEGRA_POWERGATE_VE	= 2,
	TEGRA_POWERGATE_PCX	= 3,
	TEGRA_POWERGATE_VDE	= 4,
	TEGRA_POWERGATE_L2C	= 5,
	TEGRA_POWERGATE_MPE	= 6,
	TEGRA_POWERGATE_HEG	= 7,
	TEGRA_POWERGATE_SAX	= 8,
	TEGRA_POWERGATE_CE1	= 9,
	TEGRA_POWERGATE_CE2	= 10,
	TEGRA_POWERGATE_CE3	= 11,
	TEGRA_POWERGATE_CELP	= 12,
	/* */
	TEGRA_POWERGATE_CE0	= 14,
	TEGRA_POWERGATE_C0NC	= 15,
	TEGRA_POWERGATE_C1NC	= 16,
	TEGRA_POWERGATE_SOR	= 17,
	TEGRA_POWERGATE_DIS	= 18,
	TEGRA_POWERGATE_DISB	= 19,
	TEGRA_POWERGATE_XUSBA	= 20,
	TEGRA_POWERGATE_XUSBB	= 21,
	TEGRA_POWERGATE_XUSBC	= 22,
	TEGRA_POWERGATE_VIC	= 23,
	TEGRA_POWERGATE_IRAM	= 24,
	/* */
	TEGRA_POWERGATE_3D	= 32

};

/* PARTIDs for power rails */
enum tegra_powerrail_id {
	TEGRA_IO_RAIL_CSIA	= 0,
	TEGRA_IO_RAIL_CSIB	= 1,
	TEGRA_IO_RAIL_DSI	= 2,
	TEGRA_IO_RAIL_MIPI_BIAS	= 3,
	TEGRA_IO_RAIL_PEX_BIAS	= 4,
	TEGRA_IO_RAIL_PEX_CLK1	= 5,
	TEGRA_IO_RAIL_PEX_CLK2	= 6,
	TEGRA_IO_RAIL_USB0	= 9,
	TEGRA_IO_RAIL_USB1	= 10,
	TEGRA_IO_RAIL_USB2	= 11,
	TEGRA_IO_RAIL_USB_BIAS	= 12,
	TEGRA_IO_RAIL_NAND	= 13,
	TEGRA_IO_RAIL_UART	= 14,
	TEGRA_IO_RAIL_BB	= 15,
	TEGRA_IO_RAIL_AUDIO	= 17,
	TEGRA_IO_RAIL_HSIC	= 19,
	TEGRA_IO_RAIL_COMP	= 22,
	TEGRA_IO_RAIL_HDMI	= 28,
	TEGRA_IO_RAIL_PEX_CNTRL	= 32,
	TEGRA_IO_RAIL_SDMMC1	= 33,
	TEGRA_IO_RAIL_SDMMC3	= 34,
	TEGRA_IO_RAIL_SDMMC4	= 35,
	TEGRA_IO_RAIL_CAM	= 36,
	TEGRA_IO_RAIL_RES	= 37,
	TEGRA_IO_RAIL_HV	= 38,
	TEGRA_IO_RAIL_DSIB	= 39,
	TEGRA_IO_RAIL_DSIC	= 40,
	TEGRA_IO_RAIL_DSID	= 41,
	TEGRA_IO_RAIL_CSIE	= 44,
	TEGRA_IO_RAIL_LVDS	= 57,
	TEGRA_IO_RAIL_SYS_DDC	= 58,
};

int tegra_powergate_is_powered(enum tegra_powergate_id id);
int tegra_powergate_power_on(enum tegra_powergate_id id);
int tegra_powergate_power_off(enum tegra_powergate_id id);
int tegra_powergate_remove_clamping(enum tegra_powergate_id id);
int tegra_powergate_sequence_power_up(enum tegra_powergate_id id,
    clk_t clk, hwreset_t rst);
int tegra_io_rail_power_on(int tegra_powerrail_id);
int tegra_io_rail_power_off(int tegra_powerrail_id);

#endif /*_TEGRA_PMC_H_*/