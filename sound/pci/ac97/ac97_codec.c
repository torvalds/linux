// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/tlv.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include "ac97_id.h"

#include "ac97_patch.c"

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Universal interface for Audio Codec '97");
MODULE_LICENSE("GPL");

static bool enable_loopback;

module_param(enable_loopback, bool, 0444);
MODULE_PARM_DESC(enable_loopback, "Enable AC97 ADC/DAC Loopback Control");

#ifdef CONFIG_SND_AC97_POWER_SAVE
static int power_save = CONFIG_SND_AC97_POWER_SAVE_DEFAULT;
module_param(power_save, int, 0644);
MODULE_PARM_DESC(power_save, "Automatic power-saving timeout "
		 "(in second, 0 = disable).");
#endif
/*

 */

struct ac97_codec_id {
	unsigned int id;
	unsigned int mask;
	const char *name;
	int (*patch)(struct snd_ac97 *ac97);
	int (*mpatch)(struct snd_ac97 *ac97);
	unsigned int flags;
};

static const struct ac97_codec_id snd_ac97_codec_id_vendors[] = {
{ 0x41445300, 0xffffff00, "Analog Devices",	NULL,	NULL },
{ 0x414b4d00, 0xffffff00, "Asahi Kasei",	NULL,	NULL },
{ 0x414c4300, 0xffffff00, "Realtek",		NULL,	NULL },
{ 0x414c4700, 0xffffff00, "Realtek",		NULL,	NULL },
/*
 * This is an _inofficial_ Aztech Labs entry
 * (value might differ from unknown official Aztech ID),
 * currently used by the AC97 emulation of the almost-AC97 PCI168 card.
 */
{ 0x415a5400, 0xffffff00, "Aztech Labs (emulated)",	NULL,	NULL },
{ 0x434d4900, 0xffffff00, "C-Media Electronics", NULL,	NULL },
{ 0x43525900, 0xffffff00, "Cirrus Logic",	NULL,	NULL },
{ 0x43585400, 0xffffff00, "Conexant",           NULL,	NULL },
{ 0x44543000, 0xffffff00, "Diamond Technology", NULL,	NULL },
{ 0x454d4300, 0xffffff00, "eMicro",		NULL,	NULL },
{ 0x45838300, 0xffffff00, "ESS Technology",	NULL,	NULL },
{ 0x48525300, 0xffffff00, "Intersil",		NULL,	NULL },
{ 0x49434500, 0xffffff00, "ICEnsemble",		NULL,	NULL },
{ 0x49544500, 0xffffff00, "ITE Tech.Inc",	NULL,	NULL },
{ 0x4e534300, 0xffffff00, "National Semiconductor", NULL, NULL },
{ 0x50534300, 0xffffff00, "Philips",		NULL,	NULL },
{ 0x53494c00, 0xffffff00, "Silicon Laboratory",	NULL,	NULL },
{ 0x53544d00, 0xffffff00, "STMicroelectronics",	NULL,	NULL },
{ 0x54524100, 0xffffff00, "TriTech",		NULL,	NULL },
{ 0x54584e00, 0xffffff00, "Texas Instruments",	NULL,	NULL },
{ 0x56494100, 0xffffff00, "VIA Technologies",   NULL,	NULL },
{ 0x57454300, 0xffffff00, "Winbond",		NULL,	NULL },
{ 0x574d4c00, 0xffffff00, "Wolfson",		NULL,	NULL },
{ 0x594d4800, 0xffffff00, "Yamaha",		NULL,	NULL },
{ 0x83847600, 0xffffff00, "SigmaTel",		NULL,	NULL },
{ 0,	      0, 	  NULL,			NULL,	NULL }
};

static const struct ac97_codec_id snd_ac97_codec_ids[] = {
{ 0x41445303, 0xffffffff, "AD1819",		patch_ad1819,	NULL },
{ 0x41445340, 0xffffffff, "AD1881",		patch_ad1881,	NULL },
{ 0x41445348, 0xffffffff, "AD1881A",		patch_ad1881,	NULL },
{ 0x41445360, 0xffffffff, "AD1885",		patch_ad1885,	NULL },
{ 0x41445361, 0xffffffff, "AD1886",		patch_ad1886,	NULL },
{ 0x41445362, 0xffffffff, "AD1887",		patch_ad1881,	NULL },
{ 0x41445363, 0xffffffff, "AD1886A",		patch_ad1881,	NULL },
{ 0x41445368, 0xffffffff, "AD1888",		patch_ad1888,	NULL },
{ 0x41445370, 0xffffffff, "AD1980",		patch_ad1980,	NULL },
{ 0x41445372, 0xffffffff, "AD1981A",		patch_ad1981a,	NULL },
{ 0x41445374, 0xffffffff, "AD1981B",		patch_ad1981b,	NULL },
{ 0x41445375, 0xffffffff, "AD1985",		patch_ad1985,	NULL },
{ 0x41445378, 0xffffffff, "AD1986",		patch_ad1986,	NULL },
{ 0x414b4d00, 0xffffffff, "AK4540",		NULL,		NULL },
{ 0x414b4d01, 0xffffffff, "AK4542",		NULL,		NULL },
{ 0x414b4d02, 0xffffffff, "AK4543",		NULL,		NULL },
{ 0x414b4d06, 0xffffffff, "AK4544A",		NULL,		NULL },
{ 0x414b4d07, 0xffffffff, "AK4545",		NULL,		NULL },
{ 0x414c4300, 0xffffff00, "ALC100,100P", 	NULL,		NULL },
{ 0x414c4710, 0xfffffff0, "ALC200,200P",	NULL,		NULL },
{ 0x414c4721, 0xffffffff, "ALC650D",		NULL,	NULL }, /* already patched */
{ 0x414c4722, 0xffffffff, "ALC650E",		NULL,	NULL }, /* already patched */
{ 0x414c4723, 0xffffffff, "ALC650F",		NULL,	NULL }, /* already patched */
{ 0x414c4720, 0xfffffff0, "ALC650",		patch_alc650,	NULL },
{ 0x414c4730, 0xffffffff, "ALC101",		NULL,		NULL },
{ 0x414c4740, 0xfffffff0, "ALC202",		NULL,		NULL },
{ 0x414c4750, 0xfffffff0, "ALC250",		NULL,		NULL },
{ 0x414c4760, 0xfffffff0, "ALC655",		patch_alc655,	NULL },
{ 0x414c4770, 0xfffffff0, "ALC203",		patch_alc203,	NULL },
{ 0x414c4781, 0xffffffff, "ALC658D",		NULL,	NULL }, /* already patched */
{ 0x414c4780, 0xfffffff0, "ALC658",		patch_alc655,	NULL },
{ 0x414c4790, 0xfffffff0, "ALC850",		patch_alc850,	NULL },
{ 0x415a5401, 0xffffffff, "AZF3328",		patch_aztech_azf3328,	NULL },
{ 0x434d4941, 0xffffffff, "CMI9738",		patch_cm9738,	NULL },
{ 0x434d4961, 0xffffffff, "CMI9739",		patch_cm9739,	NULL },
{ 0x434d4969, 0xffffffff, "CMI9780",		patch_cm9780,	NULL },
{ 0x434d4978, 0xffffffff, "CMI9761A",		patch_cm9761,	NULL },
{ 0x434d4982, 0xffffffff, "CMI9761B",		patch_cm9761,	NULL },
{ 0x434d4983, 0xffffffff, "CMI9761A+",		patch_cm9761,	NULL },
{ 0x43525900, 0xfffffff8, "CS4297",		NULL,		NULL },
{ 0x43525910, 0xfffffff8, "CS4297A",		patch_cirrus_spdif,	NULL },
{ 0x43525920, 0xfffffff8, "CS4298",		patch_cirrus_spdif,		NULL },
{ 0x43525928, 0xfffffff8, "CS4294",		NULL,		NULL },
{ 0x43525930, 0xfffffff8, "CS4299",		patch_cirrus_cs4299,	NULL },
{ 0x43525948, 0xfffffff8, "CS4201",		NULL,		NULL },
{ 0x43525958, 0xfffffff8, "CS4205",		patch_cirrus_spdif,	NULL },
{ 0x43525960, 0xfffffff8, "CS4291",		NULL,		NULL },
{ 0x43525970, 0xfffffff8, "CS4202",		NULL,		NULL },
{ 0x43585421, 0xffffffff, "HSD11246",		NULL,		NULL },	// SmartMC II
{ 0x43585428, 0xfffffff8, "Cx20468",		patch_conexant,	NULL }, // SmartAMC fixme: the mask might be different
{ 0x43585430, 0xffffffff, "Cx20468-31",		patch_conexant, NULL },
{ 0x43585431, 0xffffffff, "Cx20551",           patch_cx20551,  NULL },
{ 0x44543031, 0xfffffff0, "DT0398",		NULL,		NULL },
{ 0x454d4328, 0xffffffff, "EM28028",		NULL,		NULL },  // same as TR28028?
{ 0x45838308, 0xffffffff, "ESS1988",		NULL,		NULL },
{ 0x48525300, 0xffffff00, "HMP9701",		NULL,		NULL },
{ 0x49434501, 0xffffffff, "ICE1230",		NULL,		NULL },
{ 0x49434511, 0xffffffff, "ICE1232",		NULL,		NULL }, // alias VIA VT1611A?
{ 0x49434514, 0xffffffff, "ICE1232A",		NULL,		NULL },
{ 0x49434551, 0xffffffff, "VT1616", 		patch_vt1616,	NULL }, 
{ 0x49434552, 0xffffffff, "VT1616i",		patch_vt1616,	NULL }, // VT1616 compatible (chipset integrated)
{ 0x49544520, 0xffffffff, "IT2226E",		NULL,		NULL },
{ 0x49544561, 0xffffffff, "IT2646E",		patch_it2646,	NULL },
{ 0x4e534300, 0xffffffff, "LM4540,43,45,46,48",	NULL,		NULL }, // only guess --jk
{ 0x4e534331, 0xffffffff, "LM4549",		NULL,		NULL },
{ 0x4e534350, 0xffffffff, "LM4550",		patch_lm4550,  	NULL }, // volume wrap fix 
{ 0x53494c20, 0xffffffe0, "Si3036,8",		mpatch_si3036,	mpatch_si3036, AC97_MODEM_PATCH },
{ 0x53544d02, 0xffffffff, "ST7597",		NULL,		NULL },
{ 0x54524102, 0xffffffff, "TR28022",		NULL,		NULL },
{ 0x54524103, 0xffffffff, "TR28023",		NULL,		NULL },
{ 0x54524106, 0xffffffff, "TR28026",		NULL,		NULL },
{ 0x54524108, 0xffffffff, "TR28028",		patch_tritech_tr28028,	NULL }, // added by xin jin [07/09/99]
{ 0x54524123, 0xffffffff, "TR28602",		NULL,		NULL }, // only guess --jk [TR28023 = eMicro EM28023 (new CT1297)]
{ 0x54584e03, 0xffffffff, "TLV320AIC27",	NULL,		NULL },
{ 0x54584e20, 0xffffffff, "TLC320AD9xC",	NULL,		NULL },
{ 0x56494120, 0xfffffff0, "VIA1613",		patch_vt1613,	NULL },
{ 0x56494161, 0xffffffff, "VIA1612A",		NULL,		NULL }, // modified ICE1232 with S/PDIF
{ 0x56494170, 0xffffffff, "VIA1617A",		patch_vt1617a,	NULL }, // modified VT1616 with S/PDIF
{ 0x56494182, 0xffffffff, "VIA1618",		patch_vt1618,   NULL },
{ 0x57454301, 0xffffffff, "W83971D",		NULL,		NULL },
{ 0x574d4c00, 0xffffffff, "WM9701,WM9701A",	NULL,		NULL },
{ 0x574d4C03, 0xffffffff, "WM9703,WM9707,WM9708,WM9717", patch_wolfson03, NULL},
{ 0x574d4C04, 0xffffffff, "WM9704M,WM9704Q",	patch_wolfson04, NULL},
{ 0x574d4C05, 0xffffffff, "WM9705,WM9710",	patch_wolfson05, NULL},
{ 0x574d4C09, 0xffffffff, "WM9709",		NULL,		NULL},
{ 0x574d4C12, 0xffffffff, "WM9711,WM9712,WM9715",	patch_wolfson11, NULL},
{ 0x574d4c13, 0xffffffff, "WM9713,WM9714",	patch_wolfson13, NULL, AC97_DEFAULT_POWER_OFF},
{ 0x594d4800, 0xffffffff, "YMF743",		patch_yamaha_ymf743,	NULL },
{ 0x594d4802, 0xffffffff, "YMF752",		NULL,		NULL },
{ 0x594d4803, 0xffffffff, "YMF753",		patch_yamaha_ymf753,	NULL },
{ 0x83847600, 0xffffffff, "STAC9700,83,84",	patch_sigmatel_stac9700,	NULL },
{ 0x83847604, 0xffffffff, "STAC9701,3,4,5",	NULL,		NULL },
{ 0x83847605, 0xffffffff, "STAC9704",		NULL,		NULL },
{ 0x83847608, 0xffffffff, "STAC9708,11",	patch_sigmatel_stac9708,	NULL },
{ 0x83847609, 0xffffffff, "STAC9721,23",	patch_sigmatel_stac9721,	NULL },
{ 0x83847644, 0xffffffff, "STAC9744",		patch_sigmatel_stac9744,	NULL },
{ 0x83847650, 0xffffffff, "STAC9750,51",	NULL,		NULL },	// patch?
{ 0x83847652, 0xffffffff, "STAC9752,53",	NULL,		NULL }, // patch?
{ 0x83847656, 0xffffffff, "STAC9756,57",	patch_sigmatel_stac9756,	NULL },
{ 0x83847658, 0xffffffff, "STAC9758,59",	patch_sigmatel_stac9758,	NULL },
{ 0x83847666, 0xffffffff, "STAC9766,67",	NULL,		NULL }, // patch?
{ 0, 	      0,	  NULL,			NULL,		NULL }
};


static void update_power_regs(struct snd_ac97 *ac97);
#ifdef CONFIG_SND_AC97_POWER_SAVE
#define ac97_is_power_save_mode(ac97) \
	((ac97->scaps & AC97_SCAP_POWER_SAVE) && power_save)
#else
#define ac97_is_power_save_mode(ac97) 0
#endif

#define ac97_err(ac97, fmt, args...)	\
	dev_err((ac97)->bus->card->dev, fmt, ##args)
#define ac97_warn(ac97, fmt, args...)	\
	dev_warn((ac97)->bus->card->dev, fmt, ##args)
#define ac97_dbg(ac97, fmt, args...)	\
	dev_dbg((ac97)->bus->card->dev, fmt, ##args)

/*
 *  I/O routines
 */

static int snd_ac97_valid_reg(struct snd_ac97 *ac97, unsigned short reg)
{
	/* filter some registers for buggy codecs */
	switch (ac97->id) {
	case AC97_ID_ST_AC97_ID4:
		if (reg == 0x08)
			return 0;
		fallthrough;
	case AC97_ID_ST7597:
		if (reg == 0x22 || reg == 0x7a)
			return 1;
		fallthrough;
	case AC97_ID_AK4540:
	case AC97_ID_AK4542:
		if (reg <= 0x1c || reg == 0x20 || reg == 0x26 || reg >= 0x7c)
			return 1;
		return 0;
	case AC97_ID_AD1819:	/* AD1819 */
	case AC97_ID_AD1881:	/* AD1881 */
	case AC97_ID_AD1881A:	/* AD1881A */
		if (reg >= 0x3a && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_AD1885:	/* AD1885 */
	case AC97_ID_AD1886:	/* AD1886 */
	case AC97_ID_AD1886A:	/* AD1886A - !!verify!! --jk */
	case AC97_ID_AD1887:	/* AD1887 - !!verify!! --jk */
		if (reg == 0x5a)
			return 1;
		if (reg >= 0x3c && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_STAC9700:
	case AC97_ID_STAC9704:
	case AC97_ID_STAC9705:
	case AC97_ID_STAC9708:
	case AC97_ID_STAC9721:
	case AC97_ID_STAC9744:
	case AC97_ID_STAC9756:
		if (reg <= 0x3a || reg >= 0x5a)
			return 1;
		return 0;
	}
	return 1;
}

/**
 * snd_ac97_write - write a value on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Writes a value on the given register.  This will invoke the write
 * callback directly after the register check.
 * This function doesn't change the register cache unlike
 * #snd_ca97_write_cache(), so use this only when you don't want to
 * reflect the change to the suspend/resume state.
 */
void snd_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	if ((ac97->id & 0xffffff00) == AC97_ID_ALC100) {
		/* Fix H/W bug of ALC100/100P */
		if (reg == AC97_MASTER || reg == AC97_HEADPHONE)
			ac97->bus->ops->write(ac97, AC97_RESET, 0);	/* reset audio codec */
	}
	ac97->bus->ops->write(ac97, reg, value);
}

EXPORT_SYMBOL(snd_ac97_write);

/**
 * snd_ac97_read - read a value from the given register
 * 
 * @ac97: the ac97 instance
 * @reg: the register to read
 *
 * Reads a value from the given register.  This will invoke the read
 * callback directly after the register check.
 *
 * Return: The read value.
 */
unsigned short snd_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return 0;
	return ac97->bus->ops->read(ac97, reg);
}

/* read a register - return the cached value if already read */
static inline unsigned short snd_ac97_read_cache(struct snd_ac97 *ac97, unsigned short reg)
{
	if (! test_bit(reg, ac97->reg_accessed)) {
		ac97->regs[reg] = ac97->bus->ops->read(ac97, reg);
		// set_bit(reg, ac97->reg_accessed);
	}
	return ac97->regs[reg];
}

EXPORT_SYMBOL(snd_ac97_read);

/**
 * snd_ac97_write_cache - write a value on the given register and update the cache
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Writes a value on the given register and updates the register
 * cache.  The cached values are used for the cached-read and the
 * suspend/resume.
 */
void snd_ac97_write_cache(struct snd_ac97 *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	mutex_lock(&ac97->reg_mutex);
	ac97->regs[reg] = value;
	ac97->bus->ops->write(ac97, reg, value);
	set_bit(reg, ac97->reg_accessed);
	mutex_unlock(&ac97->reg_mutex);
}

EXPORT_SYMBOL(snd_ac97_write_cache);

/**
 * snd_ac97_update - update the value on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @value: the value to set
 *
 * Compares the value with the register cache and updates the value
 * only when the value is changed.
 *
 * Return: 1 if the value is changed, 0 if no change, or a negative
 * code on failure.
 */
int snd_ac97_update(struct snd_ac97 *ac97, unsigned short reg, unsigned short value)
{
	int change;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	mutex_lock(&ac97->reg_mutex);
	change = ac97->regs[reg] != value;
	if (change) {
		ac97->regs[reg] = value;
		ac97->bus->ops->write(ac97, reg, value);
	}
	set_bit(reg, ac97->reg_accessed);
	mutex_unlock(&ac97->reg_mutex);
	return change;
}

EXPORT_SYMBOL(snd_ac97_update);

/**
 * snd_ac97_update_bits - update the bits on the given register
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @mask: the bit-mask to change
 * @value: the value to set
 *
 * Updates the masked-bits on the given register only when the value
 * is changed.
 *
 * Return: 1 if the bits are changed, 0 if no change, or a negative
 * code on failure.
 */
int snd_ac97_update_bits(struct snd_ac97 *ac97, unsigned short reg, unsigned short mask, unsigned short value)
{
	int change;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	mutex_lock(&ac97->reg_mutex);
	change = snd_ac97_update_bits_nolock(ac97, reg, mask, value);
	mutex_unlock(&ac97->reg_mutex);
	return change;
}

EXPORT_SYMBOL(snd_ac97_update_bits);

/* no lock version - see snd_ac97_update_bits() */
int snd_ac97_update_bits_nolock(struct snd_ac97 *ac97, unsigned short reg,
				unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	old = snd_ac97_read_cache(ac97, reg);
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		ac97->regs[reg] = new;
		ac97->bus->ops->write(ac97, reg, new);
	}
	set_bit(reg, ac97->reg_accessed);
	return change;
}

static int snd_ac97_ad18xx_update_pcm_bits(struct snd_ac97 *ac97, int codec, unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new, cfg;

	mutex_lock(&ac97->page_mutex);
	old = ac97->spec.ad18xx.pcmreg[codec];
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		mutex_lock(&ac97->reg_mutex);
		cfg = snd_ac97_read_cache(ac97, AC97_AD_SERIAL_CFG);
		ac97->spec.ad18xx.pcmreg[codec] = new;
		/* select single codec */
		ac97->bus->ops->write(ac97, AC97_AD_SERIAL_CFG,
				 (cfg & ~0x7000) |
				 ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
		/* update PCM bits */
		ac97->bus->ops->write(ac97, AC97_PCM, new);
		/* select all codecs */
		ac97->bus->ops->write(ac97, AC97_AD_SERIAL_CFG,
				 cfg | 0x7000);
		mutex_unlock(&ac97->reg_mutex);
	}
	mutex_unlock(&ac97->page_mutex);
	return change;
}

/*
 * Controls
 */

static int snd_ac97_info_enum_double(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	struct ac97_enum *e = (struct ac97_enum *)kcontrol->private_value;
	
	return snd_ctl_enum_info(uinfo, e->shift_l == e->shift_r ? 1 : 2,
				 e->mask, e->texts);
}

static int snd_ac97_get_enum_double(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	struct ac97_enum *e = (struct ac97_enum *)kcontrol->private_value;
	unsigned short val, bitmask;
	
	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	val = snd_ac97_read_cache(ac97, e->reg);
	ucontrol->value.enumerated.item[0] = (val >> e->shift_l) & (bitmask - 1);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] = (val >> e->shift_r) & (bitmask - 1);

	return 0;
}

static int snd_ac97_put_enum_double(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	struct ac97_enum *e = (struct ac97_enum *)kcontrol->private_value;
	unsigned short val;
	unsigned short mask, bitmask;
	
	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->mask - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->mask - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}
	return snd_ac97_update_bits(ac97, e->reg, mask, val);
}

/* save/restore ac97 v2.3 paging */
static int snd_ac97_page_save(struct snd_ac97 *ac97, int reg, struct snd_kcontrol *kcontrol)
{
	int page_save = -1;
	if ((kcontrol->private_value & (1<<25)) &&
	    (ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_23 &&
	    (reg >= 0x60 && reg < 0x70)) {
		unsigned short page = (kcontrol->private_value >> 26) & 0x0f;
		mutex_lock(&ac97->page_mutex); /* lock paging */
		page_save = snd_ac97_read(ac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
		snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page);
	}
	return page_save;
}

static void snd_ac97_page_restore(struct snd_ac97 *ac97, int page_save)
{
	if (page_save >= 0) {
		snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page_save);
		mutex_unlock(&ac97->page_mutex); /* unlock paging */
	}
}

/* volume and switch controls */
static int snd_ac97_info_volsw(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = shift == rshift ? 1 : 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_get_volsw(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	int page_save;

	page_save = snd_ac97_page_save(ac97, reg, kcontrol);
	ucontrol->value.integer.value[0] = (snd_ac97_read_cache(ac97, reg) >> shift) & mask;
	if (shift != rshift)
		ucontrol->value.integer.value[1] = (snd_ac97_read_cache(ac97, reg) >> rshift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		if (shift != rshift)
			ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	snd_ac97_page_restore(ac97, page_save);
	return 0;
}

static int snd_ac97_put_volsw(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	int err, page_save;
	unsigned short val, val2, val_mask;
	
	page_save = snd_ac97_page_save(ac97, reg, kcontrol);
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val_mask = mask << shift;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		if (invert)
			val2 = mask - val2;
		val_mask |= mask << rshift;
		val |= val2 << rshift;
	}
	err = snd_ac97_update_bits(ac97, reg, val_mask, val);
	snd_ac97_page_restore(ac97, page_save);
#ifdef CONFIG_SND_AC97_POWER_SAVE
	/* check analog mixer power-down */
	if ((val_mask & AC97_PD_EAPD) &&
	    (kcontrol->private_value & (1<<30))) {
		if (val & AC97_PD_EAPD)
			ac97->power_up &= ~(1 << (reg>>1));
		else
			ac97->power_up |= 1 << (reg>>1);
		update_power_regs(ac97);
	}
#endif
	return err;
}

static const struct snd_kcontrol_new snd_ac97_controls_tone[2] = {
AC97_SINGLE("Tone Control - Bass", AC97_MASTER_TONE, 8, 15, 1),
AC97_SINGLE("Tone Control - Treble", AC97_MASTER_TONE, 0, 15, 1)
};

static const struct snd_kcontrol_new snd_ac97_controls_pc_beep[2] = {
AC97_SINGLE("Beep Playback Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("Beep Playback Volume", AC97_PC_BEEP, 1, 15, 1)
};

static const struct snd_kcontrol_new snd_ac97_controls_mic_boost =
	AC97_SINGLE("Mic Boost (+20dB)", AC97_MIC, 6, 1, 0);


static const char* std_rec_sel[] = {"Mic", "CD", "Video", "Aux", "Line", "Mix", "Mix Mono", "Phone"};
static const char* std_3d_path[] = {"pre 3D", "post 3D"};
static const char* std_mix[] = {"Mix", "Mic"};
static const char* std_mic[] = {"Mic1", "Mic2"};

static const struct ac97_enum std_enum[] = {
AC97_ENUM_DOUBLE(AC97_REC_SEL, 8, 0, 8, std_rec_sel),
AC97_ENUM_SINGLE(AC97_GENERAL_PURPOSE, 15, 2, std_3d_path),
AC97_ENUM_SINGLE(AC97_GENERAL_PURPOSE, 9, 2, std_mix),
AC97_ENUM_SINGLE(AC97_GENERAL_PURPOSE, 8, 2, std_mic),
};

static const struct snd_kcontrol_new snd_ac97_control_capture_src = 
AC97_ENUM("Capture Source", std_enum[0]); 

static const struct snd_kcontrol_new snd_ac97_control_capture_vol =
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 15, 0);

static const struct snd_kcontrol_new snd_ac97_controls_mic_capture[2] = {
AC97_SINGLE("Mic Capture Switch", AC97_REC_GAIN_MIC, 15, 1, 1),
AC97_SINGLE("Mic Capture Volume", AC97_REC_GAIN_MIC, 0, 15, 0)
};

enum {
	AC97_GENERAL_PCM_OUT = 0,
	AC97_GENERAL_STEREO_ENHANCEMENT,
	AC97_GENERAL_3D,
	AC97_GENERAL_LOUDNESS,
	AC97_GENERAL_MONO,
	AC97_GENERAL_MIC,
	AC97_GENERAL_LOOPBACK
};

static const struct snd_kcontrol_new snd_ac97_controls_general[7] = {
AC97_ENUM("PCM Out Path & Mute", std_enum[1]),
AC97_SINGLE("Simulated Stereo Enhancement", AC97_GENERAL_PURPOSE, 14, 1, 0),
AC97_SINGLE("3D Control - Switch", AC97_GENERAL_PURPOSE, 13, 1, 0),
AC97_SINGLE("Loudness (bass boost)", AC97_GENERAL_PURPOSE, 12, 1, 0),
AC97_ENUM("Mono Output Select", std_enum[2]),
AC97_ENUM("Mic Select", std_enum[3]),
AC97_SINGLE("ADC/DAC Loopback", AC97_GENERAL_PURPOSE, 7, 1, 0)
};

static const struct snd_kcontrol_new snd_ac97_controls_3d[2] = {
AC97_SINGLE("3D Control - Center", AC97_3D_CONTROL, 8, 15, 0),
AC97_SINGLE("3D Control - Depth", AC97_3D_CONTROL, 0, 15, 0)
};

static const struct snd_kcontrol_new snd_ac97_controls_center[2] = {
AC97_SINGLE("Center Playback Switch", AC97_CENTER_LFE_MASTER, 7, 1, 1),
AC97_SINGLE("Center Playback Volume", AC97_CENTER_LFE_MASTER, 0, 31, 1)
};

static const struct snd_kcontrol_new snd_ac97_controls_lfe[2] = {
AC97_SINGLE("LFE Playback Switch", AC97_CENTER_LFE_MASTER, 15, 1, 1),
AC97_SINGLE("LFE Playback Volume", AC97_CENTER_LFE_MASTER, 8, 31, 1)
};

static const struct snd_kcontrol_new snd_ac97_control_eapd =
AC97_SINGLE("External Amplifier", AC97_POWERDOWN, 15, 1, 1);

static const struct snd_kcontrol_new snd_ac97_controls_modem_switches[2] = {
AC97_SINGLE("Off-hook Switch", AC97_GPIO_STATUS, 0, 1, 0),
AC97_SINGLE("Caller ID Switch", AC97_GPIO_STATUS, 2, 1, 0)
};

/* change the existing EAPD control as inverted */
static void set_inv_eapd(struct snd_ac97 *ac97, struct snd_kcontrol *kctl)
{
	kctl->private_value = AC97_SINGLE_VALUE(AC97_POWERDOWN, 15, 1, 0);
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, (1<<15), (1<<15)); /* EAPD up */
	ac97->scaps |= AC97_SCAP_INV_EAPD;
}

static int snd_ac97_spdif_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}
                        
static int snd_ac97_spdif_cmask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_CON_EMPHASIS_5015 |
					   IEC958_AES0_CON_NOT_COPYRIGHT;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_CATEGORY |
					   IEC958_AES1_CON_ORIGINAL;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	return 0;
}
                        
static int snd_ac97_spdif_pmask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	/* FIXME: AC'97 spec doesn't say which bits are used for what */
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_PRO_FS |
					   IEC958_AES0_PRO_EMPHASIS_5015;
	return 0;
}

static int snd_ac97_spdif_default_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ac97->reg_mutex);
	ucontrol->value.iec958.status[0] = ac97->spdif_status & 0xff;
	ucontrol->value.iec958.status[1] = (ac97->spdif_status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (ac97->spdif_status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (ac97->spdif_status >> 24) & 0xff;
	mutex_unlock(&ac97->reg_mutex);
	return 0;
}
                        
static int snd_ac97_spdif_default_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned int new = 0;
	unsigned short val = 0;
	int change;

	new = val = ucontrol->value.iec958.status[0] & (IEC958_AES0_PROFESSIONAL|IEC958_AES0_NONAUDIO);
	if (ucontrol->value.iec958.status[0] & IEC958_AES0_PROFESSIONAL) {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_PRO_FS|IEC958_AES0_PRO_EMPHASIS_5015);
		switch (new & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_44100: val |= 0<<12; break;
		case IEC958_AES0_PRO_FS_48000: val |= 2<<12; break;
		case IEC958_AES0_PRO_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
		if ((new & IEC958_AES0_PRO_EMPHASIS) == IEC958_AES0_PRO_EMPHASIS_5015)
			val |= 1<<3;
	} else {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_CON_EMPHASIS_5015|IEC958_AES0_CON_NOT_COPYRIGHT);
		new |= ((ucontrol->value.iec958.status[1] & (IEC958_AES1_CON_CATEGORY|IEC958_AES1_CON_ORIGINAL)) << 8);
		new |= ((ucontrol->value.iec958.status[3] & IEC958_AES3_CON_FS) << 24);
		if ((new & IEC958_AES0_CON_EMPHASIS) == IEC958_AES0_CON_EMPHASIS_5015)
			val |= 1<<3;
		if (!(new & IEC958_AES0_CON_NOT_COPYRIGHT))
			val |= 1<<2;
		val |= ((new >> 8) & 0xff) << 4;	// category + original
		switch ((new >> 24) & 0xff) {
		case IEC958_AES3_CON_FS_44100: val |= 0<<12; break;
		case IEC958_AES3_CON_FS_48000: val |= 2<<12; break;
		case IEC958_AES3_CON_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
	}

	mutex_lock(&ac97->reg_mutex);
	change = ac97->spdif_status != new;
	ac97->spdif_status = new;

	if (ac97->flags & AC97_CS_SPDIF) {
		int x = (val >> 12) & 0x03;
		switch (x) {
		case 0: x = 1; break;  // 44.1
		case 2: x = 0; break;  // 48.0
		default: x = 0; break; // illegal.
		}
		change |= snd_ac97_update_bits_nolock(ac97, AC97_CSR_SPDIF, 0x3fff, ((val & 0xcfff) | (x << 12)));
	} else if (ac97->flags & AC97_CX_SPDIF) {
		int v;
		v = new & (IEC958_AES0_CON_EMPHASIS_5015|IEC958_AES0_CON_NOT_COPYRIGHT) ? 0 : AC97_CXR_COPYRGT;
		v |= new & IEC958_AES0_NONAUDIO ? AC97_CXR_SPDIF_AC3 : AC97_CXR_SPDIF_PCM;
		change |= snd_ac97_update_bits_nolock(ac97, AC97_CXR_AUDIO_MISC, 
						      AC97_CXR_SPDIF_MASK | AC97_CXR_COPYRGT,
						      v);
	} else if (ac97->id == AC97_ID_YMF743) {
		change |= snd_ac97_update_bits_nolock(ac97,
						      AC97_YMF7X3_DIT_CTRL,
						      0xff38,
						      ((val << 4) & 0xff00) |
						      ((val << 2) & 0x0038));
	} else {
		unsigned short extst = snd_ac97_read_cache(ac97, AC97_EXTENDED_STATUS);
		snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0); /* turn off */

		change |= snd_ac97_update_bits_nolock(ac97, AC97_SPDIF, 0x3fff, val);
		if (extst & AC97_EA_SPDIF) {
			snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
                }
	}
	mutex_unlock(&ac97->reg_mutex);

	return change;
}

static int snd_ac97_put_spsa(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	// int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned short value, old, new;
	int change;

	value = (ucontrol->value.integer.value[0] & mask);

	mutex_lock(&ac97->reg_mutex);
	mask <<= shift;
	value <<= shift;
	old = snd_ac97_read_cache(ac97, reg);
	new = (old & ~mask) | value;
	change = old != new;

	if (change) {
		unsigned short extst = snd_ac97_read_cache(ac97, AC97_EXTENDED_STATUS);
		snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0); /* turn off */
		change = snd_ac97_update_bits_nolock(ac97, reg, mask, value);
		if (extst & AC97_EA_SPDIF)
			snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
	}
	mutex_unlock(&ac97->reg_mutex);
	return change;
}

static const struct snd_kcontrol_new snd_ac97_controls_spdif[5] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_cmask_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_pmask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		.info = snd_ac97_spdif_mask_info,
		.get = snd_ac97_spdif_default_get,
		.put = snd_ac97_spdif_default_put,
	},

	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),AC97_EXTENDED_STATUS, 2, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA",
		.info = snd_ac97_info_volsw,
		.get = snd_ac97_get_volsw,
		.put = snd_ac97_put_spsa,
		.private_value = AC97_SINGLE_VALUE(AC97_EXTENDED_STATUS, 4, 3, 0)
	},
};

#define AD18XX_PCM_BITS(xname, codec, lshift, rshift, mask) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_ad18xx_pcm_info_bits, \
  .get = snd_ac97_ad18xx_pcm_get_bits, .put = snd_ac97_ad18xx_pcm_put_bits, \
  .private_value = (codec) | ((lshift) << 8) | ((rshift) << 12) | ((mask) << 16) }

static int snd_ac97_ad18xx_pcm_info_bits(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int mask = (kcontrol->private_value >> 16) & 0x0f;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES))
		uinfo->count = 2;
	else
		uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_bits(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	
	ucontrol->value.integer.value[0] = mask - ((ac97->spec.ad18xx.pcmreg[codec] >> lshift) & mask);
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES))
		ucontrol->value.integer.value[1] = mask - ((ac97->spec.ad18xx.pcmreg[codec] >> rshift) & mask);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_bits(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int lshift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	unsigned short val, valmask;
	
	val = (mask - (ucontrol->value.integer.value[0] & mask)) << lshift;
	valmask = mask << lshift;
	if (lshift != rshift && (ac97->flags & AC97_STEREO_MUTES)) {
		val |= (mask - (ucontrol->value.integer.value[1] & mask)) << rshift;
		valmask |= mask << rshift;
	}
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, valmask, val);
}

#define AD18XX_PCM_VOLUME(xname, codec) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_ad18xx_pcm_info_volume, \
  .get = snd_ac97_ad18xx_pcm_get_volume, .put = snd_ac97_ad18xx_pcm_put_volume, \
  .private_value = codec }

static int snd_ac97_ad18xx_pcm_info_volume(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_volume(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	
	mutex_lock(&ac97->page_mutex);
	ucontrol->value.integer.value[0] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 8) & 31);
	ucontrol->value.integer.value[1] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 0) & 31);
	mutex_unlock(&ac97->page_mutex);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_volume(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	unsigned short val1, val2;
	
	val1 = 31 - (ucontrol->value.integer.value[0] & 31);
	val2 = 31 - (ucontrol->value.integer.value[1] & 31);
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, 0x1f1f, (val1 << 8) | val2);
}

static const struct snd_kcontrol_new snd_ac97_controls_ad18xx_pcm[2] = {
AD18XX_PCM_BITS("PCM Playback Switch", 0, 15, 7, 1),
AD18XX_PCM_VOLUME("PCM Playback Volume", 0)
};

static const struct snd_kcontrol_new snd_ac97_controls_ad18xx_surround[2] = {
AD18XX_PCM_BITS("Surround Playback Switch", 1, 15, 7, 1),
AD18XX_PCM_VOLUME("Surround Playback Volume", 1)
};

static const struct snd_kcontrol_new snd_ac97_controls_ad18xx_center[2] = {
AD18XX_PCM_BITS("Center Playback Switch", 2, 15, 15, 1),
AD18XX_PCM_BITS("Center Playback Volume", 2, 8, 8, 31)
};

static const struct snd_kcontrol_new snd_ac97_controls_ad18xx_lfe[2] = {
AD18XX_PCM_BITS("LFE Playback Switch", 2, 7, 7, 1),
AD18XX_PCM_BITS("LFE Playback Volume", 2, 0, 0, 31)
};

/*
 *
 */

static void snd_ac97_powerdown(struct snd_ac97 *ac97);

static int snd_ac97_bus_free(struct snd_ac97_bus *bus)
{
	if (bus) {
		snd_ac97_bus_proc_done(bus);
		kfree(bus->pcms);
		if (bus->private_free)
			bus->private_free(bus);
		kfree(bus);
	}
	return 0;
}

static int snd_ac97_bus_dev_free(struct snd_device *device)
{
	struct snd_ac97_bus *bus = device->device_data;
	return snd_ac97_bus_free(bus);
}

static int snd_ac97_free(struct snd_ac97 *ac97)
{
	if (ac97) {
#ifdef CONFIG_SND_AC97_POWER_SAVE
		cancel_delayed_work_sync(&ac97->power_work);
#endif
		snd_ac97_proc_done(ac97);
		if (ac97->bus)
			ac97->bus->codec[ac97->num] = NULL;
		if (ac97->private_free)
			ac97->private_free(ac97);
		kfree(ac97);
	}
	return 0;
}

static int snd_ac97_dev_free(struct snd_device *device)
{
	struct snd_ac97 *ac97 = device->device_data;
	snd_ac97_powerdown(ac97); /* for avoiding click noises during shut down */
	return snd_ac97_free(ac97);
}

static int snd_ac97_try_volume_mix(struct snd_ac97 * ac97, int reg)
{
	unsigned short val, mask = AC97_MUTE_MASK_MONO;

	if (! snd_ac97_valid_reg(ac97, reg))
		return 0;

	switch (reg) {
	case AC97_MASTER_TONE:
		return ac97->caps & AC97_BC_BASS_TREBLE ? 1 : 0;
	case AC97_HEADPHONE:
		return ac97->caps & AC97_BC_HEADPHONE ? 1 : 0;
	case AC97_REC_GAIN_MIC:
		return ac97->caps & AC97_BC_DEDICATED_MIC ? 1 : 0;
	case AC97_3D_CONTROL:
		if (ac97->caps & AC97_BC_3D_TECH_ID_MASK) {
			val = snd_ac97_read(ac97, reg);
			/* if nonzero - fixed and we can't set it */
			return val == 0;
		}
		return 0;
	case AC97_CENTER_LFE_MASTER:	/* center */
		if ((ac97->ext_id & AC97_EI_CDAC) == 0)
			return 0;
		break;
	case AC97_CENTER_LFE_MASTER+1:	/* lfe */
		if ((ac97->ext_id & AC97_EI_LDAC) == 0)
			return 0;
		reg = AC97_CENTER_LFE_MASTER;
		mask = 0x0080;
		break;
	case AC97_SURROUND_MASTER:
		if ((ac97->ext_id & AC97_EI_SDAC) == 0)
			return 0;
		break;
	}

	val = snd_ac97_read(ac97, reg);
	if (!(val & mask)) {
		/* nothing seems to be here - mute flag is not set */
		/* try another test */
		snd_ac97_write_cache(ac97, reg, val | mask);
		val = snd_ac97_read(ac97, reg);
		val = snd_ac97_read(ac97, reg);
		if (!(val & mask))
			return 0;	/* nothing here */
	}
	return 1;		/* success, useable */
}

static void check_volume_resolution(struct snd_ac97 *ac97, int reg, unsigned char *lo_max, unsigned char *hi_max)
{
	unsigned short cbit[3] = { 0x20, 0x10, 0x01 };
	unsigned char max[3] = { 63, 31, 15 };
	int i;

	/* first look up the static resolution table */
	if (ac97->res_table) {
		const struct snd_ac97_res_table *tbl;
		for (tbl = ac97->res_table; tbl->reg; tbl++) {
			if (tbl->reg == reg) {
				*lo_max = tbl->bits & 0xff;
				*hi_max = (tbl->bits >> 8) & 0xff;
				return;
			}
		}
	}

	*lo_max = *hi_max = 0;
	for (i = 0 ; i < ARRAY_SIZE(cbit); i++) {
		unsigned short val;
		snd_ac97_write(
			ac97, reg,
			AC97_MUTE_MASK_STEREO | cbit[i] | (cbit[i] << 8)
		);
		/* Do the read twice due to buffers on some ac97 codecs.
		 * e.g. The STAC9704 returns exactly what you wrote to the register
		 * if you read it immediately. This causes the detect routine to fail.
		 */
		val = snd_ac97_read(ac97, reg);
		val = snd_ac97_read(ac97, reg);
		if (! *lo_max && (val & 0x7f) == cbit[i])
			*lo_max = max[i];
		if (! *hi_max && ((val >> 8) & 0x7f) == cbit[i])
			*hi_max = max[i];
		if (*lo_max && *hi_max)
			break;
	}
}

static int snd_ac97_try_bit(struct snd_ac97 * ac97, int reg, int bit)
{
	unsigned short mask, val, orig, res;

	mask = 1 << bit;
	orig = snd_ac97_read(ac97, reg);
	val = orig ^ mask;
	snd_ac97_write(ac97, reg, val);
	res = snd_ac97_read(ac97, reg);
	snd_ac97_write_cache(ac97, reg, orig);
	return res == val;
}

/* check the volume resolution of center/lfe */
static void snd_ac97_change_volume_params2(struct snd_ac97 * ac97, int reg, int shift, unsigned char *max)
{
	unsigned short val, val1;

	*max = 63;
	val = AC97_MUTE_MASK_STEREO | (0x20 << shift);
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 31;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, AC97_MUTE_MASK_STEREO);
}

static inline int printable(unsigned int x)
{
	x &= 0xff;
	if (x < ' ' || x >= 0x71) {
		if (x <= 0x89)
			return x - 0x71 + 'A';
		return '?';
	}
	return x;
}

static struct snd_kcontrol *snd_ac97_cnew(const struct snd_kcontrol_new *_template,
					  struct snd_ac97 * ac97)
{
	struct snd_kcontrol_new template;
	memcpy(&template, _template, sizeof(template));
	template.index = ac97->num;
	return snd_ctl_new1(&template, ac97);
}

/*
 * create mute switch(es) for normal stereo controls
 */
static int snd_ac97_cmute_new_stereo(struct snd_card *card, char *name, int reg,
				     int check_stereo, int check_amix,
				     struct snd_ac97 *ac97)
{
	struct snd_kcontrol *kctl;
	int err;
	unsigned short val, val1, mute_mask;

	if (! snd_ac97_valid_reg(ac97, reg))
		return 0;

	mute_mask = AC97_MUTE_MASK_MONO;
	val = snd_ac97_read(ac97, reg);
	if (check_stereo || (ac97->flags & AC97_STEREO_MUTES)) {
		/* check whether both mute bits work */
		val1 = val | AC97_MUTE_MASK_STEREO;
		snd_ac97_write(ac97, reg, val1);
		if (val1 == snd_ac97_read(ac97, reg))
			mute_mask = AC97_MUTE_MASK_STEREO;
	}
	if (mute_mask == AC97_MUTE_MASK_STEREO) {
		struct snd_kcontrol_new tmp = AC97_DOUBLE(name, reg, 15, 7, 1, 1);
		if (check_amix)
			tmp.private_value |= (1 << 30);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	} else {
		struct snd_kcontrol_new tmp = AC97_SINGLE(name, reg, 15, 1, 1);
		if (check_amix)
			tmp.private_value |= (1 << 30);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	}
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return err;
	/* mute as default */
	snd_ac97_write_cache(ac97, reg, val | mute_mask);
	return 0;
}

/*
 * set dB information
 */
static const DECLARE_TLV_DB_SCALE(db_scale_4bit, -4500, 300, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_5bit, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_6bit, -9450, 150, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_5bit_12db_max, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_rec_gain, 0, 150, 0);

static const unsigned int *find_db_scale(unsigned int maxval)
{
	switch (maxval) {
	case 0x0f: return db_scale_4bit;
	case 0x1f: return db_scale_5bit;
	case 0x3f: return db_scale_6bit;
	}
	return NULL;
}

static void set_tlv_db_scale(struct snd_kcontrol *kctl, const unsigned int *tlv)
{
	kctl->tlv.p = tlv;
	if (tlv)
		kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
}

/*
 * create a volume for normal stereo/mono controls
 */
static int snd_ac97_cvol_new(struct snd_card *card, char *name, int reg, unsigned int lo_max,
			     unsigned int hi_max, struct snd_ac97 *ac97)
{
	int err;
	struct snd_kcontrol *kctl;

	if (! snd_ac97_valid_reg(ac97, reg))
		return 0;
	if (hi_max) {
		/* invert */
		struct snd_kcontrol_new tmp = AC97_DOUBLE(name, reg, 8, 0, lo_max, 1);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	} else {
		/* invert */
		struct snd_kcontrol_new tmp = AC97_SINGLE(name, reg, 0, lo_max, 1);
		tmp.index = ac97->num;
		kctl = snd_ctl_new1(&tmp, ac97);
	}
	if (!kctl)
		return -ENOMEM;
	if (reg >= AC97_PHONE && reg <= AC97_PCM)
		set_tlv_db_scale(kctl, db_scale_5bit_12db_max);
	else
		set_tlv_db_scale(kctl, find_db_scale(lo_max));
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return err;
	snd_ac97_write_cache(
		ac97, reg,
		(snd_ac97_read(ac97, reg) & AC97_MUTE_MASK_STEREO)
		| lo_max | (hi_max << 8)
	);
	return 0;
}

/*
 * create a mute-switch and a volume for normal stereo/mono controls
 */
static int snd_ac97_cmix_new_stereo(struct snd_card *card, const char *pfx,
				    int reg, int check_stereo, int check_amix,
				    struct snd_ac97 *ac97)
{
	int err;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	unsigned char lo_max, hi_max;

	if (! snd_ac97_valid_reg(ac97, reg))
		return 0;

	if (snd_ac97_try_bit(ac97, reg, 15)) {
		sprintf(name, "%s Switch", pfx);
		err = snd_ac97_cmute_new_stereo(card, name, reg,
						check_stereo, check_amix,
						ac97);
		if (err < 0)
			return err;
	}
	check_volume_resolution(ac97, reg, &lo_max, &hi_max);
	if (lo_max) {
		sprintf(name, "%s Volume", pfx);
		err = snd_ac97_cvol_new(card, name, reg, lo_max, hi_max, ac97);
		if (err < 0)
			return err;
	}
	return 0;
}

#define snd_ac97_cmix_new(card, pfx, reg, acheck, ac97) \
	snd_ac97_cmix_new_stereo(card, pfx, reg, 0, acheck, ac97)
#define snd_ac97_cmute_new(card, name, reg, acheck, ac97) \
	snd_ac97_cmute_new_stereo(card, name, reg, 0, acheck, ac97)

static unsigned int snd_ac97_determine_spdif_rates(struct snd_ac97 *ac97);

static int snd_ac97_mixer_build(struct snd_ac97 * ac97)
{
	struct snd_card *card = ac97->bus->card;
	struct snd_kcontrol *kctl;
	int err;
	unsigned int idx;
	unsigned char max;

	/* build master controls */
	/* AD claims to remove this control from AD1887, although spec v2.2 does not allow this */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER)) {
		if (ac97->flags & AC97_HAS_NO_MASTER_VOL)
			err = snd_ac97_cmute_new(card, "Master Playback Switch",
						 AC97_MASTER, 0, ac97);
		else
			err = snd_ac97_cmix_new(card, "Master Playback",
						AC97_MASTER, 0, ac97);
		if (err < 0)
			return err;
	}

	ac97->regs[AC97_CENTER_LFE_MASTER] = AC97_MUTE_MASK_STEREO;

	/* build center controls */
	if ((snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER)) 
		&& !(ac97->flags & AC97_AD_MULTI)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_center[0], ac97));
		if (err < 0)
			return err;
		err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_center[1], ac97));
		if (err < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 0, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		set_tlv_db_scale(kctl, find_db_scale(max));
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max);
	}

	/* build LFE controls */
	if ((snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER+1))
		&& !(ac97->flags & AC97_AD_MULTI)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_lfe[0], ac97));
		if (err < 0)
			return err;
		err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_lfe[1], ac97));
		if (err < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 8, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		set_tlv_db_scale(kctl, find_db_scale(max));
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max << 8);
	}

	/* build surround controls */
	if ((snd_ac97_try_volume_mix(ac97, AC97_SURROUND_MASTER)) 
		&& !(ac97->flags & AC97_AD_MULTI)) {
		/* Surround Master (0x38) is with stereo mutes */
		err = snd_ac97_cmix_new_stereo(card, "Surround Playback",
					       AC97_SURROUND_MASTER, 1, 0,
					       ac97);
		if (err < 0)
			return err;
	}

	/* build headphone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_HEADPHONE)) {
		err = snd_ac97_cmix_new(card, "Headphone Playback",
					AC97_HEADPHONE, 0, ac97);
		if (err < 0)
			return err;
	}
	
	/* build master mono controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_MONO)) {
		err = snd_ac97_cmix_new(card, "Master Mono Playback",
					AC97_MASTER_MONO, 0, ac97);
		if (err < 0)
			return err;
	}
	
	/* build master tone controls */
	if (!(ac97->flags & AC97_HAS_NO_TONE)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_TONE)) {
			for (idx = 0; idx < 2; idx++) {
				kctl = snd_ac97_cnew(&snd_ac97_controls_tone[idx], ac97);
				err = snd_ctl_add(card, kctl);
				if (err < 0)
					return err;
				if (ac97->id == AC97_ID_YMF743 ||
				    ac97->id == AC97_ID_YMF753) {
					kctl->private_value &= ~(0xff << 16);
					kctl->private_value |= 7 << 16;
				}
			}
			snd_ac97_write_cache(ac97, AC97_MASTER_TONE, 0x0f0f);
		}
	}
	
	/* build Beep controls */
	if (!(ac97->flags & AC97_HAS_NO_PC_BEEP) && 
		((ac97->flags & AC97_HAS_PC_BEEP) ||
	    snd_ac97_try_volume_mix(ac97, AC97_PC_BEEP))) {
		for (idx = 0; idx < 2; idx++) {
			kctl = snd_ac97_cnew(&snd_ac97_controls_pc_beep[idx], ac97);
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
		}
		set_tlv_db_scale(kctl, db_scale_4bit);
		snd_ac97_write_cache(
			ac97,
			AC97_PC_BEEP,
			(snd_ac97_read(ac97, AC97_PC_BEEP)
				| AC97_MUTE_MASK_MONO | 0x001e)
		);
	}
	
	/* build Phone controls */
	if (!(ac97->flags & AC97_HAS_NO_PHONE)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_PHONE)) {
			err = snd_ac97_cmix_new(card, "Phone Playback",
						AC97_PHONE, 1, ac97);
			if (err < 0)
				return err;
		}
	}
	
	/* build MIC controls */
	if (!(ac97->flags & AC97_HAS_NO_MIC)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_MIC)) {
			err = snd_ac97_cmix_new(card, "Mic Playback",
						AC97_MIC, 1, ac97);
			if (err < 0)
				return err;
			err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_mic_boost, ac97));
			if (err < 0)
				return err;
		}
	}

	/* build Line controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_LINE)) {
		err = snd_ac97_cmix_new(card, "Line Playback",
					AC97_LINE, 1, ac97);
		if (err < 0)
			return err;
	}
	
	/* build CD controls */
	if (!(ac97->flags & AC97_HAS_NO_CD)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_CD)) {
			err = snd_ac97_cmix_new(card, "CD Playback",
						AC97_CD, 1, ac97);
			if (err < 0)
				return err;
		}
	}
	
	/* build Video controls */
	if (!(ac97->flags & AC97_HAS_NO_VIDEO)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_VIDEO)) {
			err = snd_ac97_cmix_new(card, "Video Playback",
						AC97_VIDEO, 1, ac97);
			if (err < 0)
				return err;
		}
	}

	/* build Aux controls */
	if (!(ac97->flags & AC97_HAS_NO_AUX)) {
		if (snd_ac97_try_volume_mix(ac97, AC97_AUX)) {
			err = snd_ac97_cmix_new(card, "Aux Playback",
						AC97_AUX, 1, ac97);
			if (err < 0)
				return err;
		}
	}

	/* build PCM controls */
	if (ac97->flags & AC97_AD_MULTI) {
		unsigned short init_val;
		if (ac97->flags & AC97_STEREO_MUTES)
			init_val = 0x9f9f;
		else
			init_val = 0x9f1f;
		for (idx = 0; idx < 2; idx++) {
			kctl = snd_ac97_cnew(&snd_ac97_controls_ad18xx_pcm[idx], ac97);
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
		}
		set_tlv_db_scale(kctl, db_scale_5bit);
		ac97->spec.ad18xx.pcmreg[0] = init_val;
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC) {
			for (idx = 0; idx < 2; idx++) {
				kctl = snd_ac97_cnew(&snd_ac97_controls_ad18xx_surround[idx], ac97);
				err = snd_ctl_add(card, kctl);
				if (err < 0)
					return err;
			}
			set_tlv_db_scale(kctl, db_scale_5bit);
			ac97->spec.ad18xx.pcmreg[1] = init_val;
		}
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC) {
			for (idx = 0; idx < 2; idx++) {
				kctl = snd_ac97_cnew(&snd_ac97_controls_ad18xx_center[idx], ac97);
				err = snd_ctl_add(card, kctl);
				if (err < 0)
					return err;
			}
			set_tlv_db_scale(kctl, db_scale_5bit);
			for (idx = 0; idx < 2; idx++) {
				kctl = snd_ac97_cnew(&snd_ac97_controls_ad18xx_lfe[idx], ac97);
				err = snd_ctl_add(card, kctl);
				if (err < 0)
					return err;
			}
			set_tlv_db_scale(kctl, db_scale_5bit);
			ac97->spec.ad18xx.pcmreg[2] = init_val;
		}
		snd_ac97_write_cache(ac97, AC97_PCM, init_val);
	} else {
		if (!(ac97->flags & AC97_HAS_NO_STD_PCM)) {
			if (ac97->flags & AC97_HAS_NO_PCM_VOL)
				err = snd_ac97_cmute_new(card,
							 "PCM Playback Switch",
							 AC97_PCM, 0, ac97);
			else
				err = snd_ac97_cmix_new(card, "PCM Playback",
							AC97_PCM, 0, ac97);
			if (err < 0)
				return err;
		}
	}

	/* build Capture controls */
	if (!(ac97->flags & AC97_HAS_NO_REC_GAIN)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_control_capture_src, ac97));
		if (err < 0)
			return err;
		if (snd_ac97_try_bit(ac97, AC97_REC_GAIN, 15)) {
			err = snd_ac97_cmute_new(card, "Capture Switch",
						 AC97_REC_GAIN, 0, ac97);
			if (err < 0)
				return err;
		}
		kctl = snd_ac97_cnew(&snd_ac97_control_capture_vol, ac97);
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
		set_tlv_db_scale(kctl, db_scale_rec_gain);
		snd_ac97_write_cache(ac97, AC97_REC_SEL, 0x0000);
		snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x0000);
	}
	/* build MIC Capture controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_REC_GAIN_MIC)) {
		for (idx = 0; idx < 2; idx++) {
			kctl = snd_ac97_cnew(&snd_ac97_controls_mic_capture[idx], ac97);
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
		}
		set_tlv_db_scale(kctl, db_scale_rec_gain);
		snd_ac97_write_cache(ac97, AC97_REC_GAIN_MIC, 0x0000);
	}

	/* build PCM out path & mute control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 15)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_PCM_OUT], ac97));
		if (err < 0)
			return err;
	}

	/* build Simulated Stereo Enhancement control */
	if (ac97->caps & AC97_BC_SIM_STEREO) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_STEREO_ENHANCEMENT], ac97));
		if (err < 0)
			return err;
	}

	/* build 3D Stereo Enhancement control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 13)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_3D], ac97));
		if (err < 0)
			return err;
	}

	/* build Loudness control */
	if (ac97->caps & AC97_BC_LOUDNESS) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOUDNESS], ac97));
		if (err < 0)
			return err;
	}

	/* build Mono output select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 9)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MONO], ac97));
		if (err < 0)
			return err;
	}

	/* build Mic select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 8)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MIC], ac97));
		if (err < 0)
			return err;
	}

	/* build ADC/DAC loopback control */
	if (enable_loopback && snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 7)) {
		err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOOPBACK], ac97));
		if (err < 0)
			return err;
	}

	snd_ac97_update_bits(ac97, AC97_GENERAL_PURPOSE, ~AC97_GP_DRSS_MASK, 0x0000);

	/* build 3D controls */
	if (ac97->build_ops->build_3d) {
		ac97->build_ops->build_3d(ac97);
	} else {
		if (snd_ac97_try_volume_mix(ac97, AC97_3D_CONTROL)) {
			unsigned short val;
			val = 0x0707;
			snd_ac97_write(ac97, AC97_3D_CONTROL, val);
			val = snd_ac97_read(ac97, AC97_3D_CONTROL);
			val = val == 0x0606;
			kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97);
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (9 << 8) | (7 << 16);
			kctl = snd_ac97_cnew(&snd_ac97_controls_3d[1], ac97);
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (1 << 8) | (7 << 16);
			snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
		}
	}

	/* build S/PDIF controls */

	/* Hack for ASUS P5P800-VM, which does not indicate S/PDIF capability */
	if (ac97->subsystem_vendor == 0x1043 &&
	    ac97->subsystem_device == 0x810f)
		ac97->ext_id |= AC97_EI_SPDIF;

	if ((ac97->ext_id & AC97_EI_SPDIF) && !(ac97->scaps & AC97_SCAP_NO_SPDIF)) {
		if (ac97->build_ops->build_spdif) {
			err = ac97->build_ops->build_spdif(ac97);
			if (err < 0)
				return err;
		} else {
			for (idx = 0; idx < 5; idx++) {
				err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_spdif[idx], ac97));
				if (err < 0)
					return err;
			}
			if (ac97->build_ops->build_post_spdif) {
				err = ac97->build_ops->build_post_spdif(ac97);
				if (err < 0)
					return err;
			}
			/* set default PCM S/PDIF params */
			/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
			snd_ac97_write_cache(ac97, AC97_SPDIF, 0x2a20);
			ac97->rates[AC97_RATES_SPDIF] = snd_ac97_determine_spdif_rates(ac97);
		}
		ac97->spdif_status = SNDRV_PCM_DEFAULT_CON_SPDIF;
	}
	
	/* build chip specific controls */
	if (ac97->build_ops->build_specific) {
		err = ac97->build_ops->build_specific(ac97);
		if (err < 0)
			return err;
	}

	if (snd_ac97_try_bit(ac97, AC97_POWERDOWN, 15)) {
		kctl = snd_ac97_cnew(&snd_ac97_control_eapd, ac97);
		if (! kctl)
			return -ENOMEM;
		if (ac97->scaps & AC97_SCAP_INV_EAPD)
			set_inv_eapd(ac97, kctl);
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
	}

	return 0;
}

static int snd_ac97_modem_build(struct snd_card *card, struct snd_ac97 * ac97)
{
	int err, idx;

	/*
	ac97_dbg(ac97, "AC97_GPIO_CFG = %x\n",
	       snd_ac97_read(ac97,AC97_GPIO_CFG));
	*/
	snd_ac97_write(ac97, AC97_GPIO_CFG, 0xffff & ~(AC97_GPIO_LINE1_OH));
	snd_ac97_write(ac97, AC97_GPIO_POLARITY, 0xffff & ~(AC97_GPIO_LINE1_OH));
	snd_ac97_write(ac97, AC97_GPIO_STICKY, 0xffff);
	snd_ac97_write(ac97, AC97_GPIO_WAKEUP, 0x0);
	snd_ac97_write(ac97, AC97_MISC_AFE, 0x0);

	/* build modem switches */
	for (idx = 0; idx < ARRAY_SIZE(snd_ac97_controls_modem_switches); idx++) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_ac97_controls_modem_switches[idx], ac97));
		if (err < 0)
			return err;
	}

	/* build chip specific controls */
	if (ac97->build_ops->build_specific) {
		err = ac97->build_ops->build_specific(ac97);
		if (err < 0)
			return err;
	}

	return 0;
}

static int snd_ac97_test_rate(struct snd_ac97 *ac97, int reg, int shadow_reg, int rate)
{
	unsigned short val;
	unsigned int tmp;

	tmp = ((unsigned int)rate * ac97->bus->clock) / 48000;
	snd_ac97_write_cache(ac97, reg, tmp & 0xffff);
	if (shadow_reg)
		snd_ac97_write_cache(ac97, shadow_reg, tmp & 0xffff);
	val = snd_ac97_read(ac97, reg);
	return val == (tmp & 0xffff);
}

static void snd_ac97_determine_rates(struct snd_ac97 *ac97, int reg, int shadow_reg, unsigned int *r_result)
{
	unsigned int result = 0;
	unsigned short saved;

	if (ac97->bus->no_vra) {
		*r_result = SNDRV_PCM_RATE_48000;
		if ((ac97->flags & AC97_DOUBLE_RATE) &&
		    reg == AC97_PCM_FRONT_DAC_RATE)
			*r_result |= SNDRV_PCM_RATE_96000;
		return;
	}

	saved = snd_ac97_read(ac97, reg);
	if ((ac97->ext_id & AC97_EI_DRA) && reg == AC97_PCM_FRONT_DAC_RATE)
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS,
				     AC97_EA_DRA, 0);
	/* test a non-standard rate */
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 11000))
		result |= SNDRV_PCM_RATE_CONTINUOUS;
	/* let's try to obtain standard rates */
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 8000))
		result |= SNDRV_PCM_RATE_8000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 11025))
		result |= SNDRV_PCM_RATE_11025;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 16000))
		result |= SNDRV_PCM_RATE_16000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 22050))
		result |= SNDRV_PCM_RATE_22050;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 32000))
		result |= SNDRV_PCM_RATE_32000;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 44100))
		result |= SNDRV_PCM_RATE_44100;
	if (snd_ac97_test_rate(ac97, reg, shadow_reg, 48000))
		result |= SNDRV_PCM_RATE_48000;
	if ((ac97->flags & AC97_DOUBLE_RATE) &&
	    reg == AC97_PCM_FRONT_DAC_RATE) {
		/* test standard double rates */
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS,
				     AC97_EA_DRA, AC97_EA_DRA);
		if (snd_ac97_test_rate(ac97, reg, shadow_reg, 64000 / 2))
			result |= SNDRV_PCM_RATE_64000;
		if (snd_ac97_test_rate(ac97, reg, shadow_reg, 88200 / 2))
			result |= SNDRV_PCM_RATE_88200;
		if (snd_ac97_test_rate(ac97, reg, shadow_reg, 96000 / 2))
			result |= SNDRV_PCM_RATE_96000;
		/* some codecs don't support variable double rates */
		if (!snd_ac97_test_rate(ac97, reg, shadow_reg, 76100 / 2))
			result &= ~SNDRV_PCM_RATE_CONTINUOUS;
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS,
				     AC97_EA_DRA, 0);
	}
	/* restore the default value */
	snd_ac97_write_cache(ac97, reg, saved);
	if (shadow_reg)
		snd_ac97_write_cache(ac97, shadow_reg, saved);
	*r_result = result;
}

/* check AC97_SPDIF register to accept which sample rates */
static unsigned int snd_ac97_determine_spdif_rates(struct snd_ac97 *ac97)
{
	unsigned int result = 0;
	int i;
	static const unsigned short ctl_bits[] = {
		AC97_SC_SPSR_44K, AC97_SC_SPSR_32K, AC97_SC_SPSR_48K
	};
	static const unsigned int rate_bits[] = {
		SNDRV_PCM_RATE_44100, SNDRV_PCM_RATE_32000, SNDRV_PCM_RATE_48000
	};

	for (i = 0; i < (int)ARRAY_SIZE(ctl_bits); i++) {
		snd_ac97_update_bits(ac97, AC97_SPDIF, AC97_SC_SPSR_MASK, ctl_bits[i]);
		if ((snd_ac97_read(ac97, AC97_SPDIF) & AC97_SC_SPSR_MASK) == ctl_bits[i])
			result |= rate_bits[i];
	}
	return result;
}

/* look for the codec id table matching with the given id */
static const struct ac97_codec_id *look_for_codec_id(const struct ac97_codec_id *table,
						     unsigned int id)
{
	const struct ac97_codec_id *pid;

	for (pid = table; pid->id; pid++)
		if (pid->id == (id & pid->mask))
			return pid;
	return NULL;
}

void snd_ac97_get_name(struct snd_ac97 *ac97, unsigned int id, char *name, int modem)
{
	const struct ac97_codec_id *pid;

	sprintf(name, "0x%x %c%c%c", id,
		printable(id >> 24),
		printable(id >> 16),
		printable(id >> 8));
	pid = look_for_codec_id(snd_ac97_codec_id_vendors, id);
	if (! pid)
		return;

	strcpy(name, pid->name);
	if (ac97 && pid->patch) {
		if ((modem && (pid->flags & AC97_MODEM_PATCH)) ||
		    (! modem && ! (pid->flags & AC97_MODEM_PATCH)))
			pid->patch(ac97);
	} 

	pid = look_for_codec_id(snd_ac97_codec_ids, id);
	if (pid) {
		strcat(name, " ");
		strcat(name, pid->name);
		if (pid->mask != 0xffffffff)
			sprintf(name + strlen(name), " rev %d", id & ~pid->mask);
		if (ac97 && pid->patch) {
			if ((modem && (pid->flags & AC97_MODEM_PATCH)) ||
			    (! modem && ! (pid->flags & AC97_MODEM_PATCH)))
				pid->patch(ac97);
		}
	} else
		sprintf(name + strlen(name), " id %x", id & 0xff);
}

/**
 * snd_ac97_get_short_name - retrieve codec name
 * @ac97: the codec instance
 *
 * Return: The short identifying name of the codec.
 */
const char *snd_ac97_get_short_name(struct snd_ac97 *ac97)
{
	const struct ac97_codec_id *pid;

	for (pid = snd_ac97_codec_ids; pid->id; pid++)
		if (pid->id == (ac97->id & pid->mask))
			return pid->name;
	return "unknown codec";
}

EXPORT_SYMBOL(snd_ac97_get_short_name);

/* wait for a while until registers are accessible after RESET
 * return 0 if ok, negative not ready
 */
static int ac97_reset_wait(struct snd_ac97 *ac97, int timeout, int with_modem)
{
	unsigned long end_time;
	unsigned short val;

	end_time = jiffies + timeout;
	do {
		
		/* use preliminary reads to settle the communication */
		snd_ac97_read(ac97, AC97_RESET);
		snd_ac97_read(ac97, AC97_VENDOR_ID1);
		snd_ac97_read(ac97, AC97_VENDOR_ID2);
		/* modem? */
		if (with_modem) {
			val = snd_ac97_read(ac97, AC97_EXTENDED_MID);
			if (val != 0xffff && (val & 1) != 0)
				return 0;
		}
		if (ac97->scaps & AC97_SCAP_DETECT_BY_VENDOR) {
			/* probably only Xbox issue - all registers are read as zero */
			val = snd_ac97_read(ac97, AC97_VENDOR_ID1);
			if (val != 0 && val != 0xffff)
				return 0;
		} else {
			/* because the PCM or MASTER volume registers can be modified,
			 * the REC_GAIN register is used for tests
			 */
			/* test if we can write to the record gain volume register */
			snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x8a05);
			if ((snd_ac97_read(ac97, AC97_REC_GAIN) & 0x7fff) == 0x0a05)
				return 0;
		}
		schedule_timeout_uninterruptible(1);
	} while (time_after_eq(end_time, jiffies));
	return -ENODEV;
}

/**
 * snd_ac97_bus - create an AC97 bus component
 * @card: the card instance
 * @num: the bus number
 * @ops: the bus callbacks table
 * @private_data: private data pointer for the new instance
 * @rbus: the pointer to store the new AC97 bus instance.
 *
 * Creates an AC97 bus component.  An struct snd_ac97_bus instance is newly
 * allocated and initialized.
 *
 * The ops table must include valid callbacks (at least read and
 * write).  The other callbacks, wait and reset, are not mandatory.
 * 
 * The clock is set to 48000.  If another clock is needed, set
 * ``(*rbus)->clock`` manually.
 *
 * The AC97 bus instance is registered as a low-level device, so you don't
 * have to release it manually.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_ac97_bus(struct snd_card *card, int num,
		 const struct snd_ac97_bus_ops *ops,
		 void *private_data, struct snd_ac97_bus **rbus)
{
	int err;
	struct snd_ac97_bus *bus;
	static const struct snd_device_ops dev_ops = {
		.dev_free =	snd_ac97_bus_dev_free,
	};

	if (snd_BUG_ON(!card))
		return -EINVAL;
	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL)
		return -ENOMEM;
	bus->card = card;
	bus->num = num;
	bus->ops = ops;
	bus->private_data = private_data;
	bus->clock = 48000;
	spin_lock_init(&bus->bus_lock);
	snd_ac97_bus_proc_init(bus);
	err = snd_device_new(card, SNDRV_DEV_BUS, bus, &dev_ops);
	if (err < 0) {
		snd_ac97_bus_free(bus);
		return err;
	}
	if (rbus)
		*rbus = bus;
	return 0;
}

EXPORT_SYMBOL(snd_ac97_bus);

/* stop no dev release warning */
static void ac97_device_release(struct device * dev)
{
}

/* register ac97 codec to bus */
static int snd_ac97_dev_register(struct snd_device *device)
{
	struct snd_ac97 *ac97 = device->device_data;
	int err;

	ac97->dev.bus = &ac97_bus_type;
	ac97->dev.parent = ac97->bus->card->dev;
	ac97->dev.release = ac97_device_release;
	dev_set_name(&ac97->dev, "%d-%d:%s",
		     ac97->bus->card->number, ac97->num,
		     snd_ac97_get_short_name(ac97));
	err = device_register(&ac97->dev);
	if (err < 0) {
		ac97_err(ac97, "Can't register ac97 bus\n");
		put_device(&ac97->dev);
		ac97->dev.bus = NULL;
		return err;
	}
	return 0;
}

/* disconnect ac97 codec */
static int snd_ac97_dev_disconnect(struct snd_device *device)
{
	struct snd_ac97 *ac97 = device->device_data;
	if (ac97->dev.bus)
		device_unregister(&ac97->dev);
	return 0;
}

/* build_ops to do nothing */
static const struct snd_ac97_build_ops null_build_ops;

#ifdef CONFIG_SND_AC97_POWER_SAVE
static void do_update_power(struct work_struct *work)
{
	update_power_regs(
		container_of(work, struct snd_ac97, power_work.work));
}
#endif

/**
 * snd_ac97_mixer - create an Codec97 component
 * @bus: the AC97 bus which codec is attached to
 * @template: the template of ac97, including index, callbacks and
 *         the private data.
 * @rac97: the pointer to store the new ac97 instance.
 *
 * Creates an Codec97 component.  An struct snd_ac97 instance is newly
 * allocated and initialized from the template.  The codec
 * is then initialized by the standard procedure.
 *
 * The template must include the codec number (num) and address (addr),
 * and the private data (private_data).
 * 
 * The ac97 instance is registered as a low-level device, so you don't
 * have to release it manually.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_ac97_mixer(struct snd_ac97_bus *bus, struct snd_ac97_template *template, struct snd_ac97 **rac97)
{
	int err;
	struct snd_ac97 *ac97;
	struct snd_card *card;
	char name[64];
	unsigned long end_time;
	unsigned int reg;
	const struct ac97_codec_id *pid;
	static const struct snd_device_ops ops = {
		.dev_free =	snd_ac97_dev_free,
		.dev_register =	snd_ac97_dev_register,
		.dev_disconnect =	snd_ac97_dev_disconnect,
	};

	if (snd_BUG_ON(!bus || !template || !rac97))
		return -EINVAL;
	*rac97 = NULL;
	if (snd_BUG_ON(template->num >= 4))
		return -EINVAL;
	if (bus->codec[template->num])
		return -EBUSY;

	card = bus->card;
	ac97 = kzalloc(sizeof(*ac97), GFP_KERNEL);
	if (ac97 == NULL)
		return -ENOMEM;
	ac97->private_data = template->private_data;
	ac97->private_free = template->private_free;
	ac97->bus = bus;
	ac97->pci = template->pci;
	ac97->num = template->num;
	ac97->addr = template->addr;
	ac97->scaps = template->scaps;
	ac97->res_table = template->res_table;
	bus->codec[ac97->num] = ac97;
	mutex_init(&ac97->reg_mutex);
	mutex_init(&ac97->page_mutex);
#ifdef CONFIG_SND_AC97_POWER_SAVE
	INIT_DELAYED_WORK(&ac97->power_work, do_update_power);
#endif

#ifdef CONFIG_PCI
	if (ac97->pci) {
		pci_read_config_word(ac97->pci, PCI_SUBSYSTEM_VENDOR_ID, &ac97->subsystem_vendor);
		pci_read_config_word(ac97->pci, PCI_SUBSYSTEM_ID, &ac97->subsystem_device);
	}
#endif
	if (bus->ops->reset) {
		bus->ops->reset(ac97);
		goto __access_ok;
	}

	ac97->id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	ac97->id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if (ac97->id && ac97->id != (unsigned int)-1) {
		pid = look_for_codec_id(snd_ac97_codec_ids, ac97->id);
		if (pid && (pid->flags & AC97_DEFAULT_POWER_OFF))
			goto __access_ok;
	}

	/* reset to defaults */
	if (!(ac97->scaps & AC97_SCAP_SKIP_AUDIO))
		snd_ac97_write(ac97, AC97_RESET, 0);
	if (!(ac97->scaps & AC97_SCAP_SKIP_MODEM))
		snd_ac97_write(ac97, AC97_EXTENDED_MID, 0);
	if (bus->ops->wait)
		bus->ops->wait(ac97);
	else {
		udelay(50);
		if (ac97->scaps & AC97_SCAP_SKIP_AUDIO)
			err = ac97_reset_wait(ac97, msecs_to_jiffies(500), 1);
		else {
			err = ac97_reset_wait(ac97, msecs_to_jiffies(500), 0);
			if (err < 0)
				err = ac97_reset_wait(ac97,
						      msecs_to_jiffies(500), 1);
		}
		if (err < 0) {
			ac97_warn(ac97, "AC'97 %d does not respond - RESET\n",
				 ac97->num);
			/* proceed anyway - it's often non-critical */
		}
	}
      __access_ok:
	ac97->id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	ac97->id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if (! (ac97->scaps & AC97_SCAP_DETECT_BY_VENDOR) &&
	    (ac97->id == 0x00000000 || ac97->id == 0xffffffff)) {
		ac97_err(ac97,
			 "AC'97 %d access is not valid [0x%x], removing mixer.\n",
			 ac97->num, ac97->id);
		snd_ac97_free(ac97);
		return -EIO;
	}
	pid = look_for_codec_id(snd_ac97_codec_ids, ac97->id);
	if (pid)
		ac97->flags |= pid->flags;
	
	/* test for AC'97 */
	if (!(ac97->scaps & AC97_SCAP_SKIP_AUDIO) && !(ac97->scaps & AC97_SCAP_AUDIO)) {
		/* test if we can write to the record gain volume register */
		snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x8a06);
		err = snd_ac97_read(ac97, AC97_REC_GAIN);
		if ((err & 0x7fff) == 0x0a06)
			ac97->scaps |= AC97_SCAP_AUDIO;
	}
	if (ac97->scaps & AC97_SCAP_AUDIO) {
		ac97->caps = snd_ac97_read(ac97, AC97_RESET);
		ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
		if (ac97->ext_id == 0xffff)	/* invalid combination */
			ac97->ext_id = 0;
	}

	/* test for MC'97 */
	if (!(ac97->scaps & AC97_SCAP_SKIP_MODEM) && !(ac97->scaps & AC97_SCAP_MODEM)) {
		ac97->ext_mid = snd_ac97_read(ac97, AC97_EXTENDED_MID);
		if (ac97->ext_mid == 0xffff)	/* invalid combination */
			ac97->ext_mid = 0;
		if (ac97->ext_mid & 1)
			ac97->scaps |= AC97_SCAP_MODEM;
	}

	if (!ac97_is_audio(ac97) && !ac97_is_modem(ac97)) {
		if (!(ac97->scaps & (AC97_SCAP_SKIP_AUDIO|AC97_SCAP_SKIP_MODEM)))
			ac97_err(ac97,
				 "AC'97 %d access error (not audio or modem codec)\n",
				 ac97->num);
		snd_ac97_free(ac97);
		return -EACCES;
	}

	if (bus->ops->reset) // FIXME: always skipping?
		goto __ready_ok;

	/* FIXME: add powerdown control */
	if (ac97_is_audio(ac97)) {
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0);
		if (! (ac97->flags & AC97_DEFAULT_POWER_OFF)) {
			snd_ac97_write_cache(ac97, AC97_RESET, 0); /* reset to defaults */
			udelay(100);
			snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0);
		}
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_GENERAL_PURPOSE, 0);
		end_time = jiffies + msecs_to_jiffies(5000);
		do {
			if ((snd_ac97_read(ac97, AC97_POWERDOWN) & 0x0f) == 0x0f)
				goto __ready_ok;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
		ac97_warn(ac97,
			  "AC'97 %d analog subsections not ready\n", ac97->num);
	}

	/* FIXME: add powerdown control */
	if (ac97_is_modem(ac97)) {
		unsigned char tmp;

		/* nothing should be in powerdown mode */
		/* note: it's important to set the rate at first */
		tmp = AC97_MEA_GPIO;
		if (ac97->ext_mid & AC97_MEI_LINE1) {
			snd_ac97_write_cache(ac97, AC97_LINE1_RATE, 8000);
			tmp |= AC97_MEA_ADC1 | AC97_MEA_DAC1;
		}
		if (ac97->ext_mid & AC97_MEI_LINE2) {
			snd_ac97_write_cache(ac97, AC97_LINE2_RATE, 8000);
			tmp |= AC97_MEA_ADC2 | AC97_MEA_DAC2;
		}
		if (ac97->ext_mid & AC97_MEI_HANDSET) {
			snd_ac97_write_cache(ac97, AC97_HANDSET_RATE, 8000);
			tmp |= AC97_MEA_HADC | AC97_MEA_HDAC;
		}
		snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0);
		udelay(100);
		/* nothing should be in powerdown mode */
		snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0);
		end_time = jiffies + msecs_to_jiffies(100);
		do {
			if ((snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS) & tmp) == tmp)
				goto __ready_ok;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
		ac97_warn(ac97,
			  "MC'97 %d converters and GPIO not ready (0x%x)\n",
			  ac97->num,
			  snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS));
	}
	
      __ready_ok:
	if (ac97_is_audio(ac97))
		ac97->addr = (ac97->ext_id & AC97_EI_ADDR_MASK) >> AC97_EI_ADDR_SHIFT;
	else
		ac97->addr = (ac97->ext_mid & AC97_MEI_ADDR_MASK) >> AC97_MEI_ADDR_SHIFT;
	if (ac97->ext_id & 0x01c9) {	/* L/R, MIC, SDAC, LDAC VRA support */
		reg = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
		reg |= ac97->ext_id & 0x01c0; /* LDAC/SDAC/CDAC */
		if (! bus->no_vra)
			reg |= ac97->ext_id & 0x0009; /* VRA/VRM */
		snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, reg);
	}
	if ((ac97->ext_id & AC97_EI_DRA) && bus->dra) {
		/* Intel controllers require double rate data to be put in
		 * slots 7+8, so let's hope the codec supports it. */
		snd_ac97_update_bits(ac97, AC97_GENERAL_PURPOSE, AC97_GP_DRSS_MASK, AC97_GP_DRSS_78);
		if ((snd_ac97_read(ac97, AC97_GENERAL_PURPOSE) & AC97_GP_DRSS_MASK) == AC97_GP_DRSS_78)
			ac97->flags |= AC97_DOUBLE_RATE;
		/* restore to slots 10/11 to avoid the confliction with surrounds */
		snd_ac97_update_bits(ac97, AC97_GENERAL_PURPOSE, AC97_GP_DRSS_MASK, 0);
	}
	if (ac97->ext_id & AC97_EI_VRA) {	/* VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_FRONT_DAC_RATE, 0, &ac97->rates[AC97_RATES_FRONT_DAC]);
		snd_ac97_determine_rates(ac97, AC97_PCM_LR_ADC_RATE, 0, &ac97->rates[AC97_RATES_ADC]);
	} else {
		ac97->rates[AC97_RATES_FRONT_DAC] = SNDRV_PCM_RATE_48000;
		if (ac97->flags & AC97_DOUBLE_RATE)
			ac97->rates[AC97_RATES_FRONT_DAC] |= SNDRV_PCM_RATE_96000;
		ac97->rates[AC97_RATES_ADC] = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & AC97_EI_SPDIF) {
		/* codec specific code (patch) should override these values */
		ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_32000;
	}
	if (ac97->ext_id & AC97_EI_VRM) {	/* MIC VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_MIC_ADC_RATE, 0, &ac97->rates[AC97_RATES_MIC_ADC]);
	} else {
		ac97->rates[AC97_RATES_MIC_ADC] = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & AC97_EI_SDAC) {	/* SDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_SURR_DAC_RATE, AC97_PCM_FRONT_DAC_RATE, &ac97->rates[AC97_RATES_SURR_DAC]);
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->ext_id & AC97_EI_LDAC) {	/* LDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_LFE_DAC_RATE, AC97_PCM_FRONT_DAC_RATE, &ac97->rates[AC97_RATES_LFE_DAC]);
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}
	/* additional initializations */
	if (bus->ops->init)
		bus->ops->init(ac97);
	snd_ac97_get_name(ac97, ac97->id, name, !ac97_is_audio(ac97));
	snd_ac97_get_name(NULL, ac97->id, name, !ac97_is_audio(ac97));  // ac97->id might be changed in the special setup code
	if (! ac97->build_ops)
		ac97->build_ops = &null_build_ops;

	if (ac97_is_audio(ac97)) {
		char comp[16];
		if (card->mixername[0] == '\0') {
			strcpy(card->mixername, name);
		} else {
			if (strlen(card->mixername) + 1 + strlen(name) + 1 <= sizeof(card->mixername)) {
				strcat(card->mixername, ",");
				strcat(card->mixername, name);
			}
		}
		sprintf(comp, "AC97a:%08x", ac97->id);
		err = snd_component_add(card, comp);
		if (err < 0) {
			snd_ac97_free(ac97);
			return err;
		}
		if (snd_ac97_mixer_build(ac97) < 0) {
			snd_ac97_free(ac97);
			return -ENOMEM;
		}
	}
	if (ac97_is_modem(ac97)) {
		char comp[16];
		if (card->mixername[0] == '\0') {
			strcpy(card->mixername, name);
		} else {
			if (strlen(card->mixername) + 1 + strlen(name) + 1 <= sizeof(card->mixername)) {
				strcat(card->mixername, ",");
				strcat(card->mixername, name);
			}
		}
		sprintf(comp, "AC97m:%08x", ac97->id);
		err = snd_component_add(card, comp);
		if (err < 0) {
			snd_ac97_free(ac97);
			return err;
		}
		if (snd_ac97_modem_build(card, ac97) < 0) {
			snd_ac97_free(ac97);
			return -ENOMEM;
		}
	}
	if (ac97_is_audio(ac97))
		update_power_regs(ac97);
	snd_ac97_proc_init(ac97);
	err = snd_device_new(card, SNDRV_DEV_CODEC, ac97, &ops);
	if (err < 0) {
		snd_ac97_free(ac97);
		return err;
	}
	*rac97 = ac97;
	return 0;
}

EXPORT_SYMBOL(snd_ac97_mixer);

/*
 * Power down the chip.
 *
 * MASTER and HEADPHONE registers are muted but the register cache values
 * are not changed, so that the values can be restored in snd_ac97_resume().
 */
static void snd_ac97_powerdown(struct snd_ac97 *ac97)
{
	unsigned short power;

	if (ac97_is_audio(ac97)) {
		/* some codecs have stereo mute bits */
		snd_ac97_write(ac97, AC97_MASTER, 0x9f9f);
		snd_ac97_write(ac97, AC97_HEADPHONE, 0x9f9f);
	}

	/* surround, CLFE, mic powerdown */
	power = ac97->regs[AC97_EXTENDED_STATUS];
	if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
		power |= AC97_EA_PRJ;
	if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
		power |= AC97_EA_PRI | AC97_EA_PRK;
	power |= AC97_EA_PRL;
	snd_ac97_write(ac97, AC97_EXTENDED_STATUS, power);

	/* powerdown external amplifier */
	if (ac97->scaps & AC97_SCAP_INV_EAPD)
		power = ac97->regs[AC97_POWERDOWN] & ~AC97_PD_EAPD;
	else if (! (ac97->scaps & AC97_SCAP_EAPD_LED))
		power = ac97->regs[AC97_POWERDOWN] | AC97_PD_EAPD;
	power |= AC97_PD_PR6;	/* Headphone amplifier powerdown */
	power |= AC97_PD_PR0 | AC97_PD_PR1;	/* ADC & DAC powerdown */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	udelay(100);
	power |= AC97_PD_PR2;	/* Analog Mixer powerdown (Vref on) */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	if (ac97_is_power_save_mode(ac97)) {
		power |= AC97_PD_PR3;	/* Analog Mixer powerdown */
		snd_ac97_write(ac97, AC97_POWERDOWN, power);
		udelay(100);
		/* AC-link powerdown, internal Clk disable */
		/* FIXME: this may cause click noises on some boards */
		power |= AC97_PD_PR4 | AC97_PD_PR5;
		snd_ac97_write(ac97, AC97_POWERDOWN, power);
	}
}


struct ac97_power_reg {
	unsigned short reg;
	unsigned short power_reg;
	unsigned short mask;
};

enum { PWIDX_ADC, PWIDX_FRONT, PWIDX_CLFE, PWIDX_SURR, PWIDX_MIC, PWIDX_SIZE };

static const struct ac97_power_reg power_regs[PWIDX_SIZE] = {
	[PWIDX_ADC] = { AC97_PCM_LR_ADC_RATE, AC97_POWERDOWN, AC97_PD_PR0},
	[PWIDX_FRONT] = { AC97_PCM_FRONT_DAC_RATE, AC97_POWERDOWN, AC97_PD_PR1},
	[PWIDX_CLFE] = { AC97_PCM_LFE_DAC_RATE, AC97_EXTENDED_STATUS,
			 AC97_EA_PRI | AC97_EA_PRK},
	[PWIDX_SURR] = { AC97_PCM_SURR_DAC_RATE, AC97_EXTENDED_STATUS,
			 AC97_EA_PRJ},
	[PWIDX_MIC] = { AC97_PCM_MIC_ADC_RATE, AC97_EXTENDED_STATUS,
			AC97_EA_PRL},
};

#ifdef CONFIG_SND_AC97_POWER_SAVE
/**
 * snd_ac97_update_power - update the powerdown register
 * @ac97: the codec instance
 * @reg: the rate register, e.g. AC97_PCM_FRONT_DAC_RATE
 * @powerup: non-zero when power up the part
 *
 * Update the AC97 powerdown register bits of the given part.
 *
 * Return: Zero.
 */
int snd_ac97_update_power(struct snd_ac97 *ac97, int reg, int powerup)
{
	int i;

	if (! ac97)
		return 0;

	if (reg) {
		/* SPDIF requires DAC power, too */
		if (reg == AC97_SPDIF)
			reg = AC97_PCM_FRONT_DAC_RATE;
		for (i = 0; i < PWIDX_SIZE; i++) {
			if (power_regs[i].reg == reg) {
				if (powerup)
					ac97->power_up |= (1 << i);
				else
					ac97->power_up &= ~(1 << i);
				break;
			}
		}
	}

	if (ac97_is_power_save_mode(ac97) && !powerup)
		/* adjust power-down bits after two seconds delay
		 * (for avoiding loud click noises for many (OSS) apps
		 *  that open/close frequently)
		 */
		schedule_delayed_work(&ac97->power_work,
				      msecs_to_jiffies(power_save * 1000));
	else {
		cancel_delayed_work(&ac97->power_work);
		update_power_regs(ac97);
	}

	return 0;
}

EXPORT_SYMBOL(snd_ac97_update_power);
#endif /* CONFIG_SND_AC97_POWER_SAVE */

static void update_power_regs(struct snd_ac97 *ac97)
{
	unsigned int power_up, bits;
	int i;

	power_up = (1 << PWIDX_FRONT) | (1 << PWIDX_ADC);
	power_up |= (1 << PWIDX_MIC);
	if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
		power_up |= (1 << PWIDX_SURR);
	if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
		power_up |= (1 << PWIDX_CLFE);
#ifdef CONFIG_SND_AC97_POWER_SAVE
	if (ac97_is_power_save_mode(ac97))
		power_up = ac97->power_up;
#endif
	if (power_up) {
		if (ac97->regs[AC97_POWERDOWN] & AC97_PD_PR2) {
			/* needs power-up analog mix and vref */
			snd_ac97_update_bits(ac97, AC97_POWERDOWN,
					     AC97_PD_PR3, 0);
			msleep(1);
			snd_ac97_update_bits(ac97, AC97_POWERDOWN,
					     AC97_PD_PR2, 0);
		}
	}
	for (i = 0; i < PWIDX_SIZE; i++) {
		if (power_up & (1 << i))
			bits = 0;
		else
			bits = power_regs[i].mask;
		snd_ac97_update_bits(ac97, power_regs[i].power_reg,
				     power_regs[i].mask, bits);
	}
	if (! power_up) {
		if (! (ac97->regs[AC97_POWERDOWN] & AC97_PD_PR2)) {
			/* power down analog mix and vref */
			snd_ac97_update_bits(ac97, AC97_POWERDOWN,
					     AC97_PD_PR2, AC97_PD_PR2);
			snd_ac97_update_bits(ac97, AC97_POWERDOWN,
					     AC97_PD_PR3, AC97_PD_PR3);
		}
	}
}


#ifdef CONFIG_PM
/**
 * snd_ac97_suspend - General suspend function for AC97 codec
 * @ac97: the ac97 instance
 *
 * Suspends the codec, power down the chip.
 */
void snd_ac97_suspend(struct snd_ac97 *ac97)
{
	if (! ac97)
		return;
	if (ac97->build_ops->suspend)
		ac97->build_ops->suspend(ac97);
#ifdef CONFIG_SND_AC97_POWER_SAVE
	cancel_delayed_work_sync(&ac97->power_work);
#endif
	snd_ac97_powerdown(ac97);
}

EXPORT_SYMBOL(snd_ac97_suspend);

/*
 * restore ac97 status
 */
static void snd_ac97_restore_status(struct snd_ac97 *ac97)
{
	int i;

	for (i = 2; i < 0x7c ; i += 2) {
		if (i == AC97_POWERDOWN || i == AC97_EXTENDED_ID)
			continue;
		/* restore only accessible registers
		 * some chip (e.g. nm256) may hang up when unsupported registers
		 * are accessed..!
		 */
		if (test_bit(i, ac97->reg_accessed)) {
			snd_ac97_write(ac97, i, ac97->regs[i]);
			snd_ac97_read(ac97, i);
		}
	}
}

/*
 * restore IEC958 status
 */
static void snd_ac97_restore_iec958(struct snd_ac97 *ac97)
{
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if (ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_SPDIF) {
			/* reset spdif status */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			snd_ac97_write(ac97, AC97_EXTENDED_STATUS, ac97->regs[AC97_EXTENDED_STATUS]);
			if (ac97->flags & AC97_CS_SPDIF)
				snd_ac97_write(ac97, AC97_CSR_SPDIF, ac97->regs[AC97_CSR_SPDIF]);
			else
				snd_ac97_write(ac97, AC97_SPDIF, ac97->regs[AC97_SPDIF]);
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF); /* turn on again */
		}
	}
}

/**
 * snd_ac97_resume - General resume function for AC97 codec
 * @ac97: the ac97 instance
 *
 * Do the standard resume procedure, power up and restoring the
 * old register values.
 */
void snd_ac97_resume(struct snd_ac97 *ac97)
{
	unsigned long end_time;

	if (! ac97)
		return;

	if (ac97->bus->ops->reset) {
		ac97->bus->ops->reset(ac97);
		goto  __reset_ready;
	}

	snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	if (! (ac97->flags & AC97_DEFAULT_POWER_OFF)) {
		if (!(ac97->scaps & AC97_SCAP_SKIP_AUDIO))
			snd_ac97_write(ac97, AC97_RESET, 0);
		else if (!(ac97->scaps & AC97_SCAP_SKIP_MODEM))
			snd_ac97_write(ac97, AC97_EXTENDED_MID, 0);
		udelay(100);
		snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	}
	snd_ac97_write(ac97, AC97_GENERAL_PURPOSE, 0);

	snd_ac97_write(ac97, AC97_POWERDOWN, ac97->regs[AC97_POWERDOWN]);
	if (ac97_is_audio(ac97)) {
		ac97->bus->ops->write(ac97, AC97_MASTER, 0x8101);
		end_time = jiffies + msecs_to_jiffies(100);
		do {
			if (snd_ac97_read(ac97, AC97_MASTER) == 0x8101)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
		/* FIXME: extra delay */
		ac97->bus->ops->write(ac97, AC97_MASTER, AC97_MUTE_MASK_MONO);
		if (snd_ac97_read(ac97, AC97_MASTER) != AC97_MUTE_MASK_MONO)
			msleep(250);
	} else {
		end_time = jiffies + msecs_to_jiffies(100);
		do {
			unsigned short val = snd_ac97_read(ac97, AC97_EXTENDED_MID);
			if (val != 0xffff && (val & 1) != 0)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
	}
__reset_ready:

	if (ac97->bus->ops->init)
		ac97->bus->ops->init(ac97);

	if (ac97->build_ops->resume)
		ac97->build_ops->resume(ac97);
	else {
		snd_ac97_restore_status(ac97);
		snd_ac97_restore_iec958(ac97);
	}
}

EXPORT_SYMBOL(snd_ac97_resume);
#endif


/*
 * Hardware tuning
 */
static void set_ctl_name(char *dst, const char *src, const char *suffix)
{
	const size_t msize = SNDRV_CTL_ELEM_ID_NAME_MAXLEN;

	if (suffix) {
		if (snprintf(dst, msize, "%s %s", src, suffix) >= msize)
			pr_warn("ALSA: AC97 control name '%s %s' truncated to '%s'\n",
				src, suffix, dst);
	} else {
		if (strscpy(dst, src, msize) < 0)
			pr_warn("ALSA: AC97 control name '%s' truncated to '%s'\n",
				src, dst);
	}
}

/* remove the control with the given name and optional suffix */
static int snd_ac97_remove_ctl(struct snd_ac97 *ac97, const char *name,
			       const char *suffix)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	set_ctl_name(id.name, name, suffix);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(ac97->bus->card, &id);
}

static struct snd_kcontrol *ctl_find(struct snd_ac97 *ac97, const char *name, const char *suffix)
{
	struct snd_ctl_elem_id sid;
	memset(&sid, 0, sizeof(sid));
	set_ctl_name(sid.name, name, suffix);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(ac97->bus->card, &sid);
}

/* rename the control with the given name and optional suffix */
static int snd_ac97_rename_ctl(struct snd_ac97 *ac97, const char *src,
			       const char *dst, const char *suffix)
{
	struct snd_kcontrol *kctl = ctl_find(ac97, src, suffix);
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	if (kctl) {
		set_ctl_name(name, dst, suffix);
		snd_ctl_rename(ac97->bus->card, kctl, name);
		return 0;
	}
	return -ENOENT;
}

/* rename both Volume and Switch controls - don't check the return value */
static void snd_ac97_rename_vol_ctl(struct snd_ac97 *ac97, const char *src,
				    const char *dst)
{
	snd_ac97_rename_ctl(ac97, src, dst, "Switch");
	snd_ac97_rename_ctl(ac97, src, dst, "Volume");
}

/* swap controls */
static int snd_ac97_swap_ctl(struct snd_ac97 *ac97, const char *s1,
			     const char *s2, const char *suffix)
{
	struct snd_kcontrol *kctl1, *kctl2;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	kctl1 = ctl_find(ac97, s1, suffix);
	kctl2 = ctl_find(ac97, s2, suffix);
	if (kctl1 && kctl2) {
		set_ctl_name(name, s2, suffix);
		snd_ctl_rename(ac97->bus->card, kctl1, name);

		set_ctl_name(name, s1, suffix);
		snd_ctl_rename(ac97->bus->card, kctl2, name);

		return 0;
	}
	return -ENOENT;
}

#if 1
/* bind hp and master controls instead of using only hp control */
static int bind_hp_volsw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int err = snd_ac97_put_volsw(kcontrol, ucontrol);
	if (err > 0) {
		unsigned long priv_saved = kcontrol->private_value;
		kcontrol->private_value = (kcontrol->private_value & ~0xff) | AC97_HEADPHONE;
		snd_ac97_put_volsw(kcontrol, ucontrol);
		kcontrol->private_value = priv_saved;
	}
	return err;
}

/* ac97 tune: bind Master and Headphone controls */
static int tune_hp_only(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *msw = ctl_find(ac97, "Master Playback Switch", NULL);
	struct snd_kcontrol *mvol = ctl_find(ac97, "Master Playback Volume", NULL);
	if (! msw || ! mvol)
		return -ENOENT;
	msw->put = bind_hp_volsw_put;
	mvol->put = bind_hp_volsw_put;
	snd_ac97_remove_ctl(ac97, "Headphone Playback", "Switch");
	snd_ac97_remove_ctl(ac97, "Headphone Playback", "Volume");
	return 0;
}

#else
/* ac97 tune: use Headphone control as master */
static int tune_hp_only(struct snd_ac97 *ac97)
{
	if (ctl_find(ac97, "Headphone Playback Switch", NULL) == NULL)
		return -ENOENT;
	snd_ac97_remove_ctl(ac97, "Master Playback", "Switch");
	snd_ac97_remove_ctl(ac97, "Master Playback", "Volume");
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Master Playback");
	return 0;
}
#endif

/* ac97 tune: swap Headphone and Master controls */
static int tune_swap_hp(struct snd_ac97 *ac97)
{
	if (ctl_find(ac97, "Headphone Playback Switch", NULL) == NULL)
		return -ENOENT;
	snd_ac97_rename_vol_ctl(ac97, "Master Playback", "Line-Out Playback");
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Master Playback");
	return 0;
}

/* ac97 tune: swap Surround and Master controls */
static int tune_swap_surround(struct snd_ac97 *ac97)
{
	if (snd_ac97_swap_ctl(ac97, "Master Playback", "Surround Playback", "Switch") ||
	    snd_ac97_swap_ctl(ac97, "Master Playback", "Surround Playback", "Volume"))
		return -ENOENT;
	return 0;
}

/* ac97 tune: set up mic sharing for AD codecs */
static int tune_ad_sharing(struct snd_ac97 *ac97)
{
	unsigned short scfg;
	if ((ac97->id & 0xffffff00) != 0x41445300) {
		ac97_err(ac97, "ac97_quirk AD_SHARING is only for AD codecs\n");
		return -EINVAL;
	}
	/* Turn on OMS bit to route microphone to back panel */
	scfg = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, scfg | 0x0200);
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_alc_jack_detect = 
AC97_SINGLE("Jack Detect", AC97_ALC650_CLOCK, 5, 1, 0);

/* ac97 tune: set up ALC jack-select */
static int tune_alc_jack(struct snd_ac97 *ac97)
{
	if ((ac97->id & 0xffffff00) != 0x414c4700) {
		ac97_err(ac97,
			 "ac97_quirk ALC_JACK is only for Realtek codecs\n");
		return -EINVAL;
	}
	snd_ac97_update_bits(ac97, 0x7a, 0x20, 0x20); /* select jack detect function */
	snd_ac97_update_bits(ac97, 0x7a, 0x01, 0x01); /* Line-out auto mute */
	if (ac97->id == AC97_ID_ALC658D)
		snd_ac97_update_bits(ac97, 0x74, 0x0800, 0x0800);
	return snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&snd_ac97_alc_jack_detect, ac97));
}

/* ac97 tune: inversed EAPD bit */
static int tune_inv_eapd(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *kctl = ctl_find(ac97, "External Amplifier", NULL);
	if (! kctl)
		return -ENOENT;
	set_inv_eapd(ac97, kctl);
	return 0;
}

static int master_mute_sw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int err = snd_ac97_put_volsw(kcontrol, ucontrol);
	if (err > 0) {
		struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
		int shift = (kcontrol->private_value >> 8) & 0x0f;
		int rshift = (kcontrol->private_value >> 12) & 0x0f;
		unsigned short mask;
		if (shift != rshift)
			mask = AC97_MUTE_MASK_STEREO;
		else
			mask = AC97_MUTE_MASK_MONO;
		snd_ac97_update_bits(ac97, AC97_POWERDOWN, AC97_PD_EAPD,
				     (ac97->regs[AC97_MASTER] & mask) == mask ?
				     AC97_PD_EAPD : 0);
	}
	return err;
}

/* ac97 tune: EAPD controls mute LED bound with the master mute */
static int tune_mute_led(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *msw = ctl_find(ac97, "Master Playback Switch", NULL);
	if (! msw)
		return -ENOENT;
	msw->put = master_mute_sw_put;
	snd_ac97_remove_ctl(ac97, "External Amplifier", NULL);
	snd_ac97_update_bits(
		ac97, AC97_POWERDOWN,
		AC97_PD_EAPD, AC97_PD_EAPD /* mute LED on */
	);
	ac97->scaps |= AC97_SCAP_EAPD_LED;
	return 0;
}

static int hp_master_mute_sw_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int err = bind_hp_volsw_put(kcontrol, ucontrol);
	if (err > 0) {
		struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
		int shift = (kcontrol->private_value >> 8) & 0x0f;
		int rshift = (kcontrol->private_value >> 12) & 0x0f;
		unsigned short mask;
		if (shift != rshift)
			mask = AC97_MUTE_MASK_STEREO;
		else
			mask = AC97_MUTE_MASK_MONO;
		snd_ac97_update_bits(ac97, AC97_POWERDOWN, AC97_PD_EAPD,
				     (ac97->regs[AC97_MASTER] & mask) == mask ?
				     AC97_PD_EAPD : 0);
	}
	return err;
}

static int tune_hp_mute_led(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *msw = ctl_find(ac97, "Master Playback Switch", NULL);
	struct snd_kcontrol *mvol = ctl_find(ac97, "Master Playback Volume", NULL);
	if (! msw || ! mvol)
		return -ENOENT;
	msw->put = hp_master_mute_sw_put;
	mvol->put = bind_hp_volsw_put;
	snd_ac97_remove_ctl(ac97, "External Amplifier", NULL);
	snd_ac97_remove_ctl(ac97, "Headphone Playback", "Switch");
	snd_ac97_remove_ctl(ac97, "Headphone Playback", "Volume");
	snd_ac97_update_bits(
		ac97, AC97_POWERDOWN,
		AC97_PD_EAPD, AC97_PD_EAPD /* mute LED on */
	);
	return 0;
}

struct quirk_table {
	const char *name;
	int (*func)(struct snd_ac97 *);
};

static const struct quirk_table applicable_quirks[] = {
	{ "none", NULL },
	{ "hp_only", tune_hp_only },
	{ "swap_hp", tune_swap_hp },
	{ "swap_surround", tune_swap_surround },
	{ "ad_sharing", tune_ad_sharing },
	{ "alc_jack", tune_alc_jack },
	{ "inv_eapd", tune_inv_eapd },
	{ "mute_led", tune_mute_led },
	{ "hp_mute_led", tune_hp_mute_led },
};

/* apply the quirk with the given type */
static int apply_quirk(struct snd_ac97 *ac97, int type)
{
	if (type <= 0)
		return 0;
	else if (type >= ARRAY_SIZE(applicable_quirks))
		return -EINVAL;
	if (applicable_quirks[type].func)
		return applicable_quirks[type].func(ac97);
	return 0;
}

/* apply the quirk with the given name */
static int apply_quirk_str(struct snd_ac97 *ac97, const char *typestr)
{
	int i;
	const struct quirk_table *q;

	for (i = 0; i < ARRAY_SIZE(applicable_quirks); i++) {
		q = &applicable_quirks[i];
		if (q->name && ! strcmp(typestr, q->name))
			return apply_quirk(ac97, i);
	}
	/* for compatibility, accept the numbers, too */
	if (*typestr >= '0' && *typestr <= '9')
		return apply_quirk(ac97, (int)simple_strtoul(typestr, NULL, 10));
	return -EINVAL;
}

/**
 * snd_ac97_tune_hardware - tune up the hardware
 * @ac97: the ac97 instance
 * @quirk: quirk list
 * @override: explicit quirk value (overrides the list if non-NULL)
 *
 * Do some workaround for each pci device, such as renaming of the
 * headphone (true line-out) control as "Master".
 * The quirk-list must be terminated with a zero-filled entry.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */

int snd_ac97_tune_hardware(struct snd_ac97 *ac97,
			   const struct ac97_quirk *quirk, const char *override)
{
	int result;

	/* quirk overriden? */
	if (override && strcmp(override, "-1") && strcmp(override, "default")) {
		result = apply_quirk_str(ac97, override);
		if (result < 0)
			ac97_err(ac97, "applying quirk type %s failed (%d)\n",
				 override, result);
		return result;
	}

	if (! quirk)
		return -EINVAL;

	for (; quirk->subvendor; quirk++) {
		if (quirk->subvendor != ac97->subsystem_vendor)
			continue;
		if ((! quirk->mask && quirk->subdevice == ac97->subsystem_device) ||
		    quirk->subdevice == (quirk->mask & ac97->subsystem_device)) {
			if (quirk->codec_id && quirk->codec_id != ac97->id)
				continue;
			ac97_dbg(ac97, "ac97 quirk for %s (%04x:%04x)\n",
				 quirk->name, ac97->subsystem_vendor,
				 ac97->subsystem_device);
			result = apply_quirk(ac97, quirk->type);
			if (result < 0)
				ac97_err(ac97,
					 "applying quirk type %d for %s failed (%d)\n",
					 quirk->type, quirk->name, result);
			return result;
		}
	}
	return 0;
}

EXPORT_SYMBOL(snd_ac97_tune_hardware);
