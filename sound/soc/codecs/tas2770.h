/* SPDX-License-Identifier: GPL-2.0
 *
 * ALSA SoC TAS2770 codec driver
 *
 *  Copyright (C) 2016-2017 Texas Instruments Incorporated - https://www.ti.com/
 */
#ifndef __TAS2770__
#define __TAS2770__

/* Book Control Register (available in page0 of each book) */
#define TAS2770_BOOKCTL_PAGE            0
#define TAS2770_BOOKCTL_REG         127
#define TAS2770_REG(page, reg)        ((page * 128) + reg)
    /* Page */
#define TAS2770_PAGE  TAS2770_REG(0X0, 0x00)
#define TAS2770_PAGE_PAGE_MASK  255
    /* Software Reset */
#define TAS2770_SW_RST  TAS2770_REG(0X0, 0x01)
#define TAS2770_RST  BIT(0)
    /* Power Control */
#define TAS2770_PWR_CTRL  TAS2770_REG(0X0, 0x02)
#define TAS2770_PWR_CTRL_MASK  GENMASK(1, 0)
#define TAS2770_PWR_CTRL_ACTIVE  0x0
#define TAS2770_PWR_CTRL_MUTE  BIT(0)
#define TAS2770_PWR_CTRL_SHUTDOWN  0x2
    /* Playback Configuration Reg0 */
#define TAS2770_PLAY_CFG_REG0  TAS2770_REG(0X0, 0x03)
    /* Playback Configuration Reg1 */
#define TAS2770_PLAY_CFG_REG1  TAS2770_REG(0X0, 0x04)
    /* Playback Configuration Reg2 */
#define TAS2770_PLAY_CFG_REG2  TAS2770_REG(0X0, 0x05)
#define TAS2770_PLAY_CFG_REG2_VMAX 0xc9
    /* Misc Configuration Reg0 */
#define TAS2770_MSC_CFG_REG0  TAS2770_REG(0X0, 0x07)
    /* TDM Configuration Reg0 */
#define TAS2770_TDM_CFG_REG0  TAS2770_REG(0X0, 0x0A)
#define TAS2770_TDM_CFG_REG0_SMP_MASK  BIT(5)
#define TAS2770_TDM_CFG_REG0_SMP_48KHZ  0x0
#define TAS2770_TDM_CFG_REG0_SMP_44_1KHZ  BIT(5)
#define TAS2770_TDM_CFG_REG0_31_MASK  GENMASK(3, 1)
#define TAS2770_TDM_CFG_REG0_31_44_1_48KHZ  0x6
#define TAS2770_TDM_CFG_REG0_31_88_2_96KHZ  0x8
#define TAS2770_TDM_CFG_REG0_31_176_4_192KHZ  0xa
#define TAS2770_TDM_CFG_REG0_FPOL_MASK  BIT(0)
#define TAS2770_TDM_CFG_REG0_FPOL_RSING  0
#define TAS2770_TDM_CFG_REG0_FPOL_FALING  1
    /* TDM Configuration Reg1 */
#define TAS2770_TDM_CFG_REG1  TAS2770_REG(0X0, 0x0B)
#define TAS2770_TDM_CFG_REG1_MASK	GENMASK(5, 1)
#define TAS2770_TDM_CFG_REG1_51_SHIFT  1
#define TAS2770_TDM_CFG_REG1_RX_MASK  BIT(0)
#define TAS2770_TDM_CFG_REG1_RX_RSING  0x0
#define TAS2770_TDM_CFG_REG1_RX_FALING  BIT(0)
    /* TDM Configuration Reg2 */
#define TAS2770_TDM_CFG_REG2  TAS2770_REG(0X0, 0x0C)
#define TAS2770_TDM_CFG_REG2_RXW_MASK	GENMASK(3, 2)
#define TAS2770_TDM_CFG_REG2_RXW_16BITS  0x0
#define TAS2770_TDM_CFG_REG2_RXW_24BITS  0x8
#define TAS2770_TDM_CFG_REG2_RXW_32BITS  0xc
#define TAS2770_TDM_CFG_REG2_RXS_MASK    GENMASK(1, 0)
#define TAS2770_TDM_CFG_REG2_RXS_16BITS  0x0
#define TAS2770_TDM_CFG_REG2_RXS_24BITS  BIT(0)
#define TAS2770_TDM_CFG_REG2_RXS_32BITS  0x2
    /* TDM Configuration Reg3 */
#define TAS2770_TDM_CFG_REG3  TAS2770_REG(0X0, 0x0D)
#define TAS2770_TDM_CFG_REG3_RXS_MASK  GENMASK(7, 4)
#define TAS2770_TDM_CFG_REG3_RXS_SHIFT 0x4
#define TAS2770_TDM_CFG_REG3_30_MASK  GENMASK(3, 0)
#define TAS2770_TDM_CFG_REG3_30_SHIFT 0
    /* TDM Configuration Reg5 */
#define TAS2770_TDM_CFG_REG5  TAS2770_REG(0X0, 0x0F)
#define TAS2770_TDM_CFG_REG5_VSNS_MASK  BIT(6)
#define TAS2770_TDM_CFG_REG5_VSNS_ENABLE  BIT(6)
#define TAS2770_TDM_CFG_REG5_50_MASK	GENMASK(5, 0)
    /* TDM Configuration Reg6 */
#define TAS2770_TDM_CFG_REG6  TAS2770_REG(0X0, 0x10)
#define TAS2770_TDM_CFG_REG6_ISNS_MASK  BIT(6)
#define TAS2770_TDM_CFG_REG6_ISNS_ENABLE  BIT(6)
#define TAS2770_TDM_CFG_REG6_50_MASK  GENMASK(5, 0)
    /* TDM Configuration Reg10 */
#define TAS2770_TDM_CFG_REG7  TAS2770_REG(0X0, 0x11)
#define TAS2770_TDM_CFG_REG7_PDM_MASK  BIT(6)
#define TAS2770_TDM_CFG_REG7_PDM_ENABLE  BIT(6)
#define TAS2770_TDM_CFG_REG7_50_MASK	GENMASK(5, 0)
    /* Brown Out Prevention Reg0 */
#define TAS2770_BO_PRV_REG0  TAS2770_REG(0X0, 0x1B)
    /* Interrupt MASK Reg0 */
#define TAS2770_INT_MASK_REG0  TAS2770_REG(0X0, 0x20)
#define TAS2770_INT_REG0_DEFAULT  0xfc
#define TAS2770_INT_MASK_REG0_DISABLE 0xff
    /* Interrupt MASK Reg1 */
#define TAS2770_INT_MASK_REG1  TAS2770_REG(0X0, 0x21)
#define TAS2770_INT_REG1_DEFAULT  0xb1
#define TAS2770_INT_MASK_REG1_DISABLE 0xff
    /* Live-Interrupt Reg0 */
#define TAS2770_LVE_INT_REG0  TAS2770_REG(0X0, 0x22)
    /* Live-Interrupt Reg1 */
#define TAS2770_LVE_INT_REG1  TAS2770_REG(0X0, 0x23)
    /* Latched-Interrupt Reg0 */
#define TAS2770_LAT_INT_REG0  TAS2770_REG(0X0, 0x24)
#define TAS2770_LAT_INT_REG0_OCE_FLG  BIT(1)
#define TAS2770_LAT_INT_REG0_OTE_FLG  BIT(0)
    /* Latched-Interrupt Reg1 */
#define TAS2770_LAT_INT_REG1  TAS2770_REG(0X0, 0x25)
#define TAS2770_LAT_INT_REG1_VBA_TOV  BIT(3)
#define TAS2770_LAT_INT_REG1_VBA_TUV  BIT(2)
#define TAS2770_LAT_INT_REG1_BOUT_FLG  BIT(1)
    /* VBAT MSB */
#define TAS2770_VBAT_MSB  TAS2770_REG(0X0, 0x27)
    /* VBAT LSB */
#define TAS2770_VBAT_LSB  TAS2770_REG(0X0, 0x28)
    /* TEMP MSB */
#define TAS2770_TEMP_MSB  TAS2770_REG(0X0, 0x29)
    /* TEMP LSB */
#define TAS2770_TEMP_LSB  TAS2770_REG(0X0, 0x2A)
    /* Interrupt Configuration */
#define TAS2770_INT_CFG  TAS2770_REG(0X0, 0x30)
    /* Misc IRQ */
#define TAS2770_MISC_IRQ  TAS2770_REG(0X0, 0x32)
    /* Clock Configuration */
#define TAS2770_CLK_CGF  TAS2770_REG(0X0, 0x3C)
    /* TDM Clock detection monitor */
#define TAS2770_TDM_CLK_DETC  TAS2770_REG(0X0, 0x77)
    /* Revision and PG ID */
#define TAS2770_REV_AND_GPID  TAS2770_REG(0X0, 0x7D)

#define TAS2770_POWER_ACTIVE	0
#define TAS2770_POWER_MUTE	BIT(0)
#define TAS2770_POWER_SHUTDOWN	BIT(1)

#define ERROR_OVER_CURRENT  BIT(0)
#define ERROR_DIE_OVERTEMP  BIT(1)
#define ERROR_OVER_VOLTAGE  BIT(2)
#define ERROR_UNDER_VOLTAGE BIT(3)
#define ERROR_BROWNOUT      BIT(4)
#define ERROR_CLASSD_PWR    BIT(5)

struct tas2770_priv {
	struct snd_soc_component *component;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *sdz_gpio;
	struct regmap *regmap;
	struct device *dev;
	int v_sense_slot;
	int i_sense_slot;
	int pdm_slot;
	bool dac_powered;
	bool unmuted;
};

#endif /* __TAS2770__ */
