/*
 *   ALSA driver for ICEnsemble VT17xx
 *
 *   Lowlevel functions for WM8766 codec
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
#include "wm8766.h"

/* low-level access */

static void snd_wm8766_write(struct snd_wm8766 *wm, u16 addr, u16 data)
{
	if (addr < WM8766_REG_COUNT)
		wm->regs[addr] = data;
	wm->ops.write(wm, addr, data);
}

/* mixer controls */

static const DECLARE_TLV_DB_SCALE(wm8766_tlv, -12750, 50, 1);

static struct snd_wm8766_ctl snd_wm8766_default_ctl[WM8766_CTL_COUNT] = {
	[WM8766_CTL_CH1_VOL] = {
		.name = "Channel 1 Playback Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8766_tlv,
		.reg1 = WM8766_REG_DACL1,
		.reg2 = WM8766_REG_DACR1,
		.mask1 = WM8766_VOL_MASK,
		.mask2 = WM8766_VOL_MASK,
		.max = 0xff,
		.flags = WM8766_FLAG_STEREO | WM8766_FLAG_VOL_UPDATE,
	},
	[WM8766_CTL_CH2_VOL] = {
		.name = "Channel 2 Playback Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8766_tlv,
		.reg1 = WM8766_REG_DACL2,
		.reg2 = WM8766_REG_DACR2,
		.mask1 = WM8766_VOL_MASK,
		.mask2 = WM8766_VOL_MASK,
		.max = 0xff,
		.flags = WM8766_FLAG_STEREO | WM8766_FLAG_VOL_UPDATE,
	},
	[WM8766_CTL_CH3_VOL] = {
		.name = "Channel 3 Playback Volume",
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.tlv = wm8766_tlv,
		.reg1 = WM8766_REG_DACL3,
		.reg2 = WM8766_REG_DACR3,
		.mask1 = WM8766_VOL_MASK,
		.mask2 = WM8766_VOL_MASK,
		.max = 0xff,
		.flags = WM8766_FLAG_STEREO | WM8766_FLAG_VOL_UPDATE,
	},
	[WM8766_CTL_CH1_SW] = {
		.name = "Channel 1 Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_MUTE1,
		.flags = WM8766_FLAG_INVERT,
	},
	[WM8766_CTL_CH2_SW] = {
		.name = "Channel 2 Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_MUTE2,
		.flags = WM8766_FLAG_INVERT,
	},
	[WM8766_CTL_CH3_SW] = {
		.name = "Channel 3 Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_MUTE3,
		.flags = WM8766_FLAG_INVERT,
	},
	[WM8766_CTL_PHASE1_SW] = {
		.name = "Channel 1 Phase Invert Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_IFCTRL,
		.mask1 = WM8766_PHASE_INVERT1,
	},
	[WM8766_CTL_PHASE2_SW] = {
		.name = "Channel 2 Phase Invert Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_IFCTRL,
		.mask1 = WM8766_PHASE_INVERT2,
	},
	[WM8766_CTL_PHASE3_SW] = {
		.name = "Channel 3 Phase Invert Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_IFCTRL,
		.mask1 = WM8766_PHASE_INVERT3,
	},
	[WM8766_CTL_DEEMPH1_SW] = {
		.name = "Channel 1 Deemphasis Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_DEEMP1,
	},
	[WM8766_CTL_DEEMPH2_SW] = {
		.name = "Channel 2 Deemphasis Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_DEEMP2,
	},
	[WM8766_CTL_DEEMPH3_SW] = {
		.name = "Channel 3 Deemphasis Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_DEEMP3,
	},
	[WM8766_CTL_IZD_SW] = {
		.name = "Infinite Zero Detect Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL1,
		.mask1 = WM8766_DAC_IZD,
	},
	[WM8766_CTL_ZC_SW] = {
		.name = "Zero Cross Detect Playback Switch",
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.reg1 = WM8766_REG_DACCTRL2,
		.mask1 = WM8766_DAC2_ZCD,
		.flags = WM8766_FLAG_INVERT,
	},
};

/* exported functions */

void snd_wm8766_init(struct snd_wm8766 *wm)
{
	int i;
	static const u16 default_values[] = {
		0x000, 0x100,
		0x120, 0x000,
		0x000, 0x100, 0x000, 0x100, 0x000,
		0x000, 0x080,
	};

	memcpy(wm->ctl, snd_wm8766_default_ctl, sizeof(wm->ctl));

	snd_wm8766_write(wm, WM8766_REG_RESET, 0x00); /* reset */
	udelay(10);
	/* load defaults */
	for (i = 0; i < ARRAY_SIZE(default_values); i++)
		snd_wm8766_write(wm, i, default_values[i]);
}

void snd_wm8766_resume(struct snd_wm8766 *wm)
{
	int i;

	for (i = 0; i < WM8766_REG_COUNT; i++)
		snd_wm8766_write(wm, i, wm->regs[i]);
}

void snd_wm8766_set_if(struct snd_wm8766 *wm, u16 dac)
{
	u16 val = wm->regs[WM8766_REG_IFCTRL] & ~WM8766_IF_MASK;

	dac &= WM8766_IF_MASK;
	snd_wm8766_write(wm, WM8766_REG_IFCTRL, val | dac);
}

void snd_wm8766_volume_restore(struct snd_wm8766 *wm)
{
	u16 val = wm->regs[WM8766_REG_DACR1];
	/* restore volume after MCLK stopped */
	snd_wm8766_write(wm, WM8766_REG_DACR1, val | WM8766_VOL_UPDATE);
}

/* mixer callbacks */

static int snd_wm8766_volume_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct snd_wm8766 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = (wm->ctl[n].flags & WM8766_FLAG_STEREO) ? 2 : 1;
	uinfo->value.integer.min = wm->ctl[n].min;
	uinfo->value.integer.max = wm->ctl[n].max;

	return 0;
}

static int snd_wm8766_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct snd_wm8766 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;

	return snd_ctl_enum_info(uinfo, 1, wm->ctl[n].max,
						wm->ctl[n].enum_names);
}

static int snd_wm8766_ctl_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_wm8766 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;
	u16 val1, val2;

	if (wm->ctl[n].get)
		wm->ctl[n].get(wm, &val1, &val2);
	else {
		val1 = wm->regs[wm->ctl[n].reg1] & wm->ctl[n].mask1;
		val1 >>= __ffs(wm->ctl[n].mask1);
		if (wm->ctl[n].flags & WM8766_FLAG_STEREO) {
			val2 = wm->regs[wm->ctl[n].reg2] & wm->ctl[n].mask2;
			val2 >>= __ffs(wm->ctl[n].mask2);
			if (wm->ctl[n].flags & WM8766_FLAG_VOL_UPDATE)
				val2 &= ~WM8766_VOL_UPDATE;
		}
	}
	if (wm->ctl[n].flags & WM8766_FLAG_INVERT) {
		val1 = wm->ctl[n].max - (val1 - wm->ctl[n].min);
		if (wm->ctl[n].flags & WM8766_FLAG_STEREO)
			val2 = wm->ctl[n].max - (val2 - wm->ctl[n].min);
	}
	ucontrol->value.integer.value[0] = val1;
	if (wm->ctl[n].flags & WM8766_FLAG_STEREO)
		ucontrol->value.integer.value[1] = val2;

	return 0;
}

static int snd_wm8766_ctl_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_wm8766 *wm = snd_kcontrol_chip(kcontrol);
	int n = kcontrol->private_value;
	u16 val, regval1, regval2;

	/* this also works for enum because value is an union */
	regval1 = ucontrol->value.integer.value[0];
	regval2 = ucontrol->value.integer.value[1];
	if (wm->ctl[n].flags & WM8766_FLAG_INVERT) {
		regval1 = wm->ctl[n].max - (regval1 - wm->ctl[n].min);
		regval2 = wm->ctl[n].max - (regval2 - wm->ctl[n].min);
	}
	if (wm->ctl[n].set)
		wm->ctl[n].set(wm, regval1, regval2);
	else {
		val = wm->regs[wm->ctl[n].reg1] & ~wm->ctl[n].mask1;
		val |= regval1 << __ffs(wm->ctl[n].mask1);
		/* both stereo controls in one register */
		if (wm->ctl[n].flags & WM8766_FLAG_STEREO &&
				wm->ctl[n].reg1 == wm->ctl[n].reg2) {
			val &= ~wm->ctl[n].mask2;
			val |= regval2 << __ffs(wm->ctl[n].mask2);
		}
		snd_wm8766_write(wm, wm->ctl[n].reg1, val);
		/* stereo controls in different registers */
		if (wm->ctl[n].flags & WM8766_FLAG_STEREO &&
				wm->ctl[n].reg1 != wm->ctl[n].reg2) {
			val = wm->regs[wm->ctl[n].reg2] & ~wm->ctl[n].mask2;
			val |= regval2 << __ffs(wm->ctl[n].mask2);
			if (wm->ctl[n].flags & WM8766_FLAG_VOL_UPDATE)
				val |= WM8766_VOL_UPDATE;
			snd_wm8766_write(wm, wm->ctl[n].reg2, val);
		}
	}

	return 0;
}

static int snd_wm8766_add_control(struct snd_wm8766 *wm, int num)
{
	struct snd_kcontrol_new cont;
	struct snd_kcontrol *ctl;

	memset(&cont, 0, sizeof(cont));
	cont.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	cont.private_value = num;
	cont.name = wm->ctl[num].name;
	cont.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	if (wm->ctl[num].flags & WM8766_FLAG_LIM ||
	    wm->ctl[num].flags & WM8766_FLAG_ALC)
		cont.access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	cont.tlv.p = NULL;
	cont.get = snd_wm8766_ctl_get;
	cont.put = snd_wm8766_ctl_put;

	switch (wm->ctl[num].type) {
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		cont.info = snd_wm8766_volume_info;
		cont.access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		cont.tlv.p = wm->ctl[num].tlv;
		break;
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		wm->ctl[num].max = 1;
		if (wm->ctl[num].flags & WM8766_FLAG_STEREO)
			cont.info = snd_ctl_boolean_stereo_info;
		else
			cont.info = snd_ctl_boolean_mono_info;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		cont.info = snd_wm8766_enum_info;
		break;
	default:
		return -EINVAL;
	}
	ctl = snd_ctl_new1(&cont, wm);
	if (!ctl)
		return -ENOMEM;
	wm->ctl[num].kctl = ctl;

	return snd_ctl_add(wm->card, ctl);
}

int snd_wm8766_build_controls(struct snd_wm8766 *wm)
{
	int err, i;

	for (i = 0; i < WM8766_CTL_COUNT; i++)
		if (wm->ctl[i].name) {
			err = snd_wm8766_add_control(wm, i);
			if (err < 0)
				return err;
		}

	return 0;
}
