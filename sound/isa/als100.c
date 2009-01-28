
/*
    card-als100.c - driver for Avance Logic ALS100 based soundcards.
    Copyright (C) 1999-2000 by Massimo Piccioni <dafastidio@libero.it>

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
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>

#define PFX "als100: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Avance Logic ALS1X0");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Avance Logic,ALS100 - PRO16PNP},"
	        "{Avance Logic,ALS110},"
	        "{Avance Logic,ALS120},"
	        "{Avance Logic,ALS200},"
	        "{3D Melody,MF1000},"
	        "{Digimate,3D Sound},"
	        "{Avance Logic,ALS120},"
	        "{RTL,RTL3000}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */
static int dma16[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for als100 based soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for als100 based soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable als100 based soundcard.");

struct snd_card_als100 {
	int dev_no;
	struct pnp_dev *dev;
	struct pnp_dev *devmpu;
	struct pnp_dev *devopl;
	struct snd_sb *chip;
};

static struct pnp_card_device_id snd_als100_pnpids[] = {
	/* ALS100 - PRO16PNP */
	{ .id = "ALS0001", .devs = { { "@@@0001" }, { "@X@0001" }, { "@H@0001" } } },
	/* ALS110 - MF1000 - Digimate 3D Sound */
	{ .id = "ALS0110", .devs = { { "@@@1001" }, { "@X@1001" }, { "@H@1001" } } },
	/* ALS120 */
	{ .id = "ALS0120", .devs = { { "@@@2001" }, { "@X@2001" }, { "@H@2001" } } },
	/* ALS200 */
	{ .id = "ALS0200", .devs = { { "@@@0020" }, { "@X@0020" }, { "@H@0001" } } },
	/* ALS200 OEM */
	{ .id = "ALS0200", .devs = { { "@@@0020" }, { "@X@0020" }, { "@H@0020" } } },
	/* RTL3000 */
	{ .id = "RTL3000", .devs = { { "@@@2001" }, { "@X@2001" }, { "@H@2001" } } },
	{ .id = "", } /* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_als100_pnpids);

#define DRIVER_NAME	"snd-card-als100"

static int __devinit snd_card_als100_pnp(int dev, struct snd_card_als100 *acard,
					 struct pnp_card_link *card,
					 const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	int err;

	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL)
		return -ENODEV;

	acard->devmpu = pnp_request_card_device(card, id->devs[1].id, acard->dev);
	acard->devopl = pnp_request_card_device(card, id->devs[2].id, acard->dev);

	pdev = acard->dev;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "AUDIO pnp configure failure\n");
		return err;
	}
	port[dev] = pnp_port_start(pdev, 0);
	dma8[dev] = pnp_dma(pdev, 1);
	dma16[dev] = pnp_dma(pdev, 0);
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
	     		snd_printk(KERN_ERR PFX "MPU401 pnp configure failure, skipping\n");
	     	}
	     	acard->devmpu = NULL;
	     	mpu_port[dev] = -1;
	}

	pdev = acard->devopl;
	if (pdev != NULL) {
		err = pnp_activate_dev(pdev);
		if (err < 0)
			goto __fm_error;
		fm_port[dev] = pnp_port_start(pdev, 0);
	} else {
	      __fm_error:
	     	if (pdev) {
		     	pnp_release_card_device(pdev);
	     		snd_printk(KERN_ERR PFX "OPL3 pnp configure failure, skipping\n");
	     	}
	     	acard->devopl = NULL;
	     	fm_port[dev] = -1;
	}

	return 0;
}

static int __devinit snd_card_als100_probe(int dev,
					struct pnp_card_link *pcard,
					const struct pnp_card_device_id *pid)
{
	int error;
	struct snd_sb *chip;
	struct snd_card *card;
	struct snd_card_als100 *acard;
	struct snd_opl3 *opl3;

	error = snd_card_create(index[dev], id[dev], THIS_MODULE,
				sizeof(struct snd_card_als100), &card);
	if (error < 0)
		return error;
	acard = card->private_data;

	if ((error = snd_card_als100_pnp(dev, acard, pcard, pid))) {
		snd_card_free(card);
		return error;
	}
	snd_card_set_dev(card, &pcard->card->dev);

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_sb16dsp_interrupt,
				      dma8[dev],
				      dma16[dev],
				      SB_HW_ALS100, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}
	acard->chip = chip;

	strcpy(card->driver, "ALS100");
	strcpy(card->shortname, "Avance Logic ALS100");
	sprintf(card->longname, "%s, %s at 0x%lx, irq %d, dma %d&%d",
		card->shortname, chip->name, chip->port,
		irq[dev], dma8[dev], dma16[dev]);

	if ((error = snd_sb16dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_ALS100,
					mpu_port[dev], 0, 
					mpu_irq[dev], IRQF_DISABLED,
					NULL) < 0)
			snd_printk(KERN_ERR PFX "no MPU-401 device at 0x%lx\n", mpu_port[dev]);
	}

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			snd_printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx\n",
				   fm_port[dev], fm_port[dev] + 2);
		} else {
			if ((error = snd_opl3_timer_new(opl3, 0, 1)) < 0) {
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	return 0;
}

static unsigned int __devinitdata als100_devices;

static int __devinit snd_als100_pnp_detect(struct pnp_card_link *card,
					   const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		res = snd_card_als100_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		als100_devices++;
		return 0;
	}
	return -ENODEV;
}

static void __devexit snd_als100_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

#ifdef CONFIG_PM
static int snd_als100_pnp_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_als100 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_sbmixer_suspend(chip);
	return 0;
}

static int snd_als100_pnp_resume(struct pnp_card_link *pcard)
{
	struct snd_card *card = pnp_get_card_drvdata(pcard);
	struct snd_card_als100 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_sbdsp_reset(chip);
	snd_sbmixer_resume(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct pnp_card_driver als100_pnpc_driver = {
	.flags          = PNP_DRIVER_RES_DISABLE,
        .name           = "als100",
        .id_table       = snd_als100_pnpids,
        .probe          = snd_als100_pnp_detect,
        .remove         = __devexit_p(snd_als100_pnp_remove),
#ifdef CONFIG_PM
	.suspend	= snd_als100_pnp_suspend,
	.resume		= snd_als100_pnp_resume,
#endif
};

static int __init alsa_card_als100_init(void)
{
	int err;

	err = pnp_register_card_driver(&als100_pnpc_driver);
	if (err)
		return err;

	if (!als100_devices) {
		pnp_unregister_card_driver(&als100_pnpc_driver);
#ifdef MODULE
		snd_printk(KERN_ERR "no ALS100 based soundcards found\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_als100_exit(void)
{
	pnp_unregister_card_driver(&als100_pnpc_driver);
}

module_init(alsa_card_als100_init)
module_exit(alsa_card_als100_exit)
