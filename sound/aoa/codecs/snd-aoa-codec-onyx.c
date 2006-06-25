/*
 * Apple Onboard Audio driver for Onyx codec
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 *
 *
 * This is a driver for the pcm3052 codec chip (codenamed Onyx)
 * that is present in newer Apple hardware (with digital output).
 *
 * The Onyx codec has the following connections (listed by the bit
 * to be used in aoa_codec.connected):
 *  0: analog output
 *  1: digital output
 *  2: line input
 *  3: microphone input
 * Note that even though I know of no machine that has for example
 * the digital output connected but not the analog, I have handled
 * all the different cases in the code so that this driver may serve
 * as a good example of what to do.
 *
 * NOTE: This driver assumes that there's at most one chip to be
 * 	 used with one alsa card, in form of creating all kinds
 *	 of mixer elements without regard for their existence.
 *	 But snd-aoa assumes that there's at most one card, so
 *	 this means you can only have one onyx on a system. This
 *	 should probably be fixed by changing the assumption of
 *	 having just a single card on a system, and making the
 *	 'card' pointer accessible to anyone who needs it instead
 *	 of hiding it in the aoa_snd_* functions...
 *
 */
#include <linux/delay.h>
#include <linux/module.h>
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("pcm3052 (onyx) codec driver for snd-aoa");

#include "snd-aoa-codec-onyx.h"
#include "../aoa.h"
#include "../soundbus/soundbus.h"


#define PFX "snd-aoa-codec-onyx: "

struct onyx {
	/* cache registers 65 to 80, they are write-only! */
	u8			cache[16];
	struct i2c_client	i2c;
	struct aoa_codec	codec;
	u32			initialised:1,
				spdif_locked:1,
				analog_locked:1,
				original_mute:2;
	int			open_count;
	struct codec_info	*codec_info;

	/* mutex serializes concurrent access to the device
	 * and this structure.
	 */
	struct mutex mutex;
};
#define codec_to_onyx(c) container_of(c, struct onyx, codec)

/* both return 0 if all ok, else on error */
static int onyx_read_register(struct onyx *onyx, u8 reg, u8 *value)
{
	s32 v;

	if (reg != ONYX_REG_CONTROL) {
		*value = onyx->cache[reg-FIRSTREGISTER];
		return 0;
	}
	v = i2c_smbus_read_byte_data(&onyx->i2c, reg);
	if (v < 0)
		return -1;
	*value = (u8)v;
	onyx->cache[ONYX_REG_CONTROL-FIRSTREGISTER] = *value;
	return 0;
}

static int onyx_write_register(struct onyx *onyx, u8 reg, u8 value)
{
	int result;

	result = i2c_smbus_write_byte_data(&onyx->i2c, reg, value);
	if (!result)
		onyx->cache[reg-FIRSTREGISTER] = value;
	return result;
}

/* alsa stuff */

static int onyx_dev_register(struct snd_device *dev)
{
	return 0;
}

static struct snd_device_ops ops = {
	.dev_register = onyx_dev_register,
};

/* this is necessary because most alsa mixer programs
 * can't properly handle the negative range */
#define VOLUME_RANGE_SHIFT	128

static int onyx_snd_vol_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = -128 + VOLUME_RANGE_SHIFT;
	uinfo->value.integer.max = -1 + VOLUME_RANGE_SHIFT;
	return 0;
}

static int onyx_snd_vol_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	s8 l, r;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DAC_ATTEN_LEFT, &l);
	onyx_read_register(onyx, ONYX_REG_DAC_ATTEN_RIGHT, &r);
	mutex_unlock(&onyx->mutex);

	ucontrol->value.integer.value[0] = l + VOLUME_RANGE_SHIFT;
	ucontrol->value.integer.value[1] = r + VOLUME_RANGE_SHIFT;

	return 0;
}

static int onyx_snd_vol_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	s8 l, r;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DAC_ATTEN_LEFT, &l);
	onyx_read_register(onyx, ONYX_REG_DAC_ATTEN_RIGHT, &r);

	if (l + VOLUME_RANGE_SHIFT == ucontrol->value.integer.value[0] &&
	    r + VOLUME_RANGE_SHIFT == ucontrol->value.integer.value[1]) {
		mutex_unlock(&onyx->mutex);
		return 0;
	}

	onyx_write_register(onyx, ONYX_REG_DAC_ATTEN_LEFT,
			    ucontrol->value.integer.value[0]
			     - VOLUME_RANGE_SHIFT);
	onyx_write_register(onyx, ONYX_REG_DAC_ATTEN_RIGHT,
			    ucontrol->value.integer.value[1]
			     - VOLUME_RANGE_SHIFT);
	mutex_unlock(&onyx->mutex);

	return 1;
}

static struct snd_kcontrol_new volume_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = onyx_snd_vol_info,
	.get = onyx_snd_vol_get,
	.put = onyx_snd_vol_put,
};

/* like above, this is necessary because a lot
 * of alsa mixer programs don't handle ranges
 * that don't start at 0 properly.
 * even alsamixer is one of them... */
#define INPUTGAIN_RANGE_SHIFT	(-3)

static int onyx_snd_inputgain_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 3 + INPUTGAIN_RANGE_SHIFT;
	uinfo->value.integer.max = 28 + INPUTGAIN_RANGE_SHIFT;
	return 0;
}

static int onyx_snd_inputgain_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 ig;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_ADC_CONTROL, &ig);
	mutex_unlock(&onyx->mutex);

	ucontrol->value.integer.value[0] =
		(ig & ONYX_ADC_PGA_GAIN_MASK) + INPUTGAIN_RANGE_SHIFT;

	return 0;
}

static int onyx_snd_inputgain_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 v, n;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_ADC_CONTROL, &v);
	n = v;
	n &= ~ONYX_ADC_PGA_GAIN_MASK;
	n |= (ucontrol->value.integer.value[0] - INPUTGAIN_RANGE_SHIFT)
		& ONYX_ADC_PGA_GAIN_MASK;
	onyx_write_register(onyx, ONYX_REG_ADC_CONTROL, n);
	mutex_unlock(&onyx->mutex);

	return n != v;
}

static struct snd_kcontrol_new inputgain_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Capture Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = onyx_snd_inputgain_info,
	.get = onyx_snd_inputgain_get,
	.put = onyx_snd_inputgain_put,
};

static int onyx_snd_capture_source_info(struct snd_kcontrol *kcontrol,
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

static int onyx_snd_capture_source_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	s8 v;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_ADC_CONTROL, &v);
	mutex_unlock(&onyx->mutex);

	ucontrol->value.enumerated.item[0] = !!(v&ONYX_ADC_INPUT_MIC);

	return 0;
}

static void onyx_set_capture_source(struct onyx *onyx, int mic)
{
	s8 v;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_ADC_CONTROL, &v);
	v &= ~ONYX_ADC_INPUT_MIC;
	if (mic)
		v |= ONYX_ADC_INPUT_MIC;
	onyx_write_register(onyx, ONYX_REG_ADC_CONTROL, v);
	mutex_unlock(&onyx->mutex);
}

static int onyx_snd_capture_source_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	onyx_set_capture_source(snd_kcontrol_chip(kcontrol),
				ucontrol->value.enumerated.item[0]);
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
	.info = onyx_snd_capture_source_info,
	.get = onyx_snd_capture_source_get,
	.put = onyx_snd_capture_source_put,
};

static int onyx_snd_mute_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int onyx_snd_mute_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 c;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DAC_CONTROL, &c);
	mutex_unlock(&onyx->mutex);

	ucontrol->value.integer.value[0] = !(c & ONYX_MUTE_LEFT);
	ucontrol->value.integer.value[1] = !(c & ONYX_MUTE_RIGHT);

	return 0;
}

static int onyx_snd_mute_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 v = 0, c = 0;
	int err = -EBUSY;

	mutex_lock(&onyx->mutex);
	if (onyx->analog_locked)
		goto out_unlock;

	onyx_read_register(onyx, ONYX_REG_DAC_CONTROL, &v);
	c = v;
	c &= ~(ONYX_MUTE_RIGHT | ONYX_MUTE_LEFT);
	if (!ucontrol->value.integer.value[0])
		c |= ONYX_MUTE_LEFT;
	if (!ucontrol->value.integer.value[1])
		c |= ONYX_MUTE_RIGHT;
	err = onyx_write_register(onyx, ONYX_REG_DAC_CONTROL, c);

 out_unlock:
	mutex_unlock(&onyx->mutex);

	return !err ? (v != c) : err;
}

static struct snd_kcontrol_new mute_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Playback Switch",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = onyx_snd_mute_info,
	.get = onyx_snd_mute_get,
	.put = onyx_snd_mute_put,
};


static int onyx_snd_single_bit_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

#define FLAG_POLARITY_INVERT	1
#define FLAG_SPDIFLOCK		2

static int onyx_snd_single_bit_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 c;
	long int pv = kcontrol->private_value;
	u8 polarity = (pv >> 16) & FLAG_POLARITY_INVERT;
	u8 address = (pv >> 8) & 0xff;
	u8 mask = pv & 0xff;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, address, &c);
	mutex_unlock(&onyx->mutex);

	ucontrol->value.integer.value[0] = !!(c & mask) ^ polarity;

	return 0;
}

static int onyx_snd_single_bit_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 v = 0, c = 0;
	int err;
	long int pv = kcontrol->private_value;
	u8 polarity = (pv >> 16) & FLAG_POLARITY_INVERT;
	u8 spdiflock = (pv >> 16) & FLAG_SPDIFLOCK;
	u8 address = (pv >> 8) & 0xff;
	u8 mask = pv & 0xff;

	mutex_lock(&onyx->mutex);
	if (spdiflock && onyx->spdif_locked) {
		/* even if alsamixer doesn't care.. */
		err = -EBUSY;
		goto out_unlock;
	}
	onyx_read_register(onyx, address, &v);
	c = v;
	c &= ~(mask);
	if (!!ucontrol->value.integer.value[0] ^ polarity)
		c |= mask;
	err = onyx_write_register(onyx, address, c);

 out_unlock:
	mutex_unlock(&onyx->mutex);

	return !err ? (v != c) : err;
}

#define SINGLE_BIT(n, type, description, address, mask, flags)	 	\
static struct snd_kcontrol_new n##_control = {				\
	.iface = SNDRV_CTL_ELEM_IFACE_##type,				\
	.name = description,						\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,			\
	.info = onyx_snd_single_bit_info,				\
	.get = onyx_snd_single_bit_get,					\
	.put = onyx_snd_single_bit_put,					\
	.private_value = (flags << 16) | (address << 8) | mask		\
}

SINGLE_BIT(spdif,
	   MIXER,
	   SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
	   ONYX_REG_DIG_INFO4,
	   ONYX_SPDIF_ENABLE,
	   FLAG_SPDIFLOCK);
SINGLE_BIT(ovr1,
	   MIXER,
	   "Oversampling Rate",
	   ONYX_REG_DAC_CONTROL,
	   ONYX_OVR1,
	   0);
SINGLE_BIT(flt0,
	   MIXER,
	   "Fast Digital Filter Rolloff",
	   ONYX_REG_DAC_FILTER,
	   ONYX_ROLLOFF_FAST,
	   FLAG_POLARITY_INVERT);
SINGLE_BIT(hpf,
	   MIXER,
	   "Highpass Filter",
	   ONYX_REG_ADC_HPF_BYPASS,
	   ONYX_HPF_DISABLE,
	   FLAG_POLARITY_INVERT);
SINGLE_BIT(dm12,
	   MIXER,
	   "Digital De-Emphasis",
	   ONYX_REG_DAC_DEEMPH,
	   ONYX_DIGDEEMPH_CTRL,
	   0);

static int onyx_spdif_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int onyx_spdif_mask_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	/* datasheet page 30, all others are 0 */
	ucontrol->value.iec958.status[0] = 0x3e;
	ucontrol->value.iec958.status[1] = 0xff;

	ucontrol->value.iec958.status[3] = 0x3f;
	ucontrol->value.iec958.status[4] = 0x0f;
	
	return 0;
}

static struct snd_kcontrol_new onyx_spdif_mask = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		onyx_spdif_info,
	.get =		onyx_spdif_mask_get,
};

static int onyx_spdif_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 v;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DIG_INFO1, &v);
	ucontrol->value.iec958.status[0] = v & 0x3e;

	onyx_read_register(onyx, ONYX_REG_DIG_INFO2, &v);
	ucontrol->value.iec958.status[1] = v;

	onyx_read_register(onyx, ONYX_REG_DIG_INFO3, &v);
	ucontrol->value.iec958.status[3] = v & 0x3f;

	onyx_read_register(onyx, ONYX_REG_DIG_INFO4, &v);
	ucontrol->value.iec958.status[4] = v & 0x0f;
	mutex_unlock(&onyx->mutex);

	return 0;
}

static int onyx_spdif_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct onyx *onyx = snd_kcontrol_chip(kcontrol);
	u8 v;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DIG_INFO1, &v);
	v = (v & ~0x3e) | (ucontrol->value.iec958.status[0] & 0x3e);
	onyx_write_register(onyx, ONYX_REG_DIG_INFO1, v);

	v = ucontrol->value.iec958.status[1];
	onyx_write_register(onyx, ONYX_REG_DIG_INFO2, v);

	onyx_read_register(onyx, ONYX_REG_DIG_INFO3, &v);
	v = (v & ~0x3f) | (ucontrol->value.iec958.status[3] & 0x3f);
	onyx_write_register(onyx, ONYX_REG_DIG_INFO3, v);

	onyx_read_register(onyx, ONYX_REG_DIG_INFO4, &v);
	v = (v & ~0x0f) | (ucontrol->value.iec958.status[4] & 0x0f);
	onyx_write_register(onyx, ONYX_REG_DIG_INFO4, v);
	mutex_unlock(&onyx->mutex);

	return 1;
}

static struct snd_kcontrol_new onyx_spdif_ctrl = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		onyx_spdif_info,
	.get =		onyx_spdif_get,
	.put =		onyx_spdif_put,
};

/* our registers */

static u8 register_map[] = {
	ONYX_REG_DAC_ATTEN_LEFT,
	ONYX_REG_DAC_ATTEN_RIGHT,
	ONYX_REG_CONTROL,
	ONYX_REG_DAC_CONTROL,
	ONYX_REG_DAC_DEEMPH,
	ONYX_REG_DAC_FILTER,
	ONYX_REG_DAC_OUTPHASE,
	ONYX_REG_ADC_CONTROL,
	ONYX_REG_ADC_HPF_BYPASS,
	ONYX_REG_DIG_INFO1,
	ONYX_REG_DIG_INFO2,
	ONYX_REG_DIG_INFO3,
	ONYX_REG_DIG_INFO4
};

static u8 initial_values[ARRAY_SIZE(register_map)] = {
	0x80, 0x80, /* muted */
	ONYX_MRST | ONYX_SRST, /* but handled specially! */
	ONYX_MUTE_LEFT | ONYX_MUTE_RIGHT,
	0, /* no deemphasis */
	ONYX_DAC_FILTER_ALWAYS,
	ONYX_OUTPHASE_INVERTED,
	(-1 /*dB*/ + 8) & 0xF, /* line in selected, -1 dB gain*/
	ONYX_ADC_HPF_ALWAYS,
	(1<<2),	/* pcm audio */
	2,	/* category: pcm coder */
	0,	/* sampling frequency 44.1 kHz, clock accuracy level II */
	1	/* 24 bit depth */
};

/* reset registers of chip, either to initial or to previous values */
static int onyx_register_init(struct onyx *onyx)
{
	int i;
	u8 val;
	u8 regs[sizeof(initial_values)];

	if (!onyx->initialised) {
		memcpy(regs, initial_values, sizeof(initial_values));
		if (onyx_read_register(onyx, ONYX_REG_CONTROL, &val))
			return -1;
		val &= ~ONYX_SILICONVERSION;
		val |= initial_values[3];
		regs[3] = val;
	} else {
		for (i=0; i<sizeof(register_map); i++)
			regs[i] = onyx->cache[register_map[i]-FIRSTREGISTER];
	}

	for (i=0; i<sizeof(register_map); i++) {
		if (onyx_write_register(onyx, register_map[i], regs[i]))
			return -1;
	}
	onyx->initialised = 1;
	return 0;
}

static struct transfer_info onyx_transfers[] = {
	/* this is first so we can skip it if no input is present...
	 * No hardware exists with that, but it's here as an example
	 * of what to do :) */
	{
		/* analog input */
		.formats = SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.transfer_in = 1,
		.must_be_clock_source = 0,
		.tag = 0,
	},
	{
		/* if analog and digital are currently off, anything should go,
		 * so this entry describes everything we can do... */
		.formats = SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE
#ifdef SNDRV_PCM_FMTBIT_COMPRESSED_16BE
			   | SNDRV_PCM_FMTBIT_COMPRESSED_16BE
#endif
		,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.tag = 0,
	},
	{
		/* analog output */
		.formats = SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.transfer_in = 0,
		.must_be_clock_source = 0,
		.tag = 1,
	},
	{
		/* digital pcm output, also possible for analog out */
		.formats = SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_BE,
		.rates = SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000,
		.transfer_in = 0,
		.must_be_clock_source = 0,
		.tag = 2,
	},
#ifdef SNDRV_PCM_FMTBIT_COMPRESSED_16BE
Once alsa gets supports for this kind of thing we can add it...
	{
		/* digital compressed output */
		.formats =  SNDRV_PCM_FMTBIT_COMPRESSED_16BE,
		.rates = SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000,
		.tag = 2,
	},
#endif
	{}
};

static int onyx_usable(struct codec_info_item *cii,
		       struct transfer_info *ti,
		       struct transfer_info *out)
{
	u8 v;
	struct onyx *onyx = cii->codec_data;
	int spdif_enabled, analog_enabled;

	mutex_lock(&onyx->mutex);
	onyx_read_register(onyx, ONYX_REG_DIG_INFO4, &v);
	spdif_enabled = !!(v & ONYX_SPDIF_ENABLE);
	onyx_read_register(onyx, ONYX_REG_DAC_CONTROL, &v);
	analog_enabled = 
		(v & (ONYX_MUTE_RIGHT|ONYX_MUTE_LEFT))
		 != (ONYX_MUTE_RIGHT|ONYX_MUTE_LEFT);
	mutex_unlock(&onyx->mutex);

	switch (ti->tag) {
	case 0: return 1;
	case 1:	return analog_enabled;
	case 2: return spdif_enabled;
	}
	return 1;
}

static int onyx_prepare(struct codec_info_item *cii,
			struct bus_info *bi,
			struct snd_pcm_substream *substream)
{
	u8 v;
	struct onyx *onyx = cii->codec_data;
	int err = -EBUSY;

	mutex_lock(&onyx->mutex);

#ifdef SNDRV_PCM_FMTBIT_COMPRESSED_16BE
	if (substream->runtime->format == SNDRV_PCM_FMTBIT_COMPRESSED_16BE) {
		/* mute and lock analog output */
		onyx_read_register(onyx, ONYX_REG_DAC_CONTROL, &v);
		if (onyx_write_register(onyx
					ONYX_REG_DAC_CONTROL,
					v | ONYX_MUTE_RIGHT | ONYX_MUTE_LEFT))
			goto out_unlock;
		onyx->analog_locked = 1;
		err = 0;
		goto out_unlock;
	}
#endif
	switch (substream->runtime->rate) {
	case 32000:
	case 44100:
	case 48000:
		/* these rates are ok for all outputs */
		/* FIXME: program spdif channel control bits here so that
		 *	  userspace doesn't have to if it only plays pcm! */
		err = 0;
		goto out_unlock;
	default:
		/* got some rate that the digital output can't do,
		 * so disable and lock it */
		onyx_read_register(cii->codec_data, ONYX_REG_DIG_INFO4, &v);
		if (onyx_write_register(onyx,
					ONYX_REG_DIG_INFO4,
					v & ~ONYX_SPDIF_ENABLE))
			goto out_unlock;
		onyx->spdif_locked = 1;
		err = 0;
		goto out_unlock;
	}

 out_unlock:
	mutex_unlock(&onyx->mutex);

	return err;
}

static int onyx_open(struct codec_info_item *cii,
		     struct snd_pcm_substream *substream)
{
	struct onyx *onyx = cii->codec_data;

	mutex_lock(&onyx->mutex);
	onyx->open_count++;
	mutex_unlock(&onyx->mutex);

	return 0;
}

static int onyx_close(struct codec_info_item *cii,
		      struct snd_pcm_substream *substream)
{
	struct onyx *onyx = cii->codec_data;

	mutex_lock(&onyx->mutex);
	onyx->open_count--;
	if (!onyx->open_count)
		onyx->spdif_locked = onyx->analog_locked = 0;
	mutex_unlock(&onyx->mutex);

	return 0;
}

static int onyx_switch_clock(struct codec_info_item *cii,
			     enum clock_switch what)
{
	struct onyx *onyx = cii->codec_data;

	mutex_lock(&onyx->mutex);
	/* this *MUST* be more elaborate later... */
	switch (what) {
	case CLOCK_SWITCH_PREPARE_SLAVE:
		onyx->codec.gpio->methods->all_amps_off(onyx->codec.gpio);
		break;
	case CLOCK_SWITCH_SLAVE:
		onyx->codec.gpio->methods->all_amps_restore(onyx->codec.gpio);
		break;
	default: /* silence warning */
		break;
	}
	mutex_unlock(&onyx->mutex);

	return 0;
}

#ifdef CONFIG_PM

static int onyx_suspend(struct codec_info_item *cii, pm_message_t state)
{
	struct onyx *onyx = cii->codec_data;
	u8 v;
	int err = -ENXIO;

	mutex_lock(&onyx->mutex);
	if (onyx_read_register(onyx, ONYX_REG_CONTROL, &v))
		goto out_unlock;
	onyx_write_register(onyx, ONYX_REG_CONTROL, v | ONYX_ADPSV | ONYX_DAPSV);
	/* Apple does a sleep here but the datasheet says to do it on resume */
	err = 0;
 out_unlock:
	mutex_unlock(&onyx->mutex);

	return err;
}

static int onyx_resume(struct codec_info_item *cii)
{
	struct onyx *onyx = cii->codec_data;
	u8 v;
	int err = -ENXIO;

	mutex_lock(&onyx->mutex);
	/* take codec out of suspend */
	if (onyx_read_register(onyx, ONYX_REG_CONTROL, &v))
		goto out_unlock;
	onyx_write_register(onyx, ONYX_REG_CONTROL, v & ~(ONYX_ADPSV | ONYX_DAPSV));
	/* FIXME: should divide by sample rate, but 8k is the lowest we go */
	msleep(2205000/8000);
	/* reset all values */
	onyx_register_init(onyx);
	err = 0;
 out_unlock:
	mutex_unlock(&onyx->mutex);

	return err;
}

#endif /* CONFIG_PM */

static struct codec_info onyx_codec_info = {
	.transfers = onyx_transfers,
	.sysclock_factor = 256,
	.bus_factor = 64,
	.owner = THIS_MODULE,
	.usable = onyx_usable,
	.prepare = onyx_prepare,
	.open = onyx_open,
	.close = onyx_close,
	.switch_clock = onyx_switch_clock,
#ifdef CONFIG_PM
	.suspend = onyx_suspend,
	.resume = onyx_resume,
#endif
};

static int onyx_init_codec(struct aoa_codec *codec)
{
	struct onyx *onyx = codec_to_onyx(codec);
	struct snd_kcontrol *ctl;
	struct codec_info *ci = &onyx_codec_info;
	u8 v;
	int err;

	if (!onyx->codec.gpio || !onyx->codec.gpio->methods) {
		printk(KERN_ERR PFX "gpios not assigned!!\n");
		return -EINVAL;
	}

	onyx->codec.gpio->methods->set_hw_reset(onyx->codec.gpio, 0);
	msleep(1);
	onyx->codec.gpio->methods->set_hw_reset(onyx->codec.gpio, 1);
	msleep(1);
	onyx->codec.gpio->methods->set_hw_reset(onyx->codec.gpio, 0);
	msleep(1);
	
	if (onyx_register_init(onyx)) {
		printk(KERN_ERR PFX "failed to initialise onyx registers\n");
		return -ENODEV;
	}

	if (aoa_snd_device_new(SNDRV_DEV_LOWLEVEL, onyx, &ops)) {
		printk(KERN_ERR PFX "failed to create onyx snd device!\n");
		return -ENODEV;
	}

	/* nothing connected? what a joke! */
	if ((onyx->codec.connected & 0xF) == 0)
		return -ENOTCONN;

	/* if no inputs are present... */
	if ((onyx->codec.connected & 0xC) == 0) {
		if (!onyx->codec_info)
			onyx->codec_info = kmalloc(sizeof(struct codec_info), GFP_KERNEL);
		if (!onyx->codec_info)
			return -ENOMEM;
		ci = onyx->codec_info;
		*ci = onyx_codec_info;
		ci->transfers++;
	}

	/* if no outputs are present... */
	if ((onyx->codec.connected & 3) == 0) {
		if (!onyx->codec_info)
			onyx->codec_info = kmalloc(sizeof(struct codec_info), GFP_KERNEL);
		if (!onyx->codec_info)
			return -ENOMEM;
		ci = onyx->codec_info;
		/* this is fine as there have to be inputs
		 * if we end up in this part of the code */
		*ci = onyx_codec_info;
		ci->transfers[1].formats = 0;
	}

	if (onyx->codec.soundbus_dev->attach_codec(onyx->codec.soundbus_dev,
						   aoa_get_card(),
						   ci, onyx)) {
		printk(KERN_ERR PFX "error creating onyx pcm\n");
		return -ENODEV;
	}
#define ADDCTL(n)							\
	do {								\
		ctl = snd_ctl_new1(&n, onyx);				\
		if (ctl) {						\
			ctl->id.device =				\
				onyx->codec.soundbus_dev->pcm->device;	\
			err = aoa_snd_ctl_add(ctl);			\
			if (err)					\
				goto error;				\
		}							\
	} while (0)

	if (onyx->codec.soundbus_dev->pcm) {
		/* give the user appropriate controls
		 * depending on what inputs are connected */
		if ((onyx->codec.connected & 0xC) == 0xC)
			ADDCTL(capture_source_control);
		else if (onyx->codec.connected & 4)
			onyx_set_capture_source(onyx, 0);
		else
			onyx_set_capture_source(onyx, 1);
		if (onyx->codec.connected & 0xC)
			ADDCTL(inputgain_control);

		/* depending on what output is connected,
		 * give the user appropriate controls */
		if (onyx->codec.connected & 1) {
			ADDCTL(volume_control);
			ADDCTL(mute_control);
			ADDCTL(ovr1_control);
			ADDCTL(flt0_control);
			ADDCTL(hpf_control);
			ADDCTL(dm12_control);
			/* spdif control defaults to off */
		}
		if (onyx->codec.connected & 2) {
			ADDCTL(onyx_spdif_mask);
			ADDCTL(onyx_spdif_ctrl);
		}
		if ((onyx->codec.connected & 3) == 3)
			ADDCTL(spdif_control);
		/* if only S/PDIF is connected, enable it unconditionally */
		if ((onyx->codec.connected & 3) == 2) {
			onyx_read_register(onyx, ONYX_REG_DIG_INFO4, &v);
			v |= ONYX_SPDIF_ENABLE;
			onyx_write_register(onyx, ONYX_REG_DIG_INFO4, v);
		}
	}
#undef ADDCTL
	printk(KERN_INFO PFX "attached to onyx codec via i2c\n");

	return 0;
 error:
	onyx->codec.soundbus_dev->detach_codec(onyx->codec.soundbus_dev, onyx);
	snd_device_free(aoa_get_card(), onyx);
	return err;
}

static void onyx_exit_codec(struct aoa_codec *codec)
{
	struct onyx *onyx = codec_to_onyx(codec);

	if (!onyx->codec.soundbus_dev) {
		printk(KERN_ERR PFX "onyx_exit_codec called without soundbus_dev!\n");
		return;
	}
	onyx->codec.soundbus_dev->detach_codec(onyx->codec.soundbus_dev, onyx);
}

static struct i2c_driver onyx_driver;

static int onyx_create(struct i2c_adapter *adapter,
		       struct device_node *node,
		       int addr)
{
	struct onyx *onyx;
	u8 dummy;

	onyx = kzalloc(sizeof(struct onyx), GFP_KERNEL);

	if (!onyx)
		return -ENOMEM;

	mutex_init(&onyx->mutex);
	onyx->i2c.driver = &onyx_driver;
	onyx->i2c.adapter = adapter;
	onyx->i2c.addr = addr & 0x7f;
	strlcpy(onyx->i2c.name, "onyx audio codec", I2C_NAME_SIZE-1);

	if (i2c_attach_client(&onyx->i2c)) {
		printk(KERN_ERR PFX "failed to attach to i2c\n");
		goto fail;
	}

	/* we try to read from register ONYX_REG_CONTROL
	 * to check if the codec is present */
	if (onyx_read_register(onyx, ONYX_REG_CONTROL, &dummy) != 0) {
		i2c_detach_client(&onyx->i2c);
		printk(KERN_ERR PFX "failed to read control register\n");
		goto fail;
	}

	strlcpy(onyx->codec.name, "onyx", MAX_CODEC_NAME_LEN-1);
	onyx->codec.owner = THIS_MODULE;
	onyx->codec.init = onyx_init_codec;
	onyx->codec.exit = onyx_exit_codec;
	onyx->codec.node = of_node_get(node);

	if (aoa_codec_register(&onyx->codec)) {
		i2c_detach_client(&onyx->i2c);
		goto fail;
	}
	printk(KERN_DEBUG PFX "created and attached onyx instance\n");
	return 0;
 fail:
	kfree(onyx);
	return -EINVAL;
}

static int onyx_i2c_attach(struct i2c_adapter *adapter)
{
	struct device_node *busnode, *dev = NULL;
	struct pmac_i2c_bus *bus;

	bus = pmac_i2c_adapter_to_bus(adapter);
	if (bus == NULL)
		return -ENODEV;
	busnode = pmac_i2c_get_bus_node(bus);

	while ((dev = of_get_next_child(busnode, dev)) != NULL) {
		if (device_is_compatible(dev, "pcm3052")) {
			u32 *addr;
			printk(KERN_DEBUG PFX "found pcm3052\n");
			addr = (u32 *) get_property(dev, "reg", NULL);
			if (!addr)
				return -ENODEV;
			return onyx_create(adapter, dev, (*addr)>>1);
		}
	}

	/* if that didn't work, try desperate mode for older
	 * machines that have stuff missing from the device tree */
	
	if (!device_is_compatible(busnode, "k2-i2c"))
		return -ENODEV;

	printk(KERN_DEBUG PFX "found k2-i2c, checking if onyx chip is on it\n");
	/* probe both possible addresses for the onyx chip */
	if (onyx_create(adapter, NULL, 0x46) == 0)
		return 0;
	return onyx_create(adapter, NULL, 0x47);
}

static int onyx_i2c_detach(struct i2c_client *client)
{
	struct onyx *onyx = container_of(client, struct onyx, i2c);
	int err;

	if ((err = i2c_detach_client(client)))
		return err;
	aoa_codec_unregister(&onyx->codec);
	of_node_put(onyx->codec.node);
	if (onyx->codec_info)
		kfree(onyx->codec_info);
	kfree(onyx);
	return 0;
}

static struct i2c_driver onyx_driver = {
	.driver = {
		.name = "aoa_codec_onyx",
		.owner = THIS_MODULE,
	},
	.attach_adapter = onyx_i2c_attach,
	.detach_client = onyx_i2c_detach,
};

static int __init onyx_init(void)
{
	return i2c_add_driver(&onyx_driver);
}

static void __exit onyx_exit(void)
{
	i2c_del_driver(&onyx_driver);
}

module_init(onyx_init);
module_exit(onyx_exit);
