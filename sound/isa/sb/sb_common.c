// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Uros Bizjak <uros@kss-loka.si>
 *
 *  Lowlevel routines for control of Sound Blaster cards
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/initval.h>

#include <asm/dma.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("ALSA lowlevel driver for Sound Blaster cards");
MODULE_LICENSE("GPL");

#define BUSY_LOOPS 100000

#undef IO_DEBUG

int snd_sbdsp_command(struct snd_sb *chip, unsigned char val)
{
	int i;
#ifdef IO_DEBUG
	snd_printk(KERN_DEBUG "command 0x%x\n", val);
#endif
	for (i = BUSY_LOOPS; i; i--)
		if ((inb(SBP(chip, STATUS)) & 0x80) == 0) {
			outb(val, SBP(chip, COMMAND));
			return 1;
		}
	snd_printd("%s [0x%lx]: timeout (0x%x)\n", __func__, chip->port, val);
	return 0;
}

int snd_sbdsp_get_byte(struct snd_sb *chip)
{
	int val;
	int i;
	for (i = BUSY_LOOPS; i; i--) {
		if (inb(SBP(chip, DATA_AVAIL)) & 0x80) {
			val = inb(SBP(chip, READ));
#ifdef IO_DEBUG
			snd_printk(KERN_DEBUG "get_byte 0x%x\n", val);
#endif
			return val;
		}
	}
	snd_printd("%s [0x%lx]: timeout\n", __func__, chip->port);
	return -ENODEV;
}

int snd_sbdsp_reset(struct snd_sb *chip)
{
	int i;

	outb(1, SBP(chip, RESET));
	udelay(10);
	outb(0, SBP(chip, RESET));
	udelay(30);
	for (i = BUSY_LOOPS; i; i--)
		if (inb(SBP(chip, DATA_AVAIL)) & 0x80) {
			if (inb(SBP(chip, READ)) == 0xaa)
				return 0;
			else
				break;
		}
	snd_printdd("%s [0x%lx] failed...\n", __func__, chip->port);
	return -ENODEV;
}

static int snd_sbdsp_version(struct snd_sb * chip)
{
	unsigned int result;

	snd_sbdsp_command(chip, SB_DSP_GET_VERSION);
	result = (short) snd_sbdsp_get_byte(chip) << 8;
	result |= (short) snd_sbdsp_get_byte(chip);
	return result;
}

static int snd_sbdsp_probe(struct snd_sb * chip)
{
	int version;
	int major, minor;
	char *str;
	unsigned long flags;

	/*
	 *  initialization sequence
	 */

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (snd_sbdsp_reset(chip) < 0) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return -ENODEV;
	}
	version = snd_sbdsp_version(chip);
	if (version < 0) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return -ENODEV;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	major = version >> 8;
	minor = version & 0xff;
	snd_printdd("SB [0x%lx]: DSP chip found, version = %i.%i\n",
		    chip->port, major, minor);

	switch (chip->hardware) {
	case SB_HW_AUTO:
		switch (major) {
		case 1:
			chip->hardware = SB_HW_10;
			str = "1.0";
			break;
		case 2:
			if (minor) {
				chip->hardware = SB_HW_201;
				str = "2.01+";
			} else {
				chip->hardware = SB_HW_20;
				str = "2.0";
			}
			break;
		case 3:
			chip->hardware = SB_HW_PRO;
			str = "Pro";
			break;
		case 4:
			chip->hardware = SB_HW_16;
			str = "16";
			break;
		default:
			snd_printk(KERN_INFO "SB [0x%lx]: unknown DSP chip version %i.%i\n",
				   chip->port, major, minor);
			return -ENODEV;
		}
		break;
	case SB_HW_ALS100:
		str = "16 (ALS-100)";
		break;
	case SB_HW_ALS4000:
		str = "16 (ALS-4000)";
		break;
	case SB_HW_DT019X:
		str = "(DT019X/ALS007)";
		break;
	case SB_HW_CS5530:
		str = "16 (CS5530)";
		break;
	case SB_HW_JAZZ16:
		str = "Pro (Jazz16)";
		break;
	default:
		return -ENODEV;
	}
	sprintf(chip->name, "Sound Blaster %s", str);
	chip->version = (major << 8) | minor;
	return 0;
}

int snd_sbdsp_create(struct snd_card *card,
		     unsigned long port,
		     int irq,
		     irq_handler_t irq_handler,
		     int dma8,
		     int dma16,
		     unsigned short hardware,
		     struct snd_sb **r_chip)
{
	struct snd_sb *chip;
	int err;

	if (snd_BUG_ON(!r_chip))
		return -EINVAL;
	*r_chip = NULL;
	chip = devm_kzalloc(card->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->open_lock);
	spin_lock_init(&chip->midi_input_lock);
	spin_lock_init(&chip->mixer_lock);
	chip->irq = -1;
	chip->dma8 = -1;
	chip->dma16 = -1;
	chip->port = port;
	
	if (devm_request_irq(card->dev, irq, irq_handler,
			     (hardware == SB_HW_ALS4000 ||
			      hardware == SB_HW_CS5530) ?
			     IRQF_SHARED : 0,
			     "SoundBlaster", (void *) chip)) {
		snd_printk(KERN_ERR "sb: can't grab irq %d\n", irq);
		return -EBUSY;
	}
	chip->irq = irq;
	card->sync_irq = chip->irq;

	if (hardware == SB_HW_ALS4000)
		goto __skip_allocation;
	
	chip->res_port = devm_request_region(card->dev, port, 16,
					     "SoundBlaster");
	if (!chip->res_port) {
		snd_printk(KERN_ERR "sb: can't grab port 0x%lx\n", port);
		return -EBUSY;
	}

#ifdef CONFIG_ISA
	if (dma8 >= 0 && snd_devm_request_dma(card->dev, dma8,
					      "SoundBlaster - 8bit")) {
		snd_printk(KERN_ERR "sb: can't grab DMA8 %d\n", dma8);
		return -EBUSY;
	}
	chip->dma8 = dma8;
	if (dma16 >= 0) {
		if (hardware != SB_HW_ALS100 && (dma16 < 5 || dma16 > 7)) {
			/* no duplex */
			dma16 = -1;
		} else if (snd_devm_request_dma(card->dev, dma16,
						"SoundBlaster - 16bit")) {
			snd_printk(KERN_ERR "sb: can't grab DMA16 %d\n", dma16);
			return -EBUSY;
		}
	}
	chip->dma16 = dma16;
#endif

      __skip_allocation:
	chip->card = card;
	chip->hardware = hardware;
	err = snd_sbdsp_probe(chip);
	if (err < 0)
		return err;
	*r_chip = chip;
	return 0;
}

EXPORT_SYMBOL(snd_sbdsp_command);
EXPORT_SYMBOL(snd_sbdsp_get_byte);
EXPORT_SYMBOL(snd_sbdsp_reset);
EXPORT_SYMBOL(snd_sbdsp_create);
/* sb_mixer.c */
EXPORT_SYMBOL(snd_sbmixer_write);
EXPORT_SYMBOL(snd_sbmixer_read);
EXPORT_SYMBOL(snd_sbmixer_new);
EXPORT_SYMBOL(snd_sbmixer_add_ctl);
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_sbmixer_suspend);
EXPORT_SYMBOL(snd_sbmixer_resume);
#endif
