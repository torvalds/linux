// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TAC5XX2 Audio Smart Amplifier
//
// Copyright (C) 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// Author: Niranjan H Y <niranjan.hy@ti.com>

#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/time.h>
#include <linux/unaligned.h>
#include <sound/pcm_params.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/sdca_asoc.h>
#include <sound/sdca_function.h>
#include <sound/sdca_regmap.h>
#include <sound/jack.h>

#include "tac5xx2.h"

#define TAC5XX2_PROBE_TIMEOUT_MS 3000
#define TAC5XX2_FW_CACHE_TIMEOUT_MS 300

#define TAC5XX2_DEVICE_RATES (SNDRV_PCM_RATE_44100 | \
			      SNDRV_PCM_RATE_48000 | \
			      SNDRV_PCM_RATE_96000 | \
			      SNDRV_PCM_RATE_88200)
#define TAC5XX2_DEVICE_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_S32_LE)
/* Define channel constants */
#define TAC_CHANNEL_LEFT	1
#define TAC_CHANNEL_RIGHT	2
#define TAC_JACK_MONO_CS	2

#define TAC_MUTE_REG(func, fu, ch) \
	SDW_SDCA_CTL(TAC_FUNCTION_ID_##func, TAC_SDCA_ENT_##fu, \
		     TAC_SDCA_CHANNEL_MUTE, TAC_CHANNEL_##ch)
#define TAC_USAGE_REG(func, ent) \
	SDW_SDCA_CTL(TAC_FUNCTION_ID_##func, TAC_SDCA_ENT_##ent, \
		     TAC_SDCA_CTL_USAGE, 0)
#define TAC_XU_BYPASS_REG(func, xu)                        \
	SDW_SDCA_CTL(TAC_FUNCTION_ID_##func, TAC_SDCA_ENT_##xu, \
			TAC_SDCA_CTL_XU_BYPASS, 0)

/* mute registers */
#define FU21_L_MUTE_REG  TAC_MUTE_REG(SA, FU21, LEFT)
#define FU21_R_MUTE_REG  TAC_MUTE_REG(SA, FU21, RIGHT)
#define FU23_L_MUTE_REG  TAC_MUTE_REG(SA, FU23, LEFT)
#define FU23_R_MUTE_REG  TAC_MUTE_REG(SA, FU23, RIGHT)
#define FU26_MUTE_REG    TAC_MUTE_REG(SA, FU26, LEFT)
#define FU11_L_MUTE_REG  TAC_MUTE_REG(SM, FU11, LEFT)
#define FU11_R_MUTE_REG  TAC_MUTE_REG(SM, FU11, RIGHT)
#define FU113_L_MUTE_REG TAC_MUTE_REG(SM, FU113, LEFT)
#define FU113_R_MUTE_REG TAC_MUTE_REG(SM, FU113, RIGHT)
#define FU41_L_MUTE_REG  TAC_MUTE_REG(UAJ, FU41, LEFT)
#define FU41_R_MUTE_REG  TAC_MUTE_REG(UAJ, FU41, RIGHT)
#define FU36_MUTE_REG    TAC_MUTE_REG(UAJ, FU36, RIGHT)

/* it/ot usage */
#define IT11_USAGE_REG  TAC_USAGE_REG(SM, IT11)
#define IT41_USAGE_REG  TAC_USAGE_REG(UAJ, IT41)
#define IT33_USAGE_REG  TAC_USAGE_REG(UAJ, IT33)
#define OT113_USAGE_REG TAC_USAGE_REG(SM, OT113)
#define OT45_USAGE_REG TAC_USAGE_REG(UAJ, OT45)
#define OT36_USAGE_REG TAC_USAGE_REG(UAJ, OT36)

/* xu bypass */
#define XU12_BYPASS_REG TAC_XU_BYPASS_REG(SM, XU12)
#define XU42_BYPASS_REG TAC_XU_BYPASS_REG(UAJ, XU42)

#define TAC_DSP_ALGO_STATUS		TAC_REG_SDW(0, 3, 12)
#define TAC_DSP_ALGO_STATUS_RUNNING	0x20
#define TAC_FW_HDR_SIZE		88
#define TAC_FW_FILE_HDR	 20
#define TAC_MAX_FW_CHUNKS 512

struct tac_fw_hdr {
	u32 size;
	u32 version_offset;
	u32 plt_id;
	u32 ppc3_ver;
	u64 timestamp;
	u8 ddc_name[64];
};

/* Firmware file/chunk structure */
struct tac_fw_file {
	u32 vendor_id;
	u32 file_id;
	u32 version;
	u32 length;
	u32 dest_addr;
	u8 *fw_data;
};

/* TLV for volume control */
static const DECLARE_TLV_DB_SCALE(tac5xx2_amp_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_SCALE(tac5xx2_dvc_tlv, -7200, 50, 0);

/* Q7.8 volume control parameters: range -72dB to +6dB, step 0.5dB */
#define TAC_DVC_STEP		128	/* 0.5 dB in Q7.8 format */
#define TAC_DVC_MIN		(-144)	/* -72 dB / 0.5 dB step */
#define TAC_DVC_MAX		12	/* +6 dB / 0.5 dB step */

/* TAC-specific stereo volume control macro using SDW_SDCA_CTL (single control for L/R) */
#define TAC_DOUBLE_Q78_TLV(name, func_id, ent_id) \
	SDCA_DOUBLE_Q78_TLV(name, \
			    SDW_SDCA_CTL(TAC_FUNCTION_ID_##func_id, TAC_SDCA_ENT_##ent_id, \
					 TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), \
			    SDW_SDCA_CTL(TAC_FUNCTION_ID_##func_id, TAC_SDCA_ENT_##ent_id, \
					 TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), \
			    TAC_DVC_MIN, TAC_DVC_MAX, TAC_DVC_STEP, tac5xx2_dvc_tlv)

struct tac5xx2_prv {
	struct snd_soc_component *component;
	struct sdw_slave *sdw_peripheral;
	struct sdca_function_data *sa_func_data;
	struct sdca_function_data *sm_func_data;
	struct sdca_function_data *uaj_func_data;
	struct sdca_function_data *hid_func_data;
	enum sdw_slave_status status;
	struct regmap *regmap;
	struct device *dev;
	bool hw_init;
	bool first_hw_init_done;
	u32 part_id;
	struct snd_soc_jack *hs_jack;
	int jack_type;
	 /* Custom fw binary. UMP File Download is not used. */
	unsigned int fw_file_cnt;
	struct tac_fw_file *fw_files;
	struct completion fw_caching_complete;
	bool fw_dl_success;
	u8 fw_binaryname[64];
};

static const struct reg_default tac_reg_default[] = {
	{TAC_SW_RESET, 0x0},
	{TAC_SLEEP_MODEZ, 0x0},
	{TAC_FEATURE_PDZ, 0x0},
	{TAC_TX_CH_EN, 0xf0},
	{TAC_REG_SDW(0, 0, 0x5), 0xcf},
	{TAC_REG_SDW(0, 0, 0x6), 0xa},
	{TAC_REG_SDW(0, 0, 0x7), 0x0},
	{TAC_REG_SDW(0, 0, 0x8), 0xfe},
	{TAC_REG_SDW(0, 0, 0x9), 0x9},
	{TAC_REG_SDW(0, 0, 0xa), 0x28},
	{TAC_REG_SDW(0, 0, 0xb), 0x1},
	{TAC_REG_SDW(0, 0, 0xc), 0x11},
	{TAC_REG_SDW(0, 0, 0xd), 0x11},
	{TAC_REG_SDW(0, 0, 0xe), 0x61},
	{TAC_REG_SDW(0, 0, 0xf), 0x0},
	{TAC_REG_SDW(0, 0, 0x10), 0x50},
	{TAC_REG_SDW(0, 0, 0x11), 0x70},
	{TAC_REG_SDW(0, 0, 0x12), 0x60},
	{TAC_REG_SDW(0, 0, 0x13), 0x28},
	{TAC_REG_SDW(0, 0, 0x14), 0x0},
	{TAC_REG_SDW(0, 0, 0x15), 0x18},
	{TAC_REG_SDW(0, 0, 0x16), 0x20},
	{TAC_REG_SDW(0, 0, 0x17), 0x0},
	{TAC_REG_SDW(0, 0, 0x18), 0x18},
	{TAC_REG_SDW(0, 0, 0x19), 0x54},
	{TAC_REG_SDW(0, 0, 0x1a), 0x8},
	{TAC_REG_SDW(0, 0, 0x1b), 0x0},
	{TAC_REG_SDW(0, 0, 0x1c), 0x30},
	{TAC_REG_SDW(0, 0, 0x1d), 0x0},
	{TAC_REG_SDW(0, 0, 0x1e), 0x0},
	{TAC_REG_SDW(0, 0, 0x1f), 0x0},
	{TAC_REG_SDW(0, 0, 0x20), 0x0},
	{TAC_REG_SDW(0, 0, 0x21), 0x20},
	{TAC_REG_SDW(0, 0, 0x22), 0x21},
	{TAC_REG_SDW(0, 0, 0x23), 0x22},
	{TAC_REG_SDW(0, 0, 0x24), 0x23},
	{TAC_REG_SDW(0, 0, 0x25), 0x4},
	{TAC_REG_SDW(0, 0, 0x26), 0x5},
	{TAC_REG_SDW(0, 0, 0x27), 0x6},
	{TAC_REG_SDW(0, 0, 0x28), 0x7},
	{TAC_REG_SDW(0, 0, 0x29), 0x0},
	{TAC_REG_SDW(0, 0, 0x2a), 0x0},
	{TAC_REG_SDW(0, 0, 0x2b), 0x0},
	{TAC_REG_SDW(0, 0, 0x2c), 0x20},
	{TAC_REG_SDW(0, 0, 0x2d), 0x21},
	{TAC_REG_SDW(0, 0, 0x2e), 0x2},
	{TAC_REG_SDW(0, 0, 0x2f), 0x3},
	{TAC_REG_SDW(0, 0, 0x30), 0x4},
	{TAC_REG_SDW(0, 0, 0x31), 0x5},
	{TAC_REG_SDW(0, 0, 0x32), 0x6},
	{TAC_REG_SDW(0, 0, 0x33), 0x7},
	{TAC_REG_SDW(0, 0, 0x34), 0x0},
	{TAC_REG_SDW(0, 0, 0x35), 0x90},
	{TAC_REG_SDW(0, 0, 0x36), 0x80},
	{TAC_REG_SDW(0, 0, 0x37), 0x0},
	{TAC_REG_SDW(0, 0, 0x39), 0x0},
	{TAC_REG_SDW(0, 0, 0x3a), 0x90},
	{TAC_REG_SDW(0, 0, 0x3b), 0x80},
	{TAC_REG_SDW(0, 0, 0x3c), 0x0},
	{TAC_REG_SDW(0, 0, 0x3e), 0x0},
	{TAC_REG_SDW(0, 0, 0x3f), 0x90},
	{TAC_REG_SDW(0, 0, 0x40), 0x80},
	{TAC_REG_SDW(0, 0, 0x41), 0x0},
	{TAC_REG_SDW(0, 0, 0x43), 0x90},
	{TAC_REG_SDW(0, 0, 0x44), 0x80},
	{TAC_REG_SDW(0, 0, 0x45), 0x0},
	{TAC_REG_SDW(0, 0, 0x47), 0x90},
	{TAC_REG_SDW(0, 0, 0x48), 0x80},
	{TAC_REG_SDW(0, 0, 0x49), 0x0},
	{TAC_REG_SDW(0, 0, 0x4b), 0x90},
	{TAC_REG_SDW(0, 0, 0x4c), 0x80},
	{TAC_REG_SDW(0, 0, 0x4d), 0x0},
	{TAC_REG_SDW(0, 0, 0x4f), 0x31},
	{TAC_REG_SDW(0, 0, 0x50), 0x0},
	{TAC_REG_SDW(0, 0, 0x51), 0x0},
	{TAC_REG_SDW(0, 0, 0x52), 0x90},
	{TAC_REG_SDW(0, 0, 0x53), 0x80},
	{TAC_REG_SDW(0, 0, 0x55), 0x90},
	{TAC_REG_SDW(0, 0, 0x56), 0x80},
	{TAC_REG_SDW(0, 0, 0x58), 0x90},
	{TAC_REG_SDW(0, 0, 0x59), 0x80},
	{TAC_REG_SDW(0, 0, 0x5b), 0x90},
	{TAC_REG_SDW(0, 0, 0x5c), 0x80},
	{TAC_REG_SDW(0, 0, 0x5e), 0x8},
	{TAC_REG_SDW(0, 0, 0x5f), 0x8},
	{TAC_REG_SDW(0, 0, 0x60), 0x0},
	{TAC_REG_SDW(0, 0, 0x61), 0x0},
	{TAC_REG_SDW(0, 0, 0x62), 0xff},
	{TAC_REG_SDW(0, 0, 0x63), 0xc0},
	{TAC_REG_SDW(0, 0, 0x64), 0x5},
	{TAC_REG_SDW(0, 0, 0x65), 0x3},
	{TAC_REG_SDW(0, 0, 0x66), 0x0},
	{TAC_REG_SDW(0, 0, 0x67), 0x0},
	{TAC_REG_SDW(0, 0, 0x68), 0x0},
	{TAC_REG_SDW(0, 0, 0x69), 0x8},
	{TAC_REG_SDW(0, 0, 0x6a), 0x0},
	{TAC_REG_SDW(0, 0, 0x6b), 0xa0},
	{TAC_REG_SDW(0, 0, 0x6c), 0x18},
	{TAC_REG_SDW(0, 0, 0x6d), 0x18},
	{TAC_REG_SDW(0, 0, 0x6e), 0x18},
	{TAC_REG_SDW(0, 0, 0x6f), 0x18},
	{TAC_REG_SDW(0, 0, 0x70), 0x88},
	{TAC_REG_SDW(0, 0, 0x71), 0xff},
	{TAC_REG_SDW(0, 0, 0x72), 0x0},
	{TAC_REG_SDW(0, 0, 0x73), 0x31},
	{TAC_REG_SDW(0, 0, 0x74), 0xc0},
	{TAC_REG_SDW(0, 0, 0x75), 0x0},
	{TAC_REG_SDW(0, 0, 0x76), 0x0},
	{TAC_REG_SDW(0, 0, 0x77), 0x0},
	{TAC_REG_SDW(0, 0, 0x78), 0x0},
	{TAC_REG_SDW(0, 0, 0x7b), 0x0},
	{TAC_REG_SDW(0, 0, 0x7c), 0xd0},
	{TAC_REG_SDW(0, 0, 0x7d), 0x0},
	{TAC_REG_SDW(0, 0, 0x7e), 0x0},
	{TAC_REG_SDW(0, 1, 0x1), 0x0},
	{TAC_REG_SDW(0, 1, 0x2), 0x0},
	{TAC_REG_SDW(0, 1, 0x3), 0x0},
	{TAC_REG_SDW(0, 1, 0x4), 0x4},
	{TAC_REG_SDW(0, 1, 0x5), 0x0},
	{TAC_REG_SDW(0, 1, 0x6), 0x0},
	{TAC_REG_SDW(0, 1, 0x7), 0x0},
	{TAC_REG_SDW(0, 1, 0x8), 0x0},
	{TAC_REG_SDW(0, 1, 0x9), 0x0},
	{TAC_REG_SDW(0, 1, 0xa), 0x0},
	{TAC_REG_SDW(0, 1, 0xb), 0x1},
	{TAC_REG_SDW(0, 1, 0xc), 0x0},
	{TAC_REG_SDW(0, 1, 0xd), 0x0},
	{TAC_REG_SDW(0, 1, 0xe), 0x0},
	{TAC_REG_SDW(0, 1, 0xf), 0x8},
	{TAC_REG_SDW(0, 1, 0x10), 0x0},
	{TAC_REG_SDW(0, 1, 0x11), 0x0},
	{TAC_REG_SDW(0, 1, 0x12), 0x1},
	{TAC_REG_SDW(0, 1, 0x13), 0x0},
	{TAC_REG_SDW(0, 1, 0x14), 0x0},
	{TAC_REG_SDW(0, 1, 0x15), 0x0},
	{TAC_REG_SDW(0, 1, 0x16), 0x0},
	{TAC_REG_SDW(0, 1, 0x17), 0x0},
	{TAC_REG_SDW(0, 1, 0x18), 0x0},
	{TAC_REG_SDW(0, 1, 0x19), 0x0},
	{TAC_REG_SDW(0, 1, 0x1a), 0x0},
	{TAC_REG_SDW(0, 1, 0x1b), 0x0},
	{TAC_REG_SDW(0, 1, 0x1c), 0x0},
	{TAC_REG_SDW(0, 1, 0x1d), 0x0},
	{TAC_REG_SDW(0, 1, 0x1e), 0x2},
	{TAC_REG_SDW(0, 1, 0x1f), 0x8},
	{TAC_REG_SDW(0, 1, 0x20), 0x9},
	{TAC_REG_SDW(0, 1, 0x21), 0xa},
	{TAC_REG_SDW(0, 1, 0x22), 0xb},
	{TAC_REG_SDW(0, 1, 0x23), 0xc},
	{TAC_REG_SDW(0, 1, 0x24), 0xd},
	{TAC_REG_SDW(0, 1, 0x25), 0xe},
	{TAC_REG_SDW(0, 1, 0x26), 0xf},
	{TAC_REG_SDW(0, 1, 0x27), 0x8},
	{TAC_REG_SDW(0, 1, 0x28), 0x9},
	{TAC_REG_SDW(0, 1, 0x29), 0xa},
	{TAC_REG_SDW(0, 1, 0x2a), 0xb},
	{TAC_REG_SDW(0, 1, 0x2b), 0xc},
	{TAC_REG_SDW(0, 1, 0x2c), 0xd},
	{TAC_REG_SDW(0, 1, 0x2d), 0xe},
	{TAC_REG_SDW(0, 1, 0x2e), 0xf},
	{TAC_REG_SDW(0, 1, 0x2f), 0x0},
	{TAC_REG_SDW(0, 1, 0x30), 0x0},
	{TAC_REG_SDW(0, 1, 0x31), 0x0},
	{TAC_REG_SDW(0, 1, 0x32), 0x0},
	{TAC_REG_SDW(0, 1, 0x33), 0x0},
	{TAC_REG_SDW(0, 1, 0x34), 0x0},
	{TAC_REG_SDW(0, 1, 0x35), 0x0},
	{TAC_REG_SDW(0, 1, 0x36), 0x0},
	{TAC_REG_SDW(0, 1, 0x37), 0x0},
	{TAC_REG_SDW(0, 1, 0x38), 0x98},
	{TAC_REG_SDW(0, 1, 0x39), 0x0},
	{TAC_REG_SDW(0, 1, 0x3a), 0x0},
	{TAC_REG_SDW(0, 1, 0x3b), 0x0},
	{TAC_REG_SDW(0, 1, 0x3c), 0x1},
	{TAC_REG_SDW(0, 1, 0x3d), 0x2},
	{TAC_REG_SDW(0, 1, 0x3e), 0x3},
	{TAC_REG_SDW(0, 1, 0x3f), 0x4},
	{TAC_REG_SDW(0, 1, 0x40), 0x5},
	{TAC_REG_SDW(0, 1, 0x41), 0x6},
	{TAC_REG_SDW(0, 1, 0x42), 0x7},
	{TAC_REG_SDW(0, 1, 0x43), 0x0},
	{TAC_REG_SDW(0, 1, 0x44), 0x0},
	{TAC_REG_SDW(0, 1, 0x45), 0x1},
	{TAC_REG_SDW(0, 1, 0x46), 0x2},
	{TAC_REG_SDW(0, 1, 0x47), 0x3},
	{TAC_REG_SDW(0, 1, 0x48), 0x4},
	{TAC_REG_SDW(0, 1, 0x49), 0x5},
	{TAC_REG_SDW(0, 1, 0x4a), 0x6},
	{TAC_REG_SDW(0, 1, 0x4b), 0x7},
	{TAC_REG_SDW(0, 1, 0x4c), 0x98},
	{TAC_REG_SDW(0, 1, 0x4d), 0x0},
	{TAC_REG_SDW(0, 1, 0x4e), 0x0},
	{TAC_REG_SDW(0, 1, 0x4f), 0x0},
	{TAC_REG_SDW(0, 1, 0x50), 0x1},
	{TAC_REG_SDW(0, 1, 0x51), 0x2},
	{TAC_REG_SDW(0, 1, 0x52), 0x3},
	{TAC_REG_SDW(0, 1, 0x53), 0x4},
	{TAC_REG_SDW(0, 1, 0x54), 0x5},
	{TAC_REG_SDW(0, 1, 0x55), 0x6},
	{TAC_REG_SDW(0, 1, 0x56), 0x7},
	{TAC_REG_SDW(0, 1, 0x57), 0x0},
	{TAC_REG_SDW(0, 1, 0x58), 0x0},
	{TAC_REG_SDW(0, 1, 0x59), 0x1},
	{TAC_REG_SDW(0, 1, 0x5a), 0x2},
	{TAC_REG_SDW(0, 1, 0x5b), 0x3},
	{TAC_REG_SDW(0, 1, 0x5c), 0x4},
	{TAC_REG_SDW(0, 1, 0x5d), 0x5},
	{TAC_REG_SDW(0, 1, 0x5e), 0x6},
	{TAC_REG_SDW(0, 1, 0x5f), 0x7},
	{TAC_REG_SDW(0, 1, 0x60), 0x98},
	{TAC_REG_SDW(0, 1, 0x61), 0x0},
	{TAC_REG_SDW(0, 1, 0x62), 0x0},
	{TAC_REG_SDW(0, 1, 0x63), 0x0},
	{TAC_REG_SDW(0, 1, 0x64), 0x1},
	{TAC_REG_SDW(0, 1, 0x65), 0x2},
	{TAC_REG_SDW(0, 1, 0x66), 0x3},
	{TAC_REG_SDW(0, 1, 0x67), 0x4},
	{TAC_REG_SDW(0, 1, 0x68), 0x5},
	{TAC_REG_SDW(0, 1, 0x69), 0x6},
	{TAC_REG_SDW(0, 1, 0x6a), 0x7},
	{TAC_REG_SDW(0, 1, 0x6b), 0x0},
	{TAC_REG_SDW(0, 1, 0x6c), 0x0},
	{TAC_REG_SDW(0, 1, 0x6d), 0x1},
	{TAC_REG_SDW(0, 1, 0x6e), 0x2},
	{TAC_REG_SDW(0, 1, 0x6f), 0x3},
	{TAC_REG_SDW(0, 1, 0x70), 0x4},
	{TAC_REG_SDW(0, 1, 0x71), 0x5},
	{TAC_REG_SDW(0, 1, 0x72), 0x6},
	{TAC_REG_SDW(0, 1, 0x73), 0x7},
};

static const struct reg_sequence tac_spk_seq[] = {
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU21,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU21,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU23,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU23,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), 0),
};

static const struct reg_sequence tac_sm_seq[] = {
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU113,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU113,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU11,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU11,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), 0),
};

static const struct reg_sequence tac_uaj_seq[] = {
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU41,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU41,
			      TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT), 0),
	REG_SEQ0(SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU36,
			      TAC_SDCA_CHANNEL_GAIN, TAC_JACK_MONO_CS), 0),
};

static bool tac_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAC_REG_SDW(0, 0, 1) ... TAC_REG_SDW(0, 0, 5):
	case TAC_REG_SDW(0, 2, 1) ... TAC_REG_SDW(0, 2, 6):
	case TAC_REG_SDW(0, 2, 24) ... TAC_REG_SDW(0, 2, 55):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_HID, TAC_SDCA_ENT_HID1,
			  TAC_SDCA_CTL_HIDTX_CURRENT_OWNER, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_HID, TAC_SDCA_ENT_HID1,
			  TAC_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_GE35,
			  TAC_SDCA_CTL_DET_MODE, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_PDE23,
			  TAC_SDCA_REQUESTED_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_PDE11,
			  TAC_SDCA_REQUESTED_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_PDE47,
			  TAC_SDCA_REQUESTED_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_PDE34,
			  TAC_SDCA_REQUESTED_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_PDE23,
			  TAC_SDCA_ACTUAL_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_PDE11,
			  TAC_SDCA_ACTUAL_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_PDE47,
			  TAC_SDCA_ACTUAL_PS, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_PDE34,
			  TAC_SDCA_ACTUAL_PS, 0):
	case SDW_SCP_SDCA_INT1:
	case SDW_SCP_SDCA_INT2:
	case SDW_SCP_SDCA_INT3:
	case SDW_SCP_SDCA_INT4:
	case SDW_SDCA_CTL(1, 0, 0x10, 0):
	case SDW_SDCA_CTL(2, 0, 0x10, 0):
	case SDW_SDCA_CTL(3, 0, 0x10, 0):
	case SDW_SDCA_CTL(4, 0, 0x1, 0):
	case 0x44007F80 ... 0x44007F87:
	case TAC_DSP_ALGO_STATUS:	/* DSP algo status - always read from HW */
		return true;
	default:
		break;
	}

	return false;
}

static int tac_sdca_mbq_size(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU21,
			  TAC_SDCA_CHANNEL_VOLUME, TAC_CHANNEL_LEFT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU21,
			  TAC_SDCA_CHANNEL_VOLUME, TAC_CHANNEL_RIGHT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU23,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU23,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SA, TAC_SDCA_ENT_FU23,
			  TAC_SDCA_MASTER_GAIN, 0):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU113,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU113,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU11,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_SM, TAC_SDCA_ENT_FU11,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU41,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_LEFT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU41,
			  TAC_SDCA_CHANNEL_GAIN, TAC_CHANNEL_RIGHT):
	case SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU36,
			  TAC_SDCA_CHANNEL_GAIN, TAC_JACK_MONO_CS):
		return 2;

	default:
		return 1;
	}
}

static const struct regmap_sdw_mbq_cfg tac_mbq_cfg = {
	.mbq_size = tac_sdca_mbq_size,
};

static const struct regmap_config tac_regmap = {
	.reg_bits = 32,
	.val_bits = 16, /* mbq support */
	.reg_defaults = tac_reg_default,
	.num_reg_defaults = ARRAY_SIZE(tac_reg_default),
	.max_register = 0x47FFFFFF,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = tac_volatile_reg,
	.use_single_read = true,
	.use_single_write = true,
};

/* Check if device has DSP algo that needs status monitoring */
static bool tac_has_dsp_algo(struct tac5xx2_prv *tac_dev)
{
	switch (tac_dev->part_id) {
	case 0x5682:
	case 0x2883:
		return true;
	default:
		return false;
	}
}

/* Check if device has UAJ (Universal Audio Jack) support */
static bool tac_has_uaj_support(struct tac5xx2_prv *tac_dev)
{
	return tac_dev->uaj_func_data;
}

/* Forward declaration for headset detection */
static int tac5xx2_sdca_headset_detect(struct tac5xx2_prv *tac_dev);

/* Volume controls for mic, hp and mic cap */
static const struct snd_kcontrol_new tac5xx2_snd_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("Amp Volume", TAC_AMP_LVL_CFG0, TAC_AMP_LVL_CFG1,
			       2, 0, 44, 1, tac5xx2_amp_tlv),
	TAC_DOUBLE_Q78_TLV("DMIC Capture Volume", SM, FU113),
	TAC_DOUBLE_Q78_TLV("Speaker Volume", SA, FU21),
};

static const struct snd_kcontrol_new tac_uaj_controls[] = {
	TAC_DOUBLE_Q78_TLV("UAJ Playback Volume", UAJ, FU41),
	SDCA_SINGLE_Q78_TLV("UAJ Capture Volume",
			    SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_FU36,
					 TAC_SDCA_CHANNEL_GAIN, TAC_JACK_MONO_CS),
			   TAC_DVC_MIN, TAC_DVC_MAX, TAC_DVC_STEP, tac5xx2_dvc_tlv),
};

static const struct snd_soc_dapm_widget tac5xx2_common_widgets[] = {
	/* Port 1: Speaker Playback Path */
	SND_SOC_DAPM_AIF_IN("AIF1 Playback", "DP1 Speaker Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA("FU21_L", FU21_L_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU21_R", FU21_R_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU23_L", FU23_L_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU23_R", FU23_R_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_OUTPUT("SPK_L"),
	SND_SOC_DAPM_OUTPUT("SPK_R"),

	/* Port 3: Smart Mic (DMIC) Capture Path */
	SND_SOC_DAPM_AIF_OUT("AIF3 Capture", "DP3 Mic Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("DMIC_L"),
	SND_SOC_DAPM_INPUT("DMIC_R"),
	SND_SOC_DAPM_PGA("IT11", IT11_USAGE_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CS113", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("FU11_L", FU11_L_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU11_R", FU11_R_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PPU11", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("XU12", XU12_BYPASS_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("FU113_L", FU113_L_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU113_R", FU113_R_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("OT113", OT113_USAGE_REG, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget tac_uaj_widgets[] = {
	/* Port 4: UAJ (Headphone) Playback Path */
	SND_SOC_DAPM_AIF_IN("AIF4 Playback", "DP4 UAJ Speaker Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA("IT41", IT41_USAGE_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("FU41_L", FU41_L_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("FU41_R", FU41_R_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("XU42", XU42_BYPASS_REG, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CS41", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_DAC("OT45", "DP4 UAJ Speaker Playback", OT45_USAGE_REG, 0, 0),
	SND_SOC_DAPM_OUTPUT("HP_L"),
	SND_SOC_DAPM_OUTPUT("HP_R"),

	/* Port 7: UAJ (Headset Mic) Capture Path */
	SND_SOC_DAPM_AIF_OUT("AIF7 Capture", "DP7 UAJ Mic Capture", 0,
			     SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("UAJ_MIC"),
	SND_SOC_DAPM_ADC("IT33", "DP7 UAJ Mic Capture", IT33_USAGE_REG, 0, 0),
	SND_SOC_DAPM_PGA("FU36", FU36_MUTE_REG, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CS36", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OT36", OT36_USAGE_REG, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route tac5xx2_common_routes[] = {
	/* Speaker Playback Path */
	{"FU21_L", NULL, "AIF1 Playback"},
	{"FU21_R", NULL, "AIF1 Playback"},

	{"FU23_L", NULL, "FU21_L"},
	{"FU23_R", NULL, "FU21_R"},

	{"SPK_L", NULL, "FU23_L"},
	{"SPK_R", NULL, "FU23_R"},

	/* Smart Mic DAPM Routes */
	{"IT11", NULL, "DMIC_L"},
	{"IT11", NULL, "DMIC_R"},
	{"FU11_L", NULL, "IT11"},
	{"FU11_R", NULL, "IT11"},
	{"PPU11", NULL, "FU11_L"},
	{"PPU11", NULL, "FU11_R"},
	{"XU12", NULL, "PPU11"},
	{"FU113_L", NULL, "XU12"},
	{"FU113_R", NULL, "XU12"},
	{"FU113_L", NULL, "CS113"},
	{"FU113_R", NULL, "CS113"},
	{"OT113", NULL, "FU113_L"},
	{"OT113", NULL, "FU113_R"},
	{"OT113", NULL, "CS113"},
	{"AIF3 Capture", NULL, "OT113"},
};

static const struct snd_soc_dapm_route tac_uaj_routes[] = {
	/* UAJ Playback routes */
	{"IT41", NULL, "AIF4 Playback"},
	{"IT41", NULL, "CS41"},
	{"FU41_L", NULL, "IT41"},
	{"FU41_R", NULL, "IT41"},
	{"XU42", NULL, "FU41_L"},
	{"XU42", NULL, "FU41_R"},
	{"OT45", NULL, "XU42"},
	{"OT45", NULL, "CS41"},
	{"HP_L", NULL, "OT45"},
	{"HP_R", NULL, "OT45"},

	/* UAJ Capture routes */
	{"IT33", NULL, "UAJ_MIC"},
	{"IT33", NULL, "CS36"},
	{"FU36", NULL, "IT33"},
	{"OT36", NULL, "FU36"},
	{"OT36", NULL, "CS36"},
	{"AIF7 Capture", NULL, "OT36"},
};

static s32 tac_set_sdw_stream(struct snd_soc_dai *dai,
			      void *sdw_stream, s32 direction)
{
	if (sdw_stream)
		snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void tac_sdw_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int tac_clear_latch(struct tac5xx2_prv *priv)
{
	/* CLR_REG is a self-clearing bit */
	return regmap_update_bits(priv->regmap, TAC_INT_CFG,
				  TAC_INT_CFG_CLR_REG, TAC_INT_CFG_CLR_REG);
}

static int tac_sdw_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tac5xx2_prv *tac_dev = snd_soc_component_get_drvdata(component);
	struct sdw_slave *sdw_peripheral = tac_dev->sdw_peripheral;
	struct sdw_stream_runtime *sdw_stream;
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	u8 sample_rate_idx = 0;
	int function_id;
	int pde_entity;
	int port_num;
	int ret;

	if (!tac_dev->hw_init) {
		dev_err(tac_dev->dev,
			"error: operation without hw initialization");
		return -EINVAL;
	}

	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!sdw_stream) {
		dev_err(tac_dev->dev, "failed to get dma data");
		return -EINVAL;
	}

	ret = tac_clear_latch(tac_dev);
	if (ret)
		dev_warn(tac_dev->dev, "clear latch failed, err=%d", ret);

	switch (dai->id) {
	case TAC5XX2_DMIC:
		function_id = TAC_FUNCTION_ID_SM;
		pde_entity = TAC_SDCA_ENT_PDE11;
		port_num = TAC_SDW_PORT_NUM_DMIC;
		break;
	case TAC5XX2_UAJ:
		function_id = TAC_FUNCTION_ID_UAJ;
		pde_entity = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				TAC_SDCA_ENT_PDE47 : TAC_SDCA_ENT_PDE34;
		port_num = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				TAC_SDW_PORT_NUM_UAJ_PLAYBACK :
				TAC_SDW_PORT_NUM_UAJ_CAPTURE;
		break;
	case TAC5XX2_SPK:
		function_id = TAC_FUNCTION_ID_SA;
		pde_entity = TAC_SDCA_ENT_PDE23;
		port_num = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				TAC_SDW_PORT_NUM_SPK_PLAYBACK :
				TAC_SDW_PORT_NUM_SPK_CAPTURE;
		break;
	default:
		dev_err(tac_dev->dev, "Invalid dai id: %d for power up\n", dai->id);
		return -EINVAL;
	}

	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);
	port_config.num = port_num;
	ret = sdw_stream_add_slave(sdw_peripheral, &stream_config,
				   &port_config, 1, sdw_stream);
	if (ret) {
		dev_err(dai->dev,
			"Unable to configure port %d: %d\n", port_num, ret);
		return ret;
	}

	switch (params_rate(params)) {
	case 48000:
		sample_rate_idx = 0x01;
		break;
	case 44100:
		sample_rate_idx = 0x02;
		break;
	case 96000:
		sample_rate_idx = 0x03;
		break;
	case 88200:
		sample_rate_idx = 0x04;
		break;
	default:
		dev_dbg(tac_dev->dev, "Unsupported sample rate: %d Hz",
			params_rate(params));
		return -EINVAL;
	}

	switch (function_id) {
	case TAC_FUNCTION_ID_SM:
		ret = regmap_write(tac_dev->regmap,
				   SDW_SDCA_CTL(function_id, TAC_SDCA_ENT_CS113,
						TAC_SDCA_CTL_CS_SAMP_RATE_IDX, 0),
			sample_rate_idx);
		if (ret) {
			dev_err(tac_dev->dev, "Failed to set CS113 sample rate: %d", ret);
			return ret;
		}

		break;
	case TAC_FUNCTION_ID_UAJ:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = regmap_write(tac_dev->regmap,
					   SDW_SDCA_CTL(function_id, TAC_SDCA_ENT_CS41,
							TAC_SDCA_CTL_CS_SAMP_RATE_IDX, 0),
					sample_rate_idx);
			if (ret) {
				dev_err(tac_dev->dev, "Failed to set CS41 sample rate: %d", ret);
				return ret;
			}
		} else {
			ret = regmap_write(tac_dev->regmap,
					   SDW_SDCA_CTL(function_id, TAC_SDCA_ENT_CS36,
							TAC_SDCA_CTL_CS_SAMP_RATE_IDX, 0),
					sample_rate_idx);
			if (ret) {
				dev_err(tac_dev->dev, "Failed to set CS36 sample rate: %d", ret);
				return ret;
			}
		}
		break;
	case TAC_FUNCTION_ID_SA:
		/* SmartAmp: no additional sample rate configuration needed */
		break;
	}

	ret = regmap_write(tac_dev->regmap, SDW_SDCA_CTL(function_id, pde_entity,
							 TAC_SDCA_REQUESTED_PS, 0), 0);
	if (ret) {
		dev_err(tac_dev->dev,
			"failed to set func %d, entity %d's requested PS to 0: %d\n",
			function_id, pde_entity, ret);
		return ret;
	}

	ret = sdca_asoc_pde_poll_actual_ps(tac_dev->dev, tac_dev->regmap, function_id, pde_entity,
					   SDCA_PDE_PS3, SDCA_PDE_PS0, NULL, 0);
	if (ret)
		dev_err(tac_dev->dev, "failed to transition func %d, pde %d from PS3 -> PS0, err=%d\n",
			function_id, pde_entity, ret);
	return ret;
}

static int tac_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct tac5xx2_prv *tac_dev = snd_soc_component_get_drvdata(dai->component);
	int pde_entity, function_id;
	int ret;

	sdw_stream_remove_slave(tac_dev->sdw_peripheral, sdw_stream);

	switch (dai->id) {
	case TAC5XX2_DMIC:
		pde_entity = TAC_SDCA_ENT_PDE11;
		function_id = TAC_FUNCTION_ID_SM;
		break;
	case TAC5XX2_UAJ:
		function_id = TAC_FUNCTION_ID_UAJ;
		pde_entity = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				TAC_SDCA_ENT_PDE47 : TAC_SDCA_ENT_PDE34;
		break;
	case TAC5XX2_SPK:
		function_id = TAC_FUNCTION_ID_SA;
		pde_entity = TAC_SDCA_ENT_PDE23;
		break;
	default:
		dev_err(tac_dev->dev, "unhandled dai %d for power down\n", dai->id);
		return -EINVAL;
	}

	ret = regmap_write(tac_dev->regmap, SDW_SDCA_CTL(function_id, pde_entity,
							 TAC_SDCA_REQUESTED_PS, 0),
			   SDCA_PDE_PS3);
	if (ret) {
		dev_err(tac_dev->dev,
			"failed to set func %d, entity %d's requested PS to 3: %d\n",
			function_id, pde_entity, ret);
		return ret;
	}

	ret = sdca_asoc_pde_poll_actual_ps(tac_dev->dev, tac_dev->regmap, function_id,
					   pde_entity, SDCA_PDE_PS0, SDCA_PDE_PS3,
					   NULL, 0);
	if (ret)
		dev_err(tac_dev->dev,
			"failed to transition func %d, pde %d from PS0 -> PS3, err=%d\n",
			function_id, pde_entity, ret);

	return ret;
}

static const struct snd_soc_dai_ops tac_dai_ops = {
	.hw_params = tac_sdw_hw_params,
	.hw_free = tac_sdw_pcm_hw_free,
	.set_stream = tac_set_sdw_stream,
	.shutdown = tac_sdw_shutdown,
};

static int tac5xx2_sdca_btn_type(unsigned char *buffer, struct tac5xx2_prv *tac_dev)
{
	switch (*buffer) {
	case 1: /* play pause */
		return SND_JACK_BTN_0;
	case 10: /* vol down */
		return SND_JACK_BTN_3;
	case 8: /* vol up */
		return SND_JACK_BTN_2;
	case 4: /* long press */
		return SND_JACK_BTN_1;
	case 2: /* next song */
	case 32: /* next song */
		return SND_JACK_BTN_4;
	default:
		return 0;
	}
}

static int tac5xx2_sdca_button_detect(struct tac5xx2_prv *tac_dev)
{
	unsigned int btn_type, offset, idx;
	int ret, value, owner;
	u8 buf[2];

	ret = regmap_read(tac_dev->regmap,
			  SDW_SDCA_CTL(TAC_FUNCTION_ID_HID, TAC_SDCA_ENT_HID1,
				       TAC_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), &owner);
	if (ret) {
		dev_err(tac_dev->dev,
			"Failed to read current UMP message owner 0x%x", ret);
		return ret;
	}

	if (owner == SDCA_UMP_OWNER_DEVICE) {
		dev_dbg(tac_dev->dev, "skip button detect as current owner is not host\n");
		return 0;
	}

	ret = regmap_read(tac_dev->regmap,
			  SDW_SDCA_CTL(TAC_FUNCTION_ID_HID, TAC_SDCA_ENT_HID1,
				       TAC_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0), &offset);
	if (ret) {
		dev_err(tac_dev->dev,
			"Failed to read current UMP message offset: %d", ret);
		goto end_btn_det;
	}

	dev_dbg(tac_dev->dev, "button detect: message offset = %x", offset);

	for (idx = 0; idx < sizeof(buf); idx++) {
		ret = regmap_read(tac_dev->regmap,
				  TAC_BUF_ADDR_HID1 + offset + idx, &value);
		if (ret) {
			dev_err(tac_dev->dev,
				"Failed to read HID buffer: %d", ret);
			goto end_btn_det;
		}
		buf[idx] = value & 0xff;
	}

	if (buf[0] == 0x1) {
		btn_type = tac5xx2_sdca_btn_type(&buf[1], tac_dev);
		ret = btn_type;
	}

end_btn_det:
	regmap_write(tac_dev->regmap,
		     SDW_SDCA_CTL(TAC_FUNCTION_ID_HID, TAC_SDCA_ENT_HID1,
				  TAC_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), 0x01);

	return ret;
}

static int tac5xx2_sdca_headset_detect(struct tac5xx2_prv *tac_dev)
{
	int val, ret;

	ret = regmap_read(tac_dev->regmap,
			  SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_GE35,
				       TAC_SDCA_CTL_DET_MODE, 0), &val);
	if (ret) {
		dev_err(tac_dev->dev, "Failed to read the detect mode");
		return ret;
	}

	switch (val) {
	case 4:
		tac_dev->jack_type = SND_JACK_MICROPHONE;
		break;
	case 5:
		tac_dev->jack_type = SND_JACK_HEADPHONE;
		break;
	case 6:
		tac_dev->jack_type = SND_JACK_HEADSET;
		break;
	case 0:
	default:
		tac_dev->jack_type = 0;
		break;
	}

	ret = regmap_write(tac_dev->regmap,
			   SDW_SDCA_CTL(TAC_FUNCTION_ID_UAJ, TAC_SDCA_ENT_GE35,
					TAC_SDCA_CTL_SEL_MODE, 0), val);
	if (ret)
		dev_err(tac_dev->dev, "Failed to update the jack type to device");

	return 0;
}

static int tac5xx2_jack_init(struct tac5xx2_prv *tac_dev)
{
	int ret = 0;

	if (!tac_dev->hs_jack)
		goto disable_interrupts;

	ret = regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INTMASK2,
			   SDW_SCP_SDCA_INTMASK_SDCA_11);
	if (ret) {
		dev_err(tac_dev->dev,
			"Failed to register jack detection interrupt: %d\n", ret);
		goto disable_interrupts;
	}

	ret = regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INTMASK3,
			   SDW_SCP_SDCA_INTMASK_SDCA_16);
	if (ret) {
		dev_err(tac_dev->dev,
			"Failed to register for button detect interrupt: %d\n", ret);
		goto disable_interrupts;
	}

	return 0;

disable_interrupts:
	/* ignore errors while disabling interrupts */
	regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INTMASK2, 0);
	regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INTMASK3, 0);

	return ret;
}

static int tac5xx2_set_jack(struct snd_soc_component *component,
			    struct snd_soc_jack *hs_jack, void *data)
{
	struct tac5xx2_prv *tac_dev = snd_soc_component_get_drvdata(component);
	int ret;

	tac_dev->hs_jack = hs_jack;

	/* resume can happen only after first hw_init */
	if (!tac_dev->first_hw_init_done)
		return 0;

	ret = pm_runtime_resume_and_get(component->dev);
	if (ret < 0) {
		if (ret != -EACCES) {
			dev_err(component->dev,
				"%s: failed to resume %d\n", __func__, ret);
			return ret;
		}

		/* pm_runtime not enabled yet */
		dev_dbg(component->dev,
			"%s: skipping jack init for now\n", __func__);
		return 0;
	}

	ret = tac5xx2_jack_init(tac_dev);
	if (ret)
		dev_err(tac_dev->dev, "jack init failed, err=%d\n", ret);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return ret;
}

static int tac_interrupt_callback(struct sdw_slave *slave,
				  struct sdw_slave_intr_status *status)
{
	unsigned int sdca_int2, sdca_int3, jack_report_mask = 0;
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(&slave->dev);
	struct device *dev = &slave->dev;
	int btn_type = 0;
	int ret = 0;

	if (status->control_port) {
		if (status->control_port & SDW_SCP_INT1_PARITY)
			dev_warn(dev, "SCP: Parity error interrupt");
		if (status->control_port & SDW_SCP_INT1_BUS_CLASH)
			dev_warn(dev, "SCP: Bus clash interrupt");
	}

	if (!tac_has_uaj_support(tac_dev))
		return 0;

	ret = regmap_read(tac_dev->regmap, SDW_SCP_SDCA_INT2, &sdca_int2);
	if (ret) {
		dev_err(dev, "Failed to read UAJ Interrupt, reg:%#x err=%d\n",
			SDW_SCP_SDCA_INT2, ret);
		return ret;
	}

	ret = regmap_read(tac_dev->regmap, SDW_SCP_SDCA_INT3, &sdca_int3);
	if (ret) {
		dev_err(dev, "Failed to read HID interrupt reg=%#x: err=%d",
			SDW_SCP_SDCA_INT3, ret);
		return ret;
	}

	dev_dbg(dev, "SDCA_INT2: 0x%02x, SDCA_INT3: 0x%02x\n",
		sdca_int2, sdca_int3);

	if (sdca_int2 & SDW_SCP_SDCA_INT_SDCA_11) {
		ret = tac5xx2_sdca_headset_detect(tac_dev);
		if (ret < 0)
			goto clear;
		jack_report_mask |= SND_JACK_HEADSET;
	}

	if (sdca_int3 & SDW_SCP_SDCA_INT_SDCA_16) {
		btn_type = tac5xx2_sdca_button_detect(tac_dev);
		if (btn_type < 0)
			btn_type = 0;
		jack_report_mask |= SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_BTN_4;
	}

	if (tac_dev->jack_type == 0)
		btn_type = 0;

	dev_dbg(tac_dev->dev, "in %s, jack_type=%d\n", __func__, tac_dev->jack_type);
	dev_dbg(tac_dev->dev, "in %s, btn_type=0x%x\n", __func__, btn_type);

	if (!tac_dev->hs_jack)
		goto clear;

	snd_soc_jack_report(tac_dev->hs_jack, tac_dev->jack_type | btn_type,
			    jack_report_mask);

clear:
	if (sdca_int2) {
		ret = regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INT2, sdca_int2);
		if (ret)
			dev_dbg(tac_dev->dev, "Failed to clear jack interrupt\n");
	}

	if (sdca_int3) {
		ret = regmap_write(tac_dev->regmap, SDW_SCP_SDCA_INT3, sdca_int3);
		if (ret)
			dev_dbg(tac_dev->dev, "failed to clear hid interrupt\n");
	}

	return 0;
}

static struct snd_soc_dai_driver tac5572_dai_driver[] = {
	{
		.name = "tac5xx2-aif1",
		.id = TAC5XX2_SPK,
		.playback = {
			.stream_name = "DP1 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
	{
		.name = "tac5xx2-aif2",
		.id = TAC5XX2_DMIC,
		.capture = {
			.stream_name = "DP3 Mic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
	{
		.name = "tac5xx2-aif3",
		.id = TAC5XX2_UAJ,
		.playback = {
			.stream_name = "DP4 UAJ Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "DP7 UAJ Mic Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
};

static struct snd_soc_dai_driver tac5672_dai_driver[] = {
	{
		.name = "tac5xx2-aif1",
		.id = TAC5XX2_SPK,
		.playback = {
			.stream_name = "DP1 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "DP8 IV Sense Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "tac5xx2-aif2",
		.id = TAC5XX2_DMIC,
		.capture = {
			.stream_name = "DP3 Mic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
	{
		.name = "tac5xx2-aif3",
		.id = TAC5XX2_UAJ,
		.playback = {
			.stream_name = "DP4 UAJ Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "DP7 UAJ Mic Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
};

static struct snd_soc_dai_driver tac5682_dai_driver[] = {
	{
		.name = "tac5xx2-aif1",
		.id = TAC5XX2_SPK,
		.playback = {
			.stream_name = "DP1 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Echo Reference Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "tac5xx2-aif2",
		.id = TAC5XX2_DMIC,
		.capture = {
			.stream_name = "DP3 Mic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
	{
		.name = "tac5xx2-aif3",
		.id = TAC5XX2_UAJ,
		.playback = {
			.stream_name = "DP4 UAJ Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "DP7 UAJ Mic Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
};

static struct snd_soc_dai_driver tas2883_dai_driver[] = {
	{
		.name = "tac5xx2-aif1",
		.id = TAC5XX2_SPK,
		.playback = {
			.stream_name = "DP1 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "tac5xx2-aif2",
		.id = TAC5XX2_DMIC,
		.capture = {
			.stream_name = "DP3 Mic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TAC5XX2_DEVICE_RATES,
			.formats = TAC5XX2_DEVICE_FORMATS,
		},
		.ops = &tac_dai_ops,
	},
};

static s32 tac_component_probe(struct snd_soc_component *component)
{
	struct tac5xx2_prv *tac_dev = snd_soc_component_get_drvdata(component);
	int ret;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	if (!tac_has_uaj_support(tac_dev))
		goto done_comp_probe;

	ret = snd_soc_dapm_new_controls(snd_soc_component_to_dapm(component),
					tac_uaj_widgets,
					ARRAY_SIZE(tac_uaj_widgets));
	if (ret) {
		dev_err(component->dev, "Failed to add UAJ widgets: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(snd_soc_component_to_dapm(component),
				      tac_uaj_routes, ARRAY_SIZE(tac_uaj_routes));
	if (ret) {
		dev_err(component->dev, "Failed to add UAJ routes: %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_component_controls(component, tac_uaj_controls,
					     ARRAY_SIZE(tac_uaj_controls));
	if (ret) {
		dev_err(component->dev, "Failed to add UAJ controls: %d\n", ret);
			return ret;
	}

done_comp_probe:
	tac_dev->component = component;
	return 0;
}

static void tac_component_remove(struct snd_soc_component *codec)
{
	struct tac5xx2_prv *tac_dev = snd_soc_component_get_drvdata(codec);

	tac_dev->component = NULL;
}

static const struct snd_soc_component_driver soc_codec_driver_tacdevice = {
	.probe = tac_component_probe,
	.remove = tac_component_remove,
	.controls = tac5xx2_snd_controls,
	.num_controls = ARRAY_SIZE(tac5xx2_snd_controls),
	.dapm_widgets = tac5xx2_common_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tac5xx2_common_widgets),
	.dapm_routes = tac5xx2_common_routes,
	.num_dapm_routes = ARRAY_SIZE(tac5xx2_common_routes),
	.idle_bias_on = 0,
	.endianness = 1,
};

static s32 tac_init(struct tac5xx2_prv *tac_dev)
{
	struct snd_soc_component_driver *component_driver;
	struct snd_soc_dai_driver *dai_drv;
	int num_dais;
	s32 ret;

	dev_set_drvdata(tac_dev->dev, tac_dev);

	switch (tac_dev->part_id) {
	case 0x5572:
		dai_drv = tac5572_dai_driver;
		num_dais = ARRAY_SIZE(tac5572_dai_driver);
		break;
	case 0x5672:
		dai_drv = tac5672_dai_driver;
		num_dais = ARRAY_SIZE(tac5672_dai_driver);
		break;
	case 0x5682:
		dai_drv = tac5682_dai_driver;
		num_dais = ARRAY_SIZE(tac5682_dai_driver);
		break;
	case 0x2883:
		dai_drv = tas2883_dai_driver;
		num_dais = ARRAY_SIZE(tas2883_dai_driver);
		break;
	default:
		dev_err(tac_dev->dev, "Unsupported device: 0x%x\n",
			tac_dev->part_id);
		return -EINVAL;
	}

	component_driver = devm_kzalloc(tac_dev->dev, sizeof(*component_driver),
					GFP_KERNEL);
	if (!component_driver)
		return -ENOMEM;

	memcpy(component_driver, &soc_codec_driver_tacdevice, sizeof(*component_driver));
	if (tac_has_uaj_support(tac_dev))
		component_driver->set_jack = tac5xx2_set_jack;

	ret = devm_snd_soc_register_component(tac_dev->dev, component_driver,
					      dai_drv, num_dais);
	if (ret) {
		dev_err(tac_dev->dev, "%s: codec register error:%d.\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static s32 tac5xx2_sdca_dev_suspend(struct device *dev)
{
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(dev);

	if (!tac_dev->hw_init)
		return 0;

	regcache_cache_only(tac_dev->regmap, true);
	return 0;
}

static s32 tac5xx2_sdca_dev_system_suspend(struct device *dev)
{
	return tac5xx2_sdca_dev_suspend(dev);
}

static s32 tac5xx2_sdca_dev_resume(struct device *dev)
{
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret;

	if (!tac_dev->first_hw_init_done) {
		dev_dbg(dev, "Device not initialized yet, skipping resume sync\n");
		return 0;
	}

	ret = sdw_slave_wait_for_init(slave, TAC5XX2_PROBE_TIMEOUT_MS);
	if (ret) {
		sdw_show_ping_status(slave->bus, true);
		return ret;
	}

	regcache_cache_only(tac_dev->regmap, false);
	regcache_mark_dirty(tac_dev->regmap);
	ret = regcache_sync(tac_dev->regmap);
	if (ret < 0)
		dev_warn(dev, "Failed to sync regcache: %d\n", ret);

	/* Detect and set jack type for UAJ path before playback.
	 * This is required as jack detection does not trigger interrupt
	 * when device is in runtime_pm suspend with bus in clock stop mode.
	 */
	if (tac_has_uaj_support(tac_dev))
		tac5xx2_sdca_headset_detect(tac_dev);

	return 0;
}

static const struct dev_pm_ops tac5xx2_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(tac5xx2_sdca_dev_system_suspend, tac5xx2_sdca_dev_resume)
	RUNTIME_PM_OPS(tac5xx2_sdca_dev_suspend, tac5xx2_sdca_dev_resume, NULL)
};

static s32 tac_fw_read_hdr(const u8 *data, struct tac_fw_hdr *hdr)
{
	hdr->size = get_unaligned_le32(data);
	hdr->version_offset = get_unaligned_le32(data + 4);
	hdr->plt_id = get_unaligned_le32(data + 8);
	hdr->ppc3_ver = get_unaligned_le32(data + 12);
	memcpy(hdr->ddc_name, data + 16, 64);
	hdr->ddc_name[63] = 0;
	hdr->timestamp = get_unaligned_le64(data + 80);

	return TAC_FW_HDR_SIZE;
}

static s32 tac_fw_get_next_file(const u8 *data, size_t data_size, struct tac_fw_file *file)
{
	u32 file_length;

	/* Validate file header size */
	if (data_size < TAC_FW_FILE_HDR)
		return -EINVAL;

	file->vendor_id = get_unaligned_le32(&data[0]);
	file->file_id = get_unaligned_le32(&data[4]);
	file->version = get_unaligned_le32(&data[8]);
	file->length = get_unaligned_le32(&data[12]);
	file->dest_addr = get_unaligned_le32(&data[16]);
	file_length = file->length;

	/* Validate file payload exists */
	if (data_size < TAC_FW_FILE_HDR + file_length)
		return -EINVAL;

	file->fw_data = (u8 *)&data[20];

	return file_length + sizeof(u32) * 5;
}

static void tac5xx2_fw_ready(const struct firmware *fmw, void *context)
{
	struct tac5xx2_prv *tac_dev = context;
	struct tac_fw_file *files;
	u32 fw_hdr_size;
	u32 num_files = 0;
	struct tac_fw_hdr hdr;
	struct tm tm_time;
	size_t img_sz;
	u32 offset;
	s32 ret = 0;
	u8 *buf;

	if (!fmw || !fmw->data || fmw->size == 0 || fmw->size < TAC_FW_HDR_SIZE + TAC_FW_FILE_HDR) {
		dev_err(tac_dev->dev, "fw file: %s is empty or invalid\n",
			tac_dev->fw_binaryname);
		goto out;
	}

	/* Verify firmware size from header */
	fw_hdr_size = get_unaligned_le32(fmw->data);
	if (fw_hdr_size != fmw->size) {
		dev_err(tac_dev->dev, "firmware size mismatch: hdr=%u, actual=%zu\n",
			fw_hdr_size, fmw->size);
		goto out;
	}

	files = devm_kzalloc(tac_dev->dev, sizeof(*files) * TAC_MAX_FW_CHUNKS, GFP_KERNEL);
	buf = devm_kmemdup(tac_dev->dev, fmw->data, fmw->size, GFP_KERNEL);
	if (!files || !buf)
		goto out;

	/* validate the cache the firmware */
	img_sz = fmw->size;
	offset = tac_fw_read_hdr(buf, &hdr);
	while (offset < img_sz && num_files < TAC_MAX_FW_CHUNKS) {
		u32 file_length;

		if (offset + TAC_FW_FILE_HDR > img_sz) {
			dev_warn(tac_dev->dev, "Incomplete block header at offset %d\n",
				 offset);
			goto out;
		}
		/* Validate that the file payload doesn't exceed buffer */
		file_length = get_unaligned_le32(&buf[offset + 12]);
		/* Check for integer overflow and buffer bounds */
		if (file_length > img_sz || offset > img_sz - TAC_FW_FILE_HDR ||
		    file_length > img_sz - offset - TAC_FW_FILE_HDR) {
			dev_warn(tac_dev->dev, "File at offset %d exceeds buffer: length=%u, available=%zu\n",
				 offset, file_length, img_sz - offset - TAC_FW_FILE_HDR);
			goto out;
		}
		ret = tac_fw_get_next_file(&buf[offset], img_sz - offset, &files[num_files]);
		if (ret < 0) {
			dev_err(tac_dev->dev, "Failed to parse file at offset %d\n", offset);
			goto out;
		}
		offset += ret;
		num_files++;
	}

	if (num_files == 0) {
		dev_err(tac_dev->dev, "firmware with no files\n");
		goto out;
	}

	/* cache ready to use validated firmware */
	tac_dev->fw_file_cnt = num_files;
	tac_dev->fw_files = files;

	time64_to_tm(hdr.timestamp, 0, &tm_time);
	dev_dbg(tac_dev->dev, "fw file: %s, num_files=%u, ts:%04ld-%02d-%02d %02d:%02d\n",
		tac_dev->fw_binaryname, tac_dev->fw_file_cnt,
		tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
		tm_time.tm_hour, tm_time.tm_min);
	dev_dbg(tac_dev->dev, "fw file: DDC Name: %s\n", hdr.ddc_name);
	dev_dbg(tac_dev->dev, "fw file: PPC3 Version: 3.%ld.%ld.%ld\n",
		FIELD_GET(GENMASK(31, 24), hdr.ppc3_ver),
		FIELD_GET(GENMASK(23, 16), hdr.ppc3_ver),
		FIELD_GET(GENMASK(15, 8), hdr.ppc3_ver) & 0x3f);

out:
	complete_all(&tac_dev->fw_caching_complete);
	if (fmw)
		release_firmware(fmw);
}

static int tac_load_and_cache_firmware_async(struct tac5xx2_prv *tac_dev)
{
	tac_dev->fw_file_cnt = 0;
	tac_dev->fw_files = NULL; /* ready to download files */

	return request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				       tac_dev->fw_binaryname, tac_dev->dev,
				       GFP_KERNEL, tac_dev, tac5xx2_fw_ready);
}

static int tac_download(struct tac5xx2_prv *tac_dev)
{
	struct tac_fw_file *files = tac_dev->fw_files;
	u32 num_files = tac_dev->fw_file_cnt;
	u32 i;
	int ret = 0;

	for (i = 0; i < num_files; i++) {
		ret = sdw_nwrite_no_pm(tac_dev->sdw_peripheral, files[i].dest_addr,
				       files[i].length, files[i].fw_data);
		if (ret < 0) {
			dev_dbg(tac_dev->dev,
				"FW write failed at addr 0x%x: %d\n",
				files[i].dest_addr, ret);
			return ret;
		}
	}

	return 0;
}

/*
 * tac5xx2 uses custom firmware binary fw.
 * This is not using UMP File Download.
 */
static s32 tac_download_fw_to_hw(struct tac5xx2_prv *tac_dev)
{
	int ret;

	ret = tac_download(tac_dev);
	if (ret < 0) {
		dev_err(tac_dev->dev, "Firmware download failed: %d\n", ret);
		return ret;
	}

	dev_dbg(tac_dev->dev, "Firmware download complete: %d chunks\n",
		tac_dev->fw_file_cnt);
	tac_dev->fw_dl_success = true;

	return 0;
}

static struct pci_dev *tac_get_pci_dev(struct sdw_slave *peripheral)
{
	struct device *dev = &peripheral->dev;

	for (; dev; dev = dev->parent) {
		if (dev_is_pci(dev))
			return to_pci_dev(dev);
	}

	return NULL;
}

static void tac_generate_fw_name(struct sdw_slave *slave, char *name, size_t size)
{
	struct sdw_bus *bus = slave->bus;
	u16 part_id = slave->id.part_id;
	u8 unique_id = slave->id.unique_id;
	struct pci_dev *pci = tac_get_pci_dev(slave);

	if (pci)
		scnprintf(name, size, "%04X-%04X-%1X-%1X.bin", part_id,
			  pci->subsystem_device, bus->link_id, unique_id);
	else
		/* Default firmware name based on part ID */
		scnprintf(name, size, "%s%04x-%1X-%1X.bin",
			  part_id == 0x2883 ? "tas" : "tac",
			  part_id, bus->link_id, unique_id);
}

static int tac_io_init(struct device *dev, struct sdw_slave *slave, bool first)
{
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(dev);
	u64 time;
	int ret;

	if (tac_dev->hw_init) {
		dev_dbg(dev, "early return hw_init already done..");
		return 0;
	}

	time = wait_for_completion_timeout(&tac_dev->fw_caching_complete,
					   msecs_to_jiffies(TAC5XX2_FW_CACHE_TIMEOUT_MS));
	if (!time) {
		ret = -ETIMEDOUT;
		dev_warn(tac_dev->dev, "%s: fw caching timeout\n", __func__);
		goto io_init_err;
	}

	if (tac_dev->fw_files && tac_dev->fw_file_cnt > 0) {
		ret = tac_download_fw_to_hw(tac_dev);
		if (ret) {
			dev_err(tac_dev->dev, "FW download failed, fw: %d\n", ret);
			goto io_init_err;
		}
	}

	if (tac_dev->sa_func_data) {
		ret = sdca_regmap_write_init(dev, tac_dev->regmap,
					     tac_dev->sa_func_data);
		if (ret) {
			dev_err(dev, "smartamp init table update failed\n");
			goto io_init_err;
		}
		dev_dbg(dev, "smartamp init done\n");

		if (first) {
			ret = regmap_multi_reg_write(tac_dev->regmap, tac_spk_seq,
						     ARRAY_SIZE(tac_spk_seq));
			if (ret) {
				dev_err(dev, "init writes failed, err=%d", ret);
				goto io_init_err;
			}
		}
	}

	if (tac_dev->sm_func_data) {
		ret = sdca_regmap_write_init(dev, tac_dev->regmap,
					     tac_dev->sm_func_data);
		if (ret) {
			dev_err(dev, "smartmic init table update failed\n");
			goto io_init_err;
		}
		dev_dbg(dev, "smartmic init done\n");

		if (first) {
			ret = regmap_multi_reg_write(tac_dev->regmap, tac_sm_seq,
						     ARRAY_SIZE(tac_sm_seq));
			if (ret) {
				dev_err(tac_dev->dev,
					"init writes failed, err=%d", ret);
				goto io_init_err;
			}
		}
	}

	if (tac_dev->uaj_func_data) {
		ret = sdca_regmap_write_init(dev, tac_dev->regmap,
					     tac_dev->uaj_func_data);
		if (ret) {
			dev_err(dev, "uaj init table update failed\n");
			goto io_init_err;
		}
		dev_dbg(dev, "uaj init done\n");

		if (first) {
			ret = regmap_multi_reg_write(tac_dev->regmap, tac_uaj_seq,
						     ARRAY_SIZE(tac_uaj_seq));
			if (ret) {
				dev_err(tac_dev->dev,
					"init writes failed, err=%d", ret);
				goto io_init_err;
			}

			if (tac_dev->hs_jack) {
				ret = tac5xx2_jack_init(tac_dev);
				if (ret) {
					dev_err(tac_dev->dev, "jack init failed");
					goto io_init_err;
				}
			}
		}
	}

	if (tac_dev->hid_func_data) {
		ret = sdca_regmap_write_init(dev, tac_dev->regmap,
					     tac_dev->hid_func_data);
		if (ret) {
			dev_err(dev, "hid init table update failed\n");
			goto io_init_err;
		}
		dev_dbg(dev, "hid init done\n");
	}

	tac_dev->hw_init = true;

	return 0;

io_init_err:
	dev_err(dev, "init writes failed, err=%d", ret);
	return ret;
}

static int tac_update_status(struct sdw_slave *slave,
			     enum sdw_slave_status status)
{
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(&slave->dev);
	struct device *dev = &slave->dev;
	bool first = false;
	int ret;

	tac_dev->status = status;
	if (status == SDW_SLAVE_UNATTACHED) {
		tac_dev->hw_init = false;
		tac_dev->fw_dl_success = false;
	}

	if (tac_dev->hw_init || tac_dev->status != SDW_SLAVE_ATTACHED) {
		dev_dbg(dev, "%s: early return, hw_init=%d, status=%d",
			__func__, tac_dev->hw_init, tac_dev->status);
		return 0;
	}

	if (!tac_dev->first_hw_init_done) {
		pm_runtime_set_active(tac_dev->dev);
		tac_dev->first_hw_init_done = true;
		first = true;
	}

	pm_runtime_get_noresume(tac_dev->dev);

	regcache_mark_dirty(tac_dev->regmap);
	regcache_cache_only(tac_dev->regmap, false);
	ret = tac_io_init(&slave->dev, slave, first);
	if (ret) {
		dev_err(dev, "Device initialization failed: %d\n", ret);
		goto err_out;
	}

	ret = regcache_sync(tac_dev->regmap);
	if (ret)
		dev_warn(dev, "Failed to sync regcache after init: %d\n", ret);

err_out:
	pm_runtime_mark_last_busy(tac_dev->dev);
	pm_runtime_put_autosuspend(tac_dev->dev);

	return ret;
}

static int tac5xx2_sdw_read_prop(struct sdw_slave *peripheral)
{
	struct device *dev = &peripheral->dev;
	int ret;

	ret = sdw_slave_read_prop(peripheral);
	if (ret) {
		dev_err(dev, "sdw_slave_read_prop failed: %d", ret);
		return ret;
	}

	return 0;
}

static int tac_port_prep(struct sdw_slave *slave, struct sdw_prepare_ch *prep_ch,
			 enum sdw_port_prep_ops pre_ops)
{
	struct device *dev = &slave->dev;
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	if (pre_ops != SDW_OPS_PORT_POST_PREP)
		return 0;

	if (!tac_dev->fw_dl_success)
		return 0;

	ret = regmap_read(tac_dev->regmap, TAC_DSP_ALGO_STATUS, &val);
	if (ret) {
		dev_err(dev, "Failed to read algo status: %d\n", ret);
		return ret;
	}

	if (val != TAC_DSP_ALGO_STATUS_RUNNING) {
		dev_dbg(dev, "Algo not running (0x%02x), re-enabling\n", val);
		ret = regmap_write(tac_dev->regmap, TAC_DSP_ALGO_STATUS,
				   TAC_DSP_ALGO_STATUS_RUNNING);
		if (ret) {
			dev_err(dev, "Failed to re-enable algo: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct sdw_slave_ops tac_sdw_ops = {
	.read_prop = tac5xx2_sdw_read_prop,
	.update_status = tac_update_status,
	.interrupt_callback = tac_interrupt_callback,
	.port_prep = tac_port_prep,
};

static s32 tac_sdw_probe(struct sdw_slave *peripheral,
			 const struct sdw_device_id *id)
{
	struct sdca_function_data *function_data = NULL;
	struct device *dev = &peripheral->dev;
	struct tac5xx2_prv *tac_dev;
	struct regmap *regmap;
	int ret, i;

	tac_dev = devm_kzalloc(dev, sizeof(*tac_dev), GFP_KERNEL);
	if (!tac_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed devm_kzalloc");

	if (peripheral->sdca_data.num_functions > 0) {
		dev_dbg(dev, "SDCA functions found: %d",
			peripheral->sdca_data.num_functions);

		for (i = 0; i < peripheral->sdca_data.num_functions; i++) {
			struct sdca_function_data **func_ptr;
			const char *func_name;

			switch (peripheral->sdca_data.function[i].type) {
			case SDCA_FUNCTION_TYPE_SMART_AMP:
				func_ptr = &tac_dev->sa_func_data;
				func_name = "smartamp";
				break;
			case SDCA_FUNCTION_TYPE_SMART_MIC:
				func_ptr = &tac_dev->sm_func_data;
				func_name = "smartmic";
				break;
			case SDCA_FUNCTION_TYPE_UAJ:
				func_ptr = &tac_dev->uaj_func_data;
				func_name = "uaj";
				break;
			case SDCA_FUNCTION_TYPE_HID:
				func_ptr = &tac_dev->hid_func_data;
				func_name = "hid";
				break;
			default:
				continue;
			}

			function_data = devm_kzalloc(dev, sizeof(*function_data),
						     GFP_KERNEL);
			if (!function_data)
				return dev_err_probe(dev, -ENOMEM,
						     "failed to allocate %s function data",
						     func_name);
			function_data->desc = &peripheral->sdca_data.function[i];
			ret = sdca_parse_function(dev, peripheral, function_data);
			if (!ret)
				*func_ptr = function_data;
			else
				devm_kfree(dev, function_data);
		}
	}

	dev_dbg(dev, "SDCA functions enabled: SA=%s SM=%s UAJ=%s HID=%s",
		tac_dev->sa_func_data ? "yes" : "no",
		tac_dev->sm_func_data ? "yes" : "no",
		tac_dev->uaj_func_data ? "yes" : "no",
		tac_dev->hid_func_data ? "yes" : "no");

	tac_dev->dev = dev;
	tac_dev->sdw_peripheral = peripheral;
	tac_dev->hw_init = false;
	tac_dev->first_hw_init_done = false;
	tac_dev->part_id = id->part_id;
	dev_set_drvdata(dev, tac_dev);

	regmap = devm_regmap_init_sdw_mbq_cfg(&peripheral->dev, peripheral,
					      &tac_regmap, &tac_mbq_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed devm_regmap_init_sdw\n");

	regcache_cache_only(regmap, true);
	tac_dev->regmap = regmap;
	tac_dev->jack_type = 0;
	init_completion(&tac_dev->fw_caching_complete);

	if (tac_has_dsp_algo(tac_dev)) {
		tac_generate_fw_name(peripheral, tac_dev->fw_binaryname,
				     sizeof(tac_dev->fw_binaryname));

		ret = tac_load_and_cache_firmware_async(tac_dev);
		if (ret) {
			complete_all(&tac_dev->fw_caching_complete);
			dev_dbg(dev, "failed to load fw: %d, use rom mode\n", ret);
		}
	} else {
		complete_all(&tac_dev->fw_caching_complete);
	}

	ret = tac_init(tac_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize tac device\n");

	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);

	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(dev);

	pm_runtime_enable(dev);
	/* the device is still not in active */

	return 0;
}

static void tac_sdw_remove(struct sdw_slave *peripheral)
{
	struct tac5xx2_prv *tac_dev = dev_get_drvdata(&peripheral->dev);

	pm_runtime_disable(tac_dev->dev);

	dev_set_drvdata(&peripheral->dev, NULL);
}

static const struct sdw_device_id tac_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x0102, 0x5572, 0),
	SDW_SLAVE_ENTRY(0x0102, 0x5672, 0),
	SDW_SLAVE_ENTRY(0x0102, 0x5682, 0),
	SDW_SLAVE_ENTRY(0x0102, 0x2883, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, tac_sdw_id);

static struct sdw_driver tac_sdw_driver = {
	.driver = {
		.name = "slave-tac5xx2",
		.pm = pm_ptr(&tac5xx2_sdca_pm),
	},
	.probe = tac_sdw_probe,
	.remove = tac_sdw_remove,
	.ops = &tac_sdw_ops,
	.id_table = tac_sdw_id,
};
module_sdw_driver(tac_sdw_driver);

MODULE_IMPORT_NS("SND_SOC_SDCA");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("ASoC TAC5XX2 SoundWire Driver");
MODULE_LICENSE("GPL");
