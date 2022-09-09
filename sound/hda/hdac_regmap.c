// SPDX-License-Identifier: GPL-2.0-only
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
#include "local.h"

static int codec_pm_lock(struct hdac_device *codec)
{
	return snd_hdac_keep_power_up(codec);
}

static void codec_pm_unlock(struct hdac_device *codec, int lock)
{
	if (lock == 1)
		snd_hdac_power_down_pm(codec);
}

#define get_verb(reg)	(((reg) >> 8) & 0xfff)

static bool hda_volatile_reg(struct device *dev, unsigned int reg)
{
	struct hdac_device *codec = dev_to_hdac_dev(dev);
	unsigned int verb = get_verb(reg);

	switch (verb) {
	case AC_VERB_GET_PROC_COEF:
		return !codec->cache_coef;
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
	struct hdac_device *codec = dev_to_hdac_dev(dev);
	unsigned int verb = get_verb(reg);
	const unsigned int *v;
	int i;

	snd_array_for_each(&codec->vendor_verbs, i, v) {
		if (verb == *v)
			return true;
	}

	if (codec->caps_overwriting)
		return true;

	switch (verb & 0xf00) {
	case AC_VERB_GET_STREAM_FORMAT:
	case AC_VERB_GET_AMP_GAIN_MUTE:
		return true;
	case AC_VERB_GET_PROC_COEF:
		return codec->cache_coef;
	case 0xf00:
		break;
	default:
		return false;
	}

	switch (verb) {
	case AC_VERB_GET_CONNECT_SEL:
	case AC_VERB_GET_SDI_SELECT:
	case AC_VERB_GET_PIN_WIDGET_CONTROL:
	case AC_VERB_GET_UNSOLICITED_RESPONSE: /* only as SET_UNSOLICITED_ENABLE */
	case AC_VERB_GET_BEEP_CONTROL:
	case AC_VERB_GET_EAPD_BTLENABLE:
	case AC_VERB_GET_DIGI_CONVERT_1:
	case AC_VERB_GET_DIGI_CONVERT_2: /* only for beep control */
	case AC_VERB_GET_VOLUME_KNOB_CONTROL:
	case AC_VERB_GET_GPIO_MASK:
	case AC_VERB_GET_GPIO_DIRECTION:
	case AC_VERB_GET_GPIO_DATA: /* not for volatile read */
	case AC_VERB_GET_GPIO_WAKE_MASK:
	case AC_VERB_GET_GPIO_UNSOLICITED_RSP_MASK:
	case AC_VERB_GET_GPIO_STICKY_MASK:
		return true;
	}

	return false;
}

static bool hda_readable_reg(struct device *dev, unsigned int reg)
{
	struct hdac_device *codec = dev_to_hdac_dev(dev);
	unsigned int verb = get_verb(reg);

	if (codec->caps_overwriting)
		return true;

	switch (verb) {
	case AC_VERB_PARAMETERS:
	case AC_VERB_GET_CONNECT_LIST:
	case AC_VERB_GET_SUBSYSTEM_ID:
		return true;
	/* below are basically writable, but disabled for reducing unnecessary
	 * writes at sync
	 */
	case AC_VERB_GET_CONFIG_DEFAULT: /* usually just read */
	case AC_VERB_GET_CONV: /* managed in PCM code */
	case AC_VERB_GET_CVT_CHAN_COUNT: /* managed in HDMI CA code */
		return true;
	}

	return hda_writeable_reg(dev, reg);
}

/*
 * Stereo amp pseudo register:
 * for making easier to handle the stereo volume control, we provide a
 * fake register to deal both left and right channels by a single
 * (pseudo) register access.  A verb consisting of SET_AMP_GAIN with
 * *both* SET_LEFT and SET_RIGHT bits takes a 16bit value, the lower 8bit
 * for the left and the upper 8bit for the right channel.
 */
static bool is_stereo_amp_verb(unsigned int reg)
{
	if (((reg >> 8) & 0x700) != AC_VERB_SET_AMP_GAIN_MUTE)
		return false;
	return (reg & (AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT)) ==
		(AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT);
}

/* read a pseudo stereo amp register (16bit left+right) */
static int hda_reg_read_stereo_amp(struct hdac_device *codec,
				   unsigned int reg, unsigned int *val)
{
	unsigned int left, right;
	int err;

	reg &= ~(AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT);
	err = snd_hdac_exec_verb(codec, reg | AC_AMP_GET_LEFT, 0, &left);
	if (err < 0)
		return err;
	err = snd_hdac_exec_verb(codec, reg | AC_AMP_GET_RIGHT, 0, &right);
	if (err < 0)
		return err;
	*val = left | (right << 8);
	return 0;
}

/* write a pseudo stereo amp register (16bit left+right) */
static int hda_reg_write_stereo_amp(struct hdac_device *codec,
				    unsigned int reg, unsigned int val)
{
	int err;
	unsigned int verb, left, right;

	verb = AC_VERB_SET_AMP_GAIN_MUTE << 8;
	if (reg & AC_AMP_GET_OUTPUT)
		verb |= AC_AMP_SET_OUTPUT;
	else
		verb |= AC_AMP_SET_INPUT | ((reg & 0xf) << 8);
	reg = (reg & ~0xfffff) | verb;

	left = val & 0xff;
	right = (val >> 8) & 0xff;
	if (left == right) {
		reg |= AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT;
		return snd_hdac_exec_verb(codec, reg | left, 0, NULL);
	}

	err = snd_hdac_exec_verb(codec, reg | AC_AMP_SET_LEFT | left, 0, NULL);
	if (err < 0)
		return err;
	err = snd_hdac_exec_verb(codec, reg | AC_AMP_SET_RIGHT | right, 0, NULL);
	if (err < 0)
		return err;
	return 0;
}

/* read a pseudo coef register (16bit) */
static int hda_reg_read_coef(struct hdac_device *codec, unsigned int reg,
			     unsigned int *val)
{
	unsigned int verb;
	int err;

	if (!codec->cache_coef)
		return -EINVAL;
	/* LSB 8bit = coef index */
	verb = (reg & ~0xfff00) | (AC_VERB_SET_COEF_INDEX << 8);
	err = snd_hdac_exec_verb(codec, verb, 0, NULL);
	if (err < 0)
		return err;
	verb = (reg & ~0xfffff) | (AC_VERB_GET_COEF_INDEX << 8);
	return snd_hdac_exec_verb(codec, verb, 0, val);
}

/* write a pseudo coef register (16bit) */
static int hda_reg_write_coef(struct hdac_device *codec, unsigned int reg,
			      unsigned int val)
{
	unsigned int verb;
	int err;

	if (!codec->cache_coef)
		return -EINVAL;
	/* LSB 8bit = coef index */
	verb = (reg & ~0xfff00) | (AC_VERB_SET_COEF_INDEX << 8);
	err = snd_hdac_exec_verb(codec, verb, 0, NULL);
	if (err < 0)
		return err;
	verb = (reg & ~0xfffff) | (AC_VERB_GET_COEF_INDEX << 8) |
		(val & 0xffff);
	return snd_hdac_exec_verb(codec, verb, 0, NULL);
}

static int hda_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct hdac_device *codec = context;
	int verb = get_verb(reg);
	int err;
	int pm_lock = 0;

	if (verb != AC_VERB_GET_POWER_STATE) {
		pm_lock = codec_pm_lock(codec);
		if (pm_lock < 0)
			return -EAGAIN;
	}
	reg |= (codec->addr << 28);
	if (is_stereo_amp_verb(reg)) {
		err = hda_reg_read_stereo_amp(codec, reg, val);
		goto out;
	}
	if (verb == AC_VERB_GET_PROC_COEF) {
		err = hda_reg_read_coef(codec, reg, val);
		goto out;
	}
	if ((verb & 0x700) == AC_VERB_SET_AMP_GAIN_MUTE)
		reg &= ~AC_AMP_FAKE_MUTE;

	err = snd_hdac_exec_verb(codec, reg, 0, val);
	if (err < 0)
		goto out;
	/* special handling for asymmetric reads */
	if (verb == AC_VERB_GET_POWER_STATE) {
		if (*val & AC_PWRST_ERROR)
			*val = -1;
		else /* take only the actual state */
			*val = (*val >> 4) & 0x0f;
	}
 out:
	codec_pm_unlock(codec, pm_lock);
	return err;
}

static int hda_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct hdac_device *codec = context;
	unsigned int verb;
	int i, bytes, err;
	int pm_lock = 0;

	if (codec->caps_overwriting)
		return 0;

	reg &= ~0x00080000U; /* drop GET bit */
	reg |= (codec->addr << 28);
	verb = get_verb(reg);

	if (verb != AC_VERB_SET_POWER_STATE) {
		pm_lock = codec_pm_lock(codec);
		if (pm_lock < 0)
			return codec->lazy_cache ? 0 : -EAGAIN;
	}

	if (is_stereo_amp_verb(reg)) {
		err = hda_reg_write_stereo_amp(codec, reg, val);
		goto out;
	}

	if (verb == AC_VERB_SET_PROC_COEF) {
		err = hda_reg_write_coef(codec, reg, val);
		goto out;
	}

	switch (verb & 0xf00) {
	case AC_VERB_SET_AMP_GAIN_MUTE:
		if ((reg & AC_AMP_FAKE_MUTE) && (val & AC_AMP_MUTE))
			val = 0;
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
			goto out;
	}

 out:
	codec_pm_unlock(codec, pm_lock);
	return err;
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
	.use_single_read = true,
	.use_single_write = true,
	.disable_locking = true,
};

/**
 * snd_hdac_regmap_init - Initialize regmap for HDA register accesses
 * @codec: the codec object
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_regmap_init(struct hdac_device *codec)
{
	struct regmap *regmap;

	regmap = regmap_init(&codec->dev, NULL, codec, &hda_regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	codec->regmap = regmap;
	snd_array_init(&codec->vendor_verbs, sizeof(unsigned int), 8);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_init);

/**
 * snd_hdac_regmap_exit - Release the regmap from HDA codec
 * @codec: the codec object
 */
void snd_hdac_regmap_exit(struct hdac_device *codec)
{
	if (codec->regmap) {
		regmap_exit(codec->regmap);
		codec->regmap = NULL;
		snd_array_free(&codec->vendor_verbs);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_exit);

/**
 * snd_hdac_regmap_add_vendor_verb - add a vendor-specific verb to regmap
 * @codec: the codec object
 * @verb: verb to allow accessing via regmap
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_regmap_add_vendor_verb(struct hdac_device *codec,
				    unsigned int verb)
{
	unsigned int *p = snd_array_new(&codec->vendor_verbs);

	if (!p)
		return -ENOMEM;
	*p = verb | 0x800; /* set GET bit */
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_add_vendor_verb);

/*
 * helper functions
 */

/* write a pseudo-register value (w/o power sequence) */
static int reg_raw_write(struct hdac_device *codec, unsigned int reg,
			 unsigned int val)
{
	int err;

	mutex_lock(&codec->regmap_lock);
	if (!codec->regmap)
		err = hda_reg_write(codec, reg, val);
	else
		err = regmap_write(codec->regmap, reg, val);
	mutex_unlock(&codec->regmap_lock);
	return err;
}

/* a helper macro to call @func_call; retry with power-up if failed */
#define CALL_RAW_FUNC(codec, func_call)				\
	({							\
		int _err = func_call;				\
		if (_err == -EAGAIN) {				\
			_err = snd_hdac_power_up_pm(codec);	\
			if (_err >= 0)				\
				_err = func_call;		\
			snd_hdac_power_down_pm(codec);		\
		}						\
		_err;})

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
	return CALL_RAW_FUNC(codec, reg_raw_write(codec, reg, val));
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_write_raw);

static int reg_raw_read(struct hdac_device *codec, unsigned int reg,
			unsigned int *val, bool uncached)
{
	int err;

	mutex_lock(&codec->regmap_lock);
	if (uncached || !codec->regmap)
		err = hda_reg_read(codec, reg, val);
	else
		err = regmap_read(codec->regmap, reg, val);
	mutex_unlock(&codec->regmap_lock);
	return err;
}

static int __snd_hdac_regmap_read_raw(struct hdac_device *codec,
				      unsigned int reg, unsigned int *val,
				      bool uncached)
{
	return CALL_RAW_FUNC(codec, reg_raw_read(codec, reg, val, uncached));
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
	return __snd_hdac_regmap_read_raw(codec, reg, val, false);
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_read_raw);

/* Works like snd_hdac_regmap_read_raw(), but this doesn't read from the
 * cache but always via hda verbs.
 */
int snd_hdac_regmap_read_raw_uncached(struct hdac_device *codec,
				      unsigned int reg, unsigned int *val)
{
	return __snd_hdac_regmap_read_raw(codec, reg, val, true);
}

static int reg_raw_update(struct hdac_device *codec, unsigned int reg,
			  unsigned int mask, unsigned int val)
{
	unsigned int orig;
	bool change;
	int err;

	mutex_lock(&codec->regmap_lock);
	if (codec->regmap) {
		err = regmap_update_bits_check(codec->regmap, reg, mask, val,
					       &change);
		if (!err)
			err = change ? 1 : 0;
	} else {
		err = hda_reg_read(codec, reg, &orig);
		if (!err) {
			val &= mask;
			val |= orig & ~mask;
			if (val != orig) {
				err = hda_reg_write(codec, reg, val);
				if (!err)
					err = 1;
			}
		}
	}
	mutex_unlock(&codec->regmap_lock);
	return err;
}

/**
 * snd_hdac_regmap_update_raw - update a pseudo register with power mgmt
 * @codec: the codec object
 * @reg: pseudo register
 * @mask: bit mask to update
 * @val: value to update
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hdac_regmap_update_raw(struct hdac_device *codec, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	return CALL_RAW_FUNC(codec, reg_raw_update(codec, reg, mask, val));
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_update_raw);

static int reg_raw_update_once(struct hdac_device *codec, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	unsigned int orig;
	int err;

	if (!codec->regmap)
		return reg_raw_update(codec, reg, mask, val);

	mutex_lock(&codec->regmap_lock);
	regcache_cache_only(codec->regmap, true);
	err = regmap_read(codec->regmap, reg, &orig);
	regcache_cache_only(codec->regmap, false);
	if (err < 0)
		err = regmap_update_bits(codec->regmap, reg, mask, val);
	mutex_unlock(&codec->regmap_lock);
	return err;
}

/**
 * snd_hdac_regmap_update_raw_once - initialize the register value only once
 * @codec: the codec object
 * @reg: pseudo register
 * @mask: bit mask to update
 * @val: value to update
 *
 * Performs the update of the register bits only once when the register
 * hasn't been initialized yet.  Used in HD-audio legacy driver.
 * Returns zero if successful or a negative error code
 */
int snd_hdac_regmap_update_raw_once(struct hdac_device *codec, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	return CALL_RAW_FUNC(codec, reg_raw_update_once(codec, reg, mask, val));
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_update_raw_once);

/**
 * snd_hdac_regmap_sync - sync out the cached values for PM resume
 * @codec: the codec object
 */
void snd_hdac_regmap_sync(struct hdac_device *codec)
{
	if (codec->regmap) {
		mutex_lock(&codec->regmap_lock);
		regcache_sync(codec->regmap);
		mutex_unlock(&codec->regmap_lock);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_regmap_sync);
