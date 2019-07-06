/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ALSA SoC CS43130 codec driver
 *
 * Copyright 2017 Cirrus Logic, Inc.
 *
 * Author: Li Xu <li.xu@cirrus.com>
 */

#ifndef __CS43130_H__
#define __CS43130_H__

/* CS43130 registers addresses */
/* all reg address is shifted by a byte for control byte to be LSB */
#define CS43130_FIRSTREG	0x010000
#define CS43130_LASTREG		0x190000
#define CS43130_CHIP_ID		0x00043130
#define CS4399_CHIP_ID		0x00043990
#define CS43131_CHIP_ID		0x00043131
#define CS43198_CHIP_ID		0x00043198
#define CS43130_DEVID_AB	0x010000	/* Device ID A & B [RO] */
#define CS43130_DEVID_CD	0x010001	/* Device ID C & D [RO] */
#define CS43130_DEVID_E		0x010002	/* Device ID E [RO] */
#define CS43130_FAB_ID		0x010003        /* Fab ID [RO] */
#define CS43130_REV_ID		0x010004        /* Revision ID [RO] */
#define CS43130_SUBREV_ID	0x010005        /* Subrevision ID */
#define CS43130_SYS_CLK_CTL_1	0x010006	/* System Clocking Ctl 1 */
#define CS43130_SP_SRATE	0x01000B        /* Serial Port Sample Rate */
#define CS43130_SP_BITSIZE	0x01000C        /* Serial Port Bit Size */
#define CS43130_PAD_INT_CFG	0x01000D	/* Pad Interface Config */
#define CS43130_DXD1            0x010010        /* DXD1 */
#define CS43130_DXD7            0x010025        /* DXD7 */
#define CS43130_DXD19           0x010026        /* DXD19 */
#define CS43130_DXD17           0x010027        /* DXD17 */
#define CS43130_DXD18           0x010028        /* DXD18 */
#define CS43130_DXD12           0x01002C        /* DXD12 */
#define CS43130_DXD8            0x01002E        /* DXD8 */
#define CS43130_PWDN_CTL	0x020000        /* Power Down Ctl */
#define CS43130_DXD2            0x020019        /* DXD2 */
#define CS43130_CRYSTAL_SET	0x020052	/* Crystal Setting */
#define CS43130_PLL_SET_1	0x030001        /* PLL Setting 1 */
#define CS43130_PLL_SET_2	0x030002        /* PLL Setting 2 */
#define CS43130_PLL_SET_3	0x030003        /* PLL Setting 3 */
#define CS43130_PLL_SET_4	0x030004        /* PLL Setting 4 */
#define CS43130_PLL_SET_5	0x030005        /* PLL Setting 5 */
#define CS43130_PLL_SET_6	0x030008        /* PLL Setting 6 */
#define CS43130_PLL_SET_7	0x03000A        /* PLL Setting 7 */
#define CS43130_PLL_SET_8	0x03001B        /* PLL Setting 8 */
#define CS43130_PLL_SET_9	0x040002        /* PLL Setting 9 */
#define CS43130_PLL_SET_10	0x040003        /* PLL Setting 10 */
#define CS43130_CLKOUT_CTL	0x040004        /* CLKOUT Ctl */
#define CS43130_ASP_NUM_1	0x040010        /* ASP Numerator 1 */
#define CS43130_ASP_NUM_2	0x040011        /* ASP Numerator 2 */
#define CS43130_ASP_DEN_1	0x040012	/* ASP Denominator 1 */
#define CS43130_ASP_DEN_2	0x040013	/* ASP Denominator 2 */
#define CS43130_ASP_LRCK_HI_TIME_1 0x040014	/* ASP LRCK High Time 1 */
#define CS43130_ASP_LRCK_HI_TIME_2 0x040015	/* ASP LRCK High Time 2 */
#define CS43130_ASP_LRCK_PERIOD_1  0x040016	/* ASP LRCK Period 1 */
#define CS43130_ASP_LRCK_PERIOD_2  0x040017	/* ASP LRCK Period 2 */
#define CS43130_ASP_CLOCK_CONF	0x040018	/* ASP Clock Config */
#define CS43130_ASP_FRAME_CONF	0x040019	/* ASP Frame Config */
#define CS43130_XSP_NUM_1	0x040020        /* XSP Numerator 1 */
#define CS43130_XSP_NUM_2	0x040021        /* XSP Numerator 2 */
#define CS43130_XSP_DEN_1	0x040022	/* XSP Denominator 1 */
#define CS43130_XSP_DEN_2	0x040023	/* XSP Denominator 2 */
#define CS43130_XSP_LRCK_HI_TIME_1 0x040024	/* XSP LRCK High Time 1 */
#define CS43130_XSP_LRCK_HI_TIME_2 0x040025	/* XSP LRCK High Time 2 */
#define CS43130_XSP_LRCK_PERIOD_1  0x040026	/* XSP LRCK Period 1 */
#define CS43130_XSP_LRCK_PERIOD_2  0x040027	/* XSP LRCK Period 2 */
#define CS43130_XSP_CLOCK_CONF	0x040028	/* XSP Clock Config */
#define CS43130_XSP_FRAME_CONF	0x040029	/* XSP Frame Config */
#define CS43130_ASP_CH_1_LOC	0x050000	/* ASP Chan 1 Location */
#define CS43130_ASP_CH_2_LOC	0x050001	/* ASP Chan 2 Location */
#define CS43130_ASP_CH_1_SZ_EN	0x05000A	/* ASP Chan 1 Size, Enable */
#define CS43130_ASP_CH_2_SZ_EN	0x05000B	/* ASP Chan 2 Size, Enable */
#define CS43130_XSP_CH_1_LOC	0x060000	/* XSP Chan 1 Location */
#define CS43130_XSP_CH_2_LOC	0x060001	/* XSP Chan 2 Location */
#define CS43130_XSP_CH_1_SZ_EN	0x06000A	/* XSP Chan 1 Size, Enable */
#define CS43130_XSP_CH_2_SZ_EN	0x06000B	/* XSP Chan 2 Size, Enable */
#define CS43130_DSD_VOL_B	0x070000        /* DSD Volume B */
#define CS43130_DSD_VOL_A	0x070001        /* DSD Volume A */
#define CS43130_DSD_PATH_CTL_1	0x070002	/* DSD Proc Path Sig Ctl 1 */
#define CS43130_DSD_INT_CFG	0x070003	/* DSD Interface Config */
#define CS43130_DSD_PATH_CTL_2	0x070004	/* DSD Proc Path Sig Ctl 2 */
#define CS43130_DSD_PCM_MIX_CTL	0x070005	/* DSD and PCM Mixing Ctl */
#define CS43130_DSD_PATH_CTL_3	0x070006	/* DSD Proc Path Sig Ctl 3 */
#define CS43130_HP_OUT_CTL_1	0x080000	/* HP Output Ctl 1 */
#define CS43130_DXD16		0x080024	/* DXD16 */
#define CS43130_DXD13		0x080032	/* DXD13 */
#define CS43130_PCM_FILT_OPT	0x090000	/* PCM Filter Option */
#define CS43130_PCM_VOL_B	0x090001        /* PCM Volume B */
#define CS43130_PCM_VOL_A	0x090002        /* PCM Volume A */
#define CS43130_PCM_PATH_CTL_1	0x090003	/* PCM Path Signal Ctl 1 */
#define CS43130_PCM_PATH_CTL_2	0x090004	/* PCM Path Signal Ctl 2 */
#define CS43130_DXD6		0x090097	/* DXD6 */
#define CS43130_CLASS_H_CTL	0x0B0000	/* Class H Ctl */
#define CS43130_DXD15		0x0B0005	/* DXD15 */
#define CS43130_DXD14		0x0B0006	/* DXD14 */
#define CS43130_DXD3		0x0C0002	/* DXD3 */
#define CS43130_DXD10		0x0C0003	/* DXD10 */
#define CS43130_DXD11		0x0C0005	/* DXD11 */
#define CS43130_DXD9		0x0C0006	/* DXD9 */
#define CS43130_DXD4		0x0C0009	/* DXD4 */
#define CS43130_DXD5		0x0C000E	/* DXD5 */
#define CS43130_HP_DETECT	0x0D0000        /* HP Detect */
#define CS43130_HP_STATUS	0x0D0001        /* HP Status [RO] */
#define CS43130_HP_LOAD_1	0x0E0000        /* HP Load 1 */
#define CS43130_HP_MEAS_LOAD_1	0x0E0003	/* HP Load Measurement 1 */
#define CS43130_HP_MEAS_LOAD_2	0x0E0004	/* HP Load Measurement 2 */
#define CS43130_HP_DC_STAT_1	0x0E000D	/* HP DC Load Status 0 [RO] */
#define CS43130_HP_DC_STAT_2	0x0E000E	/* HP DC Load Status 1 [RO] */
#define CS43130_HP_AC_STAT_1	0x0E0010	/* HP AC Load Status 0 [RO] */
#define CS43130_HP_AC_STAT_2	0x0E0011	/* HP AC Load Status 1 [RO] */
#define CS43130_HP_LOAD_STAT	0x0E001A	/* HP Load Status [RO] */
#define CS43130_INT_STATUS_1	0x0F0000	/* Interrupt Status 1 */
#define CS43130_INT_STATUS_2	0x0F0001	/* Interrupt Status 2 */
#define CS43130_INT_STATUS_3	0x0F0002	/* Interrupt Status 3 */
#define CS43130_INT_STATUS_4	0x0F0003	/* Interrupt Status 4 */
#define CS43130_INT_STATUS_5	0x0F0004	/* Interrupt Status 5 */
#define CS43130_INT_MASK_1	0x0F0010        /* Interrupt Mask 1 */
#define CS43130_INT_MASK_2	0x0F0011	/* Interrupt Mask 2 */
#define CS43130_INT_MASK_3	0x0F0012        /* Interrupt Mask 3 */
#define CS43130_INT_MASK_4	0x0F0013        /* Interrupt Mask 4 */
#define CS43130_INT_MASK_5	0x0F0014        /* Interrupt Mask 5 */

#define CS43130_MCLK_SRC_SEL_MASK	0x03
#define CS43130_MCLK_SRC_SEL_SHIFT	0
#define CS43130_MCLK_INT_MASK		0x04
#define CS43130_MCLK_INT_SHIFT		2
#define CS43130_CH_BITSIZE_MASK		0x03
#define CS43130_CH_EN_MASK		0x04
#define CS43130_CH_EN_SHIFT		2
#define CS43130_ASP_BITSIZE_MASK	0x03
#define CS43130_XSP_BITSIZE_MASK	0x0C
#define CS43130_XSP_BITSIZE_SHIFT	2
#define CS43130_SP_BITSIZE_ASP_SHIFT	0
#define CS43130_HP_DETECT_CTRL_SHIFT	6
#define CS43130_HP_DETECT_CTRL_MASK     (0x03 << CS43130_HP_DETECT_CTRL_SHIFT)
#define CS43130_HP_DETECT_INV_SHIFT	5
#define CS43130_HP_DETECT_INV_MASK      (1 << CS43130_HP_DETECT_INV_SHIFT)

/* CS43130_INT_MASK_1 */
#define CS43130_HP_PLUG_INT_SHIFT       6
#define CS43130_HP_PLUG_INT             (1 << CS43130_HP_PLUG_INT_SHIFT)
#define CS43130_HP_UNPLUG_INT_SHIFT     5
#define CS43130_HP_UNPLUG_INT           (1 << CS43130_HP_UNPLUG_INT_SHIFT)
#define CS43130_XTAL_RDY_INT_SHIFT      4
#define CS43130_XTAL_RDY_INT_MASK	0x10
#define CS43130_XTAL_RDY_INT            (1 << CS43130_XTAL_RDY_INT_SHIFT)
#define CS43130_XTAL_ERR_INT_SHIFT      3
#define CS43130_XTAL_ERR_INT            (1 << CS43130_XTAL_ERR_INT_SHIFT)
#define CS43130_PLL_RDY_INT_MASK	0x04
#define CS43130_PLL_RDY_INT_SHIFT	2
#define CS43130_PLL_RDY_INT		(1 << CS43130_PLL_RDY_INT_SHIFT)

/* CS43130_INT_MASK_4 */
#define CS43130_INT_MASK_ALL		0xFF
#define CS43130_HPLOAD_NO_DC_INT_SHIFT	7
#define CS43130_HPLOAD_NO_DC_INT	(1 << CS43130_HPLOAD_NO_DC_INT_SHIFT)
#define CS43130_HPLOAD_UNPLUG_INT_SHIFT	6
#define CS43130_HPLOAD_UNPLUG_INT	(1 << CS43130_HPLOAD_UNPLUG_INT_SHIFT)
#define CS43130_HPLOAD_OOR_INT_SHIFT	4
#define CS43130_HPLOAD_OOR_INT		(1 << CS43130_HPLOAD_OOR_INT_SHIFT)
#define CS43130_HPLOAD_AC_INT_SHIFT	3
#define CS43130_HPLOAD_AC_INT		(1 << CS43130_HPLOAD_AC_INT_SHIFT)
#define CS43130_HPLOAD_DC_INT_SHIFT	2
#define CS43130_HPLOAD_DC_INT		(1 << CS43130_HPLOAD_DC_INT_SHIFT)
#define CS43130_HPLOAD_OFF_INT_SHIFT	1
#define CS43130_HPLOAD_OFF_INT		(1 << CS43130_HPLOAD_OFF_INT_SHIFT)
#define CS43130_HPLOAD_ON_INT		1

/* CS43130_HP_LOAD_1 */
#define CS43130_HPLOAD_EN_SHIFT		7
#define CS43130_HPLOAD_EN		(1 << CS43130_HPLOAD_EN_SHIFT)
#define CS43130_HPLOAD_CHN_SEL_SHIFT	4
#define CS43130_HPLOAD_CHN_SEL		(1 << CS43130_HPLOAD_CHN_SEL_SHIFT)
#define CS43130_HPLOAD_AC_START_SHIFT	1
#define CS43130_HPLOAD_AC_START		(1 << CS43130_HPLOAD_AC_START_SHIFT)
#define CS43130_HPLOAD_DC_START		1

/* Reg CS43130_SP_BITSIZE */
#define CS43130_SP_BIT_SIZE_8	0x03
#define CS43130_SP_BIT_SIZE_16	0x02
#define CS43130_SP_BIT_SIZE_24	0x01
#define CS43130_SP_BIT_SIZE_32	0x00

/* Reg CS43130_SP_CH_SZ_EN */
#define CS43130_CH_BIT_SIZE_8	0x00
#define CS43130_CH_BIT_SIZE_16	0x01
#define CS43130_CH_BIT_SIZE_24	0x02
#define CS43130_CH_BIT_SIZE_32	0x03

/* PLL */
#define CS43130_PLL_START_MASK	0x01
#define CS43130_PLL_MODE_MASK	0x02
#define CS43130_PLL_MODE_SHIFT	1

#define CS43130_PLL_REF_PREDIV_MASK	0x3

#define CS43130_SP_STP_MASK	0x10
#define CS43130_SP_STP_SHIFT	4
#define CS43130_SP_5050_MASK	0x08
#define CS43130_SP_5050_SHIFT	3
#define CS43130_SP_FSD_MASK	0x07

#define CS43130_SP_MODE_MASK	0x10
#define CS43130_SP_MODE_SHIFT	4
#define CS43130_SP_SCPOL_OUT_MASK	0x08
#define CS43130_SP_SCPOL_OUT_SHIFT	3
#define CS43130_SP_SCPOL_IN_MASK	0x04
#define CS43130_SP_SCPOL_IN_SHIFT	2
#define CS43130_SP_LCPOL_OUT_MASK	0x02
#define CS43130_SP_LCPOL_OUT_SHIFT	1
#define CS43130_SP_LCPOL_IN_MASK	0x01
#define CS43130_SP_LCPOL_IN_SHIFT	0

/* Reg CS43130_PWDN_CTL */
#define CS43130_PDN_XSP_MASK	0x80
#define CS43130_PDN_XSP_SHIFT	7
#define CS43130_PDN_ASP_MASK	0x40
#define CS43130_PDN_ASP_SHIFT	6
#define CS43130_PDN_DSPIF_MASK	0x20
#define CS43130_PDN_DSDIF_SHIFT	5
#define CS43130_PDN_HP_MASK	0x10
#define CS43130_PDN_HP_SHIFT	4
#define CS43130_PDN_XTAL_MASK	0x08
#define CS43130_PDN_XTAL_SHIFT	3
#define CS43130_PDN_PLL_MASK	0x04
#define CS43130_PDN_PLL_SHIFT	2
#define CS43130_PDN_CLKOUT_MASK	0x02
#define CS43130_PDN_CLKOUT_SHIFT	1

/* Reg CS43130_HP_OUT_CTL_1 */
#define CS43130_HP_IN_EN_SHIFT		3
#define CS43130_HP_IN_EN_MASK		0x08

/* Reg CS43130_PAD_INT_CFG */
#define CS43130_ASP_3ST_MASK		0x01
#define CS43130_XSP_3ST_MASK		0x02

/* Reg CS43130_PLL_SET_2 */
#define CS43130_PLL_DIV_DATA_MASK	0x000000FF
#define CS43130_PLL_DIV_FRAC_0_DATA_SHIFT	0

/* Reg CS43130_PLL_SET_3 */
#define CS43130_PLL_DIV_FRAC_1_DATA_SHIFT	8

/* Reg CS43130_PLL_SET_4 */
#define CS43130_PLL_DIV_FRAC_2_DATA_SHIFT	16

/* Reg CS43130_SP_DEN_1 */
#define CS43130_SP_M_LSB_DATA_MASK	0x00FF
#define CS43130_SP_M_LSB_DATA_SHIFT	0

/* Reg CS43130_SP_DEN_2 */
#define CS43130_SP_M_MSB_DATA_MASK	0xFF00
#define CS43130_SP_M_MSB_DATA_SHIFT	8

/* Reg CS43130_SP_NUM_1 */
#define CS43130_SP_N_LSB_DATA_MASK	0x00FF
#define CS43130_SP_N_LSB_DATA_SHIFT	0

/* Reg CS43130_SP_NUM_2 */
#define CS43130_SP_N_MSB_DATA_MASK	0xFF00
#define CS43130_SP_N_MSB_DATA_SHIFT	8

/* Reg CS43130_SP_LRCK_HI_TIME_1 */
#define	CS43130_SP_LCHI_DATA_MASK	0x00FF
#define CS43130_SP_LCHI_LSB_DATA_SHIFT	0

/* Reg CS43130_SP_LRCK_HI_TIME_2 */
#define CS43130_SP_LCHI_MSB_DATA_SHIFT	8

/* Reg CS43130_SP_LRCK_PERIOD_1 */
#define CS43130_SP_LCPR_DATA_MASK	0x00FF
#define CS43130_SP_LCPR_LSB_DATA_SHIFT	0

/* Reg CS43130_SP_LRCK_PERIOD_2 */
#define CS43130_SP_LCPR_MSB_DATA_SHIFT	8

#define CS43130_PCM_FORMATS (SNDRV_PCM_FMTBIT_S8  | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

#define CS43130_DOP_FORMATS (SNDRV_PCM_FMTBIT_DSD_U16_LE | \
			     SNDRV_PCM_FMTBIT_DSD_U16_BE | \
			     SNDRV_PCM_FMTBIT_S24_LE)

/* Reg CS43130_CRYSTAL_SET */
#define CS43130_XTAL_IBIAS_MASK		0x07

/* Reg CS43130_PATH_CTL_1 */
#define CS43130_MUTE_MASK		0x03
#define CS43130_MUTE_EN			0x03

/* Reg CS43130_DSD_INT_CFG */
#define CS43130_DSD_MASTER		0x04

/* Reg CS43130_DSD_PATH_CTL_2 */
#define CS43130_DSD_SRC_MASK		0x60
#define CS43130_DSD_SRC_SHIFT		5
#define CS43130_DSD_EN_SHIFT		4
#define CS43130_DSD_SPEED_MASK		0x04
#define CS43130_DSD_SPEED_SHIFT		2

/* Reg CS43130_DSD_PCM_MIX_CTL	*/
#define CS43130_MIX_PCM_PREP_SHIFT	1
#define CS43130_MIX_PCM_PREP_MASK	0x02

#define CS43130_MIX_PCM_DSD_SHIFT	0
#define CS43130_MIX_PCM_DSD_MASK	0x01

/* Reg CS43130_HP_MEAS_LOAD */
#define CS43130_HP_MEAS_LOAD_MASK	0x000000FF
#define CS43130_HP_MEAS_LOAD_1_SHIFT	0
#define CS43130_HP_MEAS_LOAD_2_SHIFT	8

#define CS43130_MCLK_22M		22579200
#define CS43130_MCLK_24M		24576000

#define CS43130_LINEOUT_LOAD		5000
#define CS43130_JACK_LINEOUT		(SND_JACK_MECHANICAL | SND_JACK_LINEOUT)
#define CS43130_JACK_HEADPHONE		(SND_JACK_MECHANICAL | \
					 SND_JACK_HEADPHONE)
#define CS43130_JACK_MASK		(SND_JACK_MECHANICAL | \
					 SND_JACK_LINEOUT | \
					 SND_JACK_HEADPHONE)

enum cs43130_dsd_src {
	CS43130_DSD_SRC_DSD = 0,
	CS43130_DSD_SRC_ASP = 2,
	CS43130_DSD_SRC_XSP = 3,
};

enum cs43130_asp_rate {
	CS43130_ASP_SPRATE_32K = 0,
	CS43130_ASP_SPRATE_44_1K,
	CS43130_ASP_SPRATE_48K,
	CS43130_ASP_SPRATE_88_2K,
	CS43130_ASP_SPRATE_96K,
	CS43130_ASP_SPRATE_176_4K,
	CS43130_ASP_SPRATE_192K,
	CS43130_ASP_SPRATE_352_8K,
	CS43130_ASP_SPRATE_384K,
};

enum cs43130_mclk_src_sel {
	CS43130_MCLK_SRC_EXT = 0,
	CS43130_MCLK_SRC_PLL,
	CS43130_MCLK_SRC_RCO
};

enum cs43130_mclk_int_freq {
	CS43130_MCLK_24P5 = 0,
	CS43130_MCLK_22P5,
};

enum cs43130_xtal_ibias {
	CS43130_XTAL_UNUSED = -1,
	CS43130_XTAL_IBIAS_15UA = 2,
	CS43130_XTAL_IBIAS_12_5UA = 4,
	CS43130_XTAL_IBIAS_7_5UA = 6,
};

enum cs43130_dai_id {
	CS43130_ASP_PCM_DAI = 0,
	CS43130_ASP_DOP_DAI,
	CS43130_XSP_DOP_DAI,
	CS43130_XSP_DSD_DAI,
	CS43130_DAI_ID_MAX,
};

struct cs43130_clk_gen {
	unsigned int	mclk_int;
	int		fs;
	u16		den;
	u16		num;
};

/* frm_size = 16 */
static const struct cs43130_clk_gen cs43130_16_clk_gen[] = {
	{22579200,	32000,		441,		10,},
	{22579200,	44100,		32,		1,},
	{22579200,	48000,		147,		5,},
	{22579200,	88200,		16,		1,},
	{22579200,	96000,		147,		10,},
	{22579200,	176400,		8,		1,},
	{22579200,	192000,		147,		20,},
	{22579200,	352800,		4,		1,},
	{22579200,	384000,		147,		40,},
	{24576000,	32000,		48,		1,},
	{24576000,	44100,		5120,		147,},
	{24576000,	48000,		32,		1,},
	{24576000,	88200,		2560,		147,},
	{24576000,	96000,		16,		1,},
	{24576000,	176400,		1280,		147,},
	{24576000,	192000,		8,		1,},
	{24576000,	352800,		640,		147,},
	{24576000,	384000,		4,		1,},
};

/* frm_size = 32 */
static const struct cs43130_clk_gen cs43130_32_clk_gen[] = {
	{22579200,	32000,		441,		20,},
	{22579200,	44100,		16,		1,},
	{22579200,	48000,		147,		10,},
	{22579200,	88200,		8,		1,},
	{22579200,	96000,		147,		20,},
	{22579200,	176400,		4,		1,},
	{22579200,	192000,		147,		40,},
	{22579200,	352800,		2,		1,},
	{22579200,	384000,		147,		80,},
	{24576000,	32000,		24,		1,},
	{24576000,	44100,		2560,		147,},
	{24576000,	48000,		16,		1,},
	{24576000,	88200,		1280,		147,},
	{24576000,	96000,		8,		1,},
	{24576000,	176400,		640,		147,},
	{24576000,	192000,		4,		1,},
	{24576000,	352800,		320,		147,},
	{24576000,	384000,		2,		1,},
};

/* frm_size = 48 */
static const struct cs43130_clk_gen cs43130_48_clk_gen[] = {
	{22579200,	32000,		147,		100,},
	{22579200,	44100,		32,		3,},
	{22579200,	48000,		49,		5,},
	{22579200,	88200,		16,		3,},
	{22579200,	96000,		49,		10,},
	{22579200,	176400,		8,		3,},
	{22579200,	192000,		49,		20,},
	{22579200,	352800,		4,		3,},
	{22579200,	384000,		49,		40,},
	{24576000,	32000,		16,		1,},
	{24576000,	44100,		5120,		441,},
	{24576000,	48000,		32,		3,},
	{24576000,	88200,		2560,		441,},
	{24576000,	96000,		16,		3,},
	{24576000,	176400,		1280,		441,},
	{24576000,	192000,		8,		3,},
	{24576000,	352800,		640,		441,},
	{24576000,	384000,		4,		3,},
};

/* frm_size = 64 */
static const struct cs43130_clk_gen cs43130_64_clk_gen[] = {
	{22579200,	32000,		441,		40,},
	{22579200,	44100,		8,		1,},
	{22579200,	48000,		147,		20,},
	{22579200,	88200,		4,		1,},
	{22579200,	96000,		147,		40,},
	{22579200,	176400,		2,		1,},
	{22579200,	192000,		147,		80,},
	{22579200,	352800,		1,		1,},
	{24576000,	32000,		12,		1,},
	{24576000,	44100,		1280,		147,},
	{24576000,	48000,		8,		1,},
	{24576000,	88200,		640,		147,},
	{24576000,	96000,		4,		1,},
	{24576000,	176400,		320,		147,},
	{24576000,	192000,		2,		1,},
	{24576000,	352800,		160,		147,},
	{24576000,	384000,		1,		1,},
};

struct cs43130_bitwidth_map {
	unsigned int bitwidth;
	u8 sp_bit;
	u8 ch_bit;
};

struct cs43130_rate_map {
	int fs;
	int val;
};

#define HP_LEFT			0
#define HP_RIGHT		1
#define CS43130_AC_FREQ		10
#define CS43130_DC_THRESHOLD	2

#define CS43130_NUM_SUPPLIES	5
static const char *const cs43130_supply_names[CS43130_NUM_SUPPLIES] = {
	"VA",
	"VP",
	"VCP",
	"VD",
	"VL",
};

#define CS43130_NUM_INT		5       /* number of interrupt status reg */

struct cs43130_dai {
	unsigned int			sclk;
	unsigned int			dai_format;
	unsigned int			dai_mode;
};

struct	cs43130_private {
	struct snd_soc_component	*component;
	struct regmap			*regmap;
	struct regulator_bulk_data	supplies[CS43130_NUM_SUPPLIES];
	struct gpio_desc		*reset_gpio;
	unsigned int			dev_id; /* codec device ID */
	int				xtal_ibias;

	/* shared by both DAIs */
	struct mutex			clk_mutex;
	int				clk_req;
	bool				pll_bypass;
	struct completion		xtal_rdy;
	struct completion		pll_rdy;
	unsigned int			mclk;
	unsigned int			mclk_int;
	int				mclk_int_src;

	/* DAI specific */
	struct cs43130_dai		dais[CS43130_DAI_ID_MAX];

	/* HP load specific */
	bool				dc_meas;
	bool				ac_meas;
	bool				hpload_done;
	struct completion		hpload_evt;
	unsigned int			hpload_stat;
	u16				hpload_dc[2];
	u16				dc_threshold[CS43130_DC_THRESHOLD];
	u16				ac_freq[CS43130_AC_FREQ];
	u16				hpload_ac[CS43130_AC_FREQ][2];
	struct workqueue_struct		*wq;
	struct work_struct		work;
	struct snd_soc_jack		jack;
};

#endif	/* __CS43130_H__ */
