/*
 * sound/gus_card.c
 *
 * Detection routine for the Gravis Ultrasound.
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 *
 * Frank van de Pol : Fixed GUS MAX interrupt handling, enabled simultanious
 *                    usage of CS4231A codec, GUS wave and MIDI for GUS MAX.
 * Christoph Hellwig: Adapted to module_init/module_exit, simple cleanups.
 *
 * Status:
 *              Tested... 
 */
      
 
#include <linux/config.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include "sound_config.h"

#include "gus.h"
#include "gus_hw.h"

irqreturn_t gusintr(int irq, void *dev_id, struct pt_regs *dummy);

int             gus_base = 0, gus_irq = 0, gus_dma = 0;
int             gus_no_wave_dma = 0; 
extern int      gus_wave_volume;
extern int      gus_pcm_volume;
extern int      have_gus_max;
int             gus_pnp_flag = 0;
#ifdef CONFIG_SOUND_GUS16
static int      db16;	/* Has a Gus16 AD1848 on it */
#endif

static void __init attach_gus(struct address_info *hw_config)
{
	gus_wave_init(hw_config);

	if (sound_alloc_dma(hw_config->dma, "GUS"))
		printk(KERN_ERR "gus_card.c: Can't allocate DMA channel %d\n", hw_config->dma);
	if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
		if (sound_alloc_dma(hw_config->dma2, "GUS(2)"))
			printk(KERN_ERR "gus_card.c: Can't allocate DMA channel %d\n", hw_config->dma2);
	gus_midi_init(hw_config);
	if(request_irq(hw_config->irq, gusintr, 0,  "Gravis Ultrasound", hw_config)<0)
		printk(KERN_ERR "gus_card.c: Unable to allocate IRQ %d\n", hw_config->irq);

	return;
}

static int __init probe_gus(struct address_info *hw_config)
{
	int             irq;
	int             io_addr;

	if (hw_config->card_subtype == 1)
		gus_pnp_flag = 1;

	irq = hw_config->irq;

	if (hw_config->card_subtype == 0)	/* GUS/MAX/ACE */
		if (irq != 3 && irq != 5 && irq != 7 && irq != 9 &&
		    irq != 11 && irq != 12 && irq != 15)
		  {
			  printk(KERN_ERR "GUS: Unsupported IRQ %d\n", irq);
			  return 0;
		  }
	if (gus_wave_detect(hw_config->io_base))
		return 1;

#ifndef EXCLUDE_GUS_IODETECT

	/*
	 * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
	 */

	for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10) {
		if (io_addr == hw_config->io_base)	/* Already tested */
			continue;
		if (gus_wave_detect(io_addr)) {
			hw_config->io_base = io_addr;
			return 1;
		}
	}
#endif

	printk("NO GUS card found !\n");
	return 0;
}

static void __exit unload_gus(struct address_info *hw_config)
{
	DDB(printk("unload_gus(%x)\n", hw_config->io_base));

	gus_wave_unload(hw_config);

	release_region(hw_config->io_base, 16);
	release_region(hw_config->io_base + 0x100, 12);		/* 0x10c-> is MAX */
	free_irq(hw_config->irq, hw_config);

	sound_free_dma(hw_config->dma);

	if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
		sound_free_dma(hw_config->dma2);
}

irqreturn_t gusintr(int irq, void *dev_id, struct pt_regs *dummy)
{
	unsigned char src;
	extern int gus_timer_enabled;
	int handled = 0;

#ifdef CONFIG_SOUND_GUSMAX
	if (have_gus_max) {
		struct address_info *hw_config = dev_id;
		adintr(irq, (void *)hw_config->slots[1], NULL);
	}
#endif
#ifdef CONFIG_SOUND_GUS16
	if (db16) {
		struct address_info *hw_config = dev_id;
		adintr(irq, (void *)hw_config->slots[3], NULL);
	}
#endif

	while (1)
	{
		if (!(src = inb(u_IrqStatus)))
			break;
		handled = 1;
		if (src & DMA_TC_IRQ)
		{
			guswave_dma_irq();
		}
		if (src & (MIDI_TX_IRQ | MIDI_RX_IRQ))
		{
			gus_midi_interrupt(0);
		}
		if (src & (GF1_TIMER1_IRQ | GF1_TIMER2_IRQ))
		{
			if (gus_timer_enabled)
				sound_timer_interrupt();
			gus_write8(0x45, 0);	/* Ack IRQ */
			gus_timer_command(4, 0x80);		/* Reset IRQ flags */
		}
		if (src & (WAVETABLE_IRQ | ENVELOPE_IRQ))
			gus_voice_irq();
	}
	return IRQ_RETVAL(handled);
}

/*
 *	Some extra code for the 16 bit sampling option
 */

#ifdef CONFIG_SOUND_GUS16

static int __init init_gus_db16(struct address_info *hw_config)
{
	struct resource *ports;

	ports = request_region(hw_config->io_base, 4, "ad1848");
	if (!ports)
		return 0;

	if (!ad1848_detect(ports, NULL, hw_config->osp)) {
		release_region(hw_config->io_base, 4);
		return 0;
	}

	gus_pcm_volume = 100;
	gus_wave_volume = 90;

	hw_config->slots[3] = ad1848_init("GUS 16 bit sampling", ports,
					  hw_config->irq,
					  hw_config->dma,
					  hw_config->dma, 0,
					  hw_config->osp,
					  THIS_MODULE);
	return 1;
}

static void __exit unload_gus_db16(struct address_info *hw_config)
{

	ad1848_unload(hw_config->io_base,
		      hw_config->irq,
		      hw_config->dma,
		      hw_config->dma, 0);
	sound_unload_audiodev(hw_config->slots[3]);
}
#endif

#ifdef CONFIG_SOUND_GUS16
static int gus16;
#endif
#ifdef CONFIG_SOUND_GUSMAX
static int no_wave_dma;   /* Set if no dma is to be used for the
                                   wave table (GF1 chip) */
#endif


/*
 *    Note DMA2 of -1 has the right meaning in the GUS driver as well
 *      as here. 
 */

static struct address_info cfg;

static int __initdata io = -1;
static int __initdata irq = -1;
static int __initdata dma = -1;
static int __initdata dma16 = -1;	/* Set this for modules that need it */
static int __initdata type = 0;		/* 1 for PnP */

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(dma, int, 0);
module_param(dma16, int, 0);
module_param(type, int, 0);
#ifdef CONFIG_SOUND_GUSMAX
module_param(no_wave_dma, int, 0);
#endif
#ifdef CONFIG_SOUND_GUS16
module_param(db16, int, 0);
module_param(gus16, int, 0);
#endif
MODULE_LICENSE("GPL");

static int __init init_gus(void)
{
	printk(KERN_INFO "Gravis Ultrasound audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma16;
	cfg.card_subtype = type;
#ifdef CONFIG_SOUND_GUSMAX
	gus_no_wave_dma = no_wave_dma;
#endif

	if (cfg.io_base == -1 || cfg.dma == -1 || cfg.irq == -1) {
		printk(KERN_ERR "I/O, IRQ, and DMA are mandatory\n");
		return -EINVAL;
	}

#ifdef CONFIG_SOUND_GUS16
	if (gus16 && init_gus_db16(&cfg))
		db16 = 1;
#endif
	if (!probe_gus(&cfg))
		return -ENODEV;
	attach_gus(&cfg);

	return 0;
}

static void __exit cleanup_gus(void)
{
#ifdef CONFIG_SOUND_GUS16
	if (db16)
		unload_gus_db16(&cfg);
#endif
	unload_gus(&cfg);
}

module_init(init_gus);
module_exit(cleanup_gus);

#ifndef MODULE
static int __init setup_gus(char *str)
{
	/* io, irq, dma, dma2 */
	int ints[5];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma16	= ints[4];

	return 1;
}

__setup("gus=", setup_gus);
#endif
