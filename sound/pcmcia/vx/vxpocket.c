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

/*
 please add the following as /etc/pcmcia/vxpocket.conf:
 
  device "snd-vxpocket"
     class "audio" module "snd-vxpocket"

  card "Digigram VX-POCKET"
    manfid 0x01f1, 0x0100
    bind "snd-vxpocket"

 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <pcmcia/version.h>
#include "vxpocket.h"
#include <sound/initval.h>

/*
 */

#ifdef COMPILE_VXP440
#define CARD_NAME	"VXPocket440"
#else
#define CARD_NAME	"VXPocket"
#endif

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Digigram " CARD_NAME);
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram," CARD_NAME "}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */
static int ibl[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");
module_param_array(ibl, int, NULL, 0444);
MODULE_PARM_DESC(ibl, "Capture IBL size for " CARD_NAME " soundcard.");
 

/*
 */

#ifdef COMPILE_VXP440

/* 1 DSP, 1 sync UER, 1 sync World Clock (NIY) */
/* SMPTE (NIY) */
/* 2 stereo analog input (line/micro) */
/* 2 stereo analog output */
/* Only output levels can be modified */
/* UER, but only for the first two inputs and outputs. */

#define NUM_CODECS	2
#define CARD_TYPE	VX_TYPE_VXP440
#define DEV_INFO	"snd-vxp440"

#else

/* 1 DSP, 1 sync UER */
/* 1 programmable clock (NIY) */
/* 1 stereo analog input (line/micro) */
/* 1 stereo analog output */
/* Only output levels can be modified */

#define NUM_CODECS	1
#define CARD_TYPE	VX_TYPE_VXPOCKET
#define DEV_INFO	"snd-vxpocket"

#endif

static dev_info_t dev_info = DEV_INFO;

static struct snd_vx_hardware vxp_hw = {
	.name = CARD_NAME,
	.type = CARD_TYPE,

	/* hardware specs */
	.num_codecs = NUM_CODECS,
	.num_ins = NUM_CODECS,
	.num_outs = NUM_CODECS,
	.output_level_max = VX_ANALOG_OUT_LEVEL_MAX,
};	

static struct snd_vxp_entry hw_entry = {
	.dev_info = &dev_info,

	/* module parameters */
	.index_table = index,
	.id_table = id,
	.enable_table = enable,
	.ibl = ibl,

	/* h/w config */
	.hardware = &vxp_hw,
	.ops = &snd_vxpocket_ops,
};

/*
 */
static dev_link_t *vxp_attach(void)
{
	return snd_vxpocket_attach(&hw_entry);
}

static void vxp_detach(dev_link_t *link)
{
	snd_vxpocket_detach(&hw_entry, link);
}

/*
 * Module entry points
 */

static struct pcmcia_driver vxp_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= DEV_INFO,
	},
	.attach		= vxp_attach,
	.detach		= vxp_detach
};

static int __init init_vxpocket(void)
{
	return pcmcia_register_driver(&vxp_cs_driver);
}

static void __exit exit_vxpocket(void)
{
	pcmcia_unregister_driver(&vxp_cs_driver);
	BUG_ON(hw_entry.dev_list != NULL);
}

module_init(init_vxpocket);
module_exit(exit_vxpocket);
