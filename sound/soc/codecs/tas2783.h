/* SPDX-License-Identifier: GPL-2.0
 *
 * ALSA SoC Texas Instruments TAS2783 Audio Smart Amplifier
 *
 * Copyright (C) 2025 Texas Instruments Incorporated
 * https://www.ti.com
 *
 * The TAS2783 driver implements a flexible and configurable
 * algo coefficient setting for single TAS2783 chips.
 *
 * Author: Niranjan H Y <niranjanhy@ti.com>
 * Author: Baojun Xu <baojun.xu@ti.com>
 */
#include <linux/workqueue.h>

#ifndef __TAS2783_H__
#define __TAS2783_H__

#define TAS2783_DEVICE_RATES	(SNDRV_PCM_RATE_44100 | \
				SNDRV_PCM_RATE_48000 | \
				SNDRV_PCM_RATE_96000 | \
				SNDRV_PCM_RATE_88200)
#define TAS2783_DEVICE_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_S32_LE)

/* book, page, register */
#define TASDEV_REG_SDW(book, page, reg)	(((book) * 256 * 128) + \
		0x800000 + ((page) * 128) + (reg))

/* Volume control */
#define TAS2783_DVC_LVL		TASDEV_REG_SDW(0x0, 0x00, 0x1A)
#define TAS2783_AMP_LEVEL	TASDEV_REG_SDW(0x0, 0x00, 0x03)
#define TAS2783_AMP_LEVEL_MASK	GENMASK(5, 1)

#define PRAM_ADDR_START		TASDEV_REG_SDW(0x8c, 0x01, 0x8)
#define PRAM_ADDR_END		TASDEV_REG_SDW(0x8c, 0xff, 0x7f)
#define YRAM_ADDR_START		TASDEV_REG_SDW(0x00, 0x02, 0x8)
#define YRAM_ADDR_END		TASDEV_REG_SDW(0x00, 0x37, 0x7f)

/* Calibration data */
#define TAS2783_CAL_R0		TASDEV_REG_SDW(0, 0x16, 0x4C)
#define TAS2783_CAL_INVR0	TASDEV_REG_SDW(0, 0x16, 0x5C)
#define TAS2783_CAL_R0LOW	TASDEV_REG_SDW(0, 0x16, 0x64)
#define TAS2783_CAL_POWER	TASDEV_REG_SDW(0, 0x15, 0x44)
#define TAS2783_CAL_TLIM	TASDEV_REG_SDW(0, 0x17, 0x58)

/* TAS2783 SDCA Control - function number */
#define FUNC_NUM_SMART_AMP	0x01

/* TAS2783 SDCA entity */

#define TAS2783_SDCA_ENT_FU21		0x01
#define TAS2783_SDCA_ENT_FU23		0x02
#define TAS2783_SDCA_ENT_FU26		0x03
#define TAS2783_SDCA_ENT_XU22		0x04
#define TAS2783_SDCA_ENT_CS24		0x05
#define TAS2783_SDCA_ENT_CS21		0x06
#define TAS2783_SDCA_ENT_CS25		0x07
#define TAS2783_SDCA_ENT_CS26		0x08
#define TAS2783_SDCA_ENT_CS28		0x09
#define TAS2783_SDCA_ENT_PDE23		0x0C
#define TAS2783_SDCA_ENT_UDMPU23	0x0E
#define TAS2783_SDCA_ENT_SAPU29		0x0F
#define TAS2783_SDCA_ENT_PPU21		0x10
#define TAS2783_SDCA_ENT_PPU26		0x11
#define TAS2783_SDCA_ENT_TG23		0x12
#define TAS2783_SDCA_ENT_IT21		0x13
#define TAS2783_SDCA_ENT_IT29		0x14
#define TAS2783_SDCA_ENT_IT26		0x15
#define TAS2783_SDCA_ENT_IT28		0x16
#define TAS2783_SDCA_ENT_OT24		0x17
#define TAS2783_SDCA_ENT_OT23		0x18
#define TAS2783_SDCA_ENT_OT25		0x19
#define TAS2783_SDCA_ENT_OT28		0x1A
#define TAS2783_SDCA_ENT_MU26		0x1b
#define TAS2783_SDCA_ENT_OT127		0x1E
#define TAS2783_SDCA_ENT_FU127		0x1F
#define TAS2783_SDCA_ENT_CS127		0x20
#define TAS2783_SDCA_ENT_MFPU21		0x22
#define TAS2783_SDCA_ENT_MFPU26		0x23

/* TAS2783 SDCA control */
#define TAS2783_SDCA_CTL_REQ_POW_STATE	0x01
#define TAS2783_SDCA_CTL_FU_MUTE	0x01
#define TAS2783_SDCA_CTL_UDMPU_CLUSTER	0x10

#define TAS2783_DEVICE_CHANNEL_LEFT	1
#define TAS2783_DEVICE_CHANNEL_RIGHT	2

#define TAS2783_SDCA_POW_STATE_ON 0
#define TAS2783_SDCA_POW_STATE_OFF 3

/* calibration data */
#define TAS2783_CALIB_PARAMS	6 /* 5 + 1 unique id */
#define TAS2783_CALIB_MAX_SPK_COUNT	8
#define TAS2783_CALIB_HDR_SZ	12
#define TAS2783_CALIB_CRC_SZ	4
#define TAS2783_CALIB_DATA_SZ	((TAS2783_CALIB_HDR_SZ) + TAS2783_CALIB_CRC_SZ + \
				((TAS2783_CALIB_PARAMS) * 4 * (TAS2783_CALIB_MAX_SPK_COUNT)))

#if IS_ENABLED(CONFIG_SND_SOC_TAS2783_UTIL)
int32_t tas25xx_register_misc(struct sdw_slave *peripheral);
int32_t tas25xx_deregister_misc(void);
#else
static void tas25xx_register_misc(struct sdw_slave *peripheral) {}
static void tas25xx_deregister_misc(void) {}
#endif

#endif /*__TAS2783_H__ */
