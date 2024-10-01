/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_WM8766_H
#define __SOUND_WM8766_H

/*
 *   ALSA driver for ICEnsemble VT17xx
 *
 *   Lowlevel functions for WM8766 codec
 *
 *	Copyright (c) 2012 Ondrej Zary <linux@rainbow-software.org>
 */

#define WM8766_REG_DACL1	0x00
#define WM8766_REG_DACR1	0x01
#define WM8766_VOL_MASK			0x1ff		/* incl. update bit */
#define WM8766_VOL_UPDATE		(1 << 8)	/* update volume */
#define WM8766_REG_DACCTRL1	0x02
#define WM8766_DAC_MUTEALL		(1 << 0)
#define WM8766_DAC_DEEMPALL		(1 << 1)
#define WM8766_DAC_PDWN			(1 << 2)
#define WM8766_DAC_ATC			(1 << 3)
#define WM8766_DAC_IZD			(1 << 4)
#define WM8766_DAC_PL_MASK		0x1e0
#define WM8766_DAC_PL_LL		(1 << 5)	/* L chan: L signal */
#define WM8766_DAC_PL_LR		(2 << 5)	/* L chan: R signal */
#define WM8766_DAC_PL_LB		(3 << 5)	/* L chan: both */
#define WM8766_DAC_PL_RL		(1 << 7)	/* R chan: L signal */
#define WM8766_DAC_PL_RR		(2 << 7)	/* R chan: R signal */
#define WM8766_DAC_PL_RB		(3 << 7)	/* R chan: both */
#define WM8766_REG_IFCTRL	0x03
#define WM8766_IF_FMT_RIGHTJ		(0 << 0)
#define WM8766_IF_FMT_LEFTJ		(1 << 0)
#define WM8766_IF_FMT_I2S		(2 << 0)
#define WM8766_IF_FMT_DSP		(3 << 0)
#define WM8766_IF_DSP_LATE		(1 << 2)	/* in DSP mode */
#define WM8766_IF_LRC_INVERTED		(1 << 2)	/* in other modes */
#define WM8766_IF_BCLK_INVERTED		(1 << 3)
#define WM8766_IF_IWL_16BIT		(0 << 4)
#define WM8766_IF_IWL_20BIT		(1 << 4)
#define WM8766_IF_IWL_24BIT		(2 << 4)
#define WM8766_IF_IWL_32BIT		(3 << 4)
#define WM8766_IF_MASK			0x3f
#define WM8766_PHASE_INVERT1		(1 << 6)
#define WM8766_PHASE_INVERT2		(1 << 7)
#define WM8766_PHASE_INVERT3		(1 << 8)
#define WM8766_REG_DACL2	0x04
#define WM8766_REG_DACR2	0x05
#define WM8766_REG_DACL3	0x06
#define WM8766_REG_DACR3	0x07
#define WM8766_REG_MASTDA	0x08
#define WM8766_REG_DACCTRL2	0x09
#define WM8766_DAC2_ZCD			(1 << 0)
#define WM8766_DAC2_ZFLAG_ALL		(0 << 1)
#define WM8766_DAC2_ZFLAG_1		(1 << 1)
#define WM8766_DAC2_ZFLAG_2		(2 << 1)
#define WM8766_DAC2_ZFLAG_3		(3 << 1)
#define WM8766_DAC2_MUTE1		(1 << 3)
#define WM8766_DAC2_MUTE2		(1 << 4)
#define WM8766_DAC2_MUTE3		(1 << 5)
#define WM8766_DAC2_DEEMP1		(1 << 6)
#define WM8766_DAC2_DEEMP2		(1 << 7)
#define WM8766_DAC2_DEEMP3		(1 << 8)
#define WM8766_REG_DACCTRL3	0x0a
#define WM8766_DAC3_DACPD1		(1 << 1)
#define WM8766_DAC3_DACPD2		(1 << 2)
#define WM8766_DAC3_DACPD3		(1 << 3)
#define WM8766_DAC3_PWRDNALL		(1 << 4)
#define WM8766_DAC3_POWER_MASK		0x1e
#define WM8766_DAC3_MASTER		(1 << 5)
#define WM8766_DAC3_DAC128FS		(0 << 6)
#define WM8766_DAC3_DAC192FS		(1 << 6)
#define WM8766_DAC3_DAC256FS		(2 << 6)
#define WM8766_DAC3_DAC384FS		(3 << 6)
#define WM8766_DAC3_DAC512FS		(4 << 6)
#define WM8766_DAC3_DAC768FS		(5 << 6)
#define WM8766_DAC3_MSTR_MASK		0x1e0
#define WM8766_REG_MUTE1	0x0c
#define WM8766_MUTE1_MPD		(1 << 6)
#define WM8766_REG_MUTE2	0x0f
#define WM8766_MUTE2_MPD		(1 << 5)
#define WM8766_REG_RESET	0x1f

#define WM8766_REG_COUNT	0x10	/* don't cache the RESET register */

struct snd_wm8766;

struct snd_wm8766_ops {
	void (*write)(struct snd_wm8766 *wm, u16 addr, u16 data);
};

enum snd_wm8766_ctl_id {
	WM8766_CTL_CH1_VOL,
	WM8766_CTL_CH2_VOL,
	WM8766_CTL_CH3_VOL,
	WM8766_CTL_CH1_SW,
	WM8766_CTL_CH2_SW,
	WM8766_CTL_CH3_SW,
	WM8766_CTL_PHASE1_SW,
	WM8766_CTL_PHASE2_SW,
	WM8766_CTL_PHASE3_SW,
	WM8766_CTL_DEEMPH1_SW,
	WM8766_CTL_DEEMPH2_SW,
	WM8766_CTL_DEEMPH3_SW,
	WM8766_CTL_IZD_SW,
	WM8766_CTL_ZC_SW,

	WM8766_CTL_COUNT,
};

#define WM8766_ENUM_MAX		16

#define WM8766_FLAG_STEREO	(1 << 0)
#define WM8766_FLAG_VOL_UPDATE	(1 << 1)
#define WM8766_FLAG_INVERT	(1 << 2)
#define WM8766_FLAG_LIM		(1 << 3)
#define WM8766_FLAG_ALC		(1 << 4)

struct snd_wm8766_ctl {
	struct snd_kcontrol *kctl;
	const char *name;
	snd_ctl_elem_type_t type;
	const char *const enum_names[WM8766_ENUM_MAX];
	const unsigned int *tlv;
	u16 reg1, reg2, mask1, mask2, min, max, flags;
	void (*set)(struct snd_wm8766 *wm, u16 ch1, u16 ch2);
	void (*get)(struct snd_wm8766 *wm, u16 *ch1, u16 *ch2);
};

enum snd_wm8766_agc_mode { WM8766_AGC_OFF, WM8766_AGC_LIM, WM8766_AGC_ALC };

struct snd_wm8766 {
	struct snd_card *card;
	struct snd_wm8766_ctl ctl[WM8766_CTL_COUNT];
	enum snd_wm8766_agc_mode agc_mode;
	struct snd_wm8766_ops ops;
	u16 regs[WM8766_REG_COUNT];	/* 9-bit registers */
};



void snd_wm8766_init(struct snd_wm8766 *wm);
void snd_wm8766_resume(struct snd_wm8766 *wm);
void snd_wm8766_set_if(struct snd_wm8766 *wm, u16 dac);
void snd_wm8766_volume_restore(struct snd_wm8766 *wm);
int snd_wm8766_build_controls(struct snd_wm8766 *wm);

#endif /* __SOUND_WM8766_H */
