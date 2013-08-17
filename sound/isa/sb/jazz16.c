
/*
 * jazz16.c - driver for Media Vision Jazz16 based soundcards.
 * Copyright (C) 2009 Krzysztof Helt <krzysztof.h1@wp.pl>
 * Based on patches posted by Rask Ingemann Lambertsen and Rene Herman.
 * Based on OSS Sound Blaster driver.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/dma.h>
#include <linux/isa.h>
#include <sound/core.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

#define PFX "jazz16: "

MODULE_DESCRIPTION("Media Vision Jazz16");
MODULE_SUPPORTED_DEVICE("{{Media Vision ??? },"
		"{RTL,RTL3000}}");

MODULE_AUTHOR("Krzysztof Helt <krzysztof.h1@wp.pl>");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static unsigned long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static unsigned long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int dma16[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Media Vision Jazz16 based soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Media Vision Jazz16 based soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Media Vision Jazz16 based soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for jazz16 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for jazz16 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for jazz16 driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for jazz16 driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "DMA8 # for jazz16 driver.");
module_param_array(dma16, int, NULL, 0444);
MODULE_PARM_DESC(dma16, "DMA16 # for jazz16 driver.");

#define SB_JAZZ16_WAKEUP	0xaf
#define SB_JAZZ16_SET_PORTS	0x50
#define SB_DSP_GET_JAZZ_BRD_REV	0xfa
#define SB_JAZZ16_SET_DMAINTR	0xfb
#define SB_DSP_GET_JAZZ_MODEL	0xfe

struct snd_card_jazz16 {
	struct snd_sb *chip;
};

static irqreturn_t jazz16_interrupt(int irq, void *chip)
{
	return snd_sb8dsp_interrupt(chip);
}

static int __devinit jazz16_configure_ports(unsigned long port,
					    unsigned long mpu_port, int idx)
{
	unsigned char val;

	if (!request_region(0x201, 1, "jazz16 config")) {
		snd_printk(KERN_ERR "config port region is already in use.\n");
		return -EBUSY;
	}
	outb(SB_JAZZ16_WAKEUP - idx, 0x201);
	udelay(100);
	outb(SB_JAZZ16_SET_PORTS + idx, 0x201);
	udelay(100);
	val = port & 0x70;
	val |= (mpu_port & 0x30) >> 4;
	outb(val, 0x201);

	release_region(0x201, 1);
	return 0;
}

static int __devinit jazz16_detect_board(unsigned long port,
					 unsigned long mpu_port)
{
	int err;
	int val;
	struct snd_sb chip;

	if (!request_region(port, 0x10, "jazz16")) {
		snd_printk(KERN_ERR "I/O port region is already in use.\n");
		return -EBUSY;
	}
	/* just to call snd_sbdsp_command/reset/get_byte() */
	chip.port = port;

	err = snd_sbdsp_reset(&chip);
	if (err < 0)
		for (val = 0; val < 4; val++) {
			err = jazz16_configure_ports(port, mpu_port, val);
			if (err < 0)
				break;

			err = snd_sbdsp_reset(&chip);
			if (!err)
				break;
		}
	if (err < 0) {
		err = -ENODEV;
		goto err_unmap;
	}
	if (!snd_sbdsp_command(&chip, SB_DSP_GET_JAZZ_BRD_REV)) {
		err = -EBUSY;
		goto err_unmap;
	}
	val = snd_sbdsp_get_byte(&chip);
	if (val >= 0x30)
		snd_sbdsp_get_byte(&chip);

	if ((val & 0xf0) != 0x10) {
		err = -ENODEV;
		goto err_unmap;
	}
	if (!snd_sbdsp_command(&chip, SB_DSP_GET_JAZZ_MODEL)) {
		err = -EBUSY;
		goto err_unmap;
	}
	snd_sbdsp_get_byte(&chip);
	err = snd_sbdsp_get_byte(&chip);
	snd_printd("Media Vision Jazz16 board detected: rev 0x%x, model 0x%x\n",
		   val, err);

	err = 0;

err_unmap:
	release_region(port, 0x10);
	return err;
}

static int __devinit jazz16_configure_board(struct snd_sb *chip, int mpu_irq)
{
	static unsigned char jazz_irq_bits[] = { 0, 0, 2, 3, 0, 1, 0, 4,
						 0, 2, 5, 0, 0, 0, 0, 6 };
	static unsigned char jazz_dma_bits[] = { 0, 1, 0, 2, 0, 3, 0, 4 };

	if (jazz_dma_bits[chip->dma8] == 0 ||
	    jazz_dma_bits[chip->dma16] == 0 ||
	    jazz_irq_bits[chip->irq] == 0)
		return -EINVAL;

	if (!snd_sbdsp_command(chip, SB_JAZZ16_SET_DMAINTR))
		return -EBUSY;

	if (!snd_sbdsp_command(chip,
			       jazz_dma_bits[chip->dma8] |
			       (jazz_dma_bits[chip->dma16] << 4)))
		return -EBUSY;

	if (!snd_sbdsp_command(chip,
			       jazz_irq_bits[chip->irq] |
			       (jazz_irq_bits[mpu_irq] << 4)))
		return -EBUSY;

	return 0;
}

static int __devinit snd_jazz16_match(struct device *devptr, unsigned int dev)
{
	if (!enable[dev])
		return 0;
	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "please specify port\n");
		return 0;
	} else if (port[dev] == 0x200 || (port[dev] & ~0x270)) {
		snd_printk(KERN_ERR "incorrect port specified\n");
		return 0;
	}
	if (dma8[dev] != SNDRV_AUTO_DMA &&
	    dma8[dev] != 1 && dma8[dev] != 3) {
		snd_printk(KERN_ERR "dma8 must be 1 or 3\n");
		return 0;
	}
	if (dma16[dev] != SNDRV_AUTO_DMA &&
	    dma16[dev] != 5 && dma16[dev] != 7) {
		snd_printk(KERN_ERR "dma16 must be 5 or 7\n");
		return 0;
	}
	if (mpu_port[dev] != SNDRV_AUTO_PORT &&
	    (mpu_port[dev] & ~0x030) != 0x300) {
		snd_printk(KERN_ERR "incorrect mpu_port specified\n");
		return 0;
	}
	if (mpu_irq[dev] != SNDRV_AUTO_DMA &&
	    mpu_irq[dev] != 2 && mpu_irq[dev] != 3 &&
	    mpu_irq[dev] != 5 && mpu_irq[dev] != 7) {
		snd_printk(KERN_ERR "mpu_irq must be 2, 3, 5 or 7\n");
		return 0;
	}
	return 1;
}

static int __devinit snd_jazz16_probe(struct device *devptr, unsigned int dev)
{
	struct snd_card *card;
	struct snd_card_jazz16 *jazz16;
	struct snd_sb *chip;
	struct snd_opl3 *opl3;
	static int possible_irqs[] = {2, 3, 5, 7, 9, 10, 15, -1};
	static int possible_dmas8[] = {1, 3, -1};
	static int possible_dmas16[] = {5, 7, -1};
	int err, xirq, xdma8, xdma16, xmpu_port, xmpu_irq;

	err = snd_card_create(index[dev], id[dev], THIS_MODULE,
			      sizeof(struct snd_card_jazz16), &card);
	if (err < 0)
		return err;

	jazz16 = card->private_data;

	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		xirq = snd_legacy_find_free_irq(possible_irqs);
		if (xirq < 0) {
			snd_printk(KERN_ERR "unable to find a free IRQ\n");
			err = -EBUSY;
			goto err_free;
		}
	}
	xdma8 = dma8[dev];
	if (xdma8 == SNDRV_AUTO_DMA) {
		xdma8 = snd_legacy_find_free_dma(possible_dmas8);
		if (xdma8 < 0) {
			snd_printk(KERN_ERR "unable to find a free DMA8\n");
			err = -EBUSY;
			goto err_free;
		}
	}
	xdma16 = dma16[dev];
	if (xdma16 == SNDRV_AUTO_DMA) {
		xdma16 = snd_legacy_find_free_dma(possible_dmas16);
		if (xdma16 < 0) {
			snd_printk(KERN_ERR "unable to find a free DMA16\n");
			err = -EBUSY;
			goto err_free;
		}
	}

	xmpu_port = mpu_port[dev];
	if (xmpu_port == SNDRV_AUTO_PORT)
		xmpu_port = 0;
	err = jazz16_detect_board(port[dev], xmpu_port);
	if (err < 0) {
		printk(KERN_ERR "Media Vision Jazz16 board not detected\n");
		goto err_free;
	}
	err = snd_sbdsp_create(card, port[dev], irq[dev],
			       jazz16_interrupt,
			       dma8[dev], dma16[dev],
			       SB_HW_JAZZ16,
			       &chip);
	if (err < 0)
		goto err_free;

	xmpu_irq = mpu_irq[dev];
	if (xmpu_irq == SNDRV_AUTO_IRQ || mpu_port[dev] == SNDRV_AUTO_PORT)
		xmpu_irq = 0;
	err = jazz16_configure_board(chip, xmpu_irq);
	if (err < 0) {
		printk(KERN_ERR "Media Vision Jazz16 configuration failed\n");
		goto err_free;
	}

	jazz16->chip = chip;

	strcpy(card->driver, "jazz16");
	strcpy(card->shortname, "Media Vision Jazz16");
	sprintf(card->longname,
		"Media Vision Jazz16 at 0x%lx, irq %d, dma8 %d, dma16 %d",
		port[dev], xirq, xdma8, xdma16);

	err = snd_sb8dsp_pcm(chip, 0, NULL);
	if (err < 0)
		goto err_free;
	err = snd_sbmixer_new(chip);
	if (err < 0)
		goto err_free;

	err = snd_opl3_create(card, chip->port, chip->port + 2,
			      OPL3_HW_AUTO, 1, &opl3);
	if (err < 0)
		snd_printk(KERN_WARNING "no OPL device at 0x%lx-0x%lx\n",
			   chip->port, chip->port + 2);
	else {
		err = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
		if (err < 0)
			goto err_free;
	}
	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (mpu_irq[dev] == SNDRV_AUTO_IRQ)
			mpu_irq[dev] = -1;

		if (snd_mpu401_uart_new(card, 0,
					MPU401_HW_MPU401,
					mpu_port[dev], 0,
					mpu_irq[dev],
					NULL) < 0)
			snd_printk(KERN_ERR "no MPU-401 device at 0x%lx\n",
					mpu_port[dev]);
	}

	snd_card_set_dev(card, devptr);

	err = snd_card_register(card);
	if (err < 0)
		goto err_free;

	dev_set_drvdata(devptr, card);
	return 0;

err_free:
	snd_card_free(card);
	return err;
}

static int __devexit snd_jazz16_remove(struct device *devptr, unsigned int dev)
{
	struct snd_card *card = dev_get_drvdata(devptr);

	dev_set_drvdata(devptr, NULL);
	snd_card_free(card);
	return 0;
}

#ifdef CONFIG_PM
static int snd_jazz16_suspend(struct device *pdev, unsigned int n,
			       pm_message_t state)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct snd_card_jazz16 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_sbmixer_suspend(chip);
	return 0;
}

static int snd_jazz16_resume(struct device *pdev, unsigned int n)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct snd_card_jazz16 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_sbdsp_reset(chip);
	snd_sbmixer_resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct isa_driver snd_jazz16_driver = {
	.match		= snd_jazz16_match,
	.probe		= snd_jazz16_probe,
	.remove		= __devexit_p(snd_jazz16_remove),
#ifdef CONFIG_PM
	.suspend	= snd_jazz16_suspend,
	.resume		= snd_jazz16_resume,
#endif
	.driver		= {
		.name	= "jazz16"
	},
};

static int __init alsa_card_jazz16_init(void)
{
	return isa_register_driver(&snd_jazz16_driver, SNDRV_CARDS);
}

static void __exit alsa_card_jazz16_exit(void)
{
	isa_unregister_driver(&snd_jazz16_driver);
}

module_init(alsa_card_jazz16_init)
module_exit(alsa_card_jazz16_exit)
