// SPDX-License-Identifier: GPL-2.0-or-later
/*
    card-azt2320.c - driver for Aztech Systems AZT2320 based soundcards.
    Copyright (C) 1999-2000 by Massimo Piccioni <dafastidio@libero.it>

*/

/*
    This driver should provide support for most Aztech AZT2320 based cards.
    Several AZT2316 chips are also supported/tested, but autoprobe doesn't
    work: all module option have to be set.

    No docs available for us at Aztech headquarters !!!   Unbelievable ...
    No other help obtained.

    Thanks to Rainer Wiesner <rainer.wiesner@01019freenet.de> for the WSS
    activation method (full-duplex audio!).
*/

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/pnp.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/wss.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Aztech Systems AZT2320");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long wss_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for azt2320 based soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for azt2320 based soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable azt2320 based soundcard.");

struct snd_card_azt2320 {
	int dev_no;
	struct pnp_dev *dev;
	struct pnp_dev *devmpu;
	struct snd_wss *chip;
};

static const struct pnp_card_device_id snd_azt2320_pnpids[] = {
	/* PRO16V */
	{ .id = "AZT1008", .devs = { { "AZT1008" }, { "AZT2001" }, } },
	/* Aztech Sound Galaxy 16 */
	{ .id = "AZT2320", .devs = { { "AZT0001" }, { "AZT0002" }, } },
	/* Packard Bell Sound III 336 AM/SP */
	{ .id = "AZT3000", .devs = { { "AZT1003" }, { "AZT2001" }, } },
	/* AT3300 */
	{ .id = "AZT3002", .devs = { { "AZT1004" }, { "AZT2001" }, } },
	/* --- */
	{ .id = "AZT3005", .devs = { { "AZT1003" }, { "AZT2001" }, } },
	/* --- */
	{ .id = "AZT3011", .devs = { { "AZT1003" }, { "AZT2001" }, } },
	{ .id = "" }	/* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_azt2320_pnpids);

#define	DRIVER_NAME	"snd-card-azt2320"

static int snd_card_azt2320_pnp(int dev, struct snd_card_azt2320 *acard,
				struct pnp_card_link *card,
				const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	int err;

	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL)
		return -ENODEV;

	acard->devmpu = pnp_request_card_device(card, id->devs[1].id, NULL);

	pdev = acard->dev;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "AUDIO pnp configure failure\n");
		return err;
	}
	port[dev] = pnp_port_start(pdev, 0);
	fm_port[dev] = pnp_port_start(pdev, 1);
	wss_port[dev] = pnp_port_start(pdev, 2);
	dma1[dev] = pnp_dma(pdev, 0);
	dma2[dev] = pnp_dma(pdev, 1);
	irq[dev] = pnp_irq(pdev, 0);

	pdev = acard->devmpu;
	if (pdev != NULL) {
		err = pnp_activate_dev(pdev);
		if (err < 0)
			goto __mpu_error;
		mpu_port[dev] = pnp_port_start(pdev, 0);
		mpu_irq[dev] = pnp_irq(pdev, 0);
	} else {
	     __mpu_error:
	     	if (pdev) {
		     	pnp_release_card_device(pdev);
			dev_err(&pdev->dev, "MPU401 pnp configure failure, skipping\n");
	     	}
	     	acard->devmpu = NULL;
	     	mpu_port[dev] = -1;
	}

	return 0;
}

/* same of snd_sbdsp_command by Jaroslav Kysela */
static int snd_card_azt2320_command(unsigned long port, unsigned char val)
{
	int i;
	unsigned long limit;

	limit = jiffies + HZ / 10;
	for (i = 50000; i && time_after(limit, jiffies); i--)
		if (!(inb(port + 0x0c) & 0x80)) {
			outb(val, port + 0x0c);
			return 0;
		}
	return -EBUSY;
}

static int snd_card_azt2320_enable_wss(unsigned long port)
{
	int error;

	error = snd_card_azt2320_command(port, 0x09);
	if (error)
		return error;
	error = snd_card_azt2320_command(port, 0x00);
	if (error)
		return error;

	mdelay(5);
	return 0;
}

static int snd_card_azt2320_probe(int dev,
				  struct pnp_card_link *pcard,
				  const struct pnp_card_device_id *pid)
{
	int error;
	struct snd_card *card;
	struct snd_card_azt2320 *acard;
	struct snd_wss *chip;
	struct snd_opl3 *opl3;

	error = snd_devm_card_new(&pcard->card->dev,
				  index[dev], id[dev], THIS_MODULE,
				  sizeof(struct snd_card_azt2320), &card);
	if (error < 0)
		return error;
	acard = card->private_data;

	error = snd_card_azt2320_pnp(dev, acard, pcard, pid);
	if (error)
		return error;

	error = snd_card_azt2320_enable_wss(port[dev]);
	if (error)
		return error;

	error = snd_wss_create(card, wss_port[dev], -1,
			       irq[dev],
			       dma1[dev], dma2[dev],
			       WSS_HW_DETECT, 0, &chip);
	if (error < 0)
		return error;

	strcpy(card->driver, "AZT2320");
	strcpy(card->shortname, "Aztech AZT2320");
	sprintf(card->longname, "%s, WSS at 0x%lx, irq %i, dma %i&%i",
		card->shortname, chip->port, irq[dev], dma1[dev], dma2[dev]);

	error = snd_wss_pcm(chip, 0);
	if (error < 0)
		return error;
	error = snd_wss_mixer(chip);
	if (error < 0)
		return error;
	error = snd_wss_timer(chip, 0);
	if (error < 0)
		return error;

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_AZT2320,
				mpu_port[dev], 0,
				mpu_irq[dev], NULL) < 0)
			dev_err(card->dev, "no MPU-401 device at 0x%lx\n", mpu_port[dev]);
	}

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			dev_err(card->dev, "no OPL device at 0x%lx-0x%lx\n",
				fm_port[dev], fm_port[dev] + 2);
		} else {
			error = snd_opl3_timer_new(opl3, 1, 2);
			if (error < 0)
				return error;
			error = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
			if (error < 0)
				return error;
		}
	}

	error = snd_card_register(card);
	if (error < 0)
		return error;
	pnp_set_card_drvdata(pcard, card);
	return 0;
}

static unsigned int azt2320_devices;

static int snd_azt2320_pnp_detect(struct pnp_card_link *card,
				  const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		res = snd_card_azt2320_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		azt2320_devices++;
		return 0;
	}
        return -ENODEV;
}

#ifdef CONFIG_PM
static int snd_azt2320_pnp_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_azt2320 *acard = card->private_data;
	struct snd_wss *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	chip->suspend(chip);
	return 0;
}

static int snd_azt2320_pnp_resume(struct pnp_card_link *pcard)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_azt2320 *acard = card->private_data;
	struct snd_wss *chip = acard->chip;

	chip->resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct pnp_card_driver azt2320_pnpc_driver = {
	.flags          = PNP_DRIVER_RES_DISABLE,
	.name           = "azt2320",
	.id_table       = snd_azt2320_pnpids,
	.probe          = snd_azt2320_pnp_detect,
#ifdef CONFIG_PM
	.suspend	= snd_azt2320_pnp_suspend,
	.resume		= snd_azt2320_pnp_resume,
#endif
};

static int __init alsa_card_azt2320_init(void)
{
	int err;

	err = pnp_register_card_driver(&azt2320_pnpc_driver);
	if (err)
		return err;

	if (!azt2320_devices) {
		pnp_unregister_card_driver(&azt2320_pnpc_driver);
#ifdef MODULE
		pr_err("no AZT2320 based soundcards found\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_azt2320_exit(void)
{
	pnp_unregister_card_driver(&azt2320_pnpc_driver);
}

module_init(alsa_card_azt2320_init)
module_exit(alsa_card_azt2320_exit)
