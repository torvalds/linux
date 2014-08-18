/*
 * sound/oss/pas2_card.c
 *
 * Detection routine for the Pro Audio Spectrum cards.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "pas2.h"
#include "sb.h"

static unsigned char dma_bits[] = {
	4, 1, 2, 3, 0, 5, 6, 7
};

static unsigned char irq_bits[] = {
	0, 0, 1, 2, 3, 4, 5, 6, 0, 1, 7, 8, 9, 0, 10, 11
};

static unsigned char sb_irq_bits[] = {
	0x00, 0x00, 0x08, 0x10, 0x00, 0x18, 0x00, 0x20, 
	0x00, 0x08, 0x28, 0x30, 0x38, 0, 0
};

static unsigned char sb_dma_bits[] = {
	0x00, 0x40, 0x80, 0xC0, 0, 0, 0, 0
};

/*
 * The Address Translation code is used to convert I/O register addresses to
 * be relative to the given base -register
 */

int      	pas_translate_code = 0;
static int      pas_intr_mask;
static int      pas_irq;
static int      pas_sb_base;
DEFINE_SPINLOCK(pas_lock);
#ifndef CONFIG_PAS_JOYSTICK
static bool	joystick;
#else
static bool 	joystick = 1;
#endif
#ifdef SYMPHONY_PAS
static bool 	symphony = 1;
#else
static bool 	symphony;
#endif
#ifdef BROKEN_BUS_CLOCK
static bool	broken_bus_clock = 1;
#else
static bool	broken_bus_clock;
#endif

static struct address_info cfg;
static struct address_info cfg2;

char            pas_model = 0;
static char    *pas_model_names[] = {
	"", 
	"Pro AudioSpectrum+", 
	"CDPC", 
	"Pro AudioSpectrum 16", 
	"Pro AudioSpectrum 16D"
};

/*
 * pas_read() and pas_write() are equivalents of inb and outb 
 * These routines perform the I/O address translation required
 * to support other than the default base address
 */

unsigned char pas_read(int ioaddr)
{
	return inb(ioaddr + pas_translate_code);
}

void pas_write(unsigned char data, int ioaddr)
{
	outb((data), ioaddr + pas_translate_code);
}

/******************* Begin of the Interrupt Handler ********************/

static irqreturn_t pasintr(int irq, void *dev_id)
{
	int             status;

	status = pas_read(0x0B89);
	pas_write(status, 0x0B89);	/* Clear interrupt */

	if (status & 0x08)
	{
		  pas_pcm_interrupt(status, 1);
		  status &= ~0x08;
	}
	if (status & 0x10)
	{
		  pas_midi_interrupt();
		  status &= ~0x10;
	}
	return IRQ_HANDLED;
}

int pas_set_intr(int mask)
{
	if (!mask)
		return 0;

	pas_intr_mask |= mask;

	pas_write(pas_intr_mask, 0x0B8B);
	return 0;
}

int pas_remove_intr(int mask)
{
	if (!mask)
		return 0;

	pas_intr_mask &= ~mask;
	pas_write(pas_intr_mask, 0x0B8B);

	return 0;
}

/******************* End of the Interrupt handler **********************/

/******************* Begin of the Initialization Code ******************/

static int __init config_pas_hw(struct address_info *hw_config)
{
	char            ok = 1;
	unsigned        int_ptrs;	/* scsi/sound interrupt pointers */

	pas_irq = hw_config->irq;

	pas_write(0x00, 0x0B8B);
	pas_write(0x36, 0x138B);
	pas_write(0x36, 0x1388);
	pas_write(0, 0x1388);
	pas_write(0x74, 0x138B);
	pas_write(0x74, 0x1389);
	pas_write(0, 0x1389);

	pas_write(0x80 | 0x40 | 0x20 | 1, 0x0B8A);
	pas_write(0x80 | 0x20 | 0x10 | 0x08 | 0x01, 0xF8A);
	pas_write(0x01 | 0x02 | 0x04 | 0x10	/*
						 * |
						 * 0x80
						 */ , 0xB88);

	pas_write(0x80 | (joystick ? 0x40 : 0), 0xF388);

	if (pas_irq < 0 || pas_irq > 15)
	{
		printk(KERN_ERR "PAS16: Invalid IRQ %d", pas_irq);
		hw_config->irq=-1;
		ok = 0;
	}
	else
	{
		int_ptrs = pas_read(0xF38A);
		int_ptrs = (int_ptrs & 0xf0) | irq_bits[pas_irq];
		pas_write(int_ptrs, 0xF38A);
		if (!irq_bits[pas_irq])
		{
			printk(KERN_ERR "PAS16: Invalid IRQ %d", pas_irq);
			hw_config->irq=-1;
			ok = 0;
		}
		else
		{
			if (request_irq(pas_irq, pasintr, 0, "PAS16",hw_config) < 0) {
				printk(KERN_ERR "PAS16: Cannot allocate IRQ %d\n",pas_irq);
				hw_config->irq=-1;
				ok = 0;
			}
		}
	}

	if (hw_config->dma < 0 || hw_config->dma > 7)
	{
		printk(KERN_ERR "PAS16: Invalid DMA selection %d", hw_config->dma);
		hw_config->dma=-1;
		ok = 0;
	}
	else
	{
		pas_write(dma_bits[hw_config->dma], 0xF389);
		if (!dma_bits[hw_config->dma])
		{
			printk(KERN_ERR "PAS16: Invalid DMA selection %d", hw_config->dma);
			hw_config->dma=-1;
			ok = 0;
		}
		else
		{
			if (sound_alloc_dma(hw_config->dma, "PAS16"))
			{
				printk(KERN_ERR "pas2_card.c: Can't allocate DMA channel\n");
				hw_config->dma=-1;
				ok = 0;
			}
		}
	}

	/*
	 * This fixes the timing problems of the PAS due to the Symphony chipset
	 * as per Media Vision.  Only define this if your PAS doesn't work correctly.
	 */

	if(symphony)
	{
		outb((0x05), 0xa8);
		outb((0x60), 0xa9);
	}

	if(broken_bus_clock)
		pas_write(0x01 | 0x10 | 0x20 | 0x04, 0x8388);
	else
		/*
		 * pas_write(0x01, 0x8388);
		 */
		pas_write(0x01 | 0x10 | 0x20, 0x8388);

	pas_write(0x18, 0x838A);	/* ??? */
	pas_write(0x20 | 0x01, 0x0B8A);		/* Mute off, filter = 17.897 kHz */
	pas_write(8, 0xBF8A);

	mix_write(0x80 | 5, 0x078B);
	mix_write(5, 0x078B);

	{
		struct address_info *sb_config;

		sb_config = &cfg2;
		if (sb_config->io_base)
		{
			unsigned char   irq_dma;

			/*
			 * Turn on Sound Blaster compatibility
			 * bit 1 = SB emulation
			 * bit 0 = MPU401 emulation (CDPC only :-( )
			 */
			
			pas_write(0x02, 0xF788);

			/*
			 * "Emulation address"
			 */
			
			pas_write((sb_config->io_base >> 4) & 0x0f, 0xF789);
			pas_sb_base = sb_config->io_base;

			if (!sb_dma_bits[sb_config->dma])
				printk(KERN_ERR "PAS16 Warning: Invalid SB DMA %d\n\n", sb_config->dma);

			if (!sb_irq_bits[sb_config->irq])
				printk(KERN_ERR "PAS16 Warning: Invalid SB IRQ %d\n\n", sb_config->irq);

			irq_dma = sb_dma_bits[sb_config->dma] |
				sb_irq_bits[sb_config->irq];

			pas_write(irq_dma, 0xFB8A);
		}
		else
			pas_write(0x00, 0xF788);
	}

	if (!ok)
		printk(KERN_WARNING "PAS16: Driver not enabled\n");

	return ok;
}

static int __init detect_pas_hw(struct address_info *hw_config)
{
	unsigned char   board_id, foo;

	/*
	 * WARNING: Setting an option like W:1 or so that disables warm boot reset
	 * of the card will screw up this detect code something fierce. Adding code
	 * to handle this means possibly interfering with other cards on the bus if
	 * you have something on base port 0x388. SO be forewarned.
	 */

	outb((0xBC), 0x9A01);	/* Activate first board */
	outb((hw_config->io_base >> 2), 0x9A01);	/* Set base address */
	pas_translate_code = hw_config->io_base - 0x388;
	pas_write(1, 0xBF88);	/* Select one wait states */

	board_id = pas_read(0x0B8B);

	if (board_id == 0xff)
		return 0;

	/*
	 * We probably have a PAS-series board, now check for a PAS16-series board
	 * by trying to change the board revision bits. PAS16-series hardware won't
	 * let you do this - the bits are read-only.
	 */

	foo = board_id ^ 0xe0;

	pas_write(foo, 0x0B8B);
	foo = pas_read(0x0B8B);
	pas_write(board_id, 0x0B8B);

	if (board_id != foo)
		return 0;

	pas_model = pas_read(0xFF88);

	return pas_model;
}

static void __init attach_pas_card(struct address_info *hw_config)
{
	pas_irq = hw_config->irq;

	if (detect_pas_hw(hw_config))
	{

		if ((pas_model = pas_read(0xFF88)))
		{
			char            temp[100];

			if (pas_model < 0 ||
			    pas_model >= ARRAY_SIZE(pas_model_names)) {
				printk(KERN_ERR "pas2 unrecognized model.\n");
				return;
			}
			sprintf(temp,
			    "%s rev %d", pas_model_names[(int) pas_model],
				    pas_read(0x2789));
			conf_printf(temp, hw_config);
		}
		if (config_pas_hw(hw_config))
		{
			pas_pcm_init(hw_config);
			pas_midi_init();
			pas_init_mixer();
		}
	}
}

static inline int __init probe_pas(struct address_info *hw_config)
{
	return detect_pas_hw(hw_config);
}

static void __exit unload_pas(struct address_info *hw_config)
{
	extern int pas_audiodev;
	extern int pas2_mididev;

	if (hw_config->dma>0)
		sound_free_dma(hw_config->dma);
	if (hw_config->irq>0)
		free_irq(hw_config->irq, hw_config);

	if(pas_audiodev!=-1)
		sound_unload_mixerdev(audio_devs[pas_audiodev]->mixer_dev);
	if(pas2_mididev!=-1)
	        sound_unload_mididev(pas2_mididev);
	if(pas_audiodev!=-1)
		sound_unload_audiodev(pas_audiodev);
}

static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma16	= -1;	/* Set this for modules that need it */

static int __initdata sb_io	= 0;
static int __initdata sb_irq	= -1;
static int __initdata sb_dma	= -1;
static int __initdata sb_dma16	= -1;

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(dma, int, 0);
module_param(dma16, int, 0);

module_param(sb_io, int, 0);
module_param(sb_irq, int, 0);
module_param(sb_dma, int, 0);
module_param(sb_dma16, int, 0);

module_param(joystick, bool, 0);
module_param(symphony, bool, 0);
module_param(broken_bus_clock, bool, 0);

MODULE_LICENSE("GPL");

static int __init init_pas2(void)
{
	printk(KERN_INFO "Pro Audio Spectrum driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma16;

	cfg2.io_base = sb_io;
	cfg2.irq = sb_irq;
	cfg2.dma = sb_dma;
	cfg2.dma2 = sb_dma16;

	if (cfg.io_base == -1 || cfg.dma == -1 || cfg.irq == -1) {
		printk(KERN_INFO "I/O, IRQ, DMA and type are mandatory\n");
		return -EINVAL;
	}

	if (!probe_pas(&cfg))
		return -ENODEV;
	attach_pas_card(&cfg);

	return 0;
}

static void __exit cleanup_pas2(void)
{
	unload_pas(&cfg);
}

module_init(init_pas2);
module_exit(cleanup_pas2);

#ifndef MODULE
static int __init setup_pas2(char *str)
{
	/* io, irq, dma, dma2, sb_io, sb_irq, sb_dma, sb_dma2 */
	int ints[9];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);

	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma16	= ints[4];

	sb_io	= ints[5];
	sb_irq	= ints[6];
	sb_dma	= ints[7];
	sb_dma16 = ints[8];

	return 1;
}

__setup("pas2=", setup_pas2);
#endif
