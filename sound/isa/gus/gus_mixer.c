/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of ICS 2101 chip and "mixer" in GF1 chip
 *
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

#include <linux/time.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/gus.h>

/*
 *
 */

#define GF1_SINGLE(xname, xindex, shift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_gf1_info_single, \
  .get = snd_gf1_get_single, .put = snd_gf1_put_single, \
  .private_value = shift | (invert << 8) }

#define snd_gf1_info_single	snd_ctl_boolean_mono_info

static int snd_gf1_get_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	
	ucontrol->value.integer.value[0] = (gus->mix_cntrl_reg >> shift) & 1;
	if (invert)
		ucontrol->value.integer.value[0] ^= 1;
	return 0;
}

static int snd_gf1_put_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	int change;
	unsigned char oval, nval;
	
	nval = ucontrol->value.integer.value[0] & 1;
	if (invert)
		nval ^= 1;
	nval <<= shift;
	spin_lock_irqsave(&gus->reg_lock, flags);
	oval = gus->mix_cntrl_reg;
	nval = (oval & ~(1 << shift)) | nval;
	change = nval != oval;
	outb(gus->mix_cntrl_reg = nval, GUSP(gus, MIXCNTRLREG));
	outb(gus->gf1.active_voice = 0, GUSP(gus, GF1PAGE));
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return change;
}

#define ICS_DOUBLE(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_ics_info_double, \
  .get = snd_ics_get_double, .put = snd_ics_put_double, \
  .private_value = addr }

static int snd_ics_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_ics_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value & 0xff;
	unsigned char left, right;
	
	spin_lock_irqsave(&gus->reg_lock, flags);
	left = gus->gf1.ics_regs[addr][0];
	right = gus->gf1.ics_regs[addr][1];
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	ucontrol->value.integer.value[0] = left & 127;
	ucontrol->value.integer.value[1] = right & 127;
	return 0;
}

static int snd_ics_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value & 0xff;
	int change;
	unsigned char val1, val2, oval1, oval2, tmp;
	
	val1 = ucontrol->value.integer.value[0] & 127;
	val2 = ucontrol->value.integer.value[1] & 127;
	spin_lock_irqsave(&gus->reg_lock, flags);
	oval1 = gus->gf1.ics_regs[addr][0];
	oval2 = gus->gf1.ics_regs[addr][1];
	change = val1 != oval1 || val2 != oval2;
	gus->gf1.ics_regs[addr][0] = val1;
	gus->gf1.ics_regs[addr][1] = val2;
	if (gus->ics_flag && gus->ics_flipped &&
	    (addr == SNDRV_ICS_GF1_DEV || addr == SNDRV_ICS_MASTER_DEV)) {
		tmp = val1;
		val1 = val2;
		val2 = tmp;
	}
	addr <<= 3;
	outb(addr | 0, GUSP(gus, MIXCNTRLPORT));
	outb(1, GUSP(gus, MIXDATAPORT));
	outb(addr | 2, GUSP(gus, MIXCNTRLPORT));
	outb((unsigned char) val1, GUSP(gus, MIXDATAPORT));
	outb(addr | 1, GUSP(gus, MIXCNTRLPORT));
	outb(2, GUSP(gus, MIXDATAPORT));
	outb(addr | 3, GUSP(gus, MIXCNTRLPORT));
	outb((unsigned char) val2, GUSP(gus, MIXDATAPORT));
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return change;
}

static struct snd_kcontrol_new snd_gf1_controls[] = {
GF1_SINGLE("Master Playback Switch", 0, 1, 1),
GF1_SINGLE("Line Switch", 0, 0, 1),
GF1_SINGLE("Mic Switch", 0, 2, 0)
};

static struct snd_kcontrol_new snd_ics_controls[] = {
GF1_SINGLE("Master Playback Switch", 0, 1, 1),
ICS_DOUBLE("Master Playback Volume", 0, SNDRV_ICS_MASTER_DEV),
ICS_DOUBLE("Synth Playback Volume", 0, SNDRV_ICS_GF1_DEV),
GF1_SINGLE("Line Switch", 0, 0, 1),
ICS_DOUBLE("Line Playback Volume", 0, SNDRV_ICS_LINE_DEV),
GF1_SINGLE("Mic Switch", 0, 2, 0),
ICS_DOUBLE("Mic Playback Volume", 0, SNDRV_ICS_MIC_DEV),
ICS_DOUBLE("CD Playback Volume", 0, SNDRV_ICS_CD_DEV)
};

int snd_gf1_new_mixer(struct snd_gus_card * gus)
{
	struct snd_card *card;
	unsigned int idx, max;
	int err;

	snd_assert(gus != NULL, return -EINVAL);
	card = gus->card;
	snd_assert(card != NULL, return -EINVAL);

	if (gus->ics_flag)
		snd_component_add(card, "ICS2101");
	if (card->mixername[0] == '\0') {
		strcpy(card->mixername, gus->ics_flag ? "GF1,ICS2101" : "GF1");
	} else {
		if (gus->ics_flag)
			strcat(card->mixername, ",ICS2101");
		strcat(card->mixername, ",GF1");
	}

	if (!gus->ics_flag) {
		max = gus->ess_flag ? 1 : ARRAY_SIZE(snd_gf1_controls);
		for (idx = 0; idx < max; idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_gf1_controls[idx], gus))) < 0)
				return err;
		}
	} else {
		for (idx = 0; idx < ARRAY_SIZE(snd_ics_controls); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_ics_controls[idx], gus))) < 0)
				return err;
		}
	}
	return 0;
}
