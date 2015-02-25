/*
 * Regmap support for HD-audio verbs
 *
 * A virtual register is translated to one or more hda verbs for write,
 * vice versa for read.
 *
 * A few limitations:
 * - Provided for not all verbs but only subset standard non-volatile verbs.
 * - For reading, only AC_VERB_GET_* variants can be used.
 * - For writing, mapped to the *corresponding* AC_VERB_SET_* variants,
 *   so can't handle asymmetric verbs for read and write
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_regmap.h>

#ifdef CONFIG_PM
#define codec_is_running(codec)				\
	(atomic_read(&(codec)->in_pm) ||		\
	 !pm_runtime_suspended(&(codec)->dev))
#else
#define codec_is_running(codec)		true
#endif

#define get_verb(reg)	(((reg) >> 8) & 0xfff)

static bool hda_volatile_reg(struct device *dev, unsigned int reg)
{
	unsigned int verb = get_verb(reg);

	switch (verb) {
	case AC_VERB_GET_PROC_COEF:
	case AC_VERB_GET_COEF_INDEX:
	case AC_VERB_GET_PROC_STATE:
	case AC_VERB_GET_POWER_STATE:
	case AC_VERB_GET_PIN_SENSE:
	case AC_VERB_GET_HDMI_DIP_SIZE:
	case AC_VERB_GET_HDMI_ELDD:
	case AC_VERB_GET_HDMI_DIP_INDEX:
	case AC_VERB_GET_HDMI_DIP_DATA:
	case AC_VERB_GET_HDMI_DIP_XMIT:
	case AC_VERB_GET_HDMI_CP_CTRL:
	case AC_VERB_GET_HDMI_CHAN_SLOT:
	case AC_VERB_GET_DEVICE_SEL:
	case AC_VERB_GET_DEVICE_LIST:	/* read-only volatile */
		return true;
	}

	return false;
}

static bool hda_writeable_reg(struct device *dev, unsigned int reg)
{
	unsigned int verb = get_verb(reg);

	switch (verb & 0xf00) {
	case AC_VERB_GET_STREAM_FORMAT:
	case AC_VERB_GET_AMP_GAIN_MUTE:
		return true;
	case 0xf00:
		break;
	default:
		return false;
	}

	switch (verb) {
	case AC_VERB_GET_CONNECT_SEL:
	case AC_VERB_GET_SDI_SELECT:
	case AC_VERB_GET_CONV:
	case AC_VERB_GET_PIN_WIDGET_CONTROL:
	case AC_VERB_GET_UNSOLICITED_RESPONSE: /* only as SET_UNSOLICITED_ENABLE */
	case AC_VERB_GET_BEEP_CONTROL:
	case AC_VERB_GET_EAPD_BTLENABLE:
	case AC_VERB_GET_DIGI_CONVERT_1:
	case AC_VERB_GET_DIGI_CONVERT_2: /* only for beep control */
	case AC_VERB_GET_VOLUME_KNOB_CONTROL:
	case AC_VERB_GET_CONFIG_DEFAULT:
	case AC_VERB_GET_GPIO_MASK:
	case AC_VERB_GET_GPIO_DIRECTION:
	case AC_VERB_GET_GPIO_DATA: /* not for volatile read */
	case AC_VERB_GET_GPIO_WAKE_MASK:
	case AC_VERB_GET_GPIO_UNSOLICITED_RSP_MASK:
	case AC_VERB_GET_GPIO_STICKY_MASK:
	case AC_VERB_GET_CVT_CHAN_COUNT:
		return true;
	}

	return false;
}

static bool hda_readable_reg(struct device *dev, unsigned int reg)
{
	unsigned int verb = get_verb(reg);

	switch (verb) {
	case AC_VERB_PARAMETERS:
	case AC_VERB_GET_CONNECT_LIST:
	case AC_VERB_GET_SUBSYSTEM_ID:
		return true;
	}

	return hda_writeable_reg(dev, reg);
}

static int hda_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct hdac_device *codec = context;

	if (!codec_is_running(codec))
		return -EAGAIN;
	reg |= (codec->addr << 28);
	return snd_hdac_exec_verb(codec, reg, 0, val);
}

static int hda_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct hdac_device *codec = context;
	unsigned int verb;
	int i, bytes, err;

	if (!codec_is_running(codec))
		return codec->lazy_cache ? 0 : -EAGAIN;

	reg &= ~0x00080000U; /* drop GET bit */
	reg |= (codec->addr << 28);
	verb = get_verb(reg);

	switch (verb & 0xf00) {
	case AC_VERB_SET_AMP_GAIN_MUTE:
		verb = AC_VERB_SET_AMP_GAIN_MUTE;
		if (reg & AC_AMP_GET_LEFT)
			verb |= AC_AMP_SET_LEFT >> 8;
		else
			verb |= AC_AMP_SET_RIGHT >> 8;
		if (reg & AC_AMP_GET_OUTPUT) {
			verb |= AC_AMP_SET_OUTPUT >> 8;
		} else {
			verb |= AC_AMP_SET_INPUT >> 8;
			verb |= reg & 0xf;
		}
		break;
	}

	switch (verb) {
	case AC_VERB_SET_DIGI_CONVERT_1:
		bytes = 2;
		break;
	case AC_VERB_SET_CONFIG_DEFAULT_BYTES_0:
		bytes = 4;
		break;
	default:
		bytes = 1;
		break;
	}

	for (i = 0; i < bytes; i++) {
		reg &= ~0xfffff;
		reg |= (verb + i) << 8 | ((val >> (8 * i)) & 0xff);
		err = snd_hdac_exec_verb(codec, reg, 0, NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

static const struct regmap_config hda_regmap_cfg = {
	.name = "hdaudio",
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0xfffffff,
	.writeable_reg = hda_writeable_reg,
	.readable_reg = hda_readable_reg,
	.volatile_reg = hda_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.reg_read = hda_reg_read,
	.reg_write = hda_reg_write,
};

int snd_hdac_regmap_init(struct hdac_device *codec)
{
	struct regmap *regmap;

	regmap = regmap_init(&codec->dev, NULL, codec, &hda_regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	codec->regmap = regmap;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_init);

void snd_hdac_regmap_exit(struct hdac_device *codec)
{
	if (codec->regmap) {
		regmap_exit(codec->regmap);
		codec->regmap = NULL;
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_exit);

/*
 * helper functions
 */

/* write a pseudo-register value (w/o power sequence) */
static int reg_raw_write(struct hdac_device *codec, unsigned int reg,
			 unsigned int val)
{
	if (!codec->regmap)
		return hda_reg_write(codec, reg, val);
	else
		return regmap_write(codec->regmap, reg, val);
}

/**
 * snd_hdac_regmap_write_raw - write a pseudo register with power mgmt
 * @codec: the codec object
 * @reg: pseudo register
 * @val: value to write
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hdac_regmap_write_raw(struct hdac_device *codec, unsigned int reg,
			      unsigned int val)
{
	int err;

	err = reg_raw_write(codec, reg, val);
	if (err == -EAGAIN) {
		snd_hdac_power_up(codec);
		err = reg_raw_write(codec, reg, val);
		snd_hdac_power_down(codec);
	}
	return err;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_write_raw);

static int reg_raw_read(struct hdac_device *codec, unsigned int reg,
			unsigned int *val)
{
	if (!codec->regmap)
		return hda_reg_read(codec, reg, val);
	else
		return regmap_read(codec->regmap, reg, val);
}

/**
 * snd_hdac_regmap_read_raw - read a pseudo register with power mgmt
 * @codec: the codec object
 * @reg: pseudo register
 * @val: pointer to store the read value
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hdac_regmap_read_raw(struct hdac_device *codec, unsigned int reg,
			     unsigned int *val)
{
	int err;

	err = reg_raw_read(codec, reg, val);
	if (err == -EAGAIN) {
		snd_hdac_power_up(codec);
		err = reg_raw_read(codec, reg, val);
		snd_hdac_power_down(codec);
	}
	return err;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_read_raw);

/**
 * snd_hdac_regmap_update_raw - update a pseudo register with power mgmt
 * @codec: the codec object
 * @reg: pseudo register
 * @mask: bit mask to udpate
 * @val: value to update
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hdac_regmap_update_raw(struct hdac_device *codec, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	unsigned int orig;
	int err;

	val &= mask;
	err = snd_hdac_regmap_read_raw(codec, reg, &orig);
	if (err < 0)
		return err;
	val |= orig & ~mask;
	if (val == orig)
		return 0;
	err = snd_hdac_regmap_write_raw(codec, reg, val);
	if (err < 0)
		return err;
	return 1;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_update_raw);
