/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Uros Bizjak <uros@kss-loka.si>
 *
 *  Lowlevel routines for control of Sound Blaster cards
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/initval.h>

#include <asm/io.h>
#include <asm/dma.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
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
	snd_printd("%s [0x%lx]: timeout (0x%x)\n", __FUNCTION__, chip->port, val);
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
	snd_printd("%s [0x%lx]: timeout\n", __FUNCTION__, chip->port);
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
	snd_printdd("%s [0x%lx] failed...\n", __FUNCTION__, chip->port);
	return -ENODEV;
}

static int snd_sbdsp_version(struct snd_sb * chip)
{
	unsigned int result = -ENODEV;

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
	default:
		return -ENODEV;
	}
	sprintf(chip->name, "Sound Blaster %s", str);
	chip->version = (major << 8) | minor;
	return 0;
}

static int snd_sbdsp_free(struct snd_sb *chip)
{
	if (chip->res_port)
		release_and_free_resource(chip->res_port);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *) chip);
#ifdef CONFIG_ISA
	if (chip->dma8 >= 0) {
		disable_dma(chip->dma8);
		free_dma(chip->dma8);
	}
	if (chip->dma16 >= 0 && chip->dma16 != chip->dma8) {
		disable_dma(chip->dma16);
		free_dma(chip->dma16);
	}
#endif
	kfree(chip);
	return 0;
}

static int snd_sbdsp_dev_free(struct snd_device *device)
{
	struct snd_sb *chip = device->device_data;
	return snd_sbdsp_free(chip);
}

int snd_sbdsp_create(struct snd_card *card,
		     unsigned long port,
		     int irq,
		     irqreturn_t (*irq_handler)(int, void *, struct pt_regs *),
		     int dma8,
		     int dma16,
		     unsigned short hardware,
		     struct snd_sb **r_chip)
{
	struct snd_sb *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_sbdsp_dev_free,
	};

	snd_assert(r_chip != NULL, return -EINVAL);
	*r_chip = NULL;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->open_lock);
	spin_lock_init(&chip->midi_input_lock);
	spin_lock_init(&chip->mixer_lock);
	chip->irq = -1;
	chip->dma8 = -1;
	chip->dma16 = -1;
	chip->port = port;
	
	if (request_irq(irq, irq_handler, hardware == SB_HW_ALS4000 ?
			SA_INTERRUPT | SA_SHIRQ : SA_INTERRUPT,
			"SoundBlaster", (void *) chip)) {
		snd_printk(KERN_ERR "sb: can't grab irq %d\n", irq);
		snd_sbdsp_free(chip);
		return -EBUSY;
	}
	chip->irq = irq;

	if (hardware == SB_HW_ALS4000)
		goto __skip_allocation;
	
	if ((chip->res_port = request_region(port, 16, "SoundBlaster")) == NULL) {
		snd_printk(KERN_ERR "sb: can't grab port 0x%lx\n", port);
		snd_sbdsp_free(chip);
		return -EBUSY;
	}

#ifdef CONFIG_ISA
	if (dma8 >= 0 && request_dma(dma8, "SoundBlaster - 8bit")) {
		snd_printk(KERN_ERR "sb: can't grab DMA8 %d\n", dma8);
		snd_sbdsp_free(chip);
		return -EBUSY;
	}
	chip->dma8 = dma8;
	if (dma16 >= 0) {
		if (hardware != SB_HW_ALS100 && (dma16 < 5 || dma16 > 7)) {
			/* no duplex */
			dma16 = -1;
		} else if (request_dma(dma16, "SoundBlaster - 16bit")) {
			snd_printk(KERN_ERR "sb: can't grab DMA16 %d\n", dma16);
			snd_sbdsp_free(chip);
			return -EBUSY;
		}
	}
	chip->dma16 = dma16;
#endif

      __skip_allocation:
	chip->card = card;
	chip->hardware = hardware;
	if ((err = snd_sbdsp_probe(chip)) < 0) {
		snd_sbdsp_free(chip);
		return err;
	}
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_sbdsp_free(chip);
		return err;
	}
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

/*
 *  INIT part
 */

static int __init alsa_sb_common_init(void)
{
	return 0;
}

static void __exit alsa_sb_common_exit(void)
{
}

module_init(alsa_sb_common_init)
module_exit(alsa_sb_common_exit)
