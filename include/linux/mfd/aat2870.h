/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/include/linux/mfd/aat2870.h
 *
 * Copyright (c) 2011, NVIDIA Corporation.
 * Author: Jin Park <jinyoungp@nvidia.com>
 */

#ifndef __LINUX_MFD_AAT2870_H
#define __LINUX_MFD_AAT2870_H

#include <linux/debugfs.h>
#include <linux/i2c.h>

/* Register offsets */
#define AAT2870_BL_CH_EN	0x00
#define AAT2870_BLM		0x01
#define AAT2870_BLS		0x02
#define AAT2870_BL1		0x03
#define AAT2870_BL2		0x04
#define AAT2870_BL3		0x05
#define AAT2870_BL4		0x06
#define AAT2870_BL5		0x07
#define AAT2870_BL6		0x08
#define AAT2870_BL7		0x09
#define AAT2870_BL8		0x0A
#define AAT2870_FLR		0x0B
#define AAT2870_FM		0x0C
#define AAT2870_FS		0x0D
#define AAT2870_ALS_CFG0	0x0E
#define AAT2870_ALS_CFG1	0x0F
#define AAT2870_ALS_CFG2	0x10
#define AAT2870_AMB		0x11
#define AAT2870_ALS0		0x12
#define AAT2870_ALS1		0x13
#define AAT2870_ALS2		0x14
#define AAT2870_ALS3		0x15
#define AAT2870_ALS4		0x16
#define AAT2870_ALS5		0x17
#define AAT2870_ALS6		0x18
#define AAT2870_ALS7		0x19
#define AAT2870_ALS8		0x1A
#define AAT2870_ALS9		0x1B
#define AAT2870_ALSA		0x1C
#define AAT2870_ALSB		0x1D
#define AAT2870_ALSC		0x1E
#define AAT2870_ALSD		0x1F
#define AAT2870_ALSE		0x20
#define AAT2870_ALSF		0x21
#define AAT2870_SUB_SET		0x22
#define AAT2870_SUB_CTRL	0x23
#define AAT2870_LDO_AB		0x24
#define AAT2870_LDO_CD		0x25
#define AAT2870_LDO_EN		0x26
#define AAT2870_REG_NUM		0x27

/* Device IDs */
enum aat2870_id {
	AAT2870_ID_BL,
	AAT2870_ID_LDOA,
	AAT2870_ID_LDOB,
	AAT2870_ID_LDOC,
	AAT2870_ID_LDOD
};

/* Backlight channels */
#define AAT2870_BL_CH1		0x01
#define AAT2870_BL_CH2		0x02
#define AAT2870_BL_CH3		0x04
#define AAT2870_BL_CH4		0x08
#define AAT2870_BL_CH5		0x10
#define AAT2870_BL_CH6		0x20
#define AAT2870_BL_CH7		0x40
#define AAT2870_BL_CH8		0x80
#define AAT2870_BL_CH_ALL	0xFF

/* Backlight current magnitude (mA) */
enum aat2870_current {
	AAT2870_CURRENT_0_45 = 1,
	AAT2870_CURRENT_0_90,
	AAT2870_CURRENT_1_80,
	AAT2870_CURRENT_2_70,
	AAT2870_CURRENT_3_60,
	AAT2870_CURRENT_4_50,
	AAT2870_CURRENT_5_40,
	AAT2870_CURRENT_6_30,
	AAT2870_CURRENT_7_20,
	AAT2870_CURRENT_8_10,
	AAT2870_CURRENT_9_00,
	AAT2870_CURRENT_9_90,
	AAT2870_CURRENT_10_8,
	AAT2870_CURRENT_11_7,
	AAT2870_CURRENT_12_6,
	AAT2870_CURRENT_13_5,
	AAT2870_CURRENT_14_4,
	AAT2870_CURRENT_15_3,
	AAT2870_CURRENT_16_2,
	AAT2870_CURRENT_17_1,
	AAT2870_CURRENT_18_0,
	AAT2870_CURRENT_18_9,
	AAT2870_CURRENT_19_8,
	AAT2870_CURRENT_20_7,
	AAT2870_CURRENT_21_6,
	AAT2870_CURRENT_22_5,
	AAT2870_CURRENT_23_4,
	AAT2870_CURRENT_24_3,
	AAT2870_CURRENT_25_2,
	AAT2870_CURRENT_26_1,
	AAT2870_CURRENT_27_0,
	AAT2870_CURRENT_27_9
};

struct aat2870_register {
	bool readable;
	bool writeable;
	u8 value;
};

struct aat2870_data {
	struct device *dev;
	struct i2c_client *client;

	struct mutex io_lock;
	struct aat2870_register *reg_cache; /* register cache */
	int en_pin; /* enable GPIO pin (if < 0, ignore this value) */
	bool is_enable;

	/* init and uninit for platform specified */
	int (*init)(struct aat2870_data *aat2870);
	void (*uninit)(struct aat2870_data *aat2870);

	/* i2c io funcntions */
	int (*read)(struct aat2870_data *aat2870, u8 addr, u8 *val);
	int (*write)(struct aat2870_data *aat2870, u8 addr, u8 val);
	int (*update)(struct aat2870_data *aat2870, u8 addr, u8 mask, u8 val);

	/* for debugfs */
	struct dentry *dentry_root;
};

struct aat2870_subdev_info {
	int id;
	const char *name;
	void *platform_data;
};

struct aat2870_platform_data {
	int en_pin; /* enable GPIO pin (if < 0, ignore this value) */

	struct aat2870_subdev_info *subdevs;
	int num_subdevs;

	/* init and uninit for platform specified */
	int (*init)(struct aat2870_data *aat2870);
	void (*uninit)(struct aat2870_data *aat2870);
};

struct aat2870_bl_platform_data {
	/* backlight channels, default is AAT2870_BL_CH_ALL */
	int channels;
	/* backlight current magnitude, default is AAT2870_CURRENT_27_9 */
	int max_current;
	/* maximum brightness, default is 255 */
	int max_brightness;
};

#endif /* __LINUX_MFD_AAT2870_H */
