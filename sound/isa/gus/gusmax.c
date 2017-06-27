/*
 *  Driver for Gravis UltraSound MAX soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
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

#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/wss.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Gravis UltraSound MAX");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Gravis,UltraSound MAX}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x230,0x240,0x250,0x260 */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 2,3,5,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for GUS MAX soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for GUS MAX soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable GUS MAX soundcard.");
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for GUS MAX driver.");
module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for GUS MAX driver.");
module_param_hw_array(dma1, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for GUS MAX driver.");
module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for GUS MAX driver.");
module_param_array(joystick_dac, int, NULL, 0444);
MODULE_PARM_DESC(joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS MAX driver.");
module_param_array(channels, int, NULL, 0444);
MODULE_PARM_DESC(channels, "Used GF1 channels for GUS MAX driver.");
module_param_array(pcm_channels, int, NULL, 0444);
MODULE_PARM_DESC(pcm_channels, "Reserved PCM channels for GUS MAX driver.");

struct snd_gusmax {
	int irq;
	struct snd_card *card;
	struct snd_gus_card *gus;
	struct snd_wss *wss;
	unsigned short gus_status_reg;
	unsigned short pcm_status_reg;
};

#define PFX	"gusmax: "

static int snd_gusmax_detect(struct snd_gus_card *gus)
{
	unsigned char d;

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
		snd_printdd("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
		snd_printdd("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}

	return 0;
}

static irqreturn_t snd_gusmax_interrupt(int irq, void *dev_id)
{
	struct snd_gusmax *maxcard = dev_id;
	int loop, max = 5;
	int handled = 0;

	do {
		loop = 0;
		if (inb(maxcard->gus_status_reg)) {
			handled = 1;
			snd_gus_interrupt(irq, maxcard->gus);
			loop++;
		}
		if (inb(maxcard->pcm_status_reg) & 0x01) { /* IRQ bit is set? */
			handled = 1;
			snd_wss_interrupt(irq, maxcard->wss);
			loop++;
		}
	} while (loop && --max > 0);
	return IRQ_RETVAL(handled);
}

static void snd_gusmax_init(int dev, struct snd_card *card,
			    struct snd_gus_card *gus)
{
	gus->equal_irq = 1;
	gus->codec_flag = 1;
	gus->joystick_dac = joystick_dac[dev];
	/* init control register */
	gus->max_cntrl_val = (gus->gf1.port >> 4) & 0x0f;
	if (gus->gf1.dma1 > 3)
		gus->max_cntrl_val |= 0x10;
	if (gus->gf1.dma2 > 3)
		gus->max_cntrl_val |= 0x20;
	gus->max_cntrl_val |= 0x40;
	outb(gus->max_cntrl_val, GUSP(gus, MAXCNTRLPORT));
}

static int snd_gusmax_mixer(struct snd_wss *chip)
{
	struct snd_card *card = chip->card;
	struct snd_ctl_elem_id id1, id2;
	int err;
	
	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUXA to SYNTHESIZER */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "Synth Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Synth Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign AUXB to CD */
	strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
	strcpy(id2.name, "CD Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "CD Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
#if 0
	/* reassign Mono Input to MIC */
	if (snd_mixer_group_rename(mixer,
				SNDRV_MIXER_IN_MONO, 0,
				SNDRV_MIXER_IN_MIC, 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				SNDRV_MIXER_IN_MONO, 0, SNDRV_MIXER_ETYPE_INPUT,
				SNDRV_MIXER_IN_MIC, 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				"Mono Capture Volume", 0, SNDRV_MIXER_ETYPE_VOLUME1,
				"Mic Capture Volume", 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				"Mono Capture Switch", 0, SNDRV_MIXER_ETYPE_SWITCH1,
				"Mic Capture Switch", 0) < 0)
		goto __error;
#endif
	return 0;
}

static void snd_gusmax_free(struct snd_card *card)
{
	struct snd_gusmax *maxcard = card->private_data;
	
	if (maxcard == NULL)
		return;
	if (maxcard->irq >= 0)
		free_irq(maxcard->irq, (void *)maxcard);
}

static int snd_gusmax_match(struct device *pdev, unsigned int dev)
{
	return enable[dev];
}

static int snd_gusmax_probe(struct device *pdev, unsigned int dev)
{
	static int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, -1};
	static int possible_dmas[] = {5, 6, 7, 1, 3, -1};
	int xirq, xdma1, xdma2, err;
	struct snd_card *card;
	struct snd_gus_card *gus = NULL;
	struct snd_wss *wss;
	struct snd_gusmax *maxcard;

	err = snd_card_new(pdev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct snd_gusmax), &card);
	if (err < 0)
		return err;
	card->private_free = snd_gusmax_free;
	maxcard = card->private_data;
	maxcard->card = card;
	maxcard->irq = -1;
	
	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma1 = dma1[dev];
	if (xdma1 == SNDRV_AUTO_DMA) {
		if ((xdma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA1\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma2 = dma2[dev];
	if (xdma2 == SNDRV_AUTO_DMA) {
		if ((xdma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA2\n");
			err = -EBUSY;
			goto _err;
		}
	}

	if (port[dev] != SNDRV_AUTO_PORT) {
		err = snd_gus_create(card,
				     port[dev],
				     -xirq, xdma1, xdma2,
				     0, channels[dev],
				     pcm_channels[dev],
				     0, &gus);
	} else {
		static unsigned long possible_ports[] = {
			0x220, 0x230, 0x240, 0x250, 0x260
		};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			err = snd_gus_create(card,
					     possible_ports[i],
					     -xirq, xdma1, xdma2,
					     0, channels[dev],
					     pcm_channels[dev],
					     0, &gus);
			if (err >= 0) {
				port[dev] = possible_ports[i];
				break;
			}
		}
	}
	if (err < 0)
		goto _err;

	if ((err = snd_gusmax_detect(gus)) < 0)
		goto _err;

	maxcard->gus_status_reg = gus->gf1.reg_irqstat;
	maxcard->pcm_status_reg = gus->gf1.port + 0x10c + 2;
	snd_gusmax_init(dev, card, gus);
	if ((err = snd_gus_initialize(gus)) < 0)
		goto _err;

	if (!gus->max_flag) {
		snd_printk(KERN_ERR PFX "GUS MAX soundcard was not detected at 0x%lx\n", gus->gf1.port);
		err = -ENODEV;
		goto _err;
	}

	if (request_irq(xirq, snd_gusmax_interrupt, 0, "GUS MAX", (void *)maxcard)) {
		snd_printk(KERN_ERR PFX "unable to grab IRQ %d\n", xirq);
		err = -EBUSY;
		goto _err;
	}
	maxcard->irq = xirq;
	
	err = snd_wss_create(card,
			     gus->gf1.port + 0x10c, -1, xirq,
			     xdma2 < 0 ? xdma1 : xdma2, xdma1,
			     WSS_HW_DETECT,
			     WSS_HWSHARE_IRQ |
			     WSS_HWSHARE_DMA1 |
			     WSS_HWSHARE_DMA2,
			     &wss);
	if (err < 0)
		goto _err;

	err = snd_wss_pcm(wss, 0);
	if (err < 0)
		goto _err;

	err = snd_wss_mixer(wss);
	if (err < 0)
		goto _err;

	err = snd_wss_timer(wss, 2);
	if (err < 0)
		goto _err;

	if (pcm_channels[dev] > 0) {
		if ((err = snd_gf1_pcm_new(gus, 1, 1)) < 0)
			goto _err;
	}
	err = snd_gusmax_mixer(wss);
	if (err < 0)
		goto _err;

	err = snd_gf1_rawmidi_new(gus, 0);
	if (err < 0)
		goto _err;

	sprintf(card->longname + strlen(card->longname), " at 0x%lx, irq %i, dma %i", gus->gf1.port, xirq, xdma1);
	if (xdma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%i", xdma2);

	err = snd_card_register(card);
	if (err < 0)
		goto _err;
		
	maxcard->gus = gus;
	maxcard->wss = wss;

	dev_set_drvdata(pdev, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int snd_gusmax_remove(struct device *devptr, unsigned int dev)
{
	snd_card_free(dev_get_drvdata(devptr));
	return 0;
}

#define DEV_NAME "gusmax"

static struct isa_driver snd_gusmax_driver = {
	.match		= snd_gusmax_match,
	.probe		= snd_gusmax_probe,
	.remove		= snd_gusmax_remove,
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= DEV_NAME
	},
};

module_isa_driver(snd_gusmax_driver, SNDRV_CARDS);
