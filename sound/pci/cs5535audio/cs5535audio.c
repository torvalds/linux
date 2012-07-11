/*
 * Driver for audio on multifunction CS5535/6 companion device
 * Copyright (C) Jaya Kumar
 *
 * Based on Jaroslav Kysela and Takashi Iwai's examples.
 * This work was sponsored by CIS(M) Sdn Bhd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/asoundef.h>
#include "cs5535audio.h"

#define DRIVER_NAME "cs5535audio"

static char *ac97_quirk;
module_param(ac97_quirk, charp, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 board specific workarounds.");

static struct ac97_quirk ac97_quirks[] __devinitdata = {
#if 0 /* Not yet confirmed if all 5536 boards are HP only */
	{
		.subvendor = PCI_VENDOR_ID_AMD, 
		.subdevice = PCI_DEVICE_ID_AMD_CS5536_AUDIO, 
		.name = "AMD RDK",     
		.type = AC97_TUNE_HP_ONLY
	},
#endif
	{}
};

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " DRIVER_NAME);
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " DRIVER_NAME);
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " DRIVER_NAME);

static DEFINE_PCI_DEVICE_TABLE(snd_cs5535audio_ids) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_CS5535_AUDIO) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_AUDIO) },
	{}
};

MODULE_DEVICE_TABLE(pci, snd_cs5535audio_ids);

static void wait_till_cmd_acked(struct cs5535audio *cs5535au, unsigned long timeout)
{
	unsigned int tmp;
	do {
		tmp = cs_readl(cs5535au, ACC_CODEC_CNTL);
		if (!(tmp & CMD_NEW))
			break;
		udelay(1);
	} while (--timeout);
	if (!timeout)
		snd_printk(KERN_ERR "Failure writing to cs5535 codec\n");
}

static unsigned short snd_cs5535audio_codec_read(struct cs5535audio *cs5535au,
						 unsigned short reg)
{
	unsigned int regdata;
	unsigned int timeout;
	unsigned int val;

	regdata = ((unsigned int) reg) << 24;
	regdata |= ACC_CODEC_CNTL_RD_CMD;
	regdata |= CMD_NEW;

	cs_writel(cs5535au, ACC_CODEC_CNTL, regdata);
	wait_till_cmd_acked(cs5535au, 50);

	timeout = 50;
	do {
		val = cs_readl(cs5535au, ACC_CODEC_STATUS);
		if ((val & STS_NEW) && reg == (val >> 24))
			break;
		udelay(1);
	} while (--timeout);
	if (!timeout)
		snd_printk(KERN_ERR "Failure reading codec reg 0x%x,"
					"Last value=0x%x\n", reg, val);

	return (unsigned short) val;
}

static void snd_cs5535audio_codec_write(struct cs5535audio *cs5535au,
					unsigned short reg, unsigned short val)
{
	unsigned int regdata;

	regdata = ((unsigned int) reg) << 24;
	regdata |= val;
	regdata &= CMD_MASK;
	regdata |= CMD_NEW;
	regdata &= ACC_CODEC_CNTL_WR_CMD;

	cs_writel(cs5535au, ACC_CODEC_CNTL, regdata);
	wait_till_cmd_acked(cs5535au, 50);
}

static void snd_cs5535audio_ac97_codec_write(struct snd_ac97 *ac97,
					     unsigned short reg, unsigned short val)
{
	struct cs5535audio *cs5535au = ac97->private_data;
	snd_cs5535audio_codec_write(cs5535au, reg, val);
}

static unsigned short snd_cs5535audio_ac97_codec_read(struct snd_ac97 *ac97,
						      unsigned short reg)
{
	struct cs5535audio *cs5535au = ac97->private_data;
	return snd_cs5535audio_codec_read(cs5535au, reg);
}

static int __devinit snd_cs5535audio_mixer(struct cs5535audio *cs5535au)
{
	struct snd_card *card = cs5535au->card;
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_cs5535audio_ac97_codec_write,
		.read = snd_cs5535audio_ac97_codec_read,
	};

	if ((err = snd_ac97_bus(card, 0, &ops, NULL, &pbus)) < 0)
		return err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.scaps = AC97_SCAP_AUDIO | AC97_SCAP_SKIP_MODEM
			| AC97_SCAP_POWER_SAVE;
	ac97.private_data = cs5535au;
	ac97.pci = cs5535au->pci;

	/* set any OLPC-specific scaps */
	olpc_prequirks(card, &ac97);

	if ((err = snd_ac97_mixer(pbus, &ac97, &cs5535au->ac97)) < 0) {
		snd_printk(KERN_ERR "mixer failed\n");
		return err;
	}

	snd_ac97_tune_hardware(cs5535au->ac97, ac97_quirks, ac97_quirk);

	err = olpc_quirks(card, cs5535au->ac97);
	if (err < 0) {
		snd_printk(KERN_ERR "olpc quirks failed\n");
		return err;
	}

	return 0;
}

static void process_bm0_irq(struct cs5535audio *cs5535au)
{
	u8 bm_stat;
	spin_lock(&cs5535au->reg_lock);
	bm_stat = cs_readb(cs5535au, ACC_BM0_STATUS);
	spin_unlock(&cs5535au->reg_lock);
	if (bm_stat & EOP) {
		struct cs5535audio_dma *dma;
		dma = cs5535au->playback_substream->runtime->private_data;
		snd_pcm_period_elapsed(cs5535au->playback_substream);
	} else {
		snd_printk(KERN_ERR "unexpected bm0 irq src, bm_stat=%x\n",
					bm_stat);
	}
}

static void process_bm1_irq(struct cs5535audio *cs5535au)
{
	u8 bm_stat;
	spin_lock(&cs5535au->reg_lock);
	bm_stat = cs_readb(cs5535au, ACC_BM1_STATUS);
	spin_unlock(&cs5535au->reg_lock);
	if (bm_stat & EOP) {
		struct cs5535audio_dma *dma;
		dma = cs5535au->capture_substream->runtime->private_data;
		snd_pcm_period_elapsed(cs5535au->capture_substream);
	}
}

static irqreturn_t snd_cs5535audio_interrupt(int irq, void *dev_id)
{
	u16 acc_irq_stat;
	unsigned char count;
	struct cs5535audio *cs5535au = dev_id;

	if (cs5535au == NULL)
		return IRQ_NONE;

	acc_irq_stat = cs_readw(cs5535au, ACC_IRQ_STATUS);

	if (!acc_irq_stat)
		return IRQ_NONE;
	for (count = 0; count < 4; count++) {
		if (acc_irq_stat & (1 << count)) {
			switch (count) {
			case IRQ_STS:
				cs_readl(cs5535au, ACC_GPIO_STATUS);
				break;
			case WU_IRQ_STS:
				cs_readl(cs5535au, ACC_GPIO_STATUS);
				break;
			case BM0_IRQ_STS:
				process_bm0_irq(cs5535au);
				break;
			case BM1_IRQ_STS:
				process_bm1_irq(cs5535au);
				break;
			default:
				snd_printk(KERN_ERR "Unexpected irq src: "
						"0x%x\n", acc_irq_stat);
				break;
			}
		}
	}
	return IRQ_HANDLED;
}

static int snd_cs5535audio_free(struct cs5535audio *cs5535au)
{
	synchronize_irq(cs5535au->irq);
	pci_set_power_state(cs5535au->pci, 3);

	if (cs5535au->irq >= 0)
		free_irq(cs5535au->irq, cs5535au);

	pci_release_regions(cs5535au->pci);
	pci_disable_device(cs5535au->pci);
	kfree(cs5535au);
	return 0;
}

static int snd_cs5535audio_dev_free(struct snd_device *device)
{
	struct cs5535audio *cs5535au = device->device_data;
	return snd_cs5535audio_free(cs5535au);
}

static int __devinit snd_cs5535audio_create(struct snd_card *card,
					    struct pci_dev *pci,
					    struct cs5535audio **rcs5535au)
{
	struct cs5535audio *cs5535au;

	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_cs5535audio_dev_free,
	};

	*rcs5535au = NULL;
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if (pci_set_dma_mask(pci, DMA_BIT_MASK(32)) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(32)) < 0) {
		printk(KERN_WARNING "unable to get 32bit dma\n");
		err = -ENXIO;
		goto pcifail;
	}

	cs5535au = kzalloc(sizeof(*cs5535au), GFP_KERNEL);
	if (cs5535au == NULL) {
		err = -ENOMEM;
		goto pcifail;
	}

	spin_lock_init(&cs5535au->reg_lock);
	cs5535au->card = card;
	cs5535au->pci = pci;
	cs5535au->irq = -1;

	if ((err = pci_request_regions(pci, "CS5535 Audio")) < 0) {
		kfree(cs5535au);
		goto pcifail;
	}

	cs5535au->port = pci_resource_start(pci, 0);

	if (request_irq(pci->irq, snd_cs5535audio_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, cs5535au)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto sndfail;
	}

	cs5535au->irq = pci->irq;
	pci_set_master(pci);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  cs5535au, &ops)) < 0)
		goto sndfail;

	snd_card_set_dev(card, &pci->dev);

	*rcs5535au = cs5535au;
	return 0;

sndfail: /* leave the device alive, just kill the snd */
	snd_cs5535audio_free(cs5535au);
	return err;

pcifail:
	pci_disable_device(pci);
	return err;
}

static int __devinit snd_cs5535audio_probe(struct pci_dev *pci,
					   const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct cs5535audio *cs5535au;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	if ((err = snd_cs5535audio_create(card, pci, &cs5535au)) < 0)
		goto probefail_out;

	card->private_data = cs5535au;

	if ((err = snd_cs5535audio_mixer(cs5535au)) < 0)
		goto probefail_out;

	if ((err = snd_cs5535audio_pcm(cs5535au)) < 0)
		goto probefail_out;

	strcpy(card->driver, DRIVER_NAME);

	strcpy(card->shortname, "CS5535 Audio");
	sprintf(card->longname, "%s %s at 0x%lx, irq %i",
		card->shortname, card->driver,
		cs5535au->port, cs5535au->irq);

	if ((err = snd_card_register(card)) < 0)
		goto probefail_out;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;

probefail_out:
	snd_card_free(card);
	return err;
}

static void __devexit snd_cs5535audio_remove(struct pci_dev *pci)
{
	olpc_quirks_cleanup();
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver cs5535audio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_cs5535audio_ids,
	.probe = snd_cs5535audio_probe,
	.remove = __devexit_p(snd_cs5535audio_remove),
#ifdef CONFIG_PM
	.suspend = snd_cs5535audio_suspend,
	.resume = snd_cs5535audio_resume,
#endif
};

module_pci_driver(cs5535audio_driver);

MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CS5535 Audio");
MODULE_SUPPORTED_DEVICE("CS5535 Audio");
