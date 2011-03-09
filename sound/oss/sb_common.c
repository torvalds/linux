/*
 * sound/oss/sb_common.c
 *
 * Common routines for Sound Blaster compatible cards.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Daniel J. Rodriksson: Modified sbintr to handle 8 and 16 bit interrupts
 *                       for full duplex support ( only sb16 by now )
 * Rolf Fokkens:	 Added (BETA?) support for ES1887 chips.
 * (fokkensr@vertis.nl)	 Which means: You can adjust the recording levels.
 *
 * 2000/01/18 - separated sb_card and sb_common -
 * Jeff Garzik <jgarzik@pobox.com>
 *
 * 2000/09/18 - got rid of attach_uart401
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 2001/01/26 - replaced CLI/STI with spinlocks
 * Chris Rankin <rankinc@zipworld.com.au>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "sound_config.h"
#include "sound_firmware.h"

#include "mpu401.h"

#include "sb_mixer.h"
#include "sb.h"
#include "sb_ess.h"

/*
 * global module flag
 */

int sb_be_quiet;

static sb_devc *detected_devc;	/* For communication from probe to init */
static sb_devc *last_devc;	/* For MPU401 initialization */

static unsigned char jazz_irq_bits[] = {
	0, 0, 2, 3, 0, 1, 0, 4, 0, 2, 5, 0, 0, 0, 0, 6
};

static unsigned char jazz_dma_bits[] = {
	0, 1, 0, 2, 0, 3, 0, 4
};

void *smw_free;

/*
 * Jazz16 chipset specific control variables
 */

static int jazz16_base;			/* Not detected */
static unsigned char jazz16_bits;	/* I/O relocation bits */
static DEFINE_SPINLOCK(jazz16_lock);

/*
 * Logitech Soundman Wave specific initialization code
 */

#ifdef SMW_MIDI0001_INCLUDED
#include "smw-midi0001.h"
#else
static unsigned char *smw_ucode;
static int      smw_ucodeLen;

#endif

static sb_devc *last_sb;		/* Last sb loaded */

int sb_dsp_command(sb_devc * devc, unsigned char val)
{
	int i;
	unsigned long limit;

	limit = jiffies + HZ / 10;	/* Timeout */
	
	/*
	 * Note! the i<500000 is an emergency exit. The sb_dsp_command() is sometimes
	 * called while interrupts are disabled. This means that the timer is
	 * disabled also. However the timeout situation is a abnormal condition.
	 * Normally the DSP should be ready to accept commands after just couple of
	 * loops.
	 */

	for (i = 0; i < 500000 && (limit-jiffies)>0; i++)
	{
		if ((inb(DSP_STATUS) & 0x80) == 0)
		{
			outb((val), DSP_COMMAND);
			return 1;
		}
	}
	printk(KERN_WARNING "Sound Blaster:  DSP command(%x) timeout.\n", val);
	return 0;
}

int sb_dsp_get_byte(sb_devc * devc)
{
	int i;

	for (i = 1000; i; i--)
	{
		if (inb(DSP_DATA_AVAIL) & 0x80)
			return inb(DSP_READ);
	}
	return 0xffff;
}

static void sb_intr (sb_devc *devc)
{
	int status;
	unsigned char   src = 0xff;

	if (devc->model == MDL_SB16)
	{
		src = sb_getmixer(devc, IRQ_STAT);	/* Interrupt source register */

		if (src & 4)						/* MPU401 interrupt */
			if(devc->midi_irq_cookie)
				uart401intr(devc->irq, devc->midi_irq_cookie);

		if (!(src & 3))
			return;	/* Not a DSP interrupt */
	}
	if (devc->intr_active && (!devc->fullduplex || (src & 0x01)))
	{
		switch (devc->irq_mode)
		{
			case IMODE_OUTPUT:
				DMAbuf_outputintr(devc->dev, 1);
				break;

			case IMODE_INPUT:
				DMAbuf_inputintr(devc->dev);
				break;

			case IMODE_INIT:
				break;

			case IMODE_MIDI:
				sb_midi_interrupt(devc);
				break;

			default:
				/* printk(KERN_WARNING "Sound Blaster: Unexpected interrupt\n"); */
				;
		}
	}
	else if (devc->intr_active_16 && (src & 0x02))
	{
		switch (devc->irq_mode_16)
		{
			case IMODE_OUTPUT:
				DMAbuf_outputintr(devc->dev, 1);
				break;

			case IMODE_INPUT:
				DMAbuf_inputintr(devc->dev);
				break;

			case IMODE_INIT:
				break;

			default:
				/* printk(KERN_WARNING "Sound Blaster: Unexpected interrupt\n"); */
				;
		}
	}
	/*
	 * Acknowledge interrupts 
	 */

	if (src & 0x01)
		status = inb(DSP_DATA_AVAIL);

	if (devc->model == MDL_SB16 && src & 0x02)
		status = inb(DSP_DATA_AVL16);
}

static void pci_intr(sb_devc *devc)
{
	int src = inb(devc->pcibase+0x1A);
	src&=3;
	if(src)
		sb_intr(devc);
}

static irqreturn_t sbintr(int irq, void *dev_id)
{
	sb_devc *devc = dev_id;

	devc->irq_ok = 1;

	switch (devc->model) {
	case MDL_ESSPCI:
		pci_intr (devc);
		break;
		
	case MDL_ESS:
		ess_intr (devc);
		break;
	default:
		sb_intr (devc);
		break;
	}
	return IRQ_HANDLED;
}

int sb_dsp_reset(sb_devc * devc)
{
	int loopc;

	DEB(printk("Entered sb_dsp_reset()\n"));

	if (devc->model == MDL_ESS) return ess_dsp_reset (devc);

	/* This is only for non-ESS chips */

	outb(1, DSP_RESET);

	udelay(10);
	outb(0, DSP_RESET);
	udelay(30);

	for (loopc = 0; loopc < 1000 && !(inb(DSP_DATA_AVAIL) & 0x80); loopc++);

	if (inb(DSP_READ) != 0xAA)
	{
		DDB(printk("sb: No response to RESET\n"));
		return 0;	/* Sorry */
	}

	DEB(printk("sb_dsp_reset() OK\n"));

	return 1;
}

static void dsp_get_vers(sb_devc * devc)
{
	int i;

	unsigned long   flags;

	DDB(printk("Entered dsp_get_vers()\n"));
	spin_lock_irqsave(&devc->lock, flags);
	devc->major = devc->minor = 0;
	sb_dsp_command(devc, 0xe1);	/* Get version */

	for (i = 100000; i; i--)
	{
		if (inb(DSP_DATA_AVAIL) & 0x80)
		{
			if (devc->major == 0)
				devc->major = inb(DSP_READ);
			else
			{
				devc->minor = inb(DSP_READ);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&devc->lock, flags);
	DDB(printk("DSP version %d.%02d\n", devc->major, devc->minor));
}

static int sb16_set_dma_hw(sb_devc * devc)
{
	int bits;

	if (devc->dma8 != 0 && devc->dma8 != 1 && devc->dma8 != 3)
	{
		printk(KERN_ERR "SB16: Invalid 8 bit DMA (%d)\n", devc->dma8);
		return 0;
	}
	bits = (1 << devc->dma8);

	if (devc->dma16 >= 5 && devc->dma16 <= 7)
		bits |= (1 << devc->dma16);

	sb_setmixer(devc, DMA_NR, bits);
	return 1;
}

static void sb16_set_mpu_port(sb_devc * devc, struct address_info *hw_config)
{
	/*
	 * This routine initializes new MIDI port setup register of SB Vibra (CT2502).
	 */
	unsigned char   bits = sb_getmixer(devc, 0x84) & ~0x06;

	switch (hw_config->io_base)
	{
		case 0x300:
			sb_setmixer(devc, 0x84, bits | 0x04);
			break;

		case 0x330:
			sb_setmixer(devc, 0x84, bits | 0x00);
			break;

		default:
			sb_setmixer(devc, 0x84, bits | 0x02);		/* Disable MPU */
			printk(KERN_ERR "SB16: Invalid MIDI I/O port %x\n", hw_config->io_base);
	}
}

static int sb16_set_irq_hw(sb_devc * devc, int level)
{
	int ival;

	switch (level)
	{
		case 5:
			ival = 2;
			break;
		case 7:
			ival = 4;
			break;
		case 9:
			ival = 1;
			break;
		case 10:
			ival = 8;
			break;
		default:
			printk(KERN_ERR "SB16: Invalid IRQ%d\n", level);
			return 0;
	}
	sb_setmixer(devc, IRQ_NR, ival);
	return 1;
}

static void relocate_Jazz16(sb_devc * devc, struct address_info *hw_config)
{
	unsigned char bits = 0;
	unsigned long flags;

	if (jazz16_base != 0 && jazz16_base != hw_config->io_base)
		return;

	switch (hw_config->io_base)
	{
		case 0x220:
			bits = 1;
			break;
		case 0x240:
			bits = 2;
			break;
		case 0x260:
			bits = 3;
			break;
		default:
			return;
	}
	bits = jazz16_bits = bits << 5;
	jazz16_base = hw_config->io_base;

	/*
	 *	Magic wake up sequence by writing to 0x201 (aka Joystick port)
	 */
	spin_lock_irqsave(&jazz16_lock, flags);
	outb((0xAF), 0x201);
	outb((0x50), 0x201);
	outb((bits), 0x201);
	spin_unlock_irqrestore(&jazz16_lock, flags);
}

static int init_Jazz16(sb_devc * devc, struct address_info *hw_config)
{
	char name[100];
	/*
	 * First try to check that the card has Jazz16 chip. It identifies itself
	 * by returning 0x12 as response to DSP command 0xfa.
	 */

	if (!sb_dsp_command(devc, 0xfa))
		return 0;

	if (sb_dsp_get_byte(devc) != 0x12)
		return 0;

	/*
	 * OK so far. Now configure the IRQ and DMA channel used by the card.
	 */
	if (hw_config->irq < 1 || hw_config->irq > 15 || jazz_irq_bits[hw_config->irq] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid interrupt (IRQ%d)\n", hw_config->irq);
		return 0;
	}
	if (hw_config->dma < 0 || hw_config->dma > 3 || jazz_dma_bits[hw_config->dma] == 0)
	{
		  printk(KERN_ERR "Jazz16: Invalid 8 bit DMA (DMA%d)\n", hw_config->dma);
		  return 0;
	}
	if (hw_config->dma2 < 0)
	{
		printk(KERN_ERR "Jazz16: No 16 bit DMA channel defined\n");
		return 0;
	}
	if (hw_config->dma2 < 5 || hw_config->dma2 > 7 || jazz_dma_bits[hw_config->dma2] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid 16 bit DMA (DMA%d)\n", hw_config->dma2);
		return 0;
	}
	devc->dma16 = hw_config->dma2;

	if (!sb_dsp_command(devc, 0xfb))
		return 0;

	if (!sb_dsp_command(devc, jazz_dma_bits[hw_config->dma] |
			(jazz_dma_bits[hw_config->dma2] << 4)))
		return 0;

	if (!sb_dsp_command(devc, jazz_irq_bits[hw_config->irq]))
		return 0;

	/*
	 * Now we have configured a standard Jazz16 device. 
	 */
	devc->model = MDL_JAZZ;
	strcpy(name, "Jazz16");

	hw_config->name = "Jazz16";
	devc->caps |= SB_NO_MIDI;
	return 1;
}

static void relocate_ess1688(sb_devc * devc)
{
	unsigned char bits;

	switch (devc->base)
	{
		case 0x220:
			bits = 0x04;
			break;
		case 0x230:
			bits = 0x05;
			break;
		case 0x240:
			bits = 0x06;
			break;
		case 0x250:
			bits = 0x07;
			break;
		default:
			return;	/* Wrong port */
	}

	DDB(printk("Doing ESS1688 address selection\n"));
	
	/*
	 * ES1688 supports two alternative ways for software address config.
	 * First try the so called Read-Sequence-Key method.
	 */

	/* Reset the sequence logic */
	inb(0x229);
	inb(0x229);
	inb(0x229);

	/* Perform the read sequence */
	inb(0x22b);
	inb(0x229);
	inb(0x22b);
	inb(0x229);
	inb(0x229);
	inb(0x22b);
	inb(0x229);

	/* Select the base address by reading from it. Then probe using the port. */
	inb(devc->base);
	if (sb_dsp_reset(devc))	/* Bingo */
		return;

#if 0				/* This causes system lockups (Nokia 386/25 at least) */
	/*
	 * The last resort is the system control register method.
	 */

	outb((0x00), 0xfb);	/* 0xFB is the unlock register */
	outb((0x00), 0xe0);	/* Select index 0 */
	outb((bits), 0xe1);	/* Write the config bits */
	outb((0x00), 0xf9);	/* 0xFB is the lock register */
#endif
}

int sb_dsp_detect(struct address_info *hw_config, int pci, int pciio, struct sb_module_options *sbmo)
{
	sb_devc sb_info;
	sb_devc *devc = &sb_info;

	memset((char *) &sb_info, 0, sizeof(sb_info));	/* Zero everything */

	/* Copy module options in place */
	if(sbmo) memcpy(&devc->sbmo, sbmo, sizeof(struct sb_module_options));

	sb_info.my_mididev = -1;
	sb_info.my_mixerdev = -1;
	sb_info.dev = -1;

	/*
	 * Initialize variables 
	 */
	
	DDB(printk("sb_dsp_detect(%x) entered\n", hw_config->io_base));

	spin_lock_init(&devc->lock);
	devc->type = hw_config->card_subtype;

	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->dma8 = hw_config->dma;

	devc->dma16 = -1;
	devc->pcibase = pciio;
	
	if(pci == SB_PCI_ESSMAESTRO)
	{
		devc->model = MDL_ESSPCI;
		devc->caps |= SB_PCI_IRQ;
		hw_config->driver_use_1 |= SB_PCI_IRQ;
		hw_config->card_subtype	= MDL_ESSPCI;
	}
	
	if(pci == SB_PCI_YAMAHA)
	{
		devc->model = MDL_YMPCI;
		devc->caps |= SB_PCI_IRQ;
		hw_config->driver_use_1 |= SB_PCI_IRQ;
		hw_config->card_subtype	= MDL_YMPCI;
		
		printk("Yamaha PCI mode.\n");
	}
	
	if (devc->sbmo.acer)
	{
		unsigned long flags;

		spin_lock_irqsave(&devc->lock, flags);
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x00);
		spin_unlock_irqrestore(&devc->lock, flags);
	}
	/*
	 * Detect the device
	 */

	if (sb_dsp_reset(devc))
		dsp_get_vers(devc);
	else
		devc->major = 0;

	if (devc->type == 0 || devc->type == MDL_JAZZ || devc->type == MDL_SMW)
		if (devc->major == 0 || (devc->major == 3 && devc->minor == 1))
			relocate_Jazz16(devc, hw_config);

	if (devc->major == 0 && (devc->type == MDL_ESS || devc->type == 0))
		relocate_ess1688(devc);

	if (!sb_dsp_reset(devc))
	{
		DDB(printk("SB reset failed\n"));
#ifdef MODULE
		printk(KERN_INFO "sb: dsp reset failed.\n");
#endif
		return 0;
	}
	if (devc->major == 0)
		dsp_get_vers(devc);

	if (devc->major == 3 && devc->minor == 1)
	{
		if (devc->type == MDL_AZTECH)		/* SG Washington? */
		{
			if (sb_dsp_command(devc, 0x09))
				if (sb_dsp_command(devc, 0x00))	/* Enter WSS mode */
				{
					int i;

					/* Have some delay */
					for (i = 0; i < 10000; i++)
						inb(DSP_DATA_AVAIL);
					devc->caps = SB_NO_AUDIO | SB_NO_MIDI;	/* Mixer only */
					devc->model = MDL_AZTECH;
				}
		}
	}
	
	if(devc->type == MDL_ESSPCI)
		devc->model = MDL_ESSPCI;
		
	if(devc->type == MDL_YMPCI)
	{
		printk("YMPCI selected\n");
		devc->model = MDL_YMPCI;
	}
		
	/*
	 * Save device information for sb_dsp_init()
	 */


	detected_devc = kmalloc(sizeof(sb_devc), GFP_KERNEL);
	if (detected_devc == NULL)
	{
		printk(KERN_ERR "sb: Can't allocate memory for device information\n");
		return 0;
	}
	memcpy(detected_devc, devc, sizeof(sb_devc));
	MDB(printk(KERN_INFO "SB %d.%02d detected OK (%x)\n", devc->major, devc->minor, hw_config->io_base));
	return 1;
}

int sb_dsp_init(struct address_info *hw_config, struct module *owner)
{
	sb_devc *devc;
	char name[100];
	extern int sb_be_quiet;
	int	mixer22, mixer30;
	
/*
 * Check if we had detected a SB device earlier
 */
	DDB(printk("sb_dsp_init(%x) entered\n", hw_config->io_base));
	name[0] = 0;

	if (detected_devc == NULL)
	{
		MDB(printk("No detected device\n"));
		return 0;
	}
	devc = detected_devc;
	detected_devc = NULL;

	if (devc->base != hw_config->io_base)
	{
		DDB(printk("I/O port mismatch\n"));
		release_region(devc->base, 16);
		return 0;
	}
	/*
	 * Now continue initialization of the device
	 */

	devc->caps = hw_config->driver_use_1;

	if (!((devc->caps & SB_NO_AUDIO) && (devc->caps & SB_NO_MIDI)) && hw_config->irq > 0)
	{			/* IRQ setup */
		
		/*
		 *	ESS PCI cards do shared PCI IRQ stuff. Since they
		 *	will get shared PCI irq lines we must cope.
		 */
		 
		int i=(devc->caps&SB_PCI_IRQ)?IRQF_SHARED:0;
		
		if (request_irq(hw_config->irq, sbintr, i, "soundblaster", devc) < 0)
		{
			printk(KERN_ERR "SB: Can't allocate IRQ%d\n", hw_config->irq);
			release_region(devc->base, 16);
			return 0;
		}
		devc->irq_ok = 0;

		if (devc->major == 4)
			if (!sb16_set_irq_hw(devc, devc->irq))	/* Unsupported IRQ */
			{
				free_irq(devc->irq, devc);
				release_region(devc->base, 16);
				return 0;
			}
		if ((devc->type == 0 || devc->type == MDL_ESS) &&
			devc->major == 3 && devc->minor == 1)
		{		/* Handle various chipsets which claim they are SB Pro compatible */
			if ((devc->type != 0 && devc->type != MDL_ESS) ||
				!ess_init(devc, hw_config))
			{
				if ((devc->type != 0 && devc->type != MDL_JAZZ &&
					 devc->type != MDL_SMW) || !init_Jazz16(devc, hw_config))
				{
					DDB(printk("This is a genuine SB Pro\n"));
				}
			}
		}
		if (devc->major == 4 && devc->minor <= 11 )	/* Won't work */
			devc->irq_ok = 1;
		else
		{
			int n;

			for (n = 0; n < 3 && devc->irq_ok == 0; n++)
			{
				if (sb_dsp_command(devc, 0xf2))	/* Cause interrupt immediately */
				{
					int i;

					for (i = 0; !devc->irq_ok && i < 10000; i++);
				}
			}
			if (!devc->irq_ok)
				printk(KERN_WARNING "sb: Interrupt test on IRQ%d failed - Probable IRQ conflict\n", devc->irq);
			else
			{
				DDB(printk("IRQ test OK (IRQ%d)\n", devc->irq));
			}
		}
	}			/* IRQ setup */

	last_sb = devc;
	
	switch (devc->major)
	{
		case 1:		/* SB 1.0 or 1.5 */
			devc->model = hw_config->card_subtype = MDL_SB1;
			break;

		case 2:		/* SB 2.x */
			if (devc->minor == 0)
				devc->model = hw_config->card_subtype = MDL_SB2;
			else
				devc->model = hw_config->card_subtype = MDL_SB201;
			break;

		case 3:		/* SB Pro and most clones */
			switch (devc->model) {
			case 0:
				devc->model = hw_config->card_subtype = MDL_SBPRO;
				if (hw_config->name == NULL)
					hw_config->name = "Sound Blaster Pro (8 BIT ONLY)";
				break;
			case MDL_ESS:
				ess_dsp_init(devc, hw_config);
				break;
			}
			break;

		case 4:
			devc->model = hw_config->card_subtype = MDL_SB16;
			/* 
			 * ALS007 and ALS100 return DSP version 4.2 and have 2 post-reset !=0
			 * registers at 0x3c and 0x4c (output ctrl registers on ALS007) whereas
			 * a "standard" SB16 doesn't have a register at 0x4c.  ALS100 actively
			 * updates register 0x22 whenever 0x30 changes, as per the SB16 spec.
			 * Since ALS007 doesn't, this can be used to differentiate the 2 cards.
			 */
			if ((devc->minor == 2) && sb_getmixer(devc,0x3c) && sb_getmixer(devc,0x4c)) 
			{
				mixer30 = sb_getmixer(devc,0x30);
				sb_setmixer(devc,0x22,(mixer22=sb_getmixer(devc,0x22)) & 0x0f);
				sb_setmixer(devc,0x30,0xff);
				/* ALS100 will force 0x30 to 0xf8 like SB16; ALS007 will allow 0xff. */
				/* Register 0x22 & 0xf0 on ALS100 == 0xf0; on ALS007 it == 0x10.     */
				if ((sb_getmixer(devc,0x30) != 0xff) || ((sb_getmixer(devc,0x22) & 0xf0) != 0x10)) 
				{
					devc->submodel = SUBMDL_ALS100;
					if (hw_config->name == NULL)
						hw_config->name = "Sound Blaster 16 (ALS-100)";
        			}
        			else
        			{
        				sb_setmixer(devc,0x3c,0x1f);    /* Enable all inputs */
					sb_setmixer(devc,0x4c,0x1f);
					sb_setmixer(devc,0x22,mixer22); /* Restore 0x22 to original value */
					devc->submodel = SUBMDL_ALS007;
					if (hw_config->name == NULL)
						hw_config->name = "Sound Blaster 16 (ALS-007)";
				}
				sb_setmixer(devc,0x30,mixer30);
			}
			else if (hw_config->name == NULL)
				hw_config->name = "Sound Blaster 16";

			if (hw_config->dma2 == -1)
				devc->dma16 = devc->dma8;
			else if (hw_config->dma2 < 5 || hw_config->dma2 > 7)
			{
				printk(KERN_WARNING  "SB16: Bad or missing 16 bit DMA channel\n");
				devc->dma16 = devc->dma8;
			}
			else
				devc->dma16 = hw_config->dma2;

			if(!sb16_set_dma_hw(devc)) {
				free_irq(devc->irq, devc);
			        release_region(hw_config->io_base, 16);
				return 0;
			}

			devc->caps |= SB_NO_MIDI;
	}

	if (!(devc->caps & SB_NO_MIXER))
		if (devc->major == 3 || devc->major == 4)
			sb_mixer_init(devc, owner);

	if (!(devc->caps & SB_NO_MIDI))
		sb_dsp_midi_init(devc, owner);

	if (hw_config->name == NULL)
		hw_config->name = "Sound Blaster (8 BIT/MONO ONLY)";

	sprintf(name, "%s (%d.%02d)", hw_config->name, devc->major, devc->minor);
	conf_printf(name, hw_config);

	/*
	 * Assuming that a sound card is Sound Blaster (compatible) is the most common
	 * configuration error and the mother of all problems. Usually sound cards
	 * emulate SB Pro but in addition they have a 16 bit native mode which should be
	 * used in Unix. See Readme.cards for more information about configuring OSS/Free
	 * properly.
	 */
	if (devc->model <= MDL_SBPRO)
	{
		if (devc->major == 3 && devc->minor != 1)	/* "True" SB Pro should have v3.1 (rare ones may have 3.2). */
		{
			printk(KERN_INFO "This sound card may not be fully Sound Blaster Pro compatible.\n");
			printk(KERN_INFO "In many cases there is another way to configure OSS so that\n");
			printk(KERN_INFO "it works properly with OSS (for example in 16 bit mode).\n");
			printk(KERN_INFO "Please ignore this message if you _really_ have a SB Pro.\n");
		}
		else if (!sb_be_quiet && devc->model == MDL_SBPRO)
		{
			printk(KERN_INFO "SB DSP version is just %d.%02d which means that your card is\n", devc->major, devc->minor);
			printk(KERN_INFO "several years old (8 bit only device) or alternatively the sound driver\n");
			printk(KERN_INFO "is incorrectly configured.\n");
		}
	}
	hw_config->card_subtype = devc->model;
	hw_config->slots[0]=devc->dev;
	last_devc = devc;	/* For SB MPU detection */

	if (!(devc->caps & SB_NO_AUDIO) && devc->dma8 >= 0)
	{
		if (sound_alloc_dma(devc->dma8, "SoundBlaster8"))
		{
			printk(KERN_WARNING "Sound Blaster: Can't allocate 8 bit DMA channel %d\n", devc->dma8);
		}
		if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
		{
			if (sound_alloc_dma(devc->dma16, "SoundBlaster16"))
				printk(KERN_WARNING "Sound Blaster:  can't allocate 16 bit DMA channel %d.\n", devc->dma16);
		}
		sb_audio_init(devc, name, owner);
		hw_config->slots[0]=devc->dev;
	}
	else
	{
		MDB(printk("Sound Blaster:  no audio devices found.\n"));
	}
	return 1;
}

/* if (sbmpu) below we allow mpu401 to manage the midi devs
   otherwise we have to unload them. (Andrzej Krzysztofowicz) */
   
void sb_dsp_unload(struct address_info *hw_config, int sbmpu)
{
	sb_devc *devc;

	devc = audio_devs[hw_config->slots[0]]->devc;

	if (devc && devc->base == hw_config->io_base)
	{
		if ((devc->model & MDL_ESS) && devc->pcibase)
			release_region(devc->pcibase, 8);

		release_region(devc->base, 16);

		if (!(devc->caps & SB_NO_AUDIO))
		{
			sound_free_dma(devc->dma8);
			if (devc->dma16 >= 0)
				sound_free_dma(devc->dma16);
		}
		if (!(devc->caps & SB_NO_AUDIO && devc->caps & SB_NO_MIDI))
		{
			if (devc->irq > 0)
				free_irq(devc->irq, devc);

			sb_mixer_unload(devc);
			/* We don't have to do this bit any more the UART401 is its own
				master  -- Krzysztof Halasa */
			/* But we have to do it, if UART401 is not detected */
			if (!sbmpu)
				sound_unload_mididev(devc->my_mididev);
			sound_unload_audiodev(devc->dev);
		}
		kfree(devc);
	}
	else
		release_region(hw_config->io_base, 16);

	kfree(detected_devc);
}

/*
 *	Mixer access routines
 *
 *	ES1887 modifications: some mixer registers reside in the
 *	range above 0xa0. These must be accessed in another way.
 */

void sb_setmixer(sb_devc * devc, unsigned int port, unsigned int value)
{
	unsigned long flags;

	if (devc->model == MDL_ESS) {
		ess_setmixer (devc, port, value);
		return;
	}

	spin_lock_irqsave(&devc->lock, flags);

	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);
	udelay(20);
	outb(((unsigned char) (value & 0xff)), MIXER_DATA);
	udelay(20);

	spin_unlock_irqrestore(&devc->lock, flags);
}

unsigned int sb_getmixer(sb_devc * devc, unsigned int port)
{
	unsigned int val;
	unsigned long flags;

	if (devc->model == MDL_ESS) return ess_getmixer (devc, port);

	spin_lock_irqsave(&devc->lock, flags);

	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);
	udelay(20);
	val = inb(MIXER_DATA);
	udelay(20);

	spin_unlock_irqrestore(&devc->lock, flags);

	return val;
}

void sb_chgmixer
	(sb_devc * devc, unsigned int reg, unsigned int mask, unsigned int val)
{
	int value;

	value = sb_getmixer(devc, reg);
	value = (value & ~mask) | (val & mask);
	sb_setmixer(devc, reg, value);
}

/*
 *	MPU401 MIDI initialization.
 */

static void smw_putmem(sb_devc * devc, int base, int addr, unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&jazz16_lock, flags);  /* NOT the SB card? */

	outb((addr & 0xff), base + 1);	/* Low address bits */
	outb((addr >> 8), base + 2);	/* High address bits */
	outb((val), base);	/* Data */

	spin_unlock_irqrestore(&jazz16_lock, flags);
}

static unsigned char smw_getmem(sb_devc * devc, int base, int addr)
{
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&jazz16_lock, flags);  /* NOT the SB card? */

	outb((addr & 0xff), base + 1);	/* Low address bits */
	outb((addr >> 8), base + 2);	/* High address bits */
	val = inb(base);	/* Data */

	spin_unlock_irqrestore(&jazz16_lock, flags);
	return val;
}

static int smw_midi_init(sb_devc * devc, struct address_info *hw_config)
{
	int mpu_base = hw_config->io_base;
	int mp_base = mpu_base + 4;		/* Microcontroller base */
	int i;
	unsigned char control;


	/*
	 *  Reset the microcontroller so that the RAM can be accessed
	 */

	control = inb(mpu_base + 7);
	outb((control | 3), mpu_base + 7);	/* Set last two bits to 1 (?) */
	outb(((control & 0xfe) | 2), mpu_base + 7);	/* xxxxxxx0 resets the mc */

	mdelay(3);	/* Wait at least 1ms */

	outb((control & 0xfc), mpu_base + 7);	/* xxxxxx00 enables RAM */

	/*
	 *  Detect microcontroller by probing the 8k RAM area
	 */
	smw_putmem(devc, mp_base, 0, 0x00);
	smw_putmem(devc, mp_base, 1, 0xff);
	udelay(10);

	if (smw_getmem(devc, mp_base, 0) != 0x00 || smw_getmem(devc, mp_base, 1) != 0xff)
	{
		DDB(printk("SM Wave: No microcontroller RAM detected (%02x, %02x)\n", smw_getmem(devc, mp_base, 0), smw_getmem(devc, mp_base, 1)));
		return 0;	/* No RAM */
	}
	/*
	 *  There is RAM so assume it's really a SM Wave
	 */

	devc->model = MDL_SMW;
	smw_mixer_init(devc);

#ifdef MODULE
	if (!smw_ucode)
	{
		smw_ucodeLen = mod_firmware_load("/etc/sound/midi0001.bin", (void *) &smw_ucode);
		smw_free = smw_ucode;
	}
#endif
	if (smw_ucodeLen > 0)
	{
		if (smw_ucodeLen != 8192)
		{
			printk(KERN_ERR "SM Wave: Invalid microcode (MIDI0001.BIN) length\n");
			return 1;
		}
		/*
		 *  Download microcode
		 */

		for (i = 0; i < 8192; i++)
			smw_putmem(devc, mp_base, i, smw_ucode[i]);

		/*
		 *  Verify microcode
		 */

		for (i = 0; i < 8192; i++)
			if (smw_getmem(devc, mp_base, i) != smw_ucode[i])
			{
				printk(KERN_ERR "SM Wave: Microcode verification failed\n");
				return 0;
			}
	}
	control = 0;
#ifdef SMW_SCSI_IRQ
	/*
	 * Set the SCSI interrupt (IRQ2/9, IRQ3 or IRQ10). The SCSI interrupt
	 * is disabled by default.
	 *
	 * FIXME - make this a module option
	 *
	 * BTW the Zilog 5380 SCSI controller is located at MPU base + 0x10.
	 */
	{
		static unsigned char scsi_irq_bits[] = {
			0, 0, 3, 1, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0
		};
		control |= scsi_irq_bits[SMW_SCSI_IRQ] << 6;
	}
#endif

#ifdef SMW_OPL4_ENABLE
	/*
	 *  Make the OPL4 chip visible on the PC bus at 0x380.
	 *
	 *  There is no need to enable this feature since this driver
	 *  doesn't support OPL4 yet. Also there is no RAM in SM Wave so
	 *  enabling OPL4 is pretty useless.
	 */
	control |= 0x10;	/* Uses IRQ12 if bit 0x20 == 0 */
	/* control |= 0x20;      Uncomment this if you want to use IRQ7 */
#endif
	outb((control | 0x03), mpu_base + 7);	/* xxxxxx11 restarts */
	hw_config->name = "SoundMan Wave";
	return 1;
}

static int init_Jazz16_midi(sb_devc * devc, struct address_info *hw_config)
{
	int mpu_base = hw_config->io_base;
	int sb_base = devc->base;
	int irq = hw_config->irq;

	unsigned char bits = 0;
	unsigned long flags;

	if (irq < 0)
		irq *= -1;

	if (irq < 1 || irq > 15 ||
	    jazz_irq_bits[irq] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid MIDI interrupt (IRQ%d)\n", irq);
		return 0;
	}
	switch (sb_base)
	{
		case 0x220:
			bits = 1;
			break;
		case 0x240:
			bits = 2;
			break;
		case 0x260:
			bits = 3;
			break;
		default:
			return 0;
	}
	bits = jazz16_bits = bits << 5;
	switch (mpu_base)
	{
		case 0x310:
			bits |= 1;
			break;
		case 0x320:
			bits |= 2;
			break;
		case 0x330:
			bits |= 3;
			break;
		default:
			printk(KERN_ERR "Jazz16: Invalid MIDI I/O port %x\n", mpu_base);
			return 0;
	}
	/*
	 *	Magic wake up sequence by writing to 0x201 (aka Joystick port)
	 */
	spin_lock_irqsave(&jazz16_lock, flags);
	outb(0xAF, 0x201);
	outb(0x50, 0x201);
	outb(bits, 0x201);
	spin_unlock_irqrestore(&jazz16_lock, flags);

	hw_config->name = "Jazz16";
	smw_midi_init(devc, hw_config);

	if (!sb_dsp_command(devc, 0xfb))
		return 0;

	if (!sb_dsp_command(devc, jazz_dma_bits[devc->dma8] |
			    (jazz_dma_bits[devc->dma16] << 4)))
		return 0;

	if (!sb_dsp_command(devc, jazz_irq_bits[devc->irq] |
			    (jazz_irq_bits[irq] << 4)))
		return 0;

	return 1;
}

int probe_sbmpu(struct address_info *hw_config, struct module *owner)
{
	sb_devc *devc = last_devc;
	int ret;

	if (last_devc == NULL)
		return 0;

	last_devc = NULL;

	if (hw_config->io_base <= 0)
	{
		/* The real vibra16 is fine about this, but we have to go
		   wipe up after Cyrix again */
		   	   
		if(devc->model == MDL_SB16 && devc->minor >= 12)
		{
			unsigned char   bits = sb_getmixer(devc, 0x84) & ~0x06;
			sb_setmixer(devc, 0x84, bits | 0x02);		/* Disable MPU */
		}
		return 0;
	}

#if defined(CONFIG_SOUND_MPU401)
	if (devc->model == MDL_ESS)
	{
		struct resource *ports;
		ports = request_region(hw_config->io_base, 2, "mpu401");
		if (!ports) {
			printk(KERN_ERR "sbmpu: I/O port conflict (%x)\n", hw_config->io_base);
			return 0;
		}
		if (!ess_midi_init(devc, hw_config)) {
			release_region(hw_config->io_base, 2);
			return 0;
		}
		hw_config->name = "ESS1xxx MPU";
		devc->midi_irq_cookie = NULL;
		if (!probe_mpu401(hw_config, ports)) {
			release_region(hw_config->io_base, 2);
			return 0;
		}
		attach_mpu401(hw_config, owner);
		if (last_sb->irq == -hw_config->irq)
			last_sb->midi_irq_cookie =
				(void *)(long) hw_config->slots[1];
		return 1;
	}
#endif

	switch (devc->model)
	{
		case MDL_SB16:
			if (hw_config->io_base != 0x300 && hw_config->io_base != 0x330)
			{
				printk(KERN_ERR "SB16: Invalid MIDI port %x\n", hw_config->io_base);
				return 0;
			}
			hw_config->name = "Sound Blaster 16";
			if (hw_config->irq < 3 || hw_config->irq == devc->irq)
				hw_config->irq = -devc->irq;
			if (devc->minor > 12)		/* What is Vibra's version??? */
				sb16_set_mpu_port(devc, hw_config);
			break;

		case MDL_JAZZ:
			if (hw_config->irq < 3 || hw_config->irq == devc->irq)
				hw_config->irq = -devc->irq;
			if (!init_Jazz16_midi(devc, hw_config))
				return 0;
			break;

		case MDL_YMPCI:
			hw_config->name = "Yamaha PCI Legacy";
			printk("Yamaha PCI legacy UART401 check.\n");
			break;
		default:
			return 0;
	}
	
	ret = probe_uart401(hw_config, owner);
	if (ret)
		last_sb->midi_irq_cookie=midi_devs[hw_config->slots[4]]->devc;
	return ret;
}

void unload_sbmpu(struct address_info *hw_config)
{
#if defined(CONFIG_SOUND_MPU401)
	if (!strcmp (hw_config->name, "ESS1xxx MPU")) {
		unload_mpu401(hw_config);
		return;
	}
#endif
	unload_uart401(hw_config);
}

EXPORT_SYMBOL(sb_dsp_init);
EXPORT_SYMBOL(sb_dsp_detect);
EXPORT_SYMBOL(sb_dsp_unload);
EXPORT_SYMBOL(sb_be_quiet);
EXPORT_SYMBOL(probe_sbmpu);
EXPORT_SYMBOL(unload_sbmpu);
EXPORT_SYMBOL(smw_free);
MODULE_LICENSE("GPL");
