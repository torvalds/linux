// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TAS2783 Audio Smart Amplifier
//
// Copyright (C) 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2783 driver implements a flexible and configurable
// algo coefficient setting for single TAS2783 chips.
//
// Author: Niranjan H Y <niranjanhy@ti.com>
// Author: Baojun Xu <baojun.xu@ti.com>
// Author: Kevin Lu <kevin-lu@ti.com>

#include <linux/unaligned.h>
#include <linux/crc32.h>
#include <linux/efi.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/wait.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/tas2781-tlv.h>

#include "tas2783.h"

#define TIMEOUT_FW_DL_MS (3000)
#define FW_DL_OFFSET	36
#define FW_FL_HDR	12
#define TAS2783_PROBE_TIMEOUT 5000
#define TAS2783_CALI_GUID EFI_GUID(0x1f52d2a1, 0xbb3a, 0x457d, 0xbc, \
				   0x09, 0x43, 0xa3, 0xf4, 0x31, 0x0a, 0x92)

static const u32 tas2783_cali_reg[] = {
	TAS2783_CAL_R0,
	TAS2783_CAL_INVR0,
	TAS2783_CAL_R0LOW,
	TAS2783_CAL_POWER,
	TAS2783_CAL_TLIM,
};

struct bin_header_t {
	u16 vendor_id;
	u16 version;
	u32 file_id;
	u32 length;
};

struct calibration_data {
	u32 is_valid;
	unsigned long read_sz;
	u8 data[TAS2783_CALIB_DATA_SZ];
};

struct tas2783_prv {
	struct snd_soc_component *component;
	struct calibration_data cali_data;
	struct sdw_slave *sdw_peripheral;
	enum sdw_slave_status status;
	/* calibration */
	struct mutex calib_lock;
	/* pde and firmware download */
	struct mutex pde_lock;
	struct regmap *regmap;
	struct device *dev;
	struct class *class;
	struct attribute_group *cal_attr_groups;
	struct tm tm;
	u8 rca_binaryname[64];
	u8 dev_name[32];
	bool hw_init;
	/* wq for firmware download */
	wait_queue_head_t fw_wait;
	bool fw_dl_task_done;
	bool fw_dl_success;
};

static const struct reg_default tas2783_reg_default[] = {
	{TAS2783_AMP_LEVEL, 0x28},
	{TASDEV_REG_SDW(0, 0, 0x03), 0x28},
	{TASDEV_REG_SDW(0, 0, 0x04), 0x21},
	{TASDEV_REG_SDW(0, 0, 0x05), 0x41},
	{TASDEV_REG_SDW(0, 0, 0x06), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x07), 0x20},
	{TASDEV_REG_SDW(0, 0, 0x08), 0x09},
	{TASDEV_REG_SDW(0, 0, 0x09), 0x02},
	{TASDEV_REG_SDW(0, 0, 0x0a), 0x0a},
	{TASDEV_REG_SDW(0, 0, 0x0c), 0x10},
	{TASDEV_REG_SDW(0, 0, 0x0d), 0x13},
	{TASDEV_REG_SDW(0, 0, 0x0e), 0xc2},
	{TASDEV_REG_SDW(0, 0, 0x0f), 0x40},
	{TASDEV_REG_SDW(0, 0, 0x10), 0x04},
	{TASDEV_REG_SDW(0, 0, 0x13), 0x13},
	{TASDEV_REG_SDW(0, 0, 0x14), 0x12},
	{TASDEV_REG_SDW(0, 0, 0x15), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x16), 0x12},
	{TASDEV_REG_SDW(0, 0, 0x17), 0x80},
	{TAS2783_DVC_LVL, 0x00},
	{TASDEV_REG_SDW(0, 0, 0x1b), 0x61},
	{TASDEV_REG_SDW(0, 0, 0x1c), 0x36},
	{TASDEV_REG_SDW(0, 0, 0x1d), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x1f), 0x01},
	{TASDEV_REG_SDW(0, 0, 0x20), 0x2e},
	{TASDEV_REG_SDW(0, 0, 0x21), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x34), 0x06},
	{TASDEV_REG_SDW(0, 0, 0x35), 0xbd},
	{TASDEV_REG_SDW(0, 0, 0x36), 0xad},
	{TASDEV_REG_SDW(0, 0, 0x37), 0xa8},
	{TASDEV_REG_SDW(0, 0, 0x38), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x3b), 0xfc},
	{TASDEV_REG_SDW(0, 0, 0x3d), 0xdd},
	{TASDEV_REG_SDW(0, 0, 0x40), 0xf6},
	{TASDEV_REG_SDW(0, 0, 0x41), 0x14},
	{TASDEV_REG_SDW(0, 0, 0x5c), 0x19},
	{TASDEV_REG_SDW(0, 0, 0x5d), 0x80},
	{TASDEV_REG_SDW(0, 0, 0x63), 0x48},
	{TASDEV_REG_SDW(0, 0, 0x65), 0x08},
	{TASDEV_REG_SDW(0, 0, 0x66), 0xb2},
	{TASDEV_REG_SDW(0, 0, 0x67), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x6a), 0x12},
	{TASDEV_REG_SDW(0, 0, 0x6b), 0xfb},
	{TASDEV_REG_SDW(0, 0, 0x6c), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x6d), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x6e), 0x1a},
	{TASDEV_REG_SDW(0, 0, 0x6f), 0x00},
	{TASDEV_REG_SDW(0, 0, 0x70), 0x96},
	{TASDEV_REG_SDW(0, 0, 0x71), 0x02},
	{TASDEV_REG_SDW(0, 0, 0x73), 0x08},
	{TASDEV_REG_SDW(0, 0, 0x75), 0xe0},
	{TASDEV_REG_SDW(0, 0, 0x7a), 0x60},
	{TASDEV_REG_SDW(0, 0, 0x60), 0x21},
	{TASDEV_REG_SDW(0, 1, 0x02), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x17), 0xc0},
	{TASDEV_REG_SDW(0, 1, 0x19), 0x60},
	{TASDEV_REG_SDW(0, 1, 0x35), 0x75},
	{TASDEV_REG_SDW(0, 1, 0x3d), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x3e), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x3f), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x40), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x41), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x42), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x43), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x44), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x45), 0x00},
	{TASDEV_REG_SDW(0, 1, 0x47), 0xab},
	{TASDEV_REG_SDW(0, 0xfd, 0x0d), 0x0d},
	{TASDEV_REG_SDW(0, 0xfd, 0x39), 0x00},
	{TASDEV_REG_SDW(0, 0xfd, 0x3e), 0x00},
	{TASDEV_REG_SDW(0, 0xfd, 0x45), 0x00},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS21, 0x02, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS21, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS24, 0x02, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS24, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS26, 0x02, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS26, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS28, 0x02, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS28, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS127, 0x02, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS127, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU21, 0x01, 1), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU21, 0x02, 1), 0x9c00},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x01, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x01, 1), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x0b, 1), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x01, 1), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x01, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x0b, 1), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 1), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 2), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 1), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 2), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x01, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x05, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x01, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x05, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 1), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 2), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 3), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 4), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 5), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 6), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 7), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x06, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT23, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT23, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x04, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 1), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 2), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 3), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 4), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 5), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 6), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 7), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 8), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 9), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xa), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xb), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xc), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xd), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xe), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xf), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23, 0x1, 0), 0x3},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23, 0x10, 0), 0x3},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x06, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x13, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x06, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x13, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x05, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x10, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x11, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_TG23, 0x10, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x01, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x06, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x07, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x08, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x09, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x0a, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x10, 0), 0x1},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x12, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x13, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x14, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x15, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x16, 0), 0x0},
	{SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_UDMPU23, 0x10, 0), 0x0},
};

static const struct reg_sequence tas2783_init_seq[] = {
	REG_SEQ0(SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x10, 0x00), 0x04),
	REG_SEQ0(0x00800418, 0x00),
	REG_SEQ0(0x00800419, 0x00),
	REG_SEQ0(0x0080041a, 0x00),
	REG_SEQ0(0x0080041b, 0x00),
	REG_SEQ0(0x00800428, 0x40),
	REG_SEQ0(0x00800429, 0x00),
	REG_SEQ0(0x0080042a, 0x00),
	REG_SEQ0(0x0080042b, 0x00),
	REG_SEQ0(SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x1, 0x00), 0x00),
	REG_SEQ0(0x0080005c, 0xD9),
	REG_SEQ0(0x00800082, 0x20),
	REG_SEQ0(0x008000a1, 0x00),
	REG_SEQ0(0x00800097, 0xc8),
	REG_SEQ0(0x00800099, 0x20),
	REG_SEQ0(0x008000c7, 0xaa),
	REG_SEQ0(0x008000b5, 0x74),
	REG_SEQ0(0x00800082, 0x20),
	REG_SEQ0(0x00807e8d, 0x0d),
	REG_SEQ0(0x00807eb9, 0x53),
	REG_SEQ0(0x00807ebe, 0x42),
	REG_SEQ0(0x00807ec5, 0x37),
	REG_SEQ0(0x00800066, 0x92),
	REG_SEQ0(0x00800003, 0x28),
	REG_SEQ0(0x00800004, 0x21),
	REG_SEQ0(0x00800005, 0x41),
	REG_SEQ0(0x00800006, 0x00),
	REG_SEQ0(0x00800007, 0x20),
	REG_SEQ0(0x0080000c, 0x10),
	REG_SEQ0(0x00800013, 0x08),
	REG_SEQ0(0x00800015, 0x00),
	REG_SEQ0(0x00800017, 0x80),
	REG_SEQ0(0x0080001a, 0x00),
	REG_SEQ0(0x0080001b, 0x22),
	REG_SEQ0(0x0080001c, 0x36),
	REG_SEQ0(0x0080001d, 0x01),
	REG_SEQ0(0x0080001f, 0x00),
	REG_SEQ0(0x00800020, 0x2e),
	REG_SEQ0(0x00800034, 0x06),
	REG_SEQ0(0x00800035, 0xb9),
	REG_SEQ0(0x00800036, 0xad),
	REG_SEQ0(0x00800037, 0xa8),
	REG_SEQ0(0x00800038, 0x00),
	REG_SEQ0(0x0080003b, 0xfc),
	REG_SEQ0(0x0080003d, 0xdd),
	REG_SEQ0(0x00800040, 0xf6),
	REG_SEQ0(0x00800041, 0x14),
	REG_SEQ0(0x0080005c, 0x19),
	REG_SEQ0(0x0080005d, 0x80),
	REG_SEQ0(0x00800063, 0x48),
	REG_SEQ0(0x00800065, 0x08),
	REG_SEQ0(0x00800067, 0x00),
	REG_SEQ0(0x0080006a, 0x12),
	REG_SEQ0(0x0080006b, 0x7b),
	REG_SEQ0(0x0080006c, 0x00),
	REG_SEQ0(0x0080006d, 0x00),
	REG_SEQ0(0x0080006e, 0x1a),
	REG_SEQ0(0x0080006f, 0x00),
	REG_SEQ0(0x00800070, 0x96),
	REG_SEQ0(0x00800071, 0x02),
	REG_SEQ0(0x00800073, 0x08),
	REG_SEQ0(0x00800075, 0xe0),
	REG_SEQ0(0x0080007a, 0x60),
	REG_SEQ0(0x008000bd, 0x00),
	REG_SEQ0(0x008000be, 0x00),
	REG_SEQ0(0x008000bf, 0x00),
	REG_SEQ0(0x008000c0, 0x00),
	REG_SEQ0(0x008000c1, 0x00),
	REG_SEQ0(0x008000c2, 0x00),
	REG_SEQ0(0x008000c3, 0x00),
	REG_SEQ0(0x008000c4, 0x00),
	REG_SEQ0(0x008000c5, 0x00),
	REG_SEQ0(0x00800008, 0x49),
	REG_SEQ0(0x00800009, 0x02),
	REG_SEQ0(0x0080000a, 0x1a),
	REG_SEQ0(0x0080000d, 0x93),
	REG_SEQ0(0x0080000e, 0x82),
	REG_SEQ0(0x0080000f, 0x42),
	REG_SEQ0(0x00800010, 0x84),
	REG_SEQ0(0x00800014, 0x0a),
	REG_SEQ0(0x00800016, 0x00),
	REG_SEQ0(0x00800060, 0x21),
};

static int tas2783_sdca_mbq_size(struct device *dev, u32 reg)
{
	switch (reg) {
	case 0x000 ... 0x080: /* Data port 0. */
	case 0x100 ... 0x140: /* Data port 1. */
	case 0x200 ... 0x240: /* Data port 2. */
	case 0x300 ... 0x340: /* Data port 3. */
	case 0x400 ... 0x440: /* Data port 4. */
	case 0x500 ... 0x540: /* Data port 5. */
	case 0x800000 ... 0x803fff: /* Page 0 ~ 127. */
	case 0x807e80 ... 0x807eff: /* Page 253. */
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_UDMPU23,
			  TAS2783_SDCA_CTL_UDMPU_CLUSTER, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU21, TAS2783_SDCA_CTL_FU_MUTE,
			  TAS2783_DEVICE_CHANNEL_LEFT):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23, 0x1, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_TG23, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x0a, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x14, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x15, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x16, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT23, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 2):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 3):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 4):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 5):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 6):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 7):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 8):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 9):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xa):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xb):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xc):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xd):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xe):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x12, 0xf):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS21, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS21, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS24, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS24, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS25, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS25, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS127, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS127, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS26, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS26, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS28, 0x02, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_CS28, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x05, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 2):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x01, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x04, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x05, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x01, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x04, 0):
		return 1;

	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x11, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 2):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 3):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 4):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 5):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 6):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x01, 7):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU21, 0x02, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x0b, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 2):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x0b, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x0b, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x0b, 1):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x07, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x09, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x13, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x13, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x11, 0):
		return 2;

	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT21, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT26, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT28, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_IT29, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT23, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT24, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT25, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT28, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_OT127, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MU26, 0x06, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU127, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU26, 0x10, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x06, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x12, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_XU22, 0x13, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU21, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_MFPU26, 0x08, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_SAPU29, 0x05, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU21, 0x06, 0):
	case SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PPU26, 0x06, 0):
		return 4;

	default:
		return 0;
	}
}

static bool tas2783_readable_register(struct device *dev, unsigned int reg)
{
	return tas2783_sdca_mbq_size(dev, reg) > 0;
}

static bool tas2783_volatile_register(struct device *dev, u32 reg)
{
	switch (reg) {
	case 0x000 ... 0x080: /* Data port 0. */
	case 0x100 ... 0x140: /* Data port 1. */
	case 0x200 ... 0x240: /* Data port 2. */
	case 0x300 ... 0x340: /* Data port 3. */
	case 0x400 ... 0x440: /* Data port 4. */
	case 0x500 ... 0x540: /* Data port 5. */
	case 0x800001:
		return true;

	default:
		return false;
	}
}

static const struct regmap_config tas_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = tas2783_readable_register,
	.volatile_reg = tas2783_volatile_register,
	.reg_defaults = tas2783_reg_default,
	.num_reg_defaults = ARRAY_SIZE(tas2783_reg_default),
	.max_register = 0x41008000 + TASDEV_REG_SDW(0xa1, 0x60, 0x7f),
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct regmap_sdw_mbq_cfg tas2783_mbq_cfg = {
	.mbq_size = tas2783_sdca_mbq_size,
};

static s32 tas2783_digital_getvol(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw(kcontrol, ucontrol);
}

static s32 tas2783_digital_putvol(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static s32 tas2783_amp_getvol(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_get_volsw(kcontrol, ucontrol);
}

static s32 tas2783_amp_putvol(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static const struct snd_kcontrol_new tas2783_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("Amp Volume", TAS2783_AMP_LEVEL,
				 1, 0, 20, 0, tas2783_amp_getvol,
				 tas2783_amp_putvol, tas2781_amp_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("Speaker Volume", TAS2783_DVC_LVL,
				 0, 0, 200, 1, tas2783_digital_getvol,
				 tas2783_digital_putvol, tas2781_dvc_tlv),
};

static s32 tas2783_validate_calibdata(struct tas2783_prv *tas_dev,
				      u8 *data, u32 size)
{
	u32 ts, spk_count, size_calculated;
	u32 crc_calculated, crc_read, i;
	u32 *tmp_val;
	struct tm tm;

	i = 0;
	tmp_val = (u32 *)data;
	if (tmp_val[i++] != 2783) {
		dev_err(tas_dev->dev, "cal data magic number mismatch");
		return -EINVAL;
	}

	spk_count = tmp_val[i++];
	if (spk_count > TAS2783_CALIB_MAX_SPK_COUNT) {
		dev_err(tas_dev->dev, "cal data spk_count too large");
		return -EINVAL;
	}

	ts = tmp_val[i++];
	time64_to_tm(ts, 0, &tm);
	dev_dbg(tas_dev->dev, "cal data timestamp: %ld-%d-%d %d:%d:%d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	size_calculated =
		(spk_count * TAS2783_CALIB_PARAMS * sizeof(u32)) +
		TAS2783_CALIB_HDR_SZ + TAS2783_CALIB_CRC_SZ;
	if (size_calculated > TAS2783_CALIB_DATA_SZ) {
		dev_err(tas_dev->dev, "cali data sz too large");
		return -EINVAL;
	} else if (size < size_calculated) {
		dev_err(tas_dev->dev, "cali data size mismatch calc=%u vs %d\n",
			size, size_calculated);
		return -EINVAL;
	}

	crc_calculated = crc32(~0, data,
			       size_calculated - TAS2783_CALIB_CRC_SZ) ^ ~0;
	crc_read = tmp_val[(size_calculated - TAS2783_CALIB_CRC_SZ) / sizeof(u32)];
	if (crc_calculated != crc_read) {
		dev_err(tas_dev->dev,
			"calib data integrity check fail, 0x%08x vs 0x%08x\n",
			crc_calculated, crc_read);
		return -EINVAL;
	}

	return 0;
}

static void tas2783_set_calib_params_to_device(struct tas2783_prv *tas_dev, u32 *cali_data)
{
	u32 dev_count, offset, i, device_num;
	u32 reg_value;
	u8 buf[4];

	dev_count = cali_data[1];
	offset = 3;

	for (device_num = 0; device_num < dev_count; device_num++) {
		if (cali_data[offset] != tas_dev->sdw_peripheral->id.unique_id) {
			offset += TAS2783_CALIB_PARAMS;
			continue;
		}
		offset++;

		for (i = 0; i < ARRAY_SIZE(tas2783_cali_reg); i++) {
			reg_value = cali_data[offset + i];
			buf[0] = reg_value >> 24;
			buf[1] = reg_value >> 16;
			buf[2] = reg_value >> 8;
			buf[3] = reg_value & 0xff;
			regmap_bulk_write(tas_dev->regmap, tas2783_cali_reg[i],
					  buf, sizeof(u32));
		}
		break;
	}

	if (device_num == dev_count)
		dev_err(tas_dev->dev, "device not found\n");
	else
		dev_dbg(tas_dev->dev, "calib data update done\n");
}

static s32 tas2783_update_calibdata(struct tas2783_prv *tas_dev)
{
	efi_guid_t efi_guid = TAS2783_CALI_GUID;
	u32 attr, i, *tmp_val;
	unsigned long size;
	s32 ret;
	efi_status_t status;
	static efi_char16_t efi_names[][32] = {
		L"SmartAmpCalibrationData", L"CALI_DATA"};

	tmp_val = (u32 *)tas_dev->cali_data.data;
	attr = 0;
	i = 0;

	/*
	 * In some cases, the calibration is performed in Windows,
	 * and data was saved in UEFI. Linux can access it.
	 */
	for (i = 0; i < ARRAY_SIZE(efi_names); i++) {
		size = 0;
		status = efi.get_variable(efi_names[i], &efi_guid, &attr,
					  &size, NULL);
		if (size > TAS2783_CALIB_DATA_SZ) {
			dev_err(tas_dev->dev, "cali data too large\n");
			break;
		}

		tas_dev->cali_data.read_sz = size;
		if (status == EFI_BUFFER_TOO_SMALL) {
			status = efi.get_variable(efi_names[i], &efi_guid, &attr,
							&tas_dev->cali_data.read_sz,
							tas_dev->cali_data.data);
			dev_dbg(tas_dev->dev, "cali get %lu bytes result:%ld\n",
				tas_dev->cali_data.read_sz, status);
		}
		if (status == EFI_SUCCESS)
			break;
	}

	if (status != EFI_SUCCESS) {
		/* Failed got calibration data from EFI. */
		dev_dbg(tas_dev->dev, "No calibration data in UEFI.");
		return 0;
	}

	mutex_lock(&tas_dev->calib_lock);
	ret = tas2783_validate_calibdata(tas_dev, tas_dev->cali_data.data,
					 tas_dev->cali_data.read_sz);
	if (!ret)
		tas2783_set_calib_params_to_device(tas_dev, tmp_val);
	mutex_unlock(&tas_dev->calib_lock);

	return ret;
}

static s32 read_header(const u8 *data, struct bin_header_t *hdr)
{
	hdr->vendor_id = get_unaligned_le16(&data[0]);
	hdr->file_id = get_unaligned_le32(&data[2]);
	hdr->version = get_unaligned_le16(&data[6]);
	hdr->length = get_unaligned_le32(&data[8]);
	return 12;
}

static void tas2783_fw_ready(const struct firmware *fmw, void *context)
{
	struct tas2783_prv *tas_dev =
		(struct tas2783_prv *)context;
	const u8 *buf = NULL;
	s32 offset = 0, img_sz, file_blk_size, ret;
	struct bin_header_t hdr;

	if (!fmw || !fmw->data) {
		/* No firmware binary, devices will work in ROM mode. */
		dev_err(tas_dev->dev,
			"Failed to read %s, no side-effect on driver running\n",
			tas_dev->rca_binaryname);
		ret = -EINVAL;
		goto out;
	}

	img_sz = fmw->size;
	buf = fmw->data;
	offset += FW_DL_OFFSET;
	if (offset >= (img_sz - FW_FL_HDR)) {
		dev_err(tas_dev->dev,
			"firmware is too small");
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&tas_dev->pde_lock);
	while (offset < (img_sz - FW_FL_HDR)) {
		memset(&hdr, 0, sizeof(hdr));
		offset += read_header(&buf[offset], &hdr);
		dev_dbg(tas_dev->dev,
			"vndr=%d, file=%d, version=%d, len=%d, off=%d\n",
			hdr.vendor_id, hdr.file_id, hdr.version,
			hdr.length, offset);
		/* size also includes the header */
		file_blk_size = hdr.length - FW_FL_HDR;

		/* make sure that enough data is there */
		if (offset + file_blk_size > img_sz) {
			ret = -EINVAL;
			dev_err(tas_dev->dev,
				"corrupt firmware file");
			break;
		}

		switch (hdr.file_id) {
		case 0:
			ret = sdw_nwrite_no_pm(tas_dev->sdw_peripheral,
					       PRAM_ADDR_START, file_blk_size,
					       &buf[offset]);
			if (ret < 0)
				dev_err(tas_dev->dev,
					"PRAM update failed: %d", ret);
			break;

		case 1:
			ret = sdw_nwrite_no_pm(tas_dev->sdw_peripheral,
					       YRAM_ADDR_START, file_blk_size,
					       &buf[offset]);
			if (ret < 0)
				dev_err(tas_dev->dev,
					"YRAM update failed: %d", ret);

			break;

		default:
			ret = -EINVAL;
			dev_err(tas_dev->dev, "Unsupported file");
			break;
		}

		if (ret == 0)
			offset += file_blk_size;
		else
			break;
	}
	mutex_unlock(&tas_dev->pde_lock);
	if (!ret)
		tas2783_update_calibdata(tas_dev);

out:
	if (!ret)
		tas_dev->fw_dl_success = true;
	tas_dev->fw_dl_task_done = true;
	wake_up(&tas_dev->fw_wait);
	if (fmw)
		release_firmware(fmw);
}

static inline s32 tas_clear_latch(struct tas2783_prv *priv)
{
	return regmap_update_bits(priv->regmap,
				  TASDEV_REG_SDW(0, 0, 0x5c),
				  0x04, 0x04);
}

static s32 tas_fu21_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, s32 event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas2783_prv *tas_dev = snd_soc_component_get_drvdata(component);
	s32 mute;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		mute = 0;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		mute = 1;
		break;
	}

	return sdw_write_no_pm(tas_dev->sdw_peripheral,
			       SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU21,
					    TAS2783_SDCA_CTL_FU_MUTE, 1), mute);
}

static s32 tas_fu23_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, s32 event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas2783_prv *tas_dev = snd_soc_component_get_drvdata(component);
	s32 mute;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		mute = 0;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		mute = 1;
		break;
	}

	return sdw_write_no_pm(tas_dev->sdw_peripheral,
			       SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_FU23,
					    TAS2783_SDCA_CTL_FU_MUTE, 1), mute);
}

static const struct snd_soc_dapm_widget tas_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI", "ASI Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASI OUT", "ASI Capture", 0, SND_SOC_NOPM,
			     0, 0),
	SND_SOC_DAPM_DAC_E("FU21", NULL, SND_SOC_NOPM, 0, 0, tas_fu21_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU23", NULL, SND_SOC_NOPM, 0, 0, tas_fu23_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("DMIC"),
};

static const struct snd_soc_dapm_route tas_audio_map[] = {
	{"FU21", NULL, "ASI"},
	{"SPK", NULL, "FU21"},
	{"FU23", NULL, "ASI"},
	{"SPK", NULL, "FU23"},
	{"ASI OUT", NULL, "DMIC"},
};

static s32 tas_set_sdw_stream(struct snd_soc_dai *dai,
			      void *sdw_stream, s32 direction)
{
	if (!sdw_stream)
		return 0;

	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void tas_sdw_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static s32 tas_sdw_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2783_prv *tas_dev =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_runtime *sdw_stream;
	struct sdw_slave *sdw_peripheral = tas_dev->sdw_peripheral;
	s32 ret, retry = 3;

	if (!tas_dev->fw_dl_success) {
		dev_err(tas_dev->dev, "error playback without fw download");
		return -EINVAL;
	}

	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!sdw_stream)
		return -EINVAL;

	ret = tas_clear_latch(tas_dev);
	if (ret)
		dev_err(tas_dev->dev,
			"clear latch failed, err=%d", ret);

	mutex_lock(&tas_dev->pde_lock);
	/*
	 * Sometimes, there is error returned during power on.
	 * So added retry logic to ensure power on so that
	 * port prepare succeeds
	 */
	do {
		ret = regmap_write(tas_dev->regmap,
				   SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23,
						TAS2783_SDCA_CTL_REQ_POW_STATE, 0),
						TAS2783_SDCA_POW_STATE_ON);
		if (!ret)
			break;
		usleep_range(2000, 2200);
	} while (retry--);
	mutex_unlock(&tas_dev->pde_lock);
	if (ret)
		return ret;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params,
				 &stream_config, &port_config);
	/* port 1 for playback */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_config.num = 1;
	else
		port_config.num = 2;

	ret = sdw_stream_add_slave(sdw_peripheral,
				   &stream_config, &port_config, 1, sdw_stream);
	if (ret)
		dev_err(dai->dev, "Unable to configure port\n");

	return ret;
}

static s32 tas_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	s32 ret;
	struct snd_soc_component *component = dai->component;
	struct tas2783_prv *tas_dev =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	sdw_stream_remove_slave(tas_dev->sdw_peripheral, sdw_stream);

	mutex_lock(&tas_dev->pde_lock);
	ret = regmap_write(tas_dev->regmap,
			   SDW_SDCA_CTL(1, TAS2783_SDCA_ENT_PDE23,
					TAS2783_SDCA_CTL_REQ_POW_STATE, 0),
			   TAS2783_SDCA_POW_STATE_OFF);
	mutex_unlock(&tas_dev->pde_lock);

	return ret;
}

static const struct snd_soc_dai_ops tas_dai_ops = {
	.hw_params	= tas_sdw_hw_params,
	.hw_free	= tas_sdw_pcm_hw_free,
	.set_stream	= tas_set_sdw_stream,
	.shutdown	= tas_sdw_shutdown,
};

static struct snd_soc_dai_driver tas_dai_driver[] = {
	{
		.name = "tas2783-codec",
		.id = 0,
		.playback = {
			.stream_name	= "Playback",
			.channels_min	= 1,
			.channels_max	= 4,
			.rates		= TAS2783_DEVICE_RATES,
			.formats	= TAS2783_DEVICE_FORMATS,
		},
		.capture = {
			.stream_name	= "Capture",
			.channels_min	= 1,
			.channels_max	= 4,
			.rates		= TAS2783_DEVICE_RATES,
			.formats	= TAS2783_DEVICE_FORMATS,
		},
		.ops = &tas_dai_ops,
		.symmetric_rate = 1,
	},
};

static s32 tas_component_probe(struct snd_soc_component *component)
{
	struct tas2783_prv *tas_dev =
		snd_soc_component_get_drvdata(component);

	tas_dev->component = component;
	tas25xx_register_misc(tas_dev->sdw_peripheral);

	return 0;
}

static void tas_component_remove(struct snd_soc_component *codec)
{
	struct tas2783_prv *tas_dev =
			snd_soc_component_get_drvdata(codec);
	tas25xx_deregister_misc();
	tas_dev->component = NULL;
}

static const struct snd_soc_component_driver soc_codec_driver_tasdevice = {
	.probe = tas_component_probe,
	.remove = tas_component_remove,
	.controls = tas2783_snd_controls,
	.num_controls = ARRAY_SIZE(tas2783_snd_controls),
	.dapm_widgets = tas_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tas_dapm_widgets),
	.dapm_routes = tas_audio_map,
	.num_dapm_routes = ARRAY_SIZE(tas_audio_map),
	.idle_bias_on = 1,
	.endianness = 1,
};

static s32 tas_init(struct tas2783_prv *tas_dev)
{
	s32 ret;

	dev_set_drvdata(tas_dev->dev, tas_dev);
	ret = devm_snd_soc_register_component(tas_dev->dev,
					      &soc_codec_driver_tasdevice,
					      tas_dai_driver,
					      ARRAY_SIZE(tas_dai_driver));
	if (ret) {
		dev_err(tas_dev->dev, "%s: codec register error:%d.\n",
			__func__, ret);
		return ret;
	}

	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(tas_dev->dev, 3000);
	pm_runtime_use_autosuspend(tas_dev->dev);
	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(tas_dev->dev);
	pm_runtime_enable(tas_dev->dev);

	return ret;
}

static s32 tas_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	s32 nval;
	s32 i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask =
		SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x04; /* BITMAP: 00000100 */
	prop->sink_ports = 0x2; /* BITMAP:  00000010 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					  sizeof(*prop->src_dpn_prop), GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = false;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop), GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = false;
		dpn[j].ch_prep_timeout = 10;
		j++;
	}

	/* set the timeout values */
	prop->clk_stop_timeout = 200;

	return 0;
}

static s32 tas2783_sdca_dev_suspend(struct device *dev)
{
	struct tas2783_prv *tas_dev = dev_get_drvdata(dev);

	if (!tas_dev->hw_init)
		return 0;

	regcache_cache_only(tas_dev->regmap, true);
	return 0;
}

static s32 tas2783_sdca_dev_system_suspend(struct device *dev)
{
	return tas2783_sdca_dev_suspend(dev);
}

static s32 tas2783_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct tas2783_prv *tas_dev = dev_get_drvdata(dev);
	unsigned long t;

	if (!slave->unattach_request)
		goto regmap_sync;

	t = wait_for_completion_timeout(&slave->initialization_complete,
					msecs_to_jiffies(TAS2783_PROBE_TIMEOUT));
	if (!t) {
		dev_err(&slave->dev, "resume: initialization timed out\n");
		sdw_show_ping_status(slave->bus, true);
		return -ETIMEDOUT;
	}

	slave->unattach_request = 0;

regmap_sync:
	regcache_cache_only(tas_dev->regmap, false);
	regcache_sync(tas_dev->regmap);
	return 0;
}

static const struct dev_pm_ops tas2783_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(tas2783_sdca_dev_system_suspend, tas2783_sdca_dev_resume)
	RUNTIME_PM_OPS(tas2783_sdca_dev_suspend, tas2783_sdca_dev_resume, NULL)
};

static s32 tas_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct tas2783_prv *tas_dev = dev_get_drvdata(dev);
	s32 ret;
	u8 unique_id = tas_dev->sdw_peripheral->id.unique_id;

	if (tas_dev->hw_init)
		return 0;

	tas_dev->fw_dl_task_done = false;
	tas_dev->fw_dl_success = false;
	scnprintf(tas_dev->rca_binaryname, sizeof(tas_dev->rca_binaryname),
		  "tas2783-%01x.bin", unique_id);

	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				      tas_dev->rca_binaryname, tas_dev->dev,
				      GFP_KERNEL, tas_dev, tas2783_fw_ready);
	if (ret) {
		dev_err(tas_dev->dev,
			"firmware request failed for uid=%d, ret=%d\n",
			unique_id, ret);
		return ret;
	}

	ret = wait_event_timeout(tas_dev->fw_wait, tas_dev->fw_dl_task_done,
				 msecs_to_jiffies(TIMEOUT_FW_DL_MS));
	if (!ret) {
		dev_err(tas_dev->dev, "fw request, wait_event timeout\n");
		ret = -EAGAIN;
	} else {
		ret = regmap_multi_reg_write(tas_dev->regmap, tas2783_init_seq,
					     ARRAY_SIZE(tas2783_init_seq));
		tas_dev->hw_init = true;
	}

	return ret;
}

static s32 tas_update_status(struct sdw_slave *slave,
			     enum sdw_slave_status status)
{
	struct tas2783_prv *tas_dev = dev_get_drvdata(&slave->dev);
	struct device *dev = &slave->dev;

	dev_dbg(dev, "Peripheral status = %s",
		status == SDW_SLAVE_UNATTACHED ? "unattached" :
		 status == SDW_SLAVE_ATTACHED ? "attached" : "alert");

	tas_dev->status = status;
	if (status == SDW_SLAVE_UNATTACHED)
		tas_dev->hw_init = false;

	/* Perform initialization only if slave status
	 * is present and hw_init flag is false
	 */
	if (tas_dev->hw_init || tas_dev->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* updated the cache data to device */
	regcache_cache_only(tas_dev->regmap, false);
	regcache_sync(tas_dev->regmap);

	/* perform I/O transfers required for Slave initialization */
	return tas_io_init(&slave->dev, slave);
}

static const struct sdw_slave_ops tas_sdw_ops = {
	.read_prop	= tas_read_prop,
	.update_status	= tas_update_status,
};

static void tas_remove(struct tas2783_prv *tas_dev)
{
	snd_soc_unregister_component(tas_dev->dev);
}

static s32 tas_sdw_probe(struct sdw_slave *peripheral,
			 const struct sdw_device_id *id)
{
	struct regmap *regmap;
	struct device *dev = &peripheral->dev;
	struct tas2783_prv *tas_dev;

	tas_dev = devm_kzalloc(dev, sizeof(*tas_dev), GFP_KERNEL);
	if (!tas_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed devm_kzalloc");

	tas_dev->dev = dev;
	tas_dev->sdw_peripheral = peripheral;
	tas_dev->hw_init = false;
	mutex_init(&tas_dev->calib_lock);
	mutex_init(&tas_dev->pde_lock);

	init_waitqueue_head(&tas_dev->fw_wait);
	dev_set_drvdata(dev, tas_dev);
	regmap = devm_regmap_init_sdw_mbq_cfg(peripheral,
					      &tas_regmap,
					      &tas2783_mbq_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed devm_regmap_init_sdw.");

	/* keep in cache until the device is fully initialized */
	regcache_cache_only(regmap, true);
	tas_dev->regmap = regmap;
	return tas_init(tas_dev);
}

static s32 tas_sdw_remove(struct sdw_slave *peripheral)
{
	struct tas2783_prv *tas_dev = dev_get_drvdata(&peripheral->dev);

	pm_runtime_disable(tas_dev->dev);
	tas_remove(tas_dev);
	mutex_destroy(&tas_dev->calib_lock);
	mutex_destroy(&tas_dev->pde_lock);
	dev_set_drvdata(&peripheral->dev, NULL);

	return 0;
}

static const struct sdw_device_id tas_sdw_id[] = {
	/* chipid for the TAS2783 is 0x0000 */
	SDW_SLAVE_ENTRY(0x0102, 0x0000, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, tas_sdw_id);

static struct sdw_driver tas_sdw_driver = {
	.driver = {
		.name = "slave-tas2783",
		.pm = pm_ptr(&tas2783_sdca_pm),
	},
	.probe = tas_sdw_probe,
	.remove = tas_sdw_remove,
	.ops = &tas_sdw_ops,
	.id_table = tas_sdw_id,
};
module_sdw_driver(tas_sdw_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("ASoC TAS2783 SoundWire Driver");
MODULE_LICENSE("GPL");
