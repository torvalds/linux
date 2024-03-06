// SPDX-License-Identifier: GPL-2.0-only
//
// aw87390.h  --  aw87390 ALSA SoC Audio driver
//
// Copyright (c) 2023 awinic Technology CO., LTD
//
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#ifndef __AW87390_H__
#define __AW87390_H__

#define AW87390_ID_REG			(0x00)
#define AW87390_SYSCTRL_REG		(0x01)
#define AW87390_MDCTRL_REG		(0x02)
#define AW87390_CPOVP_REG		(0x03)
#define AW87390_CPP_REG		(0x04)
#define AW87390_PAG_REG		(0x05)
#define AW87390_AGC3P_REG		(0x06)
#define AW87390_AGC3PA_REG		(0x07)
#define AW87390_AGC2P_REG		(0x08)
#define AW87390_AGC2PA_REG		(0x09)
#define AW87390_AGC1PA_REG		(0x0A)
#define AW87390_SYSST_REG		(0x59)
#define AW87390_SYSINT_REG		(0x60)
#define AW87390_DFT_SYSCTRL_REG	(0x61)
#define AW87390_DFT_MDCTRL_REG		(0x62)
#define AW87390_DFT_CPADP_REG		(0x63)
#define AW87390_DFT_AGCPA_REG		(0x64)
#define AW87390_DFT_POFR_REG		(0x65)
#define AW87390_DFT_OC_REG		(0x66)
#define AW87390_DFT_ADP1_REG		(0x67)
#define AW87390_DFT_REF_REG		(0x68)
#define AW87390_DFT_LDO_REG		(0x69)
#define AW87390_ADP1_REG		(0x70)
#define AW87390_ADP2_REG		(0x71)
#define AW87390_NG1_REG		(0x72)
#define AW87390_NG2_REG		(0x73)
#define AW87390_NG3_REG		(0x74)
#define AW87390_CP_REG			(0x75)
#define AW87390_AB_REG			(0x76)
#define AW87390_TEST_REG		(0x77)
#define AW87390_ENCR_REG		(0x78)
#define AW87390_DELAY_REG_ADDR		(0xFE)

#define AW87390_SOFT_RESET_VALUE	(0xAA)
#define AW87390_POWER_DOWN_VALUE	(0x00)
#define AW87390_REG_MAX		(0xFF)
#define AW87390_DEV_DEFAULT_CH		(0)
#define AW87390_INIT_PROFILE		(0)
#define AW87390_REG_DELAY_TIME		(1000)
#define AW87390_I2C_NAME		"aw87390"
#define AW87390_ACF_FILE		"aw87390_acf.bin"

#define AW87390_PROFILE_EXT(xname, profile_info, profile_get, profile_set) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.info = profile_info, \
	.get = profile_get, \
	.put = profile_set, \
}

enum aw87390_id {
	AW87390_CHIP_ID = 0x76,
};

enum {
	AW87390_DEV_FW_FAILED = 0,
	AW87390_DEV_FW_OK,
};

enum {
	AW87390_DEV_PW_OFF = 0,
	AW87390_DEV_PW_ON,
};

struct aw87390 {
	struct aw_device *aw_pa;
	struct mutex lock;
	struct regmap *regmap;
	struct aw_container *aw_cfg;
};

#endif
