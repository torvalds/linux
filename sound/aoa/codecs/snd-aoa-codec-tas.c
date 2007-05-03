/*
 * Apple Onboard Audio driver for tas codec
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 *
 * Open questions:
 *  - How to distinguish between 3004 and versions?
 *
 * FIXMEs:
 *  - This codec driver doesn't honour the 'connected'
 *    property of the aoa_codec struct, hence if
 *    it is used in machines where not everything is
 *    connected it will display wrong mixer elements.
 *  - Driver assumes that the microphone is always
 *    monaureal and connected to the right channel of
 *    the input. This should also be a codec-dependent
 *    flag, maybe the codec should have 3 different
 *    bits for the three different possibilities how
 *    it can be hooked up...
 *    But as long as I don't see any hardware hooked
 *    up that way...
 *  - As Apple notes in their code, the tas3004 seems
 *    to delay the right channel by one sample. You can
 *    see this when for example recording stereo in
 *    audacity, or recording the tas output via cable
 *    on another machine (use a sinus generator or so).
 *    I tried programming the BiQuads but couldn't
 *    make the delay work, maybe someone can read the
 *    datasheet and fix it. The relevant Apple comment
 *    is in AppleTAS3004Audio.cpp lines 1637 ff. Note
 *    that their comment describing how they program
 *    the filters sucks...
 *
 * Other things:
 *  - this should actually register *two* aoa_codec
 *    structs since it has two inputs. Then it must
 *    use the prepare callback to forbid running the
 *    secondary output on a different clock.
 *    Also, whatever bus knows how to do this must
 *    provide two soundbus_dev devices and the fabric
 *    must be able to link them correctly.
 *
 *    I don't even know if Apple ever uses the second
 *    port on the tas3004 though, I don't think their
 *    i2s controllers can even do it. OTOH, they all
 *    derive the clocks from common clocks, so it
 *    might just be possible. The framework allows the
 *    codec to refine the transfer_info items in the
 *    usable callback, so we can simply remove the
 *    rates the second instance is not using when it
 *    actually is in use.
 *    Maybe we'll need to make the sound busses have
 *    a 'clock group id' value so the codec can
 *    determine if the two outputs can be driven at
 *    the same time. But that is likely overkill, up
 *    to the fabric to not link them up incorrectly,
 *    and up to the hardware designer to not wire
 *    them up in some weird unusable way.
 */
#include <stddef.h>
#include <linux/i2c.h>
#include <asm/pmac_low_i2c.h>
#include <asm/prom.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("tas codec driver for snd-aoa");

#include "snd-aoa-codec-tas.h"
#include "snd-aoa-codec-tas-gain-table.h"
#include "snd-aoa-codec-tas-basstreble.h"
#include "../aoa.h"
#include "../soundbus/soundbus.h"

#define PFX "snd-aoa-codec-tas: "


struct tas {
	struct aoa_codec	codec;
	struct i2c_client	i2c;
	u32			mute_l:1, mute_r:1 ,
				controls_created:1 ,
				drc_enabled:1,
				hw_enabled:1;
	u8			cached_volume_l, cached_volume_r;
	u8			mixer_l[3], mixer_r[3];
	u8			bass, treble;
	u8			acr;
	int			drc_range;
	/* protects hardware access against concurrency from
	 * userspace when hitting controls and during
	 * codec init/suspend/resume */
	struct mutex		mtx;
};

static int tas_reset_init(struct tas *tas);

static struct tas *codec_to_tas(struct aoa_codec *codec)
{
	return container_of(codec, struct tas, codec);
}

static inline int tas_write_reg(struct tas *tas, u8 reg, u8 len, u8 *data)
{
	if (len == 1)
		return i2c_smbus_write_byte_data(&tas->i2c, reg, *data);
	else
		return i2c_smbus_write_i2c_block_data(&tas->i2c, reg, len, data);
}

static void tas3004_set_drc(struct tas *tas)
{
	unsigned char val[6];

	if (tas->drc_enabled)
		val[0] = 0x50; /* 3:1 above threshold */
	else
		val[0] = 0x51; /* disabled */
	val[1] = 0x02; /* 1:1 below threshold */
	if (tas->drc_range > 0xef)
		val[2] = 0xef;
	else if (tas->drc_range < 0)
		val[2] = 0x00;
	else
		val[2] = tas->drc_range;
	val[3] = 0xb0;
	val[4] = 0x60;
	val[5] = 0xa0;

	tas_write_reg(tas, TAS_REG_DRC, 6, val);
}

static void tas_set_treble(struct tas *tas)
{
	u8 tmp;

	tmp = tas3004_treble(tas->treble);
	tas_write_reg(tas, TAS_REG_TREBLE, 1, &tmp);
}

static void tas_set_bass(struct tas *tas)
{
	u8 tmp;

	tmp = tas3004_bass(tas->bass);
	tas_write_reg(tas, TAS_REG_BASS, 1, &tmp);
}

static void tas_set_volume(struct tas *tas)
{
	u8 block[6];
	int tmp;
	u8 left, right;

	left = tas->cached_volume_l;
	right = tas->cached_volume_r;

	if (left > 177) left = 177;
	if (right > 177) right = 177;

	if (tas->mute_l) left = 0;
	if (tas->mute_r) right = 0;

	/* analysing the volume and mixer tables shows
	 * that they are similar enough when we shift
	 * the mixer table down by 4 bits. The error
	 * is miniscule, in just one item the error
	 * is 1, at a value of 0x07f17b (mixer table
	 * value is 0x07f17a) */
	tmp = tas_gaintable[left];
	block[0] = tmp>>20;
	block[1] = tmp>>12;
	block[2] = tmp>>4;
	tmp = tas_gaintable[right];
	block[3] = tmp>>20;
	block[4] = tmp>>12;
	block[5] = tmp>>4;
	tas_write_reg(tas, TAS_REG_VOL, 6, block);
}

static void tas_set_mixer(struct tas *tas)
{
	u8 block[9];
	int tmp, i;
	u8 val;

	for (i=0;i<3;i++) {
		val = tas->mixer_l[i];
		if (val > 177) val = 177;
		tmp = tas_gaintable[val];
		block[3*i+0] = tmp>>16;
		block[3*i+1] = tmp>>8;
		block[3*i+2] = tmp;
	}
	tas_write_reg(tas, TAS_REG_LMIX, 9, block);

	for (i=0;i<3;i++) {
		val = tas->mixer_r[i];
		if (val > 177) val = 177;
		tmp = tas_gaintable[val];
		block[3*i+0] = tmp>>16;
		block[3*i+1] = tmp>>8;
		block[3*i+2] = tmp;
	}
	tas_write_reg(tas, TAS_REG_RMIX, 9, block);
}

/* alsa stuff */

static int tas_dev_register(struct snd_device *dev)
{
	return 0;
}

static struct snd_device_ops ops = {
	.dev_register = tas_dev_register,
};

static int tas_snd_vol_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 177;
	return 0;
}

static int tas_snd_vol_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->cached_volume_l;
	ucontrol->value.integer.value[1] = tas->cached_volume_r;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_vol_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->cached_volume_l == ucontrol->value.integer.value[0]
	 && tas->cached_volume_r == ucontrol->value.integer.value[1]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->cached_volume_l = ucontrol->value.integer.value[0];
	tas->cached_volume_r = ucontrol->value.integer.value[1];
	if (tas->hw_enabled)
		tas_set_volume(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new volume_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_vol_info,
	.get = tas_snd_vol_get,
	.put = tas_snd_vol_put,
};

static int tas_snd_mute_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int tas_snd_mute_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = !tas->mute_l;
	ucontrol->value.integer.value[1] = !tas->mute_r;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_mute_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->mute_l == !ucontrol->value.integer.value[0]
	 && tas->mute_r == !ucontrol->value.integer.value[1]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->mute_l = !ucontrol->value.integer.value[0];
	tas->mute_r = !ucontrol->value.integer.value[1];
	if (tas->hw_enabled)
		tas_set_volume(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new mute_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Playback Switch",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_mute_info,
	.get = tas_snd_mute_get,
	.put = tas_snd_mute_put,
};

static int tas_snd_mixer_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 177;
	return 0;
}

static int tas_snd_mixer_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->mixer_l[idx];
	ucontrol->value.integer.value[1] = tas->mixer_r[idx];
	mutex_unlock(&tas->mtx);

	return 0;
}

static int tas_snd_mixer_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->private_value;

	mutex_lock(&tas->mtx);
	if (tas->mixer_l[idx] == ucontrol->value.integer.value[0]
	 && tas->mixer_r[idx] == ucontrol->value.integer.value[1]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->mixer_l[idx] = ucontrol->value.integer.value[0];
	tas->mixer_r[idx] = ucontrol->value.integer.value[1];

	if (tas->hw_enabled)
		tas_set_mixer(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

#define MIXER_CONTROL(n,descr,idx)			\
static struct snd_kcontrol_new n##_control = {		\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,		\
	.name = descr " Playback Volume",		\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,	\
	.info = tas_snd_mixer_info,			\
	.get = tas_snd_mixer_get,			\
	.put = tas_snd_mixer_put,			\
	.private_value = idx,				\
}

MIXER_CONTROL(pcm1, "PCM", 0);
MIXER_CONTROL(monitor, "Monitor", 2);

static int tas_snd_drc_range_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = TAS3004_DRC_MAX;
	return 0;
}

static int tas_snd_drc_range_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->drc_range;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_drc_range_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->drc_range == ucontrol->value.integer.value[0]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->drc_range = ucontrol->value.integer.value[0];
	if (tas->hw_enabled)
		tas3004_set_drc(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new drc_range_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DRC Range",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_drc_range_info,
	.get = tas_snd_drc_range_get,
	.put = tas_snd_drc_range_put,
};

static int tas_snd_drc_switch_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int tas_snd_drc_switch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->drc_enabled;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_drc_switch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->drc_enabled == ucontrol->value.integer.value[0]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->drc_enabled = ucontrol->value.integer.value[0];
	if (tas->hw_enabled)
		tas3004_set_drc(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new drc_switch_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DRC Range Switch",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_drc_switch_info,
	.get = tas_snd_drc_switch_get,
	.put = tas_snd_drc_switch_put,
};

static int tas_snd_capture_source_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "Line-In", "Microphone" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int tas_snd_capture_source_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.enumerated.item[0] = !!(tas->acr & TAS_ACR_INPUT_B);
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_capture_source_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);
	int oldacr;

	mutex_lock(&tas->mtx);
	oldacr = tas->acr;

	/*
	 * Despite what the data sheet says in one place, the
	 * TAS_ACR_B_MONAUREAL bit forces mono output even when
	 * input A (line in) is selected.
	 */
	tas->acr &= ~(TAS_ACR_INPUT_B | TAS_ACR_B_MONAUREAL);
	if (ucontrol->value.enumerated.item[0])
		tas->acr |= TAS_ACR_INPUT_B | TAS_ACR_B_MONAUREAL |
		      TAS_ACR_B_MON_SEL_RIGHT;
	if (oldacr == tas->acr) {
		mutex_unlock(&tas->mtx);
		return 0;
	}
	if (tas->hw_enabled)
		tas_write_reg(tas, TAS_REG_ACR, 1, &tas->acr);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new capture_source_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	/* If we name this 'Input Source', it properly shows up in
	 * alsamixer as a selection, * but it's shown under the
	 * 'Playback' category.
	 * If I name it 'Capture Source', it shows up in strange
	 * ways (two bools of which one can be selected at a
	 * time) but at least it's shown in the 'Capture'
	 * category.
	 * I was told that this was due to backward compatibility,
	 * but I don't understand then why the mangling is *not*
	 * done when I name it "Input Source".....
	 */
	.name = "Capture Source",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_capture_source_info,
	.get = tas_snd_capture_source_get,
	.put = tas_snd_capture_source_put,
};

static int tas_snd_treble_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TAS3004_TREBLE_MIN;
	uinfo->value.integer.max = TAS3004_TREBLE_MAX;
	return 0;
}

static int tas_snd_treble_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->treble;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_treble_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->treble == ucontrol->value.integer.value[0]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->treble = ucontrol->value.integer.value[0];
	if (tas->hw_enabled)
		tas_set_treble(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new treble_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Treble",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_treble_info,
	.get = tas_snd_treble_get,
	.put = tas_snd_treble_put,
};

static int tas_snd_bass_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = TAS3004_BASS_MIN;
	uinfo->value.integer.max = TAS3004_BASS_MAX;
	return 0;
}

static int tas_snd_bass_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	ucontrol->value.integer.value[0] = tas->bass;
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_snd_bass_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tas *tas = snd_kcontrol_chip(kcontrol);

	mutex_lock(&tas->mtx);
	if (tas->bass == ucontrol->value.integer.value[0]) {
		mutex_unlock(&tas->mtx);
		return 0;
	}

	tas->bass = ucontrol->value.integer.value[0];
	if (tas->hw_enabled)
		tas_set_bass(tas);
	mutex_unlock(&tas->mtx);
	return 1;
}

static struct snd_kcontrol_new bass_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Bass",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = tas_snd_bass_info,
	.get = tas_snd_bass_get,
	.put = tas_snd_bass_put,
};

static struct transfer_info tas_transfers[] = {
	{
		/* input */
		.formats = SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_BE,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.transfer_in = 1,
	},
	{
		/* output */
		.formats = SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_BE,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.transfer_in = 0,
	},
	{}
};

static int tas_usable(struct codec_info_item *cii,
		      struct transfer_info *ti,
		      struct transfer_info *out)
{
	return 1;
}

static int tas_reset_init(struct tas *tas)
{
	u8 tmp;

	tas->codec.gpio->methods->all_amps_off(tas->codec.gpio);
	msleep(5);
	tas->codec.gpio->methods->set_hw_reset(tas->codec.gpio, 0);
	msleep(5);
	tas->codec.gpio->methods->set_hw_reset(tas->codec.gpio, 1);
	msleep(20);
	tas->codec.gpio->methods->set_hw_reset(tas->codec.gpio, 0);
	msleep(10);
	tas->codec.gpio->methods->all_amps_restore(tas->codec.gpio);

	tmp = TAS_MCS_SCLK64 | TAS_MCS_SPORT_MODE_I2S | TAS_MCS_SPORT_WL_24BIT;
	if (tas_write_reg(tas, TAS_REG_MCS, 1, &tmp))
		goto outerr;

	tas->acr |= TAS_ACR_ANALOG_PDOWN;
	if (tas_write_reg(tas, TAS_REG_ACR, 1, &tas->acr))
		goto outerr;

	tmp = 0;
	if (tas_write_reg(tas, TAS_REG_MCS2, 1, &tmp))
		goto outerr;

	tas3004_set_drc(tas);

	/* Set treble & bass to 0dB */
	tas->treble = TAS3004_TREBLE_ZERO;
	tas->bass = TAS3004_BASS_ZERO;
	tas_set_treble(tas);
	tas_set_bass(tas);

	tas->acr &= ~TAS_ACR_ANALOG_PDOWN;
	if (tas_write_reg(tas, TAS_REG_ACR, 1, &tas->acr))
		goto outerr;

	return 0;
 outerr:
	return -ENODEV;
}

static int tas_switch_clock(struct codec_info_item *cii, enum clock_switch clock)
{
	struct tas *tas = cii->codec_data;

	switch(clock) {
	case CLOCK_SWITCH_PREPARE_SLAVE:
		/* Clocks are going away, mute mute mute */
		tas->codec.gpio->methods->all_amps_off(tas->codec.gpio);
		tas->hw_enabled = 0;
		break;
	case CLOCK_SWITCH_SLAVE:
		/* Clocks are back, re-init the codec */
		mutex_lock(&tas->mtx);
		tas_reset_init(tas);
		tas_set_volume(tas);
		tas_set_mixer(tas);
		tas->hw_enabled = 1;
		tas->codec.gpio->methods->all_amps_restore(tas->codec.gpio);
		mutex_unlock(&tas->mtx);
		break;
	default:
		/* doesn't happen as of now */
		return -EINVAL;
	}
	return 0;
}

/* we are controlled via i2c and assume that is always up
 * If that wasn't the case, we'd have to suspend once
 * our i2c device is suspended, and then take note of that! */
static int tas_suspend(struct tas *tas)
{
	mutex_lock(&tas->mtx);
	tas->hw_enabled = 0;
	tas->acr |= TAS_ACR_ANALOG_PDOWN;
	tas_write_reg(tas, TAS_REG_ACR, 1, &tas->acr);
	mutex_unlock(&tas->mtx);
	return 0;
}

static int tas_resume(struct tas *tas)
{
	/* reset codec */
	mutex_lock(&tas->mtx);
	tas_reset_init(tas);
	tas_set_volume(tas);
	tas_set_mixer(tas);
	tas->hw_enabled = 1;
	mutex_unlock(&tas->mtx);
	return 0;
}

#ifdef CONFIG_PM
static int _tas_suspend(struct codec_info_item *cii, pm_message_t state)
{
	return tas_suspend(cii->codec_data);
}

static int _tas_resume(struct codec_info_item *cii)
{
	return tas_resume(cii->codec_data);
}
#endif

static struct codec_info tas_codec_info = {
	.transfers = tas_transfers,
	/* in theory, we can drive it at 512 too...
	 * but so far the framework doesn't allow
	 * for that and I don't see much point in it. */
	.sysclock_factor = 256,
	/* same here, could be 32 for just one 16 bit format */
	.bus_factor = 64,
	.owner = THIS_MODULE,
	.usable = tas_usable,
	.switch_clock = tas_switch_clock,
#ifdef CONFIG_PM
	.suspend = _tas_suspend,
	.resume = _tas_resume,
#endif
};

static int tas_init_codec(struct aoa_codec *codec)
{
	struct tas *tas = codec_to_tas(codec);
	int err;

	if (!tas->codec.gpio || !tas->codec.gpio->methods) {
		printk(KERN_ERR PFX "gpios not assigned!!\n");
		return -EINVAL;
	}

	mutex_lock(&tas->mtx);
	if (tas_reset_init(tas)) {
		printk(KERN_ERR PFX "tas failed to initialise\n");
		mutex_unlock(&tas->mtx);
		return -ENXIO;
	}
	tas->hw_enabled = 1;
	mutex_unlock(&tas->mtx);

	if (tas->codec.soundbus_dev->attach_codec(tas->codec.soundbus_dev,
						   aoa_get_card(),
						   &tas_codec_info, tas)) {
		printk(KERN_ERR PFX "error attaching tas to soundbus\n");
		return -ENODEV;
	}

	if (aoa_snd_device_new(SNDRV_DEV_LOWLEVEL, tas, &ops)) {
		printk(KERN_ERR PFX "failed to create tas snd device!\n");
		return -ENODEV;
	}
	err = aoa_snd_ctl_add(snd_ctl_new1(&volume_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&mute_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&pcm1_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&monitor_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&capture_source_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&drc_range_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&drc_switch_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&treble_control, tas));
	if (err)
		goto error;

	err = aoa_snd_ctl_add(snd_ctl_new1(&bass_control, tas));
	if (err)
		goto error;

	return 0;
 error:
	tas->codec.soundbus_dev->detach_codec(tas->codec.soundbus_dev, tas);
	snd_device_free(aoa_get_card(), tas);
	return err;
}

static void tas_exit_codec(struct aoa_codec *codec)
{
	struct tas *tas = codec_to_tas(codec);

	if (!tas->codec.soundbus_dev)
		return;
	tas->codec.soundbus_dev->detach_codec(tas->codec.soundbus_dev, tas);
}
	

static struct i2c_driver tas_driver;

static int tas_create(struct i2c_adapter *adapter,
		       struct device_node *node,
		       int addr)
{
	struct tas *tas;

	tas = kzalloc(sizeof(struct tas), GFP_KERNEL);

	if (!tas)
		return -ENOMEM;

	mutex_init(&tas->mtx);
	tas->i2c.driver = &tas_driver;
	tas->i2c.adapter = adapter;
	tas->i2c.addr = addr;
	/* seems that half is a saner default */
	tas->drc_range = TAS3004_DRC_MAX / 2;
	strlcpy(tas->i2c.name, "tas audio codec", I2C_NAME_SIZE-1);

	if (i2c_attach_client(&tas->i2c)) {
		printk(KERN_ERR PFX "failed to attach to i2c\n");
		goto fail;
	}

	strlcpy(tas->codec.name, "tas", MAX_CODEC_NAME_LEN-1);
	tas->codec.owner = THIS_MODULE;
	tas->codec.init = tas_init_codec;
	tas->codec.exit = tas_exit_codec;
	tas->codec.node = of_node_get(node);

	if (aoa_codec_register(&tas->codec)) {
		goto detach;
	}
	printk(KERN_DEBUG
	       "snd-aoa-codec-tas: tas found, addr 0x%02x on %s\n",
	       addr, node->full_name);
	return 0;
 detach:
	i2c_detach_client(&tas->i2c);
 fail:
	mutex_destroy(&tas->mtx);
	kfree(tas);
	return -EINVAL;
}

static int tas_i2c_attach(struct i2c_adapter *adapter)
{
	struct device_node *busnode, *dev = NULL;
	struct pmac_i2c_bus *bus;

	bus = pmac_i2c_adapter_to_bus(adapter);
	if (bus == NULL)
		return -ENODEV;
	busnode = pmac_i2c_get_bus_node(bus);

	while ((dev = of_get_next_child(busnode, dev)) != NULL) {
		if (of_device_is_compatible(dev, "tas3004")) {
			const u32 *addr;
			printk(KERN_DEBUG PFX "found tas3004\n");
			addr = of_get_property(dev, "reg", NULL);
			if (!addr)
				continue;
			return tas_create(adapter, dev, ((*addr) >> 1) & 0x7f);
		}
		/* older machines have no 'codec' node with a 'compatible'
		 * property that says 'tas3004', they just have a 'deq'
		 * node without any such property... */
		if (strcmp(dev->name, "deq") == 0) {
			const u32 *_addr;
			u32 addr;
			printk(KERN_DEBUG PFX "found 'deq' node\n");
			_addr = of_get_property(dev, "i2c-address", NULL);
			if (!_addr)
				continue;
			addr = ((*_addr) >> 1) & 0x7f;
			/* now, if the address doesn't match any of the two
			 * that a tas3004 can have, we cannot handle this.
			 * I doubt it ever happens but hey. */
			if (addr != 0x34 && addr != 0x35)
				continue;
			return tas_create(adapter, dev, addr);
		}
	}
	return -ENODEV;
}

static int tas_i2c_detach(struct i2c_client *client)
{
	struct tas *tas = container_of(client, struct tas, i2c);
	int err;
	u8 tmp = TAS_ACR_ANALOG_PDOWN;

	if ((err = i2c_detach_client(client)))
		return err;
	aoa_codec_unregister(&tas->codec);
	of_node_put(tas->codec.node);

	/* power down codec chip */
	tas_write_reg(tas, TAS_REG_ACR, 1, &tmp);

	mutex_destroy(&tas->mtx);
	kfree(tas);
	return 0;
}

static struct i2c_driver tas_driver = {
	.driver = {
		.name = "aoa_codec_tas",
		.owner = THIS_MODULE,
	},
	.attach_adapter = tas_i2c_attach,
	.detach_client = tas_i2c_detach,
};

static int __init tas_init(void)
{
	return i2c_add_driver(&tas_driver);
}

static void __exit tas_exit(void)
{
	i2c_del_driver(&tas_driver);
}

module_init(tas_init);
module_exit(tas_exit);
