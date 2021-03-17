// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VXpocket V2/440 soundcards
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>

 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "vxpocket.h"
#include <pcmcia/ciscode.h>
#include <pcmcia/cisreg.h>
#include <sound/initval.h>
#include <sound/tlv.h>

/*
 */

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Digigram VXPocket");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram,VXPocket},{Digigram,VXPocket440}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */
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


/*
 */
static void vxpocket_release(struct pcmcia_device *link)
{
	free_irq(link->irq, link->priv);
	pcmcia_disable_device(link);
}

/*
 * destructor, called from snd_card_free_when_closed()
 */
static int snd_vxpocket_dev_free(struct snd_device *device)
{
	struct vx_core *chip = device->device_data;

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

static const DECLARE_TLV_DB_SCALE(db_scale_old_vol, -11350, 50, 0);

static const struct snd_vx_hardware vxpocket_hw = {
	.name = "VXPocket",
	.type = VX_TYPE_VXPOCKET,

	/* hardware specs */
	.num_codecs = 1,
	.num_ins = 1,
	.num_outs = 1,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
	.output_level_db_scale = db_scale_old_vol,
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

static const struct snd_vx_hardware vxp440_hw = {
	.name = "VXPocket440",
	.type = VX_TYPE_VXP440,

	/* hardware specs */
	.num_codecs = 2,
	.num_ins = 2,
	.num_outs = 2,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
	.output_level_db_scale = db_scale_old_vol,
};	


/*
 * create vxpocket instance
 */
static int snd_vxpocket_new(struct snd_card *card, int ibl,
			    struct pcmcia_device *link,
			    struct snd_vxpocket **chip_ret)
{
	struct vx_core *chip;
	struct snd_vxpocket *vxp;
	static const struct snd_device_ops ops = {
		.dev_free =	snd_vxpocket_dev_free,
	};
	int err;

	chip = snd_vx_create(card, &vxpocket_hw, &snd_vxpocket_ops,
			     sizeof(struct snd_vxpocket) - sizeof(struct vx_core));
	if (!chip)
		return -ENOMEM;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		kfree(chip);
		return err;
	}
	chip->ibl.size = ibl;

	vxp = to_vxpocket(chip);

	vxp->p_dev = link;
	link->priv = chip;

	link->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;
	link->resource[0]->end = 16;

	link->config_flags |= CONF_ENABLE_IRQ;
	link->config_index = 1;
	link->config_regs = PRESENT_OPTION;

	*chip_ret = vxp;
	return 0;
}


/**
 * snd_vxpocket_assign_resources - initialize the hardware and card instance.
 * @chip: VX core instance
 * @port: i/o port for the card
 * @irq: irq number for the card
 *
 * this function assigns the specified port and irq, boot the card,
 * create pcm and control instances, and initialize the rest hardware.
 *
 * returns 0 if successful, or a negative error code.
 */
static int snd_vxpocket_assign_resources(struct vx_core *chip, int port, int irq)
{
	int err;
	struct snd_card *card = chip->card;
	struct snd_vxpocket *vxp = to_vxpocket(chip);

	snd_printdd(KERN_DEBUG "vxpocket assign resources: port = 0x%x, irq = %d\n", port, irq);
	vxp->port = port;

	sprintf(card->shortname, "Digigram %s", card->driver);
	sprintf(card->longname, "%s at 0x%x, irq %i",
		card->shortname, port, irq);

	chip->irq = irq;
	card->sync_irq = chip->irq;

	if ((err = snd_vx_setup_firmware(chip)) < 0)
		return err;

	return 0;
}


/*
 * configuration callback
 */

static int vxpocket_config(struct pcmcia_device *link)
{
	struct vx_core *chip = link->priv;
	int ret;

	snd_printdd(KERN_DEBUG "vxpocket_config called\n");

	/* redefine hardware record according to the VERSION1 string */
	if (!strcmp(link->prod_id[1], "VX-POCKET")) {
		snd_printdd("VX-pocket is detected\n");
	} else {
		snd_printdd("VX-pocket 440 is detected\n");
		/* overwrite the hardware information */
		chip->hw = &vxp440_hw;
		chip->type = vxp440_hw.type;
		strcpy(chip->card->driver, vxp440_hw.name);
	}

	ret = pcmcia_request_io(link);
	if (ret)
		goto failed_preirq;

	ret = request_threaded_irq(link->irq, snd_vx_irq_handler,
				   snd_vx_threaded_irq_handler,
				   IRQF_SHARED, link->devname, link->priv);
	if (ret)
		goto failed_preirq;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	chip->dev = &link->dev;

	if (snd_vxpocket_assign_resources(chip, link->resource[0]->start,
						link->irq) < 0)
		goto failed;

	return 0;

 failed:
	free_irq(link->irq, link->priv);
failed_preirq:
	pcmcia_disable_device(link);
	return -ENODEV;
}

#ifdef CONFIG_PM

static int vxp_suspend(struct pcmcia_device *link)
{
	struct vx_core *chip = link->priv;

	snd_printdd(KERN_DEBUG "SUSPEND\n");
	if (chip) {
		snd_printdd(KERN_DEBUG "snd_vx_suspend calling\n");
		snd_vx_suspend(chip);
	}

	return 0;
}

static int vxp_resume(struct pcmcia_device *link)
{
	struct vx_core *chip = link->priv;

	snd_printdd(KERN_DEBUG "RESUME\n");
	if (pcmcia_dev_present(link)) {
		//struct snd_vxpocket *vxp = (struct snd_vxpocket *)chip;
		if (chip) {
			snd_printdd(KERN_DEBUG "calling snd_vx_resume\n");
			snd_vx_resume(chip);
		}
	}
	snd_printdd(KERN_DEBUG "resume done!\n");

	return 0;
}

#endif


/*
 */
static int vxpocket_probe(struct pcmcia_device *p_dev)
{
	struct snd_card *card;
	struct snd_vxpocket *vxp;
	int i, err;

	/* find an empty slot from the card list */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (!(card_alloc & (1 << i)))
			break;
	}
	if (i >= SNDRV_CARDS) {
		snd_printk(KERN_ERR "vxpocket: too many cards found\n");
		return -EINVAL;
	}
	if (! enable[i])
		return -ENODEV; /* disabled explicitly */

	/* ok, create a card instance */
	err = snd_card_new(&p_dev->dev, index[i], id[i], THIS_MODULE,
			   0, &card);
	if (err < 0) {
		snd_printk(KERN_ERR "vxpocket: cannot create a card instance\n");
		return err;
	}

	err = snd_vxpocket_new(card, ibl[i], p_dev, &vxp);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = vxp;

	vxp->index = i;
	card_alloc |= 1 << i;

	vxp->p_dev = p_dev;

	return vxpocket_config(p_dev);
}

static void vxpocket_detach(struct pcmcia_device *link)
{
	struct snd_vxpocket *vxp;
	struct vx_core *chip;

	if (! link)
		return;

	vxp = link->priv;
	chip = (struct vx_core *)vxp;
	card_alloc &= ~(1 << vxp->index);

	chip->chip_status |= VX_STAT_IS_STALE; /* to be sure */
	snd_card_disconnect(chip->card);
	vxpocket_release(link);
	snd_card_free_when_closed(chip->card);
}

/*
 * Module entry points
 */

static const struct pcmcia_device_id vxp_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01f1, 0x0100),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, vxp_ids);

static struct pcmcia_driver vxp_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "snd-vxpocket",
	.probe		= vxpocket_probe,
	.remove		= vxpocket_detach,
	.id_table	= vxp_ids,
#ifdef CONFIG_PM
	.suspend	= vxp_suspend,
	.resume		= vxp_resume,
#endif
};
module_pcmcia_driver(vxp_cs_driver);
