/*
 *  Generic driver for CS4231 chips
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Originally the CS4232/CS4232A driver, modified for use on CS4231 by
 *  Tugrul Galatali <galatalt@stuy.edu>
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
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Generic CS4231");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Crystal Semiconductors,CS4231}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for CS4231 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for CS4231 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable CS4231 soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for CS4231 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for CS4231 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for CS4231 driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for CS4231 driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for CS4231 driver.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for CS4231 driver.");

static snd_card_t *snd_cs4231_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_card_cs4231_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_cs4231 *acard;
	snd_pcm_t *pcm = NULL;
	cs4231_t *chip;
	int err;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify port\n");
		return -EINVAL;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk("specify irq\n");
		return -EINVAL;
	}
	if (dma1[dev] == SNDRV_AUTO_DMA) {
		snd_printk("specify dma1\n");
		return -EINVAL;
	}
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_card_cs4231 *)card->private_data;
	if ((err = snd_cs4231_create(card, port[dev], -1,
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "CS4231");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		pcm->name, chip->port, irq[dev], dma1[dev]);
	if (dma2[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);

	if ((err = snd_cs4231_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (mpu_irq[dev] == SNDRV_AUTO_IRQ)
			mpu_irq[dev] = -1;
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					mpu_port[dev], 0,
					mpu_irq[dev],
					mpu_irq[dev] >= 0 ? SA_INTERRUPT : 0,
					NULL) < 0)
			printk(KERN_ERR "cs4231: MPU401 not detected\n");
	}
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_cs4231_cards[dev] = card;
	return 0;
}

static int __init alsa_card_cs4231_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && enable[dev]; dev++) {
		if (snd_card_cs4231_probe(dev) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "CS4231 soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_cs4231_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_cs4231_cards[idx]);
}

module_init(alsa_card_cs4231_init)
module_exit(alsa_card_cs4231_exit)
