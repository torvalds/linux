// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * mixer interface for stereo cards
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/asoundef.h>
#include "pcxhr.h"
#include "pcxhr_core.h"
#include "pcxhr_mix22.h"


/* registers used on the DSP and Xilinx (port 2) : HR stereo cards only */
#define PCXHR_DSP_RESET		0x20
#define PCXHR_XLX_CFG		0x24
#define PCXHR_XLX_RUER		0x28
#define PCXHR_XLX_DATA		0x2C
#define PCXHR_XLX_STATUS	0x30
#define PCXHR_XLX_LOFREQ	0x34
#define PCXHR_XLX_HIFREQ	0x38
#define PCXHR_XLX_CSUER		0x3C
#define PCXHR_XLX_SELMIC	0x40

#define PCXHR_DSP 2

/* byte access only ! */
#define PCXHR_INPB(mgr, x)	inb((mgr)->port[PCXHR_DSP] + (x))
#define PCXHR_OUTPB(mgr, x, data) outb((data), (mgr)->port[PCXHR_DSP] + (x))


/* values for PCHR_DSP_RESET register */
#define PCXHR_DSP_RESET_DSP	0x01
#define PCXHR_DSP_RESET_MUTE	0x02
#define PCXHR_DSP_RESET_CODEC	0x08
#define PCXHR_DSP_RESET_SMPTE	0x10
#define PCXHR_DSP_RESET_GPO_OFFSET	5
#define PCXHR_DSP_RESET_GPO_MASK	0x60

/* values for PCHR_XLX_CFG register */
#define PCXHR_CFG_SYNCDSP_MASK		0x80
#define PCXHR_CFG_DEPENDENCY_MASK	0x60
#define PCXHR_CFG_INDEPENDANT_SEL	0x00
#define PCXHR_CFG_MASTER_SEL		0x40
#define PCXHR_CFG_SLAVE_SEL		0x20
#define PCXHR_CFG_DATA_UER1_SEL_MASK	0x10	/* 0 (UER0), 1(UER1) */
#define PCXHR_CFG_DATAIN_SEL_MASK	0x08	/* 0 (ana), 1 (UER) */
#define PCXHR_CFG_SRC_MASK		0x04	/* 0 (Bypass), 1 (SRC Actif) */
#define PCXHR_CFG_CLOCK_UER1_SEL_MASK	0x02	/* 0 (UER0), 1(UER1) */
#define PCXHR_CFG_CLOCKIN_SEL_MASK	0x01	/* 0 (internal), 1 (AES/EBU) */

/* values for PCHR_XLX_DATA register */
#define PCXHR_DATA_CODEC	0x80
#define AKM_POWER_CONTROL_CMD	0xA007
#define AKM_RESET_ON_CMD	0xA100
#define AKM_RESET_OFF_CMD	0xA103
#define AKM_CLOCK_INF_55K_CMD	0xA240
#define AKM_CLOCK_SUP_55K_CMD	0xA24D
#define AKM_MUTE_CMD		0xA38D
#define AKM_UNMUTE_CMD		0xA30D
#define AKM_LEFT_LEVEL_CMD	0xA600
#define AKM_RIGHT_LEVEL_CMD	0xA700

/* values for PCHR_XLX_STATUS register - READ */
#define PCXHR_STAT_SRC_LOCK		0x01
#define PCXHR_STAT_LEVEL_IN		0x02
#define PCXHR_STAT_GPI_OFFSET		2
#define PCXHR_STAT_GPI_MASK		0x0C
#define PCXHR_STAT_MIC_CAPS		0x10
/* values for PCHR_XLX_STATUS register - WRITE */
#define PCXHR_STAT_FREQ_SYNC_MASK	0x01
#define PCXHR_STAT_FREQ_UER1_MASK	0x02
#define PCXHR_STAT_FREQ_SAVE_MASK	0x80

/* values for PCHR_XLX_CSUER register */
#define PCXHR_SUER1_BIT_U_READ_MASK	0x80
#define PCXHR_SUER1_BIT_C_READ_MASK	0x40
#define PCXHR_SUER1_DATA_PRESENT_MASK	0x20
#define PCXHR_SUER1_CLOCK_PRESENT_MASK	0x10
#define PCXHR_SUER_BIT_U_READ_MASK	0x08
#define PCXHR_SUER_BIT_C_READ_MASK	0x04
#define PCXHR_SUER_DATA_PRESENT_MASK	0x02
#define PCXHR_SUER_CLOCK_PRESENT_MASK	0x01

#define PCXHR_SUER_BIT_U_WRITE_MASK	0x02
#define PCXHR_SUER_BIT_C_WRITE_MASK	0x01

/* values for PCXHR_XLX_SELMIC register - WRITE */
#define PCXHR_SELMIC_PREAMPLI_OFFSET	2
#define PCXHR_SELMIC_PREAMPLI_MASK	0x0C
#define PCXHR_SELMIC_PHANTOM_ALIM	0x80


static const unsigned char g_hr222_p_level[] = {
    0x00,   /* [000] -49.5 dB:	AKM[000] = -1.#INF dB	(mute) */
    0x01,   /* [001] -49.0 dB:	AKM[001] = -48.131 dB	(diff=0.86920 dB) */
    0x01,   /* [002] -48.5 dB:	AKM[001] = -48.131 dB	(diff=0.36920 dB) */
    0x01,   /* [003] -48.0 dB:	AKM[001] = -48.131 dB	(diff=0.13080 dB) */
    0x01,   /* [004] -47.5 dB:	AKM[001] = -48.131 dB	(diff=0.63080 dB) */
    0x01,   /* [005] -46.5 dB:	AKM[001] = -48.131 dB	(diff=1.63080 dB) */
    0x01,   /* [006] -47.0 dB:	AKM[001] = -48.131 dB	(diff=1.13080 dB) */
    0x01,   /* [007] -46.0 dB:	AKM[001] = -48.131 dB	(diff=2.13080 dB) */
    0x01,   /* [008] -45.5 dB:	AKM[001] = -48.131 dB	(diff=2.63080 dB) */
    0x02,   /* [009] -45.0 dB:	AKM[002] = -42.110 dB	(diff=2.88980 dB) */
    0x02,   /* [010] -44.5 dB:	AKM[002] = -42.110 dB	(diff=2.38980 dB) */
    0x02,   /* [011] -44.0 dB:	AKM[002] = -42.110 dB	(diff=1.88980 dB) */
    0x02,   /* [012] -43.5 dB:	AKM[002] = -42.110 dB	(diff=1.38980 dB) */
    0x02,   /* [013] -43.0 dB:	AKM[002] = -42.110 dB	(diff=0.88980 dB) */
    0x02,   /* [014] -42.5 dB:	AKM[002] = -42.110 dB	(diff=0.38980 dB) */
    0x02,   /* [015] -42.0 dB:	AKM[002] = -42.110 dB	(diff=0.11020 dB) */
    0x02,   /* [016] -41.5 dB:	AKM[002] = -42.110 dB	(diff=0.61020 dB) */
    0x02,   /* [017] -41.0 dB:	AKM[002] = -42.110 dB	(diff=1.11020 dB) */
    0x02,   /* [018] -40.5 dB:	AKM[002] = -42.110 dB	(diff=1.61020 dB) */
    0x03,   /* [019] -40.0 dB:	AKM[003] = -38.588 dB	(diff=1.41162 dB) */
    0x03,   /* [020] -39.5 dB:	AKM[003] = -38.588 dB	(diff=0.91162 dB) */
    0x03,   /* [021] -39.0 dB:	AKM[003] = -38.588 dB	(diff=0.41162 dB) */
    0x03,   /* [022] -38.5 dB:	AKM[003] = -38.588 dB	(diff=0.08838 dB) */
    0x03,   /* [023] -38.0 dB:	AKM[003] = -38.588 dB	(diff=0.58838 dB) */
    0x03,   /* [024] -37.5 dB:	AKM[003] = -38.588 dB	(diff=1.08838 dB) */
    0x04,   /* [025] -37.0 dB:	AKM[004] = -36.090 dB	(diff=0.91040 dB) */
    0x04,   /* [026] -36.5 dB:	AKM[004] = -36.090 dB	(diff=0.41040 dB) */
    0x04,   /* [027] -36.0 dB:	AKM[004] = -36.090 dB	(diff=0.08960 dB) */
    0x04,   /* [028] -35.5 dB:	AKM[004] = -36.090 dB	(diff=0.58960 dB) */
    0x05,   /* [029] -35.0 dB:	AKM[005] = -34.151 dB	(diff=0.84860 dB) */
    0x05,   /* [030] -34.5 dB:	AKM[005] = -34.151 dB	(diff=0.34860 dB) */
    0x05,   /* [031] -34.0 dB:	AKM[005] = -34.151 dB	(diff=0.15140 dB) */
    0x05,   /* [032] -33.5 dB:	AKM[005] = -34.151 dB	(diff=0.65140 dB) */
    0x06,   /* [033] -33.0 dB:	AKM[006] = -32.568 dB	(diff=0.43222 dB) */
    0x06,   /* [034] -32.5 dB:	AKM[006] = -32.568 dB	(diff=0.06778 dB) */
    0x06,   /* [035] -32.0 dB:	AKM[006] = -32.568 dB	(diff=0.56778 dB) */
    0x07,   /* [036] -31.5 dB:	AKM[007] = -31.229 dB	(diff=0.27116 dB) */
    0x07,   /* [037] -31.0 dB:	AKM[007] = -31.229 dB	(diff=0.22884 dB) */
    0x08,   /* [038] -30.5 dB:	AKM[008] = -30.069 dB	(diff=0.43100 dB) */
    0x08,   /* [039] -30.0 dB:	AKM[008] = -30.069 dB	(diff=0.06900 dB) */
    0x09,   /* [040] -29.5 dB:	AKM[009] = -29.046 dB	(diff=0.45405 dB) */
    0x09,   /* [041] -29.0 dB:	AKM[009] = -29.046 dB	(diff=0.04595 dB) */
    0x0a,   /* [042] -28.5 dB:	AKM[010] = -28.131 dB	(diff=0.36920 dB) */
    0x0a,   /* [043] -28.0 dB:	AKM[010] = -28.131 dB	(diff=0.13080 dB) */
    0x0b,   /* [044] -27.5 dB:	AKM[011] = -27.303 dB	(diff=0.19705 dB) */
    0x0b,   /* [045] -27.0 dB:	AKM[011] = -27.303 dB	(diff=0.30295 dB) */
    0x0c,   /* [046] -26.5 dB:	AKM[012] = -26.547 dB	(diff=0.04718 dB) */
    0x0d,   /* [047] -26.0 dB:	AKM[013] = -25.852 dB	(diff=0.14806 dB) */
    0x0e,   /* [048] -25.5 dB:	AKM[014] = -25.208 dB	(diff=0.29176 dB) */
    0x0e,   /* [049] -25.0 dB:	AKM[014] = -25.208 dB	(diff=0.20824 dB) */
    0x0f,   /* [050] -24.5 dB:	AKM[015] = -24.609 dB	(diff=0.10898 dB) */
    0x10,   /* [051] -24.0 dB:	AKM[016] = -24.048 dB	(diff=0.04840 dB) */
    0x11,   /* [052] -23.5 dB:	AKM[017] = -23.522 dB	(diff=0.02183 dB) */
    0x12,   /* [053] -23.0 dB:	AKM[018] = -23.025 dB	(diff=0.02535 dB) */
    0x13,   /* [054] -22.5 dB:	AKM[019] = -22.556 dB	(diff=0.05573 dB) */
    0x14,   /* [055] -22.0 dB:	AKM[020] = -22.110 dB	(diff=0.11020 dB) */
    0x15,   /* [056] -21.5 dB:	AKM[021] = -21.686 dB	(diff=0.18642 dB) */
    0x17,   /* [057] -21.0 dB:	AKM[023] = -20.896 dB	(diff=0.10375 dB) */
    0x18,   /* [058] -20.5 dB:	AKM[024] = -20.527 dB	(diff=0.02658 dB) */
    0x1a,   /* [059] -20.0 dB:	AKM[026] = -19.831 dB	(diff=0.16866 dB) */
    0x1b,   /* [060] -19.5 dB:	AKM[027] = -19.504 dB	(diff=0.00353 dB) */
    0x1d,   /* [061] -19.0 dB:	AKM[029] = -18.883 dB	(diff=0.11716 dB) */
    0x1e,   /* [062] -18.5 dB:	AKM[030] = -18.588 dB	(diff=0.08838 dB) */
    0x20,   /* [063] -18.0 dB:	AKM[032] = -18.028 dB	(diff=0.02780 dB) */
    0x22,   /* [064] -17.5 dB:	AKM[034] = -17.501 dB	(diff=0.00123 dB) */
    0x24,   /* [065] -17.0 dB:	AKM[036] = -17.005 dB	(diff=0.00475 dB) */
    0x26,   /* [066] -16.5 dB:	AKM[038] = -16.535 dB	(diff=0.03513 dB) */
    0x28,   /* [067] -16.0 dB:	AKM[040] = -16.090 dB	(diff=0.08960 dB) */
    0x2b,   /* [068] -15.5 dB:	AKM[043] = -15.461 dB	(diff=0.03857 dB) */
    0x2d,   /* [069] -15.0 dB:	AKM[045] = -15.067 dB	(diff=0.06655 dB) */
    0x30,   /* [070] -14.5 dB:	AKM[048] = -14.506 dB	(diff=0.00598 dB) */
    0x33,   /* [071] -14.0 dB:	AKM[051] = -13.979 dB	(diff=0.02060 dB) */
    0x36,   /* [072] -13.5 dB:	AKM[054] = -13.483 dB	(diff=0.01707 dB) */
    0x39,   /* [073] -13.0 dB:	AKM[057] = -13.013 dB	(diff=0.01331 dB) */
    0x3c,   /* [074] -12.5 dB:	AKM[060] = -12.568 dB	(diff=0.06778 dB) */
    0x40,   /* [075] -12.0 dB:	AKM[064] = -12.007 dB	(diff=0.00720 dB) */
    0x44,   /* [076] -11.5 dB:	AKM[068] = -11.481 dB	(diff=0.01937 dB) */
    0x48,   /* [077] -11.0 dB:	AKM[072] = -10.984 dB	(diff=0.01585 dB) */
    0x4c,   /* [078] -10.5 dB:	AKM[076] = -10.515 dB	(diff=0.01453 dB) */
    0x51,   /* [079] -10.0 dB:	AKM[081] = -9.961 dB	(diff=0.03890 dB) */
    0x55,   /* [080] -9.5 dB:	AKM[085] = -9.542 dB	(diff=0.04243 dB) */
    0x5a,   /* [081] -9.0 dB:	AKM[090] = -9.046 dB	(diff=0.04595 dB) */
    0x60,   /* [082] -8.5 dB:	AKM[096] = -8.485 dB	(diff=0.01462 dB) */
    0x66,   /* [083] -8.0 dB:	AKM[102] = -7.959 dB	(diff=0.04120 dB) */
    0x6c,   /* [084] -7.5 dB:	AKM[108] = -7.462 dB	(diff=0.03767 dB) */
    0x72,   /* [085] -7.0 dB:	AKM[114] = -6.993 dB	(diff=0.00729 dB) */
    0x79,   /* [086] -6.5 dB:	AKM[121] = -6.475 dB	(diff=0.02490 dB) */
    0x80,   /* [087] -6.0 dB:	AKM[128] = -5.987 dB	(diff=0.01340 dB) */
    0x87,   /* [088] -5.5 dB:	AKM[135] = -5.524 dB	(diff=0.02413 dB) */
    0x8f,   /* [089] -5.0 dB:	AKM[143] = -5.024 dB	(diff=0.02408 dB) */
    0x98,   /* [090] -4.5 dB:	AKM[152] = -4.494 dB	(diff=0.00607 dB) */
    0xa1,   /* [091] -4.0 dB:	AKM[161] = -3.994 dB	(diff=0.00571 dB) */
    0xaa,   /* [092] -3.5 dB:	AKM[170] = -3.522 dB	(diff=0.02183 dB) */
    0xb5,   /* [093] -3.0 dB:	AKM[181] = -2.977 dB	(diff=0.02277 dB) */
    0xbf,   /* [094] -2.5 dB:	AKM[191] = -2.510 dB	(diff=0.01014 dB) */
    0xcb,   /* [095] -2.0 dB:	AKM[203] = -1.981 dB	(diff=0.01912 dB) */
    0xd7,   /* [096] -1.5 dB:	AKM[215] = -1.482 dB	(diff=0.01797 dB) */
    0xe3,   /* [097] -1.0 dB:	AKM[227] = -1.010 dB	(diff=0.01029 dB) */
    0xf1,   /* [098] -0.5 dB:	AKM[241] = -0.490 dB	(diff=0.00954 dB) */
    0xff,   /* [099] +0.0 dB:	AKM[255] = +0.000 dB	(diff=0.00000 dB) */
};


static void hr222_config_akm(struct pcxhr_mgr *mgr, unsigned short data)
{
	unsigned short mask = 0x8000;
	/* activate access to codec registers */
	PCXHR_INPB(mgr, PCXHR_XLX_HIFREQ);

	while (mask) {
		PCXHR_OUTPB(mgr, PCXHR_XLX_DATA,
			    data & mask ? PCXHR_DATA_CODEC : 0);
		mask >>= 1;
	}
	/* termiate access to codec registers */
	PCXHR_INPB(mgr, PCXHR_XLX_RUER);
}


static int hr222_set_hw_playback_level(struct pcxhr_mgr *mgr,
				       int idx, int level)
{
	unsigned short cmd;
	if (idx > 1 ||
	    level < 0 ||
	    level >= ARRAY_SIZE(g_hr222_p_level))
		return -EINVAL;

	if (idx == 0)
		cmd = AKM_LEFT_LEVEL_CMD;
	else
		cmd = AKM_RIGHT_LEVEL_CMD;

	/* conversion from PmBoardCodedLevel to AKM nonlinear programming */
	cmd += g_hr222_p_level[level];

	hr222_config_akm(mgr, cmd);
	return 0;
}


static int hr222_set_hw_capture_level(struct pcxhr_mgr *mgr,
				      int level_l, int level_r, int level_mic)
{
	/* program all input levels at the same time */
	unsigned int data;
	int i;

	if (!mgr->capture_chips)
		return -EINVAL;	/* no PCX22 */

	data  = ((level_mic & 0xff) << 24);	/* micro is mono, but apply */
	data |= ((level_mic & 0xff) << 16);	/* level on both channels */
	data |= ((level_r & 0xff) << 8);	/* line input right channel */
	data |= (level_l & 0xff);		/* line input left channel */

	PCXHR_INPB(mgr, PCXHR_XLX_DATA);	/* activate input codec */
	/* send 32 bits (4 x 8 bits) */
	for (i = 0; i < 32; i++, data <<= 1) {
		PCXHR_OUTPB(mgr, PCXHR_XLX_DATA,
			    (data & 0x80000000) ? PCXHR_DATA_CODEC : 0);
	}
	PCXHR_INPB(mgr, PCXHR_XLX_RUER);	/* close input level codec */
	return 0;
}

static void hr222_micro_boost(struct pcxhr_mgr *mgr, int level);

int hr222_sub_init(struct pcxhr_mgr *mgr)
{
	unsigned char reg;

	mgr->board_has_analog = 1;	/* analog always available */
	mgr->xlx_cfg = PCXHR_CFG_SYNCDSP_MASK;

	reg = PCXHR_INPB(mgr, PCXHR_XLX_STATUS);
	if (reg & PCXHR_STAT_MIC_CAPS)
		mgr->board_has_mic = 1;	/* microphone available */
	dev_dbg(&mgr->pci->dev,
		"MIC input available = %d\n", mgr->board_has_mic);

	/* reset codec */
	PCXHR_OUTPB(mgr, PCXHR_DSP_RESET,
		    PCXHR_DSP_RESET_DSP);
	msleep(5);
	mgr->dsp_reset = PCXHR_DSP_RESET_DSP  |
			 PCXHR_DSP_RESET_MUTE |
			 PCXHR_DSP_RESET_CODEC;
	PCXHR_OUTPB(mgr, PCXHR_DSP_RESET, mgr->dsp_reset);
	/* hr222_write_gpo(mgr, 0); does the same */
	msleep(5);

	/* config AKM */
	hr222_config_akm(mgr, AKM_POWER_CONTROL_CMD);
	hr222_config_akm(mgr, AKM_CLOCK_INF_55K_CMD);
	hr222_config_akm(mgr, AKM_UNMUTE_CMD);
	hr222_config_akm(mgr, AKM_RESET_OFF_CMD);

	/* init micro boost */
	hr222_micro_boost(mgr, 0);

	return 0;
}


/* calc PLL register */
/* TODO : there is a very similar fct in pcxhr.c */
static int hr222_pll_freq_register(unsigned int freq,
				   unsigned int *pllreg,
				   unsigned int *realfreq)
{
	unsigned int reg;

	if (freq < 6900 || freq > 219000)
		return -EINVAL;
	reg = (28224000 * 2) / freq;
	reg = (reg - 1) / 2;
	if (reg < 0x100)
		*pllreg = reg + 0xC00;
	else if (reg < 0x200)
		*pllreg = reg + 0x800;
	else if (reg < 0x400)
		*pllreg = reg & 0x1ff;
	else if (reg < 0x800) {
		*pllreg = ((reg >> 1) & 0x1ff) + 0x200;
		reg &= ~1;
	} else {
		*pllreg = ((reg >> 2) & 0x1ff) + 0x400;
		reg &= ~3;
	}
	if (realfreq)
		*realfreq = (28224000 / (reg + 1));
	return 0;
}

int hr222_sub_set_clock(struct pcxhr_mgr *mgr,
			unsigned int rate,
			int *changed)
{
	unsigned int speed, pllreg = 0;
	int err;
	unsigned realfreq = rate;

	switch (mgr->use_clock_type) {
	case HR22_CLOCK_TYPE_INTERNAL:
		err = hr222_pll_freq_register(rate, &pllreg, &realfreq);
		if (err)
			return err;

		mgr->xlx_cfg &= ~(PCXHR_CFG_CLOCKIN_SEL_MASK |
				  PCXHR_CFG_CLOCK_UER1_SEL_MASK);
		break;
	case HR22_CLOCK_TYPE_AES_SYNC:
		mgr->xlx_cfg |= PCXHR_CFG_CLOCKIN_SEL_MASK;
		mgr->xlx_cfg &= ~PCXHR_CFG_CLOCK_UER1_SEL_MASK;
		break;
	case HR22_CLOCK_TYPE_AES_1:
		if (!mgr->board_has_aes1)
			return -EINVAL;

		mgr->xlx_cfg |= (PCXHR_CFG_CLOCKIN_SEL_MASK |
				 PCXHR_CFG_CLOCK_UER1_SEL_MASK);
		break;
	default:
		return -EINVAL;
	}
	hr222_config_akm(mgr, AKM_MUTE_CMD);

	if (mgr->use_clock_type == HR22_CLOCK_TYPE_INTERNAL) {
		PCXHR_OUTPB(mgr, PCXHR_XLX_HIFREQ, pllreg >> 8);
		PCXHR_OUTPB(mgr, PCXHR_XLX_LOFREQ, pllreg & 0xff);
	}

	/* set clock source */
	PCXHR_OUTPB(mgr, PCXHR_XLX_CFG, mgr->xlx_cfg);

	/* codec speed modes */
	speed = rate < 55000 ? 0 : 1;
	if (mgr->codec_speed != speed) {
		mgr->codec_speed = speed;
		if (speed == 0)
			hr222_config_akm(mgr, AKM_CLOCK_INF_55K_CMD);
		else
			hr222_config_akm(mgr, AKM_CLOCK_SUP_55K_CMD);
	}

	mgr->sample_rate_real = realfreq;
	mgr->cur_clock_type = mgr->use_clock_type;

	if (changed)
		*changed = 1;

	hr222_config_akm(mgr, AKM_UNMUTE_CMD);

	dev_dbg(&mgr->pci->dev, "set_clock to %dHz (realfreq=%d pllreg=%x)\n",
		    rate, realfreq, pllreg);
	return 0;
}

int hr222_get_external_clock(struct pcxhr_mgr *mgr,
			     enum pcxhr_clock_type clock_type,
			     int *sample_rate)
{
	int rate, calc_rate = 0;
	unsigned int ticks;
	unsigned char mask, reg;

	if (clock_type == HR22_CLOCK_TYPE_AES_SYNC) {

		mask = (PCXHR_SUER_CLOCK_PRESENT_MASK |
			PCXHR_SUER_DATA_PRESENT_MASK);
		reg = PCXHR_STAT_FREQ_SYNC_MASK;

	} else if (clock_type == HR22_CLOCK_TYPE_AES_1 && mgr->board_has_aes1) {

		mask = (PCXHR_SUER1_CLOCK_PRESENT_MASK |
			PCXHR_SUER1_DATA_PRESENT_MASK);
		reg = PCXHR_STAT_FREQ_UER1_MASK;

	} else {
		dev_dbg(&mgr->pci->dev,
			"get_external_clock : type %d not supported\n",
			    clock_type);
		return -EINVAL; /* other clocks not supported */
	}

	if ((PCXHR_INPB(mgr, PCXHR_XLX_CSUER) & mask) != mask) {
		dev_dbg(&mgr->pci->dev,
			"get_external_clock(%d) = 0 Hz\n", clock_type);
		*sample_rate = 0;
		return 0; /* no external clock locked */
	}

	PCXHR_OUTPB(mgr, PCXHR_XLX_STATUS, reg); /* calculate freq */

	/* save the measured clock frequency */
	reg |= PCXHR_STAT_FREQ_SAVE_MASK;

	if (mgr->last_reg_stat != reg) {
		udelay(500);	/* wait min 2 cycles of lowest freq (8000) */
		mgr->last_reg_stat = reg;
	}

	PCXHR_OUTPB(mgr, PCXHR_XLX_STATUS, reg); /* save */

	/* get the frequency */
	ticks = (unsigned int)PCXHR_INPB(mgr, PCXHR_XLX_CFG);
	ticks = (ticks & 0x03) << 8;
	ticks |= (unsigned int)PCXHR_INPB(mgr, PCXHR_DSP_RESET);

	if (ticks != 0)
		calc_rate = 28224000 / ticks;
	/* rounding */
	if (calc_rate > 184200)
		rate = 192000;
	else if (calc_rate > 152200)
		rate = 176400;
	else if (calc_rate > 112000)
		rate = 128000;
	else if (calc_rate > 92100)
		rate = 96000;
	else if (calc_rate > 76100)
		rate = 88200;
	else if (calc_rate > 56000)
		rate = 64000;
	else if (calc_rate > 46050)
		rate = 48000;
	else if (calc_rate > 38050)
		rate = 44100;
	else if (calc_rate > 28000)
		rate = 32000;
	else if (calc_rate > 23025)
		rate = 24000;
	else if (calc_rate > 19025)
		rate = 22050;
	else if (calc_rate > 14000)
		rate = 16000;
	else if (calc_rate > 11512)
		rate = 12000;
	else if (calc_rate > 9512)
		rate = 11025;
	else if (calc_rate > 7000)
		rate = 8000;
	else
		rate = 0;

	dev_dbg(&mgr->pci->dev, "External clock is at %d Hz (measured %d Hz)\n",
		    rate, calc_rate);
	*sample_rate = rate;
	return 0;
}


int hr222_read_gpio(struct pcxhr_mgr *mgr, int is_gpi, int *value)
{
	if (is_gpi) {
		unsigned char reg = PCXHR_INPB(mgr, PCXHR_XLX_STATUS);
		*value = (int)(reg & PCXHR_STAT_GPI_MASK) >>
			      PCXHR_STAT_GPI_OFFSET;
	} else {
		*value = (int)(mgr->dsp_reset & PCXHR_DSP_RESET_GPO_MASK) >>
			 PCXHR_DSP_RESET_GPO_OFFSET;
	}
	return 0;
}


int hr222_write_gpo(struct pcxhr_mgr *mgr, int value)
{
	unsigned char reg = mgr->dsp_reset & ~PCXHR_DSP_RESET_GPO_MASK;

	reg |= (unsigned char)(value << PCXHR_DSP_RESET_GPO_OFFSET) &
	       PCXHR_DSP_RESET_GPO_MASK;

	PCXHR_OUTPB(mgr, PCXHR_DSP_RESET, reg);
	mgr->dsp_reset = reg;
	return 0;
}

int hr222_manage_timecode(struct pcxhr_mgr *mgr, int enable)
{
	if (enable)
		mgr->dsp_reset |= PCXHR_DSP_RESET_SMPTE;
	else
		mgr->dsp_reset &= ~PCXHR_DSP_RESET_SMPTE;

	PCXHR_OUTPB(mgr, PCXHR_DSP_RESET, mgr->dsp_reset);
	return 0;
}

int hr222_update_analog_audio_level(struct snd_pcxhr *chip,
				    int is_capture, int channel)
{
	dev_dbg(chip->card->dev,
		"hr222_update_analog_audio_level(%s chan=%d)\n",
		snd_pcm_direction_name(is_capture), channel);
	if (is_capture) {
		int level_l, level_r, level_mic;
		/* we have to update all levels */
		if (chip->analog_capture_active) {
			level_l = chip->analog_capture_volume[0];
			level_r = chip->analog_capture_volume[1];
		} else {
			level_l = HR222_LINE_CAPTURE_LEVEL_MIN;
			level_r = HR222_LINE_CAPTURE_LEVEL_MIN;
		}
		if (chip->mic_active)
			level_mic = chip->mic_volume;
		else
			level_mic = HR222_MICRO_CAPTURE_LEVEL_MIN;
		return hr222_set_hw_capture_level(chip->mgr,
						 level_l, level_r, level_mic);
	} else {
		int vol;
		if (chip->analog_playback_active[channel])
			vol = chip->analog_playback_volume[channel];
		else
			vol = HR222_LINE_PLAYBACK_LEVEL_MIN;
		return hr222_set_hw_playback_level(chip->mgr, channel, vol);
	}
}


/*texts[5] = {"Line", "Digital", "Digi+SRC", "Mic", "Line+Mic"}*/
#define SOURCE_LINE	0
#define SOURCE_DIGITAL	1
#define SOURCE_DIGISRC	2
#define SOURCE_MIC	3
#define SOURCE_LINEMIC	4

int hr222_set_audio_source(struct snd_pcxhr *chip)
{
	int digital = 0;
	/* default analog source */
	chip->mgr->xlx_cfg &= ~(PCXHR_CFG_SRC_MASK |
				PCXHR_CFG_DATAIN_SEL_MASK |
				PCXHR_CFG_DATA_UER1_SEL_MASK);

	if (chip->audio_capture_source == SOURCE_DIGISRC) {
		chip->mgr->xlx_cfg |= PCXHR_CFG_SRC_MASK;
		digital = 1;
	} else {
		if (chip->audio_capture_source == SOURCE_DIGITAL)
			digital = 1;
	}
	if (digital) {
		chip->mgr->xlx_cfg |=  PCXHR_CFG_DATAIN_SEL_MASK;
		if (chip->mgr->board_has_aes1) {
			/* get data from the AES1 plug */
			chip->mgr->xlx_cfg |= PCXHR_CFG_DATA_UER1_SEL_MASK;
		}
		/* chip->mic_active = 0; */
		/* chip->analog_capture_active = 0; */
	} else {
		int update_lvl = 0;
		chip->analog_capture_active = 0;
		chip->mic_active = 0;
		if (chip->audio_capture_source == SOURCE_LINE ||
		    chip->audio_capture_source == SOURCE_LINEMIC) {
			if (chip->analog_capture_active == 0)
				update_lvl = 1;
			chip->analog_capture_active = 1;
		}
		if (chip->audio_capture_source == SOURCE_MIC ||
		    chip->audio_capture_source == SOURCE_LINEMIC) {
			if (chip->mic_active == 0)
				update_lvl = 1;
			chip->mic_active = 1;
		}
		if (update_lvl) {
			/* capture: update all 3 mutes/unmutes with one call */
			hr222_update_analog_audio_level(chip, 1, 0);
		}
	}
	/* set the source infos (max 3 bits modified) */
	PCXHR_OUTPB(chip->mgr, PCXHR_XLX_CFG, chip->mgr->xlx_cfg);
	return 0;
}


int hr222_iec958_capture_byte(struct snd_pcxhr *chip,
			     int aes_idx, unsigned char *aes_bits)
{
	unsigned char idx = (unsigned char)(aes_idx * 8);
	unsigned char temp = 0;
	unsigned char mask = chip->mgr->board_has_aes1 ?
		PCXHR_SUER1_BIT_C_READ_MASK : PCXHR_SUER_BIT_C_READ_MASK;
	int i;
	for (i = 0; i < 8; i++) {
		PCXHR_OUTPB(chip->mgr, PCXHR_XLX_RUER, idx++); /* idx < 192 */
		temp <<= 1;
		if (PCXHR_INPB(chip->mgr, PCXHR_XLX_CSUER) & mask)
			temp |= 1;
	}
	dev_dbg(chip->card->dev, "read iec958 AES %d byte %d = 0x%x\n",
		    chip->chip_idx, aes_idx, temp);
	*aes_bits = temp;
	return 0;
}


int hr222_iec958_update_byte(struct snd_pcxhr *chip,
			     int aes_idx, unsigned char aes_bits)
{
	int i;
	unsigned char new_bits = aes_bits;
	unsigned char old_bits = chip->aes_bits[aes_idx];
	unsigned char idx = (unsigned char)(aes_idx * 8);
	for (i = 0; i < 8; i++) {
		if ((old_bits & 0x01) != (new_bits & 0x01)) {
			/* idx < 192 */
			PCXHR_OUTPB(chip->mgr, PCXHR_XLX_RUER, idx);
			/* write C and U bit */
			PCXHR_OUTPB(chip->mgr, PCXHR_XLX_CSUER, new_bits&0x01 ?
				    PCXHR_SUER_BIT_C_WRITE_MASK : 0);
		}
		idx++;
		old_bits >>= 1;
		new_bits >>= 1;
	}
	chip->aes_bits[aes_idx] = aes_bits;
	return 0;
}

static void hr222_micro_boost(struct pcxhr_mgr *mgr, int level)
{
	unsigned char boost_mask;
	boost_mask = (unsigned char) (level << PCXHR_SELMIC_PREAMPLI_OFFSET);
	if (boost_mask & (~PCXHR_SELMIC_PREAMPLI_MASK))
		return; /* only values form 0 to 3 accepted */

	mgr->xlx_selmic &= ~PCXHR_SELMIC_PREAMPLI_MASK;
	mgr->xlx_selmic |= boost_mask;

	PCXHR_OUTPB(mgr, PCXHR_XLX_SELMIC, mgr->xlx_selmic);

	dev_dbg(&mgr->pci->dev, "hr222_micro_boost : set %x\n", boost_mask);
}

static void hr222_phantom_power(struct pcxhr_mgr *mgr, int power)
{
	if (power)
		mgr->xlx_selmic |= PCXHR_SELMIC_PHANTOM_ALIM;
	else
		mgr->xlx_selmic &= ~PCXHR_SELMIC_PHANTOM_ALIM;

	PCXHR_OUTPB(mgr, PCXHR_XLX_SELMIC, mgr->xlx_selmic);

	dev_dbg(&mgr->pci->dev, "hr222_phantom_power : set %d\n", power);
}


/* mic level */
static const DECLARE_TLV_DB_SCALE(db_scale_mic_hr222, -9850, 50, 650);

static int hr222_mic_vol_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = HR222_MICRO_CAPTURE_LEVEL_MIN; /* -98 dB */
	/* gains from 9 dB to 31.5 dB not recommended; use micboost instead */
	uinfo->value.integer.max = HR222_MICRO_CAPTURE_LEVEL_MAX; /*  +7 dB */
	return 0;
}

static int hr222_mic_vol_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	mutex_lock(&chip->mgr->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->mic_volume;
	mutex_unlock(&chip->mgr->mixer_mutex);
	return 0;
}

static int hr222_mic_vol_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	mutex_lock(&chip->mgr->mixer_mutex);
	if (chip->mic_volume != ucontrol->value.integer.value[0]) {
		changed = 1;
		chip->mic_volume = ucontrol->value.integer.value[0];
		hr222_update_analog_audio_level(chip, 1, 0);
	}
	mutex_unlock(&chip->mgr->mixer_mutex);
	return changed;
}

static const struct snd_kcontrol_new hr222_control_mic_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"Mic Capture Volume",
	.info =		hr222_mic_vol_info,
	.get =		hr222_mic_vol_get,
	.put =		hr222_mic_vol_put,
	.tlv = { .p = db_scale_mic_hr222 },
};


/* mic boost level */
static const DECLARE_TLV_DB_SCALE(db_scale_micboost_hr222, 0, 1800, 5400);

static int hr222_mic_boost_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;	/*  0 dB */
	uinfo->value.integer.max = 3;	/* 54 dB */
	return 0;
}

static int hr222_mic_boost_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	mutex_lock(&chip->mgr->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->mic_boost;
	mutex_unlock(&chip->mgr->mixer_mutex);
	return 0;
}

static int hr222_mic_boost_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	mutex_lock(&chip->mgr->mixer_mutex);
	if (chip->mic_boost != ucontrol->value.integer.value[0]) {
		changed = 1;
		chip->mic_boost = ucontrol->value.integer.value[0];
		hr222_micro_boost(chip->mgr, chip->mic_boost);
	}
	mutex_unlock(&chip->mgr->mixer_mutex);
	return changed;
}

static const struct snd_kcontrol_new hr222_control_mic_boost = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"MicBoost Capture Volume",
	.info =		hr222_mic_boost_info,
	.get =		hr222_mic_boost_get,
	.put =		hr222_mic_boost_put,
	.tlv = { .p = db_scale_micboost_hr222 },
};


/******************* Phantom power switch *******************/
#define hr222_phantom_power_info	snd_ctl_boolean_mono_info

static int hr222_phantom_power_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	mutex_lock(&chip->mgr->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->phantom_power;
	mutex_unlock(&chip->mgr->mixer_mutex);
	return 0;
}

static int hr222_phantom_power_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcxhr *chip = snd_kcontrol_chip(kcontrol);
	int power, changed = 0;

	mutex_lock(&chip->mgr->mixer_mutex);
	power = !!ucontrol->value.integer.value[0];
	if (chip->phantom_power != power) {
		hr222_phantom_power(chip->mgr, power);
		chip->phantom_power = power;
		changed = 1;
	}
	mutex_unlock(&chip->mgr->mixer_mutex);
	return changed;
}

static const struct snd_kcontrol_new hr222_phantom_power_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Phantom Power Switch",
	.info = hr222_phantom_power_info,
	.get = hr222_phantom_power_get,
	.put = hr222_phantom_power_put,
};


int hr222_add_mic_controls(struct snd_pcxhr *chip)
{
	int err;
	if (!chip->mgr->board_has_mic)
		return 0;

	/* controls */
	err = snd_ctl_add(chip->card, snd_ctl_new1(&hr222_control_mic_level,
						   chip));
	if (err < 0)
		return err;

	err = snd_ctl_add(chip->card, snd_ctl_new1(&hr222_control_mic_boost,
						   chip));
	if (err < 0)
		return err;

	err = snd_ctl_add(chip->card, snd_ctl_new1(&hr222_phantom_power_switch,
						   chip));
	return err;
}
