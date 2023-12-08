/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt1316-sdw.h -- RT1316 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2021 Realtek Semiconductor Corp.
 */

#ifndef __RT1316_SDW_H__
#define __RT1316_SDW_H__

#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/soc.h>

/* RT1316 SDCA Control - function number */
#define FUNC_NUM_SMART_AMP 0x04

/* RT1316 SDCA entity */
#define RT1316_SDCA_ENT_PDE23 0x31
#define RT1316_SDCA_ENT_PDE27 0x32
#define RT1316_SDCA_ENT_PDE22 0x33
#define RT1316_SDCA_ENT_PDE24 0x34
#define RT1316_SDCA_ENT_XU24 0x24
#define RT1316_SDCA_ENT_FU21 0x03
#define RT1316_SDCA_ENT_UDMPU21 0x02

/* RT1316 SDCA control */
#define RT1316_SDCA_CTL_SAMPLE_FREQ_INDEX 0x10
#define RT1316_SDCA_CTL_REQ_POWER_STATE 0x01
#define RT1316_SDCA_CTL_BYPASS 0x01
#define RT1316_SDCA_CTL_FU_MUTE 0x01
#define RT1316_SDCA_CTL_FU_VOLUME 0x02
#define RT1316_SDCA_CTL_UDMPU_CLUSTER 0x10

/* RT1316 SDCA channel */
#define CH_L 0x01
#define CH_R 0x02

struct rt1316_sdw_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct sdw_slave *sdw_slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
};

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

#endif /* __RT1316_SDW_H__ */
