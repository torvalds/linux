/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt1320-sdw.h -- RT1320 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2024 Realtek Semiconductor Corp.
 */

#ifndef __RT1320_SDW_H__
#define __RT1320_SDW_H__

#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/soc.h>
#include "../../../drivers/soundwire/bus.h"

#define RT1320_DEV_ID 0x6981
#define RT1321_DEV_ID 0x7045

/* imp-defined registers */
#define RT1320_DEV_VERSION_ID_1 0xc404
#define RT1320_DEV_ID_1 0xc405
#define RT1320_DEV_ID_0 0xc406

#define RT1320_POWER_STATE 0xc560

#define RT1321_PATCH_MAIN_VER 0x1000cffe
#define RT1321_PATCH_BETA_VER 0x1000cfff

#define RT1320_KR0_STATUS_CNT 0x1000f008
#define RT1320_KR0_INT_READY 0x1000f021
#define RT1320_HIFI_VER_0 0x3fe2e000
#define RT1320_HIFI_VER_1 0x3fe2e001
#define RT1320_HIFI_VER_2 0x3fe2e002
#define RT1320_HIFI_VER_3 0x3fe2e003

/* RT1320 SDCA Control - function number */
#define FUNC_NUM_AMP 0x04
#define FUNC_NUM_MIC 0x02

/* RT1320 SDCA entity */
#define RT1320_SDCA_ENT0 0x00
#define RT1320_SDCA_ENT_PDE11 0x2a
#define RT1320_SDCA_ENT_PDE23 0x33
#define RT1320_SDCA_ENT_PDE27 0x27
#define RT1320_SDCA_ENT_FU14 0x32
#define RT1320_SDCA_ENT_FU21 0x03
#define RT1320_SDCA_ENT_FU113 0x30
#define RT1320_SDCA_ENT_CS14 0x13
#define RT1320_SDCA_ENT_CS21 0x21
#define RT1320_SDCA_ENT_CS113 0x12
#define RT1320_SDCA_ENT_SAPU 0x29
#define RT1320_SDCA_ENT_PPU21 0x04

/* RT1320 SDCA control */
#define RT1320_SDCA_CTL_SAMPLE_FREQ_INDEX 0x10
#define RT1320_SDCA_CTL_REQ_POWER_STATE 0x01
#define RT1320_SDCA_CTL_ACTUAL_POWER_STATE 0x10
#define RT1320_SDCA_CTL_FU_MUTE 0x01
#define RT1320_SDCA_CTL_FU_VOLUME 0x02
#define RT1320_SDCA_CTL_SAPU_PROTECTION_MODE 0x10
#define RT1320_SDCA_CTL_SAPU_PROTECTION_STATUS 0x11
#define RT1320_SDCA_CTL_POSTURE_NUMBER 0x10
#define RT1320_SDCA_CTL_FUNC_STATUS 0x10

/* RT1320 SDCA channel */
#define CH_01 0x01
#define CH_02 0x02

/* Function_Status */
#define FUNCTION_NEEDS_INITIALIZATION		BIT(5)

/* Sample Frequency Index */
#define RT1320_SDCA_RATE_16000HZ		0x04
#define RT1320_SDCA_RATE_32000HZ		0x07
#define RT1320_SDCA_RATE_44100HZ		0x08
#define RT1320_SDCA_RATE_48000HZ		0x09
#define RT1320_SDCA_RATE_96000HZ		0x0b
#define RT1320_SDCA_RATE_192000HZ		0x0d

enum {
	RT1320_AIF1,
	RT1320_AIF2,
};

/*
 * The version id will be useful to distinguish the capability between the different IC versions.
 * Currently, VA and VB have different DSP FW versions.
 */
enum rt1320_version_id {
	RT1320_VA,
	RT1320_VB,
	RT1320_VC,
};

#define RT1320_VER_B_ID 0x07392238
#define RT1320_VAB_MCU_PATCH "realtek/rt1320/rt1320-patch-code-vab.bin"
#define RT1320_VC_MCU_PATCH "realtek/rt1320/rt1320-patch-code-vc.bin"
#define RT1321_VA_MCU_PATCH "realtek/rt1320/rt1321-patch-code-va.bin"

#define RT1320_FW_PARAM_ADDR 0x3fc2ab80
#define RT1320_CMD_ID 0x3fc2ab81
#define RT1320_CMD_PARAM_ADDR 0x3fc2ab90
#define RT1320_DSPFW_STATUS_ADDR 0x3fc2bfc4

#define RT1321_FW_PARAM_ADDR 0x3fc2d300
#define RT1321_CMD_ID 0x3fc2d301
#define RT1321_CMD_PARAM_ADDR 0x3fc2d310
#define RT1321_DSPFW_STATUS_ADDR 0x3fc2dfc4

/* FW parameter id 6, 7 */
struct rt1320_datafixpoint {
	int silencedetect;
	int r0;
	int meanr0;
	int advancegain;
	int ts;
	int re;
	int t;
	int invrs;
};

struct rt1320_paramcmd {
	unsigned char moudleid;
	unsigned char commandtype;
	unsigned short reserved1;
	unsigned int commandlength;
	long long reserved2;
	unsigned int paramid;
	unsigned int paramlength;
};

enum rt1320_fw_cmdid {
	RT1320_FW_READY,
	RT1320_SET_PARAM,
	RT1320_GET_PARAM,
	RT1320_GET_POOLSIZE,
};

enum rt1320_power_state {
	RT1320_NORMAL_STATE = 0x18,
	RT1320_K_R0_STATE = 0x1b,
};

enum rt1320_rw_type {
	RT1320_BRA_WRITE = 0,
	RT1320_BRA_READ = 1,
	RT1320_PARAM_WRITE = 2,
	RT1320_PARAM_READ = 3,
};

struct rt1320_sdw_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct regmap *mbq_regmap;
	struct sdw_slave *sdw_slave;
	struct sdw_bus_params params;
	bool hw_init;
	bool first_hw_init;
	int version_id;
	unsigned int dev_id;
	bool fu_dapm_mute;
	bool fu_mixer_mute[4];
	unsigned long long r0_l_reg;
	unsigned long long r0_r_reg;
	unsigned int r0_l_calib;
	unsigned int r0_r_calib;
	unsigned int temp_l_calib;
	unsigned int temp_r_calib;
	const char *dspfw_name;
	bool cali_done;
	bool fw_load_done;
	bool rae_update_done;
	struct work_struct load_dspfw_work;
	struct sdw_bpt_msg bra_msg;
};

#endif /* __RT1320_SDW_H__ */
