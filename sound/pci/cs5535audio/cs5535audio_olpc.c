// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OLPC XO-1 additional sound features
 *
 * Copyright © 2006  Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright © 2007-2008  Andres Salomon <dilinger@debian.org>
 */
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>
#include <linux/gpio.h>

#include <asm/olpc.h>
#include "cs5535audio.h"

#define DRV_NAME "cs5535audio-olpc"

/*
 * OLPC has an additional feature on top of the regular AD1888 codec features.
 * It has an Analog Input mode that is switched into (after disabling the
 * High Pass Filter) via GPIO.  It is supported on B2 and later models.
 */
void olpc_analog_input(struct snd_ac97 *ac97, int on)
{
	int err;

	if (!machine_is_olpc())
		return;

	/* update the High Pass Filter (via AC97_AD_TEST2) */
	err = snd_ac97_update_bits(ac97, AC97_AD_TEST2,
			1 << AC97_AD_HPFD_SHIFT, on << AC97_AD_HPFD_SHIFT);
	if (err < 0) {
		dev_err(ac97->bus->card->dev,
			"setting High Pass Filter - %d\n", err);
		return;
	}

	/* set Analog Input through GPIO */
	gpio_set_value(OLPC_GPIO_MIC_AC, on);
}

/*
 * OLPC XO-1's V_REFOUT is a mic bias enable.
 */
void olpc_mic_bias(struct snd_ac97 *ac97, int on)
{
	int err;

	if (!machine_is_olpc())
		return;

	on = on ? 0 : 1;
	err = snd_ac97_update_bits(ac97, AC97_AD_MISC,
			1 << AC97_AD_VREFD_SHIFT, on << AC97_AD_VREFD_SHIFT);
	if (err < 0)
		dev_err(ac97->bus->card->dev, "setting MIC Bias - %d\n", err);
}

static int olpc_dc_info(struct snd_kcontrol *kctl,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int olpc_dc_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *v)
{
	v->value.integer.value[0] = gpio_get_value(OLPC_GPIO_MIC_AC);
	return 0;
}

static int olpc_dc_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *v)
{
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kctl);

	olpc_analog_input(cs5535au->ac97, v->value.integer.value[0]);
	return 1;
}

static int olpc_mic_info(struct snd_kcontrol *kctl,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int olpc_mic_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *v)
{
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kctl);
	struct snd_ac97 *ac97 = cs5535au->ac97;
	int i;

	i = (snd_ac97_read(ac97, AC97_AD_MISC) >> AC97_AD_VREFD_SHIFT) & 0x1;
	v->value.integer.value[0] = i ? 0 : 1;
	return 0;
}

static int olpc_mic_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *v)
{
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kctl);

	olpc_mic_bias(cs5535au->ac97, v->value.integer.value[0]);
	return 1;
}

static const struct snd_kcontrol_new olpc_cs5535audio_ctls[] = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DC Mode Enable",
	.info = olpc_dc_info,
	.get = olpc_dc_get,
	.put = olpc_dc_put,
	.private_value = 0,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "MIC Bias Enable",
	.info = olpc_mic_info,
	.get = olpc_mic_get,
	.put = olpc_mic_put,
	.private_value = 0,
},
};

void olpc_prequirks(struct snd_card *card,
		    struct snd_ac97_template *ac97)
{
	if (!machine_is_olpc())
		return;

	/* invert EAPD if on an OLPC B3 or higher */
	if (olpc_board_at_least(olpc_board_pre(0xb3)))
		ac97->scaps |= AC97_SCAP_INV_EAPD;
}

int olpc_quirks(struct snd_card *card, struct snd_ac97 *ac97)
{
	struct snd_ctl_elem_id elem;
	int i, err;

	if (!machine_is_olpc())
		return 0;

	if (gpio_request(OLPC_GPIO_MIC_AC, DRV_NAME)) {
		dev_err(card->dev, "unable to allocate MIC GPIO\n");
		return -EIO;
	}
	gpio_direction_output(OLPC_GPIO_MIC_AC, 0);

	/* drop the original AD1888 HPF control */
	memset(&elem, 0, sizeof(elem));
	elem.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strscpy(elem.name, "High Pass Filter Enable", sizeof(elem.name));
	snd_ctl_remove_id(card, &elem);

	/* drop the original V_REFOUT control */
	memset(&elem, 0, sizeof(elem));
	elem.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strscpy(elem.name, "V_REFOUT Enable", sizeof(elem.name));
	snd_ctl_remove_id(card, &elem);

	/* add the OLPC-specific controls */
	for (i = 0; i < ARRAY_SIZE(olpc_cs5535audio_ctls); i++) {
		err = snd_ctl_add(card, snd_ctl_new1(&olpc_cs5535audio_ctls[i],
				ac97->private_data));
		if (err < 0) {
			gpio_free(OLPC_GPIO_MIC_AC);
			return err;
		}
	}

	/* turn off the mic by default */
	olpc_mic_bias(ac97, 0);
	return 0;
}

void olpc_quirks_cleanup(void)
{
	gpio_free(OLPC_GPIO_MIC_AC);
}
