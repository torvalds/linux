// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routines for control of the AK4114 via I2C and 4-wire serial interface
 *  IEC958 (S/PDIF) receiver by Asahi Kasei
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/ak4114.h>
#include <sound/asoundef.h>
#include <sound/info.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("AK4114 IEC958 (S/PDIF) receiver by Asahi Kasei");
MODULE_LICENSE("GPL");

#define AK4114_ADDR			0x00 /* fixed address */

static void ak4114_stats(struct work_struct *work);
static void ak4114_init_regs(struct ak4114 *chip);

static void reg_write(struct ak4114 *ak4114, unsigned char reg, unsigned char val)
{
	ak4114->write(ak4114->private_data, reg, val);
	if (reg <= AK4114_REG_INT1_MASK)
		ak4114->regmap[reg] = val;
	else if (reg >= AK4114_REG_TXCSB0 && reg <= AK4114_REG_TXCSB4)
		ak4114->txcsb[reg-AK4114_REG_TXCSB0] = val;
}

static inline unsigned char reg_read(struct ak4114 *ak4114, unsigned char reg)
{
	return ak4114->read(ak4114->private_data, reg);
}

#if 0
static void reg_dump(struct ak4114 *ak4114)
{
	int i;

	printk(KERN_DEBUG "AK4114 REG DUMP:\n");
	for (i = 0; i < 0x20; i++)
		printk(KERN_DEBUG "reg[%02x] = %02x (%02x)\n", i, reg_read(ak4114, i), i < ARRAY_SIZE(ak4114->regmap) ? ak4114->regmap[i] : 0);
}
#endif

static void snd_ak4114_free(struct ak4114 *chip)
{
	atomic_inc(&chip->wq_processing);	/* don't schedule new work */
	cancel_delayed_work_sync(&chip->work);
	kfree(chip);
}

static int snd_ak4114_dev_free(struct snd_device *device)
{
	struct ak4114 *chip = device->device_data;
	snd_ak4114_free(chip);
	return 0;
}

int snd_ak4114_create(struct snd_card *card,
		      ak4114_read_t *read, ak4114_write_t *write,
		      const unsigned char pgm[6], const unsigned char txcsb[5],
		      void *private_data, struct ak4114 **r_ak4114)
{
	struct ak4114 *chip;
	int err = 0;
	unsigned char reg;
	static const struct snd_device_ops ops = {
		.dev_free =     snd_ak4114_dev_free,
	};

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->lock);
	chip->card = card;
	chip->read = read;
	chip->write = write;
	chip->private_data = private_data;
	INIT_DELAYED_WORK(&chip->work, ak4114_stats);
	atomic_set(&chip->wq_processing, 0);
	mutex_init(&chip->reinit_mutex);

	for (reg = 0; reg < 6; reg++)
		chip->regmap[reg] = pgm[reg];
	for (reg = 0; reg < 5; reg++)
		chip->txcsb[reg] = txcsb[reg];

	ak4114_init_regs(chip);

	chip->rcs0 = reg_read(chip, AK4114_REG_RCS0) & ~(AK4114_QINT | AK4114_CINT);
	chip->rcs1 = reg_read(chip, AK4114_REG_RCS1);

	err = snd_device_new(card, SNDRV_DEV_CODEC, chip, &ops);
	if (err < 0)
		goto __fail;

	if (r_ak4114)
		*r_ak4114 = chip;
	return 0;

      __fail:
	snd_ak4114_free(chip);
	return err;
}
EXPORT_SYMBOL(snd_ak4114_create);

void snd_ak4114_reg_write(struct ak4114 *chip, unsigned char reg, unsigned char mask, unsigned char val)
{
	if (reg <= AK4114_REG_INT1_MASK)
		reg_write(chip, reg, (chip->regmap[reg] & ~mask) | val);
	else if (reg >= AK4114_REG_TXCSB0 && reg <= AK4114_REG_TXCSB4)
		reg_write(chip, reg,
			  (chip->txcsb[reg-AK4114_REG_TXCSB0] & ~mask) | val);
}
EXPORT_SYMBOL(snd_ak4114_reg_write);

static void ak4114_init_regs(struct ak4114 *chip)
{
	unsigned char old = chip->regmap[AK4114_REG_PWRDN], reg;

	/* bring the chip to reset state and powerdown state */
	reg_write(chip, AK4114_REG_PWRDN, old & ~(AK4114_RST|AK4114_PWN));
	udelay(200);
	/* release reset, but leave powerdown */
	reg_write(chip, AK4114_REG_PWRDN, (old | AK4114_RST) & ~AK4114_PWN);
	udelay(200);
	for (reg = 1; reg < 6; reg++)
		reg_write(chip, reg, chip->regmap[reg]);
	for (reg = 0; reg < 5; reg++)
		reg_write(chip, reg + AK4114_REG_TXCSB0, chip->txcsb[reg]);
	/* release powerdown, everything is initialized now */
	reg_write(chip, AK4114_REG_PWRDN, old | AK4114_RST | AK4114_PWN);
}

void snd_ak4114_reinit(struct ak4114 *chip)
{
	if (atomic_inc_return(&chip->wq_processing) == 1)
		cancel_delayed_work_sync(&chip->work);
	mutex_lock(&chip->reinit_mutex);
	ak4114_init_regs(chip);
	mutex_unlock(&chip->reinit_mutex);
	/* bring up statistics / event queing */
	if (atomic_dec_and_test(&chip->wq_processing))
		schedule_delayed_work(&chip->work, HZ / 10);
}
EXPORT_SYMBOL(snd_ak4114_reinit);

static unsigned int external_rate(unsigned char rcs1)
{
	switch (rcs1 & (AK4114_FS0|AK4114_FS1|AK4114_FS2|AK4114_FS3)) {
	case AK4114_FS_32000HZ: return 32000;
	case AK4114_FS_44100HZ: return 44100;
	case AK4114_FS_48000HZ: return 48000;
	case AK4114_FS_88200HZ: return 88200;
	case AK4114_FS_96000HZ: return 96000;
	case AK4114_FS_176400HZ: return 176400;
	case AK4114_FS_192000HZ: return 192000;
	default:		return 0;
	}
}

static int snd_ak4114_in_error_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = LONG_MAX;
	return 0;
}

static int snd_ak4114_in_error_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&chip->lock);
	ucontrol->value.integer.value[0] =
		chip->errors[kcontrol->private_value];
	chip->errors[kcontrol->private_value] = 0;
	spin_unlock_irq(&chip->lock);
	return 0;
}

#define snd_ak4114_in_bit_info		snd_ctl_boolean_mono_info

static int snd_ak4114_in_bit_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned char reg = kcontrol->private_value & 0xff;
	unsigned char bit = (kcontrol->private_value >> 8) & 0xff;
	unsigned char inv = (kcontrol->private_value >> 31) & 1;

	ucontrol->value.integer.value[0] = ((reg_read(chip, reg) & (1 << bit)) ? 1 : 0) ^ inv;
	return 0;
}

static int snd_ak4114_rate_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;
	return 0;
}

static int snd_ak4114_rate_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = external_rate(reg_read(chip, AK4114_REG_RCS1));
	return 0;
}

static int snd_ak4114_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ak4114_spdif_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned i;

	for (i = 0; i < AK4114_REG_RXCSB_SIZE; i++)
		ucontrol->value.iec958.status[i] = reg_read(chip, AK4114_REG_RXCSB0 + i);
	return 0;
}

static int snd_ak4114_spdif_playback_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned i;

	for (i = 0; i < AK4114_REG_TXCSB_SIZE; i++)
		ucontrol->value.iec958.status[i] = chip->txcsb[i];
	return 0;
}

static int snd_ak4114_spdif_playback_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned i;

	for (i = 0; i < AK4114_REG_TXCSB_SIZE; i++)
		reg_write(chip, AK4114_REG_TXCSB0 + i, ucontrol->value.iec958.status[i]);
	return 0;
}

static int snd_ak4114_spdif_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ak4114_spdif_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	memset(ucontrol->value.iec958.status, 0xff, AK4114_REG_RXCSB_SIZE);
	return 0;
}

static int snd_ak4114_spdif_pinfo(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xffff;
	uinfo->count = 4;
	return 0;
}

static int snd_ak4114_spdif_pget(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned short tmp;

	ucontrol->value.integer.value[0] = 0xf8f2;
	ucontrol->value.integer.value[1] = 0x4e1f;
	tmp = reg_read(chip, AK4114_REG_Pc0) | (reg_read(chip, AK4114_REG_Pc1) << 8);
	ucontrol->value.integer.value[2] = tmp;
	tmp = reg_read(chip, AK4114_REG_Pd0) | (reg_read(chip, AK4114_REG_Pd1) << 8);
	ucontrol->value.integer.value[3] = tmp;
	return 0;
}

static int snd_ak4114_spdif_qinfo(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = AK4114_REG_QSUB_SIZE;
	return 0;
}

static int snd_ak4114_spdif_qget(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct ak4114 *chip = snd_kcontrol_chip(kcontrol);
	unsigned i;

	for (i = 0; i < AK4114_REG_QSUB_SIZE; i++)
		ucontrol->value.bytes.data[i] = reg_read(chip, AK4114_REG_QSUB_ADDR + i);
	return 0;
}

/* Don't forget to change AK4114_CONTROLS define!!! */
static const struct snd_kcontrol_new snd_ak4114_iec958_controls[] = {
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Parity Errors",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_error_info,
	.get =		snd_ak4114_in_error_get,
	.private_value = AK4114_PARITY_ERRORS,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 V-Bit Errors",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_error_info,
	.get =		snd_ak4114_in_error_get,
	.private_value = AK4114_V_BIT_ERRORS,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 C-CRC Errors",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_error_info,
	.get =		snd_ak4114_in_error_get,
	.private_value = AK4114_CCRC_ERRORS,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Q-CRC Errors",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_error_info,
	.get =		snd_ak4114_in_error_get,
	.private_value = AK4114_QCRC_ERRORS,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 External Rate",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_rate_info,
	.get =		snd_ak4114_rate_get,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.info =		snd_ak4114_spdif_mask_info,
	.get =		snd_ak4114_spdif_mask_get,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_spdif_info,
	.get =		snd_ak4114_spdif_playback_get,
	.put =		snd_ak4114_spdif_playback_put,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",CAPTURE,MASK),
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.info =		snd_ak4114_spdif_mask_info,
	.get =		snd_ak4114_spdif_mask_get,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",CAPTURE,DEFAULT),
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_spdif_info,
	.get =		snd_ak4114_spdif_get,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Preamble Capture Default",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_spdif_pinfo,
	.get =		snd_ak4114_spdif_pget,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Q-subcode Capture Default",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_spdif_qinfo,
	.get =		snd_ak4114_spdif_qget,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Audio",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_bit_info,
	.get =		snd_ak4114_in_bit_get,
	.private_value = (1<<31) | (1<<8) | AK4114_REG_RCS0,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 Non-PCM Bitstream",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_bit_info,
	.get =		snd_ak4114_in_bit_get,
	.private_value = (6<<8) | AK4114_REG_RCS0,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 DTS Bitstream",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_bit_info,
	.get =		snd_ak4114_in_bit_get,
	.private_value = (3<<8) | AK4114_REG_RCS0,
},
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		"IEC958 PPL Lock Status",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		snd_ak4114_in_bit_info,
	.get =		snd_ak4114_in_bit_get,
	.private_value = (1<<31) | (4<<8) | AK4114_REG_RCS0,
}
};


static void snd_ak4114_proc_regs_read(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct ak4114 *ak4114 = entry->private_data;
	int reg, val;
	/* all ak4114 registers 0x00 - 0x1f */
	for (reg = 0; reg < 0x20; reg++) {
		val = reg_read(ak4114, reg);
		snd_iprintf(buffer, "0x%02x = 0x%02x\n", reg, val);
	}
}

static void snd_ak4114_proc_init(struct ak4114 *ak4114)
{
	snd_card_ro_proc_new(ak4114->card, "ak4114", ak4114,
			     snd_ak4114_proc_regs_read);
}

int snd_ak4114_build(struct ak4114 *ak4114,
		     struct snd_pcm_substream *ply_substream,
		     struct snd_pcm_substream *cap_substream)
{
	struct snd_kcontrol *kctl;
	unsigned int idx;
	int err;

	if (snd_BUG_ON(!cap_substream))
		return -EINVAL;
	ak4114->playback_substream = ply_substream;
	ak4114->capture_substream = cap_substream;
	for (idx = 0; idx < AK4114_CONTROLS; idx++) {
		kctl = snd_ctl_new1(&snd_ak4114_iec958_controls[idx], ak4114);
		if (kctl == NULL)
			return -ENOMEM;
		if (strstr(kctl->id.name, "Playback")) {
			if (ply_substream == NULL) {
				snd_ctl_free_one(kctl);
				ak4114->kctls[idx] = NULL;
				continue;
			}
			kctl->id.device = ply_substream->pcm->device;
			kctl->id.subdevice = ply_substream->number;
		} else {
			kctl->id.device = cap_substream->pcm->device;
			kctl->id.subdevice = cap_substream->number;
		}
		err = snd_ctl_add(ak4114->card, kctl);
		if (err < 0)
			return err;
		ak4114->kctls[idx] = kctl;
	}
	snd_ak4114_proc_init(ak4114);
	/* trigger workq */
	schedule_delayed_work(&ak4114->work, HZ / 10);
	return 0;
}
EXPORT_SYMBOL(snd_ak4114_build);

/* notify kcontrols if any parameters are changed */
static void ak4114_notify(struct ak4114 *ak4114,
			  unsigned char rcs0, unsigned char rcs1,
			  unsigned char c0, unsigned char c1)
{
	if (!ak4114->kctls[0])
		return;

	if (rcs0 & AK4114_PAR)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[0]->id);
	if (rcs0 & AK4114_V)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[1]->id);
	if (rcs1 & AK4114_CCRC)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[2]->id);
	if (rcs1 & AK4114_QCRC)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[3]->id);

	/* rate change */
	if (c1 & 0xf0)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[4]->id);

	if ((c0 & AK4114_PEM) | (c0 & AK4114_CINT))
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[9]->id);
	if (c0 & AK4114_QINT)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[10]->id);

	if (c0 & AK4114_AUDION)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[11]->id);
	if (c0 & AK4114_AUTO)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[12]->id);
	if (c0 & AK4114_DTSCD)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[13]->id);
	if (c0 & AK4114_UNLCK)
		snd_ctl_notify(ak4114->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &ak4114->kctls[14]->id);
}

int snd_ak4114_external_rate(struct ak4114 *ak4114)
{
	unsigned char rcs1;

	rcs1 = reg_read(ak4114, AK4114_REG_RCS1);
	return external_rate(rcs1);
}
EXPORT_SYMBOL(snd_ak4114_external_rate);

int snd_ak4114_check_rate_and_errors(struct ak4114 *ak4114, unsigned int flags)
{
	struct snd_pcm_runtime *runtime = ak4114->capture_substream ? ak4114->capture_substream->runtime : NULL;
	unsigned long _flags;
	int res = 0;
	unsigned char rcs0, rcs1;
	unsigned char c0, c1;

	rcs1 = reg_read(ak4114, AK4114_REG_RCS1);
	if (flags & AK4114_CHECK_NO_STAT)
		goto __rate;
	rcs0 = reg_read(ak4114, AK4114_REG_RCS0);
	spin_lock_irqsave(&ak4114->lock, _flags);
	if (rcs0 & AK4114_PAR)
		ak4114->errors[AK4114_PARITY_ERRORS]++;
	if (rcs1 & AK4114_V)
		ak4114->errors[AK4114_V_BIT_ERRORS]++;
	if (rcs1 & AK4114_CCRC)
		ak4114->errors[AK4114_CCRC_ERRORS]++;
	if (rcs1 & AK4114_QCRC)
		ak4114->errors[AK4114_QCRC_ERRORS]++;
	c0 = (ak4114->rcs0 & (AK4114_QINT | AK4114_CINT | AK4114_PEM | AK4114_AUDION | AK4114_AUTO | AK4114_UNLCK)) ^
                     (rcs0 & (AK4114_QINT | AK4114_CINT | AK4114_PEM | AK4114_AUDION | AK4114_AUTO | AK4114_UNLCK));
	c1 = (ak4114->rcs1 & 0xf0) ^ (rcs1 & 0xf0);
	ak4114->rcs0 = rcs0 & ~(AK4114_QINT | AK4114_CINT);
	ak4114->rcs1 = rcs1;
	spin_unlock_irqrestore(&ak4114->lock, _flags);

	ak4114_notify(ak4114, rcs0, rcs1, c0, c1);
	if (ak4114->change_callback && (c0 | c1) != 0)
		ak4114->change_callback(ak4114, c0, c1);

      __rate:
	/* compare rate */
	res = external_rate(rcs1);
	if (!(flags & AK4114_CHECK_NO_RATE) && runtime && runtime->rate != res) {
		snd_pcm_stream_lock_irqsave(ak4114->capture_substream, _flags);
		if (snd_pcm_running(ak4114->capture_substream)) {
			// printk(KERN_DEBUG "rate changed (%i <- %i)\n", runtime->rate, res);
			snd_pcm_stop(ak4114->capture_substream, SNDRV_PCM_STATE_DRAINING);
			res = 1;
		}
		snd_pcm_stream_unlock_irqrestore(ak4114->capture_substream, _flags);
	}
	return res;
}
EXPORT_SYMBOL(snd_ak4114_check_rate_and_errors);

static void ak4114_stats(struct work_struct *work)
{
	struct ak4114 *chip = container_of(work, struct ak4114, work.work);

	if (atomic_inc_return(&chip->wq_processing) == 1)
		snd_ak4114_check_rate_and_errors(chip, chip->check_flags);
	if (atomic_dec_and_test(&chip->wq_processing))
		schedule_delayed_work(&chip->work, HZ / 10);
}

#ifdef CONFIG_PM
void snd_ak4114_suspend(struct ak4114 *chip)
{
	atomic_inc(&chip->wq_processing); /* don't schedule new work */
	cancel_delayed_work_sync(&chip->work);
}
EXPORT_SYMBOL(snd_ak4114_suspend);

void snd_ak4114_resume(struct ak4114 *chip)
{
	atomic_dec(&chip->wq_processing);
	snd_ak4114_reinit(chip);
}
EXPORT_SYMBOL(snd_ak4114_resume);
#endif
