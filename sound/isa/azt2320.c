/*
    card-azt2320.c - driver for Aztech Systems AZT2320 based soundcards.
    Copyright (C) 1999-2000 by Massimo Piccioni <dafastidio@libero.it>

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

/*
    This driver should provide support for most Aztech AZT2320 based cards.
    Several AZT2316 chips are also supported/tested, but autoprobe doesn't
    work: all module option have to be set.

    No docs available for us at Aztech headquarters !!!   Unbelievable ...
    No other help obtained.

    Thanks to Rainer Wiesner <rainer.wiesner@01019freenet.de> for the WSS
    activation method (full-duplex audio!).
*/

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>

#define PFX "azt2320: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Aztech Systems AZT2320");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Aztech Systems,PRO16V},"
		"{Aztech Systems,AZT2320},"
		"{Aztech Systems,AZT3300},"
		"{Aztech Systems,AZT2320},"
		"{Aztech Systems,AZT3000}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
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
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for azt2320 driver.");
module_param_array(wss_port, long, NULL, 0444);
MODULE_PARM_DESC(wss_port, "WSS Port # for azt2320 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for azt2320 driver.");
module_param_array(fm_port, long, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for azt2320 driver.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for azt2320 driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for azt2320 driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "1st DMA # for azt2320 driver.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "2nd DMA # for azt2320 driver.");

struct snd_card_azt2320 {
	int dev_no;
	struct pnp_dev *dev;
	struct pnp_dev *devmpu;
};

static struct pnp_card_device_id snd_azt2320_pnpids[] = {
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

static int __devinit snd_card_azt2320_pnp(int dev, struct snd_card_azt2320 *acard,
					  struct pnp_card_link *card,
					  const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table * cfg = kmalloc(sizeof(struct pnp_resource_table), GFP_KERNEL);
	int err;

	if (!cfg)
		return -ENOMEM;

	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL) {
		kfree(cfg);
		return -ENODEV;
	}

	acard->devmpu = pnp_request_card_device(card, id->devs[1].id, NULL);

	pdev = acard->dev;
	pnp_init_resource_table(cfg);

	/* override resources */
	if (port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port[dev], 16);
	if (fm_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[1], fm_port[dev], 4);
	if (wss_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[2], wss_port[dev], 4);
	if (dma1[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma1[dev], 1);
	if (dma2[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[1], dma2[dev], 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);
	if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
		snd_printk(KERN_ERR PFX "AUDIO the requested resources are invalid, using auto config\n");

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "AUDIO pnp configure failure\n");
		kfree(cfg);
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
		pnp_init_resource_table(cfg);
		if (mpu_port[dev] != SNDRV_AUTO_PORT)
			pnp_resource_change(&cfg->port_resource[0], mpu_port[dev], 2);
		if (mpu_irq[dev] != SNDRV_AUTO_IRQ)
			pnp_resource_change(&cfg->irq_resource[0], mpu_irq[dev], 1);
		if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
			snd_printk(KERN_ERR PFX "MPU401 the requested resources are invalid, using auto config\n");
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

	kfree (cfg);
	return 0;
}

/* same of snd_sbdsp_command by Jaroslav Kysela */
static int __devinit snd_card_azt2320_command(unsigned long port, unsigned char val)
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

static int __devinit snd_card_azt2320_enable_wss(unsigned long port)
{
	int error;

	if ((error = snd_card_azt2320_command(port, 0x09)))
		return error;
	if ((error = snd_card_azt2320_command(port, 0x00)))
		return error;

	mdelay(5);
	return 0;
}

static int __devinit snd_card_azt2320_probe(int dev,
					    struct pnp_card_link *pcard,
					    const struct pnp_card_device_id *pid)
{
	int error;
	snd_card_t *card;
	struct snd_card_azt2320 *acard;
	cs4231_t *chip;
	opl3_t *opl3;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_azt2320))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_azt2320 *)card->private_data;

	if ((error = snd_card_azt2320_pnp(dev, acard, pcard, pid))) {
		snd_card_free(card);
		return error;
	}
	snd_card_set_dev(card, &pcard->card->dev);

	if ((error = snd_card_azt2320_enable_wss(port[dev]))) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_cs4231_create(card, wss_port[dev], -1,
				       irq[dev],
				       dma1[dev],
				       dma2[dev],
				       CS4231_HW_DETECT, 0, &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	strcpy(card->driver, "AZT2320");
	strcpy(card->shortname, "Aztech AZT2320");
	sprintf(card->longname, "%s, WSS at 0x%lx, irq %i, dma %i&%i",
		card->shortname, chip->port, irq[dev], dma1[dev], dma2[dev]);

	if ((error = snd_cs4231_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_mixer(chip)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_AZT2320,
				mpu_port[dev], 0,
				mpu_irq[dev], SA_INTERRUPT,
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
			if ((error = snd_opl3_timer_new(opl3, 1, 2)) < 0) {
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

static int __devinit snd_azt2320_pnp_detect(struct pnp_card_link *card,
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
		return 0;
	}
        return -ENODEV;
}

static void __devexit snd_azt2320_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_card_driver azt2320_pnpc_driver = {
	.flags          = PNP_DRIVER_RES_DISABLE,
	.name           = "azt2320",
	.id_table       = snd_azt2320_pnpids,
	.probe          = snd_azt2320_pnp_detect,
	.remove         = __devexit_p(snd_azt2320_pnp_remove),
};

static int __init alsa_card_azt2320_init(void)
{
	int cards = 0;

	cards += pnp_register_card_driver(&azt2320_pnpc_driver);
#ifdef MODULE
	if (!cards) {
		pnp_unregister_card_driver(&azt2320_pnpc_driver);
		snd_printk(KERN_ERR "no AZT2320 based soundcards found\n");
	}
#endif
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_azt2320_exit(void)
{
	pnp_unregister_card_driver(&azt2320_pnpc_driver);
}

module_init(alsa_card_azt2320_init)
module_exit(alsa_card_azt2320_exit)
