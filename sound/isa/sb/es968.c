
/*
    card-es968.c - driver for ESS AudioDrive ES968 based soundcards.
    Copyright (C) 1999 by Massimo Piccioni <dafastidio@libero.it>

    Thanks to Pierfrancesco 'qM2' Passerini.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <linux/init.h>
#include <linux/time.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/sb.h>

#define PFX "es968: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("ESS AudioDrive ES968");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,AudioDrive ES968}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for es968 based soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for es968 based soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable es968 based soundcard.");

struct snd_card_es968 {
	struct pnp_dev *dev;
	struct snd_sb *chip;
};

static struct pnp_card_device_id snd_es968_pnpids[] = {
	{ .id = "ESS0968", .devs = { { "@@@0968" }, } },
	{ .id = "", } /* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_es968_pnpids);

#define	DRIVER_NAME	"snd-card-es968"

static irqreturn_t snd_card_es968_interrupt(int irq, void *dev_id)
{
	struct snd_sb *chip = dev_id;

	if (chip->open & SB_OPEN_PCM) {
		return snd_sb8dsp_interrupt(chip);
	} else {
		return snd_sb8dsp_midi_interrupt(chip);
	}
}

static int __devinit snd_card_es968_pnp(int dev, struct snd_card_es968 *acard,
					struct pnp_card_link *card,
					const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	int err;

	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL)
		return -ENODEV;

	pdev = acard->dev;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "AUDIO pnp configure failure\n");
		return err;
	}
	port[dev] = pnp_port_start(pdev, 0);
	dma8[dev] = pnp_dma(pdev, 0);
	irq[dev] = pnp_irq(pdev, 0);

	return 0;
}

static int __devinit snd_card_es968_probe(int dev,
					struct pnp_card_link *pcard,
					const struct pnp_card_device_id *pid)
{
	int error;
	struct snd_sb *chip;
	struct snd_card *card;
	struct snd_card_es968 *acard;

	error = snd_card_create(index[dev], id[dev], THIS_MODULE,
				sizeof(struct snd_card_es968), &card);
	if (error < 0)
		return error;
	acard = card->private_data;
	if ((error = snd_card_es968_pnp(dev, acard, pcard, pid))) {
		snd_card_free(card);
		return error;
	}
	snd_card_set_dev(card, &pcard->card->dev);

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_card_es968_interrupt,
				      dma8[dev],
				      -1,
				      SB_HW_AUTO, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}
	acard->chip = chip;

	if ((error = snd_sb8dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb8dsp_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	strcpy(card->driver, "ES968");
	strcpy(card->shortname, "ESS ES968");
	sprintf(card->longname, "%s soundcard, %s at 0x%lx, irq %d, dma %d",
		card->shortname, chip->name, chip->port, irq[dev], dma8[dev]);

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	return 0;
}

static unsigned int __devinitdata es968_devices;

static int __devinit snd_es968_pnp_detect(struct pnp_card_link *card,
                                          const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		res = snd_card_es968_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		es968_devices++;
		return 0;
	}
	return -ENODEV;
}

static void __devexit snd_es968_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

#ifdef CONFIG_PM
static int snd_es968_pnp_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_es968 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_sbmixer_suspend(chip);
	return 0;
}

static int snd_es968_pnp_resume(struct pnp_card_link *pcard)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_es968 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_sbdsp_reset(chip);
	snd_sbmixer_resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct pnp_card_driver es968_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= "es968",
	.id_table	= snd_es968_pnpids,
	.probe		= snd_es968_pnp_detect,
	.remove		= __devexit_p(snd_es968_pnp_remove),
#ifdef CONFIG_PM
	.suspend	= snd_es968_pnp_suspend,
	.resume		= snd_es968_pnp_resume,
#endif
};

static int __init alsa_card_es968_init(void)
{
	int err = pnp_register_card_driver(&es968_pnpc_driver);
	if (err)
		return err;

	if (!es968_devices) {
		pnp_unregister_card_driver(&es968_pnpc_driver);
#ifdef MODULE
		snd_printk(KERN_ERR "no ES968 based soundcards found\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_es968_exit(void)
{
	pnp_unregister_card_driver(&es968_pnpc_driver);
}

module_init(alsa_card_es968_init)
module_exit(alsa_card_es968_exit)
