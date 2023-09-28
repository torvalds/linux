// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395.h --  ALSA SoC AW88395 codec support
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
//

#ifndef __AW88395_H__
#define __AW88395_H__

#define AW88395_CHIP_ID_REG			(0x00)
#define AW88395_START_RETRIES			(5)
#define AW88395_START_WORK_DELAY_MS		(0)

#define AW88395_DSP_16_DATA_MASK		(0x0000ffff)

#define AW88395_I2C_NAME			"aw88395"

#define AW88395_RATES (SNDRV_PCM_RATE_8000_48000 | \
			SNDRV_PCM_RATE_96000)
#define AW88395_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

#define FADE_TIME_MAX			100000
#define FADE_TIME_MIN			0

#define AW88395_PROFILE_EXT(xname, profile_info, profile_get, profile_set) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.info = profile_info, \
	.get = profile_get, \
	.put = profile_set, \
}

enum {
	AW88395_SYNC_START = 0,
	AW88395_ASYNC_START,
};

enum {
	AW88395_STREAM_CLOSE = 0,
	AW88395_STREAM_OPEN,
};

struct aw88395 {
	struct aw_device *aw_pa;
	struct mutex lock;
	struct gpio_desc *reset_gpio;
	struct delayed_work start_work;
	struct regmap *regmap;
	struct aw_container *aw_cfg;
};

#endif
