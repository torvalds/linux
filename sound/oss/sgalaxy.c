/*
 * sound/sgalaxy.c
 *
 * Low level driver for Aztech Sound Galaxy cards.
 * Copyright 1998 Artur Skawina <skawina@geocities.com>
 *
 * Supported cards:
 *    Aztech Sound Galaxy Waverider Pro 32 - 3D
 *    Aztech Sound Galaxy Washington 16
 *
 * Based on cs4232.c by Hannu Savolainen and Alan Cox.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 * 11-10-2000	Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
 *		Added __init to sb_rst() and sb_cmd()
 */

#include <linux/init.h>
#include <linux/module.h>

#include "sound_config.h"
#include "ad1848.h"

static void sleep( unsigned howlong )
{
	current->state   = TASK_INTERRUPTIBLE;
	schedule_timeout(howlong);
}

#define DPORT 0x80

/* Sound Blaster regs */

#define SBDSP_RESET      0x6
#define SBDSP_READ       0xA
#define SBDSP_COMMAND    0xC
#define SBDSP_STATUS     SBDSP_COMMAND
#define SBDSP_DATA_AVAIL 0xE

static int __init sb_rst(int base)
{
	int   i;
   
	outb( 1, base+SBDSP_RESET );     /* reset the DSP */
	outb( 0, base+SBDSP_RESET );
    
	for ( i=0; i<500; i++ )          /* delay */
		inb(DPORT);
      
	for ( i=0; i<100000; i++ )
	{
		if ( inb( base+SBDSP_DATA_AVAIL )&0x80 )
			break;
	}

	if ( inb( base+SBDSP_READ )!=0xAA )
		return 0;

	return 1;
}

static int __init sb_cmd( int base, unsigned char val )
{
	int  i;

	for ( i=100000; i; i-- )
	{
		if ( (inb( base+SBDSP_STATUS )&0x80)==0 )
		{
        		outb( val, base+SBDSP_COMMAND );
        		break;
		}
	}
	return i;      /* i>0 == success */
}


#define ai_sgbase    driver_use_1

static int __init probe_sgalaxy( struct address_info *ai )
{
	struct resource *ports;
	int n;

	if (!request_region(ai->io_base, 4, "WSS config")) {
		printk(KERN_ERR "sgalaxy: WSS IO port 0x%03x not available\n", ai->io_base);
		return 0;
	}

	ports = request_region(ai->io_base + 4, 4, "ad1848");
	if (!ports) {
		printk(KERN_ERR "sgalaxy: WSS IO port 0x%03x not available\n", ai->io_base);
		release_region(ai->io_base, 4);
		return 0;
	}

	if (!request_region( ai->ai_sgbase, 0x10, "SoundGalaxy SB")) {
		printk(KERN_ERR "sgalaxy: SB IO port 0x%03x not available\n", ai->ai_sgbase);
		release_region(ai->io_base + 4, 4);
		release_region(ai->io_base, 4);
		return 0;
	}
        
	if (ad1848_detect(ports, NULL, ai->osp))
		goto out;  /* The card is already active, check irq etc... */
        
	/* switch to MSS/WSS mode */
   
	sb_rst( ai->ai_sgbase );
   
	sb_cmd( ai->ai_sgbase, 9 );
	sb_cmd( ai->ai_sgbase, 0 );

	sleep( HZ/10 );

out:
      	if (!probe_ms_sound(ai, ports)) {
		release_region(ai->io_base + 4, 4);
		release_region(ai->io_base, 4);
		release_region(ai->ai_sgbase, 0x10);
		return 0;
	}

	attach_ms_sound(ai, ports, THIS_MODULE);
	n=ai->slots[0];
	
	if (n!=-1 && audio_devs[n]->mixer_dev != -1 ) {
		AD1848_REROUTE( SOUND_MIXER_LINE1, SOUND_MIXER_LINE );   /* Line-in */
		AD1848_REROUTE( SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH );  /* FM+Wavetable*/
		AD1848_REROUTE( SOUND_MIXER_LINE3, SOUND_MIXER_CD );     /* CD */
	}
	return 1;
}

static void __exit unload_sgalaxy( struct address_info *ai )
{
	unload_ms_sound( ai );
	release_region( ai->ai_sgbase, 0x10 );
}

static struct address_info cfg;

static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;
static int __initdata sgbase	= -1;

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(dma, int, 0);
module_param(dma2, int, 0);
module_param(sgbase, int, 0);

static int __init init_sgalaxy(void)
{
	cfg.io_base   = io;
	cfg.irq       = irq;
	cfg.dma       = dma;
	cfg.dma2      = dma2;
	cfg.ai_sgbase = sgbase;

	if (cfg.io_base == -1 || cfg.irq == -1 || cfg.dma == -1 || cfg.ai_sgbase == -1 ) {
		printk(KERN_ERR "sgalaxy: io, irq, dma and sgbase must be set.\n");
		return -EINVAL;
	}

	if ( probe_sgalaxy(&cfg) == 0 )
		return -ENODEV;

	return 0;
}

static void __exit cleanup_sgalaxy(void)
{
	unload_sgalaxy(&cfg);
}

module_init(init_sgalaxy);
module_exit(cleanup_sgalaxy);

#ifndef MODULE
static int __init setup_sgalaxy(char *str)
{
	/* io, irq, dma, dma2, sgbase */
	int ints[6];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	sgbase	= ints[5];

	return 1;
}

__setup("sgalaxy=", setup_sgalaxy);
#endif
MODULE_LICENSE("GPL");
