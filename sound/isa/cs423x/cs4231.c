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
#include <linux/err.h>
#include <linux/platform_device.h>
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

static struct platform_device *devices[SNDRV_CARDS];


static int __init snd_cs4231_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_cs4231 *chip;
	int err;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "specify port\n");
		return -EINVAL;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk(KERN_ERR "specify irq\n");
		return -EINVAL;
	}
	if (dma1[dev] == SNDRV_AUTO_DMA) {
		snd_printk(KERN_ERR "specify dma1\n");
		return -EINVAL;
	}
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if ((err = snd_cs4231_create(card, port[dev], -1,
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0, &chip)) < 0)
		goto _err;
	card->private_data = chip;

	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0)
		goto _err;

	strcpy(card->driver, "CS4231");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		pcm->name, chip->port, irq[dev], dma1[dev]);
	if (dma2[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);

	if ((err = snd_cs4231_mixer(chip)) < 0)
		goto _err;
	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0)
		goto _err;

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (mpu_irq[dev] == SNDRV_AUTO_IRQ)
			mpu_irq[dev] = -1;
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					mpu_port[dev], 0,
					mpu_irq[dev],
					mpu_irq[dev] >= 0 ? SA_INTERRUPT : 0,
					NULL) < 0)
			printk(KERN_WARNING "cs4231: MPU401 not detected\n");
	}

	snd_card_set_dev(card, &pdev->dev);

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	platform_set_drvdata(pdev, card);
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int __devexit snd_cs4231_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_cs4231_suspend(struct platform_device *dev, pm_message_t state)
{
	struct snd_card *card;
	struct snd_cs4231 *chip;
	card = platform_get_drvdata(dev);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	chip = card->private_data;
	chip->suspend(chip);
	return 0;
}

static int snd_cs4231_resume(struct platform_device *dev)
{
	struct snd_card *card;
	struct snd_cs4231 *chip;
	card = platform_get_drvdata(dev);
	chip = card->private_data;
	chip->resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

#define SND_CS4231_DRIVER	"snd_cs4231"

static struct platform_driver snd_cs4231_driver = {
	.probe		= snd_cs4231_probe,
	.remove		= __devexit_p(snd_cs4231_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cs4231_suspend,
	.resume		= snd_cs4231_resume,
#endif
	.driver		= {
		.name	= SND_CS4231_DRIVER
	},
};

static void __init_or_module snd_cs4231_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_cs4231_driver);
}

static int __init alsa_card_cs4231_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_cs4231_driver);
	if (err < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS && enable[i]; i++) {
		struct platform_device *device;
		device = platform_device_register_simple(SND_CS4231_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device)) {
			err = PTR_ERR(device);
			goto errout;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "CS4231 soundcard not found or device busy\n");
#endif
		err = -ENODEV;
		goto errout;
	}
	return 0;

 errout:
	snd_cs4231_unregister_all();
	return err;
}

static void __exit alsa_card_cs4231_exit(void)
{
	snd_cs4231_unregister_all();
}

module_init(alsa_card_cs4231_init)
module_exit(alsa_card_cs4231_exit)
