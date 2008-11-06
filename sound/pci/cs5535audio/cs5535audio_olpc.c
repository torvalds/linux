#include <linux/olpc.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>
#include "cs5535audio.h"

/* OLPC has an additional feature on top of regular AD1888 codec
features. This is support for an analog input mode. This is a
2 step process. First, to turn off the AD1888 codec bias voltage
and high pass filter. Second, to tell the embedded controller to
reroute from a capacitive trace to a direct trace using an analog
switch. The *_ec()s are what talk to that controller */

static int snd_cs5535audio_ctl_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

#define AD1888_VREFOUT_EN_BIT (1 << 2)
#define AD1888_HPF_EN_BIT (1 << 12)
static int snd_cs5535audio_ctl_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kcontrol);
	u16 reg1, reg2;

	/* if either AD1888 VRef Bias and High Pass Filter are enabled
	or the EC is not in analog mode then flag as not in analog mode.
	No EC command to read current analog state so we cache that. */
	reg1 = snd_ac97_read(cs5535au->ac97, AC97_AD_MISC);
	reg2 = snd_ac97_read(cs5535au->ac97, AC97_AD_TEST2);

	if ((reg1 & AD1888_VREFOUT_EN_BIT) && (reg2 & AD1888_HPF_EN_BIT) &&
		cs5535au->ec_analog_input_mode)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int snd_cs5535audio_ctl_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int err;
	struct cs5535audio *cs5535au = snd_kcontrol_chip(kcontrol);
	u8 value;
	struct snd_ac97 *ac97 = cs5535au->ac97;

	/* value is 1 if analog input is desired */
	value = ucontrol->value.integer.value[0];

	/* use ec mode as flag to determine if any change needed */
	if (cs5535au->ec_analog_input_mode == value)
		return 0;

	/* sets High Z on VREF Bias if 1 */
	if (value)
		err = snd_ac97_update_bits(ac97, AC97_AD_MISC,
				AD1888_VREFOUT_EN_BIT, AD1888_VREFOUT_EN_BIT);
	else
		err = snd_ac97_update_bits(ac97, AC97_AD_MISC,
				AD1888_VREFOUT_EN_BIT, 0);
	if (err < 0)
		snd_printk(KERN_ERR "Error updating AD_MISC %d\n", err);

	/* turns off High Pass Filter if 1 */
	if (value)
		err = snd_ac97_update_bits(ac97, AC97_AD_TEST2,
				AD1888_HPF_EN_BIT, AD1888_HPF_EN_BIT);
	else
		err = snd_ac97_update_bits(ac97, AC97_AD_TEST2,
				AD1888_HPF_EN_BIT, 0);
	if (err < 0)
		snd_printk(KERN_ERR "Error updating AD_TEST2 %d\n", err);

	if (value)
		err = write_ec_command(0x01); /* activate MIC_AC_OFF */
	else
		err = write_ec_command(0x02); /* deactivates MIC_AC_OFF */

	if (err < 0)
		snd_printk(KERN_ERR "Error talking to EC %d\n", err);

	cs5535au->ec_analog_input_mode = value;

	return 1;
}

static struct snd_kcontrol_new snd_cs5535audio_controls __devinitdata =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog Input Switch",
	.info = snd_cs5535audio_ctl_info,
	.get = snd_cs5535audio_ctl_get,
	.put = snd_cs5535audio_ctl_put,
	.private_value = 0
};

int __devinit olpc_quirks(struct snd_card *card, struct snd_ac97 *ac97)
{
	/* setup callback for mixer control that does analog input mode */
	return snd_ctl_add(card, snd_ctl_new1(&snd_cs5535audio_controls,
						ac97->private_data));
}

