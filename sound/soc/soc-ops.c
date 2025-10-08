// SPDX-License-Identifier: GPL-2.0+
//
// soc-ops.c  --  Generic ASoC operations
//
// Copyright 2005 Wolfson Microelectronics PLC.
// Copyright 2005 Openedhand Ltd.
// Copyright (C) 2010 Slimlogic Ltd.
// Copyright (C) 2010 Texas Instruments Inc.
//
// Author: Liam Girdwood <lrg@slimlogic.co.uk>
//         with code, comments and ideas from :-
//         Richard Purdie <richard@openedhand.com>

#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

/**
 * snd_soc_info_enum_double - enumerated double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double enumerated
 * mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_info_enum_double(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	return snd_ctl_enum_info(uinfo, e->shift_l == e->shift_r ? 1 : 2,
				 e->items, e->texts);
}
EXPORT_SYMBOL_GPL(snd_soc_info_enum_double);

/**
 * snd_soc_get_enum_double - enumerated double mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_get_enum_double(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, item;
	unsigned int reg_val;

	reg_val = snd_soc_component_read(component, e->reg);
	val = (reg_val >> e->shift_l) & e->mask;
	item = snd_soc_enum_val_to_item(e, val);
	ucontrol->value.enumerated.item[0] = item;
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		item = snd_soc_enum_val_to_item(e, val);
		ucontrol->value.enumerated.item[1] = item;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_enum_double);

/**
 * snd_soc_put_enum_double - enumerated double mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a double enumerated mixer.
 *
 * Returns 0 for success.
 */
int snd_soc_put_enum_double(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val;
	unsigned int mask;

	if (item[0] >= e->items)
		return -EINVAL;
	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (item[1] >= e->items)
			return -EINVAL;
		val |= snd_soc_enum_item_to_val(e, item[1]) << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	return snd_soc_component_update_bits(component, e->reg, mask, val);
}
EXPORT_SYMBOL_GPL(snd_soc_put_enum_double);

static int soc_mixer_reg_to_ctl(struct soc_mixer_control *mc, unsigned int reg_val,
				unsigned int mask, unsigned int shift, int max)
{
	int val = (reg_val >> shift) & mask;

	if (mc->sign_bit)
		val = sign_extend32(val, mc->sign_bit);

	val -= mc->min;

	if (mc->invert)
		val = max - val;

	return val & mask;
}

static unsigned int soc_mixer_ctl_to_reg(struct soc_mixer_control *mc, int val,
					 unsigned int mask, unsigned int shift,
					 int max)
{
	unsigned int reg_val;

	if (mc->invert)
		val = max - val;

	reg_val = val + mc->min;

	return (reg_val & mask) << shift;
}

static int soc_mixer_valid_ctl(struct soc_mixer_control *mc, long val, int max)
{
	if (val < 0)
		return -EINVAL;

	if (mc->platform_max && val > mc->platform_max)
		return -EINVAL;

	if (val > max)
		return -EINVAL;

	return 0;
}

static int soc_mixer_mask(struct soc_mixer_control *mc)
{
	if (mc->sign_bit)
		return GENMASK(mc->sign_bit, 0);
	else
		return GENMASK(fls(mc->max) - 1, 0);
}

static int soc_mixer_sx_mask(struct soc_mixer_control *mc)
{
	// min + max will take us 1-bit over the size of the mask
	return GENMASK(fls(mc->min + mc->max) - 2, 0);
}

static int soc_info_volsw(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo,
			  struct soc_mixer_control *mc, int max)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	if (max == 1) {
		/* Even two value controls ending in Volume should be integer */
		const char *vol_string = strstr(kcontrol->id.name, " Volume");

		if (!vol_string || strcmp(vol_string, " Volume"))
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	}

	if (mc->platform_max && mc->platform_max < max)
		max = mc->platform_max;

	uinfo->count = snd_soc_volsw_is_stereo(mc) ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;

	return 0;
}

static int soc_put_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol,
			 struct soc_mixer_control *mc, int mask, int max)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	unsigned int val1, val_mask;
	unsigned int val2 = 0;
	bool double_r = false;
	int ret;

	ret = soc_mixer_valid_ctl(mc, ucontrol->value.integer.value[0], max);
	if (ret)
		return ret;

	val1 = soc_mixer_ctl_to_reg(mc, ucontrol->value.integer.value[0],
				    mask, mc->shift, max);
	val_mask = mask << mc->shift;

	if (snd_soc_volsw_is_stereo(mc)) {
		ret = soc_mixer_valid_ctl(mc, ucontrol->value.integer.value[1], max);
		if (ret)
			return ret;

		if (mc->reg == mc->rreg) {
			val1 |= soc_mixer_ctl_to_reg(mc,
						     ucontrol->value.integer.value[1],
						     mask, mc->rshift, max);
			val_mask |= mask << mc->rshift;
		} else {
			val2 = soc_mixer_ctl_to_reg(mc,
						    ucontrol->value.integer.value[1],
						    mask, mc->shift, max);
			double_r = true;
		}
	}

	ret = snd_soc_component_update_bits(component, mc->reg, val_mask, val1);
	if (ret < 0)
		return ret;

	if (double_r) {
		int err = snd_soc_component_update_bits(component, mc->rreg,
							val_mask, val2);
		/* Don't drop change flag */
		if (err)
			return err;
	}

	return ret;
}

static int soc_get_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol,
			 struct soc_mixer_control *mc, int mask, int max)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	unsigned int reg_val;
	int val;

	reg_val = snd_soc_component_read(component, mc->reg);
	val = soc_mixer_reg_to_ctl(mc, reg_val, mask, mc->shift, max);

	ucontrol->value.integer.value[0] = val;

	if (snd_soc_volsw_is_stereo(mc)) {
		if (mc->reg == mc->rreg) {
			val = soc_mixer_reg_to_ctl(mc, reg_val, mask, mc->rshift, max);
		} else {
			reg_val = snd_soc_component_read(component, mc->rreg);
			val = soc_mixer_reg_to_ctl(mc, reg_val, mask, mc->shift, max);
		}

		ucontrol->value.integer.value[1] = val;
	}

	return 0;
}

/**
 * snd_soc_info_volsw - single mixer info callback with range.
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information, with a range, about a single mixer control,
 * or a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return soc_info_volsw(kcontrol, uinfo, mc, mc->max - mc->min);
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw);

/**
 * snd_soc_info_volsw_sx - Mixer info callback for SX TLV controls
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a single mixer control, or a double
 * mixer control that spans 2 registers of the SX TLV type. SX TLV controls
 * have a range that represents both positive and negative values either side
 * of zero but without a sign bit. min is the minimum register value, max is
 * the number of steps.
 *
 * Returns 0 for success.
 */
int snd_soc_info_volsw_sx(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return soc_info_volsw(kcontrol, uinfo, mc, mc->max);
}
EXPORT_SYMBOL_GPL(snd_soc_info_volsw_sx);

/**
 * snd_soc_get_volsw - single mixer get callback with range
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value, within a range, of a single mixer control, or a
 * double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mask = soc_mixer_mask(mc);

	return soc_get_volsw(kcontrol, ucontrol, mc, mask, mc->max - mc->min);
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw);

/**
 * snd_soc_put_volsw - single mixer put callback with range
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value , within a range, of a single mixer control, or
 * a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mask = soc_mixer_mask(mc);

	return soc_put_volsw(kcontrol, ucontrol, mc, mask, mc->max - mc->min);
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw);

/**
 * snd_soc_get_volsw_sx - single mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_get_volsw_sx(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mask = soc_mixer_sx_mask(mc);

	return soc_get_volsw(kcontrol, ucontrol, mc, mask, mc->max);
}
EXPORT_SYMBOL_GPL(snd_soc_get_volsw_sx);

/**
 * snd_soc_put_volsw_sx - double mixer set callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_put_volsw_sx(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mask = soc_mixer_sx_mask(mc);

	return soc_put_volsw(kcontrol, ucontrol, mc, mask, mc->max);
}
EXPORT_SYMBOL_GPL(snd_soc_put_volsw_sx);

static int snd_soc_clip_to_platform_max(struct snd_kcontrol *kctl)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;
	struct snd_ctl_elem_value *uctl;
	int ret;

	if (!mc->platform_max)
		return 0;

	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return -ENOMEM;

	ret = kctl->get(kctl, uctl);
	if (ret < 0)
		goto out;

	if (uctl->value.integer.value[0] > mc->platform_max)
		uctl->value.integer.value[0] = mc->platform_max;

	if (snd_soc_volsw_is_stereo(mc) &&
	    uctl->value.integer.value[1] > mc->platform_max)
		uctl->value.integer.value[1] = mc->platform_max;

	ret = kctl->put(kctl, uctl);

out:
	kfree(uctl);
	return ret;
}

/**
 * snd_soc_limit_volume - Set new limit to an existing volume control.
 *
 * @card: where to look for the control
 * @name: Name of the control
 * @max: new maximum limit
 *
 * Return 0 for success, else error.
 */
int snd_soc_limit_volume(struct snd_soc_card *card, const char *name, int max)
{
	struct snd_kcontrol *kctl;
	int ret = -EINVAL;

	/* Sanity check for name and max */
	if (unlikely(!name || max <= 0))
		return -EINVAL;

	kctl = snd_soc_card_get_kcontrol(card, name);
	if (kctl) {
		struct soc_mixer_control *mc =
			(struct soc_mixer_control *)kctl->private_value;

		if (max <= mc->max - mc->min) {
			mc->platform_max = max;
			ret = snd_soc_clip_to_platform_max(kctl);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_limit_volume);

int snd_soc_bytes_info(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes *params = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = params->num_regs * component->val_bytes;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_info);

int snd_soc_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes *params = (void *)kcontrol->private_value;
	int ret;

	if (component->regmap)
		ret = regmap_raw_read(component->regmap, params->base,
				      ucontrol->value.bytes.data,
				      params->num_regs * component->val_bytes);
	else
		ret = -EINVAL;

	/* Hide any masked bytes to ensure consistent data reporting */
	if (ret == 0 && params->mask) {
		switch (component->val_bytes) {
		case 1:
			ucontrol->value.bytes.data[0] &= ~params->mask;
			break;
		case 2:
			((u16 *)(&ucontrol->value.bytes.data))[0]
				&= cpu_to_be16(~params->mask);
			break;
		case 4:
			((u32 *)(&ucontrol->value.bytes.data))[0]
				&= cpu_to_be32(~params->mask);
			break;
		default:
			return -EINVAL;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_get);

int snd_soc_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_bytes *params = (void *)kcontrol->private_value;
	unsigned int val, mask;
	int ret, len;

	if (!component->regmap || !params->num_regs)
		return -EINVAL;

	len = params->num_regs * component->val_bytes;

	void *data __free(kfree) = kmemdup(ucontrol->value.bytes.data, len,
					   GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	/*
	 * If we've got a mask then we need to preserve the register
	 * bits.  We shouldn't modify the incoming data so take a
	 * copy.
	 */
	if (params->mask) {
		ret = regmap_read(component->regmap, params->base, &val);
		if (ret != 0)
			return ret;

		val &= params->mask;

		switch (component->val_bytes) {
		case 1:
			((u8 *)data)[0] &= ~params->mask;
			((u8 *)data)[0] |= val;
			break;
		case 2:
			mask = ~params->mask;
			ret = regmap_parse_val(component->regmap, &mask, &mask);
			if (ret != 0)
				return ret;

			((u16 *)data)[0] &= mask;

			ret = regmap_parse_val(component->regmap, &val, &val);
			if (ret != 0)
				return ret;

			((u16 *)data)[0] |= val;
			break;
		case 4:
			mask = ~params->mask;
			ret = regmap_parse_val(component->regmap, &mask, &mask);
			if (ret != 0)
				return ret;

			((u32 *)data)[0] &= mask;

			ret = regmap_parse_val(component->regmap, &val, &val);
			if (ret != 0)
				return ret;

			((u32 *)data)[0] |= val;
			break;
		default:
			return -EINVAL;
		}
	}

	return regmap_raw_write(component->regmap, params->base, data, len);
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_put);

int snd_soc_bytes_info_ext(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *ucontrol)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;

	ucontrol->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	ucontrol->count = params->max;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_info_ext);

int snd_soc_bytes_tlv_callback(struct snd_kcontrol *kcontrol, int op_flag,
			       unsigned int size, unsigned int __user *tlv)
{
	struct soc_bytes_ext *params = (void *)kcontrol->private_value;
	unsigned int count = size < params->max ? size : params->max;
	int ret = -ENXIO;

	switch (op_flag) {
	case SNDRV_CTL_TLV_OP_READ:
		if (params->get)
			ret = params->get(kcontrol, tlv, count);
		break;
	case SNDRV_CTL_TLV_OP_WRITE:
		if (params->put)
			ret = params->put(kcontrol, tlv, count);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_bytes_tlv_callback);

/**
 * snd_soc_info_xr_sx - signed multi register info callback
 * @kcontrol: mreg control
 * @uinfo: control element information
 *
 * Callback to provide information of a control that can span multiple
 * codec registers which together forms a single signed value. Note
 * that unlike the non-xr variant of sx controls these may or may not
 * include the sign bit, depending on nbits, and there is no shift.
 *
 * Returns 0 for success.
 */
int snd_soc_info_xr_sx(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo)
{
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = mc->min;
	uinfo->value.integer.max = mc->max;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_info_xr_sx);

/**
 * snd_soc_get_xr_sx - signed multi register get callback
 * @kcontrol: mreg control
 * @ucontrol: control element information
 *
 * Callback to get the value of a control that can span multiple codec
 * registers which together forms a single signed value. The control
 * supports specifying total no of bits used to allow for bitfields
 * across the multiple codec registers. Note that unlike the non-xr
 * variant of sx controls these may or may not include the sign bit,
 * depending on nbits, and there is no shift.
 *
 * Returns 0 for success.
 */
int snd_soc_get_xr_sx(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	unsigned int regbase = mc->regbase;
	unsigned int regcount = mc->regcount;
	unsigned int regwshift = component->val_bytes * BITS_PER_BYTE;
	unsigned int regwmask = GENMASK(regwshift - 1, 0);
	unsigned long mask = GENMASK(mc->nbits - 1, 0);
	long val = 0;
	unsigned int i;

	for (i = 0; i < regcount; i++) {
		unsigned int regval = snd_soc_component_read(component, regbase + i);

		val |= (regval & regwmask) << (regwshift * (regcount - i - 1));
	}
	val &= mask;
	if (mc->min < 0 && val > mc->max)
		val |= ~mask;
	if (mc->invert)
		val = mc->max - val;
	ucontrol->value.integer.value[0] = val;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_xr_sx);

/**
 * snd_soc_put_xr_sx - signed multi register get callback
 * @kcontrol: mreg control
 * @ucontrol: control element information
 *
 * Callback to set the value of a control that can span multiple codec
 * registers which together forms a single signed value. The control
 * supports specifying total no of bits used to allow for bitfields
 * across the multiple codec registers. Note that unlike the non-xr
 * variant of sx controls these may or may not include the sign bit,
 * depending on nbits, and there is no shift.
 *
 * Returns 0 for success.
 */
int snd_soc_put_xr_sx(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	unsigned int regbase = mc->regbase;
	unsigned int regcount = mc->regcount;
	unsigned int regwshift = component->val_bytes * BITS_PER_BYTE;
	unsigned int regwmask = GENMASK(regwshift - 1, 0);
	unsigned long mask = GENMASK(mc->nbits - 1, 0);
	long val = ucontrol->value.integer.value[0];
	int ret = 0;
	unsigned int i;

	if (val < mc->min || val > mc->max)
		return -EINVAL;
	if (mc->invert)
		val = mc->max - val;
	val &= mask;
	for (i = 0; i < regcount; i++) {
		unsigned int regval = (val >> (regwshift * (regcount - i - 1))) &
				      regwmask;
		unsigned int regmask = (mask >> (regwshift * (regcount - i - 1))) &
				       regwmask;
		int err = snd_soc_component_update_bits(component, regbase + i,
							regmask, regval);

		if (err < 0)
			return err;
		if (err > 0)
			ret = err;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_put_xr_sx);

/**
 * snd_soc_get_strobe - strobe get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback get the value of a strobe mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_get_strobe(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int invert = mc->invert != 0;
	unsigned int mask = BIT(mc->shift);
	unsigned int val;

	val = snd_soc_component_read(component, mc->reg);
	val &= mask;

	if (mc->shift != 0 && val != 0)
		val = val >> mc->shift;

	ucontrol->value.enumerated.item[0] = val ^ invert;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_get_strobe);

/**
 * snd_soc_put_strobe - strobe put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback strobe a register bit to high then low (or the inverse)
 * in one pass of a single mixer enum control.
 *
 * Returns 1 for success.
 */
int snd_soc_put_strobe(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int strobe = ucontrol->value.enumerated.item[0] != 0;
	unsigned int invert = mc->invert != 0;
	unsigned int mask = BIT(mc->shift);
	unsigned int val1 = (strobe ^ invert) ? mask : 0;
	unsigned int val2 = (strobe ^ invert) ? 0 : mask;
	int ret;

	ret = snd_soc_component_update_bits(component, mc->reg, mask, val1);
	if (ret < 0)
		return ret;

	return snd_soc_component_update_bits(component, mc->reg, mask, val2);
}
EXPORT_SYMBOL_GPL(snd_soc_put_strobe);
