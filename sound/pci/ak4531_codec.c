// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Universal routines for AK4531 codec
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/ak4531_codec.h>
#include <sound/tlv.h>

/*
MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Universal routines for AK4531 codec");
MODULE_LICENSE("GPL");
*/

static void snd_ak4531_proc_init(struct snd_card *card, struct snd_ak4531 *ak4531);

/*
 *
 */
 
#if 0

static void snd_ak4531_dump(struct snd_ak4531 *ak4531)
{
	int idx;
	
	for (idx = 0; idx < 0x19; idx++)
		printk(KERN_DEBUG "ak4531 0x%x: 0x%x\n",
		       idx, ak4531->regs[idx]);
}

#endif

/*
 *
 */

#define AK4531_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_ak4531_info_single, \
  .get = snd_ak4531_get_single, .put = snd_ak4531_put_single, \
  .private_value = reg | (shift << 16) | (mask << 24) | (invert << 22) }
#define AK4531_SINGLE_TLV(xname, xindex, reg, shift, mask, invert, xtlv)    \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
  .name = xname, .index = xindex, \
  .info = snd_ak4531_info_single, \
  .get = snd_ak4531_get_single, .put = snd_ak4531_put_single, \
  .private_value = reg | (shift << 16) | (mask << 24) | (invert << 22), \
  .tlv = { .p = (xtlv) } }

static int snd_ak4531_info_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}
 
static int snd_ak4531_get_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int val;

	mutex_lock(&ak4531->reg_mutex);
	val = (ak4531->regs[reg] >> shift) & mask;
	mutex_unlock(&ak4531->reg_mutex);
	if (invert) {
		val = mask - val;
	}
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int snd_ak4531_put_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	int val;

	val = ucontrol->value.integer.value[0] & mask;
	if (invert) {
		val = mask - val;
	}
	val <<= shift;
	mutex_lock(&ak4531->reg_mutex);
	val = (ak4531->regs[reg] & ~(mask << shift)) | val;
	change = val != ak4531->regs[reg];
	ak4531->write(ak4531, reg, ak4531->regs[reg] = val);
	mutex_unlock(&ak4531->reg_mutex);
	return change;
}

#define AK4531_DOUBLE(xname, xindex, left_reg, right_reg, left_shift, right_shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_ak4531_info_double, \
  .get = snd_ak4531_get_double, .put = snd_ak4531_put_double, \
  .private_value = left_reg | (right_reg << 8) | (left_shift << 16) | (right_shift << 19) | (mask << 24) | (invert << 22) }
#define AK4531_DOUBLE_TLV(xname, xindex, left_reg, right_reg, left_shift, right_shift, mask, invert, xtlv) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
  .name = xname, .index = xindex, \
  .info = snd_ak4531_info_double, \
  .get = snd_ak4531_get_double, .put = snd_ak4531_put_double, \
  .private_value = left_reg | (right_reg << 8) | (left_shift << 16) | (right_shift << 19) | (mask << 24) | (invert << 22), \
  .tlv = { .p = (xtlv) } }

static int snd_ak4531_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}
 
static int snd_ak4531_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int left, right;

	mutex_lock(&ak4531->reg_mutex);
	left = (ak4531->regs[left_reg] >> left_shift) & mask;
	right = (ak4531->regs[right_reg] >> right_shift) & mask;
	mutex_unlock(&ak4531->reg_mutex);
	if (invert) {
		left = mask - left;
		right = mask - right;
	}
	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;
	return 0;
}

static int snd_ak4531_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	int left, right;

	left = ucontrol->value.integer.value[0] & mask;
	right = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		left = mask - left;
		right = mask - right;
	}
	left <<= left_shift;
	right <<= right_shift;
	mutex_lock(&ak4531->reg_mutex);
	if (left_reg == right_reg) {
		left = (ak4531->regs[left_reg] & ~((mask << left_shift) | (mask << right_shift))) | left | right;
		change = left != ak4531->regs[left_reg];
		ak4531->write(ak4531, left_reg, ak4531->regs[left_reg] = left);
	} else {
		left = (ak4531->regs[left_reg] & ~(mask << left_shift)) | left;
		right = (ak4531->regs[right_reg] & ~(mask << right_shift)) | right;
		change = left != ak4531->regs[left_reg] || right != ak4531->regs[right_reg];
		ak4531->write(ak4531, left_reg, ak4531->regs[left_reg] = left);
		ak4531->write(ak4531, right_reg, ak4531->regs[right_reg] = right);
	}
	mutex_unlock(&ak4531->reg_mutex);
	return change;
}

#define AK4531_INPUT_SW(xname, xindex, reg1, reg2, left_shift, right_shift) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_ak4531_info_input_sw, \
  .get = snd_ak4531_get_input_sw, .put = snd_ak4531_put_input_sw, \
  .private_value = reg1 | (reg2 << 8) | (left_shift << 16) | (right_shift << 24) }

static int snd_ak4531_info_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int snd_ak4531_get_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;

	mutex_lock(&ak4531->reg_mutex);
	ucontrol->value.integer.value[0] = (ak4531->regs[reg1] >> left_shift) & 1;
	ucontrol->value.integer.value[1] = (ak4531->regs[reg2] >> left_shift) & 1;
	ucontrol->value.integer.value[2] = (ak4531->regs[reg1] >> right_shift) & 1;
	ucontrol->value.integer.value[3] = (ak4531->regs[reg2] >> right_shift) & 1;
	mutex_unlock(&ak4531->reg_mutex);
	return 0;
}

static int snd_ak4531_put_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ak4531 *ak4531 = snd_kcontrol_chip(kcontrol);
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;
	int change;
	int val1, val2;

	mutex_lock(&ak4531->reg_mutex);
	val1 = ak4531->regs[reg1] & ~((1 << left_shift) | (1 << right_shift));
	val2 = ak4531->regs[reg2] & ~((1 << left_shift) | (1 << right_shift));
	val1 |= (ucontrol->value.integer.value[0] & 1) << left_shift;
	val2 |= (ucontrol->value.integer.value[1] & 1) << left_shift;
	val1 |= (ucontrol->value.integer.value[2] & 1) << right_shift;
	val2 |= (ucontrol->value.integer.value[3] & 1) << right_shift;
	change = val1 != ak4531->regs[reg1] || val2 != ak4531->regs[reg2];
	ak4531->write(ak4531, reg1, ak4531->regs[reg1] = val1);
	ak4531->write(ak4531, reg2, ak4531->regs[reg2] = val2);
	mutex_unlock(&ak4531->reg_mutex);
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_master, -6200, 200, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_mono, -2800, 400, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_input, -5000, 200, 0);

static const struct snd_kcontrol_new snd_ak4531_controls[] = {

AK4531_DOUBLE_TLV("Master Playback Switch", 0,
		  AK4531_LMASTER, AK4531_RMASTER, 7, 7, 1, 1,
		  db_scale_master),
AK4531_DOUBLE("Master Playback Volume", 0, AK4531_LMASTER, AK4531_RMASTER, 0, 0, 0x1f, 1),

AK4531_SINGLE_TLV("Master Mono Playback Switch", 0, AK4531_MONO_OUT, 7, 1, 1,
		  db_scale_mono),
AK4531_SINGLE("Master Mono Playback Volume", 0, AK4531_MONO_OUT, 0, 0x07, 1),

AK4531_DOUBLE("PCM Switch", 0, AK4531_LVOICE, AK4531_RVOICE, 7, 7, 1, 1),
AK4531_DOUBLE_TLV("PCM Volume", 0, AK4531_LVOICE, AK4531_RVOICE, 0, 0, 0x1f, 1,
		  db_scale_input),
AK4531_DOUBLE("PCM Playback Switch", 0, AK4531_OUT_SW2, AK4531_OUT_SW2, 3, 2, 1, 0),
AK4531_DOUBLE("PCM Capture Switch", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 2, 2, 1, 0),

AK4531_DOUBLE("PCM Switch", 1, AK4531_LFM, AK4531_RFM, 7, 7, 1, 1),
AK4531_DOUBLE_TLV("PCM Volume", 1, AK4531_LFM, AK4531_RFM, 0, 0, 0x1f, 1,
		  db_scale_input),
AK4531_DOUBLE("PCM Playback Switch", 1, AK4531_OUT_SW1, AK4531_OUT_SW1, 6, 5, 1, 0),
AK4531_INPUT_SW("PCM Capture Route", 1, AK4531_LIN_SW1, AK4531_RIN_SW1, 6, 5),

AK4531_DOUBLE("CD Switch", 0, AK4531_LCD, AK4531_RCD, 7, 7, 1, 1),
AK4531_DOUBLE_TLV("CD Volume", 0, AK4531_LCD, AK4531_RCD, 0, 0, 0x1f, 1,
		  db_scale_input),
AK4531_DOUBLE("CD Playback Switch", 0, AK4531_OUT_SW1, AK4531_OUT_SW1, 2, 1, 1, 0),
AK4531_INPUT_SW("CD Capture Route", 0, AK4531_LIN_SW1, AK4531_RIN_SW1, 2, 1),

AK4531_DOUBLE("Line Switch", 0, AK4531_LLINE, AK4531_RLINE, 7, 7, 1, 1),
AK4531_DOUBLE_TLV("Line Volume", 0, AK4531_LLINE, AK4531_RLINE, 0, 0, 0x1f, 1,
		  db_scale_input),
AK4531_DOUBLE("Line Playback Switch", 0, AK4531_OUT_SW1, AK4531_OUT_SW1, 4, 3, 1, 0),
AK4531_INPUT_SW("Line Capture Route", 0, AK4531_LIN_SW1, AK4531_RIN_SW1, 4, 3),

AK4531_DOUBLE("Aux Switch", 0, AK4531_LAUXA, AK4531_RAUXA, 7, 7, 1, 1),
AK4531_DOUBLE_TLV("Aux Volume", 0, AK4531_LAUXA, AK4531_RAUXA, 0, 0, 0x1f, 1,
		  db_scale_input),
AK4531_DOUBLE("Aux Playback Switch", 0, AK4531_OUT_SW2, AK4531_OUT_SW2, 5, 4, 1, 0),
AK4531_INPUT_SW("Aux Capture Route", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 4, 3),

AK4531_SINGLE("Mono Switch", 0, AK4531_MONO1, 7, 1, 1),
AK4531_SINGLE_TLV("Mono Volume", 0, AK4531_MONO1, 0, 0x1f, 1, db_scale_input),
AK4531_SINGLE("Mono Playback Switch", 0, AK4531_OUT_SW2, 0, 1, 0),
AK4531_DOUBLE("Mono Capture Switch", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 0, 0, 1, 0),

AK4531_SINGLE("Mono Switch", 1, AK4531_MONO2, 7, 1, 1),
AK4531_SINGLE_TLV("Mono Volume", 1, AK4531_MONO2, 0, 0x1f, 1, db_scale_input),
AK4531_SINGLE("Mono Playback Switch", 1, AK4531_OUT_SW2, 1, 1, 0),
AK4531_DOUBLE("Mono Capture Switch", 1, AK4531_LIN_SW2, AK4531_RIN_SW2, 1, 1, 1, 0),

AK4531_SINGLE_TLV("Mic Volume", 0, AK4531_MIC, 0, 0x1f, 1, db_scale_input),
AK4531_SINGLE("Mic Switch", 0, AK4531_MIC, 7, 1, 1),
AK4531_SINGLE("Mic Playback Switch", 0, AK4531_OUT_SW1, 0, 1, 0),
AK4531_DOUBLE("Mic Capture Switch", 0, AK4531_LIN_SW1, AK4531_RIN_SW1, 0, 0, 1, 0),

AK4531_DOUBLE("Mic Bypass Capture Switch", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 7, 7, 1, 0),
AK4531_DOUBLE("Mono1 Bypass Capture Switch", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 6, 6, 1, 0),
AK4531_DOUBLE("Mono2 Bypass Capture Switch", 0, AK4531_LIN_SW2, AK4531_RIN_SW2, 5, 5, 1, 0),

AK4531_SINGLE("AD Input Select", 0, AK4531_AD_IN, 0, 1, 0),
AK4531_SINGLE("Mic Boost (+30dB)", 0, AK4531_MIC_GAIN, 0, 1, 0)
};

static int snd_ak4531_free(struct snd_ak4531 *ak4531)
{
	if (ak4531) {
		if (ak4531->private_free)
			ak4531->private_free(ak4531);
		kfree(ak4531);
	}
	return 0;
}

static int snd_ak4531_dev_free(struct snd_device *device)
{
	struct snd_ak4531 *ak4531 = device->device_data;
	return snd_ak4531_free(ak4531);
}

static const u8 snd_ak4531_initial_map[0x19 + 1] = {
	0x9f,		/* 00: Master Volume Lch */
	0x9f,		/* 01: Master Volume Rch */
	0x9f,		/* 02: Voice Volume Lch */
	0x9f,		/* 03: Voice Volume Rch */
	0x9f,		/* 04: FM Volume Lch */
	0x9f,		/* 05: FM Volume Rch */
	0x9f,		/* 06: CD Audio Volume Lch */
	0x9f,		/* 07: CD Audio Volume Rch */
	0x9f,		/* 08: Line Volume Lch */
	0x9f,		/* 09: Line Volume Rch */
	0x9f,		/* 0a: Aux Volume Lch */
	0x9f,		/* 0b: Aux Volume Rch */
	0x9f,		/* 0c: Mono1 Volume */
	0x9f,		/* 0d: Mono2 Volume */
	0x9f,		/* 0e: Mic Volume */
	0x87,		/* 0f: Mono-out Volume */
	0x00,		/* 10: Output Mixer SW1 */
	0x00,		/* 11: Output Mixer SW2 */
	0x00,		/* 12: Lch Input Mixer SW1 */
	0x00,		/* 13: Rch Input Mixer SW1 */
	0x00,		/* 14: Lch Input Mixer SW2 */
	0x00,		/* 15: Rch Input Mixer SW2 */
	0x00,		/* 16: Reset & Power Down */
	0x00,		/* 17: Clock Select */
	0x00,		/* 18: AD Input Select */
	0x01		/* 19: Mic Amp Setup */
};

int snd_ak4531_mixer(struct snd_card *card,
		     struct snd_ak4531 *_ak4531,
		     struct snd_ak4531 **rak4531)
{
	unsigned int idx;
	int err;
	struct snd_ak4531 *ak4531;
	static const struct snd_device_ops ops = {
		.dev_free =	snd_ak4531_dev_free,
	};

	if (snd_BUG_ON(!card || !_ak4531))
		return -EINVAL;
	if (rak4531)
		*rak4531 = NULL;
	ak4531 = kzalloc(sizeof(*ak4531), GFP_KERNEL);
	if (ak4531 == NULL)
		return -ENOMEM;
	*ak4531 = *_ak4531;
	mutex_init(&ak4531->reg_mutex);
	if ((err = snd_component_add(card, "AK4531")) < 0) {
		snd_ak4531_free(ak4531);
		return err;
	}
	strcpy(card->mixername, "Asahi Kasei AK4531");
	ak4531->write(ak4531, AK4531_RESET, 0x03);	/* no RST, PD */
	udelay(100);
	ak4531->write(ak4531, AK4531_CLOCK, 0x00);	/* CODEC ADC and CODEC DAC use {LR,B}CLK2 and run off LRCLK2 PLL */
	for (idx = 0; idx <= 0x19; idx++) {
		if (idx == AK4531_RESET || idx == AK4531_CLOCK)
			continue;
		ak4531->write(ak4531, idx, ak4531->regs[idx] = snd_ak4531_initial_map[idx]);	/* recording source is mixer */
	}
	for (idx = 0; idx < ARRAY_SIZE(snd_ak4531_controls); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_ak4531_controls[idx], ak4531))) < 0) {
			snd_ak4531_free(ak4531);
			return err;
		}
	}
	snd_ak4531_proc_init(card, ak4531);
	if ((err = snd_device_new(card, SNDRV_DEV_CODEC, ak4531, &ops)) < 0) {
		snd_ak4531_free(ak4531);
		return err;
	}

#if 0
	snd_ak4531_dump(ak4531);
#endif
	if (rak4531)
		*rak4531 = ak4531;
	return 0;
}

/*
 * power management
 */
#ifdef CONFIG_PM
void snd_ak4531_suspend(struct snd_ak4531 *ak4531)
{
	/* mute */
	ak4531->write(ak4531, AK4531_LMASTER, 0x9f);
	ak4531->write(ak4531, AK4531_RMASTER, 0x9f);
	/* powerdown */
	ak4531->write(ak4531, AK4531_RESET, 0x01);
}

void snd_ak4531_resume(struct snd_ak4531 *ak4531)
{
	int idx;

	/* initialize */
	ak4531->write(ak4531, AK4531_RESET, 0x03);
	udelay(100);
	ak4531->write(ak4531, AK4531_CLOCK, 0x00);
	/* restore mixer registers */
	for (idx = 0; idx <= 0x19; idx++) {
		if (idx == AK4531_RESET || idx == AK4531_CLOCK)
			continue;
		ak4531->write(ak4531, idx, ak4531->regs[idx]);
	}
}
#endif

/*
 * /proc interface
 */

static void snd_ak4531_proc_read(struct snd_info_entry *entry, 
				 struct snd_info_buffer *buffer)
{
	struct snd_ak4531 *ak4531 = entry->private_data;

	snd_iprintf(buffer, "Asahi Kasei AK4531\n\n");
	snd_iprintf(buffer, "Recording source   : %s\n"
		    "MIC gain           : %s\n",
		    ak4531->regs[AK4531_AD_IN] & 1 ? "external" : "mixer",
		    ak4531->regs[AK4531_MIC_GAIN] & 1 ? "+30dB" : "+0dB");
}

static void
snd_ak4531_proc_init(struct snd_card *card, struct snd_ak4531 *ak4531)
{
	snd_card_ro_proc_new(card, "ak4531", ak4531, snd_ak4531_proc_read);
}
