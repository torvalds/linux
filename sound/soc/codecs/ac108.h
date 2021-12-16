/*
 * ac108.h --  ac108 ALSA Soc Audio driver
 *
 * Author: panjunwen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _AC108_H
#define _AC108_H


/*** AC108 Codec Register Define***/

//Chip Reset
#define CHIP_RST			0x00
#define CHIP_RST_VAL		0x12

//Power Control
#define PWR_CTRL1			0x01
#define PWR_CTRL2			0x02
#define PWR_CTRL3			0x03
#define PWR_CTRL4			0x04
#define PWR_CTRL5			0x05
#define PWR_CTRL6			0x06
#define PWR_CTRL7			0x07
#define PWR_CTRL8			0x08
#define PWR_CTRL9			0x09

//PLL Configure Control
#define PLL_CTRL1			0x10
#define PLL_CTRL2			0x11
#define PLL_CTRL3			0x12
#define PLL_CTRL4			0x13
#define PLL_CTRL5			0x14
#define PLL_CTRL6			0x16
#define PLL_CTRL7			0x17
#define PLL_LOCK_CTRL		0x18

//System Clock Control
#define SYSCLK_CTRL			0x20
#define MOD_CLK_EN			0x21
#define MOD_RST_CTRL		0x22
#define DSM_CLK_CTRL		0x25

//I2S Common Control
#define I2S_CTRL			0x30
#define I2S_BCLK_CTRL		0x31
#define I2S_LRCK_CTRL1		0x32
#define I2S_LRCK_CTRL2		0x33
#define I2S_FMT_CTRL1		0x34
#define I2S_FMT_CTRL2		0x35
#define I2S_FMT_CTRL3		0x36

//I2S TX1 Control
#define I2S_TX1_CTRL1		0x38
#define I2S_TX1_CTRL2		0x39
#define I2S_TX1_CTRL3		0x3A
#define I2S_TX1_CHMP_CTRL1	0x3C
#define I2S_TX1_CHMP_CTRL2	0x3D
#define I2S_TX1_CHMP_CTRL3	0x3E
#define I2S_TX1_CHMP_CTRL4	0x3F

//I2S TX2 Control
#define I2S_TX2_CTRL1		0x40
#define I2S_TX2_CTRL2		0x41
#define I2S_TX2_CTRL3		0x42
#define I2S_TX2_CHMP_CTRL1	0x44
#define I2S_TX2_CHMP_CTRL2	0x45
#define I2S_TX2_CHMP_CTRL3	0x46
#define I2S_TX2_CHMP_CTRL4	0x47

//I2S RX1 Control
#define I2S_RX1_CTRL1		0x50
#define I2S_RX1_CHMP_CTRL1	0x54
#define I2S_RX1_CHMP_CTRL2	0x55
#define I2S_RX1_CHMP_CTRL3	0x56
#define I2S_RX1_CHMP_CTRL4	0x57

//I2S Loopback Debug
#define I2S_LPB_DEBUG		0x58

//ADC Common Control
#define ADC_SPRC			0x60
#define ADC_DIG_EN			0x61
#define DMIC_EN				0x62
#define ADC_DSR				0x63
#define ADC_FIR				0x64
#define ADC_DDT_CTRL		0x65

//HPF Control
#define HPF_EN				0x66
#define HPF_COEF_REGH1		0x67
#define HPF_COEF_REGH2		0x68
#define HPF_COEF_REGL1		0x69
#define HPF_COEF_REGL2		0x6A
#define HPF_GAIN_REGH1		0x6B
#define HPF_GAIN_REGH2		0x6C
#define HPF_GAIN_REGL1		0x6D
#define HPF_GAIN_REGL2		0x6E

//ADC Digital Channel Volume Control
#define ADC1_DVOL_CTRL		0x70
#define ADC2_DVOL_CTRL		0x71
#define ADC3_DVOL_CTRL		0x72
#define ADC4_DVOL_CTRL		0x73

//ADC Digital Mixer Source and Gain Control
#define ADC1_DMIX_SRC		0x76
#define ADC2_DMIX_SRC		0x77
#define ADC3_DMIX_SRC		0x78
#define ADC4_DMIX_SRC		0x79

//ADC Digital Debug Control
#define ADC_DIG_DEBUG		0x7F

//I2S Pad Drive Control
#define I2S_DAT_PADDRV_CTRL	0x80
#define I2S_CLK_PADDRV_CTRL	0x81

//Analog PGA Control
#define ANA_PGA1_CTRL		0x90
#define ANA_PGA2_CTRL		0x91
#define ANA_PGA3_CTRL		0x92
#define ANA_PGA4_CTRL		0x93

//MIC Offset Control
#define MIC_OFFSET_CTRL1	0x96
#define MIC_OFFSET_CTRL2	0x97
#define MIC1_OFFSET_STATU1	0x98
#define MIC1_OFFSET_STATU2	0x99
#define MIC2_OFFSET_STATU1	0x9A
#define MIC2_OFFSET_STATU2	0x9B
#define MIC3_OFFSET_STATU1	0x9C
#define MIC3_OFFSET_STATU2	0x9D
#define MIC4_OFFSET_STATU1	0x9E
#define MIC4_OFFSET_STATU2	0x9F

//ADC1 Analog Control
#define ANA_ADC1_CTRL1		0xA0
#define ANA_ADC1_CTRL2		0xA1
#define ANA_ADC1_CTRL3		0xA2
#define ANA_ADC1_CTRL4		0xA3
#define ANA_ADC1_CTRL5		0xA4
#define ANA_ADC1_CTRL6		0xA5
#define ANA_ADC1_CTRL7		0xA6

//ADC2 Analog Control
#define ANA_ADC2_CTRL1		0xA7
#define ANA_ADC2_CTRL2		0xA8
#define ANA_ADC2_CTRL3		0xA9
#define ANA_ADC2_CTRL4		0xAA
#define ANA_ADC2_CTRL5		0xAB
#define ANA_ADC2_CTRL6		0xAC
#define ANA_ADC2_CTRL7		0xAD

//ADC3 Analog Control
#define ANA_ADC3_CTRL1		0xAE
#define ANA_ADC3_CTRL2		0xAF
#define ANA_ADC3_CTRL3		0xB0
#define ANA_ADC3_CTRL4		0xB1
#define ANA_ADC3_CTRL5		0xB2
#define ANA_ADC3_CTRL6		0xB3
#define ANA_ADC3_CTRL7		0xB4

//ADC4 Analog Control
#define ANA_ADC4_CTRL1		0xB5
#define ANA_ADC4_CTRL2		0xB6
#define ANA_ADC4_CTRL3		0xB7
#define ANA_ADC4_CTRL4		0xB8
#define ANA_ADC4_CTRL5		0xB9
#define ANA_ADC4_CTRL6		0xBA
#define ANA_ADC4_CTRL7		0xBB

//GPIO Configure
#define GPIO_CFG1			0xC0
#define GPIO_CFG2			0xC1
#define GPIO_DAT			0xC2
#define GPIO_DRV			0xC3
#define GPIO_PULL			0xC4
#define GPIO_INT_CFG		0xC5
#define GPIO_INT_EN			0xC6
#define GPIO_INT_STATUS		0xC7

//Misc
#define BGTC_DAT			0xD1
#define BGVC_DAT			0xD2
#define PRNG_CLK_CTRL		0xDF



/*** AC108 Codec Register Bit Define***/

/*PWR_CTRL1*/
#define CP12_CTRL				4
#define CP12_SENSE_SELECT		3

/*PWR_CTRL2*/
#define CP12_SENSE_FILT			6
#define CP12_COMP_FF_EN			3
#define CP12_FORCE_ENABLE		2
#define CP12_FORCE_RSTB			1

/*PWR_CTRL3*/
#define LDO33DIG_CTRL			0

/*PWR_CTRL6*/
#define LDO33ANA_2XHDRM			2
#define LDO33ANA_ENABLE			0

/*PWR_CTRL7*/
#define VREF_SEL				3
#define VREF_FASTSTART_ENABLE	1
#define VREF_ENABLE				0

/*PWR_CTRL9*/
#define VREFP_FASTSTART_ENABLE	7
#define VREFP_RESCTRL			5
#define VREFP_LPMODE			4
#define IGEN_TRIM				1
#define VREFP_ENABLE			0


/*PLL_CTRL1*/
#define PLL_IBIAS				4
#define PLL_NDET				3
#define PLL_LOCKED_STATUS		2
#define PLL_COM_EN				1
#define PLL_EN					0

/*PLL_CTRL2*/
#define PLL_PREDIV2				5
#define PLL_PREDIV1				0

/*PLL_CTRL3*/
#define PLL_LOOPDIV_MSB			0

/*PLL_CTRL4*/
#define PLL_LOOPDIV_LSB			0

/*PLL_CTRL5*/
#define PLL_POSTDIV2			5
#define PLL_POSTDIV1			0

/*PLL_CTRL6*/
#define PLL_LDO					6
#define PLL_CP					0

/*PLL_CTRL7*/
#define PLL_CAP					6
#define PLL_RES					4
#define PLL_TEST_EN				0

/*PLL_LOCK_CTRL*/
#define LOCK_LEVEL1				2
#define LOCK_LEVEL2				1
#define PLL_LOCK_EN				0


/*SYSCLK_CTRL*/
#define PLLCLK_EN				7
#define PLLCLK_SRC				4
#define SYSCLK_SRC				3
#define SYSCLK_EN				0

/*MOD_CLK_EN & MOD_RST_CTRL*/
#define I2S						7
#define ADC_DIGITAL				4
#define MIC_OFFSET_CALIBRATION	1
#define ADC_ANALOG				0

/*DSM_CLK_CTRL*/
#define MIC_OFFSET_DIV			4
#define DSM_CLK_SEL				0


/*I2S_CTRL*/
#define BCLK_IOEN				7
#define LRCK_IOEN				6
#define SDO2_EN					5
#define SDO1_EN					4
#define TXEN					2
#define RXEN					1
#define GEN						0

/*I2S_BCLK_CTRL*/
#define EDGE_TRANSFER			5
#define BCLK_POLARITY			4
#define BCLKDIV					0

/*I2S_LRCK_CTRL1*/
#define LRCK_POLARITY			4
#define LRCK_PERIODH			0

/*I2S_LRCK_CTRL2*/
#define LRCK_PERIODL			0

/*I2S_FMT_CTRL1*/
#define ENCD_SEL				6
#define MODE_SEL				4
#define TX2_OFFSET				3
#define TX1_OFFSET				2
#define TX_SLOT_HIZ				1
#define TX_STATE				0

/*I2S_FMT_CTRL2*/
#define SLOT_WIDTH_SEL			4
#define SAMPLE_RESOLUTION		0

/*I2S_FMT_CTRL3*/
#define TX_MLS					7
#define SEXT					5
#define OUT2_MUTE				4
#define OUT1_MUTE				3
#define LRCK_WIDTH				2
#define TX_PDM					0


/*I2S_TX1_CTRL1*/
#define TX1_CHSEL				0

/*I2S_TX1_CTRL2*/
#define TX1_CH8_EN				7
#define TX1_CH7_EN				6
#define TX1_CH6_EN				5
#define TX1_CH5_EN				4
#define TX1_CH4_EN				3
#define TX1_CH3_EN				2
#define TX1_CH2_EN				1
#define TX1_CH1_EN				0

/*I2S_TX1_CTRL3*/
#define TX1_CH16_EN				7
#define TX1_CH15_EN				6
#define TX1_CH14_EN				5
#define TX1_CH13_EN				4
#define TX1_CH12_EN				3
#define TX1_CH11_EN				2
#define TX1_CH10_EN				1
#define TX1_CH9_EN				0

/*I2S_TX1_CHMP_CTRL1*/
#define TX1_CH4_MAP				6
#define TX1_CH3_MAP				4
#define TX1_CH2_MAP				2
#define TX1_CH1_MAP				0

/*I2S_TX1_CHMP_CTRL2*/
#define TX1_CH8_MAP				6
#define TX1_CH7_MAP				4
#define TX1_CH6_MAP				2
#define TX1_CH5_MAP				0

/*I2S_TX1_CHMP_CTRL3*/
#define TX1_CH12_MAP			6
#define TX1_CH11_MAP			4
#define TX1_CH10_MAP			2
#define TX1_CH9_MAP				0

/*I2S_TX1_CHMP_CTRL4*/
#define TX1_CH16_MAP			6
#define TX1_CH15_MAP			4
#define TX1_CH14_MAP			2
#define TX1_CH13_MAP			0


/*I2S_TX2_CTRL1*/
#define TX2_CHSEL				0

/*I2S_TX2_CHMP_CTRL1*/
#define TX2_CH4_MAP				6
#define TX2_CH3_MAP				4
#define TX2_CH2_MAP				2
#define TX2_CH1_MAP				0

/*I2S_TX2_CHMP_CTRL2*/
#define TX2_CH8_MAP				6
#define TX2_CH7_MAP				4
#define TX2_CH6_MAP				2
#define TX2_CH5_MAP				0

/*I2S_TX2_CHMP_CTRL3*/
#define TX2_CH12_MAP			6
#define TX2_CH11_MAP			4
#define TX2_CH10_MAP			2
#define TX2_CH9_MAP				0

/*I2S_TX2_CHMP_CTRL4*/
#define TX2_CH16_MAP			6
#define TX2_CH15_MAP			4
#define TX2_CH14_MAP			2
#define TX2_CH13_MAP			0


/*I2S_RX1_CTRL1*/
#define RX1_CHSEL				0

/*I2S_RX1_CHMP_CTRL1*/
#define RX1_CH4_MAP				6
#define RX1_CH3_MAP				4
#define RX1_CH2_MAP				2
#define RX1_CH1_MAP				0

/*I2S_RX1_CHMP_CTRL2*/
#define RX1_CH8_MAP				6
#define RX1_CH7_MAP				4
#define RX1_CH6_MAP				2
#define RX1_CH5_MAP				0

/*I2S_RX1_CHMP_CTRL3*/
#define RX1_CH12_MAP			6
#define RX1_CH11_MAP			4
#define RX1_CH10_MAP			2
#define RX1_CH9_MAP				0

/*I2S_RX1_CHMP_CTRL4*/
#define RX1_CH16_MAP			6
#define RX1_CH15_MAP			4
#define RX1_CH14_MAP			2
#define RX1_CH13_MAP			0


/*I2S_LPB_DEBUG*/
#define I2S_LPB_DEBUG_EN		0


/*ADC_SPRC*/
#define ADC_FS_I2S1				0

/*ADC_DIG_EN*/
#define DG_EN					4
#define ENAD4					3
#define ENAD3					2
#define ENAD2					1
#define ENAD1					0

/*DMIC_EN*/
#define DMIC2_EN				1
#define DMIC1_EN				0

/*ADC_DSR*/
#define DIG_ADC4_SRS			6
#define DIG_ADC3_SRS			4
#define DIG_ADC2_SRS			2
#define DIG_ADC1_SRS			0

/*ADC_DDT_CTRL*/
#define ADOUT_DLY_EN			2
#define ADOUT_DTS				0


/*HPF_EN*/
#define DIG_ADC4_HPF_EN			3
#define DIG_ADC3_HPF_EN			2
#define DIG_ADC2_HPF_EN			1
#define DIG_ADC1_HPF_EN			0


/*ADC1_DMIX_SRC*/
#define ADC1_ADC4_DMXL_GC		7
#define ADC1_ADC3_DMXL_GC		6
#define ADC1_ADC2_DMXL_GC		5
#define ADC1_ADC1_DMXL_GC		4
#define ADC1_ADC4_DMXL_SRC		3
#define ADC1_ADC3_DMXL_SRC		2
#define ADC1_ADC2_DMXL_SRC		1
#define ADC1_ADC1_DMXL_SRC		0

/*ADC2_DMIX_SRC*/
#define ADC2_ADC4_DMXL_GC		7
#define ADC2_ADC3_DMXL_GC		6
#define ADC2_ADC2_DMXL_GC		5
#define ADC2_ADC1_DMXL_GC		4
#define ADC2_ADC4_DMXL_SRC		3
#define ADC2_ADC3_DMXL_SRC		2
#define ADC2_ADC2_DMXL_SRC		1
#define ADC2_ADC1_DMXL_SRC		0

/*ADC3_DMIX_SRC*/
#define ADC3_ADC4_DMXL_GC		7
#define ADC3_ADC3_DMXL_GC		6
#define ADC3_ADC2_DMXL_GC		5
#define ADC3_ADC1_DMXL_GC		4
#define ADC3_ADC4_DMXL_SRC		3
#define ADC3_ADC3_DMXL_SRC		2
#define ADC3_ADC2_DMXL_SRC		1
#define ADC3_ADC1_DMXL_SRC		0

/*ADC4_DMIX_SRC*/
#define ADC4_ADC4_DMXL_GC		7
#define ADC4_ADC3_DMXL_GC		6
#define ADC4_ADC2_DMXL_GC		5
#define ADC4_ADC1_DMXL_GC		4
#define ADC4_ADC4_DMXL_SRC		3
#define ADC4_ADC3_DMXL_SRC		2
#define ADC4_ADC2_DMXL_SRC		1
#define ADC4_ADC1_DMXL_SRC		0


/*ADC_DIG_DEBUG*/
#define ADC_PTN_SEL				0


/*I2S_DAT_PADDRV_CTRL*/
#define TX2_DAT_DRV				4
#define TX1_DAT_DRV				0

/*I2S_CLK_PADDRV_CTRL*/
#define LRCK_DRV				4
#define BCLK_DRV				0


/*ANA_PGA1_CTRL*/
#define ADC1_ANALOG_PGA			1
#define ADC1_ANALOG_PGA_STEP	0

/*ANA_PGA2_CTRL*/
#define ADC2_ANALOG_PGA			1
#define ADC2_ANALOG_PGA_STEP	0

/*ANA_PGA3_CTRL*/
#define ADC3_ANALOG_PGA			1
#define ADC3_ANALOG_PGA_STEP	0

/*ANA_PGA4_CTRL*/
#define ADC4_ANALOG_PGA			1
#define ADC4_ANALOG_PGA_STEP	0


/*MIC_OFFSET_CTRL1*/
#define MIC_OFFSET_CAL_EN4		3
#define MIC_OFFSET_CAL_EN3		2
#define MIC_OFFSET_CAL_EN2		1
#define MIC_OFFSET_CAL_EN1		0

/*MIC_OFFSET_CTRL2*/
#define MIC_OFFSET_CAL_GAIN		3
#define MIC_OFFSET_CAL_CHANNEL	1
#define MIC_OFFSET_CAL_EN_ONCE	0

/*MIC1_OFFSET_STATU1*/
#define MIC1_OFFSET_CAL_DONE	7
#define MIC1_OFFSET_CAL_RUN_STA	6
#define MIC1_OFFSET_MSB			0

/*MIC1_OFFSET_STATU2*/
#define MIC1_OFFSET_LSB			0

/*MIC2_OFFSET_STATU1*/
#define MIC2_OFFSET_CAL_DONE	7
#define MIC2_OFFSET_CAL_RUN_STA	6
#define MIC2_OFFSET_MSB			0

/*MIC2_OFFSET_STATU2*/
#define MIC2_OFFSET_LSB			0

/*MIC3_OFFSET_STATU1*/
#define MIC3_OFFSET_CAL_DONE	7
#define MIC3_OFFSET_CAL_RUN_STA	6
#define MIC3_OFFSET_MSB			0

/*MIC3_OFFSET_STATU2*/
#define MIC3_OFFSET_LSB			0

/*MIC4_OFFSET_STATU1*/
#define MIC4_OFFSET_CAL_DONE	7
#define MIC4_OFFSET_CAL_RUN_STA	6
#define MIC4_OFFSET_MSB			0

/*MIC4_OFFSET_STATU2*/
#define MIC4_OFFSET_LSB			0


/*ANA_ADC1_CTRL1*/
#define ADC1_PGA_BYPASS			7
#define ADC1_PGA_BYP_RCM		6
#define ADC1_PGA_CTRL_RCM		4
#define ADC1_PGA_MUTE			3
#define ADC1_DSM_ENABLE			2
#define ADC1_PGA_ENABLE			1
#define ADC1_MICBIAS_EN			0

/*ANA_ADC1_CTRL3*/
#define ADC1_ANA_CAL_EN			5
#define ADC1_SEL_OUT_EDGE		3
#define ADC1_DSM_DISABLE		2
#define ADC1_VREFP_DISABLE		1
#define ADC1_AAF_DISABLE		0

/*ANA_ADC1_CTRL6*/
#define PGA_CTRL_TC				6
#define PGA_CTRL_RC				4
#define PGA_CTRL_I_LIN			2
#define PGA_CTRL_I_IN			0

/*ANA_ADC1_CTRL7*/
#define PGA_CTRL_HI_Z			7
#define PGA_CTRL_SHORT_RF		6
#define PGA_CTRL_VCM_VG			4
#define PGA_CTRL_VCM_IN			0


/*ANA_ADC2_CTRL1*/
#define ADC2_PGA_BYPASS			7
#define ADC2_PGA_BYP_RCM		6
#define ADC2_PGA_CTRL_RCM		4
#define ADC2_PGA_MUTE			3
#define ADC2_DSM_ENABLE			2
#define ADC2_PGA_ENABLE			1
#define ADC2_MICBIAS_EN			0

/*ANA_ADC2_CTRL3*/
#define ADC2_ANA_CAL_EN			5
#define ADC2_SEL_OUT_EDGE		3
#define ADC2_DSM_DISABLE		2
#define ADC2_VREFP_DISABLE		1
#define ADC2_AAF_DISABLE		0

/*ANA_ADC2_CTRL6*/
#define PGA_CTRL_IBOOST			7
#define PGA_CTRL_IQCTRL			6
#define PGA_CTRL_OABIAS			4
#define PGA_CTRL_CMLP_DIS		3
#define PGA_CTRL_PDB_RIN		2
#define PGA_CTRL_PEAKDET		0

/*ANA_ADC2_CTRL7*/
#define AAF_LPMODE_EN			7
#define AAF_STG2_IB_SEL			4
#define AAFDSM_IB_DIV2			3
#define AAF_STG1_IB_SEL			0


/*ANA_ADC3_CTRL1*/
#define ADC3_PGA_BYPASS			7
#define ADC3_PGA_BYP_RCM		6
#define ADC3_PGA_CTRL_RCM		4
#define ADC3_PGA_MUTE			3
#define ADC3_DSM_ENABLE			2
#define ADC3_PGA_ENABLE			1
#define ADC3_MICBIAS_EN			0

/*ANA_ADC3_CTRL3*/
#define ADC3_ANA_CAL_EN			5
#define ADC3_INVERT_CLK			4
#define ADC3_SEL_OUT_EDGE		3
#define ADC3_DSM_DISABLE		2
#define ADC3_VREFP_DISABLE		1
#define ADC3_AAF_DISABLE		0

/*ANA_ADC3_CTRL7*/
#define DSM_COMP_IB_SEL			6
#define DSM_OTA_CTRL			4
#define DSM_LPMODE				3
#define DSM_OTA_IB_SEL			0


/*ANA_ADC4_CTRL1*/
#define ADC4_PGA_BYPASS			7
#define ADC4_PGA_BYP_RCM		6
#define ADC4_PGA_CTRL_RCM		4
#define ADC4_PGA_MUTE			3
#define ADC4_DSM_ENABLE			2
#define ADC4_PGA_ENABLE			1
#define ADC4_MICBIAS_EN			0

/*ANA_ADC4_CTRL3*/
#define ADC4_ANA_CAL_EN			5
#define ADC4_SEL_OUT_EDGE		3
#define ADC4_DSM_DISABLE		2
#define ADC4_VREFP_DISABLE		1
#define ADC4_AAF_DISABLE		0

/*ANA_ADC4_CTRL6*/
#define DSM_DEMOFF				5
#define DSM_EN_DITHER			4
#define DSM_VREFP_LPMODE		2
#define DSM_VREFP_OUTCTRL		0

/*ANA_ADC4_CTRL7*/
#define CK8M_EN					5
#define OSC_EN					4
#define ADC4_CLK_GATING			3
#define ADC3_CLK_GATING			2
#define ADC2_CLK_GATING			1
#define ADC1_CLK_GATING			0


/*GPIO_CFG1*/
#define GPIO2_SELECT			4
#define GPIO1_SELECT			0

/*GPIO_CFG2*/
#define GPIO4_SELECT			4
#define GPIO3_SELECT			0

/*GPIO_DAT*///order???
#define GPIO4_DAT				3
#define GPIO3_DAT				2
#define GPIO2_DAT				1
#define GPIO1_DAT				0

/*GPIO_DRV*/
#define GPIO4_DRV				6
#define GPIO3_DRV				4
#define GPIO2_DRV				2
#define GPIO1_DRV				0

/*GPIO_PULL*/
#define GPIO4_PULL				6
#define GPIO3_PULL				4
#define GPIO2_PULL				2
#define GPIO1_PULL				0

/*GPIO_INT_CFG*/
#define GPIO4_EINT_CFG			6
#define GPIO3_EINT_CFG			4
#define GPIO2_EINT_CFG			2
#define GPIO1_EINT_CFG			0

/*GPIO_INT_EN*///order???
#define GPIO4_EINT_EN			3
#define GPIO3_EINT_EN			2
#define GPIO2_EINT_EN			1
#define GPIO1_EINT_EN			0

/*GPIO_INT_STATUS*///order???
#define GPIO4_EINT_STA			3
#define GPIO3_EINT_STA			2
#define GPIO2_EINT_STA			1
#define GPIO1_EINT_STA			0


/*PRNG_CLK_CTRL*/
#define PRNG_CLK_EN				1
#define PRNG_CLK_POS			0



/*** Some Config Value ***/

//[SYSCLK_CTRL]: PLLCLK_SRC
#define PLLCLK_SRC_MCLK			0
#define PLLCLK_SRC_BCLK			1
#define PLLCLK_SRC_GPIO2		2
#define PLLCLK_SRC_GPIO3		3

//[SYSCLK_CTRL]: SYSCLK_SRC
#define SYSCLK_SRC_MCLK			0
#define SYSCLK_SRC_PLL			1

//I2S BCLK POLARITY Control
#define BCLK_NORMAL_DRIVE_N_SAMPLE_P	0
#define BCLK_INVERT_DRIVE_P_SAMPLE_N	1

//I2S LRCK POLARITY Control
#define	LRCK_LEFT_LOW_RIGHT_HIGH		0
#define LRCK_LEFT_HIGH_RIGHT_LOW		1

//I2S Format Selection
#define PCM_FORMAT						0
#define LEFT_JUSTIFIED_FORMAT			1
#define RIGHT_JUSTIFIED_FORMAT			2


//I2S data protocol types

#define IS_ENCODING_MODE		 0

#endif

