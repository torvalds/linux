/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * PDM Microphone Interface for the NXP i.MX SoC
 * Copyright 2018 NXP
 */

#ifndef _FSL_MICFIL_H
#define _FSL_MICFIL_H

/* MICFIL Register Map */
#define REG_MICFIL_CTRL1		0x00
#define REG_MICFIL_CTRL2		0x04
#define REG_MICFIL_STAT			0x08
#define REG_MICFIL_FIFO_CTRL		0x10
#define REG_MICFIL_FIFO_STAT		0x14
#define REG_MICFIL_DATACH0		0x24
#define REG_MICFIL_DATACH1		0x28
#define REG_MICFIL_DATACH2		0x2C
#define REG_MICFIL_DATACH3		0x30
#define REG_MICFIL_DATACH4		0x34
#define REG_MICFIL_DATACH5		0x38
#define REG_MICFIL_DATACH6		0x3C
#define REG_MICFIL_DATACH7		0x40
#define REG_MICFIL_DC_CTRL		0x64
#define REG_MICFIL_OUT_CTRL		0x74
#define REG_MICFIL_OUT_STAT		0x7C
#define REG_MICFIL_FSYNC_CTRL		0x80
#define REG_MICFIL_VERID		0x84
#define REG_MICFIL_PARAM		0x88
#define REG_MICFIL_VAD0_CTRL1		0x90
#define REG_MICFIL_VAD0_CTRL2		0x94
#define REG_MICFIL_VAD0_STAT		0x98
#define REG_MICFIL_VAD0_SCONFIG		0x9C
#define REG_MICFIL_VAD0_NCONFIG		0xA0
#define REG_MICFIL_VAD0_NDATA		0xA4
#define REG_MICFIL_VAD0_ZCD		0xA8

/* MICFIL Control Register 1 -- REG_MICFILL_CTRL1 0x00 */
#define MICFIL_CTRL1_MDIS		BIT(31)
#define MICFIL_CTRL1_DOZEN		BIT(30)
#define MICFIL_CTRL1_PDMIEN		BIT(29)
#define MICFIL_CTRL1_DBG		BIT(28)
#define MICFIL_CTRL1_SRES		BIT(27)
#define MICFIL_CTRL1_DBGE		BIT(26)
#define MICFIL_CTRL1_DECFILS		BIT(20)
#define MICFIL_CTRL1_FSYNCEN		BIT(16)

#define MICFIL_CTRL1_DISEL_DISABLE	0
#define MICFIL_CTRL1_DISEL_DMA		1
#define MICFIL_CTRL1_DISEL_IRQ		2
#define MICFIL_CTRL1_DISEL		GENMASK(25, 24)
#define MICFIL_CTRL1_ERREN		BIT(23)
#define MICFIL_CTRL1_CHEN(ch)		BIT(ch)

/* MICFIL Control Register 2 -- REG_MICFILL_CTRL2 0x04 */
#define MICFIL_CTRL2_QSEL_SHIFT		25
#define MICFIL_CTRL2_QSEL		GENMASK(27, 25)
#define MICFIL_QSEL_MEDIUM_QUALITY	0
#define MICFIL_QSEL_HIGH_QUALITY	1
#define MICFIL_QSEL_LOW_QUALITY		7
#define MICFIL_QSEL_VLOW0_QUALITY	6
#define MICFIL_QSEL_VLOW1_QUALITY	5
#define MICFIL_QSEL_VLOW2_QUALITY	4

#define MICFIL_CTRL2_CICOSR		GENMASK(19, 16)
#define MICFIL_CTRL2_CLKDIV		GENMASK(7, 0)

/* MICFIL Status Register -- REG_MICFIL_STAT 0x08 */
#define MICFIL_STAT_BSY_FIL		BIT(31)
#define MICFIL_STAT_FIR_RDY		BIT(30)
#define MICFIL_STAT_LOWFREQF		BIT(29)
#define MICFIL_STAT_CHXF(ch)		BIT(ch)

/* MICFIL FIFO Control Register -- REG_MICFIL_FIFO_CTRL 0x10 */
#define MICFIL_FIFO_CTRL_FIFOWMK	GENMASK(2, 0)

/* MICFIL FIFO Status Register -- REG_MICFIL_FIFO_STAT 0x14 */
#define MICFIL_FIFO_STAT_FIFOX_OVER(ch)	BIT(ch)
#define MICFIL_FIFO_STAT_FIFOX_UNDER(ch)	BIT((ch) + 8)

/* MICFIL DC Remover Control Register -- REG_MICFIL_DC_CTRL */
#define MICFIL_DC_CTRL_CONFIG          GENMASK(15, 0)
#define MICFIL_DC_CHX_SHIFT(ch)        ((ch) << 1)
#define MICFIL_DC_CHX(ch)              GENMASK((((ch) << 1) + 1), ((ch) << 1))
#define MICFIL_DC_CUTOFF_21HZ          0
#define MICFIL_DC_CUTOFF_83HZ          1
#define MICFIL_DC_CUTOFF_152Hz         2
#define MICFIL_DC_BYPASS               3

/* MICFIL VERID Register -- REG_MICFIL_VERID */
#define MICFIL_VERID_MAJOR_SHIFT        24
#define MICFIL_VERID_MAJOR_MASK         GENMASK(31, 24)
#define MICFIL_VERID_MINOR_SHIFT        16
#define MICFIL_VERID_MINOR_MASK         GENMASK(23, 16)
#define MICFIL_VERID_FEATURE_SHIFT      0
#define MICFIL_VERID_FEATURE_MASK       GENMASK(15, 0)

/* MICFIL PARAM Register -- REG_MICFIL_PARAM */
#define MICFIL_PARAM_NUM_HWVAD_SHIFT    24
#define MICFIL_PARAM_NUM_HWVAD_MASK     GENMASK(27, 24)
#define MICFIL_PARAM_HWVAD_ZCD          BIT(19)
#define MICFIL_PARAM_HWVAD_ENERGY_MODE  BIT(17)
#define MICFIL_PARAM_HWVAD              BIT(16)
#define MICFIL_PARAM_DC_OUT_BYPASS      BIT(11)
#define MICFIL_PARAM_DC_IN_BYPASS       BIT(10)
#define MICFIL_PARAM_LOW_POWER          BIT(9)
#define MICFIL_PARAM_FIL_OUT_WIDTH      BIT(8)
#define MICFIL_PARAM_FIFO_PTRWID_SHIFT  4
#define MICFIL_PARAM_FIFO_PTRWID_MASK   GENMASK(7, 4)
#define MICFIL_PARAM_NPAIR_SHIFT        0
#define MICFIL_PARAM_NPAIR_MASK         GENMASK(3, 0)

/* MICFIL HWVAD0 Control 1 Register -- REG_MICFIL_VAD0_CTRL1*/
#define MICFIL_VAD0_CTRL1_CHSEL		GENMASK(26, 24)
#define MICFIL_VAD0_CTRL1_CICOSR	GENMASK(19, 16)
#define MICFIL_VAD0_CTRL1_INITT		GENMASK(12, 8)
#define MICFIL_VAD0_CTRL1_ST10		BIT(4)
#define MICFIL_VAD0_CTRL1_ERIE		BIT(3)
#define MICFIL_VAD0_CTRL1_IE		BIT(2)
#define MICFIL_VAD0_CTRL1_RST		BIT(1)
#define MICFIL_VAD0_CTRL1_EN		BIT(0)

/* MICFIL HWVAD0 Control 2 Register -- REG_MICFIL_VAD0_CTRL2*/
#define MICFIL_VAD0_CTRL2_FRENDIS	BIT(31)
#define MICFIL_VAD0_CTRL2_PREFEN	BIT(30)
#define MICFIL_VAD0_CTRL2_FOUTDIS	BIT(28)
#define MICFIL_VAD0_CTRL2_FRAMET	GENMASK(21, 16)
#define MICFIL_VAD0_CTRL2_INPGAIN	GENMASK(11, 8)
#define MICFIL_VAD0_CTRL2_HPF		GENMASK(1, 0)

/* MICFIL HWVAD0 Signal CONFIG Register -- REG_MICFIL_VAD0_SCONFIG */
#define MICFIL_VAD0_SCONFIG_SFILEN		BIT(31)
#define MICFIL_VAD0_SCONFIG_SMAXEN		BIT(30)
#define MICFIL_VAD0_SCONFIG_SGAIN		GENMASK(3, 0)

/* MICFIL HWVAD0 Noise CONFIG Register -- REG_MICFIL_VAD0_NCONFIG */
#define MICFIL_VAD0_NCONFIG_NFILAUT		BIT(31)
#define MICFIL_VAD0_NCONFIG_NMINEN		BIT(30)
#define MICFIL_VAD0_NCONFIG_NDECEN		BIT(29)
#define MICFIL_VAD0_NCONFIG_NOREN		BIT(28)
#define MICFIL_VAD0_NCONFIG_NFILADJ		GENMASK(12, 8)
#define MICFIL_VAD0_NCONFIG_NGAIN		GENMASK(3, 0)

/* MICFIL HWVAD0 Zero-Crossing Detector - REG_MICFIL_VAD0_ZCD */
#define MICFIL_VAD0_ZCD_ZCDTH		GENMASK(25, 16)
#define MICFIL_VAD0_ZCD_ZCDADJ		GENMASK(11, 8)
#define MICFIL_VAD0_ZCD_ZCDAND		BIT(4)
#define MICFIL_VAD0_ZCD_ZCDAUT		BIT(2)
#define MICFIL_VAD0_ZCD_ZCDEN		BIT(0)

/* MICFIL HWVAD0 Status Register - REG_MICFIL_VAD0_STAT */
#define MICFIL_VAD0_STAT_INITF		BIT(31)
#define MICFIL_VAD0_STAT_INSATF		BIT(16)
#define MICFIL_VAD0_STAT_EF		BIT(15)
#define MICFIL_VAD0_STAT_IF		BIT(0)

/* MICFIL Output Control Register */
#define MICFIL_OUTGAIN_CHX_SHIFT(v)	(4 * (v))

/* Constants */
#define MICFIL_OUTPUT_CHANNELS		8
#define MICFIL_FIFO_NUM			8

#define FIFO_PTRWID			3
#define FIFO_LEN			BIT(FIFO_PTRWID)

#define MICFIL_IRQ_LINES		4
#define MICFIL_MAX_RETRY		25
#define MICFIL_SLEEP_MIN		90000 /* in us */
#define MICFIL_SLEEP_MAX		100000 /* in us */
#define MICFIL_DMA_MAXBURST_RX		6

/* HWVAD Constants */
#define MICFIL_HWVAD_ENVELOPE_MODE	0
#define MICFIL_HWVAD_ENERGY_MODE	1

/**
 * struct fsl_micfil_verid - version id data
 * @version: version number
 * @feature: feature specification number
 */
struct fsl_micfil_verid {
	u32 version;
	u32 feature;
};

/**
 * struct fsl_micfil_param - parameter data
 * @hwvad_num: the number of HWVADs
 * @hwvad_zcd: HWVAD zero-cross detector is active
 * @hwvad_energy_mode: HWVAD energy mode is active
 * @hwvad: HWVAD is active
 * @dc_out_bypass: points out if the output DC remover is disabled
 * @dc_in_bypass: points out if the input DC remover is disabled
 * @low_power: low power decimation filter
 * @fil_out_width: filter output width
 * @fifo_ptrwid: FIFO pointer width
 * @npair: number of microphone pairs
 */
struct fsl_micfil_param {
	u32 hwvad_num;
	bool hwvad_zcd;
	bool hwvad_energy_mode;
	bool hwvad;
	bool dc_out_bypass;
	bool dc_in_bypass;
	bool low_power;
	bool fil_out_width;
	u32 fifo_ptrwid;
	u32 npair;
};

#endif /* _FSL_MICFIL_H */
