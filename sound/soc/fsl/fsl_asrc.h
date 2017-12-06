/*
 * fsl_asrc.h - Freescale ASRC ALSA SoC header file
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <nicoleotsuka@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef _FSL_ASRC_H
#define _FSL_ASRC_H

#define IN	0
#define OUT	1

#define ASRC_DMA_BUFFER_NUM		2
#define ASRC_INPUTFIFO_THRESHOLD	32
#define ASRC_OUTPUTFIFO_THRESHOLD	32
#define ASRC_FIFO_THRESHOLD_MIN		0
#define ASRC_FIFO_THRESHOLD_MAX		63
#define ASRC_DMA_BUFFER_SIZE		(1024 * 48 * 4)
#define ASRC_MAX_BUFFER_SIZE		(1024 * 48)
#define ASRC_OUTPUT_LAST_SAMPLE		8

#define IDEAL_RATIO_RATE		1000000

#define REG_ASRCTR			0x00
#define REG_ASRIER			0x04
#define REG_ASRCNCR			0x0C
#define REG_ASRCFG			0x10
#define REG_ASRCSR			0x14

#define REG_ASRCDR1			0x18
#define REG_ASRCDR2			0x1C
#define REG_ASRCDR(i)			((i < 2) ? REG_ASRCDR1 : REG_ASRCDR2)

#define REG_ASRSTR			0x20
#define REG_ASRRA			0x24
#define REG_ASRRB			0x28
#define REG_ASRRC			0x2C
#define REG_ASRPM1			0x40
#define REG_ASRPM2			0x44
#define REG_ASRPM3			0x48
#define REG_ASRPM4			0x4C
#define REG_ASRPM5			0x50
#define REG_ASRTFR1			0x54
#define REG_ASRCCR			0x5C

#define REG_ASRDIA			0x60
#define REG_ASRDOA			0x64
#define REG_ASRDIB			0x68
#define REG_ASRDOB			0x6C
#define REG_ASRDIC			0x70
#define REG_ASRDOC			0x74
#define REG_ASRDI(i)			(REG_ASRDIA + (i << 3))
#define REG_ASRDO(i)			(REG_ASRDOA + (i << 3))
#define REG_ASRDx(x, i)			((x) == IN ? REG_ASRDI(i) : REG_ASRDO(i))

#define REG_ASRIDRHA			0x80
#define REG_ASRIDRLA			0x84
#define REG_ASRIDRHB			0x88
#define REG_ASRIDRLB			0x8C
#define REG_ASRIDRHC			0x90
#define REG_ASRIDRLC			0x94
#define REG_ASRIDRH(i)			(REG_ASRIDRHA + (i << 3))
#define REG_ASRIDRL(i)			(REG_ASRIDRLA + (i << 3))

#define REG_ASR76K			0x98
#define REG_ASR56K			0x9C

#define REG_ASRMCRA			0xA0
#define REG_ASRFSTA			0xA4
#define REG_ASRMCRB			0xA8
#define REG_ASRFSTB			0xAC
#define REG_ASRMCRC			0xB0
#define REG_ASRFSTC			0xB4
#define REG_ASRMCR(i)			(REG_ASRMCRA + (i << 3))
#define REG_ASRFST(i)			(REG_ASRFSTA + (i << 3))

#define REG_ASRMCR1A			0xC0
#define REG_ASRMCR1B			0xC4
#define REG_ASRMCR1C			0xC8
#define REG_ASRMCR1(i)			(REG_ASRMCR1A + (i << 2))


/* REG0 0x00 REG_ASRCTR */
#define ASRCTR_ATSi_SHIFT(i)		(20 + i)
#define ASRCTR_ATSi_MASK(i)		(1 << ASRCTR_ATSi_SHIFT(i))
#define ASRCTR_ATS(i)			(1 << ASRCTR_ATSi_SHIFT(i))
#define ASRCTR_USRi_SHIFT(i)		(14 + (i << 1))
#define ASRCTR_USRi_MASK(i)		(1 << ASRCTR_USRi_SHIFT(i))
#define ASRCTR_USR(i)			(1 << ASRCTR_USRi_SHIFT(i))
#define ASRCTR_IDRi_SHIFT(i)		(13 + (i << 1))
#define ASRCTR_IDRi_MASK(i)		(1 << ASRCTR_IDRi_SHIFT(i))
#define ASRCTR_IDR(i)			(1 << ASRCTR_IDRi_SHIFT(i))
#define ASRCTR_SRST_SHIFT		4
#define ASRCTR_SRST_MASK		(1 << ASRCTR_SRST_SHIFT)
#define ASRCTR_SRST			(1 << ASRCTR_SRST_SHIFT)
#define ASRCTR_ASRCEi_SHIFT(i)		(1 + i)
#define ASRCTR_ASRCEi_MASK(i)		(1 << ASRCTR_ASRCEi_SHIFT(i))
#define ASRCTR_ASRCE(i)			(1 << ASRCTR_ASRCEi_SHIFT(i))
#define ASRCTR_ASRCEi_ALL_MASK		(0x7 << ASRCTR_ASRCEi_SHIFT(0))
#define ASRCTR_ASRCEN_SHIFT		0
#define ASRCTR_ASRCEN_MASK		(1 << ASRCTR_ASRCEN_SHIFT)
#define ASRCTR_ASRCEN			(1 << ASRCTR_ASRCEN_SHIFT)

/* REG1 0x04 REG_ASRIER */
#define ASRIER_AFPWE_SHIFT		7
#define ASRIER_AFPWE_MASK		(1 << ASRIER_AFPWE_SHIFT)
#define ASRIER_AFPWE			(1 << ASRIER_AFPWE_SHIFT)
#define ASRIER_AOLIE_SHIFT		6
#define ASRIER_AOLIE_MASK		(1 << ASRIER_AOLIE_SHIFT)
#define ASRIER_AOLIE			(1 << ASRIER_AOLIE_SHIFT)
#define ASRIER_ADOEi_SHIFT(i)		(3 + i)
#define ASRIER_ADOEi_MASK(i)		(1 << ASRIER_ADOEi_SHIFT(i))
#define ASRIER_ADOE(i)			(1 << ASRIER_ADOEi_SHIFT(i))
#define ASRIER_ADIEi_SHIFT(i)		(0 + i)
#define ASRIER_ADIEi_MASK(i)		(1 << ASRIER_ADIEi_SHIFT(i))
#define ASRIER_ADIE(i)			(1 << ASRIER_ADIEi_SHIFT(i))

/* REG2 0x0C REG_ASRCNCR */
#define ASRCNCR_ANCi_SHIFT(i, b)	(b * i)
#define ASRCNCR_ANCi_MASK(i, b)		(((1 << b) - 1) << ASRCNCR_ANCi_SHIFT(i, b))
#define ASRCNCR_ANCi(i, v, b)		((v << ASRCNCR_ANCi_SHIFT(i, b)) & ASRCNCR_ANCi_MASK(i, b))

/* REG3 0x10 REG_ASRCFG */
#define ASRCFG_INIRQi_SHIFT(i)		(21 + i)
#define ASRCFG_INIRQi_MASK(i)		(1 << ASRCFG_INIRQi_SHIFT(i))
#define ASRCFG_INIRQi			(1 << ASRCFG_INIRQi_SHIFT(i))
#define ASRCFG_NDPRi_SHIFT(i)		(18 + i)
#define ASRCFG_NDPRi_MASK(i)		(1 << ASRCFG_NDPRi_SHIFT(i))
#define ASRCFG_NDPRi_ALL_SHIFT		18
#define ASRCFG_NDPRi_ALL_MASK		(7 << ASRCFG_NDPRi_ALL_SHIFT)
#define ASRCFG_NDPRi			(1 << ASRCFG_NDPRi_SHIFT(i))
#define ASRCFG_POSTMODi_SHIFT(i)	(8 + (i << 2))
#define ASRCFG_POSTMODi_WIDTH		2
#define ASRCFG_POSTMODi_MASK(i)		(((1 << ASRCFG_POSTMODi_WIDTH) - 1) << ASRCFG_POSTMODi_SHIFT(i))
#define ASRCFG_POSTMODi_ALL_MASK	(ASRCFG_POSTMODi_MASK(0) | ASRCFG_POSTMODi_MASK(1) | ASRCFG_POSTMODi_MASK(2))
#define ASRCFG_POSTMOD(i, v)		((v) << ASRCFG_POSTMODi_SHIFT(i))
#define ASRCFG_POSTMODi_UP(i)		(0 << ASRCFG_POSTMODi_SHIFT(i))
#define ASRCFG_POSTMODi_DCON(i)		(1 << ASRCFG_POSTMODi_SHIFT(i))
#define ASRCFG_POSTMODi_DOWN(i)		(2 << ASRCFG_POSTMODi_SHIFT(i))
#define ASRCFG_PREMODi_SHIFT(i)		(6 + (i << 2))
#define ASRCFG_PREMODi_WIDTH		2
#define ASRCFG_PREMODi_MASK(i)		(((1 << ASRCFG_PREMODi_WIDTH) - 1) << ASRCFG_PREMODi_SHIFT(i))
#define ASRCFG_PREMODi_ALL_MASK		(ASRCFG_PREMODi_MASK(0) | ASRCFG_PREMODi_MASK(1) | ASRCFG_PREMODi_MASK(2))
#define ASRCFG_PREMOD(i, v)		((v) << ASRCFG_PREMODi_SHIFT(i))
#define ASRCFG_PREMODi_UP(i)		(0 << ASRCFG_PREMODi_SHIFT(i))
#define ASRCFG_PREMODi_DCON(i)		(1 << ASRCFG_PREMODi_SHIFT(i))
#define ASRCFG_PREMODi_DOWN(i)		(2 << ASRCFG_PREMODi_SHIFT(i))
#define ASRCFG_PREMODi_BYPASS(i)	(3 << ASRCFG_PREMODi_SHIFT(i))

/* REG4 0x14 REG_ASRCSR */
#define ASRCSR_AxCSi_WIDTH		4
#define ASRCSR_AxCSi_MASK		((1 << ASRCSR_AxCSi_WIDTH) - 1)
#define ASRCSR_AOCSi_SHIFT(i)		(12 + (i << 2))
#define ASRCSR_AOCSi_MASK(i)		(((1 << ASRCSR_AxCSi_WIDTH) - 1) << ASRCSR_AOCSi_SHIFT(i))
#define ASRCSR_AOCS(i, v)		((v) << ASRCSR_AOCSi_SHIFT(i))
#define ASRCSR_AICSi_SHIFT(i)		(i << 2)
#define ASRCSR_AICSi_MASK(i)		(((1 << ASRCSR_AxCSi_WIDTH) - 1) << ASRCSR_AICSi_SHIFT(i))
#define ASRCSR_AICS(i, v)		((v) << ASRCSR_AICSi_SHIFT(i))

/* REG5&6 0x18 & 0x1C REG_ASRCDR1 & ASRCDR2 */
#define ASRCDRi_AxCPi_WIDTH		3
#define ASRCDRi_AICPi_SHIFT(i)		(0 + (i % 2) * 6)
#define ASRCDRi_AICPi_MASK(i)		(((1 << ASRCDRi_AxCPi_WIDTH) - 1) << ASRCDRi_AICPi_SHIFT(i))
#define ASRCDRi_AICP(i, v)		((v) << ASRCDRi_AICPi_SHIFT(i))
#define ASRCDRi_AICDi_SHIFT(i)		(3 + (i % 2) * 6)
#define ASRCDRi_AICDi_MASK(i)		(((1 << ASRCDRi_AxCPi_WIDTH) - 1) << ASRCDRi_AICDi_SHIFT(i))
#define ASRCDRi_AICD(i, v)		((v) << ASRCDRi_AICDi_SHIFT(i))
#define ASRCDRi_AOCPi_SHIFT(i)		((i < 2) ? 12 + i * 6 : 6)
#define ASRCDRi_AOCPi_MASK(i)		(((1 << ASRCDRi_AxCPi_WIDTH) - 1) << ASRCDRi_AOCPi_SHIFT(i))
#define ASRCDRi_AOCP(i, v)		((v) << ASRCDRi_AOCPi_SHIFT(i))
#define ASRCDRi_AOCDi_SHIFT(i)		((i < 2) ? 15 + i * 6 : 9)
#define ASRCDRi_AOCDi_MASK(i)		(((1 << ASRCDRi_AxCPi_WIDTH) - 1) << ASRCDRi_AOCDi_SHIFT(i))
#define ASRCDRi_AOCD(i, v)		((v) << ASRCDRi_AOCDi_SHIFT(i))

/* REG7 0x20 REG_ASRSTR */
#define ASRSTR_DSLCNT_SHIFT		21
#define ASRSTR_DSLCNT_MASK		(1 << ASRSTR_DSLCNT_SHIFT)
#define ASRSTR_DSLCNT			(1 << ASRSTR_DSLCNT_SHIFT)
#define ASRSTR_ATQOL_SHIFT		20
#define ASRSTR_ATQOL_MASK		(1 << ASRSTR_ATQOL_SHIFT)
#define ASRSTR_ATQOL			(1 << ASRSTR_ATQOL_SHIFT)
#define ASRSTR_AOOLi_SHIFT(i)		(17 + i)
#define ASRSTR_AOOLi_MASK(i)		(1 << ASRSTR_AOOLi_SHIFT(i))
#define ASRSTR_AOOL(i)			(1 << ASRSTR_AOOLi_SHIFT(i))
#define ASRSTR_AIOLi_SHIFT(i)		(14 + i)
#define ASRSTR_AIOLi_MASK(i)		(1 << ASRSTR_AIOLi_SHIFT(i))
#define ASRSTR_AIOL(i)			(1 << ASRSTR_AIOLi_SHIFT(i))
#define ASRSTR_AODOi_SHIFT(i)		(11 + i)
#define ASRSTR_AODOi_MASK(i)		(1 << ASRSTR_AODOi_SHIFT(i))
#define ASRSTR_AODO(i)			(1 << ASRSTR_AODOi_SHIFT(i))
#define ASRSTR_AIDUi_SHIFT(i)		(8 + i)
#define ASRSTR_AIDUi_MASK(i)		(1 << ASRSTR_AIDUi_SHIFT(i))
#define ASRSTR_AIDU(i)			(1 << ASRSTR_AIDUi_SHIFT(i))
#define ASRSTR_FPWT_SHIFT		7
#define ASRSTR_FPWT_MASK		(1 << ASRSTR_FPWT_SHIFT)
#define ASRSTR_FPWT			(1 << ASRSTR_FPWT_SHIFT)
#define ASRSTR_AOLE_SHIFT		6
#define ASRSTR_AOLE_MASK		(1 << ASRSTR_AOLE_SHIFT)
#define ASRSTR_AOLE			(1 << ASRSTR_AOLE_SHIFT)
#define ASRSTR_AODEi_SHIFT(i)		(3 + i)
#define ASRSTR_AODFi_MASK(i)		(1 << ASRSTR_AODEi_SHIFT(i))
#define ASRSTR_AODF(i)			(1 << ASRSTR_AODEi_SHIFT(i))
#define ASRSTR_AIDEi_SHIFT(i)		(0 + i)
#define ASRSTR_AIDEi_MASK(i)		(1 << ASRSTR_AIDEi_SHIFT(i))
#define ASRSTR_AIDE(i)			(1 << ASRSTR_AIDEi_SHIFT(i))

/* REG10 0x54 REG_ASRTFR1 */
#define ASRTFR1_TF_BASE_WIDTH		7
#define ASRTFR1_TF_BASE_SHIFT		6
#define ASRTFR1_TF_BASE_MASK		(((1 << ASRTFR1_TF_BASE_WIDTH) - 1) << ASRTFR1_TF_BASE_SHIFT)
#define ASRTFR1_TF_BASE(i)		((i) << ASRTFR1_TF_BASE_SHIFT)

/*
 * REG22 0xA0 REG_ASRMCRA
 * REG24 0xA8 REG_ASRMCRB
 * REG26 0xB0 REG_ASRMCRC
 */
#define ASRMCRi_ZEROBUFi_SHIFT		23
#define ASRMCRi_ZEROBUFi_MASK		(1 << ASRMCRi_ZEROBUFi_SHIFT)
#define ASRMCRi_ZEROBUFi		(1 << ASRMCRi_ZEROBUFi_SHIFT)
#define ASRMCRi_EXTTHRSHi_SHIFT		22
#define ASRMCRi_EXTTHRSHi_MASK		(1 << ASRMCRi_EXTTHRSHi_SHIFT)
#define ASRMCRi_EXTTHRSHi		(1 << ASRMCRi_EXTTHRSHi_SHIFT)
#define ASRMCRi_BUFSTALLi_SHIFT		21
#define ASRMCRi_BUFSTALLi_MASK		(1 << ASRMCRi_BUFSTALLi_SHIFT)
#define ASRMCRi_BUFSTALLi		(1 << ASRMCRi_BUFSTALLi_SHIFT)
#define ASRMCRi_BYPASSPOLYi_SHIFT	20
#define ASRMCRi_BYPASSPOLYi_MASK	(1 << ASRMCRi_BYPASSPOLYi_SHIFT)
#define ASRMCRi_BYPASSPOLYi		(1 << ASRMCRi_BYPASSPOLYi_SHIFT)
#define ASRMCRi_OUTFIFO_THRESHOLD_WIDTH	6
#define ASRMCRi_OUTFIFO_THRESHOLD_SHIFT	12
#define ASRMCRi_OUTFIFO_THRESHOLD_MASK	(((1 << ASRMCRi_OUTFIFO_THRESHOLD_WIDTH) - 1) << ASRMCRi_OUTFIFO_THRESHOLD_SHIFT)
#define ASRMCRi_OUTFIFO_THRESHOLD(v)	(((v) << ASRMCRi_OUTFIFO_THRESHOLD_SHIFT) & ASRMCRi_OUTFIFO_THRESHOLD_MASK)
#define ASRMCRi_RSYNIFi_SHIFT		11
#define ASRMCRi_RSYNIFi_MASK		(1 << ASRMCRi_RSYNIFi_SHIFT)
#define ASRMCRi_RSYNIFi			(1 << ASRMCRi_RSYNIFi_SHIFT)
#define ASRMCRi_RSYNOFi_SHIFT		10
#define ASRMCRi_RSYNOFi_MASK		(1 << ASRMCRi_RSYNOFi_SHIFT)
#define ASRMCRi_RSYNOFi			(1 << ASRMCRi_RSYNOFi_SHIFT)
#define ASRMCRi_INFIFO_THRESHOLD_WIDTH	6
#define ASRMCRi_INFIFO_THRESHOLD_SHIFT	0
#define ASRMCRi_INFIFO_THRESHOLD_MASK	(((1 << ASRMCRi_INFIFO_THRESHOLD_WIDTH) - 1) << ASRMCRi_INFIFO_THRESHOLD_SHIFT)
#define ASRMCRi_INFIFO_THRESHOLD(v)	(((v) << ASRMCRi_INFIFO_THRESHOLD_SHIFT) & ASRMCRi_INFIFO_THRESHOLD_MASK)

/*
 * REG23 0xA4 REG_ASRFSTA
 * REG25 0xAC REG_ASRFSTB
 * REG27 0xB4 REG_ASRFSTC
 */
#define ASRFSTi_OAFi_SHIFT		23
#define ASRFSTi_OAFi_MASK		(1 << ASRFSTi_OAFi_SHIFT)
#define ASRFSTi_OAFi			(1 << ASRFSTi_OAFi_SHIFT)
#define ASRFSTi_OUTPUT_FIFO_WIDTH	7
#define ASRFSTi_OUTPUT_FIFO_SHIFT	12
#define ASRFSTi_OUTPUT_FIFO_MASK	(((1 << ASRFSTi_OUTPUT_FIFO_WIDTH) - 1) << ASRFSTi_OUTPUT_FIFO_SHIFT)
#define ASRFSTi_IAEi_SHIFT		11
#define ASRFSTi_IAEi_MASK		(1 << ASRFSTi_OAFi_SHIFT)
#define ASRFSTi_IAEi			(1 << ASRFSTi_OAFi_SHIFT)
#define ASRFSTi_INPUT_FIFO_WIDTH	7
#define ASRFSTi_INPUT_FIFO_SHIFT	0
#define ASRFSTi_INPUT_FIFO_MASK		((1 << ASRFSTi_INPUT_FIFO_WIDTH) - 1)

/* REG28 0xC0 & 0xC4 & 0xC8 REG_ASRMCR1i */
#define ASRMCR1i_IWD_WIDTH		3
#define ASRMCR1i_IWD_SHIFT		9
#define ASRMCR1i_IWD_MASK		(((1 << ASRMCR1i_IWD_WIDTH) - 1) << ASRMCR1i_IWD_SHIFT)
#define ASRMCR1i_IWD(v)			((v) << ASRMCR1i_IWD_SHIFT)
#define ASRMCR1i_IMSB_SHIFT		8
#define ASRMCR1i_IMSB_MASK		(1 << ASRMCR1i_IMSB_SHIFT)
#define ASRMCR1i_IMSB_MSB		(1 << ASRMCR1i_IMSB_SHIFT)
#define ASRMCR1i_IMSB_LSB		(0 << ASRMCR1i_IMSB_SHIFT)
#define ASRMCR1i_OMSB_SHIFT		2
#define ASRMCR1i_OMSB_MASK		(1 << ASRMCR1i_OMSB_SHIFT)
#define ASRMCR1i_OMSB_MSB		(1 << ASRMCR1i_OMSB_SHIFT)
#define ASRMCR1i_OMSB_LSB		(0 << ASRMCR1i_OMSB_SHIFT)
#define ASRMCR1i_OSGN_SHIFT		1
#define ASRMCR1i_OSGN_MASK		(1 << ASRMCR1i_OSGN_SHIFT)
#define ASRMCR1i_OSGN			(1 << ASRMCR1i_OSGN_SHIFT)
#define ASRMCR1i_OW16_SHIFT		0
#define ASRMCR1i_OW16_MASK		(1 << ASRMCR1i_OW16_SHIFT)
#define ASRMCR1i_OW16(v)		((v) << ASRMCR1i_OW16_SHIFT)


enum asrc_pair_index {
	ASRC_INVALID_PAIR = -1,
	ASRC_PAIR_A = 0,
	ASRC_PAIR_B = 1,
	ASRC_PAIR_C = 2,
};

#define ASRC_PAIR_MAX_NUM	(ASRC_PAIR_C + 1)

enum asrc_inclk {
	INCLK_NONE = 0x03,
	INCLK_ESAI_RX = 0x00,
	INCLK_SSI1_RX = 0x01,
	INCLK_SSI2_RX = 0x02,
	INCLK_SSI3_RX = 0x07,
	INCLK_SPDIF_RX = 0x04,
	INCLK_MLB_CLK = 0x05,
	INCLK_PAD = 0x06,
	INCLK_ESAI_TX = 0x08,
	INCLK_SSI1_TX = 0x09,
	INCLK_SSI2_TX = 0x0a,
	INCLK_SSI3_TX = 0x0b,
	INCLK_SPDIF_TX = 0x0c,
	INCLK_ASRCK1_CLK = 0x0f,
};

enum asrc_outclk {
	OUTCLK_NONE = 0x03,
	OUTCLK_ESAI_TX = 0x00,
	OUTCLK_SSI1_TX = 0x01,
	OUTCLK_SSI2_TX = 0x02,
	OUTCLK_SSI3_TX = 0x07,
	OUTCLK_SPDIF_TX = 0x04,
	OUTCLK_MLB_CLK = 0x05,
	OUTCLK_PAD = 0x06,
	OUTCLK_ESAI_RX = 0x08,
	OUTCLK_SSI1_RX = 0x09,
	OUTCLK_SSI2_RX = 0x0a,
	OUTCLK_SSI3_RX = 0x0b,
	OUTCLK_SPDIF_RX = 0x0c,
	OUTCLK_ASRCK1_CLK = 0x0f,
};

#define ASRC_CLK_MAX_NUM	16

enum asrc_word_width {
	ASRC_WIDTH_24_BIT = 0,
	ASRC_WIDTH_16_BIT = 1,
	ASRC_WIDTH_8_BIT = 2,
};

struct asrc_config {
	enum asrc_pair_index pair;
	unsigned int channel_num;
	unsigned int buffer_num;
	unsigned int dma_buffer_size;
	unsigned int input_sample_rate;
	unsigned int output_sample_rate;
	enum asrc_word_width input_word_width;
	enum asrc_word_width output_word_width;
	enum asrc_inclk inclk;
	enum asrc_outclk outclk;
};

struct asrc_req {
	unsigned int chn_num;
	enum asrc_pair_index index;
};

struct asrc_querybuf {
	unsigned int buffer_index;
	unsigned int input_length;
	unsigned int output_length;
	unsigned long input_offset;
	unsigned long output_offset;
};

struct asrc_convert_buffer {
	void *input_buffer_vaddr;
	void *output_buffer_vaddr;
	unsigned int input_buffer_length;
	unsigned int output_buffer_length;
};

struct asrc_status_flags {
	enum asrc_pair_index index;
	unsigned int overload_error;
};

enum asrc_error_status {
	ASRC_TASK_Q_OVERLOAD		= 0x01,
	ASRC_OUTPUT_TASK_OVERLOAD	= 0x02,
	ASRC_INPUT_TASK_OVERLOAD	= 0x04,
	ASRC_OUTPUT_BUFFER_OVERFLOW	= 0x08,
	ASRC_INPUT_BUFFER_UNDERRUN	= 0x10,
};

struct dma_block {
	dma_addr_t dma_paddr;
	void *dma_vaddr;
	unsigned int length;
};

/**
 * fsl_asrc_pair: ASRC Pair private data
 *
 * @asrc_priv: pointer to its parent module
 * @config: configuration profile
 * @error: error record
 * @index: pair index (ASRC_PAIR_A, ASRC_PAIR_B, ASRC_PAIR_C)
 * @channels: occupied channel number
 * @desc: input and output dma descriptors
 * @dma_chan: inputer and output DMA channels
 * @dma_data: private dma data
 * @pos: hardware pointer position
 * @private: pair private area
 */
struct fsl_asrc_pair {
	struct fsl_asrc *asrc_priv;
	struct asrc_config *config;
	unsigned int error;

	enum asrc_pair_index index;
	unsigned int channels;

	struct dma_async_tx_descriptor *desc[2];
	struct dma_chan *dma_chan[2];
	struct imx_dma_data dma_data;
	unsigned int pos;

	void *private;
};

/**
 * fsl_asrc_pair: ASRC private data
 *
 * @dma_params_rx: DMA parameters for receive channel
 * @dma_params_tx: DMA parameters for transmit channel
 * @pdev: platform device pointer
 * @regmap: regmap handler
 * @paddr: physical address to the base address of registers
 * @mem_clk: clock source to access register
 * @ipg_clk: clock source to drive peripheral
 * @spba_clk: SPBA clock (optional, depending on SoC design)
 * @asrck_clk: clock sources to driver ASRC internal logic
 * @lock: spin lock for resource protection
 * @pair: pair pointers
 * @channel_bits: width of ASRCNCR register for each pair
 * @channel_avail: non-occupied channel numbers
 * @asrc_rate: default sample rate for ASoC Back-Ends
 * @asrc_width: default sample width for ASoC Back-Ends
 * @regcache_cfg: store register value of REG_ASRCFG
 */
struct fsl_asrc {
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
	struct platform_device *pdev;
	struct regmap *regmap;
	unsigned long paddr;
	struct clk *mem_clk;
	struct clk *ipg_clk;
	struct clk *spba_clk;
	struct clk *asrck_clk[ASRC_CLK_MAX_NUM];
	spinlock_t lock;

	struct fsl_asrc_pair *pair[ASRC_PAIR_MAX_NUM];
	unsigned int channel_bits;
	unsigned int channel_avail;

	int asrc_rate;
	int asrc_width;

	u32 regcache_cfg;
};

extern struct snd_soc_platform_driver fsl_asrc_platform;
struct dma_chan *fsl_asrc_get_dma_channel(struct fsl_asrc_pair *pair, bool dir);
#endif /* _FSL_ASRC_H */
