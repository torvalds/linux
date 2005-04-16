/*
 * sound/opl3sa.c
 *
 * Low level driver for Yamaha YMF701B aka OPL3-SA chip
 * 
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 *	Alan Cox		Modularisation
 *	Christoph Hellwig	Adapted to module_init/module_exit
 *	Arnaldo C. de Melo	got rid of attach_uart401
 *
 * FIXME:
 * 	Check for install of mpu etc is wrong, should check result of the mss stuff
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#undef  SB_OK

#include "sound_config.h"

#include "ad1848.h"
#include "mpu401.h"

#ifdef SB_OK
#include "sb.h"
static int sb_initialized;
#endif

static DEFINE_SPINLOCK(lock);

static unsigned char opl3sa_read(int addr)
{
	unsigned long flags;
	unsigned char tmp;

	spin_lock_irqsave(&lock,flags);
	outb((0x1d), 0xf86);	/* password */
	outb(((unsigned char) addr), 0xf86);	/* address */
	tmp = inb(0xf87);	/* data */
	spin_unlock_irqrestore(&lock,flags);

	return tmp;
}

static void opl3sa_write(int addr, int data)
{
	unsigned long flags;

	spin_lock_irqsave(&lock,flags);
	outb((0x1d), 0xf86);	/* password */
	outb(((unsigned char) addr), 0xf86);	/* address */
	outb(((unsigned char) data), 0xf87);	/* data */
	spin_unlock_irqrestore(&lock,flags);
}

static int __init opl3sa_detect(void)
{
	int tmp;

	if (((tmp = opl3sa_read(0x01)) & 0xc4) != 0x04)
	{
		DDB(printk("OPL3-SA detect error 1 (%x)\n", opl3sa_read(0x01)));
		/* return 0; */
	}

	/*
	 * Check that the password feature has any effect
	 */
	
	if (inb(0xf87) == tmp)
	{
		DDB(printk("OPL3-SA detect failed 2 (%x/%x)\n", tmp, inb(0xf87)));
		return 0;
	}
	tmp = (opl3sa_read(0x04) & 0xe0) >> 5;

	if (tmp != 0 && tmp != 1)
	{
		DDB(printk("OPL3-SA detect failed 3 (%d)\n", tmp));
		return 0;
	}
	DDB(printk("OPL3-SA mode %x detected\n", tmp));

	opl3sa_write(0x01, 0x00);	/* Disable MSS */
	opl3sa_write(0x02, 0x00);	/* Disable SB */
	opl3sa_write(0x03, 0x00);	/* Disable MPU */

	return 1;
}

/*
 *    Probe and attach routines for the Windows Sound System mode of
 *     OPL3-SA
 */

static int __init probe_opl3sa_wss(struct address_info *hw_config, struct resource *ports)
{
	unsigned char tmp = 0x24;	/* WSS enable */

	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (OPL3-SA for example)
	 * return 0x00.
	 */

	if (!opl3sa_detect())
	{
		printk(KERN_ERR "OSS: OPL3-SA chip not found\n");
		return 0;
	}
	
	switch (hw_config->io_base)
	{
		case 0x530:
			tmp |= 0x00;
			break;
		case 0xe80:
			tmp |= 0x08;
			break;
		case 0xf40:
			tmp |= 0x10;
			break;
		case 0x604:
			tmp |= 0x18;
			break;
		default:
			printk(KERN_ERR "OSS: Unsupported OPL3-SA/WSS base %x\n", hw_config->io_base);
		  return 0;
	}

	opl3sa_write(0x01, tmp);	/* WSS setup register */

	return probe_ms_sound(hw_config, ports);
}

static void __init attach_opl3sa_wss(struct address_info *hw_config, struct resource *ports)
{
	int nm = num_mixers;

	/* FIXME */
	attach_ms_sound(hw_config, ports, THIS_MODULE);
	if (num_mixers > nm)	/* A mixer was installed */
	{
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_CD);
		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH);
		AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
	}
}


static int __init probe_opl3sa_mpu(struct address_info *hw_config)
{
	unsigned char conf;
	static signed char irq_bits[] = {
		-1, -1, -1, -1, -1, 1, -1, 2, -1, 3, 4
	};

	if (hw_config->irq > 10)
	{
		printk(KERN_ERR "OPL3-SA: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (irq_bits[hw_config->irq] == -1)
	{
		printk(KERN_ERR "OPL3-SA: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	switch (hw_config->io_base)
	{
		case 0x330:
			conf = 0x00;
			break;
		case 0x332:
			conf = 0x20;
			break;
		case 0x334:
			conf = 0x40;
			break;
		case 0x300:
			conf = 0x60;
			break;
		default:
			return 0;	/* Invalid port */
	}

	conf |= 0x83;		/* MPU & OPL3 (synth) & game port enable */
	conf |= irq_bits[hw_config->irq] << 2;

	opl3sa_write(0x03, conf);

	hw_config->name = "OPL3-SA (MPU401)";

	return probe_uart401(hw_config, THIS_MODULE);
}

static void __exit unload_opl3sa_wss(struct address_info *hw_config)
{
	int dma2 = hw_config->dma2;

	if (dma2 == -1)
		dma2 = hw_config->dma;

	release_region(0xf86, 2);
	release_region(hw_config->io_base, 4);

	ad1848_unload(hw_config->io_base + 4,
		      hw_config->irq,
		      hw_config->dma,
		      dma2,
		      0);
	sound_unload_audiodev(hw_config->slots[0]);
}

static inline void __exit unload_opl3sa_mpu(struct address_info *hw_config)
{
	unload_uart401(hw_config);
}

#ifdef SB_OK
static inline void __exit unload_opl3sa_sb(struct address_info *hw_config)
{
	sb_dsp_unload(hw_config);
}
#endif

static int found_mpu;

static struct address_info cfg;
static struct address_info cfg_mpu;

static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;
static int __initdata mpu_io	= -1;
static int __initdata mpu_irq	= -1;

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(dma, int, 0);
module_param(dma2, int, 0);
module_param(mpu_io, int, 0);
module_param(mpu_irq, int, 0);

static int __init init_opl3sa(void)
{
	struct resource *ports;
	if (io == -1 || irq == -1 || dma == -1) {
		printk(KERN_ERR "opl3sa: dma, irq and io must be set.\n");
		return -EINVAL;
	}

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;
	
	cfg_mpu.io_base = mpu_io;
	cfg_mpu.irq = mpu_irq;

	ports = request_region(io + 4, 4, "ad1848");
	if (!ports)
		return -EBUSY;

	if (!request_region(0xf86, 2, "OPL3-SA"))/* Control port is busy */ {
		release_region(io + 4, 4);
		return 0;
	}

	if (!request_region(io, 4, "WSS config")) {
		release_region(0x86, 2);
		release_region(io + 4, 4);
		return 0;
	}

	if (probe_opl3sa_wss(&cfg, ports) == 0) {
		release_region(0xf86, 2);
		release_region(io, 4);
		release_region(io + 4, 4);
		return -ENODEV;
	}

	found_mpu=probe_opl3sa_mpu(&cfg_mpu);

	attach_opl3sa_wss(&cfg, ports);
	return 0;
}

static void __exit cleanup_opl3sa(void)
{
	if(found_mpu)
		unload_opl3sa_mpu(&cfg_mpu);
	unload_opl3sa_wss(&cfg);
}

module_init(init_opl3sa);
module_exit(cleanup_opl3sa);

#ifndef MODULE
static int __init setup_opl3sa(char *str)
{
	/* io, irq, dma, dma2, mpu_io, mpu_irq */
	int ints[7];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	mpu_io	= ints[5];
	mpu_irq	= ints[6];

	return 1;
}

__setup("opl3sa=", setup_opl3sa);
#endif
MODULE_LICENSE("GPL");
