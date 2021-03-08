// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for generic ESS AudioDrive ESx688 soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/isapnp.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/es1688.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

#define CRD_NAME "Generic ESS ES1688/ES688 AudioDrive"
#define DEV_NAME "es1688"

MODULE_DESCRIPTION(CRD_NAME);
MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,ES688 PnP AudioDrive,pnp:ESS0100},"
	        "{ESS,ES1688 PnP AudioDrive,pnp:ESS0102},"
	        "{ESS,ES688 AudioDrive,pnp:ESS6881},"
	        "{ESS,ES1688 AudioDrive,pnp:ESS1681}}");

MODULE_ALIAS("snd_es968");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
#ifdef CONFIG_PNP
static bool isapnp[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;
#endif
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* Usually 0x388 */
static long mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CRD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CRD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "PnP detection for specified soundcard.");
#endif
MODULE_PARM_DESC(enable, "Enable " CRD_NAME " soundcard.");
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " CRD_NAME " driver.");
module_param_hw_array(mpu_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for " CRD_NAME " driver.");
module_param_hw_array(irq, int, irq, NULL, 0444);
module_param_hw_array(fm_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for ES1688 driver.");
MODULE_PARM_DESC(irq, "IRQ # for " CRD_NAME " driver.");
module_param_hw_array(mpu_irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for " CRD_NAME " driver.");
module_param_hw_array(dma8, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for " CRD_NAME " driver.");

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

static int snd_es1688_match(struct device *dev, unsigned int n)
{
	return enable[n] && !is_isapnp_selected(n);
}

static int snd_es1688_legacy_create(struct snd_card *card,
				    struct device *dev, unsigned int n)
{
	struct snd_es1688 *chip = card->private_data;
	static const long possible_ports[] = {0x220, 0x240, 0x260};
	static const int possible_irqs[] = {5, 9, 10, 7, -1};
	static const int possible_dmas[] = {1, 3, 0, -1};

	int i, error;

	if (irq[n] == SNDRV_AUTO_IRQ) {
		irq[n] = snd_legacy_find_free_irq(possible_irqs);
		if (irq[n] < 0) {
			dev_err(dev, "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (dma8[n] == SNDRV_AUTO_DMA) {
		dma8[n] = snd_legacy_find_free_dma(possible_dmas);
		if (dma8[n] < 0) {
			dev_err(dev, "unable to find a free DMA\n");
			return -EBUSY;
		}
	}

	if (port[n] != SNDRV_AUTO_PORT)
		return snd_es1688_create(card, chip, port[n], mpu_port[n],
				irq[n], mpu_irq[n], dma8[n], ES1688_HW_AUTO);

	i = 0;
	do {
		port[n] = possible_ports[i];
		error = snd_es1688_create(card, chip, port[n], mpu_port[n],
				irq[n], mpu_irq[n], dma8[n], ES1688_HW_AUTO);
	} while (error < 0 && ++i < ARRAY_SIZE(possible_ports));

	return error;
}

static int snd_es1688_probe(struct snd_card *card, unsigned int n)
{
	struct snd_es1688 *chip = card->private_data;
	struct snd_opl3 *opl3;
	int error;

	error = snd_es1688_pcm(card, chip, 0);
	if (error < 0)
		return error;

	error = snd_es1688_mixer(card, chip);
	if (error < 0)
		return error;

	strscpy(card->driver, "ES1688", sizeof(card->driver));
	strscpy(card->shortname, chip->pcm->name, sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		"%s at 0x%lx, irq %i, dma %i", chip->pcm->name, chip->port,
		 chip->irq, chip->dma8);

	if (fm_port[n] == SNDRV_AUTO_PORT)
		fm_port[n] = port[n];	/* share the same port */

	if (fm_port[n] > 0) {
		if (snd_opl3_create(card, fm_port[n], fm_port[n] + 2,
				OPL3_HW_OPL3, 0, &opl3) < 0)
			dev_warn(card->dev,
				 "opl3 not detected at 0x%lx\n", fm_port[n]);
		else {
			error =	snd_opl3_hwdep_new(opl3, 0, 1, NULL);
			if (error < 0)
				return error;
		}
	}

	if (mpu_irq[n] >= 0 && mpu_irq[n] != SNDRV_AUTO_IRQ &&
			chip->mpu_port > 0) {
		error = snd_mpu401_uart_new(card, 0, MPU401_HW_ES1688,
				chip->mpu_port, 0,
				mpu_irq[n], NULL);
		if (error < 0)
			return error;
	}

	return snd_card_register(card);
}

static int snd_es1688_isa_probe(struct device *dev, unsigned int n)
{
	struct snd_card *card;
	int error;

	error = snd_card_new(dev, index[n], id[n], THIS_MODULE,
			     sizeof(struct snd_es1688), &card);
	if (error < 0)
		return error;

	error = snd_es1688_legacy_create(card, dev, n);
	if (error < 0)
		goto out;

	error = snd_es1688_probe(card, n);
	if (error < 0)
		goto out;

	dev_set_drvdata(dev, card);

	return 0;
out:
	snd_card_free(card);
	return error;
}

static void snd_es1688_isa_remove(struct device *dev, unsigned int n)
{
	snd_card_free(dev_get_drvdata(dev));
}

static struct isa_driver snd_es1688_driver = {
	.match		= snd_es1688_match,
	.probe		= snd_es1688_isa_probe,
	.remove		= snd_es1688_isa_remove,
#if 0	/* FIXME */
	.suspend	= snd_es1688_suspend,
	.resume		= snd_es1688_resume,
#endif
	.driver		= {
		.name	= DEV_NAME
	}
};

static int snd_es968_pnp_is_probed;

#ifdef CONFIG_PNP
static int snd_card_es968_pnp(struct snd_card *card, unsigned int n,
			      struct pnp_card_link *pcard,
			      const struct pnp_card_device_id *pid)
{
	struct snd_es1688 *chip = card->private_data;
	struct pnp_dev *pdev;
	int error;

	pdev = pnp_request_card_device(pcard, pid->devs[0].id, NULL);
	if (pdev == NULL)
		return -ENODEV;

	error = pnp_activate_dev(pdev);
	if (error < 0) {
		snd_printk(KERN_ERR "ES968 pnp configure failure\n");
		return error;
	}
	port[n] = pnp_port_start(pdev, 0);
	dma8[n] = pnp_dma(pdev, 0);
	irq[n] = pnp_irq(pdev, 0);

	return snd_es1688_create(card, chip, port[n], mpu_port[n], irq[n],
				 mpu_irq[n], dma8[n], ES1688_HW_AUTO);
}

static int snd_es968_pnp_detect(struct pnp_card_link *pcard,
				const struct pnp_card_device_id *pid)
{
	struct snd_card *card;
	static unsigned int dev;
	int error;

	if (snd_es968_pnp_is_probed)
		return -EBUSY;
	for ( ; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev == SNDRV_CARDS)
		return -ENODEV;

	error = snd_card_new(&pcard->card->dev,
			     index[dev], id[dev], THIS_MODULE,
			     sizeof(struct snd_es1688), &card);
	if (error < 0)
		return error;

	error = snd_card_es968_pnp(card, dev, pcard, pid);
	if (error < 0) {
		snd_card_free(card);
		return error;
	}
	error = snd_es1688_probe(card, dev);
	if (error < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	snd_es968_pnp_is_probed = 1;
	return 0;
}

static void snd_es968_pnp_remove(struct pnp_card_link *pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
	snd_es968_pnp_is_probed = 0;
}

#ifdef CONFIG_PM
static int snd_es968_pnp_suspend(struct pnp_card_link *pcard,
				 pm_message_t state)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	return 0;
}

static int snd_es968_pnp_resume(struct pnp_card_link *pcard)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_es1688 *chip = card->private_data;

	snd_es1688_reset(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static const struct pnp_card_device_id snd_es968_pnpids[] = {
	{ .id = "ESS0968", .devs = { { "@@@0968" }, } },
	{ .id = "ESS0968", .devs = { { "ESS0968" }, } },
	{ .id = "", } /* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_es968_pnpids);

static struct pnp_card_driver es968_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= DEV_NAME " PnP",
	.id_table	= snd_es968_pnpids,
	.probe		= snd_es968_pnp_detect,
	.remove		= snd_es968_pnp_remove,
#ifdef CONFIG_PM
	.suspend	= snd_es968_pnp_suspend,
	.resume		= snd_es968_pnp_resume,
#endif
};
#endif

static int __init alsa_card_es1688_init(void)
{
#ifdef CONFIG_PNP
	pnp_register_card_driver(&es968_pnpc_driver);
	if (snd_es968_pnp_is_probed)
		return 0;
	pnp_unregister_card_driver(&es968_pnpc_driver);
#endif
	return isa_register_driver(&snd_es1688_driver, SNDRV_CARDS);
}

static void __exit alsa_card_es1688_exit(void)
{
	if (!snd_es968_pnp_is_probed) {
		isa_unregister_driver(&snd_es1688_driver);
		return;
	}
#ifdef CONFIG_PNP
	pnp_unregister_card_driver(&es968_pnpc_driver);
#endif
}

module_init(alsa_card_es1688_init);
module_exit(alsa_card_es1688_exit);
