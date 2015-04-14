#ifndef __SOUND_WM8776_H
#define __SOUND_WM8776_H

/*
 *   ALSA driver for ICEnsemble VT17xx
 *
 *   Lowlevel functions for WM8776 codec
 *
 *	Copyright (c) 2012 Ondrej Zary <linux@rainbow-software.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define WM8776_REG_HPLVOL	0x00
#define WM8776_REG_HPRVOL	0x01
#define WM8776_REG_HPMASTER	0x02
#define WM8776_HPVOL_MASK		0x17f		/* incl. update bit */
#define WM8776_VOL_HPZCEN		(1 << 7)	/* zero cross detect */
#define WM8776_VOL_UPDATE		(1 << 8)	/* update volume */
#define WM8776_REG_DACLVOL	0x03
#define WM8776_REG_DACRVOL	0x04
#define WM8776_REG_DACMASTER	0x05
#define WM8776_DACVOL_MASK		0x1ff		/* incl. update bit */
#define WM8776_REG_PHASESWAP	0x06
#define WM8776_PHASE_INVERTL		(1 << 0)
#define WM8776_PHASE_INVERTR		(1 << 1)
#define WM8776_REG_DACCTRL1	0x07
#define WM8776_DAC_DZCEN		(1 << 0)
#define WM8776_DAC_ATC			(1 << 1)
#define WM8776_DAC_IZD			(1 << 2)
#define WM8776_DAC_TOD			(1 << 3)
#define WM8776_DAC_PL_MASK		0xf0
#define WM8776_DAC_PL_LL		(1 << 4)	/* L chan: L signal */
#define WM8776_DAC_PL_LR		(2 << 4)	/* L chan: R signal */
#define WM8776_DAC_PL_LB		(3 << 4)	/* L chan: both */
#define WM8776_DAC_PL_RL		(1 << 6)	/* R chan: L signal */
#define WM8776_DAC_PL_RR		(2 << 6)	/* R chan: R signal */
#define WM8776_DAC_PL_RB		(3 << 6)	/* R chan: both */
#define WM8776_REG_DACMUTE	0x08
#define WM8776_DACMUTE			(1 << 0)
#define WM8776_REG_DACCTRL2	0x09
#define WM8776_DAC2_DEEMPH		(1 << 0)
#define WM8776_DAC2_ZFLAG_DISABLE	(0 << 1)
#define WM8776_DAC2_ZFLAG_OWN		(1 << 1)
#define WM8776_DAC2_ZFLAG_BOTH		(2 << 1)
#define WM8776_DAC2_ZFLAG_EITHER	(3 << 1)
#define WM8776_REG_DACIFCTRL	0x0a
#define WM8776_FMT_RIGHTJ		(0 << 0)
#define WM8776_FMT_LEFTJ		(1 << 0)
#define WM8776_FMT_I2S			(2 << 0)
#define WM8776_FMT_DSP			(3 << 0)
#define WM8776_FMT_DSP_LATE		(1 << 2)	/* in DSP mode */
#define WM8776_FMT_LRC_INVERTED		(1 << 2)	/* in other modes */
#define WM8776_FMT_BCLK_INVERTED	(1 << 3)
#define WM8776_FMT_16BIT		(0 << 4)
#define WM8776_FMT_20BIT		(1 << 4)
#define WM8776_FMT_24BIT		(2 << 4)
#define WM8776_FMT_32BIT		(3 << 4)
#define WM8776_REG_ADCIFCTRL	0x0b
#define WM8776_FMT_ADCMCLK_INVERTED	(1 << 6)
#define WM8776_FMT_ADCHPD		(1 << 8)
#define WM8776_REG_MSTRCTRL	0x0c
#define WM8776_IF_ADC256FS		(2 << 0)
#define WM8776_IF_ADC384FS		(3 << 0)
#define WM8776_IF_ADC512FS		(4 << 0)
#define WM8776_IF_ADC768FS		(5 << 0)
#define WM8776_IF_OVERSAMP64		(1 << 3)
#define WM8776_IF_DAC128FS		(0 << 4)
#define WM8776_IF_DAC192FS		(1 << 4)
#define WM8776_IF_DAC256FS		(2 << 4)
#define WM8776_IF_DAC384FS		(3 << 4)
#define WM8776_IF_DAC512FS		(4 << 4)
#define WM8776_IF_DAC768FS		(5 << 4)
#define WM8776_IF_DAC_MASTER		(1 << 7)
#define WM8776_IF_ADC_MASTER		(1 << 8)
#define WM8776_REG_PWRDOWN	0x0d
#define WM8776_PWR_PDWN			(1 << 0)
#define WM8776_PWR_ADCPD		(1 << 1)
#define WM8776_PWR_DACPD		(1 << 2)
#define WM8776_PWR_HPPD			(1 << 3)
#define WM8776_PWR_AINPD		(1 << 6)
#define WM8776_REG_ADCLVOL	0x0e
#define WM8776_REG_ADCRVOL	0x0f
#define WM8776_ADC_GAIN_MASK		0xff
#define WM8776_ADC_ZCEN			(1 << 8)
#define WM8776_REG_ALCCTRL1	0x10
#define WM8776_ALC1_LCT_MASK		0x0f	/* 0=-16dB, 1=-15dB..15=-1dB */
#define WM8776_ALC1_MAXGAIN_MASK	0x70	/* 0,1=0dB, 2=+4dB...7=+24dB */
#define WM8776_ALC1_LCSEL_MASK		0x180
#define WM8776_ALC1_LCSEL_LIMITER	(0 << 7)
#define WM8776_ALC1_LCSEL_ALCR		(1 << 7)
#define WM8776_ALC1_LCSEL_ALCL		(2 << 7)
#define WM8776_ALC1_LCSEL_ALCSTEREO	(3 << 7)
#define WM8776_REG_ALCCTRL2	0x11
#define WM8776_ALC2_HOLD_MASK		0x0f	/*0=0ms, 1=2.67ms, 2=5.33ms.. */
#define WM8776_ALC2_ZCEN		(1 << 7)
#define WM8776_ALC2_LCEN		(1 << 8)
#define WM8776_REG_ALCCTRL3	0x12
#define WM8776_ALC3_ATK_MASK		0x0f
#define WM8776_ALC3_DCY_MASK		0xf0
#define WM8776_ALC3_FDECAY		(1 << 8)
#define WM8776_REG_NOISEGATE	0x13
#define WM8776_NGAT_ENABLE		(1 << 0)
#define WM8776_NGAT_THR_MASK		0x1c	/*0=-78dB, 1=-72dB...7=-36dB */
#define WM8776_REG_LIMITER	0x14
#define WM8776_LIM_MAXATTEN_MASK	0x0f
#define WM8776_LIM_TRANWIN_MASK		0x70	/*0=0us, 1=62.5us, 2=125us.. */
#define WM8776_REG_ADCMUX	0x15
#define WM8776_ADC_MUX_AIN1		(1 << 0)
#define WM8776_ADC_MUX_AIN2		(1 << 1)
#define WM8776_ADC_MUX_AIN3		(1 << 2)
#define WM8776_ADC_MUX_AIN4		(1 << 3)
#define WM8776_ADC_MUX_AIN5		(1 << 4)
#define WM8776_ADC_MUTER		(1 << 6)
#define WM8776_ADC_MUTEL		(1 << 7)
#define WM8776_ADC_LRBOTH		(1 << 8)
#define WM8776_REG_OUTMUX	0x16
#define WM8776_OUTMUX_DAC		(1 << 0)
#define WM8776_OUTMUX_AUX		(1 << 1)
#define WM8776_OUTMUX_BYPASS		(1 << 2)
#define WM8776_REG_RESET	0x17

#define WM8776_REG_COUNT	0x17	/* don't cache the RESET register */

struct snd_wm8776;

struct snd_wm8776_ops {
	void (*write)(struct snd_wm8776 *wm, u8 addr, u8 data);
};

enum snd_wm8776_ctl_id {
	WM8776_CTL_DAC_VOL,
	WM8776_CTL_DAC_SW,
	WM8776_CTL_DAC_ZC_SW,
	WM8776_CTL_HP_VOL,
	WM8776_CTL_HP_SW,
	WM8776_CTL_HP_ZC_SW,
	WM8776_CTL_AUX_SW,
	WM8776_CTL_BYPASS_SW,
	WM8776_CTL_DAC_IZD_SW,
	WM8776_CTL_PHASE_SW,
	WM8776_CTL_DEEMPH_SW,
	WM8776_CTL_ADC_VOL,
	WM8776_CTL_ADC_SW,
	WM8776_CTL_INPUT1_SW,
	WM8776_CTL_INPUT2_SW,
	WM8776_CTL_INPUT3_SW,
	WM8776_CTL_INPUT4_SW,
	WM8776_CTL_INPUT5_SW,
	WM8776_CTL_AGC_SEL,
	WM8776_CTL_LIM_THR,
	WM8776_CTL_LIM_ATK,
	WM8776_CTL_LIM_DCY,
	WM8776_CTL_LIM_TRANWIN,
	WM8776_CTL_LIM_MAXATTN,
	WM8776_CTL_ALC_TGT,
	WM8776_CTL_ALC_ATK,
	WM8776_CTL_ALC_DCY,
	WM8776_CTL_ALC_MAXGAIN,
	WM8776_CTL_ALC_MAXATTN,
	WM8776_CTL_ALC_HLD,
	WM8776_CTL_NGT_SW,
	WM8776_CTL_NGT_THR,

	WM8776_CTL_COUNT,
};

#define WM8776_ENUM_MAX		16

#define WM8776_FLAG_STEREO	(1 << 0)
#define WM8776_FLAG_VOL_UPDATE	(1 << 1)
#define WM8776_FLAG_INVERT	(1 << 2)
#define WM8776_FLAG_LIM		(1 << 3)
#define WM8776_FLAG_ALC		(1 << 4)

struct snd_wm8776_ctl {
	const char *name;
	snd_ctl_elem_type_t type;
	const char *const enum_names[WM8776_ENUM_MAX];
	const unsigned int *tlv;
	u16 reg1, reg2, mask1, mask2, min, max, flags;
	void (*set)(struct snd_wm8776 *wm, u16 ch1, u16 ch2);
	void (*get)(struct snd_wm8776 *wm, u16 *ch1, u16 *ch2);
};

enum snd_wm8776_agc_mode {
	WM8776_AGC_OFF,
	WM8776_AGC_LIM,
	WM8776_AGC_ALC_R,
	WM8776_AGC_ALC_L,
	WM8776_AGC_ALC_STEREO
};

struct snd_wm8776 {
	struct snd_card *card;
	struct snd_wm8776_ctl ctl[WM8776_CTL_COUNT];
	enum snd_wm8776_agc_mode agc_mode;
	struct snd_wm8776_ops ops;
	u16 regs[WM8776_REG_COUNT];	/* 9-bit registers */
};



void snd_wm8776_init(struct snd_wm8776 *wm);
void snd_wm8776_resume(struct snd_wm8776 *wm);
void snd_wm8776_set_power(struct snd_wm8776 *wm, u16 power);
void snd_wm8776_volume_restore(struct snd_wm8776 *wm);
int snd_wm8776_build_controls(struct snd_wm8776 *wm);

#endif /* __SOUND_WM8776_H */
