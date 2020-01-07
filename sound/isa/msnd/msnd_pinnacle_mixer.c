// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
			  msnd_pinnacle_mixer.c  -  description
			     -------------------
    begin		: Fre Jun 7 2002
    copyright 		: (C) 2002 by karsten wiese
    email		: annabellesgarden@yahoo.de
 ***************************************************************************/

/***************************************************************************
 *							      		   *
 *									   *
 ***************************************************************************/

#include <linux/io.h>
#include <linux/export.h>

#include <sound/core.h>
#include <sound/control.h>
#include "msnd.h"
#include "msnd_pinnacle.h"


#define MSND_MIXER_VOLUME	0
#define MSND_MIXER_PCM		1
#define MSND_MIXER_AUX		2	/* Input source 1  (aux1) */
#define MSND_MIXER_IMIX		3	/*  Recording monitor  */
#define MSND_MIXER_SYNTH	4
#define MSND_MIXER_SPEAKER	5
#define MSND_MIXER_LINE		6
#define MSND_MIXER_MIC		7
#define MSND_MIXER_RECLEV	11	/* Recording level */
#define MSND_MIXER_IGAIN	12	/* Input gain */
#define MSND_MIXER_OGAIN	13	/* Output gain */
#define MSND_MIXER_DIGITAL	17	/* Digital (input) 1 */

/*	Device mask bits	*/

#define MSND_MASK_VOLUME	(1 << MSND_MIXER_VOLUME)
#define MSND_MASK_SYNTH		(1 << MSND_MIXER_SYNTH)
#define MSND_MASK_PCM		(1 << MSND_MIXER_PCM)
#define MSND_MASK_SPEAKER	(1 << MSND_MIXER_SPEAKER)
#define MSND_MASK_LINE		(1 << MSND_MIXER_LINE)
#define MSND_MASK_MIC		(1 << MSND_MIXER_MIC)
#define MSND_MASK_IMIX		(1 << MSND_MIXER_IMIX)
#define MSND_MASK_RECLEV	(1 << MSND_MIXER_RECLEV)
#define MSND_MASK_IGAIN		(1 << MSND_MIXER_IGAIN)
#define MSND_MASK_OGAIN		(1 << MSND_MIXER_OGAIN)
#define MSND_MASK_AUX		(1 << MSND_MIXER_AUX)
#define MSND_MASK_DIGITAL	(1 << MSND_MIXER_DIGITAL)

static int snd_msndmix_info_mux(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[3] = {
		"Analog", "MASS", "SPDIF",
	};
	struct snd_msnd *chip = snd_kcontrol_chip(kcontrol);
	unsigned items = test_bit(F_HAVEDIGITAL, &chip->flags) ? 3 : 2;

	return snd_ctl_enum_info(uinfo, 1, items, texts);
}

static int snd_msndmix_get_mux(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_msnd *chip = snd_kcontrol_chip(kcontrol);
	/* MSND_MASK_IMIX is the default */
	ucontrol->value.enumerated.item[0] = 0;

	if (chip->recsrc & MSND_MASK_SYNTH) {
		ucontrol->value.enumerated.item[0] = 1;
	} else if ((chip->recsrc & MSND_MASK_DIGITAL) &&
		 test_bit(F_HAVEDIGITAL, &chip->flags)) {
		ucontrol->value.enumerated.item[0] = 2;
	}


	return 0;
}

static int snd_msndmix_set_mux(struct snd_msnd *chip, int val)
{
	unsigned newrecsrc;
	int change;
	unsigned char msndbyte;

	switch (val) {
	case 0:
		newrecsrc = MSND_MASK_IMIX;
		msndbyte = HDEXAR_SET_ANA_IN;
		break;
	case 1:
		newrecsrc = MSND_MASK_SYNTH;
		msndbyte = HDEXAR_SET_SYNTH_IN;
		break;
	case 2:
		newrecsrc = MSND_MASK_DIGITAL;
		msndbyte = HDEXAR_SET_DAT_IN;
		break;
	default:
		return -EINVAL;
	}
	change  = newrecsrc != chip->recsrc;
	if (change) {
		change = 0;
		if (!snd_msnd_send_word(chip, 0, 0, msndbyte))
			if (!snd_msnd_send_dsp_cmd(chip, HDEX_AUX_REQ)) {
				chip->recsrc = newrecsrc;
				change = 1;
			}
	}
	return change;
}

static int snd_msndmix_put_mux(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_msnd *msnd = snd_kcontrol_chip(kcontrol);
	return snd_msndmix_set_mux(msnd, ucontrol->value.enumerated.item[0]);
}


static int snd_msndmix_volume_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}

static int snd_msndmix_volume_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_msnd *msnd = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;
	unsigned long flags;

	spin_lock_irqsave(&msnd->mixer_lock, flags);
	ucontrol->value.integer.value[0] = msnd->left_levels[addr] * 100;
	ucontrol->value.integer.value[0] /= 0xFFFF;
	ucontrol->value.integer.value[1] = msnd->right_levels[addr] * 100;
	ucontrol->value.integer.value[1] /= 0xFFFF;
	spin_unlock_irqrestore(&msnd->mixer_lock, flags);
	return 0;
}

#define update_volm(a, b)						\
	do {								\
		writew((dev->left_levels[a] >> 1) *			\
		       readw(dev->SMA + SMA_wCurrMastVolLeft) / 0xffff,	\
		       dev->SMA + SMA_##b##Left);			\
		writew((dev->right_levels[a] >> 1)  *			\
		       readw(dev->SMA + SMA_wCurrMastVolRight) / 0xffff, \
		       dev->SMA + SMA_##b##Right);			\
	} while (0);

#define update_potm(d, s, ar)						\
	do {								\
		writeb((dev->left_levels[d] >> 8) *			\
		       readw(dev->SMA + SMA_wCurrMastVolLeft) / 0xffff, \
		       dev->SMA + SMA_##s##Left);			\
		writeb((dev->right_levels[d] >> 8) *			\
		       readw(dev->SMA + SMA_wCurrMastVolRight) / 0xffff, \
		       dev->SMA + SMA_##s##Right);			\
		if (snd_msnd_send_word(dev, 0, 0, ar) == 0)		\
			snd_msnd_send_dsp_cmd(dev, HDEX_AUX_REQ);	\
	} while (0);

#define update_pot(d, s, ar)						\
	do {								\
		writeb(dev->left_levels[d] >> 8,			\
		       dev->SMA + SMA_##s##Left);			\
		writeb(dev->right_levels[d] >> 8,			\
		       dev->SMA + SMA_##s##Right);			\
		if (snd_msnd_send_word(dev, 0, 0, ar) == 0)		\
			snd_msnd_send_dsp_cmd(dev, HDEX_AUX_REQ);	\
	} while (0);


static int snd_msndmix_set(struct snd_msnd *dev, int d, int left, int right)
{
	int bLeft, bRight;
	int wLeft, wRight;
	int updatemaster = 0;

	if (d >= LEVEL_ENTRIES)
		return -EINVAL;

	bLeft = left * 0xff / 100;
	wLeft = left * 0xffff / 100;

	bRight = right * 0xff / 100;
	wRight = right * 0xffff / 100;

	dev->left_levels[d] = wLeft;
	dev->right_levels[d] = wRight;

	switch (d) {
		/* master volume unscaled controls */
	case MSND_MIXER_LINE:			/* line pot control */
		/* scaled by IMIX in digital mix */
		writeb(bLeft, dev->SMA + SMA_bInPotPosLeft);
		writeb(bRight, dev->SMA + SMA_bInPotPosRight);
		if (snd_msnd_send_word(dev, 0, 0, HDEXAR_IN_SET_POTS) == 0)
			snd_msnd_send_dsp_cmd(dev, HDEX_AUX_REQ);
		break;
	case MSND_MIXER_MIC:			/* mic pot control */
		if (dev->type == msndClassic)
			return -EINVAL;
		/* scaled by IMIX in digital mix */
		writeb(bLeft, dev->SMA + SMA_bMicPotPosLeft);
		writeb(bRight, dev->SMA + SMA_bMicPotPosRight);
		if (snd_msnd_send_word(dev, 0, 0, HDEXAR_MIC_SET_POTS) == 0)
			snd_msnd_send_dsp_cmd(dev, HDEX_AUX_REQ);
		break;
	case MSND_MIXER_VOLUME:		/* master volume */
		writew(wLeft, dev->SMA + SMA_wCurrMastVolLeft);
		writew(wRight, dev->SMA + SMA_wCurrMastVolRight);
		/* fall through */

	case MSND_MIXER_AUX:			/* aux pot control */
		/* scaled by master volume */
		/* fall through */

		/* digital controls */
	case MSND_MIXER_SYNTH:			/* synth vol (dsp mix) */
	case MSND_MIXER_PCM:			/* pcm vol (dsp mix) */
	case MSND_MIXER_IMIX:			/* input monitor (dsp mix) */
		/* scaled by master volume */
		updatemaster = 1;
		break;

	default:
		return -EINVAL;
	}

	if (updatemaster) {
		/* update master volume scaled controls */
		update_volm(MSND_MIXER_PCM, wCurrPlayVol);
		update_volm(MSND_MIXER_IMIX, wCurrInVol);
		if (dev->type == msndPinnacle)
			update_volm(MSND_MIXER_SYNTH, wCurrMHdrVol);
		update_potm(MSND_MIXER_AUX, bAuxPotPos, HDEXAR_AUX_SET_POTS);
	}

	return 0;
}

static int snd_msndmix_volume_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_msnd *msnd = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;
	unsigned long flags;

	left = ucontrol->value.integer.value[0] % 101;
	right = ucontrol->value.integer.value[1] % 101;
	spin_lock_irqsave(&msnd->mixer_lock, flags);
	change = msnd->left_levels[addr] != left
		|| msnd->right_levels[addr] != right;
	snd_msndmix_set(msnd, addr, left, right);
	spin_unlock_irqrestore(&msnd->mixer_lock, flags);
	return change;
}


#define DUMMY_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_msndmix_volume_info, \
  .get = snd_msndmix_volume_get, .put = snd_msndmix_volume_put, \
  .private_value = addr }


static const struct snd_kcontrol_new snd_msnd_controls[] = {
DUMMY_VOLUME("Master Volume", 0, MSND_MIXER_VOLUME),
DUMMY_VOLUME("PCM Volume", 0, MSND_MIXER_PCM),
DUMMY_VOLUME("Aux Volume", 0, MSND_MIXER_AUX),
DUMMY_VOLUME("Line Volume", 0, MSND_MIXER_LINE),
DUMMY_VOLUME("Mic Volume", 0, MSND_MIXER_MIC),
DUMMY_VOLUME("Monitor",	0, MSND_MIXER_IMIX),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_msndmix_info_mux,
	.get = snd_msndmix_get_mux,
	.put = snd_msndmix_put_mux,
}
};


int snd_msndmix_new(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	unsigned int idx;
	int err;

	if (snd_BUG_ON(!chip))
		return -EINVAL;
	spin_lock_init(&chip->mixer_lock);
	strcpy(card->mixername, "MSND Pinnacle Mixer");

	for (idx = 0; idx < ARRAY_SIZE(snd_msnd_controls); idx++) {
		err = snd_ctl_add(card,
				  snd_ctl_new1(snd_msnd_controls + idx, chip));
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(snd_msndmix_new);

void snd_msndmix_setup(struct snd_msnd *dev)
{
	update_pot(MSND_MIXER_LINE, bInPotPos, HDEXAR_IN_SET_POTS);
	update_potm(MSND_MIXER_AUX, bAuxPotPos, HDEXAR_AUX_SET_POTS);
	update_volm(MSND_MIXER_PCM, wCurrPlayVol);
	update_volm(MSND_MIXER_IMIX, wCurrInVol);
	if (dev->type == msndPinnacle) {
		update_pot(MSND_MIXER_MIC, bMicPotPos, HDEXAR_MIC_SET_POTS);
		update_volm(MSND_MIXER_SYNTH, wCurrMHdrVol);
	}
}
EXPORT_SYMBOL(snd_msndmix_setup);

int snd_msndmix_force_recsrc(struct snd_msnd *dev, int recsrc)
{
	dev->recsrc = -1;
	return snd_msndmix_set_mux(dev, recsrc);
}
EXPORT_SYMBOL(snd_msndmix_force_recsrc);
