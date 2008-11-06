#include <sound/driver.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>

#include <asm/olpc.h>
#include "cs5535audio.h"

/*
 * OLPC has an additional feature on top of the regular AD1888 codec features.
 * It has an Analog Input mode that is switched into (after disabling the
 * High Pass Filter) via GPIO.  It is supported on B2 and later models.
 */
void olpc_analog_input(struct snd_ac97 *ac97, int on)
{
	int err;

	/* update the High Pass Filter (via AC97_AD_TEST2) */
	err = snd_ac97_update_bits(ac97, AC97_AD_TEST2,
			1 << AC97_AD_HPFD_SHIFT, on << AC97_AD_HPFD_SHIFT);
	if (err < 0) {
		snd_printk(KERN_ERR "setting High Pass Filter - %d\n", err);
		return;
	}

	/* set Analog Input through GPIO */
	if (on)
		geode_gpio_set(OLPC_GPIO_MIC_AC, GPIO_OUTPUT_VAL);
	else
		geode_gpio_clear(OLPC_GPIO_MIC_AC, GPIO_OUTPUT_VAL);
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
	v->value.integer.value[0] = geode_gpio_isset(OLPC_GPIO_MIC_AC,
			GPIO_OUTPUT_VAL);
	return 0;
}

static int olpc_dc_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *v)
{
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kctl);

	olpc_analog_input(cs5535au->ac97, v->value.integer.value[0]);
	return 1;
}

static struct snd_kcontrol_new snd_cs5535audio_controls __devinitdata =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DC Mode Enable",
	.info = olpc_dc_info,
	.get = olpc_dc_get,
	.put = olpc_dc_put,
	.private_value = 0
};

void __devinit olpc_prequirks(struct snd_card *card,
		struct snd_ac97_template *ac97)
{
	if (!machine_is_olpc())
		return;

	/* invert EAPD if on an OLPC B3 or higher */
	if (olpc_board_at_least(olpc_board_pre(0xb3)))
		ac97->scaps |= AC97_SCAP_INV_EAPD;
}

int __devinit olpc_quirks(struct snd_card *card, struct snd_ac97 *ac97)
{
	struct snd_ctl_elem_id elem;

	if (!machine_is_olpc())
		return 0;

	/* drop the original AD1888 HPF control */
	memset(&elem, 0, sizeof(elem));
	elem.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strncpy(elem.name, "High Pass Filter Enable", sizeof(elem.name));
	snd_ctl_remove_id(card, &elem);

	/* add the override for OLPC's HPF */
	return snd_ctl_add(card, snd_ctl_new1(&snd_cs5535audio_controls,
						ac97->private_data));
}

