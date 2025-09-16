// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395_device.h --  AW88395 function for ALSA Audio Driver
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
//

#ifndef __AW88395_DEVICE_FILE_H__
#define __AW88395_DEVICE_FILE_H__

#include "aw88395.h"
#include "aw88395_data_type.h"
#include "aw88395_lib.h"

#define AW88395_DEV_DEFAULT_CH				(0)
#define AW88395_DEV_DSP_CHECK_MAX			(5)
#define AW88395_DSP_I2C_WRITES
#define AW88395_MAX_RAM_WRITE_BYTE_SIZE		(128)
#define AW88395_DSP_ODD_NUM_BIT_TEST			(0x5555)
#define AW88395_DSP_EVEN_NUM_BIT_TEST			(0xAAAA)
#define AW88395_DSP_ST_CHECK_MAX			(2)
#define AW88395_FADE_IN_OUT_DEFAULT			(0)
#define AW88395_CALI_RE_MAX				(15000)
#define AW88395_CALI_RE_MIN				(4000)
#define AW88395_CALI_DELAY_CACL(value)			((value * 32) / 48)

#define AW88395_DSP_RE_TO_SHOW_RE(re, shift)		(((re) * (1000)) >> (shift))
#define AW88395_SHOW_RE_TO_DSP_RE(re, shift)		(((re) << shift) / (1000))

#define AW88395_ACF_FILE				"aw88395_acf.bin"
#define AW88395_DEV_SYSST_CHECK_MAX			(10)

enum {
	AW88395_DEV_VDSEL_DAC = 0,
	AW88395_DEV_VDSEL_VSENSE = 1,
};

enum {
	AW88395_DSP_CRC_NA = 0,
	AW88395_DSP_CRC_OK = 1,
};

enum {
	AW88395_DSP_FW_UPDATE_OFF = 0,
	AW88395_DSP_FW_UPDATE_ON = 1,
};

enum {
	AW88395_FORCE_UPDATE_OFF = 0,
	AW88395_FORCE_UPDATE_ON = 1,
};

enum {
	AW88395_1000_US = 1000,
	AW88395_2000_US = 2000,
	AW88395_3000_US = 3000,
	AW88395_4000_US = 4000,
	AW88395_5000_US = 5000,
	AW88395_10000_US = 10000,
	AW88395_100000_US = 100000,
};

enum {
	AW88395_DEV_TYPE_OK = 0,
	AW88395_DEV_TYPE_NONE = 1,
};


enum AW88395_DEV_STATUS {
	AW88395_DEV_PW_OFF = 0,
	AW88395_DEV_PW_ON,
};

enum AW88395_DEV_FW_STATUS {
	AW88395_DEV_FW_FAILED = 0,
	AW88395_DEV_FW_OK,
};

enum AW88395_DEV_MEMCLK {
	AW88395_DEV_MEMCLK_OSC = 0,
	AW88395_DEV_MEMCLK_PLL = 1,
};

enum AW88395_DEV_DSP_CFG {
	AW88395_DEV_DSP_WORK = 0,
	AW88395_DEV_DSP_BYPASS = 1,
};

enum {
	AW88395_DSP_16_DATA = 0,
	AW88395_DSP_32_DATA = 1,
};

enum {
	AW88395_NOT_RCV_MODE = 0,
	AW88395_RCV_MODE = 1,
};

struct aw_profctrl_desc {
	unsigned int cur_mode;
};

enum {
	CALI_RESULT_NORMAL,
	CALI_RESULT_ERROR,
};

struct aw_volume_desc {
	unsigned int init_volume;
	unsigned int mute_volume;
	unsigned int ctl_volume;
	unsigned int max_volume;
};

struct aw_dsp_mem_desc {
	unsigned int dsp_madd_reg;
	unsigned int dsp_mdat_reg;
	unsigned int dsp_fw_base_addr;
	unsigned int dsp_cfg_base_addr;
};

struct aw_vmax_desc {
	unsigned int init_vmax;
};

struct aw_cali_delay_desc {
	unsigned int delay;
};

#define AW_CALI_CFG_NUM (4)
struct cali_cfg {
	uint32_t data[AW_CALI_CFG_NUM];
};

struct aw_cali_backup_desc {
	unsigned int dsp_ng_cfg;
	unsigned int dsp_lp_cfg;
};

struct aw_cali_desc {
	u32 cali_re;
	u32 ra;
	bool cali_switch;
	bool cali_running;
	uint16_t cali_result;
	uint16_t store_vol;
	struct cali_cfg cali_cfg;
	struct aw_cali_backup_desc backup_info;
};

struct aw_container {
	int len;
	u8 data[];
};

struct aw_device {
	int status;
	struct mutex dsp_lock;

	unsigned char prof_cur;
	unsigned char prof_index;
	unsigned char dsp_crc_st;
	unsigned char dsp_cfg;
	u16 chip_id;

	unsigned int channel;
	unsigned int fade_step;
	unsigned int prof_data_type;

	struct i2c_client *i2c;
	struct device *dev;
	struct regmap *regmap;
	char *acf;

	u32 dsp_fw_len;
	u32 dsp_cfg_len;
	u8 platform;
	u8 fw_status;

	unsigned int fade_in_time;
	unsigned int fade_out_time;

	struct aw_prof_info prof_info;
	struct aw_sec_data_desc crc_dsp_cfg;
	struct aw_profctrl_desc profctrl_desc;
	struct aw_volume_desc volume_desc;
	struct aw_dsp_mem_desc dsp_mem_desc;
	struct aw_vmax_desc vmax_desc;

	struct aw_cali_delay_desc cali_delay_desc;
	struct aw_cali_desc cali_desc;

};

int aw88395_init(struct aw_device **aw_dev, struct i2c_client *i2c, struct regmap *regmap);
int aw88395_dev_init(struct aw_device *aw_dev, struct aw_container *aw_cfg);
int aw88395_dev_start(struct aw_device *aw_dev);
int aw88395_dev_stop(struct aw_device *aw_dev);
int aw88395_dev_fw_update(struct aw_device *aw_dev, bool up_dsp_fw_en, bool force_up_en);

void aw88395_dev_set_volume(struct aw_device *aw_dev, unsigned short set_vol);
int aw88395_dev_get_prof_data(struct aw_device *aw_dev, int index,
			struct aw_prof_desc **prof_desc);
int aw88395_dev_get_prof_name(struct aw_device *aw_dev, int index, char **prof_name);
int aw88395_dev_set_profile_index(struct aw_device *aw_dev, int index);
int aw88395_dev_get_profile_index(struct aw_device *aw_dev);
int aw88395_dev_get_profile_count(struct aw_device *aw_dev);
int aw88395_dev_load_acf_check(struct aw_device *aw_dev, struct aw_container *aw_cfg);
int aw88395_dev_cfg_load(struct aw_device *aw_dev, struct aw_container *aw_cfg);
void aw88395_dev_mute(struct aw_device *aw_dev, bool is_mute);

#endif
