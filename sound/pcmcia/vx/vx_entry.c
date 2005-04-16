/*
 * Driver for Digigram VXpocket soundcards
 *
 * PCMCIA entry part
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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
#include "vxpocket.h"
#include <pcmcia/ciscode.h>
#include <pcmcia/cisreg.h>


MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Common routines for Digigram PCMCIA VX drivers");
MODULE_LICENSE("GPL");

/*
 * prototypes
 */
static void vxpocket_config(dev_link_t *link);
static int vxpocket_event(event_t event, int priority, event_callback_args_t *args);


static void vxpocket_release(dev_link_t *link)
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
static int snd_vxpocket_free(vx_core_t *chip)
{
	struct snd_vxpocket *vxp = (struct snd_vxpocket *)chip;
	struct snd_vxp_entry *hw;
	dev_link_t *link = &vxp->link;

	vxpocket_release(link);

	/* Break the link with Card Services */
	if (link->handle)
		pcmcia_deregister_client(link->handle);

	hw = vxp->hw_entry;
	if (hw)
		hw->card_list[vxp->index] = NULL;
	chip->card = NULL;
	if (chip->dev)
		kfree(chip->dev);

	snd_vx_free_firmware(chip);
	kfree(chip);
	return 0;
}

static int snd_vxpocket_dev_free(snd_device_t *device)
{
	vx_core_t *chip = device->device_data;
	return snd_vxpocket_free(chip);
}

/*
 * snd_vxpocket_attach - attach callback for cs
 * @hw: the hardware information
 */
dev_link_t *snd_vxpocket_attach(struct snd_vxp_entry *hw)
{
	client_reg_t client_reg;	/* Register with cardmgr */
	dev_link_t *link;		/* Info for cardmgr */
	int i, ret;
	vx_core_t *chip;
	struct snd_vxpocket *vxp;
	snd_card_t *card;
	static snd_device_ops_t ops = {
		.dev_free =	snd_vxpocket_dev_free,
	};

	snd_printdd(KERN_DEBUG "vxpocket_attach called\n");
	/* find an empty slot from the card list */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (! hw->card_list[i])
			break;
	}
	if (i >= SNDRV_CARDS) {
		snd_printk(KERN_ERR "vxpocket: too many cards found\n");
		return NULL;
	}
	if (! hw->enable_table[i])
		return NULL; /* disabled explicitly */

	/* ok, create a card instance */
	card = snd_card_new(hw->index_table[i], hw->id_table[i], THIS_MODULE, 0);
	if (card == NULL) {
		snd_printk(KERN_ERR "vxpocket: cannot create a card instance\n");
		return NULL;
	}

	chip = snd_vx_create(card, hw->hardware, hw->ops,
			     sizeof(struct snd_vxpocket) - sizeof(vx_core_t));
	if (! chip)
		return NULL;

#ifdef SND_VX_FW_LOADER
	/* fake a device here since pcmcia doesn't give a valid device... */
	chip->dev = kcalloc(1, sizeof(*chip->dev), GFP_KERNEL);
	if (! chip->dev) {
		snd_printk(KERN_ERR "vxp: can't malloc chip->dev\n");
		kfree(chip);
		snd_card_free(card);
		return NULL;
	}
	device_initialize(chip->dev);
	sprintf(chip->dev->bus_id, "vxpocket%d", i);
#endif

	if (snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops) < 0) {
		kfree(chip);
		snd_card_free(card);
		return NULL;
	}

	vxp = (struct snd_vxpocket *)chip;
	vxp->index = i;
	vxp->hw_entry = hw;
	chip->ibl.size = hw->ibl[i];
	hw->card_list[i] = chip;

	link = &vxp->link;
	link->priv = chip;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	link->io.NumPorts1 = 16;

	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	// link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED;

	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->irq.Handler = &snd_vx_irq_handler;
	link->irq.Instance = chip;

	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.Vcc = 50;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->conf.ConfigIndex = 1;
	link->conf.Present = PRESENT_OPTION;

	/* Register with Card Services */
	memset(&client_reg, 0, sizeof(client_reg));
	client_reg.dev_info = hw->dev_info;
	client_reg.EventMask = 
		CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL
#ifdef CONFIG_PM
		| CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET
		| CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME
#endif
		;
	client_reg.event_handler = &vxpocket_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;

	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		snd_card_free(card);
		return NULL;
	}

	/* Chain drivers */
	link->next = hw->dev_list;
	hw->dev_list = link;

	/* snd_card_set_pm_callback(card, snd_vxpocket_suspend, snd_vxpocket_resume, chip); */

	return link;
}


/**
 * snd_vxpocket_assign_resources - initialize the hardware and card instance.
 * @port: i/o port for the card
 * @irq: irq number for the card
 *
 * this function assigns the specified port and irq, boot the card,
 * create pcm and control instances, and initialize the rest hardware.
 *
 * returns 0 if successful, or a negative error code.
 */
static int snd_vxpocket_assign_resources(vx_core_t *chip, int port, int irq)
{
	int err;
	snd_card_t *card = chip->card;
	struct snd_vxpocket *vxp = (struct snd_vxpocket *)chip;

	snd_printdd(KERN_DEBUG "vxpocket assign resources: port = 0x%x, irq = %d\n", port, irq);
	vxp->port = port;

	sprintf(card->shortname, "Digigram %s", card->driver);
	sprintf(card->longname, "%s at 0x%x, irq %i",
		card->shortname, port, irq);

	chip->irq = irq;

	if ((err = snd_vx_setup_firmware(chip)) < 0)
		return err;

	return 0;
}


/*
 * snd_vxpocket_detach - detach callback for cs
 * @hw: the hardware information
 */
void snd_vxpocket_detach(struct snd_vxp_entry *hw, dev_link_t *link)
{
	vx_core_t *chip;

	if (! link)
		return;

	chip = link->priv;

	snd_printdd(KERN_DEBUG "vxpocket_detach called\n");
	/* Remove the interface data from the linked list */
	if (hw) {
		dev_link_t **linkp;
		/* Locate device structure */
		for (linkp = &hw->dev_list; *linkp; linkp = &(*linkp)->next)
			if (*linkp == link) {
				*linkp = link->next;
				break;
			}
	}
	chip->chip_status |= VX_STAT_IS_STALE; /* to be sure */
	snd_card_disconnect(chip->card);
	snd_card_free_in_thread(chip->card);
}

/*
 * configuration callback
 */

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void vxpocket_config(dev_link_t *link)
{
	client_handle_t handle = link->handle;
	vx_core_t *chip = link->priv;
	struct snd_vxpocket *vxp = (struct snd_vxpocket *)chip;
	tuple_t tuple;
	cisparse_t *parse = NULL;
	u_short buf[32];
	int last_fn, last_ret;

	snd_printdd(KERN_DEBUG "vxpocket_config called\n");
	parse = kmalloc(sizeof(*parse), GFP_KERNEL);
	if (! parse) {
		snd_printk(KERN_ERR "vx: cannot allocate\n");
		return;
	}
	tuple.Attributes = 0;
	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	tuple.DesiredTuple = CISTPL_CONFIG;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, parse));
	link->conf.ConfigBase = parse->config.base;
	link->conf.Present = parse->config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	CS_CHECK(RequestIO, pcmcia_request_io(handle, &link->io));
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link->handle, &link->conf));

	if (snd_vxpocket_assign_resources(chip, link->io.BasePort1, link->irq.AssignedIRQ) < 0)
		goto failed;

	link->dev = &vxp->node;
	link->state &= ~DEV_CONFIG_PENDING;
	kfree(parse);
	return;

cs_failed:
	cs_error(link->handle, last_fn, last_ret);
failed:
	pcmcia_release_configuration(link->handle);
	pcmcia_release_io(link->handle, &link->io);
	pcmcia_release_irq(link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;
	kfree(parse);
}


/*
 * event callback
 */
static int vxpocket_event(event_t event, int priority, event_callback_args_t *args)
{
	dev_link_t *link = args->client_data;
	vx_core_t *chip = link->priv;

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		snd_printdd(KERN_DEBUG "CARD_REMOVAL..\n");
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			chip->chip_status |= VX_STAT_IS_STALE;
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		snd_printdd(KERN_DEBUG "CARD_INSERTION..\n");
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		vxpocket_config(link);
		break;
#ifdef CONFIG_PM
	case CS_EVENT_PM_SUSPEND:
		snd_printdd(KERN_DEBUG "SUSPEND\n");
		link->state |= DEV_SUSPEND;
		if (chip && chip->card->pm_suspend) {
			snd_printdd(KERN_DEBUG "snd_vx_suspend calling\n");
			chip->card->pm_suspend(chip->card, PMSG_SUSPEND);
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
			//struct snd_vxpocket *vxp = (struct snd_vxpocket *)chip;
			snd_printdd(KERN_DEBUG "requestconfig...\n");
			pcmcia_request_configuration(link->handle, &link->conf);
			if (chip && chip->card->pm_resume) {
				snd_printdd(KERN_DEBUG "calling snd_vx_resume\n");
				chip->card->pm_resume(chip->card);
			}
		}
		snd_printdd(KERN_DEBUG "resume done!\n");
		break;
#endif
	}
	return 0;
}

/*
 * exported stuffs
 */
EXPORT_SYMBOL(snd_vxpocket_ops);
EXPORT_SYMBOL(snd_vxpocket_attach);
EXPORT_SYMBOL(snd_vxpocket_detach);
