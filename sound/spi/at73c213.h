/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for the AT73C213 16-bit stereo DAC on Atmel ATSTK1000
 *
 * Copyright (C) 2006 - 2007 Atmel Corporation
 */

#ifndef _SND_AT73C213_H
#define _SND_AT73C213_H

/* DAC control register */
#define DAC_CTRL		0x00
#define DAC_CTRL_ONPADRV	7
#define DAC_CTRL_ONAUXIN	6
#define DAC_CTRL_ONDACR		5
#define DAC_CTRL_ONDACL		4
#define DAC_CTRL_ONLNOR		3
#define DAC_CTRL_ONLNOL		2
#define DAC_CTRL_ONLNIR		1
#define DAC_CTRL_ONLNIL		0

/* DAC left line in gain register */
#define DAC_LLIG		0x01
#define DAC_LLIG_LLIG		0

/* DAC right line in gain register */
#define DAC_RLIG		0x02
#define DAC_RLIG_RLIG		0

/* DAC Left Master Playback Gain Register */
#define DAC_LMPG		0x03
#define DAC_LMPG_LMPG		0

/* DAC Right Master Playback Gain Register */
#define DAC_RMPG		0x04
#define DAC_RMPG_RMPG		0

/* DAC Left Line Out Gain Register */
#define DAC_LLOG		0x05
#define DAC_LLOG_LLOG		0

/* DAC Right Line Out Gain Register */
#define DAC_RLOG		0x06
#define DAC_RLOG_RLOG		0

/* DAC Output Level Control Register */
#define DAC_OLC			0x07
#define DAC_OLC_RSHORT		7
#define DAC_OLC_ROLC		4
#define DAC_OLC_LSHORT		3
#define DAC_OLC_LOLC		0

/* DAC Mixer Control Register */
#define DAC_MC			0x08
#define DAC_MC_INVR		5
#define DAC_MC_INVL		4
#define DAC_MC_RMSMIN2		3
#define DAC_MC_RMSMIN1		2
#define DAC_MC_LMSMIN2		1
#define DAC_MC_LMSMIN1		0

/* DAC Clock and Sampling Frequency Control Register */
#define DAC_CSFC		0x09
#define DAC_CSFC_OVRSEL		4

/* DAC Miscellaneous Register */
#define DAC_MISC		0x0A
#define DAC_MISC_VCMCAPSEL	7
#define DAC_MISC_DINTSEL	4
#define DAC_MISC_DITHEN		3
#define DAC_MISC_DEEMPEN	2
#define DAC_MISC_NBITS		0

/* DAC Precharge Control Register */
#define DAC_PRECH		0x0C
#define DAC_PRECH_PRCHGPDRV	7
#define DAC_PRECH_PRCHGAUX1	6
#define DAC_PRECH_PRCHGLNOR	5
#define DAC_PRECH_PRCHGLNOL	4
#define DAC_PRECH_PRCHGLNIR	3
#define DAC_PRECH_PRCHGLNIL	2
#define DAC_PRECH_PRCHG		1
#define DAC_PRECH_ONMSTR	0

/* DAC Auxiliary Input Gain Control Register */
#define DAC_AUXG		0x0D
#define DAC_AUXG_AUXG		0

/* DAC Reset Register */
#define DAC_RST			0x10
#define DAC_RST_RESMASK		2
#define DAC_RST_RESFILZ		1
#define DAC_RST_RSTZ		0

/* Power Amplifier Control Register */
#define PA_CTRL			0x11
#define PA_CTRL_APAON		6
#define PA_CTRL_APAPRECH	5
#define PA_CTRL_APALP		4
#define PA_CTRL_APAGAIN		0

#endif /* _SND_AT73C213_H */
