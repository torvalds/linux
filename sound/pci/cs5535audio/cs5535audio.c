// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for audio on multifunction CS5535/6 companion device
 * Copyright (C) Jaya Kumar
 *
 * Based on Jaroslav Kysela and Takashi Iwai's examples.
 * This work was sponsored by CIS(M) Sdn Bhd.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/io.h>
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

static const struct ac97_quirk ac97_quirks[] = {
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

static const struct pci_device_id snd_cs5535audio_ids[] = {
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
		dev_err(cs5535au->card->dev,
			"Failure writing to cs5535 codec\n");
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
		dev_err(cs5535au->card->dev,
			"Failure reading codec reg 0x%x, Last value=0x%x\n",
			reg, val);

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

static int snd_cs5535audio_mixer(struct cs5535audio *cs5535au)
{
	struct snd_card *card = cs5535au->card;
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static const struct snd_ac97_bus_ops ops = {
		.write = snd_cs5535audio_ac97_codec_write,
		.read = snd_cs5535audio_ac97_codec_read,
	};

	err = snd_ac97_bus(card, 0, &ops, NULL, &pbus);
	if (err < 0)
		return err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.scaps = AC97_SCAP_AUDIO | AC97_SCAP_SKIP_MODEM
			| AC97_SCAP_POWER_SAVE;
	ac97.private_data = cs5535au;
	ac97.pci = cs5535au->pci;

	/* set any OLPC-specific scaps */
	olpc_prequirks(card, &ac97);

	err = snd_ac97_mixer(pbus, &ac97, &cs5535au->ac97);
	if (err < 0) {
		dev_err(card->dev, "mixer failed\n");
		return err;
	}

	snd_ac97_tune_hardware(cs5535au->ac97, ac97_quirks, ac97_quirk);

	err = olpc_quirks(card, cs5535au->ac97);
	if (err < 0) {
		dev_err(card->dev, "olpc quirks failed\n");
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
		snd_pcm_period_elapsed(cs5535au->playback_substream);
	} else {
		dev_err(cs5535au->card->dev,
			"unexpected bm0 irq src, bm_stat=%x\n",
			bm_stat);
	}
}

static void process_bm1_irq(struct cs5535audio *cs5535au)
{
	u8 bm_stat;
	spin_lock(&cs5535au->reg_lock);
	bm_stat = cs_readb(cs5535au, ACC_BM1_STATUS);
	spin_unlock(&cs5535au->reg_lock);
	if (bm_stat & EOP)
		snd_pcm_period_elapsed(cs5535au->capture_substream);
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
				dev_err(cs5535au->card->dev,
					"Unexpected irq src: 0x%x\n",
					acc_irq_stat);
				break;
			}
		}
	}
	return IRQ_HANDLED;
}

static void snd_cs5535audio_free(struct snd_card *card)
{
	olpc_quirks_cleanup();
}

static int snd_cs5535audio_create(struct snd_card *card,
				  struct pci_dev *pci)
{
	struct cs5535audio *cs5535au = card->private_data;
	int err;

	err = pcim_enable_device(pci);
	if (err < 0)
		return err;

	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(32))) {
		dev_warn(card->dev, "unable to get 32bit dma\n");
		return -ENXIO;
	}

	spin_lock_init(&cs5535au->reg_lock);
	cs5535au->card = card;
	cs5535au->pci = pci;
	cs5535au->irq = -1;

	err = pci_request_regions(pci, "CS5535 Audio");
	if (err < 0)
		return err;

	cs5535au->port = pci_resource_start(pci, 0);

	if (devm_request_irq(&pci->dev, pci->irq, snd_cs5535audio_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, cs5535au)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	cs5535au->irq = pci->irq;
	card->sync_irq = cs5535au->irq;
	pci_set_master(pci);

	return 0;
}

static int snd_cs5535audio_probe(struct pci_dev *pci,
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

	err = snd_devm_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
				sizeof(*cs5535au), &card);
	if (err < 0)
		return err;
	cs5535au = card->private_data;
	card->private_free = snd_cs5535audio_free;

	err = snd_cs5535audio_create(card, pci);
	if (err < 0)
		return err;

	err = snd_cs5535audio_mixer(cs5535au);
	if (err < 0)
		return err;

	err = snd_cs5535audio_pcm(cs5535au);
	if (err < 0)
		return err;

	strcpy(card->driver, DRIVER_NAME);

	strcpy(card->shortname, "CS5535 Audio");
	sprintf(card->longname, "%s %s at 0x%lx, irq %i",
		card->shortname, card->driver,
		cs5535au->port, cs5535au->irq);

	err = snd_card_register(card);
	if (err < 0)
		return err;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static struct pci_driver cs5535audio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_cs5535audio_ids,
	.probe = snd_cs5535audio_probe,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &snd_cs5535audio_pm,
	},
#endif
};

module_pci_driver(cs5535audio_driver);

MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CS5535 Audio");
