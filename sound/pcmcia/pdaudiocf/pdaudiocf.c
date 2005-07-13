/*
 * Driver for Sound Core PDAudioCF soundcard
 *
 * Copyright (c) 2003 by Jaroslav Kysela <perex@suse.cz>
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
 */

#include <sound/driver.h>
#include <sound/core.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/cisreg.h>
#include "pdaudiocf.h"
#include <sound/initval.h>
#include <linux/init.h>

/*
 */

#define CARD_NAME	"PDAudio-CF"

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Sound Core " CARD_NAME);
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Sound Core," CARD_NAME "}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");

/*
 */

static dev_info_t dev_info = "snd-pdaudiocf";
static snd_card_t *card_list[SNDRV_CARDS];
static dev_link_t *dev_list;

/*
 * prototypes
 */
static void pdacf_config(dev_link_t *link);
static int pdacf_event(event_t event, int priority, event_callback_args_t *args);
static void snd_pdacf_detach(dev_link_t *link);

static void pdacf_release(dev_link_t *link)
{
	if (link->state & DEV_CONFIG) {
		/* release cs resources */
		pcmcia_release_configuration(link->handle);
		pcmcia_release_io(link->handle, &link->io);
		pcmcia_release_irq(link->handle, &link->irq);
		link->state &= ~DEV_CONFIG;
	}
}

/*
 * destructor
 */
static int snd_pdacf_free(pdacf_t *pdacf)
{
	dev_link_t *link = &pdacf->link;

	pdacf_release(link);

	/* Break the link with Card Services */
	if (link->handle)
		pcmcia_deregister_client(link->handle);

	card_list[pdacf->index] = NULL;
	pdacf->card = NULL;

	kfree(pdacf);
	return 0;
}

static int snd_pdacf_dev_free(snd_device_t *device)
{
	pdacf_t *chip = device->device_data;
	return snd_pdacf_free(chip);
}

/*
 * snd_pdacf_attach - attach callback for cs
 */
static dev_link_t *snd_pdacf_attach(void)
{
	client_reg_t client_reg;	/* Register with cardmgr */
	dev_link_t *link;		/* Info for cardmgr */
	int i, ret;
	pdacf_t *pdacf;
	snd_card_t *card;
	static snd_device_ops_t ops = {
		.dev_free =	snd_pdacf_dev_free,
	};

	snd_printdd(KERN_DEBUG "pdacf_attach called\n");
	/* find an empty slot from the card list */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (! card_list[i])
			break;
	}
	if (i >= SNDRV_CARDS) {
		snd_printk(KERN_ERR "pdacf: too many cards found\n");
		return NULL;
	}
	if (! enable[i])
		return NULL; /* disabled explicitly */

	/* ok, create a card instance */
	card = snd_card_new(index[i], id[i], THIS_MODULE, 0);
	if (card == NULL) {
		snd_printk(KERN_ERR "pdacf: cannot create a card instance\n");
		return NULL;
	}

	pdacf = snd_pdacf_create(card);
	if (! pdacf)
		return NULL;

	if (snd_device_new(card, SNDRV_DEV_LOWLEVEL, pdacf, &ops) < 0) {
		kfree(pdacf);
		snd_card_free(card);
		return NULL;
	}

	pdacf->index = i;
	card_list[i] = card;

	link = &pdacf->link;
	link->priv = pdacf;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	link->io.NumPorts1 = 16;

	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT | IRQ_FORCED_PULSE;
	// link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED;

	link->irq.IRQInfo1 = 0 /* | IRQ_LEVEL_ID */;
	link->irq.Handler = pdacf_interrupt;
	link->irq.Instance = pdacf;
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->conf.ConfigIndex = 1;
	link->conf.Present = PRESENT_OPTION;

	/* Chain drivers */
	link->next = dev_list;
	dev_list = link;

	/* Register with Card Services */
	client_reg.dev_info = &dev_info;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;

	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		snd_pdacf_detach(link);
		return NULL;
	}

	return link;
}


/**
 * snd_pdacf_assign_resources - initialize the hardware and card instance.
 * @port: i/o port for the card
 * @irq: irq number for the card
 *
 * this function assigns the specified port and irq, boot the card,
 * create pcm and control instances, and initialize the rest hardware.
 *
 * returns 0 if successful, or a negative error code.
 */
static int snd_pdacf_assign_resources(pdacf_t *pdacf, int port, int irq)
{
	int err;
	snd_card_t *card = pdacf->card;

	snd_printdd(KERN_DEBUG "pdacf assign resources: port = 0x%x, irq = %d\n", port, irq);
	pdacf->port = port;
	pdacf->irq = irq;
	pdacf->chip_status |= PDAUDIOCF_STAT_IS_CONFIGURED;

	err = snd_pdacf_ak4117_create(pdacf);
	if (err < 0)
		return err;	

	strcpy(card->driver, "PDAudio-CF");
	sprintf(card->shortname, "Core Sound %s", card->driver);
	sprintf(card->longname, "%s at 0x%x, irq %i",
		card->shortname, port, irq);

	err = snd_pdacf_pcm_new(pdacf);
	if (err < 0)
		return err;

	snd_card_set_pm_callback(card, snd_pdacf_suspend, snd_pdacf_resume, pdacf);

	if ((err = snd_card_register(card)) < 0)
		return err;

	return 0;
}


/*
 * snd_pdacf_detach - detach callback for cs
 */
static void snd_pdacf_detach(dev_link_t *link)
{
	pdacf_t *chip = link->priv;

	snd_printdd(KERN_DEBUG "pdacf_detach called\n");
	/* Remove the interface data from the linked list */
	{
		dev_link_t **linkp;
		/* Locate device structure */
		for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
			if (*linkp == link)
				break;
		if (*linkp)
			*linkp = link->next;
	}
	if (chip->chip_status & PDAUDIOCF_STAT_IS_CONFIGURED)
		snd_pdacf_powerdown(chip);
	chip->chip_status |= PDAUDIOCF_STAT_IS_STALE; /* to be sure */
	snd_card_disconnect(chip->card);
	snd_card_free_in_thread(chip->card);
}

/*
 * configuration callback
 */

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void pdacf_config(dev_link_t *link)
{
	client_handle_t handle = link->handle;
	pdacf_t *pdacf = link->priv;
	tuple_t tuple;
	cisparse_t *parse = NULL;
	config_info_t conf;
	u_short buf[32];
	int last_fn, last_ret;

	snd_printdd(KERN_DEBUG "pdacf_config called\n");
	parse = kmalloc(sizeof(*parse), GFP_KERNEL);
	if (! parse) {
		snd_printk(KERN_ERR "pdacf_config: cannot allocate\n");
		return;
	}
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple.Attributes = 0;
	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	tuple.DesiredTuple = CISTPL_CONFIG;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, parse));
	link->conf.ConfigBase = parse->config.base;
	link->conf.ConfigIndex = 0x5;
	kfree(parse);

	CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &conf));
	link->conf.Vcc = conf.Vcc;

	/* Configure card */
	link->state |= DEV_CONFIG;

	CS_CHECK(RequestIO, pcmcia_request_io(handle, &link->io));
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link->handle, &link->conf));

	if (snd_pdacf_assign_resources(pdacf, link->io.BasePort1, link->irq.AssignedIRQ) < 0)
		goto failed;

	link->dev = &pdacf->node;
	link->state &= ~DEV_CONFIG_PENDING;
	return;

cs_failed:
	cs_error(link->handle, last_fn, last_ret);
failed:
	pcmcia_release_configuration(link->handle);
	pcmcia_release_io(link->handle, &link->io);
	pcmcia_release_irq(link->handle, &link->irq);
}

/*
 * event callback
 */
static int pdacf_event(event_t event, int priority, event_callback_args_t *args)
{
	dev_link_t *link = args->client_data;
	pdacf_t *chip = link->priv;

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		snd_printdd(KERN_DEBUG "CARD_REMOVAL..\n");
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			chip->chip_status |= PDAUDIOCF_STAT_IS_STALE;
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		snd_printdd(KERN_DEBUG "CARD_INSERTION..\n");
		link->state |= DEV_PRESENT;
		pdacf_config(link);
		break;
#ifdef CONFIG_PM
	case CS_EVENT_PM_SUSPEND:
		snd_printdd(KERN_DEBUG "SUSPEND\n");
		link->state |= DEV_SUSPEND;
		if (chip) {
			snd_printdd(KERN_DEBUG "snd_pdacf_suspend calling\n");
			snd_pdacf_suspend(chip->card, PMSG_SUSPEND);
		}
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		snd_printdd(KERN_DEBUG "RESET_PHYSICAL\n");
		if (link->state & DEV_CONFIG)
			pcmcia_release_configuration(link->handle);
		break;
	case CS_EVENT_PM_RESUME:
		snd_printdd(KERN_DEBUG "RESUME\n");
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		snd_printdd(KERN_DEBUG "CARD_RESET\n");
		if (DEV_OK(link)) {
			snd_printdd(KERN_DEBUG "requestconfig...\n");
			pcmcia_request_configuration(link->handle, &link->conf);
			if (chip) {
				snd_printdd(KERN_DEBUG "calling snd_pdacf_resume\n");
				snd_pdacf_resume(chip->card);
			}
		}
		snd_printdd(KERN_DEBUG "resume done!\n");
		break;
#endif
	}
	return 0;
}

/*
 * Module entry points
 */
static struct pcmcia_device_id snd_pdacf_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x015d, 0x4c45),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, snd_pdacf_ids);

static struct pcmcia_driver pdacf_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "snd-pdaudiocf",
	},
	.attach		= snd_pdacf_attach,
	.event		= pdacf_event,
	.detach		= snd_pdacf_detach,
	.id_table	= snd_pdacf_ids,
};

static int __init init_pdacf(void)
{
	return pcmcia_register_driver(&pdacf_cs_driver);
}

static void __exit exit_pdacf(void)
{
	pcmcia_unregister_driver(&pdacf_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_pdacf);
module_exit(exit_pdacf);
