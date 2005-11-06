/*
 * Driver for Digigram VXpocket V2/440 soundcards
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
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include "vxpocket.h"
#include <pcmcia/ciscode.h>
#include <pcmcia/cisreg.h>
#include <sound/initval.h>

/*
 */

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Digigram VXPocket");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram,VXPocket},{Digigram,VXPocket440}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */
static int ibl[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for VXPocket soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for VXPocket soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable VXPocket soundcard.");
module_param_array(ibl, int, NULL, 0444);
MODULE_PARM_DESC(ibl, "Capture IBL size for VXPocket soundcard.");
 

/*
 */

static unsigned int card_alloc;
static dev_link_t *dev_list;		/* Linked list of devices */
static dev_info_t dev_info = "snd-vxpocket";


static int vxpocket_event(event_t event, int priority, event_callback_args_t *args);


/*
 */
static void vxpocket_release(dev_link_t *link)
{
	if (link->state & DEV_CONFIG) {
		/* release cs resources */
		pcmcia_release_configuration(link->handle);
		pcmcia_release_io(link->handle, &link->io);
		pcmcia_release_irq(link->handle, &link->irq);
		link->state &= ~DEV_CONFIG;
	}
	if (link->handle) {
		/* Break the link with Card Services */
		pcmcia_deregister_client(link->handle);
		link->handle = NULL;
	}
}

/*
 * destructor, called from snd_card_free_in_thread()
 */
static int snd_vxpocket_dev_free(snd_device_t *device)
{
	vx_core_t *chip = device->device_data;

	snd_vx_free_firmware(chip);
	kfree(chip);
	return 0;
}


/*
 * Hardware information
 */

/* VX-pocket V2
 *
 * 1 DSP, 1 sync UER
 * 1 programmable clock (NIY)
 * 1 stereo analog input (line/micro)
 * 1 stereo analog output
 * Only output levels can be modified
 */

static struct snd_vx_hardware vxpocket_hw = {
	.name = "VXPocket",
	.type = VX_TYPE_VXPOCKET,

	/* hardware specs */
	.num_codecs = 1,
	.num_ins = 1,
	.num_outs = 1,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
};	

/* VX-pocket 440
 *
 * 1 DSP, 1 sync UER, 1 sync World Clock (NIY)
 * SMPTE (NIY)
 * 2 stereo analog input (line/micro)
 * 2 stereo analog output
 * Only output levels can be modified
 * UER, but only for the first two inputs and outputs.
 */

static struct snd_vx_hardware vxp440_hw = {
	.name = "VXPocket440",
	.type = VX_TYPE_VXP440,

	/* hardware specs */
	.num_codecs = 2,
	.num_ins = 2,
	.num_outs = 2,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
};	


/*
 * create vxpocket instance
 */
static struct snd_vxpocket *snd_vxpocket_new(snd_card_t *card, int ibl)
{
	client_reg_t client_reg;	/* Register with cardmgr */
	dev_link_t *link;		/* Info for cardmgr */
	vx_core_t *chip;
	struct snd_vxpocket *vxp;
	int ret;
	static snd_device_ops_t ops = {
		.dev_free =	snd_vxpocket_dev_free,
	};

	chip = snd_vx_create(card, &vxpocket_hw, &snd_vxpocket_ops,
			     sizeof(struct snd_vxpocket) - sizeof(vx_core_t));
	if (! chip)
		return NULL;

	if (snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops) < 0) {
		kfree(chip);
		return NULL;
	}
	chip->ibl.size = ibl;

	vxp = (struct snd_vxpocket *)chip;

	link = &vxp->link;
	link->priv = chip;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	link->io.NumPorts1 = 16;

	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;

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
	client_reg.dev_info = &dev_info;
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
		return NULL;
	}

	return vxp;
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
	cisparse_t *parse;
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

	/* redefine hardware record according to the VERSION1 string */
	tuple.DesiredTuple = CISTPL_VERS_1;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, parse));
	if (! strcmp(parse->version_1.str + parse->version_1.ofs[1], "VX-POCKET")) {
		snd_printdd("VX-pocket is detected\n");
	} else {
		snd_printdd("VX-pocket 440 is detected\n");
		/* overwrite the hardware information */
		chip->hw = &vxp440_hw;
		chip->type = vxp440_hw.type;
		strcpy(chip->card->driver, vxp440_hw.name);
	}

	/* Configure card */
	link->state |= DEV_CONFIG;

	CS_CHECK(RequestIO, pcmcia_request_io(handle, &link->io));
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link->handle, &link->conf));

	chip->dev = &handle_to_dev(link->handle);
	snd_card_set_dev(chip->card, chip->dev);

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
		if (link->state & DEV_CONFIG)
			chip->chip_status |= VX_STAT_IS_STALE;
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
 */
static dev_link_t *vxpocket_attach(void)
{
	snd_card_t *card;
	struct snd_vxpocket *vxp;
	int i;

	/* find an empty slot from the card list */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (! card_alloc & (1 << i))
			break;
	}
	if (i >= SNDRV_CARDS) {
		snd_printk(KERN_ERR "vxpocket: too many cards found\n");
		return NULL;
	}
	if (! enable[i])
		return NULL; /* disabled explicitly */

	/* ok, create a card instance */
	card = snd_card_new(index[i], id[i], THIS_MODULE, 0);
	if (card == NULL) {
		snd_printk(KERN_ERR "vxpocket: cannot create a card instance\n");
		return NULL;
	}

	vxp = snd_vxpocket_new(card, ibl[i]);
	if (! vxp) {
		snd_card_free(card);
		return NULL;
	}

	vxp->index = i;
	card_alloc |= 1 << i;

	/* Chain drivers */
	vxp->link.next = dev_list;
	dev_list = &vxp->link;

	return &vxp->link;
}

static void vxpocket_detach(dev_link_t *link)
{
	struct snd_vxpocket *vxp;
	vx_core_t *chip;
	dev_link_t **linkp;

	if (! link)
		return;

	vxp = link->priv;
	chip = (vx_core_t *)vxp;
	card_alloc &= ~(1 << vxp->index);

	/* Remove the interface data from the linked list */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link) {
			*linkp = link->next;
			break;
		}

	chip->chip_status |= VX_STAT_IS_STALE; /* to be sure */
	snd_card_disconnect(chip->card);
	vxpocket_release(link);
	snd_card_free_in_thread(chip->card);
}

/*
 * Module entry points
 */

static struct pcmcia_device_id vxp_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01f1, 0x0100),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, vxp_ids);

static struct pcmcia_driver vxp_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "snd-vxpocket",
	},
	.attach		= vxpocket_attach,
	.detach		= vxpocket_detach,
	.event		= vxpocket_event,
	.id_table	= vxp_ids,
};

static int __init init_vxpocket(void)
{
	return pcmcia_register_driver(&vxp_cs_driver);
}

static void __exit exit_vxpocket(void)
{
	pcmcia_unregister_driver(&vxp_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_vxpocket);
module_exit(exit_vxpocket);
