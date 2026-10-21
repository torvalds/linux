// SPDX-License-Identifier: GPL-2.0-only
//
// aw88399.h --  ALSA SoC AW88399 codec support
//
// Copyright (c) 2023 AWINIC Technology CO., LTD
//
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#ifndef __AW88399_H__
#define __AW88399_H__

#include <sound/aw88399.h>

#define AW88399_I2C_NAME			"aw88399"

#define AW88399_RATES (SNDRV_PCM_RATE_8000_48000 | \
			SNDRV_PCM_RATE_96000)
#define AW88399_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

#define FADE_TIME_MAX		100000
#define FADE_TIME_MIN		0

#define AW_CALI_READ_CNT_MAX			(8)
#define AW88399_DSP_REG_CALRE			(0x8141)
#define AW88399_DSP_REG_CALRE_SHIFT		(10)
#define AW_CALI_DATA_SUM_RM			(2)

#define AW88399_DSP_REG_CFG_MBMEC_ACTAMPTH	(0x9B4C)
#define AW88399_DSP_REG_CFG_MBMEC_NOISEAMPTH	(0x9B4E)
#define AW88399_DSP_REG_CFG_ADPZ_USTEPN	(0x9B6E)
#define AW88399_DSP_REG_CFG_RE_ALPHA		(0x9BD4)
#define AW_GET_IV_CNT_MAX			(6)

#define AW88399_DSP_VOL_MUTE			(0XFF00)

#define AW88399_DSP_LOW_POWER_SWITCH_CFG_ADDR	(0x9BEC)
#define AW88399_DSP_LOW_POWER_SWITCH_DISABLE	(0x110b)

#define AW88399_PROFILE_EXT(xname, profile_info, profile_get, profile_set) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.info = profile_info, \
	.get = profile_get, \
	.put = profile_set, \
}

#endif /* __AW88399_H__ */
