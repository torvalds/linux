/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2024 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
// Author: Kevin Lu <kevin-lu@ti.com>
//

#ifndef __TAS2781_DSP_H__
#define __TAS2781_DSP_H__

#define MAIN_ALL_DEVICES			0x0d
#define MAIN_DEVICE_A				0x01
#define MAIN_DEVICE_B				0x08
#define MAIN_DEVICE_C				0x10
#define MAIN_DEVICE_D				0x14
#define COEFF_DEVICE_A				0x03
#define COEFF_DEVICE_B				0x0a
#define COEFF_DEVICE_C				0x11
#define COEFF_DEVICE_D				0x15
#define PRE_DEVICE_A				0x04
#define PRE_DEVICE_B				0x0b
#define PRE_DEVICE_C				0x12
#define PRE_DEVICE_D				0x16

#define PPC3_VERSION				0x4100
#define PPC3_VERSION_TAS2781			0x14600
#define TASDEVICE_DEVICE_SUM			8
#define TASDEVICE_CONFIG_SUM			64

#define TASDEVICE_MAX_CHANNELS			8

enum tasdevice_dsp_dev_idx {
	TASDEVICE_DSP_TAS_2555 = 0,
	TASDEVICE_DSP_TAS_2555_STEREO,
	TASDEVICE_DSP_TAS_2557_MONO,
	TASDEVICE_DSP_TAS_2557_DUAL_MONO,
	TASDEVICE_DSP_TAS_2559,
	TASDEVICE_DSP_TAS_2563,
	TASDEVICE_DSP_TAS_2563_DUAL_MONO = 7,
	TASDEVICE_DSP_TAS_2563_QUAD,
	TASDEVICE_DSP_TAS_2563_21,
	TASDEVICE_DSP_TAS_2781,
	TASDEVICE_DSP_TAS_2781_DUAL_MONO,
	TASDEVICE_DSP_TAS_2781_21,
	TASDEVICE_DSP_TAS_2781_QUAD,
	TASDEVICE_DSP_TAS_MAX_DEVICE
};

struct tasdevice_fw_fixed_hdr {
	unsigned int fwsize;
	unsigned int ppcver;
	unsigned int drv_ver;
};

struct tasdevice_dspfw_hdr {
	struct tasdevice_fw_fixed_hdr fixed_hdr;
	unsigned short device_family;
	unsigned short device;
	unsigned char ndev;
};

struct tasdev_blk {
	int nr_retry;
	unsigned int type;
	unsigned char is_pchksum_present;
	unsigned char pchksum;
	unsigned char is_ychksum_present;
	unsigned char ychksum;
	unsigned int nr_cmds;
	unsigned int blk_size;
	unsigned int nr_subblocks;
	/* fixed m68k compiling issue, storing the dev_idx as a member of block
	 * can reduce unnecessary timeand system resource comsumption of
	 * dev_idx mapping every time the block data writing to the dsp.
	 */
	unsigned char dev_idx;
	unsigned char *data;
};

struct tasdevice_data {
	char name[64];
	unsigned int nr_blk;
	struct tasdev_blk *dev_blks;
};

struct tasdevice_prog {
	unsigned int prog_size;
	struct tasdevice_data dev_data;
};

struct tasdevice_config {
	unsigned int cfg_size;
	char name[64];
	struct tasdevice_data dev_data;
};

struct tasdevice_calibration {
	struct tasdevice_data dev_data;
};

struct tasdevice_fw {
	struct tasdevice_dspfw_hdr fw_hdr;
	unsigned short nr_programs;
	struct tasdevice_prog *programs;
	unsigned short nr_configurations;
	struct tasdevice_config *configs;
	unsigned short nr_calibrations;
	struct tasdevice_calibration *calibrations;
	struct device *dev;
};

enum tasdevice_fw_state {
	/* Driver in startup mode, not load any firmware. */
	TASDEVICE_DSP_FW_PENDING,
	/* DSP firmware in the system, but parsing error. */
	TASDEVICE_DSP_FW_FAIL,
	/*
	 * Only RCA (Reconfigurable Architecture) firmware load
	 * successfully.
	 */
	TASDEVICE_RCA_FW_OK,
	/* Both RCA and DSP firmware load successfully. */
	TASDEVICE_DSP_FW_ALL_OK,
};

enum tasdevice_bin_blk_type {
	TASDEVICE_BIN_BLK_COEFF = 1,
	TASDEVICE_BIN_BLK_POST_POWER_UP,
	TASDEVICE_BIN_BLK_PRE_SHUTDOWN,
	TASDEVICE_BIN_BLK_PRE_POWER_UP,
	TASDEVICE_BIN_BLK_POST_SHUTDOWN
};

struct tasdevice_rca_hdr {
	unsigned int img_sz;
	unsigned int checksum;
	unsigned int binary_version_num;
	unsigned int drv_fw_version;
	unsigned char plat_type;
	unsigned char dev_family;
	unsigned char reserve;
	unsigned char ndev;
	unsigned char devs[TASDEVICE_DEVICE_SUM];
	unsigned int nconfig;
	unsigned int config_size[TASDEVICE_CONFIG_SUM];
};

struct tasdev_blk_data {
	unsigned char dev_idx;
	unsigned char block_type;
	unsigned short yram_checksum;
	unsigned int block_size;
	unsigned int n_subblks;
	unsigned char *regdata;
};

struct tasdevice_config_info {
	unsigned int nblocks;
	unsigned int real_nblocks;
	unsigned char active_dev;
	struct tasdev_blk_data **blk_data;
};

struct tasdevice_rca {
	struct tasdevice_rca_hdr fw_hdr;
	int ncfgs;
	struct tasdevice_config_info **cfg_info;
	int profile_cfg_id;
};

void tasdevice_select_cfg_blk(void *context, int conf_no,
	unsigned char block_type);
void tasdevice_config_info_remove(void *context);
void tasdevice_dsp_remove(void *context);
int tasdevice_dsp_parser(void *context);
int tasdevice_rca_parser(void *context, const struct firmware *fmw);
void tasdevice_dsp_remove(void *context);
void tasdevice_calbin_remove(void *context);
int tasdevice_select_tuningprm_cfg(void *context, int prm,
	int cfg_no, int rca_conf_no);
int tasdevice_prmg_load(void *context, int prm_no);
void tasdevice_tuning_switch(void *context, int state);
int tas2781_load_calibration(void *context, char *file_name,
	unsigned short i);

#endif
