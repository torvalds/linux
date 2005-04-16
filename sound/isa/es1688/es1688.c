/*
 *  Driver for generic ESS AudioDrive ESx688 soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("ESS ESx688 AudioDrive");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,ES688 PnP AudioDrive,pnp:ESS0100},"
	        "{ESS,ES1688 PnP AudioDrive,pnp:ESS0102},"
	        "{ESS,ES688 AudioDrive,pnp:ESS6881},"
	        "{ESS,ES1688 AudioDrive,pnp:ESS1681}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ESx688 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ESx688 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ESx688 soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for ESx688 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for ESx688 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for ESx688 driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for ESx688 driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for ESx688 driver.");

static snd_card_t *snd_audiodrive_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_audiodrive_probe(int dev)
{
	static int possible_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dmas[] = {1, 3, 0, -1};
	int xirq, xdma, xmpu_irq;
	snd_card_t *card;
	es1688_t *chip;
	opl3_t *opl3;
	snd_pcm_t *pcm;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	xmpu_irq = mpu_irq[dev];
	xdma = dma8[dev];
	if (xdma == SNDRV_AUTO_DMA) {
		if ((xdma = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA\n");
			return -EBUSY;
		}
	}

	if ((err = snd_es1688_create(card, port[dev], mpu_port[dev],
				     xirq, xmpu_irq, xdma,
				     ES1688_HW_AUTO, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1688_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1688_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "ES1688");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i", pcm->name, chip->port, xirq, xdma);

	if ((snd_opl3_create(card, chip->port, chip->port + 2, OPL3_HW_OPL3, 0, &opl3)) < 0) {
		printk(KERN_ERR "es1688: opl3 not detected at 0x%lx\n", chip->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (xmpu_irq >= 0 && xmpu_irq != SNDRV_AUTO_IRQ && chip->mpu_port > 0) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
					       chip->mpu_port, 0,
					       xmpu_irq,
					       SA_INTERRUPT,
					       NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_audiodrive_cards[dev] = card;
	return 0;

}

static int __init snd_audiodrive_legacy_auto_probe(unsigned long xport)
{
	static int dev;
	int res;
	
	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] != SNDRV_AUTO_PORT)
			continue;
		port[dev] = xport;
		res = snd_audiodrive_probe(dev);
		if (res < 0)
			port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

static int __init alsa_card_es1688_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, -1};
	int dev, cards = 0, i;

	for (dev = cards = 0; dev < SNDRV_CARDS && enable[dev]; dev++) {
		if (port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_audiodrive_probe(dev) >= 0)
			cards++;
	}
	i = snd_legacy_auto_probe(possible_ports, snd_audiodrive_legacy_auto_probe);
	if (i > 0)
		cards += i;

	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "ESS AudioDrive ES1688 soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_es1688_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_audiodrive_cards[idx]);
}

module_init(alsa_card_es1688_init)
module_exit(alsa_card_es1688_exit)
