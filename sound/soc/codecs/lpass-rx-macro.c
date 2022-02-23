// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/of_clk.h>
#include <linux/clk-provider.h>

#define CDC_RX_TOP_TOP_CFG0		(0x0000)
#define CDC_RX_TOP_SWR_CTRL		(0x0008)
#define CDC_RX_TOP_DEBUG		(0x000C)
#define CDC_RX_TOP_DEBUG_BUS		(0x0010)
#define CDC_RX_TOP_DEBUG_EN0		(0x0014)
#define CDC_RX_TOP_DEBUG_EN1		(0x0018)
#define CDC_RX_TOP_DEBUG_EN2		(0x001C)
#define CDC_RX_TOP_HPHL_COMP_WR_LSB	(0x0020)
#define CDC_RX_TOP_HPHL_COMP_WR_MSB	(0x0024)
#define CDC_RX_TOP_HPHL_COMP_LUT	(0x0028)
#define CDC_RX_TOP_HPH_LUT_BYPASS_MASK	BIT(7)
#define CDC_RX_TOP_HPHL_COMP_RD_LSB	(0x002C)
#define CDC_RX_TOP_HPHL_COMP_RD_MSB	(0x0030)
#define CDC_RX_TOP_HPHR_COMP_WR_LSB	(0x0034)
#define CDC_RX_TOP_HPHR_COMP_WR_MSB	(0x0038)
#define CDC_RX_TOP_HPHR_COMP_LUT	(0x003C)
#define CDC_RX_TOP_HPHR_COMP_RD_LSB	(0x0040)
#define CDC_RX_TOP_HPHR_COMP_RD_MSB	(0x0044)
#define CDC_RX_TOP_DSD0_DEBUG_CFG0	(0x0070)
#define CDC_RX_TOP_DSD0_DEBUG_CFG1	(0x0074)
#define CDC_RX_TOP_DSD0_DEBUG_CFG2	(0x0078)
#define CDC_RX_TOP_DSD0_DEBUG_CFG3	(0x007C)
#define CDC_RX_TOP_DSD1_DEBUG_CFG0	(0x0080)
#define CDC_RX_TOP_DSD1_DEBUG_CFG1	(0x0084)
#define CDC_RX_TOP_DSD1_DEBUG_CFG2	(0x0088)
#define CDC_RX_TOP_DSD1_DEBUG_CFG3	(0x008C)
#define CDC_RX_TOP_RX_I2S_CTL		(0x0090)
#define CDC_RX_TOP_TX_I2S2_CTL		(0x0094)
#define CDC_RX_TOP_I2S_CLK		(0x0098)
#define CDC_RX_TOP_I2S_RESET		(0x009C)
#define CDC_RX_TOP_I2S_MUX		(0x00A0)
#define CDC_RX_CLK_RST_CTRL_MCLK_CONTROL	(0x0100)
#define CDC_RX_CLK_MCLK_EN_MASK		BIT(0)
#define CDC_RX_CLK_MCLK_ENABLE		BIT(0)
#define CDC_RX_CLK_MCLK2_EN_MASK	BIT(1)
#define CDC_RX_CLK_MCLK2_ENABLE		BIT(1)
#define CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL	(0x0104)
#define CDC_RX_FS_MCLK_CNT_EN_MASK	BIT(0)
#define CDC_RX_FS_MCLK_CNT_ENABLE	BIT(0)
#define CDC_RX_FS_MCLK_CNT_CLR_MASK	BIT(1)
#define CDC_RX_FS_MCLK_CNT_CLR		BIT(1)
#define CDC_RX_CLK_RST_CTRL_SWR_CONTROL	(0x0108)
#define CDC_RX_SWR_CLK_EN_MASK		BIT(0)
#define CDC_RX_SWR_RESET_MASK		BIT(1)
#define CDC_RX_SWR_RESET		BIT(1)
#define CDC_RX_CLK_RST_CTRL_DSD_CONTROL	(0x010C)
#define CDC_RX_CLK_RST_CTRL_ASRC_SHARE_CONTROL	(0x0110)
#define CDC_RX_SOFTCLIP_CRC		(0x0140)
#define CDC_RX_SOFTCLIP_CLK_EN_MASK	BIT(0)
#define CDC_RX_SOFTCLIP_SOFTCLIP_CTRL	(0x0144)
#define CDC_RX_SOFTCLIP_EN_MASK		BIT(0)
#define CDC_RX_INP_MUX_RX_INT0_CFG0	(0x0180)
#define CDC_RX_INTX_1_MIX_INP0_SEL_MASK	GENMASK(3, 0)
#define CDC_RX_INTX_1_MIX_INP1_SEL_MASK	GENMASK(7, 4)
#define CDC_RX_INP_MUX_RX_INT0_CFG1	(0x0184)
#define CDC_RX_INTX_2_SEL_MASK		GENMASK(3, 0)
#define CDC_RX_INTX_1_MIX_INP2_SEL_MASK	GENMASK(7, 4)
#define CDC_RX_INP_MUX_RX_INT1_CFG0	(0x0188)
#define CDC_RX_INP_MUX_RX_INT1_CFG1	(0x018C)
#define CDC_RX_INP_MUX_RX_INT2_CFG0	(0x0190)
#define CDC_RX_INP_MUX_RX_INT2_CFG1	(0x0194)
#define CDC_RX_INP_MUX_RX_MIX_CFG4	(0x0198)
#define CDC_RX_INP_MUX_RX_MIX_CFG5	(0x019C)
#define CDC_RX_INP_MUX_SIDETONE_SRC_CFG0	(0x01A0)
#define CDC_RX_CLSH_CRC			(0x0200)
#define CDC_RX_CLSH_CLK_EN_MASK		BIT(0)
#define CDC_RX_CLSH_DLY_CTRL		(0x0204)
#define CDC_RX_CLSH_DECAY_CTRL		(0x0208)
#define CDC_RX_CLSH_DECAY_RATE_MASK	GENMASK(2, 0)
#define CDC_RX_CLSH_HPH_V_PA		(0x020C)
#define CDC_RX_CLSH_HPH_V_PA_MIN_MASK	GENMASK(5, 0)
#define CDC_RX_CLSH_EAR_V_PA		(0x0210)
#define CDC_RX_CLSH_HPH_V_HD		(0x0214)
#define CDC_RX_CLSH_EAR_V_HD		(0x0218)
#define CDC_RX_CLSH_K1_MSB		(0x021C)
#define CDC_RX_CLSH_K1_MSB_COEFF_MASK	GENMASK(3, 0)
#define CDC_RX_CLSH_K1_LSB		(0x0220)
#define CDC_RX_CLSH_K2_MSB		(0x0224)
#define CDC_RX_CLSH_K2_LSB		(0x0228)
#define CDC_RX_CLSH_IDLE_CTRL		(0x022C)
#define CDC_RX_CLSH_IDLE_HPH		(0x0230)
#define CDC_RX_CLSH_IDLE_EAR		(0x0234)
#define CDC_RX_CLSH_TEST0		(0x0238)
#define CDC_RX_CLSH_TEST1		(0x023C)
#define CDC_RX_CLSH_OVR_VREF		(0x0240)
#define CDC_RX_CLSH_CLSG_CTL		(0x0244)
#define CDC_RX_CLSH_CLSG_CFG1		(0x0248)
#define CDC_RX_CLSH_CLSG_CFG2		(0x024C)
#define CDC_RX_BCL_VBAT_PATH_CTL	(0x0280)
#define CDC_RX_BCL_VBAT_CFG		(0x0284)
#define CDC_RX_BCL_VBAT_ADC_CAL1	(0x0288)
#define CDC_RX_BCL_VBAT_ADC_CAL2	(0x028C)
#define CDC_RX_BCL_VBAT_ADC_CAL3	(0x0290)
#define CDC_RX_BCL_VBAT_PK_EST1		(0x0294)
#define CDC_RX_BCL_VBAT_PK_EST2		(0x0298)
#define CDC_RX_BCL_VBAT_PK_EST3		(0x029C)
#define CDC_RX_BCL_VBAT_RF_PROC1	(0x02A0)
#define CDC_RX_BCL_VBAT_RF_PROC2	(0x02A4)
#define CDC_RX_BCL_VBAT_TAC1		(0x02A8)
#define CDC_RX_BCL_VBAT_TAC2		(0x02AC)
#define CDC_RX_BCL_VBAT_TAC3		(0x02B0)
#define CDC_RX_BCL_VBAT_TAC4		(0x02B4)
#define CDC_RX_BCL_VBAT_GAIN_UPD1	(0x02B8)
#define CDC_RX_BCL_VBAT_GAIN_UPD2	(0x02BC)
#define CDC_RX_BCL_VBAT_GAIN_UPD3	(0x02C0)
#define CDC_RX_BCL_VBAT_GAIN_UPD4	(0x02C4)
#define CDC_RX_BCL_VBAT_GAIN_UPD5	(0x02C8)
#define CDC_RX_BCL_VBAT_DEBUG1		(0x02CC)
#define CDC_RX_BCL_VBAT_GAIN_UPD_MON	(0x02D0)
#define CDC_RX_BCL_VBAT_GAIN_MON_VAL	(0x02D4)
#define CDC_RX_BCL_VBAT_BAN		(0x02D8)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD1	(0x02DC)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD2	(0x02E0)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD3	(0x02E4)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD4	(0x02E8)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD5	(0x02EC)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD6	(0x02F0)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD7	(0x02F4)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD8	(0x02F8)
#define CDC_RX_BCL_VBAT_BCL_GAIN_UPD9	(0x02FC)
#define CDC_RX_BCL_VBAT_ATTN1		(0x0300)
#define CDC_RX_BCL_VBAT_ATTN2		(0x0304)
#define CDC_RX_BCL_VBAT_ATTN3		(0x0308)
#define CDC_RX_BCL_VBAT_DECODE_CTL1	(0x030C)
#define CDC_RX_BCL_VBAT_DECODE_CTL2	(0x0310)
#define CDC_RX_BCL_VBAT_DECODE_CFG1	(0x0314)
#define CDC_RX_BCL_VBAT_DECODE_CFG2	(0x0318)
#define CDC_RX_BCL_VBAT_DECODE_CFG3	(0x031C)
#define CDC_RX_BCL_VBAT_DECODE_CFG4	(0x0320)
#define CDC_RX_BCL_VBAT_DECODE_ST	(0x0324)
#define CDC_RX_INTR_CTRL_CFG		(0x0340)
#define CDC_RX_INTR_CTRL_CLR_COMMIT	(0x0344)
#define CDC_RX_INTR_CTRL_PIN1_MASK0	(0x0360)
#define CDC_RX_INTR_CTRL_PIN1_STATUS0	(0x0368)
#define CDC_RX_INTR_CTRL_PIN1_CLEAR0	(0x0370)
#define CDC_RX_INTR_CTRL_PIN2_MASK0	(0x0380)
#define CDC_RX_INTR_CTRL_PIN2_STATUS0	(0x0388)
#define CDC_RX_INTR_CTRL_PIN2_CLEAR0	(0x0390)
#define CDC_RX_INTR_CTRL_LEVEL0		(0x03C0)
#define CDC_RX_INTR_CTRL_BYPASS0	(0x03C8)
#define CDC_RX_INTR_CTRL_SET0		(0x03D0)
#define CDC_RX_RXn_RX_PATH_CTL(n)	(0x0400 + 0x80 * n)
#define CDC_RX_RX0_RX_PATH_CTL		(0x0400)
#define CDC_RX_PATH_RESET_EN_MASK	BIT(6)
#define CDC_RX_PATH_CLK_EN_MASK		BIT(5)
#define CDC_RX_PATH_CLK_ENABLE		BIT(5)
#define CDC_RX_PATH_PGA_MUTE_MASK	BIT(4)
#define CDC_RX_PATH_PGA_MUTE_ENABLE	BIT(4)
#define CDC_RX_PATH_PCM_RATE_MASK	GENMASK(3, 0)
#define CDC_RX_RXn_RX_PATH_CFG0(n)	(0x0404 + 0x80 * n)
#define CDC_RX_RXn_COMP_EN_MASK		BIT(1)
#define CDC_RX_RX0_RX_PATH_CFG0		(0x0404)
#define CDC_RX_RXn_CLSH_EN_MASK		BIT(6)
#define CDC_RX_DLY_ZN_EN_MASK		BIT(3)
#define CDC_RX_DLY_ZN_ENABLE		BIT(3)
#define CDC_RX_RXn_HD2_EN_MASK		BIT(2)
#define CDC_RX_RXn_RX_PATH_CFG1(n)	(0x0408 + 0x80 * n)
#define CDC_RX_RXn_SIDETONE_EN_MASK	BIT(4)
#define CDC_RX_RX0_RX_PATH_CFG1		(0x0408)
#define CDC_RX_RX0_HPH_L_EAR_SEL_MASK	BIT(1)
#define CDC_RX_RXn_RX_PATH_CFG2(n)	(0x040C + 0x80 * n)
#define CDC_RX_RXn_HPF_CUT_FREQ_MASK	GENMASK(1, 0)
#define CDC_RX_RX0_RX_PATH_CFG2		(0x040C)
#define CDC_RX_RXn_RX_PATH_CFG3(n)	(0x0410 + 0x80 * n)
#define CDC_RX_RX0_RX_PATH_CFG3		(0x0410)
#define CDC_RX_DC_COEFF_SEL_MASK	GENMASK(1, 0)
#define CDC_RX_DC_COEFF_SEL_TWO		0x2
#define CDC_RX_RXn_RX_VOL_CTL(n)	(0x0414 + 0x80 * n)
#define CDC_RX_RX0_RX_VOL_CTL		(0x0414)
#define CDC_RX_RXn_RX_PATH_MIX_CTL(n)	(0x0418 + 0x80 * n)
#define CDC_RX_RXn_MIX_PCM_RATE_MASK	GENMASK(3, 0)
#define CDC_RX_RXn_MIX_RESET_MASK	BIT(6)
#define CDC_RX_RXn_MIX_RESET		BIT(6)
#define CDC_RX_RXn_MIX_CLK_EN_MASK	BIT(5)
#define CDC_RX_RX0_RX_PATH_MIX_CTL	(0x0418)
#define CDC_RX_RX0_RX_PATH_MIX_CFG	(0x041C)
#define CDC_RX_RXn_RX_VOL_MIX_CTL(n)	(0x0420 + 0x80 * n)
#define CDC_RX_RX0_RX_VOL_MIX_CTL	(0x0420)
#define CDC_RX_RX0_RX_PATH_SEC1		(0x0424)
#define CDC_RX_RX0_RX_PATH_SEC2		(0x0428)
#define CDC_RX_RX0_RX_PATH_SEC3		(0x042C)
#define CDC_RX_RX0_RX_PATH_SEC4		(0x0430)
#define CDC_RX_RX0_RX_PATH_SEC7		(0x0434)
#define CDC_RX_DSM_OUT_DELAY_SEL_MASK	GENMASK(2, 0)
#define CDC_RX_DSM_OUT_DELAY_TWO_SAMPLE	0x2
#define CDC_RX_RX0_RX_PATH_MIX_SEC0	(0x0438)
#define CDC_RX_RX0_RX_PATH_MIX_SEC1	(0x043C)
#define CDC_RX_RXn_RX_PATH_DSM_CTL(n)	(0x0440 + 0x80 * n)
#define CDC_RX_RXn_DSM_CLK_EN_MASK	BIT(0)
#define CDC_RX_RX0_RX_PATH_DSM_CTL	(0x0440)
#define CDC_RX_RX0_RX_PATH_DSM_DATA1	(0x0444)
#define CDC_RX_RX0_RX_PATH_DSM_DATA2	(0x0448)
#define CDC_RX_RX0_RX_PATH_DSM_DATA3	(0x044C)
#define CDC_RX_RX0_RX_PATH_DSM_DATA4	(0x0450)
#define CDC_RX_RX0_RX_PATH_DSM_DATA5	(0x0454)
#define CDC_RX_RX0_RX_PATH_DSM_DATA6	(0x0458)
#define CDC_RX_RX1_RX_PATH_CTL		(0x0480)
#define CDC_RX_RX1_RX_PATH_CFG0		(0x0484)
#define CDC_RX_RX1_RX_PATH_CFG1		(0x0488)
#define CDC_RX_RX1_RX_PATH_CFG2		(0x048C)
#define CDC_RX_RX1_RX_PATH_CFG3		(0x0490)
#define CDC_RX_RX1_RX_VOL_CTL		(0x0494)
#define CDC_RX_RX1_RX_PATH_MIX_CTL	(0x0498)
#define CDC_RX_RX1_RX_PATH_MIX_CFG	(0x049C)
#define CDC_RX_RX1_RX_VOL_MIX_CTL	(0x04A0)
#define CDC_RX_RX1_RX_PATH_SEC1		(0x04A4)
#define CDC_RX_RX1_RX_PATH_SEC2		(0x04A8)
#define CDC_RX_RX1_RX_PATH_SEC3		(0x04AC)
#define CDC_RX_RXn_HD2_ALPHA_MASK	GENMASK(5, 2)
#define CDC_RX_RX1_RX_PATH_SEC4		(0x04B0)
#define CDC_RX_RX1_RX_PATH_SEC7		(0x04B4)
#define CDC_RX_RX1_RX_PATH_MIX_SEC0	(0x04B8)
#define CDC_RX_RX1_RX_PATH_MIX_SEC1	(0x04BC)
#define CDC_RX_RX1_RX_PATH_DSM_CTL	(0x04C0)
#define CDC_RX_RX1_RX_PATH_DSM_DATA1	(0x04C4)
#define CDC_RX_RX1_RX_PATH_DSM_DATA2	(0x04C8)
#define CDC_RX_RX1_RX_PATH_DSM_DATA3	(0x04CC)
#define CDC_RX_RX1_RX_PATH_DSM_DATA4	(0x04D0)
#define CDC_RX_RX1_RX_PATH_DSM_DATA5	(0x04D4)
#define CDC_RX_RX1_RX_PATH_DSM_DATA6	(0x04D8)
#define CDC_RX_RX2_RX_PATH_CTL		(0x0500)
#define CDC_RX_RX2_RX_PATH_CFG0		(0x0504)
#define CDC_RX_RX2_CLSH_EN_MASK		BIT(4)
#define CDC_RX_RX2_DLY_Z_EN_MASK	BIT(3)
#define CDC_RX_RX2_RX_PATH_CFG1		(0x0508)
#define CDC_RX_RX2_RX_PATH_CFG2		(0x050C)
#define CDC_RX_RX2_RX_PATH_CFG3		(0x0510)
#define CDC_RX_RX2_RX_VOL_CTL		(0x0514)
#define CDC_RX_RX2_RX_PATH_MIX_CTL	(0x0518)
#define CDC_RX_RX2_RX_PATH_MIX_CFG	(0x051C)
#define CDC_RX_RX2_RX_VOL_MIX_CTL	(0x0520)
#define CDC_RX_RX2_RX_PATH_SEC0		(0x0524)
#define CDC_RX_RX2_RX_PATH_SEC1		(0x0528)
#define CDC_RX_RX2_RX_PATH_SEC2		(0x052C)
#define CDC_RX_RX2_RX_PATH_SEC3		(0x0530)
#define CDC_RX_RX2_RX_PATH_SEC4		(0x0534)
#define CDC_RX_RX2_RX_PATH_SEC5		(0x0538)
#define CDC_RX_RX2_RX_PATH_SEC6		(0x053C)
#define CDC_RX_RX2_RX_PATH_SEC7		(0x0540)
#define CDC_RX_RX2_RX_PATH_MIX_SEC0	(0x0544)
#define CDC_RX_RX2_RX_PATH_MIX_SEC1	(0x0548)
#define CDC_RX_RX2_RX_PATH_DSM_CTL	(0x054C)
#define CDC_RX_IDLE_DETECT_PATH_CTL	(0x0780)
#define CDC_RX_IDLE_DETECT_CFG0		(0x0784)
#define CDC_RX_IDLE_DETECT_CFG1		(0x0788)
#define CDC_RX_IDLE_DETECT_CFG2		(0x078C)
#define CDC_RX_IDLE_DETECT_CFG3		(0x0790)
#define CDC_RX_COMPANDERn_CTL0(n)	(0x0800 + 0x40 * n)
#define CDC_RX_COMPANDERn_CLK_EN_MASK	BIT(0)
#define CDC_RX_COMPANDERn_SOFT_RST_MASK	BIT(1)
#define CDC_RX_COMPANDERn_HALT_MASK	BIT(2)
#define CDC_RX_COMPANDER0_CTL0		(0x0800)
#define CDC_RX_COMPANDER0_CTL1		(0x0804)
#define CDC_RX_COMPANDER0_CTL2		(0x0808)
#define CDC_RX_COMPANDER0_CTL3		(0x080C)
#define CDC_RX_COMPANDER0_CTL4		(0x0810)
#define CDC_RX_COMPANDER0_CTL5		(0x0814)
#define CDC_RX_COMPANDER0_CTL6		(0x0818)
#define CDC_RX_COMPANDER0_CTL7		(0x081C)
#define CDC_RX_COMPANDER1_CTL0		(0x0840)
#define CDC_RX_COMPANDER1_CTL1		(0x0844)
#define CDC_RX_COMPANDER1_CTL2		(0x0848)
#define CDC_RX_COMPANDER1_CTL3		(0x084C)
#define CDC_RX_COMPANDER1_CTL4		(0x0850)
#define CDC_RX_COMPANDER1_CTL5		(0x0854)
#define CDC_RX_COMPANDER1_CTL6		(0x0858)
#define CDC_RX_COMPANDER1_CTL7		(0x085C)
#define CDC_RX_COMPANDER1_HPH_LOW_PWR_MODE_MASK	BIT(5)
#define CDC_RX_SIDETONE_IIR0_IIR_PATH_CTL	(0x0A00)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL	(0x0A04)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL	(0x0A08)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL	(0x0A0C)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL	(0x0A10)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B5_CTL	(0x0A14)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B6_CTL	(0x0A18)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B7_CTL	(0x0A1C)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_B8_CTL	(0x0A20)
#define CDC_RX_SIDETONE_IIR0_IIR_CTL		(0x0A24)
#define CDC_RX_SIDETONE_IIR0_IIR_GAIN_TIMER_CTL	(0x0A28)
#define CDC_RX_SIDETONE_IIR0_IIR_COEF_B1_CTL	(0x0A2C)
#define CDC_RX_SIDETONE_IIR0_IIR_COEF_B2_CTL	(0x0A30)
#define CDC_RX_SIDETONE_IIR1_IIR_PATH_CTL	(0x0A80)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL	(0x0A84)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL	(0x0A88)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL	(0x0A8C)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL	(0x0A90)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B5_CTL	(0x0A94)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B6_CTL	(0x0A98)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B7_CTL	(0x0A9C)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_B8_CTL	(0x0AA0)
#define CDC_RX_SIDETONE_IIR1_IIR_CTL		(0x0AA4)
#define CDC_RX_SIDETONE_IIR1_IIR_GAIN_TIMER_CTL	(0x0AA8)
#define CDC_RX_SIDETONE_IIR1_IIR_COEF_B1_CTL	(0x0AAC)
#define CDC_RX_SIDETONE_IIR1_IIR_COEF_B2_CTL	(0x0AB0)
#define CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG0	(0x0B00)
#define CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG1	(0x0B04)
#define CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG2	(0x0B08)
#define CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG3	(0x0B0C)
#define CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG0	(0x0B10)
#define CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG1	(0x0B14)
#define CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG2	(0x0B18)
#define CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG3	(0x0B1C)
#define CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CTL	(0x0B40)
#define CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CFG1	(0x0B44)
#define CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CTL	(0x0B50)
#define CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CFG1	(0x0B54)
#define CDC_RX_EC_REF_HQ0_EC_REF_HQ_PATH_CTL	(0x0C00)
#define CDC_RX_EC_REF_HQ0_EC_REF_HQ_CFG0	(0x0C04)
#define CDC_RX_EC_REF_HQ1_EC_REF_HQ_PATH_CTL	(0x0C40)
#define CDC_RX_EC_REF_HQ1_EC_REF_HQ_CFG0	(0x0C44)
#define CDC_RX_EC_REF_HQ2_EC_REF_HQ_PATH_CTL	(0x0C80)
#define CDC_RX_EC_REF_HQ2_EC_REF_HQ_CFG0	(0x0C84)
#define CDC_RX_EC_ASRC0_CLK_RST_CTL		(0x0D00)
#define CDC_RX_EC_ASRC0_CTL0			(0x0D04)
#define CDC_RX_EC_ASRC0_CTL1			(0x0D08)
#define CDC_RX_EC_ASRC0_FIFO_CTL		(0x0D0C)
#define CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_LSB	(0x0D10)
#define CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_MSB	(0x0D14)
#define CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_LSB	(0x0D18)
#define CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_MSB	(0x0D1C)
#define CDC_RX_EC_ASRC0_STATUS_FIFO		(0x0D20)
#define CDC_RX_EC_ASRC1_CLK_RST_CTL		(0x0D40)
#define CDC_RX_EC_ASRC1_CTL0			(0x0D44)
#define CDC_RX_EC_ASRC1_CTL1			(0x0D48)
#define CDC_RX_EC_ASRC1_FIFO_CTL		(0x0D4C)
#define CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_LSB	(0x0D50)
#define CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_MSB	(0x0D54)
#define CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_LSB	(0x0D58)
#define CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_MSB	(0x0D5C)
#define CDC_RX_EC_ASRC1_STATUS_FIFO		(0x0D60)
#define CDC_RX_EC_ASRC2_CLK_RST_CTL		(0x0D80)
#define CDC_RX_EC_ASRC2_CTL0			(0x0D84)
#define CDC_RX_EC_ASRC2_CTL1			(0x0D88)
#define CDC_RX_EC_ASRC2_FIFO_CTL		(0x0D8C)
#define CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_LSB	(0x0D90)
#define CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_MSB	(0x0D94)
#define CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_LSB	(0x0D98)
#define CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_MSB	(0x0D9C)
#define CDC_RX_EC_ASRC2_STATUS_FIFO		(0x0DA0)
#define CDC_RX_DSD0_PATH_CTL			(0x0F00)
#define CDC_RX_DSD0_CFG0			(0x0F04)
#define CDC_RX_DSD0_CFG1			(0x0F08)
#define CDC_RX_DSD0_CFG2			(0x0F0C)
#define CDC_RX_DSD1_PATH_CTL			(0x0F80)
#define CDC_RX_DSD1_CFG0			(0x0F84)
#define CDC_RX_DSD1_CFG1			(0x0F88)
#define CDC_RX_DSD1_CFG2			(0x0F8C)
#define RX_MAX_OFFSET				(0x0F8C)

#define MCLK_FREQ		9600000

#define RX_MACRO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 |\
			SNDRV_PCM_RATE_384000)
/* Fractional Rates */
#define RX_MACRO_FRAC_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_88200 |\
				SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_352800)

#define RX_MACRO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

#define RX_MACRO_ECHO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_48000)
#define RX_MACRO_ECHO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE)

#define RX_MACRO_MAX_DMA_CH_PER_PORT 2

#define RX_MACRO_EC_MIX_TX0_MASK 0xf0
#define RX_MACRO_EC_MIX_TX1_MASK 0x0f
#define RX_MACRO_EC_MIX_TX2_MASK 0x0f

#define COMP_MAX_COEFF 25
#define RX_NUM_CLKS_MAX	5

struct comp_coeff_val {
	u8 lsb;
	u8 msb;
};

enum {
	HPH_ULP,
	HPH_LOHIFI,
	HPH_MODE_MAX,
};

static const struct comp_coeff_val comp_coeff_table[HPH_MODE_MAX][COMP_MAX_COEFF] = {
	{
		{0x40, 0x00},
		{0x4C, 0x00},
		{0x5A, 0x00},
		{0x6B, 0x00},
		{0x7F, 0x00},
		{0x97, 0x00},
		{0xB3, 0x00},
		{0xD5, 0x00},
		{0xFD, 0x00},
		{0x2D, 0x01},
		{0x66, 0x01},
		{0xA7, 0x01},
		{0xF8, 0x01},
		{0x57, 0x02},
		{0xC7, 0x02},
		{0x4B, 0x03},
		{0xE9, 0x03},
		{0xA3, 0x04},
		{0x7D, 0x05},
		{0x90, 0x06},
		{0xD1, 0x07},
		{0x49, 0x09},
		{0x00, 0x0B},
		{0x01, 0x0D},
		{0x59, 0x0F},
	},
	{
		{0x40, 0x00},
		{0x4C, 0x00},
		{0x5A, 0x00},
		{0x6B, 0x00},
		{0x80, 0x00},
		{0x98, 0x00},
		{0xB4, 0x00},
		{0xD5, 0x00},
		{0xFE, 0x00},
		{0x2E, 0x01},
		{0x66, 0x01},
		{0xA9, 0x01},
		{0xF8, 0x01},
		{0x56, 0x02},
		{0xC4, 0x02},
		{0x4F, 0x03},
		{0xF0, 0x03},
		{0xAE, 0x04},
		{0x8B, 0x05},
		{0x8E, 0x06},
		{0xBC, 0x07},
		{0x56, 0x09},
		{0x0F, 0x0B},
		{0x13, 0x0D},
		{0x6F, 0x0F},
	},
};

struct rx_macro_reg_mask_val {
	u16 reg;
	u8 mask;
	u8 val;
};

enum {
	INTERP_HPHL,
	INTERP_HPHR,
	INTERP_AUX,
	INTERP_MAX
};

enum {
	RX_MACRO_RX0,
	RX_MACRO_RX1,
	RX_MACRO_RX2,
	RX_MACRO_RX3,
	RX_MACRO_RX4,
	RX_MACRO_RX5,
	RX_MACRO_PORTS_MAX
};

enum {
	RX_MACRO_COMP1, /* HPH_L */
	RX_MACRO_COMP2, /* HPH_R */
	RX_MACRO_COMP_MAX
};

enum {
	RX_MACRO_EC0_MUX = 0,
	RX_MACRO_EC1_MUX,
	RX_MACRO_EC2_MUX,
	RX_MACRO_EC_MUX_MAX,
};

enum {
	INTn_1_INP_SEL_ZERO = 0,
	INTn_1_INP_SEL_DEC0,
	INTn_1_INP_SEL_DEC1,
	INTn_1_INP_SEL_IIR0,
	INTn_1_INP_SEL_IIR1,
	INTn_1_INP_SEL_RX0,
	INTn_1_INP_SEL_RX1,
	INTn_1_INP_SEL_RX2,
	INTn_1_INP_SEL_RX3,
	INTn_1_INP_SEL_RX4,
	INTn_1_INP_SEL_RX5,
};

enum {
	INTn_2_INP_SEL_ZERO = 0,
	INTn_2_INP_SEL_RX0,
	INTn_2_INP_SEL_RX1,
	INTn_2_INP_SEL_RX2,
	INTn_2_INP_SEL_RX3,
	INTn_2_INP_SEL_RX4,
	INTn_2_INP_SEL_RX5,
};

enum {
	INTERP_MAIN_PATH,
	INTERP_MIX_PATH,
};

/* Codec supports 2 IIR filters */
enum {
	IIR0 = 0,
	IIR1,
	IIR_MAX,
};

/* Each IIR has 5 Filter Stages */
enum {
	BAND1 = 0,
	BAND2,
	BAND3,
	BAND4,
	BAND5,
	BAND_MAX,
};

#define RX_MACRO_IIR_FILTER_SIZE	(sizeof(u32) * BAND_MAX)

#define RX_MACRO_IIR_FILTER_CTL(xname, iidx, bidx) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = rx_macro_iir_filter_info, \
	.get = rx_macro_get_iir_band_audio_mixer, \
	.put = rx_macro_put_iir_band_audio_mixer, \
	.private_value = (unsigned long)&(struct wcd_iir_filter_ctl) { \
		.iir_idx = iidx, \
		.band_idx = bidx, \
		.bytes_ext = {.max = RX_MACRO_IIR_FILTER_SIZE, }, \
	} \
}

struct interp_sample_rate {
	int sample_rate;
	int rate_val;
};

static struct interp_sample_rate sr_val_tbl[] = {
	{8000, 0x0}, {16000, 0x1}, {32000, 0x3}, {48000, 0x4}, {96000, 0x5},
	{192000, 0x6}, {384000, 0x7}, {44100, 0x9}, {88200, 0xA},
	{176400, 0xB}, {352800, 0xC},
};

enum {
	RX_MACRO_AIF_INVALID = 0,
	RX_MACRO_AIF1_PB,
	RX_MACRO_AIF2_PB,
	RX_MACRO_AIF3_PB,
	RX_MACRO_AIF4_PB,
	RX_MACRO_AIF_ECHO,
	RX_MACRO_MAX_DAIS,
};

enum {
	RX_MACRO_AIF1_CAP = 0,
	RX_MACRO_AIF2_CAP,
	RX_MACRO_AIF3_CAP,
	RX_MACRO_MAX_AIF_CAP_DAIS
};

struct rx_macro {
	struct device *dev;
	int comp_enabled[RX_MACRO_COMP_MAX];
	/* Main path clock users count */
	int main_clk_users[INTERP_MAX];
	int rx_port_value[RX_MACRO_PORTS_MAX];
	u16 prim_int_users[INTERP_MAX];
	int rx_mclk_users;
	bool reset_swr;
	int clsh_users;
	int rx_mclk_cnt;
	bool is_ear_mode_on;
	bool hph_pwr_mode;
	bool hph_hd2_mode;
	struct snd_soc_component *component;
	unsigned long active_ch_mask[RX_MACRO_MAX_DAIS];
	unsigned long active_ch_cnt[RX_MACRO_MAX_DAIS];
	u16 bit_width[RX_MACRO_MAX_DAIS];
	int is_softclip_on;
	int is_aux_hpf_on;
	int softclip_clk_users;

	struct regmap *regmap;
	struct clk_bulk_data clks[RX_NUM_CLKS_MAX];
	struct clk_hw hw;
};
#define to_rx_macro(_hw) container_of(_hw, struct rx_macro, hw)

struct wcd_iir_filter_ctl {
	unsigned int iir_idx;
	unsigned int band_idx;
	struct soc_bytes_ext bytes_ext;
};

static const DECLARE_TLV_DB_SCALE(digital_gain, -8400, 100, -8400);

static const char * const rx_int_mix_mux_text[] = {
	"ZERO", "RX0", "RX1", "RX2", "RX3", "RX4", "RX5"
};

static const char * const rx_prim_mix_text[] = {
	"ZERO", "DEC0", "DEC1", "IIR0", "IIR1", "RX0", "RX1", "RX2",
	"RX3", "RX4", "RX5"
};

static const char * const rx_sidetone_mix_text[] = {
	"ZERO", "SRC0", "SRC1", "SRC_SUM"
};

static const char * const iir_inp_mux_text[] = {
	"ZERO", "DEC0", "DEC1", "DEC2", "DEC3",
	"RX0", "RX1", "RX2", "RX3", "RX4", "RX5"
};

static const char * const rx_int_dem_inp_mux_text[] = {
	"NORMAL_DSM_OUT", "CLSH_DSM_OUT",
};

static const char * const rx_int0_1_interp_mux_text[] = {
	"ZERO", "RX INT0_1 MIX1",
};

static const char * const rx_int1_1_interp_mux_text[] = {
	"ZERO", "RX INT1_1 MIX1",
};

static const char * const rx_int2_1_interp_mux_text[] = {
	"ZERO", "RX INT2_1 MIX1",
};

static const char * const rx_int0_2_interp_mux_text[] = {
	"ZERO", "RX INT0_2 MUX",
};

static const char * const rx_int1_2_interp_mux_text[] = {
	"ZERO", "RX INT1_2 MUX",
};

static const char * const rx_int2_2_interp_mux_text[] = {
	"ZERO", "RX INT2_2 MUX",
};

static const char *const rx_macro_mux_text[] = {
	"ZERO", "AIF1_PB", "AIF2_PB", "AIF3_PB", "AIF4_PB"
};

static const char *const rx_macro_hph_pwr_mode_text[] = {
	"ULP", "LOHIFI"
};

static const char * const rx_echo_mux_text[] = {
	"ZERO", "RX_MIX0", "RX_MIX1", "RX_MIX2"
};

static const struct soc_enum rx_macro_hph_pwr_mode_enum =
		SOC_ENUM_SINGLE_EXT(2, rx_macro_hph_pwr_mode_text);
static const struct soc_enum rx_mix_tx2_mux_enum =
		SOC_ENUM_SINGLE(CDC_RX_INP_MUX_RX_MIX_CFG5, 0, 4, rx_echo_mux_text);
static const struct soc_enum rx_mix_tx1_mux_enum =
		SOC_ENUM_SINGLE(CDC_RX_INP_MUX_RX_MIX_CFG4, 0, 4, rx_echo_mux_text);
static const struct soc_enum rx_mix_tx0_mux_enum =
		SOC_ENUM_SINGLE(CDC_RX_INP_MUX_RX_MIX_CFG4, 4, 4, rx_echo_mux_text);

static SOC_ENUM_SINGLE_DECL(rx_int0_2_enum, CDC_RX_INP_MUX_RX_INT0_CFG1, 0,
			    rx_int_mix_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_2_enum, CDC_RX_INP_MUX_RX_INT1_CFG1, 0,
			    rx_int_mix_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_2_enum, CDC_RX_INP_MUX_RX_INT2_CFG1, 0,
			    rx_int_mix_mux_text);

static SOC_ENUM_SINGLE_DECL(rx_int0_1_mix_inp0_enum, CDC_RX_INP_MUX_RX_INT0_CFG0, 0,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int0_1_mix_inp1_enum, CDC_RX_INP_MUX_RX_INT0_CFG0, 4,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int0_1_mix_inp2_enum, CDC_RX_INP_MUX_RX_INT0_CFG1, 4,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_1_mix_inp0_enum, CDC_RX_INP_MUX_RX_INT1_CFG0, 0,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_1_mix_inp1_enum, CDC_RX_INP_MUX_RX_INT1_CFG0, 4,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_1_mix_inp2_enum, CDC_RX_INP_MUX_RX_INT1_CFG1, 4,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_1_mix_inp0_enum, CDC_RX_INP_MUX_RX_INT2_CFG0, 0,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_1_mix_inp1_enum, CDC_RX_INP_MUX_RX_INT2_CFG0, 4,
			    rx_prim_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_1_mix_inp2_enum, CDC_RX_INP_MUX_RX_INT2_CFG1, 4,
			    rx_prim_mix_text);

static SOC_ENUM_SINGLE_DECL(rx_int0_mix2_inp_enum, CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 2,
			    rx_sidetone_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_mix2_inp_enum, CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 4,
			    rx_sidetone_mix_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_mix2_inp_enum, CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 6,
			    rx_sidetone_mix_text);
static SOC_ENUM_SINGLE_DECL(iir0_inp0_enum, CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG0, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir0_inp1_enum, CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG1, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir0_inp2_enum, CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG2, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir0_inp3_enum, CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG3, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir1_inp0_enum, CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG0, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir1_inp1_enum, CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG1, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir1_inp2_enum, CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG2, 0,
			    iir_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(iir1_inp3_enum, CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG3, 0,
			    iir_inp_mux_text);

static SOC_ENUM_SINGLE_DECL(rx_int0_1_interp_enum, SND_SOC_NOPM, 0,
			    rx_int0_1_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_1_interp_enum, SND_SOC_NOPM, 0,
			    rx_int1_1_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_1_interp_enum, SND_SOC_NOPM, 0,
			    rx_int2_1_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int0_2_interp_enum, SND_SOC_NOPM, 0,
			    rx_int0_2_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_2_interp_enum, SND_SOC_NOPM, 0,
			    rx_int1_2_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int2_2_interp_enum, SND_SOC_NOPM, 0,
			    rx_int2_2_interp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int0_dem_inp_enum, CDC_RX_RX0_RX_PATH_CFG1, 0,
			    rx_int_dem_inp_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_int1_dem_inp_enum, CDC_RX_RX1_RX_PATH_CFG1, 0,
			    rx_int_dem_inp_mux_text);

static SOC_ENUM_SINGLE_DECL(rx_macro_rx0_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_macro_rx1_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_macro_rx2_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_macro_rx3_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_macro_rx4_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);
static SOC_ENUM_SINGLE_DECL(rx_macro_rx5_enum, SND_SOC_NOPM, 0, rx_macro_mux_text);

static const struct snd_kcontrol_new rx_mix_tx1_mux =
		SOC_DAPM_ENUM("RX MIX TX1_MUX Mux", rx_mix_tx1_mux_enum);
static const struct snd_kcontrol_new rx_mix_tx2_mux = 
		SOC_DAPM_ENUM("RX MIX TX2_MUX Mux", rx_mix_tx2_mux_enum);
static const struct snd_kcontrol_new rx_int0_2_mux =
		SOC_DAPM_ENUM("rx_int0_2", rx_int0_2_enum);
static const struct snd_kcontrol_new rx_int1_2_mux =
		SOC_DAPM_ENUM("rx_int1_2", rx_int1_2_enum);
static const struct snd_kcontrol_new rx_int2_2_mux =
		SOC_DAPM_ENUM("rx_int2_2", rx_int2_2_enum);
static const struct snd_kcontrol_new rx_int0_1_mix_inp0_mux =
		SOC_DAPM_ENUM("rx_int0_1_mix_inp0", rx_int0_1_mix_inp0_enum);
static const struct snd_kcontrol_new rx_int0_1_mix_inp1_mux =
		SOC_DAPM_ENUM("rx_int0_1_mix_inp1", rx_int0_1_mix_inp1_enum);
static const struct snd_kcontrol_new rx_int0_1_mix_inp2_mux =
		SOC_DAPM_ENUM("rx_int0_1_mix_inp2", rx_int0_1_mix_inp2_enum);
static const struct snd_kcontrol_new rx_int1_1_mix_inp0_mux =
		SOC_DAPM_ENUM("rx_int1_1_mix_inp0", rx_int1_1_mix_inp0_enum);
static const struct snd_kcontrol_new rx_int1_1_mix_inp1_mux =
		SOC_DAPM_ENUM("rx_int1_1_mix_inp1", rx_int1_1_mix_inp1_enum);
static const struct snd_kcontrol_new rx_int1_1_mix_inp2_mux =
		SOC_DAPM_ENUM("rx_int1_1_mix_inp2", rx_int1_1_mix_inp2_enum);
static const struct snd_kcontrol_new rx_int2_1_mix_inp0_mux =
		SOC_DAPM_ENUM("rx_int2_1_mix_inp0", rx_int2_1_mix_inp0_enum);
static const struct snd_kcontrol_new rx_int2_1_mix_inp1_mux =
		SOC_DAPM_ENUM("rx_int2_1_mix_inp1", rx_int2_1_mix_inp1_enum);
static const struct snd_kcontrol_new rx_int2_1_mix_inp2_mux =
		SOC_DAPM_ENUM("rx_int2_1_mix_inp2", rx_int2_1_mix_inp2_enum);
static const struct snd_kcontrol_new rx_int0_mix2_inp_mux =
		SOC_DAPM_ENUM("rx_int0_mix2_inp", rx_int0_mix2_inp_enum);
static const struct snd_kcontrol_new rx_int1_mix2_inp_mux =
		SOC_DAPM_ENUM("rx_int1_mix2_inp", rx_int1_mix2_inp_enum);
static const struct snd_kcontrol_new rx_int2_mix2_inp_mux =
		SOC_DAPM_ENUM("rx_int2_mix2_inp", rx_int2_mix2_inp_enum);
static const struct snd_kcontrol_new iir0_inp0_mux =
		SOC_DAPM_ENUM("iir0_inp0", iir0_inp0_enum);
static const struct snd_kcontrol_new iir0_inp1_mux =
		SOC_DAPM_ENUM("iir0_inp1", iir0_inp1_enum);
static const struct snd_kcontrol_new iir0_inp2_mux =
		SOC_DAPM_ENUM("iir0_inp2", iir0_inp2_enum);
static const struct snd_kcontrol_new iir0_inp3_mux =
		SOC_DAPM_ENUM("iir0_inp3", iir0_inp3_enum);
static const struct snd_kcontrol_new iir1_inp0_mux =
		SOC_DAPM_ENUM("iir1_inp0", iir1_inp0_enum);
static const struct snd_kcontrol_new iir1_inp1_mux =
		SOC_DAPM_ENUM("iir1_inp1", iir1_inp1_enum);
static const struct snd_kcontrol_new iir1_inp2_mux =
		SOC_DAPM_ENUM("iir1_inp2", iir1_inp2_enum);
static const struct snd_kcontrol_new iir1_inp3_mux =
		SOC_DAPM_ENUM("iir1_inp3", iir1_inp3_enum);
static const struct snd_kcontrol_new rx_int0_1_interp_mux =
		SOC_DAPM_ENUM("rx_int0_1_interp", rx_int0_1_interp_enum);
static const struct snd_kcontrol_new rx_int1_1_interp_mux =
		SOC_DAPM_ENUM("rx_int1_1_interp", rx_int1_1_interp_enum);
static const struct snd_kcontrol_new rx_int2_1_interp_mux =
		SOC_DAPM_ENUM("rx_int2_1_interp", rx_int2_1_interp_enum);
static const struct snd_kcontrol_new rx_int0_2_interp_mux =
		SOC_DAPM_ENUM("rx_int0_2_interp", rx_int0_2_interp_enum);
static const struct snd_kcontrol_new rx_int1_2_interp_mux =
		SOC_DAPM_ENUM("rx_int1_2_interp", rx_int1_2_interp_enum);
static const struct snd_kcontrol_new rx_int2_2_interp_mux =
		SOC_DAPM_ENUM("rx_int2_2_interp", rx_int2_2_interp_enum);
static const struct snd_kcontrol_new rx_mix_tx0_mux =
		SOC_DAPM_ENUM("RX MIX TX0_MUX Mux", rx_mix_tx0_mux_enum);

static const struct reg_default rx_defaults[] = {
	/* RX Macro */
	{ CDC_RX_TOP_TOP_CFG0, 0x00 },
	{ CDC_RX_TOP_SWR_CTRL, 0x00 },
	{ CDC_RX_TOP_DEBUG, 0x00 },
	{ CDC_RX_TOP_DEBUG_BUS, 0x00 },
	{ CDC_RX_TOP_DEBUG_EN0, 0x00 },
	{ CDC_RX_TOP_DEBUG_EN1, 0x00 },
	{ CDC_RX_TOP_DEBUG_EN2, 0x00 },
	{ CDC_RX_TOP_HPHL_COMP_WR_LSB, 0x00 },
	{ CDC_RX_TOP_HPHL_COMP_WR_MSB, 0x00 },
	{ CDC_RX_TOP_HPHL_COMP_LUT, 0x00 },
	{ CDC_RX_TOP_HPHL_COMP_RD_LSB, 0x00 },
	{ CDC_RX_TOP_HPHL_COMP_RD_MSB, 0x00 },
	{ CDC_RX_TOP_HPHR_COMP_WR_LSB, 0x00 },
	{ CDC_RX_TOP_HPHR_COMP_WR_MSB, 0x00 },
	{ CDC_RX_TOP_HPHR_COMP_LUT, 0x00 },
	{ CDC_RX_TOP_HPHR_COMP_RD_LSB, 0x00 },
	{ CDC_RX_TOP_HPHR_COMP_RD_MSB, 0x00 },
	{ CDC_RX_TOP_DSD0_DEBUG_CFG0, 0x11 },
	{ CDC_RX_TOP_DSD0_DEBUG_CFG1, 0x20 },
	{ CDC_RX_TOP_DSD0_DEBUG_CFG2, 0x00 },
	{ CDC_RX_TOP_DSD0_DEBUG_CFG3, 0x00 },
	{ CDC_RX_TOP_DSD1_DEBUG_CFG0, 0x11 },
	{ CDC_RX_TOP_DSD1_DEBUG_CFG1, 0x20 },
	{ CDC_RX_TOP_DSD1_DEBUG_CFG2, 0x00 },
	{ CDC_RX_TOP_DSD1_DEBUG_CFG3, 0x00 },
	{ CDC_RX_TOP_RX_I2S_CTL, 0x0C },
	{ CDC_RX_TOP_TX_I2S2_CTL, 0x0C },
	{ CDC_RX_TOP_I2S_CLK, 0x0C },
	{ CDC_RX_TOP_I2S_RESET, 0x00 },
	{ CDC_RX_TOP_I2S_MUX, 0x00 },
	{ CDC_RX_CLK_RST_CTRL_MCLK_CONTROL, 0x00 },
	{ CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL, 0x00 },
	{ CDC_RX_CLK_RST_CTRL_SWR_CONTROL, 0x00 },
	{ CDC_RX_CLK_RST_CTRL_DSD_CONTROL, 0x00 },
	{ CDC_RX_CLK_RST_CTRL_ASRC_SHARE_CONTROL, 0x08 },
	{ CDC_RX_SOFTCLIP_CRC, 0x00 },
	{ CDC_RX_SOFTCLIP_SOFTCLIP_CTRL, 0x38 },
	{ CDC_RX_INP_MUX_RX_INT0_CFG0, 0x00 },
	{ CDC_RX_INP_MUX_RX_INT0_CFG1, 0x00 },
	{ CDC_RX_INP_MUX_RX_INT1_CFG0, 0x00 },
	{ CDC_RX_INP_MUX_RX_INT1_CFG1, 0x00 },
	{ CDC_RX_INP_MUX_RX_INT2_CFG0, 0x00 },
	{ CDC_RX_INP_MUX_RX_INT2_CFG1, 0x00 },
	{ CDC_RX_INP_MUX_RX_MIX_CFG4, 0x00 },
	{ CDC_RX_INP_MUX_RX_MIX_CFG5, 0x00 },
	{ CDC_RX_INP_MUX_SIDETONE_SRC_CFG0, 0x00 },
	{ CDC_RX_CLSH_CRC, 0x00 },
	{ CDC_RX_CLSH_DLY_CTRL, 0x03 },
	{ CDC_RX_CLSH_DECAY_CTRL, 0x02 },
	{ CDC_RX_CLSH_HPH_V_PA, 0x1C },
	{ CDC_RX_CLSH_EAR_V_PA, 0x39 },
	{ CDC_RX_CLSH_HPH_V_HD, 0x0C },
	{ CDC_RX_CLSH_EAR_V_HD, 0x0C },
	{ CDC_RX_CLSH_K1_MSB, 0x01 },
	{ CDC_RX_CLSH_K1_LSB, 0x00 },
	{ CDC_RX_CLSH_K2_MSB, 0x00 },
	{ CDC_RX_CLSH_K2_LSB, 0x80 },
	{ CDC_RX_CLSH_IDLE_CTRL, 0x00 },
	{ CDC_RX_CLSH_IDLE_HPH, 0x00 },
	{ CDC_RX_CLSH_IDLE_EAR, 0x00 },
	{ CDC_RX_CLSH_TEST0, 0x07 },
	{ CDC_RX_CLSH_TEST1, 0x00 },
	{ CDC_RX_CLSH_OVR_VREF, 0x00 },
	{ CDC_RX_CLSH_CLSG_CTL, 0x02 },
	{ CDC_RX_CLSH_CLSG_CFG1, 0x9A },
	{ CDC_RX_CLSH_CLSG_CFG2, 0x10 },
	{ CDC_RX_BCL_VBAT_PATH_CTL, 0x00 },
	{ CDC_RX_BCL_VBAT_CFG, 0x10 },
	{ CDC_RX_BCL_VBAT_ADC_CAL1, 0x00 },
	{ CDC_RX_BCL_VBAT_ADC_CAL2, 0x00 },
	{ CDC_RX_BCL_VBAT_ADC_CAL3, 0x04 },
	{ CDC_RX_BCL_VBAT_PK_EST1, 0xE0 },
	{ CDC_RX_BCL_VBAT_PK_EST2, 0x01 },
	{ CDC_RX_BCL_VBAT_PK_EST3, 0x40 },
	{ CDC_RX_BCL_VBAT_RF_PROC1, 0x2A },
	{ CDC_RX_BCL_VBAT_RF_PROC1, 0x00 },
	{ CDC_RX_BCL_VBAT_TAC1, 0x00 },
	{ CDC_RX_BCL_VBAT_TAC2, 0x18 },
	{ CDC_RX_BCL_VBAT_TAC3, 0x18 },
	{ CDC_RX_BCL_VBAT_TAC4, 0x03 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD1, 0x01 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD2, 0x00 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD3, 0x00 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD4, 0x64 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD5, 0x01 },
	{ CDC_RX_BCL_VBAT_DEBUG1, 0x00 },
	{ CDC_RX_BCL_VBAT_GAIN_UPD_MON, 0x00 },
	{ CDC_RX_BCL_VBAT_GAIN_MON_VAL, 0x00 },
	{ CDC_RX_BCL_VBAT_BAN, 0x0C },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD1, 0x00 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD2, 0x77 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD3, 0x01 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD4, 0x00 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD5, 0x4B },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD6, 0x00 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD7, 0x01 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD8, 0x00 },
	{ CDC_RX_BCL_VBAT_BCL_GAIN_UPD9, 0x00 },
	{ CDC_RX_BCL_VBAT_ATTN1, 0x04 },
	{ CDC_RX_BCL_VBAT_ATTN2, 0x08 },
	{ CDC_RX_BCL_VBAT_ATTN3, 0x0C },
	{ CDC_RX_BCL_VBAT_DECODE_CTL1, 0xE0 },
	{ CDC_RX_BCL_VBAT_DECODE_CTL2, 0x00 },
	{ CDC_RX_BCL_VBAT_DECODE_CFG1, 0x00 },
	{ CDC_RX_BCL_VBAT_DECODE_CFG2, 0x00 },
	{ CDC_RX_BCL_VBAT_DECODE_CFG3, 0x00 },
	{ CDC_RX_BCL_VBAT_DECODE_CFG4, 0x00 },
	{ CDC_RX_BCL_VBAT_DECODE_ST, 0x00 },
	{ CDC_RX_INTR_CTRL_CFG, 0x00 },
	{ CDC_RX_INTR_CTRL_CLR_COMMIT, 0x00 },
	{ CDC_RX_INTR_CTRL_PIN1_MASK0, 0xFF },
	{ CDC_RX_INTR_CTRL_PIN1_STATUS0, 0x00 },
	{ CDC_RX_INTR_CTRL_PIN1_CLEAR0, 0x00 },
	{ CDC_RX_INTR_CTRL_PIN2_MASK0, 0xFF },
	{ CDC_RX_INTR_CTRL_PIN2_STATUS0, 0x00 },
	{ CDC_RX_INTR_CTRL_PIN2_CLEAR0, 0x00 },
	{ CDC_RX_INTR_CTRL_LEVEL0, 0x00 },
	{ CDC_RX_INTR_CTRL_BYPASS0, 0x00 },
	{ CDC_RX_INTR_CTRL_SET0, 0x00 },
	{ CDC_RX_RX0_RX_PATH_CTL, 0x04 },
	{ CDC_RX_RX0_RX_PATH_CFG0, 0x00 },
	{ CDC_RX_RX0_RX_PATH_CFG1, 0x64 },
	{ CDC_RX_RX0_RX_PATH_CFG2, 0x8F },
	{ CDC_RX_RX0_RX_PATH_CFG3, 0x00 },
	{ CDC_RX_RX0_RX_VOL_CTL, 0x00 },
	{ CDC_RX_RX0_RX_PATH_MIX_CTL, 0x04 },
	{ CDC_RX_RX0_RX_PATH_MIX_CFG, 0x7E },
	{ CDC_RX_RX0_RX_VOL_MIX_CTL, 0x00 },
	{ CDC_RX_RX0_RX_PATH_SEC1, 0x08 },
	{ CDC_RX_RX0_RX_PATH_SEC2, 0x00 },
	{ CDC_RX_RX0_RX_PATH_SEC3, 0x00 },
	{ CDC_RX_RX0_RX_PATH_SEC4, 0x00 },
	{ CDC_RX_RX0_RX_PATH_SEC7, 0x00 },
	{ CDC_RX_RX0_RX_PATH_MIX_SEC0, 0x08 },
	{ CDC_RX_RX0_RX_PATH_MIX_SEC1, 0x00 },
	{ CDC_RX_RX0_RX_PATH_DSM_CTL, 0x08 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA1, 0x00 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA2, 0x00 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA3, 0x00 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA4, 0x55 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA5, 0x55 },
	{ CDC_RX_RX0_RX_PATH_DSM_DATA6, 0x55 },
	{ CDC_RX_RX1_RX_PATH_CTL, 0x04 },
	{ CDC_RX_RX1_RX_PATH_CFG0, 0x00 },
	{ CDC_RX_RX1_RX_PATH_CFG1, 0x64 },
	{ CDC_RX_RX1_RX_PATH_CFG2, 0x8F },
	{ CDC_RX_RX1_RX_PATH_CFG3, 0x00 },
	{ CDC_RX_RX1_RX_VOL_CTL, 0x00 },
	{ CDC_RX_RX1_RX_PATH_MIX_CTL, 0x04 },
	{ CDC_RX_RX1_RX_PATH_MIX_CFG, 0x7E },
	{ CDC_RX_RX1_RX_VOL_MIX_CTL, 0x00 },
	{ CDC_RX_RX1_RX_PATH_SEC1, 0x08 },
	{ CDC_RX_RX1_RX_PATH_SEC2, 0x00 },
	{ CDC_RX_RX1_RX_PATH_SEC3, 0x00 },
	{ CDC_RX_RX1_RX_PATH_SEC4, 0x00 },
	{ CDC_RX_RX1_RX_PATH_SEC7, 0x00 },
	{ CDC_RX_RX1_RX_PATH_MIX_SEC0, 0x08 },
	{ CDC_RX_RX1_RX_PATH_MIX_SEC1, 0x00 },
	{ CDC_RX_RX1_RX_PATH_DSM_CTL, 0x08 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA1, 0x00 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA2, 0x00 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA3, 0x00 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA4, 0x55 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA5, 0x55 },
	{ CDC_RX_RX1_RX_PATH_DSM_DATA6, 0x55 },
	{ CDC_RX_RX2_RX_PATH_CTL, 0x04 },
	{ CDC_RX_RX2_RX_PATH_CFG0, 0x00 },
	{ CDC_RX_RX2_RX_PATH_CFG1, 0x64 },
	{ CDC_RX_RX2_RX_PATH_CFG2, 0x8F },
	{ CDC_RX_RX2_RX_PATH_CFG3, 0x00 },
	{ CDC_RX_RX2_RX_VOL_CTL, 0x00 },
	{ CDC_RX_RX2_RX_PATH_MIX_CTL, 0x04 },
	{ CDC_RX_RX2_RX_PATH_MIX_CFG, 0x7E },
	{ CDC_RX_RX2_RX_VOL_MIX_CTL, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC0, 0x04 },
	{ CDC_RX_RX2_RX_PATH_SEC1, 0x08 },
	{ CDC_RX_RX2_RX_PATH_SEC2, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC3, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC4, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC5, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC6, 0x00 },
	{ CDC_RX_RX2_RX_PATH_SEC7, 0x00 },
	{ CDC_RX_RX2_RX_PATH_MIX_SEC0, 0x08 },
	{ CDC_RX_RX2_RX_PATH_MIX_SEC1, 0x00 },
	{ CDC_RX_RX2_RX_PATH_DSM_CTL, 0x00 },
	{ CDC_RX_IDLE_DETECT_PATH_CTL, 0x00 },
	{ CDC_RX_IDLE_DETECT_CFG0, 0x07 },
	{ CDC_RX_IDLE_DETECT_CFG1, 0x3C },
	{ CDC_RX_IDLE_DETECT_CFG2, 0x00 },
	{ CDC_RX_IDLE_DETECT_CFG3, 0x00 },
	{ CDC_RX_COMPANDER0_CTL0, 0x60 },
	{ CDC_RX_COMPANDER0_CTL1, 0xDB },
	{ CDC_RX_COMPANDER0_CTL2, 0xFF },
	{ CDC_RX_COMPANDER0_CTL3, 0x35 },
	{ CDC_RX_COMPANDER0_CTL4, 0xFF },
	{ CDC_RX_COMPANDER0_CTL5, 0x00 },
	{ CDC_RX_COMPANDER0_CTL6, 0x01 },
	{ CDC_RX_COMPANDER0_CTL7, 0x28 },
	{ CDC_RX_COMPANDER1_CTL0, 0x60 },
	{ CDC_RX_COMPANDER1_CTL1, 0xDB },
	{ CDC_RX_COMPANDER1_CTL2, 0xFF },
	{ CDC_RX_COMPANDER1_CTL3, 0x35 },
	{ CDC_RX_COMPANDER1_CTL4, 0xFF },
	{ CDC_RX_COMPANDER1_CTL5, 0x00 },
	{ CDC_RX_COMPANDER1_CTL6, 0x01 },
	{ CDC_RX_COMPANDER1_CTL7, 0x28 },
	{ CDC_RX_SIDETONE_IIR0_IIR_PATH_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B5_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B6_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B7_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_B8_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_CTL, 0x40 },
	{ CDC_RX_SIDETONE_IIR0_IIR_GAIN_TIMER_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_COEF_B1_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR0_IIR_COEF_B2_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_PATH_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B5_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B6_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B7_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_B8_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_CTL, 0x40 },
	{ CDC_RX_SIDETONE_IIR1_IIR_GAIN_TIMER_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_COEF_B1_CTL, 0x00 },
	{ CDC_RX_SIDETONE_IIR1_IIR_COEF_B2_CTL, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG0, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG1, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG2, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG3, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG0, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG1, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG2, 0x00 },
	{ CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG3, 0x00 },
	{ CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CTL, 0x04 },
	{ CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CFG1, 0x00 },
	{ CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CTL, 0x04 },
	{ CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CFG1, 0x00 },
	{ CDC_RX_EC_REF_HQ0_EC_REF_HQ_PATH_CTL, 0x00 },
	{ CDC_RX_EC_REF_HQ0_EC_REF_HQ_CFG0, 0x01 },
	{ CDC_RX_EC_REF_HQ1_EC_REF_HQ_PATH_CTL, 0x00 },
	{ CDC_RX_EC_REF_HQ1_EC_REF_HQ_CFG0, 0x01 },
	{ CDC_RX_EC_REF_HQ2_EC_REF_HQ_PATH_CTL, 0x00 },
	{ CDC_RX_EC_REF_HQ2_EC_REF_HQ_CFG0, 0x01 },
	{ CDC_RX_EC_ASRC0_CLK_RST_CTL, 0x00 },
	{ CDC_RX_EC_ASRC0_CTL0, 0x00 },
	{ CDC_RX_EC_ASRC0_CTL1, 0x00 },
	{ CDC_RX_EC_ASRC0_FIFO_CTL, 0xA8 },
	{ CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC0_STATUS_FIFO, 0x00 },
	{ CDC_RX_EC_ASRC1_CLK_RST_CTL, 0x00 },
	{ CDC_RX_EC_ASRC1_CTL0, 0x00 },
	{ CDC_RX_EC_ASRC1_CTL1, 0x00 },
	{ CDC_RX_EC_ASRC1_FIFO_CTL, 0xA8 },
	{ CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC1_STATUS_FIFO, 0x00 },
	{ CDC_RX_EC_ASRC2_CLK_RST_CTL, 0x00 },
	{ CDC_RX_EC_ASRC2_CTL0, 0x00 },
	{ CDC_RX_EC_ASRC2_CTL1, 0x00 },
	{ CDC_RX_EC_ASRC2_FIFO_CTL, 0xA8 },
	{ CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_LSB, 0x00 },
	{ CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_MSB, 0x00 },
	{ CDC_RX_EC_ASRC2_STATUS_FIFO, 0x00 },
	{ CDC_RX_DSD0_PATH_CTL, 0x00 },
	{ CDC_RX_DSD0_CFG0, 0x00 },
	{ CDC_RX_DSD0_CFG1, 0x62 },
	{ CDC_RX_DSD0_CFG2, 0x96 },
	{ CDC_RX_DSD1_PATH_CTL, 0x00 },
	{ CDC_RX_DSD1_CFG0, 0x00 },
	{ CDC_RX_DSD1_CFG1, 0x62 },
	{ CDC_RX_DSD1_CFG2, 0x96 },
};

static bool rx_is_wronly_register(struct device *dev,
					unsigned int reg)
{
	switch (reg) {
	case CDC_RX_BCL_VBAT_GAIN_UPD_MON:
	case CDC_RX_INTR_CTRL_CLR_COMMIT:
	case CDC_RX_INTR_CTRL_PIN1_CLEAR0:
	case CDC_RX_INTR_CTRL_PIN2_CLEAR0:
		return true;
	}

	return false;
}

static bool rx_is_volatile_register(struct device *dev, unsigned int reg)
{
	/* Update volatile list for rx/tx macros */
	switch (reg) {
	case CDC_RX_TOP_HPHL_COMP_RD_LSB:
	case CDC_RX_TOP_HPHL_COMP_WR_LSB:
	case CDC_RX_TOP_HPHL_COMP_RD_MSB:
	case CDC_RX_TOP_HPHL_COMP_WR_MSB:
	case CDC_RX_TOP_HPHR_COMP_RD_LSB:
	case CDC_RX_TOP_HPHR_COMP_WR_LSB:
	case CDC_RX_TOP_HPHR_COMP_RD_MSB:
	case CDC_RX_TOP_HPHR_COMP_WR_MSB:
	case CDC_RX_TOP_DSD0_DEBUG_CFG2:
	case CDC_RX_TOP_DSD1_DEBUG_CFG2:
	case CDC_RX_BCL_VBAT_GAIN_MON_VAL:
	case CDC_RX_BCL_VBAT_DECODE_ST:
	case CDC_RX_INTR_CTRL_PIN1_STATUS0:
	case CDC_RX_INTR_CTRL_PIN2_STATUS0:
	case CDC_RX_COMPANDER0_CTL6:
	case CDC_RX_COMPANDER1_CTL6:
	case CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC0_STATUS_FIFO:
	case CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC1_STATUS_FIFO:
	case CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC2_STATUS_FIFO:
		return true;
	}
	return false;
}

static bool rx_is_rw_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_RX_TOP_TOP_CFG0:
	case CDC_RX_TOP_SWR_CTRL:
	case CDC_RX_TOP_DEBUG:
	case CDC_RX_TOP_DEBUG_BUS:
	case CDC_RX_TOP_DEBUG_EN0:
	case CDC_RX_TOP_DEBUG_EN1:
	case CDC_RX_TOP_DEBUG_EN2:
	case CDC_RX_TOP_HPHL_COMP_WR_LSB:
	case CDC_RX_TOP_HPHL_COMP_WR_MSB:
	case CDC_RX_TOP_HPHL_COMP_LUT:
	case CDC_RX_TOP_HPHR_COMP_WR_LSB:
	case CDC_RX_TOP_HPHR_COMP_WR_MSB:
	case CDC_RX_TOP_HPHR_COMP_LUT:
	case CDC_RX_TOP_DSD0_DEBUG_CFG0:
	case CDC_RX_TOP_DSD0_DEBUG_CFG1:
	case CDC_RX_TOP_DSD0_DEBUG_CFG3:
	case CDC_RX_TOP_DSD1_DEBUG_CFG0:
	case CDC_RX_TOP_DSD1_DEBUG_CFG1:
	case CDC_RX_TOP_DSD1_DEBUG_CFG3:
	case CDC_RX_TOP_RX_I2S_CTL:
	case CDC_RX_TOP_TX_I2S2_CTL:
	case CDC_RX_TOP_I2S_CLK:
	case CDC_RX_TOP_I2S_RESET:
	case CDC_RX_TOP_I2S_MUX:
	case CDC_RX_CLK_RST_CTRL_MCLK_CONTROL:
	case CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL:
	case CDC_RX_CLK_RST_CTRL_SWR_CONTROL:
	case CDC_RX_CLK_RST_CTRL_DSD_CONTROL:
	case CDC_RX_CLK_RST_CTRL_ASRC_SHARE_CONTROL:
	case CDC_RX_SOFTCLIP_CRC:
	case CDC_RX_SOFTCLIP_SOFTCLIP_CTRL:
	case CDC_RX_INP_MUX_RX_INT0_CFG0:
	case CDC_RX_INP_MUX_RX_INT0_CFG1:
	case CDC_RX_INP_MUX_RX_INT1_CFG0:
	case CDC_RX_INP_MUX_RX_INT1_CFG1:
	case CDC_RX_INP_MUX_RX_INT2_CFG0:
	case CDC_RX_INP_MUX_RX_INT2_CFG1:
	case CDC_RX_INP_MUX_RX_MIX_CFG4:
	case CDC_RX_INP_MUX_RX_MIX_CFG5:
	case CDC_RX_INP_MUX_SIDETONE_SRC_CFG0:
	case CDC_RX_CLSH_CRC:
	case CDC_RX_CLSH_DLY_CTRL:
	case CDC_RX_CLSH_DECAY_CTRL:
	case CDC_RX_CLSH_HPH_V_PA:
	case CDC_RX_CLSH_EAR_V_PA:
	case CDC_RX_CLSH_HPH_V_HD:
	case CDC_RX_CLSH_EAR_V_HD:
	case CDC_RX_CLSH_K1_MSB:
	case CDC_RX_CLSH_K1_LSB:
	case CDC_RX_CLSH_K2_MSB:
	case CDC_RX_CLSH_K2_LSB:
	case CDC_RX_CLSH_IDLE_CTRL:
	case CDC_RX_CLSH_IDLE_HPH:
	case CDC_RX_CLSH_IDLE_EAR:
	case CDC_RX_CLSH_TEST0:
	case CDC_RX_CLSH_TEST1:
	case CDC_RX_CLSH_OVR_VREF:
	case CDC_RX_CLSH_CLSG_CTL:
	case CDC_RX_CLSH_CLSG_CFG1:
	case CDC_RX_CLSH_CLSG_CFG2:
	case CDC_RX_BCL_VBAT_PATH_CTL:
	case CDC_RX_BCL_VBAT_CFG:
	case CDC_RX_BCL_VBAT_ADC_CAL1:
	case CDC_RX_BCL_VBAT_ADC_CAL2:
	case CDC_RX_BCL_VBAT_ADC_CAL3:
	case CDC_RX_BCL_VBAT_PK_EST1:
	case CDC_RX_BCL_VBAT_PK_EST2:
	case CDC_RX_BCL_VBAT_PK_EST3:
	case CDC_RX_BCL_VBAT_RF_PROC1:
	case CDC_RX_BCL_VBAT_RF_PROC2:
	case CDC_RX_BCL_VBAT_TAC1:
	case CDC_RX_BCL_VBAT_TAC2:
	case CDC_RX_BCL_VBAT_TAC3:
	case CDC_RX_BCL_VBAT_TAC4:
	case CDC_RX_BCL_VBAT_GAIN_UPD1:
	case CDC_RX_BCL_VBAT_GAIN_UPD2:
	case CDC_RX_BCL_VBAT_GAIN_UPD3:
	case CDC_RX_BCL_VBAT_GAIN_UPD4:
	case CDC_RX_BCL_VBAT_GAIN_UPD5:
	case CDC_RX_BCL_VBAT_DEBUG1:
	case CDC_RX_BCL_VBAT_BAN:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD1:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD2:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD3:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD4:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD5:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD6:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD7:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD8:
	case CDC_RX_BCL_VBAT_BCL_GAIN_UPD9:
	case CDC_RX_BCL_VBAT_ATTN1:
	case CDC_RX_BCL_VBAT_ATTN2:
	case CDC_RX_BCL_VBAT_ATTN3:
	case CDC_RX_BCL_VBAT_DECODE_CTL1:
	case CDC_RX_BCL_VBAT_DECODE_CTL2:
	case CDC_RX_BCL_VBAT_DECODE_CFG1:
	case CDC_RX_BCL_VBAT_DECODE_CFG2:
	case CDC_RX_BCL_VBAT_DECODE_CFG3:
	case CDC_RX_BCL_VBAT_DECODE_CFG4:
	case CDC_RX_INTR_CTRL_CFG:
	case CDC_RX_INTR_CTRL_PIN1_MASK0:
	case CDC_RX_INTR_CTRL_PIN2_MASK0:
	case CDC_RX_INTR_CTRL_LEVEL0:
	case CDC_RX_INTR_CTRL_BYPASS0:
	case CDC_RX_INTR_CTRL_SET0:
	case CDC_RX_RX0_RX_PATH_CTL:
	case CDC_RX_RX0_RX_PATH_CFG0:
	case CDC_RX_RX0_RX_PATH_CFG1:
	case CDC_RX_RX0_RX_PATH_CFG2:
	case CDC_RX_RX0_RX_PATH_CFG3:
	case CDC_RX_RX0_RX_VOL_CTL:
	case CDC_RX_RX0_RX_PATH_MIX_CTL:
	case CDC_RX_RX0_RX_PATH_MIX_CFG:
	case CDC_RX_RX0_RX_VOL_MIX_CTL:
	case CDC_RX_RX0_RX_PATH_SEC1:
	case CDC_RX_RX0_RX_PATH_SEC2:
	case CDC_RX_RX0_RX_PATH_SEC3:
	case CDC_RX_RX0_RX_PATH_SEC4:
	case CDC_RX_RX0_RX_PATH_SEC7:
	case CDC_RX_RX0_RX_PATH_MIX_SEC0:
	case CDC_RX_RX0_RX_PATH_MIX_SEC1:
	case CDC_RX_RX0_RX_PATH_DSM_CTL:
	case CDC_RX_RX0_RX_PATH_DSM_DATA1:
	case CDC_RX_RX0_RX_PATH_DSM_DATA2:
	case CDC_RX_RX0_RX_PATH_DSM_DATA3:
	case CDC_RX_RX0_RX_PATH_DSM_DATA4:
	case CDC_RX_RX0_RX_PATH_DSM_DATA5:
	case CDC_RX_RX0_RX_PATH_DSM_DATA6:
	case CDC_RX_RX1_RX_PATH_CTL:
	case CDC_RX_RX1_RX_PATH_CFG0:
	case CDC_RX_RX1_RX_PATH_CFG1:
	case CDC_RX_RX1_RX_PATH_CFG2:
	case CDC_RX_RX1_RX_PATH_CFG3:
	case CDC_RX_RX1_RX_VOL_CTL:
	case CDC_RX_RX1_RX_PATH_MIX_CTL:
	case CDC_RX_RX1_RX_PATH_MIX_CFG:
	case CDC_RX_RX1_RX_VOL_MIX_CTL:
	case CDC_RX_RX1_RX_PATH_SEC1:
	case CDC_RX_RX1_RX_PATH_SEC2:
	case CDC_RX_RX1_RX_PATH_SEC3:
	case CDC_RX_RX1_RX_PATH_SEC4:
	case CDC_RX_RX1_RX_PATH_SEC7:
	case CDC_RX_RX1_RX_PATH_MIX_SEC0:
	case CDC_RX_RX1_RX_PATH_MIX_SEC1:
	case CDC_RX_RX1_RX_PATH_DSM_CTL:
	case CDC_RX_RX1_RX_PATH_DSM_DATA1:
	case CDC_RX_RX1_RX_PATH_DSM_DATA2:
	case CDC_RX_RX1_RX_PATH_DSM_DATA3:
	case CDC_RX_RX1_RX_PATH_DSM_DATA4:
	case CDC_RX_RX1_RX_PATH_DSM_DATA5:
	case CDC_RX_RX1_RX_PATH_DSM_DATA6:
	case CDC_RX_RX2_RX_PATH_CTL:
	case CDC_RX_RX2_RX_PATH_CFG0:
	case CDC_RX_RX2_RX_PATH_CFG1:
	case CDC_RX_RX2_RX_PATH_CFG2:
	case CDC_RX_RX2_RX_PATH_CFG3:
	case CDC_RX_RX2_RX_VOL_CTL:
	case CDC_RX_RX2_RX_PATH_MIX_CTL:
	case CDC_RX_RX2_RX_PATH_MIX_CFG:
	case CDC_RX_RX2_RX_VOL_MIX_CTL:
	case CDC_RX_RX2_RX_PATH_SEC0:
	case CDC_RX_RX2_RX_PATH_SEC1:
	case CDC_RX_RX2_RX_PATH_SEC2:
	case CDC_RX_RX2_RX_PATH_SEC3:
	case CDC_RX_RX2_RX_PATH_SEC4:
	case CDC_RX_RX2_RX_PATH_SEC5:
	case CDC_RX_RX2_RX_PATH_SEC6:
	case CDC_RX_RX2_RX_PATH_SEC7:
	case CDC_RX_RX2_RX_PATH_MIX_SEC0:
	case CDC_RX_RX2_RX_PATH_MIX_SEC1:
	case CDC_RX_RX2_RX_PATH_DSM_CTL:
	case CDC_RX_IDLE_DETECT_PATH_CTL:
	case CDC_RX_IDLE_DETECT_CFG0:
	case CDC_RX_IDLE_DETECT_CFG1:
	case CDC_RX_IDLE_DETECT_CFG2:
	case CDC_RX_IDLE_DETECT_CFG3:
	case CDC_RX_COMPANDER0_CTL0:
	case CDC_RX_COMPANDER0_CTL1:
	case CDC_RX_COMPANDER0_CTL2:
	case CDC_RX_COMPANDER0_CTL3:
	case CDC_RX_COMPANDER0_CTL4:
	case CDC_RX_COMPANDER0_CTL5:
	case CDC_RX_COMPANDER0_CTL7:
	case CDC_RX_COMPANDER1_CTL0:
	case CDC_RX_COMPANDER1_CTL1:
	case CDC_RX_COMPANDER1_CTL2:
	case CDC_RX_COMPANDER1_CTL3:
	case CDC_RX_COMPANDER1_CTL4:
	case CDC_RX_COMPANDER1_CTL5:
	case CDC_RX_COMPANDER1_CTL7:
	case CDC_RX_SIDETONE_IIR0_IIR_PATH_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B5_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B6_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B7_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_B8_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_GAIN_TIMER_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_COEF_B1_CTL:
	case CDC_RX_SIDETONE_IIR0_IIR_COEF_B2_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_PATH_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B5_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B6_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B7_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_B8_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_GAIN_TIMER_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_COEF_B1_CTL:
	case CDC_RX_SIDETONE_IIR1_IIR_COEF_B2_CTL:
	case CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG0:
	case CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG1:
	case CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG2:
	case CDC_RX_IIR_INP_MUX_IIR0_MIX_CFG3:
	case CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG0:
	case CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG1:
	case CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG2:
	case CDC_RX_IIR_INP_MUX_IIR1_MIX_CFG3:
	case CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CTL:
	case CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CFG1:
	case CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CTL:
	case CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CFG1:
	case CDC_RX_EC_REF_HQ0_EC_REF_HQ_PATH_CTL:
	case CDC_RX_EC_REF_HQ0_EC_REF_HQ_CFG0:
	case CDC_RX_EC_REF_HQ1_EC_REF_HQ_PATH_CTL:
	case CDC_RX_EC_REF_HQ1_EC_REF_HQ_CFG0:
	case CDC_RX_EC_REF_HQ2_EC_REF_HQ_PATH_CTL:
	case CDC_RX_EC_REF_HQ2_EC_REF_HQ_CFG0:
	case CDC_RX_EC_ASRC0_CLK_RST_CTL:
	case CDC_RX_EC_ASRC0_CTL0:
	case CDC_RX_EC_ASRC0_CTL1:
	case CDC_RX_EC_ASRC0_FIFO_CTL:
	case CDC_RX_EC_ASRC1_CLK_RST_CTL:
	case CDC_RX_EC_ASRC1_CTL0:
	case CDC_RX_EC_ASRC1_CTL1:
	case CDC_RX_EC_ASRC1_FIFO_CTL:
	case CDC_RX_EC_ASRC2_CLK_RST_CTL:
	case CDC_RX_EC_ASRC2_CTL0:
	case CDC_RX_EC_ASRC2_CTL1:
	case CDC_RX_EC_ASRC2_FIFO_CTL:
	case CDC_RX_DSD0_PATH_CTL:
	case CDC_RX_DSD0_CFG0:
	case CDC_RX_DSD0_CFG1:
	case CDC_RX_DSD0_CFG2:
	case CDC_RX_DSD1_PATH_CTL:
	case CDC_RX_DSD1_CFG0:
	case CDC_RX_DSD1_CFG1:
	case CDC_RX_DSD1_CFG2:
		return true;
	}

	return false;
}

static bool rx_is_writeable_register(struct device *dev, unsigned int reg)
{
	bool ret;

	ret = rx_is_rw_register(dev, reg);
	if (!ret)
		return rx_is_wronly_register(dev, reg);

	return ret;
}

static bool rx_is_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_RX_TOP_HPHL_COMP_RD_LSB:
	case CDC_RX_TOP_HPHL_COMP_RD_MSB:
	case CDC_RX_TOP_HPHR_COMP_RD_LSB:
	case CDC_RX_TOP_HPHR_COMP_RD_MSB:
	case CDC_RX_TOP_DSD0_DEBUG_CFG2:
	case CDC_RX_TOP_DSD1_DEBUG_CFG2:
	case CDC_RX_BCL_VBAT_GAIN_MON_VAL:
	case CDC_RX_BCL_VBAT_DECODE_ST:
	case CDC_RX_INTR_CTRL_PIN1_STATUS0:
	case CDC_RX_INTR_CTRL_PIN2_STATUS0:
	case CDC_RX_COMPANDER0_CTL6:
	case CDC_RX_COMPANDER1_CTL6:
	case CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC0_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC0_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC0_STATUS_FIFO:
	case CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC1_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC1_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC1_STATUS_FIFO:
	case CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_LSB:
	case CDC_RX_EC_ASRC2_STATUS_FMIN_CNTR_MSB:
	case CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_LSB:
	case CDC_RX_EC_ASRC2_STATUS_FMAX_CNTR_MSB:
	case CDC_RX_EC_ASRC2_STATUS_FIFO:
		return true;
	}

	return rx_is_rw_register(dev, reg);
}

static const struct regmap_config rx_regmap_config = {
	.name = "rx_macro",
	.reg_bits = 16,
	.val_bits = 32, /* 8 but with 32 bit read/write */
	.reg_stride = 4,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = rx_defaults,
	.num_reg_defaults = ARRAY_SIZE(rx_defaults),
	.max_register = RX_MAX_OFFSET,
	.writeable_reg = rx_is_writeable_register,
	.volatile_reg = rx_is_volatile_register,
	.readable_reg = rx_is_readable_register,
};

static int rx_macro_int_dem_inp_mux_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short look_ahead_dly_reg;
	unsigned int val;

	val = ucontrol->value.enumerated.item[0];

	if (e->reg == CDC_RX_RX0_RX_PATH_CFG1)
		look_ahead_dly_reg = CDC_RX_RX0_RX_PATH_CFG0;
	else if (e->reg == CDC_RX_RX1_RX_PATH_CFG1)
		look_ahead_dly_reg = CDC_RX_RX1_RX_PATH_CFG0;

	/* Set Look Ahead Delay */
	if (val)
		snd_soc_component_update_bits(component, look_ahead_dly_reg,
					      CDC_RX_DLY_ZN_EN_MASK,
					      CDC_RX_DLY_ZN_ENABLE);
	else
		snd_soc_component_update_bits(component, look_ahead_dly_reg,
					      CDC_RX_DLY_ZN_EN_MASK, 0);
	/* Set DEM INP Select */
	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static const struct snd_kcontrol_new rx_int0_dem_inp_mux =
		SOC_DAPM_ENUM_EXT("rx_int0_dem_inp", rx_int0_dem_inp_enum,
		  snd_soc_dapm_get_enum_double, rx_macro_int_dem_inp_mux_put);
static const struct snd_kcontrol_new rx_int1_dem_inp_mux =
		SOC_DAPM_ENUM_EXT("rx_int1_dem_inp", rx_int1_dem_inp_enum,
		  snd_soc_dapm_get_enum_double, rx_macro_int_dem_inp_mux_put);

static int rx_macro_set_prim_interpolator_rate(struct snd_soc_dai *dai,
					       int rate_reg_val, u32 sample_rate)
{

	u8 int_1_mix1_inp;
	u32 j, port;
	u16 int_mux_cfg0, int_mux_cfg1;
	u16 int_fs_reg;
	u8 inp0_sel, inp1_sel, inp2_sel;
	struct snd_soc_component *component = dai->component;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	for_each_set_bit(port, &rx->active_ch_mask[dai->id], RX_MACRO_PORTS_MAX) {
		int_1_mix1_inp = port;
		int_mux_cfg0 = CDC_RX_INP_MUX_RX_INT0_CFG0;
		/*
		 * Loop through all interpolator MUX inputs and find out
		 * to which interpolator input, the rx port
		 * is connected
		 */
		for (j = 0; j < INTERP_MAX; j++) {
			int_mux_cfg1 = int_mux_cfg0 + 4;

			inp0_sel = snd_soc_component_read_field(component, int_mux_cfg0,
								CDC_RX_INTX_1_MIX_INP0_SEL_MASK);
			inp1_sel = snd_soc_component_read_field(component, int_mux_cfg0,
								CDC_RX_INTX_1_MIX_INP1_SEL_MASK);
			inp2_sel = snd_soc_component_read_field(component, int_mux_cfg1,
								CDC_RX_INTX_1_MIX_INP2_SEL_MASK);

			if ((inp0_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0) ||
			    (inp1_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0) ||
			    (inp2_sel == int_1_mix1_inp + INTn_1_INP_SEL_RX0)) {
				int_fs_reg = CDC_RX_RXn_RX_PATH_CTL(j);
				/* sample_rate is in Hz */
				snd_soc_component_update_bits(component, int_fs_reg,
							      CDC_RX_PATH_PCM_RATE_MASK,
							      rate_reg_val);
			}
			int_mux_cfg0 += 8;
		}
	}

	return 0;
}

static int rx_macro_set_mix_interpolator_rate(struct snd_soc_dai *dai,
					      int rate_reg_val, u32 sample_rate)
{

	u8 int_2_inp;
	u32 j, port;
	u16 int_mux_cfg1, int_fs_reg;
	u8 int_mux_cfg1_val;
	struct snd_soc_component *component = dai->component;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	for_each_set_bit(port, &rx->active_ch_mask[dai->id], RX_MACRO_PORTS_MAX) {
		int_2_inp = port;

		int_mux_cfg1 = CDC_RX_INP_MUX_RX_INT0_CFG1;
		for (j = 0; j < INTERP_MAX; j++) {
			int_mux_cfg1_val = snd_soc_component_read_field(component, int_mux_cfg1,
									CDC_RX_INTX_2_SEL_MASK);

			if (int_mux_cfg1_val == int_2_inp + INTn_2_INP_SEL_RX0) {
				int_fs_reg = CDC_RX_RXn_RX_PATH_MIX_CTL(j);
				snd_soc_component_update_bits(component, int_fs_reg,
							      CDC_RX_RXn_MIX_PCM_RATE_MASK,
							      rate_reg_val);
			}
			int_mux_cfg1 += 8;
		}
	}
	return 0;
}

static int rx_macro_set_interpolator_rate(struct snd_soc_dai *dai,
					  u32 sample_rate)
{
	int rate_val = 0;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(sr_val_tbl); i++)
		if (sample_rate == sr_val_tbl[i].sample_rate)
			rate_val = sr_val_tbl[i].rate_val;

	ret = rx_macro_set_prim_interpolator_rate(dai, rate_val, sample_rate);
	if (ret)
		return ret;

	ret = rx_macro_set_mix_interpolator_rate(dai, rate_val, sample_rate);

	return ret;
}

static int rx_macro_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);
	int ret;

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = rx_macro_set_interpolator_rate(dai, params_rate(params));
		if (ret) {
			dev_err(component->dev, "%s: cannot set sample rate: %u\n",
				__func__, params_rate(params));
			return ret;
		}
		rx->bit_width[dai->id] = params_width(params);
		break;
	default:
		break;
	}
	return 0;
}

static int rx_macro_get_channel_map(struct snd_soc_dai *dai,
				    unsigned int *tx_num, unsigned int *tx_slot,
				    unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_component *component = dai->component;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);
	u16 val, mask = 0, cnt = 0, temp;

	switch (dai->id) {
	case RX_MACRO_AIF1_PB:
	case RX_MACRO_AIF2_PB:
	case RX_MACRO_AIF3_PB:
	case RX_MACRO_AIF4_PB:
		for_each_set_bit(temp, &rx->active_ch_mask[dai->id],
			 RX_MACRO_PORTS_MAX) {
			mask |= (1 << temp);
			if (++cnt == RX_MACRO_MAX_DMA_CH_PER_PORT)
				break;
		}
		/*
		 * CDC_DMA_RX_0 port drives RX0/RX1 -- ch_mask 0x1/0x2/0x3
		 * CDC_DMA_RX_1 port drives RX2/RX3 -- ch_mask 0x1/0x2/0x3
		 * CDC_DMA_RX_2 port drives RX4     -- ch_mask 0x1
		 * CDC_DMA_RX_3 port drives RX5     -- ch_mask 0x1
		 * AIFn can pair to any CDC_DMA_RX_n port.
		 * In general, below convention is used::
		 * CDC_DMA_RX_0(AIF1)/CDC_DMA_RX_1(AIF2)/
		 * CDC_DMA_RX_2(AIF3)/CDC_DMA_RX_3(AIF4)
		 */
		if (mask & 0x0C)
			mask = mask >> 2;
		if ((mask & 0x10) || (mask & 0x20))
			mask = 0x1;
		*rx_slot = mask;
		*rx_num = rx->active_ch_cnt[dai->id];
		break;
	case RX_MACRO_AIF_ECHO:
		val = snd_soc_component_read(component,	CDC_RX_INP_MUX_RX_MIX_CFG4);
		if (val & RX_MACRO_EC_MIX_TX0_MASK) {
			mask |= 0x1;
			cnt++;
		}
		if (val & RX_MACRO_EC_MIX_TX1_MASK) {
			mask |= 0x2;
			cnt++;
		}
		val = snd_soc_component_read(component,
			CDC_RX_INP_MUX_RX_MIX_CFG5);
		if (val & RX_MACRO_EC_MIX_TX2_MASK) {
			mask |= 0x4;
			cnt++;
		}
		*tx_slot = mask;
		*tx_num = cnt;
		break;
	default:
		dev_err(component->dev, "%s: Invalid AIF\n", __func__);
		break;
	}
	return 0;
}

static int rx_macro_digital_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	uint16_t j, reg, mix_reg, dsm_reg;
	u16 int_mux_cfg0, int_mux_cfg1;
	u8 int_mux_cfg0_val, int_mux_cfg1_val;

	switch (dai->id) {
	case RX_MACRO_AIF1_PB:
	case RX_MACRO_AIF2_PB:
	case RX_MACRO_AIF3_PB:
	case RX_MACRO_AIF4_PB:
		for (j = 0; j < INTERP_MAX; j++) {
			reg = CDC_RX_RXn_RX_PATH_CTL(j);
			mix_reg = CDC_RX_RXn_RX_PATH_MIX_CTL(j);
			dsm_reg = CDC_RX_RXn_RX_PATH_DSM_CTL(j);

			if (mute) {
				snd_soc_component_update_bits(component, reg,
							      CDC_RX_PATH_PGA_MUTE_MASK,
							      CDC_RX_PATH_PGA_MUTE_ENABLE);
				snd_soc_component_update_bits(component, mix_reg,
							      CDC_RX_PATH_PGA_MUTE_MASK,
							      CDC_RX_PATH_PGA_MUTE_ENABLE);
			} else {
				snd_soc_component_update_bits(component, reg,
							      CDC_RX_PATH_PGA_MUTE_MASK, 0x0);
				snd_soc_component_update_bits(component, mix_reg,
							      CDC_RX_PATH_PGA_MUTE_MASK, 0x0);
			}

			if (j == INTERP_AUX)
				dsm_reg = CDC_RX_RX2_RX_PATH_DSM_CTL;

			int_mux_cfg0 = CDC_RX_INP_MUX_RX_INT0_CFG0 + j * 8;
			int_mux_cfg1 = int_mux_cfg0 + 4;
			int_mux_cfg0_val = snd_soc_component_read(component, int_mux_cfg0);
			int_mux_cfg1_val = snd_soc_component_read(component, int_mux_cfg1);

			if (snd_soc_component_read(component, dsm_reg) & 0x01) {
				if (int_mux_cfg0_val || (int_mux_cfg1_val & 0xF0))
					snd_soc_component_update_bits(component, reg, 0x20, 0x20);
				if (int_mux_cfg1_val & 0x0F) {
					snd_soc_component_update_bits(component, reg, 0x20, 0x20);
					snd_soc_component_update_bits(component, mix_reg, 0x20,
								      0x20);
				}
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct snd_soc_dai_ops rx_macro_dai_ops = {
	.hw_params = rx_macro_hw_params,
	.get_channel_map = rx_macro_get_channel_map,
	.mute_stream = rx_macro_digital_mute,
};

static struct snd_soc_dai_driver rx_macro_dai[] = {
	{
		.name = "rx_macro_rx1",
		.id = RX_MACRO_AIF1_PB,
		.playback = {
			.stream_name = "RX_MACRO_AIF1 Playback",
			.rates = RX_MACRO_RATES | RX_MACRO_FRAC_RATES,
			.formats = RX_MACRO_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &rx_macro_dai_ops,
	},
	{
		.name = "rx_macro_rx2",
		.id = RX_MACRO_AIF2_PB,
		.playback = {
			.stream_name = "RX_MACRO_AIF2 Playback",
			.rates = RX_MACRO_RATES | RX_MACRO_FRAC_RATES,
			.formats = RX_MACRO_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &rx_macro_dai_ops,
	},
	{
		.name = "rx_macro_rx3",
		.id = RX_MACRO_AIF3_PB,
		.playback = {
			.stream_name = "RX_MACRO_AIF3 Playback",
			.rates = RX_MACRO_RATES | RX_MACRO_FRAC_RATES,
			.formats = RX_MACRO_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &rx_macro_dai_ops,
	},
	{
		.name = "rx_macro_rx4",
		.id = RX_MACRO_AIF4_PB,
		.playback = {
			.stream_name = "RX_MACRO_AIF4 Playback",
			.rates = RX_MACRO_RATES | RX_MACRO_FRAC_RATES,
			.formats = RX_MACRO_FORMATS,
			.rate_max = 384000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &rx_macro_dai_ops,
	},
	{
		.name = "rx_macro_echo",
		.id = RX_MACRO_AIF_ECHO,
		.capture = {
			.stream_name = "RX_AIF_ECHO Capture",
			.rates = RX_MACRO_ECHO_RATES,
			.formats = RX_MACRO_ECHO_FORMATS,
			.rate_max = 48000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 3,
		},
		.ops = &rx_macro_dai_ops,
	},
};

static void rx_macro_mclk_enable(struct rx_macro *rx, bool mclk_enable)
{
	struct regmap *regmap = rx->regmap;

	if (mclk_enable) {
		if (rx->rx_mclk_users == 0) {
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_RX_CLK_MCLK_EN_MASK |
					   CDC_RX_CLK_MCLK2_EN_MASK,
					   CDC_RX_CLK_MCLK_ENABLE |
					   CDC_RX_CLK_MCLK2_ENABLE);
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_RX_FS_MCLK_CNT_CLR_MASK, 0x00);
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_RX_FS_MCLK_CNT_EN_MASK,
					   CDC_RX_FS_MCLK_CNT_ENABLE);
			regcache_mark_dirty(regmap);
			regcache_sync(regmap);
		}
		rx->rx_mclk_users++;
	} else {
		if (rx->rx_mclk_users <= 0) {
			dev_err(rx->dev, "%s: clock already disabled\n", __func__);
			rx->rx_mclk_users = 0;
			return;
		}
		rx->rx_mclk_users--;
		if (rx->rx_mclk_users == 0) {
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_RX_FS_MCLK_CNT_EN_MASK, 0x0);
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL,
					   CDC_RX_FS_MCLK_CNT_CLR_MASK,
					   CDC_RX_FS_MCLK_CNT_CLR);
			regmap_update_bits(regmap, CDC_RX_CLK_RST_CTRL_MCLK_CONTROL,
					   CDC_RX_CLK_MCLK_EN_MASK |
					   CDC_RX_CLK_MCLK2_EN_MASK, 0x0);
		}
	}
}

static int rx_macro_mclk_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rx_macro_mclk_enable(rx, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		rx_macro_mclk_enable(rx, false);
		break;
	default:
		dev_err(component->dev, "%s: invalid DAPM event %d\n", __func__, event);
		ret = -EINVAL;
	}
	return ret;
}

static bool rx_macro_adie_lb(struct snd_soc_component *component,
			     int interp_idx)
{
	u16 int_mux_cfg0, int_mux_cfg1;
	u8 int_n_inp0, int_n_inp1, int_n_inp2;

	int_mux_cfg0 = CDC_RX_INP_MUX_RX_INT0_CFG0 + interp_idx * 8;
	int_mux_cfg1 = int_mux_cfg0 + 4;

	int_n_inp0 = snd_soc_component_read_field(component, int_mux_cfg0,
						  CDC_RX_INTX_1_MIX_INP0_SEL_MASK);
	int_n_inp1 = snd_soc_component_read_field(component, int_mux_cfg0,
						  CDC_RX_INTX_1_MIX_INP1_SEL_MASK);
	int_n_inp2 = snd_soc_component_read_field(component, int_mux_cfg1,
						  CDC_RX_INTX_1_MIX_INP2_SEL_MASK);

	if (int_n_inp0 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp0 == INTn_1_INP_SEL_DEC1 ||
		int_n_inp0 == INTn_1_INP_SEL_IIR0 ||
		int_n_inp0 == INTn_1_INP_SEL_IIR1)
		return true;

	if (int_n_inp1 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp1 == INTn_1_INP_SEL_DEC1 ||
		int_n_inp1 == INTn_1_INP_SEL_IIR0 ||
		int_n_inp1 == INTn_1_INP_SEL_IIR1)
		return true;

	if (int_n_inp2 == INTn_1_INP_SEL_DEC0 ||
		int_n_inp2 == INTn_1_INP_SEL_DEC1 ||
		int_n_inp2 == INTn_1_INP_SEL_IIR0 ||
		int_n_inp2 == INTn_1_INP_SEL_IIR1)
		return true;

	return false;
}

static int rx_macro_enable_interp_clk(struct snd_soc_component *component,
				      int event, int interp_idx);
static int rx_macro_enable_main_path(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg, reg;

	reg = CDC_RX_RXn_RX_PATH_CTL(w->shift);
	gain_reg = CDC_RX_RXn_RX_VOL_CTL(w->shift);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rx_macro_enable_interp_clk(component, event, w->shift);
		if (rx_macro_adie_lb(component, w->shift))
			snd_soc_component_update_bits(component, reg,
						      CDC_RX_PATH_CLK_EN_MASK,
						      CDC_RX_PATH_CLK_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write(component, gain_reg,
			snd_soc_component_read(component, gain_reg));
		break;
	case SND_SOC_DAPM_POST_PMD:
		rx_macro_enable_interp_clk(component, event, w->shift);
		break;
	}

	return 0;
}

static int rx_macro_config_compander(struct snd_soc_component *component,
				struct rx_macro *rx,
				int comp, int event)
{
	u8 pcm_rate, val;

	/* AUX does not have compander */
	if (comp == INTERP_AUX)
		return 0;

	pcm_rate = snd_soc_component_read(component, CDC_RX_RXn_RX_PATH_CTL(comp)) & 0x0F;
	if (pcm_rate < 0x06)
		val = 0x03;
	else if (pcm_rate < 0x08)
		val = 0x01;
	else if (pcm_rate < 0x0B)
		val = 0x02;
	else
		val = 0x00;

	if (SND_SOC_DAPM_EVENT_ON(event))
		snd_soc_component_update_bits(component, CDC_RX_RXn_RX_PATH_CFG3(comp),
					      CDC_RX_DC_COEFF_SEL_MASK, val);

	if (SND_SOC_DAPM_EVENT_OFF(event))
		snd_soc_component_update_bits(component, CDC_RX_RXn_RX_PATH_CFG3(comp),
					      CDC_RX_DC_COEFF_SEL_MASK, 0x3);
	if (!rx->comp_enabled[comp])
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Enable Compander Clock */
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_CLK_EN_MASK, 0x1);
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_SOFT_RST_MASK, 0x1);
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_SOFT_RST_MASK, 0x0);
		snd_soc_component_write_field(component, CDC_RX_RXn_RX_PATH_CFG0(comp),
					      CDC_RX_RXn_COMP_EN_MASK, 0x1);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_HALT_MASK, 0x1);
		snd_soc_component_write_field(component, CDC_RX_RXn_RX_PATH_CFG0(comp),
					      CDC_RX_RXn_COMP_EN_MASK, 0x0);
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_CLK_EN_MASK, 0x0);
		snd_soc_component_write_field(component, CDC_RX_COMPANDERn_CTL0(comp),
					      CDC_RX_COMPANDERn_HALT_MASK, 0x0);
	}

	return 0;
}

static int rx_macro_load_compander_coeff(struct snd_soc_component *component,
					 struct rx_macro *rx,
					 int comp, int event)
{
	u16 comp_coeff_lsb_reg, comp_coeff_msb_reg;
	int i;
	int hph_pwr_mode;

	if (!rx->comp_enabled[comp])
		return 0;

	if (comp == INTERP_HPHL) {
		comp_coeff_lsb_reg = CDC_RX_TOP_HPHL_COMP_WR_LSB;
		comp_coeff_msb_reg = CDC_RX_TOP_HPHL_COMP_WR_MSB;
	} else if (comp == INTERP_HPHR) {
		comp_coeff_lsb_reg = CDC_RX_TOP_HPHR_COMP_WR_LSB;
		comp_coeff_msb_reg = CDC_RX_TOP_HPHR_COMP_WR_MSB;
	} else {
		/* compander coefficients are loaded only for hph path */
		return 0;
	}

	hph_pwr_mode = rx->hph_pwr_mode;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Load Compander Coeff */
		for (i = 0; i < COMP_MAX_COEFF; i++) {
			snd_soc_component_write(component, comp_coeff_lsb_reg,
					comp_coeff_table[hph_pwr_mode][i].lsb);
			snd_soc_component_write(component, comp_coeff_msb_reg,
					comp_coeff_table[hph_pwr_mode][i].msb);
		}
	}

	return 0;
}

static void rx_macro_enable_softclip_clk(struct snd_soc_component *component,
					 struct rx_macro *rx, bool enable)
{
	if (enable) {
		if (rx->softclip_clk_users == 0)
			snd_soc_component_write_field(component, CDC_RX_SOFTCLIP_CRC,
						      CDC_RX_SOFTCLIP_CLK_EN_MASK, 1);
		rx->softclip_clk_users++;
	} else {
		rx->softclip_clk_users--;
		if (rx->softclip_clk_users == 0)
			snd_soc_component_write_field(component, CDC_RX_SOFTCLIP_CRC,
						      CDC_RX_SOFTCLIP_CLK_EN_MASK, 0);
	}
}

static int rx_macro_config_softclip(struct snd_soc_component *component,
				    struct rx_macro *rx, int event)
{

	if (!rx->is_softclip_on)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Enable Softclip clock */
		rx_macro_enable_softclip_clk(component, rx, true);
		/* Enable Softclip control */
		snd_soc_component_write_field(component, CDC_RX_SOFTCLIP_SOFTCLIP_CTRL,
					     CDC_RX_SOFTCLIP_EN_MASK, 0x01);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_write_field(component, CDC_RX_SOFTCLIP_SOFTCLIP_CTRL,
					     CDC_RX_SOFTCLIP_EN_MASK, 0x0);
		rx_macro_enable_softclip_clk(component, rx, false);
	}

	return 0;
}

static int rx_macro_config_aux_hpf(struct snd_soc_component *component,
				   struct rx_macro *rx, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Update Aux HPF control */
		if (!rx->is_aux_hpf_on)
			snd_soc_component_update_bits(component,
				CDC_RX_RX2_RX_PATH_CFG1, 0x04, 0x00);
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		/* Reset to default (HPF=ON) */
		snd_soc_component_update_bits(component,
			CDC_RX_RX2_RX_PATH_CFG1, 0x04, 0x04);
	}

	return 0;
}

static inline void rx_macro_enable_clsh_block(struct rx_macro *rx, bool enable)
{
	if ((enable && ++rx->clsh_users == 1) || (!enable && --rx->clsh_users == 0))
		snd_soc_component_update_bits(rx->component, CDC_RX_CLSH_CRC,
					     CDC_RX_CLSH_CLK_EN_MASK, enable);
	if (rx->clsh_users < 0)
		rx->clsh_users = 0;
}

static int rx_macro_config_classh(struct snd_soc_component *component,
				struct rx_macro *rx,
				int interp_n, int event)
{
	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		rx_macro_enable_clsh_block(rx, false);
		return 0;
	}

	if (!SND_SOC_DAPM_EVENT_ON(event))
		return 0;

	rx_macro_enable_clsh_block(rx, true);
	if (interp_n == INTERP_HPHL ||
		interp_n == INTERP_HPHR) {
		/*
		 * These K1 values depend on the Headphone Impedance
		 * For now it is assumed to be 16 ohm
		 */
		snd_soc_component_write(component, CDC_RX_CLSH_K1_LSB, 0xc0);
		snd_soc_component_write_field(component, CDC_RX_CLSH_K1_MSB,
					      CDC_RX_CLSH_K1_MSB_COEFF_MASK, 0);
	}
	switch (interp_n) {
	case INTERP_HPHL:
		if (rx->is_ear_mode_on)
			snd_soc_component_update_bits(component,
				CDC_RX_CLSH_HPH_V_PA,
				CDC_RX_CLSH_HPH_V_PA_MIN_MASK, 0x39);
		else
			snd_soc_component_update_bits(component,
				CDC_RX_CLSH_HPH_V_PA,
				CDC_RX_CLSH_HPH_V_PA_MIN_MASK, 0x1c);
		snd_soc_component_update_bits(component,
				CDC_RX_CLSH_DECAY_CTRL,
				CDC_RX_CLSH_DECAY_RATE_MASK, 0x0);
		snd_soc_component_write_field(component,
				CDC_RX_RX0_RX_PATH_CFG0,
				CDC_RX_RXn_CLSH_EN_MASK, 0x1);
		break;
	case INTERP_HPHR:
		if (rx->is_ear_mode_on)
			snd_soc_component_update_bits(component,
				CDC_RX_CLSH_HPH_V_PA,
				CDC_RX_CLSH_HPH_V_PA_MIN_MASK, 0x39);
		else
			snd_soc_component_update_bits(component,
				CDC_RX_CLSH_HPH_V_PA,
				CDC_RX_CLSH_HPH_V_PA_MIN_MASK, 0x1c);
		snd_soc_component_update_bits(component,
				CDC_RX_CLSH_DECAY_CTRL,
				CDC_RX_CLSH_DECAY_RATE_MASK, 0x0);
		snd_soc_component_write_field(component,
				CDC_RX_RX1_RX_PATH_CFG0,
				CDC_RX_RXn_CLSH_EN_MASK, 0x1);
		break;
	case INTERP_AUX:
		snd_soc_component_update_bits(component,
				CDC_RX_RX2_RX_PATH_CFG0,
				CDC_RX_RX2_DLY_Z_EN_MASK, 1);
		snd_soc_component_write_field(component,
				CDC_RX_RX2_RX_PATH_CFG0,
				CDC_RX_RX2_CLSH_EN_MASK, 1);
		break;
	}

	return 0;
}

static void rx_macro_hd2_control(struct snd_soc_component *component,
				 u16 interp_idx, int event)
{
	u16 hd2_scale_reg, hd2_enable_reg;

	switch (interp_idx) {
	case INTERP_HPHL:
		hd2_scale_reg = CDC_RX_RX0_RX_PATH_SEC3;
		hd2_enable_reg = CDC_RX_RX0_RX_PATH_CFG0;
		break;
	case INTERP_HPHR:
		hd2_scale_reg = CDC_RX_RX1_RX_PATH_SEC3;
		hd2_enable_reg = CDC_RX_RX1_RX_PATH_CFG0;
		break;
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, hd2_scale_reg,
				CDC_RX_RXn_HD2_ALPHA_MASK, 0x14);
		snd_soc_component_write_field(component, hd2_enable_reg,
					      CDC_RX_RXn_HD2_EN_MASK, 1);
	}

	if (hd2_enable_reg && SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_write_field(component, hd2_enable_reg,
					      CDC_RX_RXn_HD2_EN_MASK, 0);
		snd_soc_component_update_bits(component, hd2_scale_reg,
				CDC_RX_RXn_HD2_ALPHA_MASK, 0x0);
	}
}

static int rx_macro_get_compander(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	int comp = ((struct soc_mixer_control *) kcontrol->private_value)->shift;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->comp_enabled[comp];
	return 0;
}

static int rx_macro_set_compander(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int comp = ((struct soc_mixer_control *)  kcontrol->private_value)->shift;
	int value = ucontrol->value.integer.value[0];
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->comp_enabled[comp] = value;

	return 0;
}

static int rx_macro_mux_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] =
			rx->rx_port_value[widget->shift];
	return 0;
}

static int rx_macro_mux_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;
	u32 rx_port_value = ucontrol->value.integer.value[0];
	u32 aif_rst;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	aif_rst = rx->rx_port_value[widget->shift];
	if (!rx_port_value) {
		if (aif_rst == 0) {
			dev_err(component->dev, "%s:AIF reset already\n", __func__);
			return 0;
		}
		if (aif_rst > RX_MACRO_AIF4_PB) {
			dev_err(component->dev, "%s: Invalid AIF reset\n", __func__);
			return 0;
		}
	}
	rx->rx_port_value[widget->shift] = rx_port_value;

	switch (rx_port_value) {
	case 0:
		if (rx->active_ch_cnt[aif_rst]) {
			clear_bit(widget->shift,
				&rx->active_ch_mask[aif_rst]);
			rx->active_ch_cnt[aif_rst]--;
		}
		break;
	case 1:
	case 2:
	case 3:
	case 4:
		set_bit(widget->shift,
			&rx->active_ch_mask[rx_port_value]);
		rx->active_ch_cnt[rx_port_value]++;
		break;
	default:
		dev_err(component->dev,
			"%s:Invalid AIF_ID for RX_MACRO MUX %d\n",
			__func__, rx_port_value);
		goto err;
	}

	snd_soc_dapm_mux_update_power(widget->dapm, kcontrol,
					rx_port_value, e, update);
	return 0;
err:
	return -EINVAL;
}

static const struct snd_kcontrol_new rx_macro_rx0_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx0", rx_macro_rx0_enum,
		  rx_macro_mux_get, rx_macro_mux_put);
static const struct snd_kcontrol_new rx_macro_rx1_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx1", rx_macro_rx1_enum,
		  rx_macro_mux_get, rx_macro_mux_put);
static const struct snd_kcontrol_new rx_macro_rx2_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx2", rx_macro_rx2_enum,
		  rx_macro_mux_get, rx_macro_mux_put);
static const struct snd_kcontrol_new rx_macro_rx3_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx3", rx_macro_rx3_enum,
		  rx_macro_mux_get, rx_macro_mux_put);
static const struct snd_kcontrol_new rx_macro_rx4_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx4", rx_macro_rx4_enum,
		  rx_macro_mux_get, rx_macro_mux_put);
static const struct snd_kcontrol_new rx_macro_rx5_mux =
		SOC_DAPM_ENUM_EXT("rx_macro_rx5", rx_macro_rx5_enum,
		  rx_macro_mux_get, rx_macro_mux_put);

static int rx_macro_get_ear_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->is_ear_mode_on;
	return 0;
}

static int rx_macro_put_ear_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->is_ear_mode_on = (!ucontrol->value.integer.value[0] ? false : true);
	return 0;
}

static int rx_macro_get_hph_hd2_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->hph_hd2_mode;
	return 0;
}

static int rx_macro_put_hph_hd2_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->hph_hd2_mode = ucontrol->value.integer.value[0];
	return 0;
}

static int rx_macro_get_hph_pwr_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->hph_pwr_mode;
	return 0;
}

static int rx_macro_put_hph_pwr_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->hph_pwr_mode = ucontrol->value.integer.value[0];
	return 0;
}

static int rx_macro_soft_clip_enable_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->is_softclip_on;

	return 0;
}

static int rx_macro_soft_clip_enable_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->is_softclip_on = ucontrol->value.integer.value[0];

	return 0;
}

static int rx_macro_aux_hpf_mode_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rx->is_aux_hpf_on;

	return 0;
}

static int rx_macro_aux_hpf_mode_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	rx->is_aux_hpf_on = ucontrol->value.integer.value[0];

	return 0;
}

static int rx_macro_hphdelay_lutbypass(struct snd_soc_component *component,
					struct rx_macro *rx,
					u16 interp_idx, int event)
{
	u16 hph_lut_bypass_reg;
	u16 hph_comp_ctrl7;

	switch (interp_idx) {
	case INTERP_HPHL:
		hph_lut_bypass_reg = CDC_RX_TOP_HPHL_COMP_LUT;
		hph_comp_ctrl7 = CDC_RX_COMPANDER0_CTL7;
		break;
	case INTERP_HPHR:
		hph_lut_bypass_reg = CDC_RX_TOP_HPHR_COMP_LUT;
		hph_comp_ctrl7 = CDC_RX_COMPANDER1_CTL7;
		break;
	default:
		return -EINVAL;
	}

	if (hph_lut_bypass_reg && SND_SOC_DAPM_EVENT_ON(event)) {
		if (interp_idx == INTERP_HPHL) {
			if (rx->is_ear_mode_on)
				snd_soc_component_write_field(component,
					CDC_RX_RX0_RX_PATH_CFG1,
					CDC_RX_RX0_HPH_L_EAR_SEL_MASK, 0x1);
			else
				snd_soc_component_write_field(component,
					hph_lut_bypass_reg,
					CDC_RX_TOP_HPH_LUT_BYPASS_MASK, 1);
		} else {
			snd_soc_component_write_field(component, hph_lut_bypass_reg,
					CDC_RX_TOP_HPH_LUT_BYPASS_MASK, 1);
		}
		if (rx->hph_pwr_mode)
			snd_soc_component_write_field(component, hph_comp_ctrl7,
					CDC_RX_COMPANDER1_HPH_LOW_PWR_MODE_MASK, 0x0);
	}

	if (hph_lut_bypass_reg && SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_write_field(component,
					CDC_RX_RX0_RX_PATH_CFG1,
					CDC_RX_RX0_HPH_L_EAR_SEL_MASK, 0x0);
		snd_soc_component_update_bits(component, hph_lut_bypass_reg,
					CDC_RX_TOP_HPH_LUT_BYPASS_MASK, 0);
		snd_soc_component_write_field(component, hph_comp_ctrl7,
					CDC_RX_COMPANDER1_HPH_LOW_PWR_MODE_MASK, 0x1);
	}

	return 0;
}

static int rx_macro_enable_interp_clk(struct snd_soc_component *component,
				      int event, int interp_idx)
{
	u16 main_reg, dsm_reg, rx_cfg2_reg;
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	main_reg = CDC_RX_RXn_RX_PATH_CTL(interp_idx);
	dsm_reg = CDC_RX_RXn_RX_PATH_DSM_CTL(interp_idx);
	if (interp_idx == INTERP_AUX)
		dsm_reg = CDC_RX_RX2_RX_PATH_DSM_CTL;
	rx_cfg2_reg = CDC_RX_RXn_RX_PATH_CFG2(interp_idx);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (rx->main_clk_users[interp_idx] == 0) {
			/* Main path PGA mute enable */
			snd_soc_component_write_field(component, main_reg,
						      CDC_RX_PATH_PGA_MUTE_MASK, 0x1);
			snd_soc_component_write_field(component, dsm_reg,
						      CDC_RX_RXn_DSM_CLK_EN_MASK, 0x1);
			snd_soc_component_update_bits(component, rx_cfg2_reg,
					CDC_RX_RXn_HPF_CUT_FREQ_MASK, 0x03);
			rx_macro_load_compander_coeff(component, rx, interp_idx, event);
			if (rx->hph_hd2_mode)
				rx_macro_hd2_control(component, interp_idx, event);
			rx_macro_hphdelay_lutbypass(component, rx, interp_idx, event);
			rx_macro_config_compander(component, rx, interp_idx, event);
			if (interp_idx == INTERP_AUX) {
				rx_macro_config_softclip(component, rx,	event);
				rx_macro_config_aux_hpf(component, rx, event);
			}
			rx_macro_config_classh(component, rx, interp_idx, event);
		}
		rx->main_clk_users[interp_idx]++;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		rx->main_clk_users[interp_idx]--;
		if (rx->main_clk_users[interp_idx] <= 0) {
			rx->main_clk_users[interp_idx] = 0;
			/* Main path PGA mute enable */
			snd_soc_component_write_field(component, main_reg,
						      CDC_RX_PATH_PGA_MUTE_MASK, 0x1);
			/* Clk Disable */
			snd_soc_component_write_field(component, dsm_reg,
						      CDC_RX_RXn_DSM_CLK_EN_MASK, 0);
			snd_soc_component_write_field(component, main_reg,
						      CDC_RX_PATH_CLK_EN_MASK, 0);
			/* Reset enable and disable */
			snd_soc_component_write_field(component, main_reg,
						      CDC_RX_PATH_RESET_EN_MASK, 1);
			snd_soc_component_write_field(component, main_reg,
						      CDC_RX_PATH_RESET_EN_MASK, 0);
			/* Reset rate to 48K*/
			snd_soc_component_update_bits(component, main_reg,
						      CDC_RX_PATH_PCM_RATE_MASK,
						      0x04);
			snd_soc_component_update_bits(component, rx_cfg2_reg,
						      CDC_RX_RXn_HPF_CUT_FREQ_MASK, 0x00);
			rx_macro_config_classh(component, rx, interp_idx, event);
			rx_macro_config_compander(component, rx, interp_idx, event);
			if (interp_idx ==  INTERP_AUX) {
				rx_macro_config_softclip(component, rx,	event);
				rx_macro_config_aux_hpf(component, rx, event);
			}
			rx_macro_hphdelay_lutbypass(component, rx, interp_idx, event);
			if (rx->hph_hd2_mode)
				rx_macro_hd2_control(component, interp_idx, event);
		}
	}

	return rx->main_clk_users[interp_idx];
}

static int rx_macro_enable_mix_path(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 gain_reg, mix_reg;

	gain_reg = CDC_RX_RXn_RX_VOL_MIX_CTL(w->shift);
	mix_reg = CDC_RX_RXn_RX_PATH_MIX_CTL(w->shift);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rx_macro_enable_interp_clk(component, event, w->shift);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write(component, gain_reg,
					snd_soc_component_read(component, gain_reg));
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Clk Disable */
		snd_soc_component_update_bits(component, mix_reg,
					      CDC_RX_RXn_MIX_CLK_EN_MASK, 0x00);
		rx_macro_enable_interp_clk(component, event, w->shift);
		/* Reset enable and disable */
		snd_soc_component_update_bits(component, mix_reg,
					      CDC_RX_RXn_MIX_RESET_MASK,
					      CDC_RX_RXn_MIX_RESET);
		snd_soc_component_update_bits(component, mix_reg,
					      CDC_RX_RXn_MIX_RESET_MASK, 0x00);
		break;
	}

	return 0;
}

static int rx_macro_enable_rx_path_clk(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		rx_macro_enable_interp_clk(component, event, w->shift);
		snd_soc_component_write_field(component, CDC_RX_RXn_RX_PATH_CFG1(w->shift),
					      CDC_RX_RXn_SIDETONE_EN_MASK, 1);
		snd_soc_component_write_field(component, CDC_RX_RXn_RX_PATH_CTL(w->shift),
					      CDC_RX_PATH_CLK_EN_MASK, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write_field(component, CDC_RX_RXn_RX_PATH_CFG1(w->shift),
					      CDC_RX_RXn_SIDETONE_EN_MASK, 0);
		rx_macro_enable_interp_clk(component, event, w->shift);
		break;
	default:
		break;
	}
	return 0;
}

static int rx_macro_set_iir_gain(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU: /* fall through */
	case SND_SOC_DAPM_PRE_PMD:
		if (strnstr(w->name, "IIR0", sizeof("IIR0"))) {
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL));
		} else {
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL));
			snd_soc_component_write(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL,
			snd_soc_component_read(component,
				CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL));
		}
		break;
	}
	return 0;
}

static uint32_t get_iir_band_coeff(struct snd_soc_component *component,
				   int iir_idx, int band_idx, int coeff_idx)
{
	u32 value;
	int reg, b2_reg;

	/* Address does not automatically update if reading */
	reg = CDC_RX_SIDETONE_IIR0_IIR_COEF_B1_CTL + 0x80 * iir_idx;
	b2_reg = CDC_RX_SIDETONE_IIR0_IIR_COEF_B2_CTL + 0x80 * iir_idx;

	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx) *
				 sizeof(uint32_t)) & 0x7F);

	value = snd_soc_component_read(component, b2_reg);
	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx)
				 * sizeof(uint32_t) + 1) & 0x7F);

	value |= (snd_soc_component_read(component, b2_reg) << 8);
	snd_soc_component_write(component, reg,
				((band_idx * BAND_MAX + coeff_idx)
				 * sizeof(uint32_t) + 2) & 0x7F);

	value |= (snd_soc_component_read(component, b2_reg) << 16);
	snd_soc_component_write(component, reg,
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t) + 3) & 0x7F);

	/* Mask bits top 2 bits since they are reserved */
	value |= (snd_soc_component_read(component, b2_reg) << 24);
	return value;
}

static void set_iir_band_coeff(struct snd_soc_component *component,
			       int iir_idx, int band_idx, uint32_t value)
{
	int reg = CDC_RX_SIDETONE_IIR0_IIR_COEF_B2_CTL + 0x80 * iir_idx;

	snd_soc_component_write(component, reg, (value & 0xFF));
	snd_soc_component_write(component, reg, (value >> 8) & 0xFF);
	snd_soc_component_write(component, reg, (value >> 16) & 0xFF);
	/* Mask top 2 bits, 7-8 are reserved */
	snd_soc_component_write(component, reg, (value >> 24) & 0x3F);
}

static int rx_macro_put_iir_band_audio_mixer(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];
	int reg = CDC_RX_SIDETONE_IIR0_IIR_COEF_B1_CTL + 0x80 * iir_idx;

	memcpy(&coeff[0], ucontrol->value.bytes.data, params->max);

	/* Mask top bit it is reserved */
	/* Updates addr automatically for each B2 write */
	snd_soc_component_write(component, reg, (band_idx * BAND_MAX *
						 sizeof(uint32_t)) & 0x7F);

	set_iir_band_coeff(component, iir_idx, band_idx, coeff[0]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[1]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[2]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[3]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[4]);

	return 0;
}

static int rx_macro_get_iir_band_audio_mixer(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];

	coeff[0] = get_iir_band_coeff(component, iir_idx, band_idx, 0);
	coeff[1] = get_iir_band_coeff(component, iir_idx, band_idx, 1);
	coeff[2] = get_iir_band_coeff(component, iir_idx, band_idx, 2);
	coeff[3] = get_iir_band_coeff(component, iir_idx, band_idx, 3);
	coeff[4] = get_iir_band_coeff(component, iir_idx, band_idx, 4);

	memcpy(ucontrol->value.bytes.data, &coeff[0], params->max);

	return 0;
}

static int rx_macro_iir_filter_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *ucontrol)
{
	struct wcd_iir_filter_ctl *ctl =
		(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;

	ucontrol->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	ucontrol->count = params->max;

	return 0;
}

static const struct snd_kcontrol_new rx_macro_snd_controls[] = {
	SOC_SINGLE_S8_TLV("RX_RX0 Digital Volume", CDC_RX_RX0_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX_RX1 Digital Volume", CDC_RX_RX1_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX_RX2 Digital Volume", CDC_RX_RX2_RX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX_RX0 Mix Digital Volume", CDC_RX_RX0_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX_RX1 Mix Digital Volume", CDC_RX_RX1_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("RX_RX2 Mix Digital Volume", CDC_RX_RX2_RX_VOL_MIX_CTL,
			  -84, 40, digital_gain),

	SOC_SINGLE_EXT("RX_COMP1 Switch", SND_SOC_NOPM, RX_MACRO_COMP1, 1, 0,
		rx_macro_get_compander, rx_macro_set_compander),
	SOC_SINGLE_EXT("RX_COMP2 Switch", SND_SOC_NOPM, RX_MACRO_COMP2, 1, 0,
		rx_macro_get_compander, rx_macro_set_compander),

	SOC_SINGLE_EXT("RX_EAR Mode Switch", SND_SOC_NOPM, 0, 1, 0,
		rx_macro_get_ear_mode, rx_macro_put_ear_mode),

	SOC_SINGLE_EXT("RX_HPH HD2 Mode Switch", SND_SOC_NOPM, 0, 1, 0,
		rx_macro_get_hph_hd2_mode, rx_macro_put_hph_hd2_mode),

	SOC_ENUM_EXT("RX_HPH PWR Mode", rx_macro_hph_pwr_mode_enum,
		rx_macro_get_hph_pwr_mode, rx_macro_put_hph_pwr_mode),

	SOC_SINGLE_EXT("RX_Softclip Switch", SND_SOC_NOPM, 0, 1, 0,
		     rx_macro_soft_clip_enable_get,
		     rx_macro_soft_clip_enable_put),
	SOC_SINGLE_EXT("AUX_HPF Switch", SND_SOC_NOPM, 0, 1, 0,
			rx_macro_aux_hpf_mode_get,
			rx_macro_aux_hpf_mode_put),

	SOC_SINGLE_S8_TLV("IIR0 INP0 Volume",
		CDC_RX_SIDETONE_IIR0_IIR_GAIN_B1_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP1 Volume",
		CDC_RX_SIDETONE_IIR0_IIR_GAIN_B2_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP2 Volume",
		CDC_RX_SIDETONE_IIR0_IIR_GAIN_B3_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR0 INP3 Volume",
		CDC_RX_SIDETONE_IIR0_IIR_GAIN_B4_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP0 Volume",
		CDC_RX_SIDETONE_IIR1_IIR_GAIN_B1_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP1 Volume",
		CDC_RX_SIDETONE_IIR1_IIR_GAIN_B2_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP2 Volume",
		CDC_RX_SIDETONE_IIR1_IIR_GAIN_B3_CTL, -84, 40,
		digital_gain),
	SOC_SINGLE_S8_TLV("IIR1 INP3 Volume",
		CDC_RX_SIDETONE_IIR1_IIR_GAIN_B4_CTL, -84, 40,
		digital_gain),

	SOC_SINGLE("IIR1 Band1 Switch", CDC_RX_SIDETONE_IIR0_IIR_CTL,
		   0, 1, 0),
	SOC_SINGLE("IIR1 Band2 Switch", CDC_RX_SIDETONE_IIR0_IIR_CTL,
		   1, 1, 0),
	SOC_SINGLE("IIR1 Band3 Switch", CDC_RX_SIDETONE_IIR0_IIR_CTL,
		   2, 1, 0),
	SOC_SINGLE("IIR1 Band4 Switch", CDC_RX_SIDETONE_IIR0_IIR_CTL,
		   3, 1, 0),
	SOC_SINGLE("IIR1 Band5 Switch", CDC_RX_SIDETONE_IIR0_IIR_CTL,
		   4, 1, 0),
	SOC_SINGLE("IIR2 Band1 Switch", CDC_RX_SIDETONE_IIR1_IIR_CTL,
		   0, 1, 0),
	SOC_SINGLE("IIR2 Band2 Switch", CDC_RX_SIDETONE_IIR1_IIR_CTL,
		   1, 1, 0),
	SOC_SINGLE("IIR2 Band3 Switch", CDC_RX_SIDETONE_IIR1_IIR_CTL,
		   2, 1, 0),
	SOC_SINGLE("IIR2 Band4 Switch", CDC_RX_SIDETONE_IIR1_IIR_CTL,
		   3, 1, 0),
	SOC_SINGLE("IIR2 Band5 Switch", CDC_RX_SIDETONE_IIR1_IIR_CTL,
		   4, 1, 0),

	RX_MACRO_IIR_FILTER_CTL("IIR0 Band1", IIR0, BAND1),
	RX_MACRO_IIR_FILTER_CTL("IIR0 Band2", IIR0, BAND2),
	RX_MACRO_IIR_FILTER_CTL("IIR0 Band3", IIR0, BAND3),
	RX_MACRO_IIR_FILTER_CTL("IIR0 Band4", IIR0, BAND4),
	RX_MACRO_IIR_FILTER_CTL("IIR0 Band5", IIR0, BAND5),

	RX_MACRO_IIR_FILTER_CTL("IIR1 Band1", IIR1, BAND1),
	RX_MACRO_IIR_FILTER_CTL("IIR1 Band2", IIR1, BAND2),
	RX_MACRO_IIR_FILTER_CTL("IIR1 Band3", IIR1, BAND3),
	RX_MACRO_IIR_FILTER_CTL("IIR1 Band4", IIR1, BAND4),
	RX_MACRO_IIR_FILTER_CTL("IIR1 Band5", IIR1, BAND5),

};

static int rx_macro_enable_echo(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 val, ec_hq_reg;
	int ec_tx = -1;

	val = snd_soc_component_read(component,
			CDC_RX_INP_MUX_RX_MIX_CFG4);
	if (!(strcmp(w->name, "RX MIX TX0 MUX")))
		ec_tx = ((val & 0xf0) >> 0x4) - 1;
	else if (!(strcmp(w->name, "RX MIX TX1 MUX")))
		ec_tx = (val & 0x0f) - 1;

	val = snd_soc_component_read(component,
			CDC_RX_INP_MUX_RX_MIX_CFG5);
	if (!(strcmp(w->name, "RX MIX TX2 MUX")))
		ec_tx = (val & 0x0f) - 1;

	if (ec_tx < 0 || (ec_tx >= RX_MACRO_EC_MUX_MAX)) {
		dev_err(component->dev, "%s: EC mix control not set correctly\n",
			__func__);
		return -EINVAL;
	}
	ec_hq_reg = CDC_RX_EC_REF_HQ0_EC_REF_HQ_PATH_CTL +
			    0x40 * ec_tx;
	snd_soc_component_update_bits(component, ec_hq_reg, 0x01, 0x01);
	ec_hq_reg = CDC_RX_EC_REF_HQ0_EC_REF_HQ_CFG0 +
				0x40 * ec_tx;
	/* default set to 48k */
	snd_soc_component_update_bits(component, ec_hq_reg, 0x1E, 0x08);

	return 0;
}

static const struct snd_soc_dapm_widget rx_macro_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX AIF1 PB", "RX_MACRO_AIF1 Playback", 0,
		SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("RX AIF2 PB", "RX_MACRO_AIF2 Playback", 0,
		SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("RX AIF3 PB", "RX_MACRO_AIF3 Playback", 0,
		SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("RX AIF4 PB", "RX_MACRO_AIF4 Playback", 0,
		SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT("RX AIF_ECHO", "RX_AIF_ECHO Capture", 0,
		SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX("RX_MACRO RX0 MUX", SND_SOC_NOPM, RX_MACRO_RX0, 0,
			 &rx_macro_rx0_mux),
	SND_SOC_DAPM_MUX("RX_MACRO RX1 MUX", SND_SOC_NOPM, RX_MACRO_RX1, 0,
			 &rx_macro_rx1_mux),
	SND_SOC_DAPM_MUX("RX_MACRO RX2 MUX", SND_SOC_NOPM, RX_MACRO_RX2, 0,
			 &rx_macro_rx2_mux),
	SND_SOC_DAPM_MUX("RX_MACRO RX3 MUX", SND_SOC_NOPM, RX_MACRO_RX3, 0,
			 &rx_macro_rx3_mux),
	SND_SOC_DAPM_MUX("RX_MACRO RX4 MUX", SND_SOC_NOPM, RX_MACRO_RX4, 0,
			 &rx_macro_rx4_mux),
	SND_SOC_DAPM_MUX("RX_MACRO RX5 MUX", SND_SOC_NOPM, RX_MACRO_RX5, 0,
			 &rx_macro_rx5_mux),

	SND_SOC_DAPM_MIXER("RX_RX0", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX_RX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX_RX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX_RX3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX_RX4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX_RX5", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("IIR0 INP0 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp0_mux),
	SND_SOC_DAPM_MUX("IIR0 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp1_mux),
	SND_SOC_DAPM_MUX("IIR0 INP2 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp2_mux),
	SND_SOC_DAPM_MUX("IIR0 INP3 MUX", SND_SOC_NOPM, 0, 0, &iir0_inp3_mux),
	SND_SOC_DAPM_MUX("IIR1 INP0 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp0_mux),
	SND_SOC_DAPM_MUX("IIR1 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp1_mux),
	SND_SOC_DAPM_MUX("IIR1 INP2 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp2_mux),
	SND_SOC_DAPM_MUX("IIR1 INP3 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp3_mux),

	SND_SOC_DAPM_MUX_E("RX MIX TX0 MUX", SND_SOC_NOPM,
			   RX_MACRO_EC0_MUX, 0,
			   &rx_mix_tx0_mux, rx_macro_enable_echo,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX MIX TX1 MUX", SND_SOC_NOPM,
			   RX_MACRO_EC1_MUX, 0,
			   &rx_mix_tx1_mux, rx_macro_enable_echo,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX MIX TX2 MUX", SND_SOC_NOPM,
			   RX_MACRO_EC2_MUX, 0,
			   &rx_mix_tx2_mux, rx_macro_enable_echo,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER_E("IIR0", CDC_RX_SIDETONE_IIR0_IIR_PATH_CTL,
		4, 0, NULL, 0, rx_macro_set_iir_gain,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER_E("IIR1", CDC_RX_SIDETONE_IIR1_IIR_PATH_CTL,
		4, 0, NULL, 0, rx_macro_set_iir_gain,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER("SRC0", CDC_RX_SIDETONE_SRC0_ST_SRC_PATH_CTL,
		4, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SRC1", CDC_RX_SIDETONE_SRC1_ST_SRC_PATH_CTL,
		4, 0, NULL, 0),

	SND_SOC_DAPM_MUX("RX INT0 DEM MUX", SND_SOC_NOPM, 0, 0,
			 &rx_int0_dem_inp_mux),
	SND_SOC_DAPM_MUX("RX INT1 DEM MUX", SND_SOC_NOPM, 0, 0,
			 &rx_int1_dem_inp_mux),

	SND_SOC_DAPM_MUX_E("RX INT0_2 MUX", SND_SOC_NOPM, INTERP_HPHL, 0,
		&rx_int0_2_mux, rx_macro_enable_mix_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1_2 MUX", SND_SOC_NOPM, INTERP_HPHR, 0,
		&rx_int1_2_mux, rx_macro_enable_mix_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2_2 MUX", SND_SOC_NOPM, INTERP_AUX, 0,
		&rx_int2_2_mux, rx_macro_enable_mix_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP0", SND_SOC_NOPM, 0, 0, &rx_int0_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP1", SND_SOC_NOPM, 0, 0, &rx_int0_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT0_1 MIX1 INP2", SND_SOC_NOPM, 0, 0, &rx_int0_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP0", SND_SOC_NOPM, 0, 0, &rx_int1_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP1", SND_SOC_NOPM, 0, 0, &rx_int1_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT1_1 MIX1 INP2", SND_SOC_NOPM, 0, 0, &rx_int1_1_mix_inp2_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP0", SND_SOC_NOPM, 0, 0, &rx_int2_1_mix_inp0_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP1", SND_SOC_NOPM, 0, 0, &rx_int2_1_mix_inp1_mux),
	SND_SOC_DAPM_MUX("RX INT2_1 MIX1 INP2", SND_SOC_NOPM, 0, 0, &rx_int2_1_mix_inp2_mux),

	SND_SOC_DAPM_MUX_E("RX INT0_1 INTERP", SND_SOC_NOPM, INTERP_HPHL, 0,
		&rx_int0_1_interp_mux, rx_macro_enable_main_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1_1 INTERP", SND_SOC_NOPM, INTERP_HPHR, 0,
		&rx_int1_1_interp_mux, rx_macro_enable_main_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2_1 INTERP", SND_SOC_NOPM, INTERP_AUX, 0,
		&rx_int2_1_interp_mux, rx_macro_enable_main_path,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("RX INT0_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int0_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT1_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int1_2_interp_mux),
	SND_SOC_DAPM_MUX("RX INT2_2 INTERP", SND_SOC_NOPM, 0, 0,
			 &rx_int2_2_interp_mux),

	SND_SOC_DAPM_MIXER("RX INT0_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT0 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2_1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 SEC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX_E("RX INT0 MIX2 INP", SND_SOC_NOPM, INTERP_HPHL,
		0, &rx_int0_mix2_inp_mux, rx_macro_enable_rx_path_clk,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT1 MIX2 INP", SND_SOC_NOPM, INTERP_HPHR,
		0, &rx_int1_mix2_inp_mux, rx_macro_enable_rx_path_clk,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("RX INT2 MIX2 INP", SND_SOC_NOPM, INTERP_AUX,
		0, &rx_int2_mix2_inp_mux, rx_macro_enable_rx_path_clk,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("RX INT0 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT1 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX INT2 MIX2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPHL_OUT"),
	SND_SOC_DAPM_OUTPUT("HPHR_OUT"),
	SND_SOC_DAPM_OUTPUT("AUX_OUT"),

	SND_SOC_DAPM_INPUT("RX_TX DEC0_INP"),
	SND_SOC_DAPM_INPUT("RX_TX DEC1_INP"),
	SND_SOC_DAPM_INPUT("RX_TX DEC2_INP"),
	SND_SOC_DAPM_INPUT("RX_TX DEC3_INP"),

	SND_SOC_DAPM_SUPPLY_S("RX_MCLK", 0, SND_SOC_NOPM, 0, 0,
	rx_macro_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route rx_audio_map[] = {
	{"RX AIF1 PB", NULL, "RX_MCLK"},
	{"RX AIF2 PB", NULL, "RX_MCLK"},
	{"RX AIF3 PB", NULL, "RX_MCLK"},
	{"RX AIF4 PB", NULL, "RX_MCLK"},

	{"RX_MACRO RX0 MUX", "AIF1_PB", "RX AIF1 PB"},
	{"RX_MACRO RX1 MUX", "AIF1_PB", "RX AIF1 PB"},
	{"RX_MACRO RX2 MUX", "AIF1_PB", "RX AIF1 PB"},
	{"RX_MACRO RX3 MUX", "AIF1_PB", "RX AIF1 PB"},
	{"RX_MACRO RX4 MUX", "AIF1_PB", "RX AIF1 PB"},
	{"RX_MACRO RX5 MUX", "AIF1_PB", "RX AIF1 PB"},

	{"RX_MACRO RX0 MUX", "AIF2_PB", "RX AIF2 PB"},
	{"RX_MACRO RX1 MUX", "AIF2_PB", "RX AIF2 PB"},
	{"RX_MACRO RX2 MUX", "AIF2_PB", "RX AIF2 PB"},
	{"RX_MACRO RX3 MUX", "AIF2_PB", "RX AIF2 PB"},
	{"RX_MACRO RX4 MUX", "AIF2_PB", "RX AIF2 PB"},
	{"RX_MACRO RX5 MUX", "AIF2_PB", "RX AIF2 PB"},

	{"RX_MACRO RX0 MUX", "AIF3_PB", "RX AIF3 PB"},
	{"RX_MACRO RX1 MUX", "AIF3_PB", "RX AIF3 PB"},
	{"RX_MACRO RX2 MUX", "AIF3_PB", "RX AIF3 PB"},
	{"RX_MACRO RX3 MUX", "AIF3_PB", "RX AIF3 PB"},
	{"RX_MACRO RX4 MUX", "AIF3_PB", "RX AIF3 PB"},
	{"RX_MACRO RX5 MUX", "AIF3_PB", "RX AIF3 PB"},

	{"RX_MACRO RX0 MUX", "AIF4_PB", "RX AIF4 PB"},
	{"RX_MACRO RX1 MUX", "AIF4_PB", "RX AIF4 PB"},
	{"RX_MACRO RX2 MUX", "AIF4_PB", "RX AIF4 PB"},
	{"RX_MACRO RX3 MUX", "AIF4_PB", "RX AIF4 PB"},
	{"RX_MACRO RX4 MUX", "AIF4_PB", "RX AIF4 PB"},
	{"RX_MACRO RX5 MUX", "AIF4_PB", "RX AIF4 PB"},

	{"RX_RX0", NULL, "RX_MACRO RX0 MUX"},
	{"RX_RX1", NULL, "RX_MACRO RX1 MUX"},
	{"RX_RX2", NULL, "RX_MACRO RX2 MUX"},
	{"RX_RX3", NULL, "RX_MACRO RX3 MUX"},
	{"RX_RX4", NULL, "RX_MACRO RX4 MUX"},
	{"RX_RX5", NULL, "RX_MACRO RX5 MUX"},

	{"RX INT0_1 MIX1 INP0", "RX0", "RX_RX0"},
	{"RX INT0_1 MIX1 INP0", "RX1", "RX_RX1"},
	{"RX INT0_1 MIX1 INP0", "RX2", "RX_RX2"},
	{"RX INT0_1 MIX1 INP0", "RX3", "RX_RX3"},
	{"RX INT0_1 MIX1 INP0", "RX4", "RX_RX4"},
	{"RX INT0_1 MIX1 INP0", "RX5", "RX_RX5"},
	{"RX INT0_1 MIX1 INP0", "IIR0", "IIR0"},
	{"RX INT0_1 MIX1 INP0", "IIR1", "IIR1"},
	{"RX INT0_1 MIX1 INP0", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT0_1 MIX1 INP0", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT0_1 MIX1 INP1", "RX0", "RX_RX0"},
	{"RX INT0_1 MIX1 INP1", "RX1", "RX_RX1"},
	{"RX INT0_1 MIX1 INP1", "RX2", "RX_RX2"},
	{"RX INT0_1 MIX1 INP1", "RX3", "RX_RX3"},
	{"RX INT0_1 MIX1 INP1", "RX4", "RX_RX4"},
	{"RX INT0_1 MIX1 INP1", "RX5", "RX_RX5"},
	{"RX INT0_1 MIX1 INP1", "IIR0", "IIR0"},
	{"RX INT0_1 MIX1 INP1", "IIR1", "IIR1"},
	{"RX INT0_1 MIX1 INP1", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT0_1 MIX1 INP1", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT0_1 MIX1 INP2", "RX0", "RX_RX0"},
	{"RX INT0_1 MIX1 INP2", "RX1", "RX_RX1"},
	{"RX INT0_1 MIX1 INP2", "RX2", "RX_RX2"},
	{"RX INT0_1 MIX1 INP2", "RX3", "RX_RX3"},
	{"RX INT0_1 MIX1 INP2", "RX4", "RX_RX4"},
	{"RX INT0_1 MIX1 INP2", "RX5", "RX_RX5"},
	{"RX INT0_1 MIX1 INP2", "IIR0", "IIR0"},
	{"RX INT0_1 MIX1 INP2", "IIR1", "IIR1"},
	{"RX INT0_1 MIX1 INP2", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT0_1 MIX1 INP2", "DEC1", "RX_TX DEC1_INP"},

	{"RX INT1_1 MIX1 INP0", "RX0", "RX_RX0"},
	{"RX INT1_1 MIX1 INP0", "RX1", "RX_RX1"},
	{"RX INT1_1 MIX1 INP0", "RX2", "RX_RX2"},
	{"RX INT1_1 MIX1 INP0", "RX3", "RX_RX3"},
	{"RX INT1_1 MIX1 INP0", "RX4", "RX_RX4"},
	{"RX INT1_1 MIX1 INP0", "RX5", "RX_RX5"},
	{"RX INT1_1 MIX1 INP0", "IIR0", "IIR0"},
	{"RX INT1_1 MIX1 INP0", "IIR1", "IIR1"},
	{"RX INT1_1 MIX1 INP0", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT1_1 MIX1 INP0", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT1_1 MIX1 INP1", "RX0", "RX_RX0"},
	{"RX INT1_1 MIX1 INP1", "RX1", "RX_RX1"},
	{"RX INT1_1 MIX1 INP1", "RX2", "RX_RX2"},
	{"RX INT1_1 MIX1 INP1", "RX3", "RX_RX3"},
	{"RX INT1_1 MIX1 INP1", "RX4", "RX_RX4"},
	{"RX INT1_1 MIX1 INP1", "RX5", "RX_RX5"},
	{"RX INT1_1 MIX1 INP1", "IIR0", "IIR0"},
	{"RX INT1_1 MIX1 INP1", "IIR1", "IIR1"},
	{"RX INT1_1 MIX1 INP1", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT1_1 MIX1 INP1", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT1_1 MIX1 INP2", "RX0", "RX_RX0"},
	{"RX INT1_1 MIX1 INP2", "RX1", "RX_RX1"},
	{"RX INT1_1 MIX1 INP2", "RX2", "RX_RX2"},
	{"RX INT1_1 MIX1 INP2", "RX3", "RX_RX3"},
	{"RX INT1_1 MIX1 INP2", "RX4", "RX_RX4"},
	{"RX INT1_1 MIX1 INP2", "RX5", "RX_RX5"},
	{"RX INT1_1 MIX1 INP2", "IIR0", "IIR0"},
	{"RX INT1_1 MIX1 INP2", "IIR1", "IIR1"},
	{"RX INT1_1 MIX1 INP2", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT1_1 MIX1 INP2", "DEC1", "RX_TX DEC1_INP"},

	{"RX INT2_1 MIX1 INP0", "RX0", "RX_RX0"},
	{"RX INT2_1 MIX1 INP0", "RX1", "RX_RX1"},
	{"RX INT2_1 MIX1 INP0", "RX2", "RX_RX2"},
	{"RX INT2_1 MIX1 INP0", "RX3", "RX_RX3"},
	{"RX INT2_1 MIX1 INP0", "RX4", "RX_RX4"},
	{"RX INT2_1 MIX1 INP0", "RX5", "RX_RX5"},
	{"RX INT2_1 MIX1 INP0", "IIR0", "IIR0"},
	{"RX INT2_1 MIX1 INP0", "IIR1", "IIR1"},
	{"RX INT2_1 MIX1 INP0", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT2_1 MIX1 INP0", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT2_1 MIX1 INP1", "RX0", "RX_RX0"},
	{"RX INT2_1 MIX1 INP1", "RX1", "RX_RX1"},
	{"RX INT2_1 MIX1 INP1", "RX2", "RX_RX2"},
	{"RX INT2_1 MIX1 INP1", "RX3", "RX_RX3"},
	{"RX INT2_1 MIX1 INP1", "RX4", "RX_RX4"},
	{"RX INT2_1 MIX1 INP1", "RX5", "RX_RX5"},
	{"RX INT2_1 MIX1 INP1", "IIR0", "IIR0"},
	{"RX INT2_1 MIX1 INP1", "IIR1", "IIR1"},
	{"RX INT2_1 MIX1 INP1", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT2_1 MIX1 INP1", "DEC1", "RX_TX DEC1_INP"},
	{"RX INT2_1 MIX1 INP2", "RX0", "RX_RX0"},
	{"RX INT2_1 MIX1 INP2", "RX1", "RX_RX1"},
	{"RX INT2_1 MIX1 INP2", "RX2", "RX_RX2"},
	{"RX INT2_1 MIX1 INP2", "RX3", "RX_RX3"},
	{"RX INT2_1 MIX1 INP2", "RX4", "RX_RX4"},
	{"RX INT2_1 MIX1 INP2", "RX5", "RX_RX5"},
	{"RX INT2_1 MIX1 INP2", "IIR0", "IIR0"},
	{"RX INT2_1 MIX1 INP2", "IIR1", "IIR1"},
	{"RX INT2_1 MIX1 INP2", "DEC0", "RX_TX DEC0_INP"},
	{"RX INT2_1 MIX1 INP2", "DEC1", "RX_TX DEC1_INP"},

	{"RX INT0_1 MIX1", NULL, "RX INT0_1 MIX1 INP0"},
	{"RX INT0_1 MIX1", NULL, "RX INT0_1 MIX1 INP1"},
	{"RX INT0_1 MIX1", NULL, "RX INT0_1 MIX1 INP2"},
	{"RX INT1_1 MIX1", NULL, "RX INT1_1 MIX1 INP0"},
	{"RX INT1_1 MIX1", NULL, "RX INT1_1 MIX1 INP1"},
	{"RX INT1_1 MIX1", NULL, "RX INT1_1 MIX1 INP2"},
	{"RX INT2_1 MIX1", NULL, "RX INT2_1 MIX1 INP0"},
	{"RX INT2_1 MIX1", NULL, "RX INT2_1 MIX1 INP1"},
	{"RX INT2_1 MIX1", NULL, "RX INT2_1 MIX1 INP2"},

	{"RX MIX TX0 MUX", "RX_MIX0", "RX INT0 SEC MIX"},
	{"RX MIX TX0 MUX", "RX_MIX1", "RX INT1 SEC MIX"},
	{"RX MIX TX0 MUX", "RX_MIX2", "RX INT2 SEC MIX"},
	{"RX MIX TX1 MUX", "RX_MIX0", "RX INT0 SEC MIX"},
	{"RX MIX TX1 MUX", "RX_MIX1", "RX INT1 SEC MIX"},
	{"RX MIX TX1 MUX", "RX_MIX2", "RX INT2 SEC MIX"},
	{"RX MIX TX2 MUX", "RX_MIX0", "RX INT0 SEC MIX"},
	{"RX MIX TX2 MUX", "RX_MIX1", "RX INT1 SEC MIX"},
	{"RX MIX TX2 MUX", "RX_MIX2", "RX INT2 SEC MIX"},
	{"RX AIF_ECHO", NULL, "RX MIX TX0 MUX"},
	{"RX AIF_ECHO", NULL, "RX MIX TX1 MUX"},
	{"RX AIF_ECHO", NULL, "RX MIX TX2 MUX"},
	{"RX AIF_ECHO", NULL, "RX_MCLK"},

	/* Mixing path INT0 */
	{"RX INT0_2 MUX", "RX0", "RX_RX0"},
	{"RX INT0_2 MUX", "RX1", "RX_RX1"},
	{"RX INT0_2 MUX", "RX2", "RX_RX2"},
	{"RX INT0_2 MUX", "RX3", "RX_RX3"},
	{"RX INT0_2 MUX", "RX4", "RX_RX4"},
	{"RX INT0_2 MUX", "RX5", "RX_RX5"},
	{"RX INT0_2 INTERP", NULL, "RX INT0_2 MUX"},
	{"RX INT0 SEC MIX", NULL, "RX INT0_2 INTERP"},

	/* Mixing path INT1 */
	{"RX INT1_2 MUX", "RX0", "RX_RX0"},
	{"RX INT1_2 MUX", "RX1", "RX_RX1"},
	{"RX INT1_2 MUX", "RX2", "RX_RX2"},
	{"RX INT1_2 MUX", "RX3", "RX_RX3"},
	{"RX INT1_2 MUX", "RX4", "RX_RX4"},
	{"RX INT1_2 MUX", "RX5", "RX_RX5"},
	{"RX INT1_2 INTERP", NULL, "RX INT1_2 MUX"},
	{"RX INT1 SEC MIX", NULL, "RX INT1_2 INTERP"},

	/* Mixing path INT2 */
	{"RX INT2_2 MUX", "RX0", "RX_RX0"},
	{"RX INT2_2 MUX", "RX1", "RX_RX1"},
	{"RX INT2_2 MUX", "RX2", "RX_RX2"},
	{"RX INT2_2 MUX", "RX3", "RX_RX3"},
	{"RX INT2_2 MUX", "RX4", "RX_RX4"},
	{"RX INT2_2 MUX", "RX5", "RX_RX5"},
	{"RX INT2_2 INTERP", NULL, "RX INT2_2 MUX"},
	{"RX INT2 SEC MIX", NULL, "RX INT2_2 INTERP"},

	{"RX INT0_1 INTERP", NULL, "RX INT0_1 MIX1"},
	{"RX INT0 SEC MIX", NULL, "RX INT0_1 INTERP"},
	{"RX INT0 MIX2", NULL, "RX INT0 SEC MIX"},
	{"RX INT0 MIX2", NULL, "RX INT0 MIX2 INP"},
	{"RX INT0 DEM MUX", "CLSH_DSM_OUT", "RX INT0 MIX2"},
	{"HPHL_OUT", NULL, "RX INT0 DEM MUX"},
	{"HPHL_OUT", NULL, "RX_MCLK"},

	{"RX INT1_1 INTERP", NULL, "RX INT1_1 MIX1"},
	{"RX INT1 SEC MIX", NULL, "RX INT1_1 INTERP"},
	{"RX INT1 MIX2", NULL, "RX INT1 SEC MIX"},
	{"RX INT1 MIX2", NULL, "RX INT1 MIX2 INP"},
	{"RX INT1 DEM MUX", "CLSH_DSM_OUT", "RX INT1 MIX2"},
	{"HPHR_OUT", NULL, "RX INT1 DEM MUX"},
	{"HPHR_OUT", NULL, "RX_MCLK"},

	{"RX INT2_1 INTERP", NULL, "RX INT2_1 MIX1"},

	{"RX INT2 SEC MIX", NULL, "RX INT2_1 INTERP"},
	{"RX INT2 MIX2", NULL, "RX INT2 SEC MIX"},
	{"RX INT2 MIX2", NULL, "RX INT2 MIX2 INP"},
	{"AUX_OUT", NULL, "RX INT2 MIX2"},
	{"AUX_OUT", NULL, "RX_MCLK"},

	{"IIR0", NULL, "RX_MCLK"},
	{"IIR0", NULL, "IIR0 INP0 MUX"},
	{"IIR0 INP0 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR0 INP0 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR0 INP0 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR0 INP0 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR0 INP0 MUX", "RX0", "RX_RX0"},
	{"IIR0 INP0 MUX", "RX1", "RX_RX1"},
	{"IIR0 INP0 MUX", "RX2", "RX_RX2"},
	{"IIR0 INP0 MUX", "RX3", "RX_RX3"},
	{"IIR0 INP0 MUX", "RX4", "RX_RX4"},
	{"IIR0 INP0 MUX", "RX5", "RX_RX5"},
	{"IIR0", NULL, "IIR0 INP1 MUX"},
	{"IIR0 INP1 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR0 INP1 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR0 INP1 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR0 INP1 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR0 INP1 MUX", "RX0", "RX_RX0"},
	{"IIR0 INP1 MUX", "RX1", "RX_RX1"},
	{"IIR0 INP1 MUX", "RX2", "RX_RX2"},
	{"IIR0 INP1 MUX", "RX3", "RX_RX3"},
	{"IIR0 INP1 MUX", "RX4", "RX_RX4"},
	{"IIR0 INP1 MUX", "RX5", "RX_RX5"},
	{"IIR0", NULL, "IIR0 INP2 MUX"},
	{"IIR0 INP2 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR0 INP2 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR0 INP2 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR0 INP2 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR0 INP2 MUX", "RX0", "RX_RX0"},
	{"IIR0 INP2 MUX", "RX1", "RX_RX1"},
	{"IIR0 INP2 MUX", "RX2", "RX_RX2"},
	{"IIR0 INP2 MUX", "RX3", "RX_RX3"},
	{"IIR0 INP2 MUX", "RX4", "RX_RX4"},
	{"IIR0 INP2 MUX", "RX5", "RX_RX5"},
	{"IIR0", NULL, "IIR0 INP3 MUX"},
	{"IIR0 INP3 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR0 INP3 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR0 INP3 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR0 INP3 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR0 INP3 MUX", "RX0", "RX_RX0"},
	{"IIR0 INP3 MUX", "RX1", "RX_RX1"},
	{"IIR0 INP3 MUX", "RX2", "RX_RX2"},
	{"IIR0 INP3 MUX", "RX3", "RX_RX3"},
	{"IIR0 INP3 MUX", "RX4", "RX_RX4"},
	{"IIR0 INP3 MUX", "RX5", "RX_RX5"},

	{"IIR1", NULL, "RX_MCLK"},
	{"IIR1", NULL, "IIR1 INP0 MUX"},
	{"IIR1 INP0 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR1 INP0 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR1 INP0 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR1 INP0 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR1 INP0 MUX", "RX0", "RX_RX0"},
	{"IIR1 INP0 MUX", "RX1", "RX_RX1"},
	{"IIR1 INP0 MUX", "RX2", "RX_RX2"},
	{"IIR1 INP0 MUX", "RX3", "RX_RX3"},
	{"IIR1 INP0 MUX", "RX4", "RX_RX4"},
	{"IIR1 INP0 MUX", "RX5", "RX_RX5"},
	{"IIR1", NULL, "IIR1 INP1 MUX"},
	{"IIR1 INP1 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR1 INP1 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR1 INP1 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR1 INP1 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR1 INP1 MUX", "RX0", "RX_RX0"},
	{"IIR1 INP1 MUX", "RX1", "RX_RX1"},
	{"IIR1 INP1 MUX", "RX2", "RX_RX2"},
	{"IIR1 INP1 MUX", "RX3", "RX_RX3"},
	{"IIR1 INP1 MUX", "RX4", "RX_RX4"},
	{"IIR1 INP1 MUX", "RX5", "RX_RX5"},
	{"IIR1", NULL, "IIR1 INP2 MUX"},
	{"IIR1 INP2 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR1 INP2 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR1 INP2 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR1 INP2 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR1 INP2 MUX", "RX0", "RX_RX0"},
	{"IIR1 INP2 MUX", "RX1", "RX_RX1"},
	{"IIR1 INP2 MUX", "RX2", "RX_RX2"},
	{"IIR1 INP2 MUX", "RX3", "RX_RX3"},
	{"IIR1 INP2 MUX", "RX4", "RX_RX4"},
	{"IIR1 INP2 MUX", "RX5", "RX_RX5"},
	{"IIR1", NULL, "IIR1 INP3 MUX"},
	{"IIR1 INP3 MUX", "DEC0", "RX_TX DEC0_INP"},
	{"IIR1 INP3 MUX", "DEC1", "RX_TX DEC1_INP"},
	{"IIR1 INP3 MUX", "DEC2", "RX_TX DEC2_INP"},
	{"IIR1 INP3 MUX", "DEC3", "RX_TX DEC3_INP"},
	{"IIR1 INP3 MUX", "RX0", "RX_RX0"},
	{"IIR1 INP3 MUX", "RX1", "RX_RX1"},
	{"IIR1 INP3 MUX", "RX2", "RX_RX2"},
	{"IIR1 INP3 MUX", "RX3", "RX_RX3"},
	{"IIR1 INP3 MUX", "RX4", "RX_RX4"},
	{"IIR1 INP3 MUX", "RX5", "RX_RX5"},

	{"SRC0", NULL, "IIR0"},
	{"SRC1", NULL, "IIR1"},
	{"RX INT0 MIX2 INP", "SRC0", "SRC0"},
	{"RX INT0 MIX2 INP", "SRC1", "SRC1"},
	{"RX INT1 MIX2 INP", "SRC0", "SRC0"},
	{"RX INT1 MIX2 INP", "SRC1", "SRC1"},
	{"RX INT2 MIX2 INP", "SRC0", "SRC0"},
	{"RX INT2 MIX2 INP", "SRC1", "SRC1"},
};

static int rx_macro_component_probe(struct snd_soc_component *component)
{
	struct rx_macro *rx = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, rx->regmap);

	snd_soc_component_update_bits(component, CDC_RX_RX0_RX_PATH_SEC7,
				      CDC_RX_DSM_OUT_DELAY_SEL_MASK,
				      CDC_RX_DSM_OUT_DELAY_TWO_SAMPLE);
	snd_soc_component_update_bits(component, CDC_RX_RX1_RX_PATH_SEC7,
				      CDC_RX_DSM_OUT_DELAY_SEL_MASK,
				      CDC_RX_DSM_OUT_DELAY_TWO_SAMPLE);
	snd_soc_component_update_bits(component, CDC_RX_RX2_RX_PATH_SEC7,
				      CDC_RX_DSM_OUT_DELAY_SEL_MASK,
				      CDC_RX_DSM_OUT_DELAY_TWO_SAMPLE);
	snd_soc_component_update_bits(component, CDC_RX_RX0_RX_PATH_CFG3,
				      CDC_RX_DC_COEFF_SEL_MASK,
				      CDC_RX_DC_COEFF_SEL_TWO);
	snd_soc_component_update_bits(component, CDC_RX_RX1_RX_PATH_CFG3,
				      CDC_RX_DC_COEFF_SEL_MASK,
				      CDC_RX_DC_COEFF_SEL_TWO);
	snd_soc_component_update_bits(component, CDC_RX_RX2_RX_PATH_CFG3,
				      CDC_RX_DC_COEFF_SEL_MASK,
				      CDC_RX_DC_COEFF_SEL_TWO);

	rx->component = component;

	return 0;
}

static int swclk_gate_enable(struct clk_hw *hw)
{
	struct rx_macro *rx = to_rx_macro(hw);

	rx_macro_mclk_enable(rx, true);
	if (rx->reset_swr)
		regmap_update_bits(rx->regmap, CDC_RX_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_RX_SWR_RESET_MASK,
				   CDC_RX_SWR_RESET);

	regmap_update_bits(rx->regmap, CDC_RX_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_RX_SWR_CLK_EN_MASK, 1);

	if (rx->reset_swr)
		regmap_update_bits(rx->regmap, CDC_RX_CLK_RST_CTRL_SWR_CONTROL,
				   CDC_RX_SWR_RESET_MASK, 0);
	rx->reset_swr = false;

	return 0;
}

static void swclk_gate_disable(struct clk_hw *hw)
{
	struct rx_macro *rx = to_rx_macro(hw);

	regmap_update_bits(rx->regmap, CDC_RX_CLK_RST_CTRL_SWR_CONTROL, 
			   CDC_RX_SWR_CLK_EN_MASK, 0);

	rx_macro_mclk_enable(rx, false);
}

static int swclk_gate_is_enabled(struct clk_hw *hw)
{
	struct rx_macro *rx = to_rx_macro(hw);
	int ret, val;

	regmap_read(rx->regmap, CDC_RX_CLK_RST_CTRL_SWR_CONTROL, &val);
	ret = val & BIT(0);

	return ret;
}

static unsigned long swclk_recalc_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	return parent_rate / 2;
}

static const struct clk_ops swclk_gate_ops = {
	.prepare = swclk_gate_enable,
	.unprepare = swclk_gate_disable,
	.is_enabled = swclk_gate_is_enabled,
	.recalc_rate = swclk_recalc_rate,

};

static struct clk *rx_macro_register_mclk_output(struct rx_macro *rx)
{
	struct device *dev = rx->dev;
	struct device_node *np = dev->of_node;
	const char *parent_clk_name = NULL;
	const char *clk_name = "lpass-rx-mclk";
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	parent_clk_name = __clk_get_name(rx->clks[2].clk);

	init.name = clk_name;
	init.ops = &swclk_gate_ops;
	init.flags = 0;
	init.parent_names = &parent_clk_name;
	init.num_parents = 1;
	rx->hw.init = &init;
	hw = &rx->hw;
	ret = clk_hw_register(rx->dev, hw);
	if (ret)
		return ERR_PTR(ret);

	of_clk_add_provider(np, of_clk_src_simple_get, hw->clk);

	return NULL;
}

static const struct snd_soc_component_driver rx_macro_component_drv = {
	.name = "RX-MACRO",
	.probe = rx_macro_component_probe,
	.controls = rx_macro_snd_controls,
	.num_controls = ARRAY_SIZE(rx_macro_snd_controls),
	.dapm_widgets = rx_macro_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rx_macro_dapm_widgets),
	.dapm_routes = rx_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rx_audio_map),
};

static int rx_macro_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rx_macro *rx;
	void __iomem *base;
	int ret;

	rx = devm_kzalloc(dev, sizeof(*rx), GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	rx->clks[0].id = "macro";
	rx->clks[1].id = "dcodec";
	rx->clks[2].id = "mclk";
	rx->clks[3].id = "npl";
	rx->clks[4].id = "fsgen";

	ret = devm_clk_bulk_get_optional(dev, RX_NUM_CLKS_MAX, rx->clks);
	if (ret) {
		dev_err(dev, "Error getting RX Clocks (%d)\n", ret);
		return ret;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rx->regmap = devm_regmap_init_mmio(dev, base, &rx_regmap_config);

	dev_set_drvdata(dev, rx);

	rx->reset_swr = true;
	rx->dev = dev;

	/* set MCLK and NPL rates */
	clk_set_rate(rx->clks[2].clk, MCLK_FREQ);
	clk_set_rate(rx->clks[3].clk, 2 * MCLK_FREQ);

	ret = clk_bulk_prepare_enable(RX_NUM_CLKS_MAX, rx->clks);
	if (ret)
		return ret;

	rx_macro_register_mclk_output(rx);

	ret = devm_snd_soc_register_component(dev, &rx_macro_component_drv,
					      rx_macro_dai,
					      ARRAY_SIZE(rx_macro_dai));
	if (ret)
		clk_bulk_disable_unprepare(RX_NUM_CLKS_MAX, rx->clks);

	return ret;
}

static int rx_macro_remove(struct platform_device *pdev)
{
	struct rx_macro *rx = dev_get_drvdata(&pdev->dev);

	of_clk_del_provider(pdev->dev.of_node);
	clk_bulk_disable_unprepare(RX_NUM_CLKS_MAX, rx->clks);
	return 0;
}

static const struct of_device_id rx_macro_dt_match[] = {
	{ .compatible = "qcom,sc7280-lpass-rx-macro" },
	{ .compatible = "qcom,sm8250-lpass-rx-macro" },
	{ }
};
MODULE_DEVICE_TABLE(of, rx_macro_dt_match);

static struct platform_driver rx_macro_driver = {
	.driver = {
		.name = "rx_macro",
		.of_match_table = rx_macro_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = rx_macro_probe,
	.remove = rx_macro_remove,
};

module_platform_driver(rx_macro_driver);

MODULE_DESCRIPTION("RX macro driver");
MODULE_LICENSE("GPL");
