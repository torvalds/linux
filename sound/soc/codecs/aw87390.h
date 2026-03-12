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

#define AW87391_SYSCTRL_REG		(0x01)
#define AW87391_REG_VER_SEL_LOW		(0 << 6)
#define AW87391_REG_VER_SEL_NORMAL	(1 << 6)
#define AW87391_REG_VER_SEL_SUPER	(2 << 6)
#define AW87391_REG_EN_ADAP		BIT(5)
#define AW87391_REG_EN_2X		BIT(4)
#define AW87391_EN_SPK			BIT(3)
#define AW87391_EN_PA			BIT(2)
#define AW87391_REG_EN_CP		BIT(1)
#define AW87391_EN_SW			BIT(0)

#define AW87391_CP_REG                  (0x02)
#define AW87391_REG_CP_OVP_6_50V	0
#define AW87391_REG_CP_OVP_6_75V	1
#define AW87391_REG_CP_OVP_7_00V	2
#define AW87391_REG_CP_OVP_7_25V	3
#define AW87391_REG_CP_OVP_7_50V	4
#define AW87391_REG_CP_OVP_7_75V	5
#define AW87391_REG_CP_OVP_8_00V	6
#define AW87391_REG_CP_OVP_8_25V	7
#define AW87391_REG_CP_OVP_8_50V	8

#define AW87391_PAG_REG                 (0x03)
#define AW87391_GAIN_12DB		0
#define AW87391_GAIN_15DB		1
#define AW87391_GAIN_18DB		2
#define AW87391_GAIN_21DB		3
#define AW87391_GAIN_24DB		4

#define AW87391_AGCPO_REG               (0x04)
#define AW87391_AK1_S_016		(2 << 5)
#define AW87391_AK1_S_032		(3 << 5)
#define AW87391_PD_AGC1_PWRDN		BIT(4)
/* AGC2PO supports values between 500mW (0000) to 1600mW (1011) */
#define AW87391_AGC2PO_MW(n)		((n / 100) - 5)

#define AW87391_AGC2PA_REG              (0x05)
#define AW87391_RK_S_5_12		(0 << 5)
#define AW87391_RK_S_10_24		(1 << 5)
#define AW87391_RK_S_20_48		(2 << 5)
#define AW87391_RK_S_41			(3 << 5)
#define AW87391_RK_S_82			(4 << 5)
#define AW87391_RK_S_164		(5 << 5)
#define AW87391_RK_S_328		(6 << 5)
#define AW87391_RK_S_656		(7 << 5)
#define AW87391_AK2_S_1_28		(0 << 2)
#define AW87391_AK2_S_2_56		(1 << 2)
#define AW87391_AK2_S_10_24		(2 << 2)
#define AW87391_AK2_S_41		(3 << 2)
#define AW87391_AK2_S_82		(4 << 2)
#define AW87391_AK2_S_164		(5 << 2)
#define AW87391_AK2_S_328		(6 << 2)
#define AW87391_AK2_S_656		(7 << 2)
#define AW87391_AK2F_S_10_24		0
#define AW87391_AK2F_S_20_48		1
#define AW87391_AK2F_S_41		2
#define AW87391_AK2F_S_82		3

#define AW87391_SYSST_REG               (0x06)
#define AW87391_UVLO			BIT(7)
#define AW87391_OTN			BIT(6)
#define AW87391_OC_FLAG			BIT(5)
#define AW87391_ADAP_CP			BIT(4)
#define AW87391_STARTOK			BIT(3)
#define AW87391_CP_OVP			BIT(2)
#define AW87391_PORN			BIT(1)

#define AW87391_SYSINT_REG              (0x07)
#define AW87391_UVLOI			BIT(7)
#define AW87391_ONTI			BIT(6)
#define AW87391_OC_FLAGI		BIT(5)
#define AW87391_ADAP_CPI		BIT(4)
#define AW87391_STARTOKI		BIT(3)
#define AW87391_CP_OVPI			BIT(2)
#define AW87391_PORNI			BIT(1)

#define AW87391_DFT_THGEN0_REG          (0x63)
#define AW87391_ADAPVTH_01W		(0 << 2)
#define AW87391_ADAPVTH_02W		(1 << 2)
#define AW87391_ADAPVTH_03W		(2 << 2)
#define AW87391_ADAPVTH_04W		(3 << 2)

#define AW87391_I2C_NAME                "aw87391"

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
	AW87391_CHIP_ID = 0xc1,
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
	struct regulator *vdd_reg;
};

#endif
