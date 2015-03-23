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

#include <linux/delay.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include "wm8776.h"

/* low-level access */

static void snd_wm8776_write(struct snd_wm8776 *wm, u16 addr, u16 data)
{
	u8 bus_addr = addr << 1 | data >> 8;	/* addr + 9th data bit */
	u8 bus_data = data & 0xff;		/* remaining 8 data bits */

	if (addr < WM8776_REG_RESET)
		wm->regs[addr] = data;
	wm->ops.write(wm, bus_addr, bus_data);
}

/* register-level functions */

static void snd_wm8776_activate_ctl(struct snd_wm8776 *wm,
				    const char *ctl_name,
				    bool active)
{
	struct snd_card *card = wm->card;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	struct snd_ctl_elem_id elem_id;
	unsigned int index_offset;

	memset(&elem_id, 0, sizeof(elem_id));
	strlcpy(elem_id.name, ctl_name, sizeof(elem_id.name));
	elem_id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kctl = snd_ctl_find_id(card, &elem_id);
	if (!kctl)
		return;
	index_offset = snd_ctl_get_ioff(kctl, &kctl->id);
	vd = &kctl->vd[index_offset];
	if (active)
		vd->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	else
		vd->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO, &kctl->id);
}

static void snd_wm8776_update_agc_ctl(struct snd_wm8776 *wm)
{
	int i, flags_on = 0, flags_off = 0;

	switch (wm->agc_mode) {
	case WM8776_AGC_OFF:
		flags_off = WM8776_FLAG_LIM | WM8776_FLAG_ALC;
		break;
	case WM8776_AGC_LIM:
		flags_off = WM8776_FLAG_ALC;
		flags_on = WM8776_FLAG_LIM;
		break;
	case WM8776_AGC_ALC_R:
	case WM8776_AGC_ALC_L:
	case WM8776_AGC_ALC_STEREO:
		flags_off = WM8776_FLAG_LIM;
		flags_on = WM8776_FLAG_ALC;
		break;
	}

	for (i = 0; i < WM8776_CTL_COUNT; i++)
		if (wm->ctl[i].flags & flags_off)
			snd_wm8776_activate_ctl(wm, wm->ctl[i].name, false);
		else if (wm->ctl[i].flags & flags_on)
			snd_wm8776_activate_ctl(wm, wm->ctl[i].name, true);
}

static void snd_wm8776_set_agc(struct snd_wm8776 *wm, u16 agc, u16 nothing)
{
	u16 alc1 = wm->regs[WM8776_REG_ALCCTRL1] & ~WM8776_ALC1_LCT_MASK;
	u16 alc2 = wm->regs[WM8776_REG_ALCCTRL2] & ~WM8776_ALC2_LCEN;

	switch (agc) {
	case 0:	/* Off */
		wm->agc_mode = WM8776_AGC_OFF;
		break;
	case 1: /* Limiter */
		alc2 |= WM8776_ALC2_LCEN;
		wm->agc_mode = WM8776_AGC_LIM;
		break;
	case 2: /* ALC Right */
		alc1 |= WM8776_ALC1_LCSEL_ALCR;
		alc2 |= WM8776_ALC2_LCEN;
		wm->agc_mode = WM8776_AGC_ALC_R;
		break;
	case 3: /* ALC Left */
		alc1 |= WM8776_ALC1_LCSEL_ALCL;
		alc2 |= WM8776_ALC2_LCEN;
		wm->agc_mode = WM8776_AGC_ALC_L;
		break;
	case 4: /* ALC Stereo */
		alc1 |= WM8776_ALC1_LCSEL_ALCSTEREO;
		alc2 |= WM8776_ALC2_LCEN;
		wm->agc_mode = WM8776_AGC_ALC_STEREO;
		break;
	}
	snd_wm8776_write(wm, WM8776_REG_ALCCTRL1, alc1);
	snd_wm8776_write(wm, WM8776_REG_ALCCTRL2, alc2);
	snd_wm8776_update_agc_ctl(wm);
}

static void snd_wm8776_get_agc(struct snd_wm8776 *wm, u16 *mode, u16 *nothing)
{
	*mode = wm->agc_mode;
}

/* mixer controls */

static const DECLARE_TLV_DB_SCALE(wm8776_hp_tlv, -7400, 100, 1);
static const DECLARE_TLV_DB_SCALE(wm8776_dac_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(wm8776_adc_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(wm8776_lct_tlv, -1600, 100, 0);
static const DECLARE_TLV_DB_SCALE(wm8776_maxgain_tlv, 0, 400, 0);
static const DECLARE_TLV_DB_SCALE(wm8776_ngth_tlv, -7800, 600, 0);
static const DECLARE_TLV_DB_SCALE(wm8776_maxatten_lim_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(wm8776_maxatten_alc_tlv, -2100, 400, 0);

static struct snd_wm8776_ctl snd_wm8776_default_ctl[WM8776_CTL_COUNT] = {
	[WM8776_CTL_DAC_VOL] = {
		.name = "Master Playback Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_dac_tlv,
		.reg1 = WM8776_REG_DACLVOL,
		.reg2 = WM8776_REG_DACRVOL,
		.mask1 = WM8776_DACVOL_MASK,
		.mask2 = WM8776_DACVOL_MASK,
		.max = 0xff,
		.flags = WM8776_FLAG_STEREO | WM8776_FLAG_VOL_UPDATE,
	},
	[WM8776_CTL_DAC_SW] = {
		.name = "Master Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_DACCTRL1,
		.reg2 = WM8776_REG_DACCTRL1,
		.mask1 = WM8776_DAC_PL_LL,
		.mask2 = WM8776_DAC_PL_RR,
		.flags = WM8776_FLAG_STEREO,
	},
	[WM8776_CTL_DAC_ZC_SW] = {
		.name = "Master Zero Cross Detect Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_DACCTRL1,
		.mask1 = WM8776_DAC_DZCEN,
	},
	[WM8776_CTL_HP_VOL] = {
		.name = "Headphone Playback Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_hp_tlv,
		.reg1 = WM8776_REG_HPLVOL,
		.reg2 = WM8776_REG_HPRVOL,
		.mask1 = WM8776_HPVOL_MASK,
		.mask2 = WM8776_HPVOL_MASK,
		.min = 0x2f,
		.max = 0x7f,
		.flags = WM8776_FLAG_STEREO | WM8776_FLAG_VOL_UPDATE,
	},
	[WM8776_CTL_HP_SW] = {
		.name = "Headphone Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_PWRDOWN,
		.mask1 = WM8776_PWR_HPPD,
		.flags = WM8776_FLAG_INVERT,
	},
	[WM8776_CTL_HP_ZC_SW] = {
		.name = "Headphone Zero Cross Detect Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_HPLVOL,
		.reg2 = WM8776_REG_HPRVOL,
		.mask1 = WM8776_VOL_HPZCEN,
		.mask2 = WM8776_VOL_HPZCEN,
		.flags = WM8776_FLAG_STEREO,
	},
	[WM8776_CTL_AUX_SW] = {
		.name = "AUX Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_OUTMUX,
		.mask1 = WM8776_OUTMUX_AUX,
	},
	[WM8776_CTL_BYPASS_SW] = {
		.name = "Bypass Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_OUTMUX,
		.mask1 = WM8776_OUTMUX_BYPASS,
	},
	[WM8776_CTL_DAC_IZD_SW] = {
		.name = "Infinite Zero Detect Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_DACCTRL1,
		.mask1 = WM8776_DAC_IZD,
	},
	[WM8776_CTL_PHASE_SW] = {
		.name = "Phase Invert Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_PHASESWAP,
		.reg2 = WM8776_REG_PHASESWAP,
		.mask1 = WM8776_PHASE_INVERTL,
		.mask2 = WM8776_PHASE_INVERTR,
		.flags = WM8776_FLAG_STEREO,
	},
	[WM8776_CTL_DEEMPH_SW] = {
		.name = "Deemphasis Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_DACCTRL2,
		.mask1 = WM8776_DAC2_DEEMPH,
	},
	[WM8776_CTL_ADC_VOL] = {
		.name = "Input Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_adc_tlv,
		.reg1 = WM8776_REG_ADCLVOL,
		.reg2 = WM8776_REG_ADCRVOL,
		.mask1 = WM8776_ADC_GAIN_MASK,
		.mask2 = WM8776_ADC_GAIN_MASK,
		.max = 0xff,
		.flags = WM8776_FLAG_STEREO | WM8776_FLAG_VOL_UPDATE,
	},
	[WM8776_CTL_ADC_SW] = {
		.name = "Input Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.reg2 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUTEL,
		.mask2 = WM8776_ADC_MUTER,
		.flags = WM8776_FLAG_STEREO | WM8776_FLAG_INVERT,
	},
	[WM8776_CTL_INPUT1_SW] = {
		.name = "AIN1 Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUX_AIN1,
	},
	[WM8776_CTL_INPUT2_SW] = {
		.name = "AIN2 Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUX_AIN2,
	},
	[WM8776_CTL_INPUT3_SW] = {
		.name = "AIN3 Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUX_AIN3,
	},
	[WM8776_CTL_INPUT4_SW] = {
		.name = "AIN4 Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUX_AIN4,
	},
	[WM8776_CTL_INPUT5_SW] = {
		.name = "AIN5 Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_ADCMUX,
		.mask1 = WM8776_ADC_MUX_AIN5,
	},
	[WM8776_CTL_AGC_SEL] = {
		.name = "AGC Select Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = { "Off", "Limiter", "ALC Right", "ALC Left",
				"ALC Stereo" },
		.max = 5,	/* .enum_names item count */
		.set = snd_wm8776_set_agc,
		.get = snd_wm8776_get_agc,
	},
	[WM8776_CTL_LIM_THR] = {
		.name = "Limiter Threshold Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_lct_tlv,
		.reg1 = WM8776_REG_ALCCTRL1,
		.mask1 = WM8776_ALC1_LCT_MASK,
		.max = 15,
		.flags = WM8776_FLAG_LIM,
	},
	[WM8776_CTL_LIM_ATK] = {
		.name = "Limiter Attack Time Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = { "0.25 ms", "0.5 ms", "1 ms", "2 ms", "4 ms",
			"8 ms", "16 ms", "32 ms", "64 ms", "128 ms", "256 ms" },
		.max = 11,	/* .enum_names item count */
		.reg1 = WM8776_REG_ALCCTRL3,
		.mask1 = WM8776_ALC3_ATK_MASK,
		.flags = WM8776_FLAG_LIM,
	},
	[WM8776_CTL_LIM_DCY] = {
		.name = "Limiter Decay Time Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = {	"1.2 ms", "2.4 ms", "4.8 ms", "9.6 ms",
			"19.2 ms", "38.4 ms", "76.8 ms", "154 ms", "307 ms",
			"614 ms", "1.23 s" },
		.max = 11,	/* .enum_names item count */
		.reg1 = WM8776_REG_ALCCTRL3,
		.mask1 = WM8776_ALC3_DCY_MASK,
		.flags = WM8776_FLAG_LIM,
	},
	[WM8776_CTL_LIM_TRANWIN] = {
		.name = "Limiter Transient Window Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = {	"0 us", "62.5 us", "125 us", "250 us", "500 us",
			"1 ms", "2 ms", "4 ms" },
		.max = 8,	/* .enum_names item count */
		.reg1 = WM8776_REG_LIMITER,
		.mask1 = WM8776_LIM_TRANWIN_MASK,
		.flags = WM8776_FLAG_LIM,
	},
	[WM8776_CTL_LIM_MAXATTN] = {
		.name = "Limiter Maximum Attenuation Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_maxatten_lim_tlv,
		.reg1 = WM8776_REG_LIMITER,
		.mask1 = WM8776_LIM_MAXATTEN_MASK,
		.min = 3,
		.max = 12,
		.flags = WM8776_FLAG_LIM | WM8776_FLAG_INVERT,
	},
	[WM8776_CTL_ALC_TGT] = {
		.name = "ALC Target Level Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_lct_tlv,
		.reg1 = WM8776_REG_ALCCTRL1,
		.mask1 = WM8776_ALC1_LCT_MASK,
		.max = 15,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_ALC_ATK] = {
		.name = "ALC Attack Time Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = { "8.40 ms", "16.8 ms", "33.6 ms", "67.2 ms",
			"134 ms", "269 ms", "538 ms", "1.08 s",	"2.15 s",
			"4.3 s", "8.6 s" },
		.max = 11,	/* .enum_names item count */
		.reg1 = WM8776_REG_ALCCTRL3,
		.mask1 = WM8776_ALC3_ATK_MASK,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_ALC_DCY] = {
		.name = "ALC Decay Time Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = {	"33.5 ms", "67.0 ms", "134 ms", "268 ms",
			"536 ms", "1.07 s", "2.14 s", "4.29 s",	"8.58 s",
			"17.2 s", "34.3 s" },
		.max = 11,	/* .enum_names item count */
		.reg1 = WM8776_REG_ALCCTRL3,
		.mask1 = WM8776_ALC3_DCY_MASK,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_ALC_MAXGAIN] = {
		.name = "ALC Maximum Gain Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_maxgain_tlv,
		.reg1 = WM8776_REG_ALCCTRL1,
		.mask1 = WM8776_ALC1_MAXGAIN_MASK,
		.min = 1,
		.max = 7,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_ALC_MAXATTN] = {
		.name = "ALC Maximum Attenuation Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_maxatten_alc_tlv,
		.reg1 = WM8776_REG_LIMITER,
		.mask1 = WM8776_LIM_MAXATTEN_MASK,
		.min = 10,
		.max = 15,
		.flags = WM8776_FLAG_ALC | WM8776_FLAG_INVERT,
	},
	[WM8776_CTL_ALC_HLD] = {
		.name = "ALC Hold Time Capture Enum",
		.type = SNDRV_CTL_ELEM_TYPE_ENUMERATED,
		.enum_names = {	"0 ms", "2.67 ms", "5.33 ms", "10.6 ms",
			"21.3 ms", "42.7 ms", "85.3 ms", "171 ms", "341 ms",
			"683 ms", "1.37 s", "2.73 s", "5.46 s", "10.9 s",
			"21.8 s", "43.7 s" },
		.max = 16,	/* .enum_names item count */
		.reg1 = WM8776_REG_ALCCTRL2,
		.mask1 = WM8776_ALC2_HOLD_MASK,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_NGT_SW] = {
		.name = "Noise Gate Capture Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8776_REG_NOISEGATE,
		.mask1 = WM8776_NGAT_ENABLE,
		.flags = WM8776_FLAG_ALC,
	},
	[WM8776_CTL_NGT_THR] = {
		.name = "Noise Gate Threshold Capture Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8776_ngth_tlv,
		.reg1 = WM8776_REG_NOISEGATE,
		.mask1 = WM8776_NGAT_THR_MASK,
		.max = 7,
		.flags = WM8776_FLAG_ALC,
	},
};

/* exported functions */

void snd_wm8776_init(struct snd_wm8776 *wm)
{
	int i;
	static const u16 default_values[] = {
		0x000, 0x100, 0x000,
		0x000, 0x100, 0x000,
		0x000, 0x090, 0x000, 0x000,
		0x022, 0x022, 0x022,
		0x008, 0x0cf, 0x0cf, 0x07b, 0x000,
		0x032, 0x000, 0x0a6, 0x001, 0x001
	};

	memcpy(wm->ctl, snd_wm8776_default_ctl, sizeof(wm->ctl));

	snd_wm8776_write(wm, WM8776_REG_RESET, 0x00); /* reset */
	udelay(10);
	/* load defaults */
	for (i = 0; i < ARRAY_SIZE(default_values); i++)
		snd_wm8776_write(wm, i, default_values[i]);
}

void snd_wm8776_resume(struct snd_wm8776 *wm)
{
	int i;

	for (i = 0; i < WM8776_REG_COUNT; i++)
		snd_wm8776_write(wm, i, wm->regs[i]);
}

void snd_wm8776_set_power(struct snd_wm8776 *wm, u16 power)
{
	snd_wm8776_write(wm, WM8776_REG_PWRDOWN, power);
}

void snd_wm8776_volume_restore(struct snd_wm8776 *wm)
{
	u16 val = wm->regs[WM8776_REG_DACRVOL];
	/* restore volume after MCLK stopped */
	snd_wm8776_write(wm, WM8776_REG_DACRVOL, val | WM8776_VOL_UPDATE);
}

/* mixer callbacks */

static int snd_wm8776_volume_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct snd_wm8776 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = (wm->ctl[n].flags & WM8776_FLAG_STEREO) ? 2 : 1;
	uinfo->value.integer.min = wm->ctl[n].min;
	uinfo->value.integer.max = wm->ctl[n].max;

	return 0;
}

static int snd_wm8776_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct snd_wm8776 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;

	return snd_ctl_enum_info(uinfo, 1, wm->ctl[n].max,
						wm->ctl[n].enum_names);
}

static int snd_wm8776_ctl_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_wm8776 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;
	u16 val1, val2;

	if (wm->ctl[n].get)
		wm->ctl[n].get(wm, &val1, &val2);
	else {
		val1 = wm->regs[wm->ctl[n].reg1] & wm->ctl[n].mask1;
		val1 >>= __ffs(wm->ctl[n].mask1);
		if (wm->ctl[n].flags & WM8776_FLAG_STEREO) {
			val2 = wm->regs[wm->ctl[n].reg2] & wm->ctl[n].mask2;
			val2 >>= __ffs(wm->ctl[n].mask2);
			if (wm->ctl[n].flags & WM8776_FLAG_VOL_UPDATE)
				val2 &= ~WM8776_VOL_UPDATE;
		}
	}
	if (wm->ctl[n].flags & WM8776_FLAG_INVERT) {
		val1 = wm->ctl[n].max - (val1 - wm->ctl[n].min);
		if (wm->ctl[n].flags & WM8776_FLAG_STEREO)
			val2 = wm->ctl[n].max - (val2 - wm->ctl[n].min);
	}
	ucontrol->value.integer.value[0] = val1;
	if (wm->ctl[n].flags & WM8776_FLAG_STEREO)
		ucontrol->value.integer.value[1] = val2;

	return 0;
}

static int snd_wm8776_ctl_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_wm8776 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;
	u16 val, regval1, regval2;

	/* this also works for enum because value is an union */
	regval1 = ucontrol->value.integer.value[0];
	regval2 = ucontrol->value.integer.value[1];
	if (wm->ctl[n].flags & WM8776_FLAG_INVERT) {
		regval1 = wm->ctl[n].max - (regval1 - wm->ctl[n].min);
		regval2 = wm->ctl[n].max - (regval2 - wm->ctl[n].min);
	}
	if (wm->ctl[n].set)
		wm->ctl[n].set(wm, regval1, regval2);
	else {
		val = wm->regs[wm->ctl[n].reg1] & ~wm->ctl[n].mask1;
		val |= regval1 << __ffs(wm->ctl[n].mask1);
		/* both stereo controls in one register */
		if (wm->ctl[n].flags & WM8776_FLAG_STEREO &&
				wm->ctl[n].reg1 == wm->ctl[n].reg2) {
			val &= ~wm->ctl[n].mask2;
			val |= regval2 << __ffs(wm->ctl[n].mask2);
		}
		snd_wm8776_write(wm, wm->ctl[n].reg1, val);
		/* stereo controls in different registers */
		if (wm->ctl[n].flags & WM8776_FLAG_STEREO &&
				wm->ctl[n].reg1 != wm->ctl[n].reg2) {
			val = wm->regs[wm->ctl[n].reg2] & ~wm->ctl[n].mask2;
			val |= regval2 << __ffs(wm->ctl[n].mask2);
			if (wm->ctl[n].flags & WM8776_FLAG_VOL_UPDATE)
				val |= WM8776_VOL_UPDATE;
			snd_wm8776_write(wm, wm->ctl[n].reg2, val);
		}
	}

	return 0;
}

static int snd_wm8776_add_control(struct snd_wm8776 *wm, int num)
{
	struct snd_kcontrol_new cont;
	struct snd_kcontrol *ctl;

	memset(&cont, 0, sizeof(cont));
	cont.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	cont.private_value = num;
	cont.name = wm->ctl[num].name;
	cont.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	if (wm->ctl[num].flags & WM8776_FLAG_LIM ||
	    wm->ctl[num].flags & WM8776_FLAG_ALC)
		cont.access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	cont.tlv.p = NULL;
	cont.get = snd_wm8776_ctl_get;
	cont.put = snd_wm8776_ctl_put;

	switch (wm->ctl[num].type) {
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		cont.info = snd_wm8776_volume_info;
		cont.access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		cont.tlv.p = wm->ctl[num].tlv;
		break;
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		wm->ctl[num].max = 1;
		if (wm->ctl[num].flags & WM8776_FLAG_STEREO)
			cont.info = snd_ctl_boolean_stereo_info;
		else
			cont.info = snd_ctl_boolean_mono_info;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		cont.info = snd_wm8776_enum_info;
		break;
	default:
		return -EINVAL;
	}
	ctl = snd_ctl_new1(&cont, wm);
	if (!ctl)
		return -ENOMEM;

	return snd_ctl_add(wm->card, ctl);
}

int snd_wm8776_build_controls(struct snd_wm8776 *wm)
{
	int err, i;

	for (i = 0; i < WM8776_CTL_COUNT; i++)
		if (wm->ctl[i].name) {
			err = snd_wm8776_add_control(wm, i);
			if (err < 0)
				return err;
		}

	return 0;
}
