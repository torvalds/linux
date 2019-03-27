/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 M. Warner Losh.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this software may have been developed with reference to
 * the SD Simplified Specification.  The following disclaimer may apply:
 *
 * The following conditions apply to the release of the simplified
 * specification ("Simplified Specification") by the SD Card Association and
 * the SD Group. The Simplified Specification is a subset of the complete SD
 * Specification which is owned by the SD Card Association and the SD
 * Group. This Simplified Specification is provided on a non-confidential
 * basis subject to the disclaimers below. Any implementation of the
 * Simplified Specification may require a license from the SD Card
 * Association, SD Group, SD-3C LLC or other third parties.
 *
 * Disclaimers:
 *
 * The information contained in the Simplified Specification is presented only
 * as a standard specification for SD Cards and SD Host/Ancillary products and
 * is provided "AS-IS" without any representations or warranties of any
 * kind. No responsibility is assumed by the SD Group, SD-3C LLC or the SD
 * Card Association for any damages, any infringements of patents or other
 * right of the SD Group, SD-3C LLC, the SD Card Association or any third
 * parties, which may result from its use. No license is granted by
 * implication, estoppel or otherwise under any patent or other rights of the
 * SD Group, SD-3C LLC, the SD Card Association or any third party. Nothing
 * herein shall be construed as an obligation by the SD Group, the SD-3C LLC
 * or the SD Card Association to disclose or distribute any technical
 * information, know-how or other confidential information to any third party.
 *
 * $FreeBSD$
 */

#ifndef DEV_MMC_BRIDGE_H
#define	DEV_MMC_BRIDGE_H

#include <sys/bus.h>

/*
 * This file defines interfaces for the mmc bridge.  The names chosen
 * are similar to or the same as the names used in Linux to allow for
 * easy porting of what Linux calls mmc host drivers.  I use the
 * FreeBSD terminology of bridge and bus for consistency with other
 * drivers in the system.  This file corresponds roughly to the Linux
 * linux/mmc/host.h file.
 *
 * A mmc bridge is a chipset that can have one or more mmc and/or sd
 * cards attached to it.  mmc devices are attached on a bus topology,
 * while sd and sdio cards usually are attached using a star topology
 * (meaning in practice each sd card has its own, independent slot).
 * Since SDHCI v3.00, buses for esd and esdio are possible, though.
 *
 * Attached to the mmc bridge is an mmcbus.  The mmcbus is described
 * in dev/mmc/mmcbus_if.m.
 */

/*
 * mmc_ios is a structure that is used to store the state of the mmc/sd
 * bus configuration.  This include the bus' clock speed, its voltage,
 * the bus mode for command output, the SPI chip select, some power
 * states and the bus width.
 */
enum mmc_vdd {
	vdd_150 = 0, vdd_155, vdd_160, vdd_165, vdd_170, vdd_180,
	vdd_190, vdd_200, vdd_210, vdd_220, vdd_230, vdd_240, vdd_250,
	vdd_260, vdd_270, vdd_280, vdd_290, vdd_300, vdd_310, vdd_320,
	vdd_330, vdd_340, vdd_350, vdd_360
};

enum mmc_vccq {
	vccq_120 = 0, vccq_180, vccq_330
};

enum mmc_power_mode {
	power_off = 0, power_up, power_on
};

enum mmc_bus_mode {
	opendrain = 1, pushpull
};

enum mmc_chip_select {
	cs_dontcare = 0, cs_high, cs_low
};

enum mmc_bus_width {
	bus_width_1 = 0, bus_width_4 = 2, bus_width_8 = 3
};

enum mmc_drv_type {
	drv_type_b = 0, drv_type_a, drv_type_c, drv_type_d
};

enum mmc_bus_timing {
	bus_timing_normal = 0, bus_timing_hs, bus_timing_uhs_sdr12,
	bus_timing_uhs_sdr25, bus_timing_uhs_sdr50, bus_timing_uhs_ddr50,
	bus_timing_uhs_sdr104, bus_timing_mmc_ddr52, bus_timing_mmc_hs200,
	bus_timing_mmc_hs400, bus_timing_mmc_hs400es, bus_timing_max =
	bus_timing_mmc_hs400es
};

struct mmc_ios {
	uint32_t	clock;	/* Speed of the clock in Hz to move data */
	enum mmc_vdd	vdd;	/* Voltage to apply to the power pins */
	enum mmc_vccq	vccq;	/* Voltage to use for signaling */
	enum mmc_bus_mode bus_mode;
	enum mmc_chip_select chip_select;
	enum mmc_bus_width bus_width;
	enum mmc_power_mode power_mode;
	enum mmc_bus_timing timing;
	enum mmc_drv_type drv_type;
};

enum mmc_card_mode {
	mode_mmc, mode_sd
};

enum mmc_retune_req {
	retune_req_none = 0, retune_req_normal, retune_req_reset
};

struct mmc_host {
	int f_min;
	int f_max;
	uint32_t host_ocr;
	uint32_t ocr;
	uint32_t caps;
#define	MMC_CAP_4_BIT_DATA	(1 <<  0) /* Can do 4-bit data transfers */
#define	MMC_CAP_8_BIT_DATA	(1 <<  1) /* Can do 8-bit data transfers */
#define	MMC_CAP_HSPEED		(1 <<  2) /* Can do High Speed transfers */
#define	MMC_CAP_BOOT_NOACC	(1 <<  4) /* Cannot access boot partitions */
#define	MMC_CAP_WAIT_WHILE_BUSY	(1 <<  5) /* Host waits for busy responses */
#define	MMC_CAP_UHS_SDR12	(1 <<  6) /* Can do UHS SDR12 */
#define	MMC_CAP_UHS_SDR25	(1 <<  7) /* Can do UHS SDR25 */
#define	MMC_CAP_UHS_SDR50	(1 <<  8) /* Can do UHS SDR50 */
#define	MMC_CAP_UHS_SDR104	(1 <<  9) /* Can do UHS SDR104 */
#define	MMC_CAP_UHS_DDR50	(1 << 10) /* Can do UHS DDR50 */
#define	MMC_CAP_MMC_DDR52_120	(1 << 11) /* Can do eMMC DDR52 at 1.2 V */
#define	MMC_CAP_MMC_DDR52_180	(1 << 12) /* Can do eMMC DDR52 at 1.8 V */
#define	MMC_CAP_MMC_DDR52	(MMC_CAP_MMC_DDR52_120 | MMC_CAP_MMC_DDR52_180)
#define	MMC_CAP_MMC_HS200_120	(1 << 13) /* Can do eMMC HS200 at 1.2 V */
#define	MMC_CAP_MMC_HS200_180	(1 << 14) /* Can do eMMC HS200 at 1.8 V */
#define	MMC_CAP_MMC_HS200	(MMC_CAP_MMC_HS200_120| MMC_CAP_MMC_HS200_180)
#define	MMC_CAP_MMC_HS400_120	(1 << 15) /* Can do eMMC HS400 at 1.2 V */
#define	MMC_CAP_MMC_HS400_180	(1 << 16) /* Can do eMMC HS400 at 1.8 V */
#define	MMC_CAP_MMC_HS400	(MMC_CAP_MMC_HS400_120 | MMC_CAP_MMC_HS400_180)
#define	MMC_CAP_MMC_HSX00_120	(MMC_CAP_MMC_HS200_120 | MMC_CAP_MMC_HS400_120)
#define	MMC_CAP_MMC_ENH_STROBE	(1 << 17) /* Can do eMMC Enhanced Strobe */
#define	MMC_CAP_SIGNALING_120	(1 << 18) /* Can do signaling at 1.2 V */
#define	MMC_CAP_SIGNALING_180	(1 << 19) /* Can do signaling at 1.8 V */
#define	MMC_CAP_SIGNALING_330	(1 << 20) /* Can do signaling at 3.3 V */
#define	MMC_CAP_DRIVER_TYPE_A	(1 << 21) /* Can do Driver Type A */
#define	MMC_CAP_DRIVER_TYPE_C	(1 << 22) /* Can do Driver Type C */
#define	MMC_CAP_DRIVER_TYPE_D	(1 << 23) /* Can do Driver Type D */
	enum mmc_card_mode mode;
	struct mmc_ios ios;	/* Current state of the host */
};

#ifdef _KERNEL
extern driver_t   mmc_driver;
extern devclass_t mmc_devclass;

#define	MMC_VERSION	5

#define	MMC_DECLARE_BRIDGE(name)					\
    DRIVER_MODULE(mmc, name, mmc_driver, mmc_devclass, NULL, NULL);	\
    MODULE_DEPEND(name, mmc, MMC_VERSION, MMC_VERSION, MMC_VERSION);
#define	MMC_DEPEND(name)						\
    MODULE_DEPEND(name, mmc, MMC_VERSION, MMC_VERSION, MMC_VERSION);
#endif /* _KERNEL */

#endif /* DEV_MMC_BRIDGE_H */
