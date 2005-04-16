/*
 * sound/gus2_midi.c
 *
 * The low level driver for the GUS Midi Interface.
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
 *		Added __init to gus_midi_init()
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "gus.h"
#include "gus_hw.h"

static int      midi_busy, input_opened;
static int      my_dev;
static int      output_used;
static volatile unsigned char gus_midi_control;
static void     (*midi_input_intr) (int dev, unsigned char data);

static unsigned char tmp_queue[256];
extern int      gus_pnp_flag;
static volatile int qlen;
static volatile unsigned char qhead, qtail;
extern int      gus_base, gus_irq, gus_dma;
extern int     *gus_osp;
extern spinlock_t gus_lock;

static int GUS_MIDI_STATUS(void)
{
	return inb(u_MidiStatus);
}

static int gus_midi_open(int dev, int mode, void (*input) (int dev, unsigned char data), void (*output) (int dev))
{
	if (midi_busy)
	{
/*		printk("GUS: Midi busy\n");*/
		return -EBUSY;
	}
	outb((MIDI_RESET), u_MidiControl);
	gus_delay();

	gus_midi_control = 0;
	input_opened = 0;

	if (mode == OPEN_READ || mode == OPEN_READWRITE)
		if (!gus_pnp_flag)
		{
			gus_midi_control |= MIDI_ENABLE_RCV;
			input_opened = 1;
		}
	outb((gus_midi_control), u_MidiControl);	/* Enable */

	midi_busy = 1;
	qlen = qhead = qtail = output_used = 0;
	midi_input_intr = input;

	return 0;
}

static int dump_to_midi(unsigned char midi_byte)
{
	unsigned long   flags;
	int             ok = 0;

	output_used = 1;

	spin_lock_irqsave(&gus_lock, flags);

	if (GUS_MIDI_STATUS() & MIDI_XMIT_EMPTY)
	{
		ok = 1;
		outb((midi_byte), u_MidiData);
	}
	else
	{
		/*
		 * Enable Midi xmit interrupts (again)
		 */
		gus_midi_control |= MIDI_ENABLE_XMIT;
		outb((gus_midi_control), u_MidiControl);
	}

	spin_unlock_irqrestore(&gus_lock,flags);
	return ok;
}

static void gus_midi_close(int dev)
{
	/*
	 * Reset FIFO pointers, disable intrs
	 */

	outb((MIDI_RESET), u_MidiControl);
	midi_busy = 0;
}

static int gus_midi_out(int dev, unsigned char midi_byte)
{
	unsigned long   flags;

	/*
	 * Drain the local queue first
	 */
	spin_lock_irqsave(&gus_lock, flags);

	while (qlen && dump_to_midi(tmp_queue[qhead]))
	{
		qlen--;
		qhead++;
	}
	spin_unlock_irqrestore(&gus_lock,flags);

	/*
	 *	Output the byte if the local queue is empty.
	 */

	if (!qlen)
		if (dump_to_midi(midi_byte))
			return 1;	/*
					 * OK
					 */

	/*
	 *	Put to the local queue
	 */

	if (qlen >= 256)
		return 0;	/*
				 * Local queue full
				 */
	spin_lock_irqsave(&gus_lock, flags);

	tmp_queue[qtail] = midi_byte;
	qlen++;
	qtail++;

	spin_unlock_irqrestore(&gus_lock,flags);
	return 1;
}

static int gus_midi_start_read(int dev)
{
	return 0;
}

static int gus_midi_end_read(int dev)
{
	return 0;
}

static void gus_midi_kick(int dev)
{
}

static int gus_midi_buffer_status(int dev)
{
	unsigned long   flags;

	if (!output_used)
		return 0;

	spin_lock_irqsave(&gus_lock, flags);

	if (qlen && dump_to_midi(tmp_queue[qhead]))
	{
		qlen--;
		qhead++;
	}
	spin_unlock_irqrestore(&gus_lock,flags);
	return (qlen > 0) || !(GUS_MIDI_STATUS() & MIDI_XMIT_EMPTY);
}

#define MIDI_SYNTH_NAME	"Gravis Ultrasound Midi"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations gus_midi_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"Gravis UltraSound Midi", 0, 0, SNDCARD_GUS},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= gus_midi_open,
	.close		= gus_midi_close,
	.outputc	= gus_midi_out,
	.start_read	= gus_midi_start_read,
	.end_read	= gus_midi_end_read,
	.kick		= gus_midi_kick,
	.buffer_status	= gus_midi_buffer_status,
};

void __init gus_midi_init(struct address_info *hw_config)
{
	int dev = sound_alloc_mididev();

	if (dev == -1)
	{
		printk(KERN_INFO "gus_midi: Too many midi devices detected\n");
		return;
	}
	outb((MIDI_RESET), u_MidiControl);

	std_midi_synth.midi_dev = my_dev = dev;
	hw_config->slots[2] = dev;
	midi_devs[dev] = &gus_midi_operations;
	sequencer_init();
	return;
}

void gus_midi_interrupt(int dummy)
{
	volatile unsigned char stat, data;
	int timeout = 10;

	spin_lock(&gus_lock);

	while (timeout-- > 0 && (stat = GUS_MIDI_STATUS()) & (MIDI_RCV_FULL | MIDI_XMIT_EMPTY))
	{
		if (stat & MIDI_RCV_FULL)
		{
			data = inb(u_MidiData);
			if (input_opened)
				midi_input_intr(my_dev, data);
		}
		if (stat & MIDI_XMIT_EMPTY)
		{
			while (qlen && dump_to_midi(tmp_queue[qhead]))
			{
				qlen--;
				qhead++;
			}
			if (!qlen)
			{
			      /*
			       * Disable Midi output interrupts, since no data in the buffer
			       */
			      gus_midi_control &= ~MIDI_ENABLE_XMIT;
			      outb((gus_midi_control), u_MidiControl);
			      outb((gus_midi_control), u_MidiControl);
			}
		}
	}
	spin_unlock(&gus_lock);
}
