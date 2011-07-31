/*
 * sound/oss/sb_card.c
 *
 * Detection routine for the ISA Sound Blaster and compatable sound
 * cards.
 *
 * This file is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this
 * software for more info.
 *
 * This is a complete rewrite of the detection routines. This was
 * prompted by the PnP API change during v2.5 and the ugly state the
 * code was in.
 *
 * Copyright (C) by Paul Laufer 2002. Based on code originally by
 * Hannu Savolainen which was modified by many others over the
 * years. Authors specifically mentioned in the previous version were:
 * Daniel Stone, Alessandro Zummo, Jeff Garzik, Arnaldo Carvalho de
 * Melo, Daniel Church, and myself.
 *
 * 02-05-2003 Original Release, Paul Laufer <paul@laufernet.com>
 * 02-07-2003 Bug made it into first release. Take two.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/init.h>
#include "sound_config.h"
#include "sb_mixer.h"
#include "sb.h"
#ifdef CONFIG_PNP
#include <linux/pnp.h>
#endif /* CONFIG_PNP */
#include "sb_card.h"

MODULE_DESCRIPTION("OSS Soundblaster ISA PnP and legacy sound driver");
MODULE_LICENSE("GPL");

extern void *smw_free;

static int __initdata mpu_io	= 0;
static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma16	= -1;
static int __initdata type	= 0; /* Can set this to a specific card type */
static int __initdata esstype   = 0; /* ESS chip type */
static int __initdata acer 	= 0; /* Do acer notebook init? */
static int __initdata sm_games 	= 0; /* Logitech soundman games? */

static struct sb_card_config *legacy = NULL;

#ifdef CONFIG_PNP
static int pnp_registered;
static int __initdata pnp       = 1;
/*
static int __initdata uart401	= 0;
*/
#else
static int __initdata pnp       = 0;
#endif

module_param(io, int, 000);
MODULE_PARM_DESC(io,       "Soundblaster i/o base address (0x220,0x240,0x260,0x280)");
module_param(irq, int, 000);
MODULE_PARM_DESC(irq,	   "IRQ (5,7,9,10)");
module_param(dma, int, 000);
MODULE_PARM_DESC(dma,	   "8-bit DMA channel (0,1,3)");
module_param(dma16, int, 000);
MODULE_PARM_DESC(dma16,	   "16-bit DMA channel (5,6,7)");
module_param(mpu_io, int, 000);
MODULE_PARM_DESC(mpu_io,   "MPU base address");
module_param(type, int, 000);
MODULE_PARM_DESC(type,	   "You can set this to specific card type (doesn't " \
		 "work with pnp)");
module_param(sm_games, int, 000);
MODULE_PARM_DESC(sm_games, "Enable support for Logitech soundman games " \
		 "(doesn't work with pnp)");
module_param(esstype, int, 000);
MODULE_PARM_DESC(esstype,  "ESS chip type (doesn't work with pnp)");
module_param(acer, int, 000);
MODULE_PARM_DESC(acer,	   "Set this to detect cards in some ACER notebooks "\
		 "(doesn't work with pnp)");

#ifdef CONFIG_PNP
module_param(pnp, int, 000);
MODULE_PARM_DESC(pnp,     "Went set to 0 will disable detection using PnP. "\
		  "Default is 1.\n");
/* Not done yet.... */
/*
module_param(uart401, int, 000);
MODULE_PARM_DESC(uart401,  "When set to 1, will attempt to detect and enable"\
		 "the mpu on some clones");
*/
#endif /* CONFIG_PNP */

/* OSS subsystem card registration shared by PnP and legacy routines */
static int sb_register_oss(struct sb_card_config *scc, struct sb_module_options *sbmo)
{
	if (!request_region(scc->conf.io_base, 16, "soundblaster")) {
		printk(KERN_ERR "sb: ports busy.\n");
		kfree(scc);
		return -EBUSY;
	}

	if (!sb_dsp_detect(&scc->conf, 0, 0, sbmo)) {
		release_region(scc->conf.io_base, 16);
		printk(KERN_ERR "sb: Failed DSP Detect.\n");
		kfree(scc);
		return -ENODEV;
	}
	if(!sb_dsp_init(&scc->conf, THIS_MODULE)) {
		printk(KERN_ERR "sb: Failed DSP init.\n");
		kfree(scc);
		return -ENODEV;
	}
	if(scc->mpucnf.io_base > 0) {
		scc->mpu = 1;
		printk(KERN_INFO "sb: Turning on MPU\n");
		if(!probe_sbmpu(&scc->mpucnf, THIS_MODULE))
			scc->mpu = 0;
	}

	return 1;
}

static void sb_unload(struct sb_card_config *scc)
{
	sb_dsp_unload(&scc->conf, 0);
	if(scc->mpu)
		unload_sbmpu(&scc->mpucnf);
	kfree(scc);
}

/* Register legacy card with OSS subsystem */
static int __init sb_init_legacy(void)
{
	struct sb_module_options sbmo = {0};

	if((legacy = kzalloc(sizeof(struct sb_card_config), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "sb: Error: Could not allocate memory\n");
		return -ENOMEM;
	}

	legacy->conf.io_base      = io;
	legacy->conf.irq          = irq;
	legacy->conf.dma          = dma;
	legacy->conf.dma2         = dma16;
	legacy->conf.card_subtype = type;

	legacy->mpucnf.io_base = mpu_io;
	legacy->mpucnf.irq     = -1;
	legacy->mpucnf.dma     = -1;
	legacy->mpucnf.dma2    = -1;

	sbmo.esstype  = esstype;
	sbmo.sm_games = sm_games;
	sbmo.acer     = acer;

	return sb_register_oss(legacy, &sbmo);
}

#ifdef CONFIG_PNP

/* Populate the OSS subsystem structures with information from PnP */
static void sb_dev2cfg(struct pnp_dev *dev, struct sb_card_config *scc)
{
	scc->conf.io_base   = -1;
	scc->conf.irq       = -1;
	scc->conf.dma       = -1;
	scc->conf.dma2      = -1;
	scc->mpucnf.io_base = -1;
	scc->mpucnf.irq     = -1;
	scc->mpucnf.dma     = -1;
	scc->mpucnf.dma2    = -1;

	/* All clones layout their PnP tables differently and some use
	   different logical devices for the MPU */
	if(!strncmp("CTL",scc->card_id,3)) {
		scc->conf.io_base   = pnp_port_start(dev,0);
		scc->conf.irq       = pnp_irq(dev,0);
		scc->conf.dma       = pnp_dma(dev,0);
		scc->conf.dma2      = pnp_dma(dev,1);
		scc->mpucnf.io_base = pnp_port_start(dev,1);
		return;
	}
	if(!strncmp("tBA",scc->card_id,3)) {
		scc->conf.io_base   = pnp_port_start(dev,0);
		scc->conf.irq       = pnp_irq(dev,0);
		scc->conf.dma       = pnp_dma(dev,0);
		scc->conf.dma2      = pnp_dma(dev,1);
		return;
	}
	if(!strncmp("ESS",scc->card_id,3)) {
		scc->conf.io_base   = pnp_port_start(dev,0);
		scc->conf.irq       = pnp_irq(dev,0);
		scc->conf.dma       = pnp_dma(dev,0);
		scc->conf.dma2      = pnp_dma(dev,1);
	       	scc->mpucnf.io_base = pnp_port_start(dev,2);
		return;
	}
	if(!strncmp("CMI",scc->card_id,3)) {
		scc->conf.io_base = pnp_port_start(dev,0);
		scc->conf.irq     = pnp_irq(dev,0);
		scc->conf.dma     = pnp_dma(dev,0);
		scc->conf.dma2    = pnp_dma(dev,1);
		return;
	}
	if(!strncmp("RWB",scc->card_id,3)) {
		scc->conf.io_base = pnp_port_start(dev,0);
		scc->conf.irq     = pnp_irq(dev,0);
		scc->conf.dma     = pnp_dma(dev,0);
		return;
	}
	if(!strncmp("ALS",scc->card_id,3)) {
		if(!strncmp("ALS0007",scc->card_id,7)) {
			scc->conf.io_base = pnp_port_start(dev,0);
			scc->conf.irq     = pnp_irq(dev,0);
			scc->conf.dma     = pnp_dma(dev,0);
		} else {
			scc->conf.io_base = pnp_port_start(dev,0);
			scc->conf.irq     = pnp_irq(dev,0);
			scc->conf.dma     = pnp_dma(dev,1);
			scc->conf.dma2    = pnp_dma(dev,0);
		}
		return;
	}
	if(!strncmp("RTL",scc->card_id,3)) {
		scc->conf.io_base = pnp_port_start(dev,0);
		scc->conf.irq     = pnp_irq(dev,0);
		scc->conf.dma     = pnp_dma(dev,1);
		scc->conf.dma2    = pnp_dma(dev,0);
	}
}

static unsigned int sb_pnp_devices;

/* Probe callback function for the PnP API */
static int sb_pnp_probe(struct pnp_card_link *card, const struct pnp_card_device_id *card_id)
{
	struct sb_card_config *scc;
	struct sb_module_options sbmo = {0}; /* Default to 0 for PnP */
	struct pnp_dev *dev = pnp_request_card_device(card, card_id->devs[0].id, NULL);
	
	if(!dev){
		return -EBUSY;
	}

	if((scc = kzalloc(sizeof(struct sb_card_config), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "sb: Error: Could not allocate memory\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "sb: PnP: Found Card Named = \"%s\", Card PnP id = " \
	       "%s, Device PnP id = %s\n", card->card->name, card_id->id,
	       dev->id->id);

	scc->card_id = card_id->id;
	scc->dev_id = dev->id->id;
	sb_dev2cfg(dev, scc);

	printk(KERN_INFO "sb: PnP:      Detected at: io=0x%x, irq=%d, " \
	       "dma=%d, dma16=%d\n", scc->conf.io_base, scc->conf.irq,
	       scc->conf.dma, scc->conf.dma2);

	pnp_set_card_drvdata(card, scc);
	sb_pnp_devices++;

	return sb_register_oss(scc, &sbmo);
}

static void sb_pnp_remove(struct pnp_card_link *card)
{
	struct sb_card_config *scc = pnp_get_card_drvdata(card);

	if(!scc)
		return;

	printk(KERN_INFO "sb: PnP: Removing %s\n", scc->card_id);

	sb_unload(scc);
}

static struct pnp_card_driver sb_pnp_driver = {
	.name          = "OSS SndBlstr", /* 16 character limit */
	.id_table      = sb_pnp_card_table,
	.probe         = sb_pnp_probe,
	.remove        = sb_pnp_remove,
};
MODULE_DEVICE_TABLE(pnp_card, sb_pnp_card_table);
#endif /* CONFIG_PNP */

static void sb_unregister_all(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_card_driver(&sb_pnp_driver);
#endif
}

static int __init sb_init(void)
{
	int lres = 0;
	int pres = 0;

	printk(KERN_INFO "sb: Init: Starting Probe...\n");

	if(io != -1 && irq != -1 && dma != -1) {
		printk(KERN_INFO "sb: Probing legacy card with io=%x, "\
		       "irq=%d, dma=%d, dma16=%d\n",io, irq, dma, dma16);
		lres = sb_init_legacy();
	} else if((io != -1 || irq != -1 || dma != -1) ||
		  (!pnp && (io == -1 && irq == -1 && dma == -1)))
		printk(KERN_ERR "sb: Error: At least io, irq, and dma "\
		       "must be set for legacy cards.\n");

#ifdef CONFIG_PNP
	if(pnp) {
		int err = pnp_register_card_driver(&sb_pnp_driver);
		if (!err)
			pnp_registered = 1;
		pres = sb_pnp_devices;
	}
#endif
	printk(KERN_INFO "sb: Init: Done\n");

	/* If either PnP or Legacy registered a card then return
	 * success */
	if (pres == 0 && lres <= 0) {
		sb_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit sb_exit(void)
{
	printk(KERN_INFO "sb: Unloading...\n");

	/* Unload legacy card */
	if (legacy) {
		printk (KERN_INFO "sb: Unloading legacy card\n");
		sb_unload(legacy);
	}

	sb_unregister_all();

	vfree(smw_free);
	smw_free = NULL;
}

module_init(sb_init);
module_exit(sb_exit);
