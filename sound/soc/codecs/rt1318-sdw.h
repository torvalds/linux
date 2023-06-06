/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt1318-sdw.h -- RT1318 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2022 Realtek Semiconductor Corp.
 */

#ifndef __RT1318_SDW_H__
#define __RT1318_SDW_H__

#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/soc.h>

/* imp-defined registers */
#define RT1318_SAPU_SM 0x3203

#define R1318_TCON	0xc203
#define R1318_TCON_RELATED_1	0xc206

#define R1318_SPK_TEMPERATRUE_PROTECTION_0	0xdb00
#define R1318_SPK_TEMPERATRUE_PROTECTION_L_4	0xdb08
#define R1318_SPK_TEMPERATRUE_PROTECTION_R_4	0xdd08

#define R1318_SPK_TEMPERATRUE_PROTECTION_L_6	0xdb12
#define R1318_SPK_TEMPERATRUE_PROTECTION_R_6	0xdd12

#define RT1318_INIT_RECIPROCAL_REG_L_24		0xdbb5
#define RT1318_INIT_RECIPROCAL_REG_L_23_16	0xdbb6
#define RT1318_INIT_RECIPROCAL_REG_L_15_8	0xdbb7
#define RT1318_INIT_RECIPROCAL_REG_L_7_0	0xdbb8
#define RT1318_INIT_RECIPROCAL_REG_R_24		0xddb5
#define RT1318_INIT_RECIPROCAL_REG_R_23_16	0xddb6
#define RT1318_INIT_RECIPROCAL_REG_R_15_8	0xddb7
#define RT1318_INIT_RECIPROCAL_REG_R_7_0	0xddb8

#define RT1318_INIT_R0_RECIPROCAL_SYN_L_24 0xdbc5
#define RT1318_INIT_R0_RECIPROCAL_SYN_L_23_16 0xdbc6
#define RT1318_INIT_R0_RECIPROCAL_SYN_L_15_8 0xdbc7
#define RT1318_INIT_R0_RECIPROCAL_SYN_L_7_0 0xdbc8
#define RT1318_INIT_R0_RECIPROCAL_SYN_R_24 0xddc5
#define RT1318_INIT_R0_RECIPROCAL_SYN_R_23_16 0xddc6
#define RT1318_INIT_R0_RECIPROCAL_SYN_R_15_8 0xddc7
#define RT1318_INIT_R0_RECIPROCAL_SYN_R_7_0 0xddc8

#define RT1318_R0_COMPARE_FLAG_L	0xdb35
#define RT1318_R0_COMPARE_FLAG_R	0xdd35

#define RT1318_STP_INITIAL_RS_TEMP_H 0xdd93
#define RT1318_STP_INITIAL_RS_TEMP_L 0xdd94

/* RT1318 SDCA Control - function number */
#define FUNC_NUM_SMART_AMP 0x04

/* RT1318 SDCA entity */
#define RT1318_SDCA_ENT_PDE23 0x31
#define RT1318_SDCA_ENT_XU24 0x24
#define RT1318_SDCA_ENT_FU21 0x03
#define RT1318_SDCA_ENT_UDMPU21 0x02
#define RT1318_SDCA_ENT_CS21 0x21
#define RT1318_SDCA_ENT_SAPU 0x29

/* RT1318 SDCA control */
#define RT1318_SDCA_CTL_SAMPLE_FREQ_INDEX 0x10
#define RT1318_SDCA_CTL_REQ_POWER_STATE 0x01
#define RT1318_SDCA_CTL_FU_MUTE 0x01
#define RT1318_SDCA_CTL_FU_VOLUME 0x02
#define RT1318_SDCA_CTL_UDMPU_CLUSTER 0x10
#define RT1318_SDCA_CTL_SAPU_PROTECTION_MODE 0x10
#define RT1318_SDCA_CTL_SAPU_PROTECTION_STATUS 0x11

/* RT1318 SDCA channel */
#define CH_L 0x01
#define CH_R 0x02

/* sample frequency index */
#define RT1318_SDCA_RATE_16000HZ		0x04
#define RT1318_SDCA_RATE_32000HZ		0x07
#define RT1318_SDCA_RATE_44100HZ		0x08
#define RT1318_SDCA_RATE_48000HZ		0x09
#define RT1318_SDCA_RATE_96000HZ		0x0b
#define RT1318_SDCA_RATE_192000HZ		0x0d


struct rt1318_sdw_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct sdw_slave *sdw_slave;
	enum sdw_slave_status status;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
};

#endif /* __RT1318_SDW_H__ */
