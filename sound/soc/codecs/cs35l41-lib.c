// SPDX-License-Identifier: GPL-2.0
//
// cs35l41-lib.c -- CS35L41 Common functions for HDA and ASoC Audio drivers
//
// Copyright 2017-2021 Cirrus Logic, Inc.
//
// Author: David Rhodes <david.rhodes@cirrus.com>
// Author: Lucas Tanure <lucas.tanure@cirrus.com>

#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/firmware/cirrus/wmfw.h>

#include <sound/cs35l41.h>

static const struct reg_default cs35l41_reg[] = {
	{ CS35L41_PWR_CTRL1,			0x00000000 },
	{ CS35L41_PWR_CTRL2,			0x00000000 },
	{ CS35L41_PWR_CTRL3,			0x01000010 },
	{ CS35L41_GPIO_PAD_CONTROL,		0x00000000 },
	{ CS35L41_GLOBAL_CLK_CTRL,		0x00000003 },
	{ CS35L41_TST_FS_MON0,			0x00020016 },
	{ CS35L41_BSTCVRT_COEFF,		0x00002424 },
	{ CS35L41_BSTCVRT_SLOPE_LBST,		0x00007500 },
	{ CS35L41_BSTCVRT_PEAK_CUR,		0x0000004A },
	{ CS35L41_SP_ENABLES,			0x00000000 },
	{ CS35L41_SP_RATE_CTRL,			0x00000028 },
	{ CS35L41_SP_FORMAT,			0x18180200 },
	{ CS35L41_SP_HIZ_CTRL,			0x00000002 },
	{ CS35L41_SP_FRAME_TX_SLOT,		0x03020100 },
	{ CS35L41_SP_FRAME_RX_SLOT,		0x00000100 },
	{ CS35L41_SP_TX_WL,			0x00000018 },
	{ CS35L41_SP_RX_WL,			0x00000018 },
	{ CS35L41_DAC_PCM1_SRC,			0x00000008 },
	{ CS35L41_ASP_TX1_SRC,			0x00000018 },
	{ CS35L41_ASP_TX2_SRC,			0x00000019 },
	{ CS35L41_ASP_TX3_SRC,			0x00000020 },
	{ CS35L41_ASP_TX4_SRC,			0x00000021 },
	{ CS35L41_DSP1_RX1_SRC,			0x00000008 },
	{ CS35L41_DSP1_RX2_SRC,			0x00000009 },
	{ CS35L41_DSP1_RX3_SRC,			0x00000018 },
	{ CS35L41_DSP1_RX4_SRC,			0x00000019 },
	{ CS35L41_DSP1_RX5_SRC,			0x00000020 },
	{ CS35L41_DSP1_RX6_SRC,			0x00000021 },
	{ CS35L41_DSP1_RX7_SRC,			0x0000003A },
	{ CS35L41_DSP1_RX8_SRC,			0x00000001 },
	{ CS35L41_NGATE1_SRC,			0x00000008 },
	{ CS35L41_NGATE2_SRC,			0x00000009 },
	{ CS35L41_AMP_DIG_VOL_CTRL,		0x00008000 },
	{ CS35L41_CLASSH_CFG,			0x000B0405 },
	{ CS35L41_WKFET_CFG,			0x00000111 },
	{ CS35L41_NG_CFG,			0x00000033 },
	{ CS35L41_AMP_GAIN_CTRL,		0x00000000 },
	{ CS35L41_IRQ1_MASK1,			0xFFFFFFFF },
	{ CS35L41_IRQ1_MASK2,			0xFFFFFFFF },
	{ CS35L41_IRQ1_MASK3,			0xFFFF87FF },
	{ CS35L41_IRQ1_MASK4,			0xFEFFFFFF },
	{ CS35L41_GPIO1_CTRL1,			0xE1000001 },
	{ CS35L41_GPIO2_CTRL1,			0xE1000001 },
	{ CS35L41_MIXER_NGATE_CFG,		0x00000000 },
	{ CS35L41_MIXER_NGATE_CH1_CFG,		0x00000303 },
	{ CS35L41_MIXER_NGATE_CH2_CFG,		0x00000303 },
	{ CS35L41_DSP1_CCM_CORE_CTRL,		0x00000101 },
};

static bool cs35l41_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L41_DEVID:
	case CS35L41_REVID:
	case CS35L41_FABID:
	case CS35L41_RELID:
	case CS35L41_OTPID:
	case CS35L41_TEST_KEY_CTL:
	case CS35L41_USER_KEY_CTL:
	case CS35L41_OTP_CTRL0:
	case CS35L41_OTP_CTRL3:
	case CS35L41_OTP_CTRL4:
	case CS35L41_OTP_CTRL5:
	case CS35L41_OTP_CTRL6:
	case CS35L41_OTP_CTRL7:
	case CS35L41_OTP_CTRL8:
	case CS35L41_PWR_CTRL1:
	case CS35L41_PWR_CTRL2:
	case CS35L41_PWR_CTRL3:
	case CS35L41_CTRL_OVRRIDE:
	case CS35L41_AMP_OUT_MUTE:
	case CS35L41_PROTECT_REL_ERR_IGN:
	case CS35L41_GPIO_PAD_CONTROL:
	case CS35L41_JTAG_CONTROL:
	case CS35L41_PWRMGT_CTL:
	case CS35L41_WAKESRC_CTL:
	case CS35L41_PWRMGT_STS:
	case CS35L41_PLL_CLK_CTRL:
	case CS35L41_DSP_CLK_CTRL:
	case CS35L41_GLOBAL_CLK_CTRL:
	case CS35L41_DATA_FS_SEL:
	case CS35L41_TST_FS_MON0:
	case CS35L41_MDSYNC_EN:
	case CS35L41_MDSYNC_TX_ID:
	case CS35L41_MDSYNC_PWR_CTRL:
	case CS35L41_MDSYNC_DATA_TX:
	case CS35L41_MDSYNC_TX_STATUS:
	case CS35L41_MDSYNC_DATA_RX:
	case CS35L41_MDSYNC_RX_STATUS:
	case CS35L41_MDSYNC_ERR_STATUS:
	case CS35L41_MDSYNC_SYNC_PTE2:
	case CS35L41_MDSYNC_SYNC_PTE3:
	case CS35L41_MDSYNC_SYNC_MSM_STATUS:
	case CS35L41_BSTCVRT_VCTRL1:
	case CS35L41_BSTCVRT_VCTRL2:
	case CS35L41_BSTCVRT_PEAK_CUR:
	case CS35L41_BSTCVRT_SFT_RAMP:
	case CS35L41_BSTCVRT_COEFF:
	case CS35L41_BSTCVRT_SLOPE_LBST:
	case CS35L41_BSTCVRT_SW_FREQ:
	case CS35L41_BSTCVRT_DCM_CTRL:
	case CS35L41_BSTCVRT_DCM_MODE_FORCE:
	case CS35L41_BSTCVRT_OVERVOLT_CTRL:
	case CS35L41_VI_VOL_POL:
	case CS35L41_DTEMP_WARN_THLD:
	case CS35L41_DTEMP_CFG:
	case CS35L41_DTEMP_EN:
	case CS35L41_VPVBST_FS_SEL:
	case CS35L41_SP_ENABLES:
	case CS35L41_SP_RATE_CTRL:
	case CS35L41_SP_FORMAT:
	case CS35L41_SP_HIZ_CTRL:
	case CS35L41_SP_FRAME_TX_SLOT:
	case CS35L41_SP_FRAME_RX_SLOT:
	case CS35L41_SP_TX_WL:
	case CS35L41_SP_RX_WL:
	case CS35L41_DAC_PCM1_SRC:
	case CS35L41_ASP_TX1_SRC:
	case CS35L41_ASP_TX2_SRC:
	case CS35L41_ASP_TX3_SRC:
	case CS35L41_ASP_TX4_SRC:
	case CS35L41_DSP1_RX1_SRC:
	case CS35L41_DSP1_RX2_SRC:
	case CS35L41_DSP1_RX3_SRC:
	case CS35L41_DSP1_RX4_SRC:
	case CS35L41_DSP1_RX5_SRC:
	case CS35L41_DSP1_RX6_SRC:
	case CS35L41_DSP1_RX7_SRC:
	case CS35L41_DSP1_RX8_SRC:
	case CS35L41_NGATE1_SRC:
	case CS35L41_NGATE2_SRC:
	case CS35L41_AMP_DIG_VOL_CTRL:
	case CS35L41_VPBR_CFG:
	case CS35L41_VBBR_CFG:
	case CS35L41_VPBR_STATUS:
	case CS35L41_VBBR_STATUS:
	case CS35L41_OVERTEMP_CFG:
	case CS35L41_AMP_ERR_VOL:
	case CS35L41_VOL_STATUS_TO_DSP:
	case CS35L41_CLASSH_CFG:
	case CS35L41_WKFET_CFG:
	case CS35L41_NG_CFG:
	case CS35L41_AMP_GAIN_CTRL:
	case CS35L41_DAC_MSM_CFG:
	case CS35L41_IRQ1_CFG:
	case CS35L41_IRQ1_STATUS:
	case CS35L41_IRQ1_STATUS1:
	case CS35L41_IRQ1_STATUS2:
	case CS35L41_IRQ1_STATUS3:
	case CS35L41_IRQ1_STATUS4:
	case CS35L41_IRQ1_RAW_STATUS1:
	case CS35L41_IRQ1_RAW_STATUS2:
	case CS35L41_IRQ1_RAW_STATUS3:
	case CS35L41_IRQ1_RAW_STATUS4:
	case CS35L41_IRQ1_MASK1:
	case CS35L41_IRQ1_MASK2:
	case CS35L41_IRQ1_MASK3:
	case CS35L41_IRQ1_MASK4:
	case CS35L41_IRQ1_FRC1:
	case CS35L41_IRQ1_FRC2:
	case CS35L41_IRQ1_FRC3:
	case CS35L41_IRQ1_FRC4:
	case CS35L41_IRQ1_EDGE1:
	case CS35L41_IRQ1_EDGE4:
	case CS35L41_IRQ1_POL1:
	case CS35L41_IRQ1_POL2:
	case CS35L41_IRQ1_POL3:
	case CS35L41_IRQ1_POL4:
	case CS35L41_IRQ1_DB3:
	case CS35L41_IRQ2_CFG:
	case CS35L41_IRQ2_STATUS:
	case CS35L41_IRQ2_STATUS1:
	case CS35L41_IRQ2_STATUS2:
	case CS35L41_IRQ2_STATUS3:
	case CS35L41_IRQ2_STATUS4:
	case CS35L41_IRQ2_RAW_STATUS1:
	case CS35L41_IRQ2_RAW_STATUS2:
	case CS35L41_IRQ2_RAW_STATUS3:
	case CS35L41_IRQ2_RAW_STATUS4:
	case CS35L41_IRQ2_MASK1:
	case CS35L41_IRQ2_MASK2:
	case CS35L41_IRQ2_MASK3:
	case CS35L41_IRQ2_MASK4:
	case CS35L41_IRQ2_FRC1:
	case CS35L41_IRQ2_FRC2:
	case CS35L41_IRQ2_FRC3:
	case CS35L41_IRQ2_FRC4:
	case CS35L41_IRQ2_EDGE1:
	case CS35L41_IRQ2_EDGE4:
	case CS35L41_IRQ2_POL1:
	case CS35L41_IRQ2_POL2:
	case CS35L41_IRQ2_POL3:
	case CS35L41_IRQ2_POL4:
	case CS35L41_IRQ2_DB3:
	case CS35L41_GPIO_STATUS1:
	case CS35L41_GPIO1_CTRL1:
	case CS35L41_GPIO2_CTRL1:
	case CS35L41_MIXER_NGATE_CFG:
	case CS35L41_MIXER_NGATE_CH1_CFG:
	case CS35L41_MIXER_NGATE_CH2_CFG:
	case CS35L41_DSP_MBOX_1 ... CS35L41_DSP_VIRT2_MBOX_8:
	case CS35L41_CLOCK_DETECT_1:
	case CS35L41_DIE_STS1:
	case CS35L41_DIE_STS2:
	case CS35L41_TEMP_CAL1:
	case CS35L41_TEMP_CAL2:
	case CS35L41_DSP1_TIMESTAMP_COUNT:
	case CS35L41_DSP1_SYS_ID:
	case CS35L41_DSP1_SYS_VERSION:
	case CS35L41_DSP1_SYS_CORE_ID:
	case CS35L41_DSP1_SYS_AHB_ADDR:
	case CS35L41_DSP1_SYS_XSRAM_SIZE:
	case CS35L41_DSP1_SYS_YSRAM_SIZE:
	case CS35L41_DSP1_SYS_PSRAM_SIZE:
	case CS35L41_DSP1_SYS_PM_BOOT_SIZE:
	case CS35L41_DSP1_SYS_FEATURES:
	case CS35L41_DSP1_SYS_FIR_FILTERS:
	case CS35L41_DSP1_SYS_LMS_FILTERS:
	case CS35L41_DSP1_SYS_XM_BANK_SIZE:
	case CS35L41_DSP1_SYS_YM_BANK_SIZE:
	case CS35L41_DSP1_SYS_PM_BANK_SIZE:
	case CS35L41_DSP1_RX1_RATE:
	case CS35L41_DSP1_RX2_RATE:
	case CS35L41_DSP1_RX3_RATE:
	case CS35L41_DSP1_RX4_RATE:
	case CS35L41_DSP1_RX5_RATE:
	case CS35L41_DSP1_RX6_RATE:
	case CS35L41_DSP1_RX7_RATE:
	case CS35L41_DSP1_RX8_RATE:
	case CS35L41_DSP1_TX1_RATE:
	case CS35L41_DSP1_TX2_RATE:
	case CS35L41_DSP1_TX3_RATE:
	case CS35L41_DSP1_TX4_RATE:
	case CS35L41_DSP1_TX5_RATE:
	case CS35L41_DSP1_TX6_RATE:
	case CS35L41_DSP1_TX7_RATE:
	case CS35L41_DSP1_TX8_RATE:
	case CS35L41_DSP1_SCRATCH1:
	case CS35L41_DSP1_SCRATCH2:
	case CS35L41_DSP1_SCRATCH3:
	case CS35L41_DSP1_SCRATCH4:
	case CS35L41_DSP1_CCM_CORE_CTRL:
	case CS35L41_DSP1_CCM_CLK_OVERRIDE:
	case CS35L41_DSP1_XM_MSTR_EN:
	case CS35L41_DSP1_XM_CORE_PRI:
	case CS35L41_DSP1_XM_AHB_PACK_PL_PRI:
	case CS35L41_DSP1_XM_AHB_UP_PL_PRI:
	case CS35L41_DSP1_XM_ACCEL_PL0_PRI:
	case CS35L41_DSP1_XM_NPL0_PRI:
	case CS35L41_DSP1_YM_MSTR_EN:
	case CS35L41_DSP1_YM_CORE_PRI:
	case CS35L41_DSP1_YM_AHB_PACK_PL_PRI:
	case CS35L41_DSP1_YM_AHB_UP_PL_PRI:
	case CS35L41_DSP1_YM_ACCEL_PL0_PRI:
	case CS35L41_DSP1_YM_NPL0_PRI:
	case CS35L41_DSP1_MPU_XM_ACCESS0:
	case CS35L41_DSP1_MPU_YM_ACCESS0:
	case CS35L41_DSP1_MPU_WNDW_ACCESS0:
	case CS35L41_DSP1_MPU_XREG_ACCESS0:
	case CS35L41_DSP1_MPU_YREG_ACCESS0:
	case CS35L41_DSP1_MPU_XM_ACCESS1:
	case CS35L41_DSP1_MPU_YM_ACCESS1:
	case CS35L41_DSP1_MPU_WNDW_ACCESS1:
	case CS35L41_DSP1_MPU_XREG_ACCESS1:
	case CS35L41_DSP1_MPU_YREG_ACCESS1:
	case CS35L41_DSP1_MPU_XM_ACCESS2:
	case CS35L41_DSP1_MPU_YM_ACCESS2:
	case CS35L41_DSP1_MPU_WNDW_ACCESS2:
	case CS35L41_DSP1_MPU_XREG_ACCESS2:
	case CS35L41_DSP1_MPU_YREG_ACCESS2:
	case CS35L41_DSP1_MPU_XM_ACCESS3:
	case CS35L41_DSP1_MPU_YM_ACCESS3:
	case CS35L41_DSP1_MPU_WNDW_ACCESS3:
	case CS35L41_DSP1_MPU_XREG_ACCESS3:
	case CS35L41_DSP1_MPU_YREG_ACCESS3:
	case CS35L41_DSP1_MPU_XM_VIO_ADDR:
	case CS35L41_DSP1_MPU_XM_VIO_STATUS:
	case CS35L41_DSP1_MPU_YM_VIO_ADDR:
	case CS35L41_DSP1_MPU_YM_VIO_STATUS:
	case CS35L41_DSP1_MPU_PM_VIO_ADDR:
	case CS35L41_DSP1_MPU_PM_VIO_STATUS:
	case CS35L41_DSP1_MPU_LOCK_CONFIG:
	case CS35L41_DSP1_MPU_WDT_RST_CTRL:
	case CS35L41_OTP_TRIM_1:
	case CS35L41_OTP_TRIM_2:
	case CS35L41_OTP_TRIM_3:
	case CS35L41_OTP_TRIM_4:
	case CS35L41_OTP_TRIM_5:
	case CS35L41_OTP_TRIM_6:
	case CS35L41_OTP_TRIM_7:
	case CS35L41_OTP_TRIM_8:
	case CS35L41_OTP_TRIM_9:
	case CS35L41_OTP_TRIM_10:
	case CS35L41_OTP_TRIM_11:
	case CS35L41_OTP_TRIM_12:
	case CS35L41_OTP_TRIM_13:
	case CS35L41_OTP_TRIM_14:
	case CS35L41_OTP_TRIM_15:
	case CS35L41_OTP_TRIM_16:
	case CS35L41_OTP_TRIM_17:
	case CS35L41_OTP_TRIM_18:
	case CS35L41_OTP_TRIM_19:
	case CS35L41_OTP_TRIM_20:
	case CS35L41_OTP_TRIM_21:
	case CS35L41_OTP_TRIM_22:
	case CS35L41_OTP_TRIM_23:
	case CS35L41_OTP_TRIM_24:
	case CS35L41_OTP_TRIM_25:
	case CS35L41_OTP_TRIM_26:
	case CS35L41_OTP_TRIM_27:
	case CS35L41_OTP_TRIM_28:
	case CS35L41_OTP_TRIM_29:
	case CS35L41_OTP_TRIM_30:
	case CS35L41_OTP_TRIM_31:
	case CS35L41_OTP_TRIM_32:
	case CS35L41_OTP_TRIM_33:
	case CS35L41_OTP_TRIM_34:
	case CS35L41_OTP_TRIM_35:
	case CS35L41_OTP_TRIM_36:
	case CS35L41_OTP_MEM0 ... CS35L41_OTP_MEM31:
	case CS35L41_DSP1_XMEM_PACK_0 ... CS35L41_DSP1_XMEM_PACK_3068:
	case CS35L41_DSP1_XMEM_UNPACK32_0 ... CS35L41_DSP1_XMEM_UNPACK32_2046:
	case CS35L41_DSP1_XMEM_UNPACK24_0 ... CS35L41_DSP1_XMEM_UNPACK24_4093:
	case CS35L41_DSP1_YMEM_PACK_0 ... CS35L41_DSP1_YMEM_PACK_1532:
	case CS35L41_DSP1_YMEM_UNPACK32_0 ... CS35L41_DSP1_YMEM_UNPACK32_1022:
	case CS35L41_DSP1_YMEM_UNPACK24_0 ... CS35L41_DSP1_YMEM_UNPACK24_2045:
	case CS35L41_DSP1_PMEM_0 ... CS35L41_DSP1_PMEM_5114:
	/*test regs*/
	case CS35L41_PLL_OVR:
	case CS35L41_BST_TEST_DUTY:
	case CS35L41_DIGPWM_IOCTRL:
		return true;
	default:
		return false;
	}
}

static bool cs35l41_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L41_TEST_KEY_CTL:
	case CS35L41_USER_KEY_CTL:
	case CS35L41_OTP_MEM0 ... CS35L41_OTP_MEM31:
	case CS35L41_TST_FS_MON0:
	case CS35L41_DSP1_XMEM_PACK_0 ... CS35L41_DSP1_XMEM_PACK_3068:
	case CS35L41_DSP1_YMEM_PACK_0 ... CS35L41_DSP1_YMEM_PACK_1532:
	case CS35L41_DSP1_PMEM_0 ... CS35L41_DSP1_PMEM_5114:
		return true;
	default:
		return false;
	}
}

static bool cs35l41_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L41_DEVID:
	case CS35L41_SFT_RESET:
	case CS35L41_FABID:
	case CS35L41_REVID:
	case CS35L41_OTPID:
	case CS35L41_TEST_KEY_CTL:
	case CS35L41_USER_KEY_CTL:
	case CS35L41_PWRMGT_CTL:
	case CS35L41_WAKESRC_CTL:
	case CS35L41_PWRMGT_STS:
	case CS35L41_DTEMP_EN:
	case CS35L41_IRQ1_STATUS:
	case CS35L41_IRQ1_STATUS1:
	case CS35L41_IRQ1_STATUS2:
	case CS35L41_IRQ1_STATUS3:
	case CS35L41_IRQ1_STATUS4:
	case CS35L41_IRQ1_RAW_STATUS1:
	case CS35L41_IRQ1_RAW_STATUS2:
	case CS35L41_IRQ1_RAW_STATUS3:
	case CS35L41_IRQ1_RAW_STATUS4:
	case CS35L41_IRQ2_STATUS:
	case CS35L41_IRQ2_STATUS1:
	case CS35L41_IRQ2_STATUS2:
	case CS35L41_IRQ2_STATUS3:
	case CS35L41_IRQ2_STATUS4:
	case CS35L41_IRQ2_RAW_STATUS1:
	case CS35L41_IRQ2_RAW_STATUS2:
	case CS35L41_IRQ2_RAW_STATUS3:
	case CS35L41_IRQ2_RAW_STATUS4:
	case CS35L41_GPIO_STATUS1:
	case CS35L41_DSP_MBOX_1 ... CS35L41_DSP_VIRT2_MBOX_8:
	case CS35L41_DSP1_XMEM_PACK_0 ... CS35L41_DSP1_XMEM_PACK_3068:
	case CS35L41_DSP1_XMEM_UNPACK32_0 ... CS35L41_DSP1_XMEM_UNPACK32_2046:
	case CS35L41_DSP1_XMEM_UNPACK24_0 ... CS35L41_DSP1_XMEM_UNPACK24_4093:
	case CS35L41_DSP1_YMEM_PACK_0 ... CS35L41_DSP1_YMEM_PACK_1532:
	case CS35L41_DSP1_YMEM_UNPACK32_0 ... CS35L41_DSP1_YMEM_UNPACK32_1022:
	case CS35L41_DSP1_YMEM_UNPACK24_0 ... CS35L41_DSP1_YMEM_UNPACK24_2045:
	case CS35L41_DSP1_PMEM_0 ... CS35L41_DSP1_PMEM_5114:
	case CS35L41_DSP1_SCRATCH1:
	case CS35L41_DSP1_SCRATCH2:
	case CS35L41_DSP1_SCRATCH3:
	case CS35L41_DSP1_SCRATCH4:
	case CS35L41_DSP1_CCM_CLK_OVERRIDE ... CS35L41_DSP1_WDT_STATUS:
	case CS35L41_OTP_MEM0 ... CS35L41_OTP_MEM31:
		return true;
	default:
		return false;
	}
}

static const struct cs35l41_otp_packed_element_t otp_map_1[] = {
	/* addr         shift   size */
	{ 0x00002030,	0,	4 }, /*TRIM_OSC_FREQ_TRIM*/
	{ 0x00002030,	7,	1 }, /*TRIM_OSC_TRIM_DONE*/
	{ 0x0000208c,	24,	6 }, /*TST_DIGREG_VREF_TRIM*/
	{ 0x00002090,	14,	4 }, /*TST_REF_TRIM*/
	{ 0x00002090,	10,	4 }, /*TST_REF_TEMPCO_TRIM*/
	{ 0x0000300C,	11,	4 }, /*PLL_LDOA_TST_VREF_TRIM*/
	{ 0x0000394C,	23,	2 }, /*BST_ATEST_CM_VOFF*/
	{ 0x00003950,	0,	7 }, /*BST_ATRIM_IADC_OFFSET*/
	{ 0x00003950,	8,	7 }, /*BST_ATRIM_IADC_GAIN1*/
	{ 0x00003950,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET1*/
	{ 0x00003950,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN1*/
	{ 0x00003954,	0,	7 }, /*BST_ATRIM_IADC_OFFSET2*/
	{ 0x00003954,	8,	7 }, /*BST_ATRIM_IADC_GAIN2*/
	{ 0x00003954,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET2*/
	{ 0x00003954,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN2*/
	{ 0x00003958,	0,	7 }, /*BST_ATRIM_IADC_OFFSET3*/
	{ 0x00003958,	8,	7 }, /*BST_ATRIM_IADC_GAIN3*/
	{ 0x00003958,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET3*/
	{ 0x00003958,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN3*/
	{ 0x0000395C,	0,	7 }, /*BST_ATRIM_IADC_OFFSET4*/
	{ 0x0000395C,	8,	7 }, /*BST_ATRIM_IADC_GAIN4*/
	{ 0x0000395C,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET4*/
	{ 0x0000395C,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN4*/
	{ 0x0000416C,	0,	8 }, /*VMON_GAIN_OTP_VAL*/
	{ 0x00004160,	0,	7 }, /*VMON_OFFSET_OTP_VAL*/
	{ 0x0000416C,	8,	8 }, /*IMON_GAIN_OTP_VAL*/
	{ 0x00004160,	16,	10 }, /*IMON_OFFSET_OTP_VAL*/
	{ 0x0000416C,	16,	12 }, /*VMON_CM_GAIN_OTP_VAL*/
	{ 0x0000416C,	28,	1 }, /*VMON_CM_GAIN_SIGN_OTP_VAL*/
	{ 0x00004170,	0,	6 }, /*IMON_CAL_TEMPCO_OTP_VAL*/
	{ 0x00004170,	6,	1 }, /*IMON_CAL_TEMPCO_SIGN_OTP*/
	{ 0x00004170,	8,	6 }, /*IMON_CAL_TEMPCO2_OTP_VAL*/
	{ 0x00004170,	14,	1 }, /*IMON_CAL_TEMPCO2_DN_UPB_OTP_VAL*/
	{ 0x00004170,	16,	9 }, /*IMON_CAL_TEMPCO_TBASE_OTP_VAL*/
	{ 0x00004360,	0,	5 }, /*TEMP_GAIN_OTP_VAL*/
	{ 0x00004360,	6,	9 }, /*TEMP_OFFSET_OTP_VAL*/
	{ 0x00004448,	0,	8 }, /*VP_SARADC_OFFSET*/
	{ 0x00004448,	8,	8 }, /*VP_GAIN_INDEX*/
	{ 0x00004448,	16,	8 }, /*VBST_SARADC_OFFSET*/
	{ 0x00004448,	24,	8 }, /*VBST_GAIN_INDEX*/
	{ 0x0000444C,	0,	3 }, /*ANA_SELINVREF*/
	{ 0x00006E30,	0,	5 }, /*GAIN_ERR_COEFF_0*/
	{ 0x00006E30,	8,	5 }, /*GAIN_ERR_COEFF_1*/
	{ 0x00006E30,	16,	5 }, /*GAIN_ERR_COEFF_2*/
	{ 0x00006E30,	24,	5 }, /*GAIN_ERR_COEFF_3*/
	{ 0x00006E34,	0,	5 }, /*GAIN_ERR_COEFF_4*/
	{ 0x00006E34,	8,	5 }, /*GAIN_ERR_COEFF_5*/
	{ 0x00006E34,	16,	5 }, /*GAIN_ERR_COEFF_6*/
	{ 0x00006E34,	24,	5 }, /*GAIN_ERR_COEFF_7*/
	{ 0x00006E38,	0,	5 }, /*GAIN_ERR_COEFF_8*/
	{ 0x00006E38,	8,	5 }, /*GAIN_ERR_COEFF_9*/
	{ 0x00006E38,	16,	5 }, /*GAIN_ERR_COEFF_10*/
	{ 0x00006E38,	24,	5 }, /*GAIN_ERR_COEFF_11*/
	{ 0x00006E3C,	0,	5 }, /*GAIN_ERR_COEFF_12*/
	{ 0x00006E3C,	8,	5 }, /*GAIN_ERR_COEFF_13*/
	{ 0x00006E3C,	16,	5 }, /*GAIN_ERR_COEFF_14*/
	{ 0x00006E3C,	24,	5 }, /*GAIN_ERR_COEFF_15*/
	{ 0x00006E40,	0,	5 }, /*GAIN_ERR_COEFF_16*/
	{ 0x00006E40,	8,	5 }, /*GAIN_ERR_COEFF_17*/
	{ 0x00006E40,	16,	5 }, /*GAIN_ERR_COEFF_18*/
	{ 0x00006E40,	24,	5 }, /*GAIN_ERR_COEFF_19*/
	{ 0x00006E44,	0,	5 }, /*GAIN_ERR_COEFF_20*/
	{ 0x00006E48,	0,	10 }, /*VOFF_GAIN_0*/
	{ 0x00006E48,	10,	10 }, /*VOFF_GAIN_1*/
	{ 0x00006E48,	20,	10 }, /*VOFF_GAIN_2*/
	{ 0x00006E4C,	0,	10 }, /*VOFF_GAIN_3*/
	{ 0x00006E4C,	10,	10 }, /*VOFF_GAIN_4*/
	{ 0x00006E4C,	20,	10 }, /*VOFF_GAIN_5*/
	{ 0x00006E50,	0,	10 }, /*VOFF_GAIN_6*/
	{ 0x00006E50,	10,	10 }, /*VOFF_GAIN_7*/
	{ 0x00006E50,	20,	10 }, /*VOFF_GAIN_8*/
	{ 0x00006E54,	0,	10 }, /*VOFF_GAIN_9*/
	{ 0x00006E54,	10,	10 }, /*VOFF_GAIN_10*/
	{ 0x00006E54,	20,	10 }, /*VOFF_GAIN_11*/
	{ 0x00006E58,	0,	10 }, /*VOFF_GAIN_12*/
	{ 0x00006E58,	10,	10 }, /*VOFF_GAIN_13*/
	{ 0x00006E58,	20,	10 }, /*VOFF_GAIN_14*/
	{ 0x00006E5C,	0,	10 }, /*VOFF_GAIN_15*/
	{ 0x00006E5C,	10,	10 }, /*VOFF_GAIN_16*/
	{ 0x00006E5C,	20,	10 }, /*VOFF_GAIN_17*/
	{ 0x00006E60,	0,	10 }, /*VOFF_GAIN_18*/
	{ 0x00006E60,	10,	10 }, /*VOFF_GAIN_19*/
	{ 0x00006E60,	20,	10 }, /*VOFF_GAIN_20*/
	{ 0x00006E64,	0,	10 }, /*VOFF_INT1*/
	{ 0x00007418,	7,	5 }, /*DS_SPK_INT1_CAP_TRIM*/
	{ 0x0000741C,	0,	5 }, /*DS_SPK_INT2_CAP_TRIM*/
	{ 0x0000741C,	11,	4 }, /*DS_SPK_LPF_CAP_TRIM*/
	{ 0x0000741C,	19,	4 }, /*DS_SPK_QUAN_CAP_TRIM*/
	{ 0x00007434,	17,	1 }, /*FORCE_CAL*/
	{ 0x00007434,	18,	7 }, /*CAL_OVERRIDE*/
	{ 0x00007068,	0,	9 }, /*MODIX*/
	{ 0x0000410C,	7,	1 }, /*VIMON_DLY_NOT_COMB*/
	{ 0x0000400C,	0,	7 }, /*VIMON_DLY*/
	{ 0x00000000,	0,	1 }, /*extra bit*/
	{ 0x00017040,	0,	8 }, /*X_COORDINATE*/
	{ 0x00017040,	8,	8 }, /*Y_COORDINATE*/
	{ 0x00017040,	16,	8 }, /*WAFER_ID*/
	{ 0x00017040,	24,	8 }, /*DVS*/
	{ 0x00017044,	0,	24 }, /*LOT_NUMBER*/
};

static const struct cs35l41_otp_packed_element_t otp_map_2[] = {
	/* addr         shift   size */
	{ 0x00002030,	0,	4 }, /*TRIM_OSC_FREQ_TRIM*/
	{ 0x00002030,	7,	1 }, /*TRIM_OSC_TRIM_DONE*/
	{ 0x0000208c,	24,	6 }, /*TST_DIGREG_VREF_TRIM*/
	{ 0x00002090,	14,	4 }, /*TST_REF_TRIM*/
	{ 0x00002090,	10,	4 }, /*TST_REF_TEMPCO_TRIM*/
	{ 0x0000300C,	11,	4 }, /*PLL_LDOA_TST_VREF_TRIM*/
	{ 0x0000394C,	23,	2 }, /*BST_ATEST_CM_VOFF*/
	{ 0x00003950,	0,	7 }, /*BST_ATRIM_IADC_OFFSET*/
	{ 0x00003950,	8,	7 }, /*BST_ATRIM_IADC_GAIN1*/
	{ 0x00003950,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET1*/
	{ 0x00003950,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN1*/
	{ 0x00003954,	0,	7 }, /*BST_ATRIM_IADC_OFFSET2*/
	{ 0x00003954,	8,	7 }, /*BST_ATRIM_IADC_GAIN2*/
	{ 0x00003954,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET2*/
	{ 0x00003954,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN2*/
	{ 0x00003958,	0,	7 }, /*BST_ATRIM_IADC_OFFSET3*/
	{ 0x00003958,	8,	7 }, /*BST_ATRIM_IADC_GAIN3*/
	{ 0x00003958,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET3*/
	{ 0x00003958,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN3*/
	{ 0x0000395C,	0,	7 }, /*BST_ATRIM_IADC_OFFSET4*/
	{ 0x0000395C,	8,	7 }, /*BST_ATRIM_IADC_GAIN4*/
	{ 0x0000395C,	16,	8 }, /*BST_ATRIM_IPKCOMP_OFFSET4*/
	{ 0x0000395C,	24,	8 }, /*BST_ATRIM_IPKCOMP_GAIN4*/
	{ 0x0000416C,	0,	8 }, /*VMON_GAIN_OTP_VAL*/
	{ 0x00004160,	0,	7 }, /*VMON_OFFSET_OTP_VAL*/
	{ 0x0000416C,	8,	8 }, /*IMON_GAIN_OTP_VAL*/
	{ 0x00004160,	16,	10 }, /*IMON_OFFSET_OTP_VAL*/
	{ 0x0000416C,	16,	12 }, /*VMON_CM_GAIN_OTP_VAL*/
	{ 0x0000416C,	28,	1 }, /*VMON_CM_GAIN_SIGN_OTP_VAL*/
	{ 0x00004170,	0,	6 }, /*IMON_CAL_TEMPCO_OTP_VAL*/
	{ 0x00004170,	6,	1 }, /*IMON_CAL_TEMPCO_SIGN_OTP*/
	{ 0x00004170,	8,	6 }, /*IMON_CAL_TEMPCO2_OTP_VAL*/
	{ 0x00004170,	14,	1 }, /*IMON_CAL_TEMPCO2_DN_UPB_OTP_VAL*/
	{ 0x00004170,	16,	9 }, /*IMON_CAL_TEMPCO_TBASE_OTP_VAL*/
	{ 0x00004360,	0,	5 }, /*TEMP_GAIN_OTP_VAL*/
	{ 0x00004360,	6,	9 }, /*TEMP_OFFSET_OTP_VAL*/
	{ 0x00004448,	0,	8 }, /*VP_SARADC_OFFSET*/
	{ 0x00004448,	8,	8 }, /*VP_GAIN_INDEX*/
	{ 0x00004448,	16,	8 }, /*VBST_SARADC_OFFSET*/
	{ 0x00004448,	24,	8 }, /*VBST_GAIN_INDEX*/
	{ 0x0000444C,	0,	3 }, /*ANA_SELINVREF*/
	{ 0x00006E30,	0,	5 }, /*GAIN_ERR_COEFF_0*/
	{ 0x00006E30,	8,	5 }, /*GAIN_ERR_COEFF_1*/
	{ 0x00006E30,	16,	5 }, /*GAIN_ERR_COEFF_2*/
	{ 0x00006E30,	24,	5 }, /*GAIN_ERR_COEFF_3*/
	{ 0x00006E34,	0,	5 }, /*GAIN_ERR_COEFF_4*/
	{ 0x00006E34,	8,	5 }, /*GAIN_ERR_COEFF_5*/
	{ 0x00006E34,	16,	5 }, /*GAIN_ERR_COEFF_6*/
	{ 0x00006E34,	24,	5 }, /*GAIN_ERR_COEFF_7*/
	{ 0x00006E38,	0,	5 }, /*GAIN_ERR_COEFF_8*/
	{ 0x00006E38,	8,	5 }, /*GAIN_ERR_COEFF_9*/
	{ 0x00006E38,	16,	5 }, /*GAIN_ERR_COEFF_10*/
	{ 0x00006E38,	24,	5 }, /*GAIN_ERR_COEFF_11*/
	{ 0x00006E3C,	0,	5 }, /*GAIN_ERR_COEFF_12*/
	{ 0x00006E3C,	8,	5 }, /*GAIN_ERR_COEFF_13*/
	{ 0x00006E3C,	16,	5 }, /*GAIN_ERR_COEFF_14*/
	{ 0x00006E3C,	24,	5 }, /*GAIN_ERR_COEFF_15*/
	{ 0x00006E40,	0,	5 }, /*GAIN_ERR_COEFF_16*/
	{ 0x00006E40,	8,	5 }, /*GAIN_ERR_COEFF_17*/
	{ 0x00006E40,	16,	5 }, /*GAIN_ERR_COEFF_18*/
	{ 0x00006E40,	24,	5 }, /*GAIN_ERR_COEFF_19*/
	{ 0x00006E44,	0,	5 }, /*GAIN_ERR_COEFF_20*/
	{ 0x00006E48,	0,	10 }, /*VOFF_GAIN_0*/
	{ 0x00006E48,	10,	10 }, /*VOFF_GAIN_1*/
	{ 0x00006E48,	20,	10 }, /*VOFF_GAIN_2*/
	{ 0x00006E4C,	0,	10 }, /*VOFF_GAIN_3*/
	{ 0x00006E4C,	10,	10 }, /*VOFF_GAIN_4*/
	{ 0x00006E4C,	20,	10 }, /*VOFF_GAIN_5*/
	{ 0x00006E50,	0,	10 }, /*VOFF_GAIN_6*/
	{ 0x00006E50,	10,	10 }, /*VOFF_GAIN_7*/
	{ 0x00006E50,	20,	10 }, /*VOFF_GAIN_8*/
	{ 0x00006E54,	0,	10 }, /*VOFF_GAIN_9*/
	{ 0x00006E54,	10,	10 }, /*VOFF_GAIN_10*/
	{ 0x00006E54,	20,	10 }, /*VOFF_GAIN_11*/
	{ 0x00006E58,	0,	10 }, /*VOFF_GAIN_12*/
	{ 0x00006E58,	10,	10 }, /*VOFF_GAIN_13*/
	{ 0x00006E58,	20,	10 }, /*VOFF_GAIN_14*/
	{ 0x00006E5C,	0,	10 }, /*VOFF_GAIN_15*/
	{ 0x00006E5C,	10,	10 }, /*VOFF_GAIN_16*/
	{ 0x00006E5C,	20,	10 }, /*VOFF_GAIN_17*/
	{ 0x00006E60,	0,	10 }, /*VOFF_GAIN_18*/
	{ 0x00006E60,	10,	10 }, /*VOFF_GAIN_19*/
	{ 0x00006E60,	20,	10 }, /*VOFF_GAIN_20*/
	{ 0x00006E64,	0,	10 }, /*VOFF_INT1*/
	{ 0x00007418,	7,	5 }, /*DS_SPK_INT1_CAP_TRIM*/
	{ 0x0000741C,	0,	5 }, /*DS_SPK_INT2_CAP_TRIM*/
	{ 0x0000741C,	11,	4 }, /*DS_SPK_LPF_CAP_TRIM*/
	{ 0x0000741C,	19,	4 }, /*DS_SPK_QUAN_CAP_TRIM*/
	{ 0x00007434,	17,	1 }, /*FORCE_CAL*/
	{ 0x00007434,	18,	7 }, /*CAL_OVERRIDE*/
	{ 0x00007068,	0,	9 }, /*MODIX*/
	{ 0x0000410C,	7,	1 }, /*VIMON_DLY_NOT_COMB*/
	{ 0x0000400C,	0,	7 }, /*VIMON_DLY*/
	{ 0x00004000,	11,	1 }, /*VMON_POL*/
	{ 0x00017040,	0,	8 }, /*X_COORDINATE*/
	{ 0x00017040,	8,	8 }, /*Y_COORDINATE*/
	{ 0x00017040,	16,	8 }, /*WAFER_ID*/
	{ 0x00017040,	24,	8 }, /*DVS*/
	{ 0x00017044,	0,	24 }, /*LOT_NUMBER*/
};

static const struct reg_sequence cs35l41_reva0_errata_patch[] = {
	{ 0x00003854,			 0x05180240 },
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_OTP_TRIM_30,		 0x9091A1C8 },
	{ 0x00003014,			 0x0200EE0E },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ 0x00000054,			 0x00000004 },
	{ CS35L41_IRQ1_DB3,		 0x00000000 },
	{ CS35L41_IRQ2_DB3,		 0x00000000 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_PWR_CTRL2,		 0x00000000 },
	{ CS35L41_AMP_GAIN_CTRL,	 0x00000000 },
};

static const struct reg_sequence cs35l41_revb0_errata_patch[] = {
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_PWR_CTRL2,		 0x00000000 },
	{ CS35L41_AMP_GAIN_CTRL,	 0x00000000 },
};

static const struct reg_sequence cs35l41_revb2_errata_patch[] = {
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_PWR_CTRL2,		 0x00000000 },
	{ CS35L41_AMP_GAIN_CTRL,	 0x00000000 },
};

static const struct reg_sequence cs35l41_fs_errata_patch[] = {
	{ CS35L41_DSP1_RX1_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX2_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX3_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX4_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX5_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX6_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX7_RATE,	0x00000001 },
	{ CS35L41_DSP1_RX8_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX1_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX2_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX3_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX4_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX5_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX6_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX7_RATE,	0x00000001 },
	{ CS35L41_DSP1_TX8_RATE,	0x00000001 },
};

static const struct cs35l41_otp_map_element_t cs35l41_otp_map_map[] = {
	{
		.id = 0x01,
		.map = otp_map_1,
		.num_elements = ARRAY_SIZE(otp_map_1),
		.bit_offset = 16,
		.word_offset = 2,
	},
	{
		.id = 0x02,
		.map = otp_map_2,
		.num_elements = ARRAY_SIZE(otp_map_2),
		.bit_offset = 16,
		.word_offset = 2,
	},
	{
		.id = 0x03,
		.map = otp_map_2,
		.num_elements = ARRAY_SIZE(otp_map_2),
		.bit_offset = 16,
		.word_offset = 2,
	},
	{
		.id = 0x06,
		.map = otp_map_2,
		.num_elements = ARRAY_SIZE(otp_map_2),
		.bit_offset = 16,
		.word_offset = 2,
	},
	{
		.id = 0x08,
		.map = otp_map_1,
		.num_elements = ARRAY_SIZE(otp_map_1),
		.bit_offset = 16,
		.word_offset = 2,
	},
};

struct regmap_config cs35l41_regmap_i2c = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = CS35L41_REGSTRIDE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L41_LASTREG,
	.reg_defaults = cs35l41_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l41_reg),
	.volatile_reg = cs35l41_volatile_reg,
	.readable_reg = cs35l41_readable_reg,
	.precious_reg = cs35l41_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(cs35l41_regmap_i2c);

struct regmap_config cs35l41_regmap_spi = {
	.reg_bits = 32,
	.val_bits = 32,
	.pad_bits = 16,
	.reg_stride = CS35L41_REGSTRIDE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L41_LASTREG,
	.reg_defaults = cs35l41_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l41_reg),
	.volatile_reg = cs35l41_volatile_reg,
	.readable_reg = cs35l41_readable_reg,
	.precious_reg = cs35l41_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(cs35l41_regmap_spi);

static const struct cs35l41_otp_map_element_t *cs35l41_find_otp_map(u32 otp_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_otp_map_map); i++) {
		if (cs35l41_otp_map_map[i].id == otp_id)
			return &cs35l41_otp_map_map[i];
	}

	return NULL;
}

int cs35l41_test_key_unlock(struct device *dev, struct regmap *regmap)
{
	static const struct reg_sequence unlock[] = {
		{ CS35L41_TEST_KEY_CTL, 0x00000055 },
		{ CS35L41_TEST_KEY_CTL, 0x000000AA },
	};
	int ret;

	ret = regmap_multi_reg_write(regmap, unlock, ARRAY_SIZE(unlock));
	if (ret)
		dev_err(dev, "Failed to unlock test key: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_test_key_unlock);

int cs35l41_test_key_lock(struct device *dev, struct regmap *regmap)
{
	static const struct reg_sequence unlock[] = {
		{ CS35L41_TEST_KEY_CTL, 0x000000CC },
		{ CS35L41_TEST_KEY_CTL, 0x00000033 },
	};
	int ret;

	ret = regmap_multi_reg_write(regmap, unlock, ARRAY_SIZE(unlock));
	if (ret)
		dev_err(dev, "Failed to lock test key: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_test_key_lock);

/* Must be called with the TEST_KEY unlocked */
int cs35l41_otp_unpack(struct device *dev, struct regmap *regmap)
{
	const struct cs35l41_otp_map_element_t *otp_map_match;
	const struct cs35l41_otp_packed_element_t *otp_map;
	int bit_offset, word_offset, ret, i;
	unsigned int bit_sum = 8;
	u32 otp_val, otp_id_reg;
	u32 *otp_mem;

	otp_mem = kmalloc_array(CS35L41_OTP_SIZE_WORDS, sizeof(*otp_mem), GFP_KERNEL);
	if (!otp_mem)
		return -ENOMEM;

	ret = regmap_read(regmap, CS35L41_OTPID, &otp_id_reg);
	if (ret) {
		dev_err(dev, "Read OTP ID failed: %d\n", ret);
		goto err_otp_unpack;
	}

	otp_map_match = cs35l41_find_otp_map(otp_id_reg);

	if (!otp_map_match) {
		dev_err(dev, "OTP Map matching ID %d not found\n", otp_id_reg);
		ret = -EINVAL;
		goto err_otp_unpack;
	}

	ret = regmap_bulk_read(regmap, CS35L41_OTP_MEM0, otp_mem, CS35L41_OTP_SIZE_WORDS);
	if (ret) {
		dev_err(dev, "Read OTP Mem failed: %d\n", ret);
		goto err_otp_unpack;
	}

	otp_map = otp_map_match->map;

	bit_offset = otp_map_match->bit_offset;
	word_offset = otp_map_match->word_offset;

	for (i = 0; i < otp_map_match->num_elements; i++) {
		dev_dbg(dev, "bitoffset= %d, word_offset=%d, bit_sum mod 32=%d, otp_map[i].size = %u\n",
			bit_offset, word_offset, bit_sum % 32, otp_map[i].size);
		if (bit_offset + otp_map[i].size - 1 >= 32) {
			otp_val = (otp_mem[word_offset] &
					GENMASK(31, bit_offset)) >> bit_offset;
			otp_val |= (otp_mem[++word_offset] &
					GENMASK(bit_offset + otp_map[i].size - 33, 0)) <<
					(32 - bit_offset);
			bit_offset += otp_map[i].size - 32;
		} else if (bit_offset + otp_map[i].size - 1 >= 0) {
			otp_val = (otp_mem[word_offset] &
				   GENMASK(bit_offset + otp_map[i].size - 1, bit_offset)
				  ) >> bit_offset;
			bit_offset += otp_map[i].size;
		} else /* both bit_offset and otp_map[i].size are 0 */
			otp_val = 0;

		bit_sum += otp_map[i].size;

		if (bit_offset == 32) {
			bit_offset = 0;
			word_offset++;
		}

		if (otp_map[i].reg != 0) {
			ret = regmap_update_bits(regmap, otp_map[i].reg,
						 GENMASK(otp_map[i].shift + otp_map[i].size - 1,
							 otp_map[i].shift),
						 otp_val << otp_map[i].shift);
			if (ret < 0) {
				dev_err(dev, "Write OTP val failed: %d\n", ret);
				goto err_otp_unpack;
			}
		}
	}

	ret = 0;

err_otp_unpack:
	kfree(otp_mem);

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_otp_unpack);

/* Must be called with the TEST_KEY unlocked */
int cs35l41_register_errata_patch(struct device *dev, struct regmap *reg, unsigned int reg_revid)
{
	char *rev;
	int ret;

	switch (reg_revid) {
	case CS35L41_REVID_A0:
		ret = regmap_register_patch(reg, cs35l41_reva0_errata_patch,
					    ARRAY_SIZE(cs35l41_reva0_errata_patch));
		rev = "A0";
		break;
	case CS35L41_REVID_B0:
		ret = regmap_register_patch(reg, cs35l41_revb0_errata_patch,
					    ARRAY_SIZE(cs35l41_revb0_errata_patch));
		rev = "B0";
		break;
	case CS35L41_REVID_B2:
		ret = regmap_register_patch(reg, cs35l41_revb2_errata_patch,
					    ARRAY_SIZE(cs35l41_revb2_errata_patch));
		rev = "B2";
		break;
	default:
		ret = -EINVAL;
		rev = "XX";
		break;
	}

	if (ret)
		dev_err(dev, "Failed to apply %s errata patch: %d\n", rev, ret);

	ret = regmap_write(reg, CS35L41_DSP1_CCM_CORE_CTRL, 0);
	if (ret < 0)
		dev_err(dev, "Write CCM_CORE_CTRL failed: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_register_errata_patch);

int cs35l41_set_channels(struct device *dev, struct regmap *reg,
			 unsigned int tx_num, unsigned int *tx_slot,
			 unsigned int rx_num, unsigned int *rx_slot)
{
	unsigned int val, mask;
	int i;

	if (tx_num > 4 || rx_num > 2)
		return -EINVAL;

	val = 0;
	mask = 0;
	for (i = 0; i < rx_num; i++) {
		dev_dbg(dev, "rx slot %d position = %d\n", i, rx_slot[i]);
		val |= rx_slot[i] << (i * 8);
		mask |= 0x3F << (i * 8);
	}
	regmap_update_bits(reg, CS35L41_SP_FRAME_RX_SLOT, mask, val);

	val = 0;
	mask = 0;
	for (i = 0; i < tx_num; i++) {
		dev_dbg(dev, "tx slot %d position = %d\n", i, tx_slot[i]);
		val |= tx_slot[i] << (i * 8);
		mask |= 0x3F << (i * 8);
	}
	regmap_update_bits(reg, CS35L41_SP_FRAME_TX_SLOT, mask, val);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l41_set_channels);

static const unsigned char cs35l41_bst_k1_table[4][5] = {
	{ 0x24, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x24, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x40, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x40, 0x32, 0x32, 0x4F, 0x57 }
};

static const unsigned char cs35l41_bst_k2_table[4][5] = {
	{ 0x24, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x24, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x48, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x48, 0x49, 0x66, 0xA3, 0xEA }
};

static const unsigned char cs35l41_bst_slope_table[4] = {
	0x75, 0x6B, 0x3B, 0x28
};

static int cs35l41_boost_config(struct device *dev, struct regmap *regmap, int boost_ind,
				int boost_cap, int boost_ipk)
{
	unsigned char bst_lbst_val, bst_cbst_range, bst_ipk_scaled;
	int ret;

	switch (boost_ind) {
	case 1000:	/* 1.0 uH */
		bst_lbst_val = 0;
		break;
	case 1200:	/* 1.2 uH */
		bst_lbst_val = 1;
		break;
	case 1500:	/* 1.5 uH */
		bst_lbst_val = 2;
		break;
	case 2200:	/* 2.2 uH */
		bst_lbst_val = 3;
		break;
	default:
		dev_err(dev, "Invalid boost inductor value: %d nH\n", boost_ind);
		return -EINVAL;
	}

	switch (boost_cap) {
	case 0 ... 19:
		bst_cbst_range = 0;
		break;
	case 20 ... 50:
		bst_cbst_range = 1;
		break;
	case 51 ... 100:
		bst_cbst_range = 2;
		break;
	case 101 ... 200:
		bst_cbst_range = 3;
		break;
	default:
		if (boost_cap < 0) {
			dev_err(dev, "Invalid boost capacitor value: %d nH\n", boost_cap);
			return -EINVAL;
		}
		/* 201 uF and greater */
		bst_cbst_range = 4;
	}

	if (boost_ipk < 1600 || boost_ipk > 4500) {
		dev_err(dev, "Invalid boost inductor peak current: %d mA\n", boost_ipk);
		return -EINVAL;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_COEFF,
				 CS35L41_BST_K1_MASK | CS35L41_BST_K2_MASK,
				 cs35l41_bst_k1_table[bst_lbst_val][bst_cbst_range]
					<< CS35L41_BST_K1_SHIFT |
				 cs35l41_bst_k2_table[bst_lbst_val][bst_cbst_range]
					<< CS35L41_BST_K2_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost coefficients: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_SLOPE_LBST,
				 CS35L41_BST_SLOPE_MASK | CS35L41_BST_LBST_VAL_MASK,
				 cs35l41_bst_slope_table[bst_lbst_val]
					<< CS35L41_BST_SLOPE_SHIFT |
				 bst_lbst_val << CS35L41_BST_LBST_VAL_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost slope/inductor value: %d\n", ret);
		return ret;
	}

	bst_ipk_scaled = ((boost_ipk - 1600) / 50) + 0x10;

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_PEAK_CUR, CS35L41_BST_IPK_MASK,
				 bst_ipk_scaled << CS35L41_BST_IPK_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost inductor peak current: %d\n", ret);
		return ret;
	}

	regmap_update_bits(regmap, CS35L41_PWR_CTRL2, CS35L41_BST_EN_MASK,
			   CS35L41_BST_EN_DEFAULT << CS35L41_BST_EN_SHIFT);

	return 0;
}

static const struct reg_sequence cs35l41_safe_to_reset[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x0000393C,			0x000000C0, 6000},
	{ 0x0000393C,			0x00000000 },
	{ 0x00007414,			0x00C82222 },
	{ 0x0000742C,			0x00000000 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_active_to_safe[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ CS35L41_PWR_CTRL1,		0x00000000 },
	{ 0x0000742C,			0x00000009, 3000 },
	{ 0x00007438,			0x00580941 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_safe_to_active[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x0000742C,			0x0000000F },
	{ 0x0000742C,			0x00000079 },
	{ 0x00007438,			0x00585941 },
	{ CS35L41_PWR_CTRL1,		0x00000001, 3000 }, // GLOBAL_EN = 1
	{ 0x0000742C,			0x000000F9 },
	{ 0x00007438,			0x00580941 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_reset_to_safe[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ 0x00007414,			0x08C82222 },
	{ 0x0000742C,			0x00000009 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

int cs35l41_init_boost(struct device *dev, struct regmap *regmap,
		       struct cs35l41_hw_cfg *hw_cfg)
{
	int ret;

	switch (hw_cfg->bst_type) {
	case CS35L41_INT_BOOST:
		ret = cs35l41_boost_config(dev, regmap, hw_cfg->bst_ind,
					   hw_cfg->bst_cap, hw_cfg->bst_ipk);
		if (ret)
			dev_err(dev, "Error in Boost DT config: %d\n", ret);
		break;
	case CS35L41_EXT_BOOST:
	case CS35L41_EXT_BOOST_NO_VSPK_SWITCH:
		/* Only CLSA0100 doesn't use GPIO as VSPK switch, but even on that laptop we can
		 * toggle GPIO1 as is not connected to anything.
		 * There will be no other device without VSPK switch.
		 */
		regmap_write(regmap, CS35L41_GPIO1_CTRL1, 0x00000001);
		regmap_multi_reg_write(regmap, cs35l41_reset_to_safe,
				       ARRAY_SIZE(cs35l41_reset_to_safe));
		ret = regmap_update_bits(regmap, CS35L41_PWR_CTRL2, CS35L41_BST_EN_MASK,
					 CS35L41_BST_DIS_FET_OFF << CS35L41_BST_EN_SHIFT);
		break;
	default:
		dev_err(dev, "Boost type %d not supported\n", hw_cfg->bst_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_init_boost);

bool cs35l41_safe_reset(struct regmap *regmap, enum cs35l41_boost_type b_type)
{
	switch (b_type) {
	/* There is only one laptop that doesn't have VSPK switch. */
	case CS35L41_EXT_BOOST_NO_VSPK_SWITCH:
		return false;
	case CS35L41_EXT_BOOST:
		regmap_write(regmap, CS35L41_GPIO1_CTRL1, 0x00000001);
		regmap_multi_reg_write(regmap, cs35l41_safe_to_reset,
				       ARRAY_SIZE(cs35l41_safe_to_reset));
		return true;
	default:
		return true;
	}
}
EXPORT_SYMBOL_GPL(cs35l41_safe_reset);

int cs35l41_global_enable(struct regmap *regmap, enum cs35l41_boost_type b_type, int enable)
{
	int ret;

	switch (b_type) {
	case CS35L41_INT_BOOST:
		ret = regmap_update_bits(regmap, CS35L41_PWR_CTRL1, CS35L41_GLOBAL_EN_MASK,
					 enable << CS35L41_GLOBAL_EN_SHIFT);
		usleep_range(3000, 3100);
		break;
	case CS35L41_EXT_BOOST:
	case CS35L41_EXT_BOOST_NO_VSPK_SWITCH:
		if (enable)
			ret = regmap_multi_reg_write(regmap, cs35l41_safe_to_active,
						     ARRAY_SIZE(cs35l41_safe_to_active));
		else
			ret = regmap_multi_reg_write(regmap, cs35l41_active_to_safe,
						     ARRAY_SIZE(cs35l41_active_to_safe));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_global_enable);

int cs35l41_gpio_config(struct regmap *regmap, struct cs35l41_hw_cfg *hw_cfg)
{
	struct cs35l41_gpio_cfg *gpio1 = &hw_cfg->gpio1;
	struct cs35l41_gpio_cfg *gpio2 = &hw_cfg->gpio2;
	int irq_pol = IRQF_TRIGGER_NONE;

	regmap_update_bits(regmap, CS35L41_GPIO1_CTRL1,
			   CS35L41_GPIO_POL_MASK | CS35L41_GPIO_DIR_MASK,
			   gpio1->pol_inv << CS35L41_GPIO_POL_SHIFT |
			   !gpio1->out_en << CS35L41_GPIO_DIR_SHIFT);

	regmap_update_bits(regmap, CS35L41_GPIO2_CTRL1,
			   CS35L41_GPIO_POL_MASK | CS35L41_GPIO_DIR_MASK,
			   gpio2->pol_inv << CS35L41_GPIO_POL_SHIFT |
			   !gpio2->out_en << CS35L41_GPIO_DIR_SHIFT);

	if (gpio1->valid)
		regmap_update_bits(regmap, CS35L41_GPIO_PAD_CONTROL, CS35L41_GPIO1_CTRL_MASK,
				   gpio1->func << CS35L41_GPIO1_CTRL_SHIFT);

	if (gpio2->valid) {
		regmap_update_bits(regmap, CS35L41_GPIO_PAD_CONTROL, CS35L41_GPIO2_CTRL_MASK,
				   gpio2->func << CS35L41_GPIO2_CTRL_SHIFT);

		switch (gpio2->func) {
		case CS35L41_GPIO2_INT_PUSH_PULL_LOW:
		case CS35L41_GPIO2_INT_OPEN_DRAIN:
			irq_pol = IRQF_TRIGGER_LOW;
			break;
		case CS35L41_GPIO2_INT_PUSH_PULL_HIGH:
			irq_pol = IRQF_TRIGGER_HIGH;
			break;
		default:
			break;
		}
	}

	return irq_pol;
}
EXPORT_SYMBOL_GPL(cs35l41_gpio_config);

static const struct cs_dsp_region cs35l41_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L41_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L41_DSP1_XMEM_PACK_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L41_DSP1_YMEM_PACK_0 },
	{. type = WMFW_ADSP2_XM,	.base = CS35L41_DSP1_XMEM_UNPACK24_0},
	{. type = WMFW_ADSP2_YM,	.base = CS35L41_DSP1_YMEM_UNPACK24_0},
};

void cs35l41_configure_cs_dsp(struct device *dev, struct regmap *reg, struct cs_dsp *dsp)
{
	dsp->num = 1;
	dsp->type = WMFW_HALO;
	dsp->rev = 0;
	dsp->dev = dev;
	dsp->regmap = reg;
	dsp->base = CS35L41_DSP1_CTRL_BASE;
	dsp->base_sysinfo = CS35L41_DSP1_SYS_ID;
	dsp->mem = cs35l41_dsp1_regions;
	dsp->num_mems = ARRAY_SIZE(cs35l41_dsp1_regions);
	dsp->lock_regions = 0xFFFFFFFF;
}
EXPORT_SYMBOL_GPL(cs35l41_configure_cs_dsp);

static bool cs35l41_check_cspl_mbox_sts(enum cs35l41_cspl_mbox_cmd cmd,
					enum cs35l41_cspl_mbox_status sts)
{
	switch (cmd) {
	case CSPL_MBOX_CMD_NONE:
	case CSPL_MBOX_CMD_UNKNOWN_CMD:
		return true;
	case CSPL_MBOX_CMD_PAUSE:
	case CSPL_MBOX_CMD_OUT_OF_HIBERNATE:
		return (sts == CSPL_MBOX_STS_PAUSED);
	case CSPL_MBOX_CMD_RESUME:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_REINIT:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_STOP_PRE_REINIT:
		return (sts == CSPL_MBOX_STS_RDY_FOR_REINIT);
	default:
		return false;
	}
}

int cs35l41_set_cspl_mbox_cmd(struct device *dev, struct regmap *regmap,
			      enum cs35l41_cspl_mbox_cmd cmd)
{
	unsigned int sts = 0, i;
	int ret;

	// Set mailbox cmd
	ret = regmap_write(regmap, CS35L41_DSP_VIRT1_MBOX_1, cmd);
	if (ret < 0) {
		if (cmd != CSPL_MBOX_CMD_OUT_OF_HIBERNATE)
			dev_err(dev, "Failed to write MBOX: %d\n", ret);
		return ret;
	}

	// Read mailbox status and verify it is appropriate for the given cmd
	for (i = 0; i < 5; i++) {
		usleep_range(1000, 1100);

		ret = regmap_read(regmap, CS35L41_DSP_MBOX_2, &sts);
		if (ret < 0) {
			dev_err(dev, "Failed to read MBOX STS: %d\n", ret);
			continue;
		}

		if (!cs35l41_check_cspl_mbox_sts(cmd, sts))
			dev_dbg(dev, "[%u] cmd %u returned invalid sts %u", i, cmd, sts);
		else
			return 0;
	}

	dev_err(dev, "Failed to set mailbox cmd %u (status %u)\n", cmd, sts);

	return -ENOMSG;
}
EXPORT_SYMBOL_GPL(cs35l41_set_cspl_mbox_cmd);

int cs35l41_write_fs_errata(struct device *dev, struct regmap *regmap)
{
	int ret;

	ret = regmap_multi_reg_write(regmap, cs35l41_fs_errata_patch,
				     ARRAY_SIZE(cs35l41_fs_errata_patch));
	if (ret < 0)
		dev_err(dev, "Failed to write fs errata: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cs35l41_write_fs_errata);

MODULE_DESCRIPTION("CS35L41 library");
MODULE_AUTHOR("David Rhodes, Cirrus Logic Inc, <david.rhodes@cirrus.com>");
MODULE_AUTHOR("Lucas Tanure, Cirrus Logic Inc, <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
