/*
 *	Initialisation code for Cyrix/NatSemi VSA1 softaudio
 *
 *	(C) Copyright 2003 Red Hat Inc <alan@lxorguk.ukuu.org.uk>
 *
 * XpressAudio(tm) is used on the Cyrix MediaGX (now NatSemi Geode) systems.
 * The older version (VSA1) provides fairly good soundblaster emulation
 * although there are a couple of bugs: large DMA buffers break record,
 * and the MPU event handling seems suspect. VSA2 allows the native driver
 * to control the AC97 audio engine directly and requires a different driver.
 *
 * Thanks to National Semiconductor for providing the needed information
 * on the XpressAudio(tm) internals.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * TO DO:
 *	Investigate whether we can portably support Cognac (5520) in the
 *	same manner.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "sound_config.h"

#include "sb.h"

/*
 *	Read a soundblaster compatible mixer register.
 *	In this case we are actually reading an SMI trap
 *	not real hardware.
 */

static u8 __devinit mixer_read(unsigned long io, u8 reg)
{
	outb(reg, io + 4);
	udelay(20);
	reg = inb(io + 5);
	udelay(20);
	return reg;
}

static int __devinit probe_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct address_info *hw_config;
	unsigned long base;
	void __iomem *mem;
	unsigned long io;
	u16 map;
	u8 irq, dma8, dma16;
	int oldquiet;
	extern int sb_be_quiet;
		
	base = pci_resource_start(pdev, 0);
	if(base == 0UL)
		return 1;
	
	mem = ioremap(base, 128);
	if (!mem)
		return 1;
	map = readw(mem + 0x18);	/* Read the SMI enables */
	iounmap(mem);
	
	/* Map bits
		0:1	* 0x20 + 0x200 = sb base
		2	sb enable
		3	adlib enable
		5	MPU enable 0x330
		6	MPU enable 0x300
		
	   The other bits may be used internally so must be masked */

	io = 0x220 + 0x20 * (map & 3);	   
	
	if(map & (1<<2))
		printk(KERN_INFO "kahlua: XpressAudio at 0x%lx\n", io);
	else
		return 1;
		
	if(map & (1<<5))
		printk(KERN_INFO "kahlua: MPU at 0x300\n");
	else if(map & (1<<6))
		printk(KERN_INFO "kahlua: MPU at 0x330\n");
	
	irq = mixer_read(io, 0x80) & 0x0F;
	dma8 = mixer_read(io, 0x81);

	// printk("IRQ=%x MAP=%x DMA=%x\n", irq, map, dma8);
	
	if(dma8 & 0x20)
		dma16 = 5;
	else if(dma8 & 0x40)
		dma16 = 6;
	else if(dma8 & 0x80)
		dma16 = 7;
	else
	{
		printk(KERN_ERR "kahlua: No 16bit DMA enabled.\n");
		return 1;
	}
		
	if(dma8 & 0x01)
		dma8 = 0;
	else if(dma8 & 0x02)
		dma8 = 1;
	else if(dma8 & 0x08)
		dma8 = 3;
	else
	{
		printk(KERN_ERR "kahlua: No 8bit DMA enabled.\n");
		return 1;
	}
	
	if(irq & 1)
		irq = 9;
	else if(irq & 2)
		irq = 5;
	else if(irq & 4)
		irq = 7;
	else if(irq & 8)
		irq = 10;
	else
	{
		printk(KERN_ERR "kahlua: SB IRQ not set.\n");
		return 1;
	}
	
	printk(KERN_INFO "kahlua: XpressAudio on IRQ %d, DMA %d, %d\n",
		irq, dma8, dma16);
	
	hw_config = kzalloc(sizeof(struct address_info), GFP_KERNEL);
	if(hw_config == NULL)
	{
		printk(KERN_ERR "kahlua: out of memory.\n");
		return 1;
	}
	
	pci_set_drvdata(pdev, hw_config);
	
	hw_config->io_base = io;
	hw_config->irq = irq;
	hw_config->dma = dma8;
	hw_config->dma2 = dma16;
	hw_config->name = "Cyrix XpressAudio";
	hw_config->driver_use_1 = SB_NO_MIDI | SB_PCI_IRQ;

	if (!request_region(io, 16, "soundblaster"))
		goto err_out_free;
	
	if(sb_dsp_detect(hw_config, 0, 0, NULL)==0)
	{
		printk(KERN_ERR "kahlua: audio not responding.\n");
		release_region(io, 16);
		goto err_out_free;
	}

	oldquiet = sb_be_quiet;	
	sb_be_quiet = 1;
	if(sb_dsp_init(hw_config, THIS_MODULE))
	{
		sb_be_quiet = oldquiet;
		goto err_out_free;
	}
	sb_be_quiet = oldquiet;
	
	return 0;

err_out_free:
	pci_set_drvdata(pdev, NULL);
	kfree(hw_config);
	return 1;
}

static void __devexit remove_one(struct pci_dev *pdev)
{
	struct address_info *hw_config = pci_get_drvdata(pdev);
	sb_dsp_unload(hw_config, 0);
	pci_set_drvdata(pdev, NULL);
	kfree(hw_config);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Kahlua VSA1 PCI Audio");
MODULE_LICENSE("GPL");

/*
 *	5530 only. The 5510/5520 decode is different.
 */

static DEFINE_PCI_DEVICE_TABLE(id_tbl) = {
	{ PCI_VDEVICE(CYRIX, PCI_DEVICE_ID_CYRIX_5530_AUDIO), 0 },
	{ }
};

MODULE_DEVICE_TABLE(pci, id_tbl);

static struct pci_driver kahlua_driver = {
	.name		= "kahlua",
	.id_table	= id_tbl,
	.probe		= probe_one,
	.remove		= __devexit_p(remove_one),
};


static int __init kahlua_init_module(void)
{
	printk(KERN_INFO "Cyrix Kahlua VSA1 XpressAudio support (c) Copyright 2003 Red Hat Inc\n");
	return pci_register_driver(&kahlua_driver);
}

static void __devexit kahlua_cleanup_module(void)
{
	pci_unregister_driver(&kahlua_driver);
}


module_init(kahlua_init_module);
module_exit(kahlua_cleanup_module);

