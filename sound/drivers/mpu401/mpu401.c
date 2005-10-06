/*
 *  Driver for generic MPU-401 boards (UART mode only)
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Copyright (c) 2004 by Castet Matthieu <castet.matthieu@free.fr>
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
#include <linux/init.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("MPU-401 UART");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -2}; /* exclude the first card */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef CONFIG_PNP
static int pnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* MPU-401 port number */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* MPU-401 IRQ */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for MPU-401 device.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for MPU-401 device.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable MPU-401 device.");
#ifdef CONFIG_PNP
module_param_array(pnp, bool, NULL, 0444);
MODULE_PARM_DESC(pnp, "PnP detection for MPU-401 device.");
#endif
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for MPU-401 device.");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for MPU-401 device.");

static snd_card_t *snd_mpu401_legacy_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;
static int pnp_registered = 0;

static int snd_mpu401_create(int dev, snd_card_t **rcard)
{
	snd_card_t *card;
	int err;

	*rcard = NULL;
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	strcpy(card->driver, "MPU-401 UART");
	strcpy(card->shortname, card->driver);
	sprintf(card->longname, "%s at %#lx, ", card->shortname, port[dev]);
	if (irq[dev] >= 0) {
		sprintf(card->longname + strlen(card->longname), "irq %d", irq[dev]);
	} else {
		strcat(card->longname, "polled");
	}

	if ((err = snd_mpu401_uart_new(card, 0,
				       MPU401_HW_MPU401,
				       port[dev], 0,
				       irq[dev], irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL)) < 0) {
		printk(KERN_ERR "MPU401 not detected at 0x%lx\n", port[dev]);
		goto _err;
	}

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto _err;

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	*rcard = card;
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int __devinit snd_mpu401_probe(int dev)
{
	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "specify port\n");
		return -EINVAL;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk(KERN_ERR "specify or disable IRQ\n");
		return -EINVAL;
	}
	return snd_mpu401_create(dev, &snd_mpu401_legacy_cards[dev]);
}

#ifdef CONFIG_PNP

#define IO_EXTENT 2

static struct pnp_device_id snd_mpu401_pnpids[] = {
	{ .id = "PNPb006" },
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp, snd_mpu401_pnpids);

static int __init snd_mpu401_pnp(int dev, struct pnp_dev *device,
				 const struct pnp_device_id *id)
{
	if (!pnp_port_valid(device, 0) ||
	    pnp_port_flags(device, 0) & IORESOURCE_DISABLED) {
		snd_printk(KERN_ERR "no PnP port\n");
		return -ENODEV;
	}
	if (pnp_port_len(device, 0) < IO_EXTENT) {
		snd_printk(KERN_ERR "PnP port length is %ld, expected %d\n",
			   pnp_port_len(device, 0), IO_EXTENT);
		return -ENODEV;
	}
	port[dev] = pnp_port_start(device, 0);

	if (!pnp_irq_valid(device, 0) ||
	    pnp_irq_flags(device, 0) & IORESOURCE_DISABLED) {
		snd_printk(KERN_WARNING "no PnP irq, using polling\n");
		irq[dev] = -1;
	} else {
		irq[dev] = pnp_irq(device, 0);
	}
	return 0;
}

static int __devinit snd_mpu401_pnp_probe(struct pnp_dev *pnp_dev,
					  const struct pnp_device_id *id)
{
	static int dev;
	snd_card_t *card;
	int err;

	for ( ; dev < SNDRV_CARDS; ++dev) {
		if (!enable[dev] || !pnp[dev])
			continue;
		err = snd_mpu401_pnp(dev, pnp_dev, id);
		if (err < 0)
			return err;
		err = snd_mpu401_create(dev, &card);
		if (err < 0)
			return err;
		snd_card_set_dev(card, &pnp_dev->dev);
		pnp_set_drvdata(pnp_dev, card);
		++dev;
		return 0;
	}
	return -ENODEV;
}

static void __devexit snd_mpu401_pnp_remove(struct pnp_dev *dev)
{
	snd_card_t *card = (snd_card_t *) pnp_get_drvdata(dev);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_driver snd_mpu401_pnp_driver = {
	.name = "mpu401",
	.id_table = snd_mpu401_pnpids,
	.probe = snd_mpu401_pnp_probe,
	.remove = __devexit_p(snd_mpu401_pnp_remove),
};
#else
static struct pnp_driver snd_mpu401_pnp_driver;
#endif

static int __init alsa_card_mpu401_init(void)
{
	int dev, devices = 0;
	int err;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
#ifdef CONFIG_PNP
		if (pnp[dev])
			continue;
#endif
		if (snd_mpu401_probe(dev) >= 0)
			devices++;
	}
	if ((err = pnp_register_driver(&snd_mpu401_pnp_driver)) >= 0) {
		pnp_registered = 1;
		devices += err;
	}

	if (!devices) {
#ifdef MODULE
		printk(KERN_ERR "MPU-401 device not found or device busy\n");
#endif
		if (pnp_registered)
			pnp_unregister_driver(&snd_mpu401_pnp_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_mpu401_exit(void)
{
	int idx;

	if (pnp_registered)
		pnp_unregister_driver(&snd_mpu401_pnp_driver);
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_mpu401_legacy_cards[idx]);
}

module_init(alsa_card_mpu401_init)
module_exit(alsa_card_mpu401_exit)
