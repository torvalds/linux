/*
 * sound/pas2_midi.c
 *
 * The low level driver for the PAS Midi Interface.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Bartlomiej Zolnierkiewicz	: Added __init to pas_init_mixer()
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "pas2.h"

extern spinlock_t pas_lock;

static int      midi_busy, input_opened;
static int      my_dev;

int pas2_mididev=-1;

static unsigned char tmp_queue[256];
static volatile int qlen;
static volatile unsigned char qhead, qtail;

static void     (*midi_input_intr) (int dev, unsigned char data);

static int pas_midi_open(int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
	int             err;
	unsigned long   flags;
	unsigned char   ctrl;


	if (midi_busy)
		return -EBUSY;

	/*
	 * Reset input and output FIFO pointers
	 */
	pas_write(0x20 | 0x40,
		  0x178b);

	spin_lock_irqsave(&pas_lock, flags);

	if ((err = pas_set_intr(0x10)) < 0)
	{
		spin_unlock_irqrestore(&pas_lock, flags);
		return err;
	}
	/*
	 * Enable input available and output FIFO empty interrupts
	 */

	ctrl = 0;
	input_opened = 0;
	midi_input_intr = input;

	if (mode == OPEN_READ || mode == OPEN_READWRITE)
	{
		ctrl |= 0x04;	/* Enable input */
		input_opened = 1;
	}
	if (mode == OPEN_WRITE || mode == OPEN_READWRITE)
	{
		ctrl |= 0x08 | 0x10;	/* Enable output */
	}
	pas_write(ctrl, 0x178b);

	/*
	 * Acknowledge any pending interrupts
	 */

	pas_write(0xff, 0x1B88);

	spin_unlock_irqrestore(&pas_lock, flags);

	midi_busy = 1;
	qlen = qhead = qtail = 0;
	return 0;
}

static void pas_midi_close(int dev)
{

	/*
	 * Reset FIFO pointers, disable intrs
	 */
	pas_write(0x20 | 0x40, 0x178b);

	pas_remove_intr(0x10);
	midi_busy = 0;
}

static int dump_to_midi(unsigned char midi_byte)
{
	int fifo_space, x;

	fifo_space = ((x = pas_read(0x1B89)) >> 4) & 0x0f;

	/*
	 * The MIDI FIFO space register and it's documentation is nonunderstandable.
	 * There seem to be no way to differentiate between buffer full and buffer
	 * empty situations. For this reason we don't never write the buffer
	 * completely full. In this way we can assume that 0 (or is it 15)
	 * means that the buffer is empty.
	 */

	if (fifo_space < 2 && fifo_space != 0)	/* Full (almost) */
		return 0;	/* Ask upper layers to retry after some time */

	pas_write(midi_byte, 0x178A);

	return 1;
}

static int pas_midi_out(int dev, unsigned char midi_byte)
{

	unsigned long flags;

	/*
	 * Drain the local queue first
	 */

	spin_lock_irqsave(&pas_lock, flags);

	while (qlen && dump_to_midi(tmp_queue[qhead]))
	{
		qlen--;
		qhead++;
	}

	spin_unlock_irqrestore(&pas_lock, flags);

	/*
	 *	Output the byte if the local queue is empty.
	 */

	if (!qlen)
		if (dump_to_midi(midi_byte))
			return 1;

	/*
	 *	Put to the local queue
	 */

	if (qlen >= 256)
		return 0;	/* Local queue full */

	spin_lock_irqsave(&pas_lock, flags);

	tmp_queue[qtail] = midi_byte;
	qlen++;
	qtail++;

	spin_unlock_irqrestore(&pas_lock, flags);

	return 1;
}

static int pas_midi_start_read(int dev)
{
	return 0;
}

static int pas_midi_end_read(int dev)
{
	return 0;
}

static void pas_midi_kick(int dev)
{
}

static int pas_buffer_status(int dev)
{
	return qlen;
}

#define MIDI_SYNTH_NAME	"Pro Audio Spectrum Midi"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations pas_midi_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"Pro Audio Spectrum", 0, 0, SNDCARD_PAS},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= pas_midi_open,
	.close		= pas_midi_close,
	.outputc	= pas_midi_out,
	.start_read	= pas_midi_start_read,
	.end_read	= pas_midi_end_read,
	.kick		= pas_midi_kick,
	.buffer_status	= pas_buffer_status,
};

void __init pas_midi_init(void)
{
	int dev = sound_alloc_mididev();

	if (dev == -1)
	{
		printk(KERN_WARNING "pas_midi_init: Too many midi devices detected\n");
		return;
	}
	std_midi_synth.midi_dev = my_dev = dev;
	midi_devs[dev] = &pas_midi_operations;
	pas2_mididev = dev;
	sequencer_init();
}

void pas_midi_interrupt(void)
{
	unsigned char   stat;
	int             i, incount;

	stat = pas_read(0x1B88);

	if (stat & 0x04)	/* Input data available */
	{
		incount = pas_read(0x1B89) & 0x0f;	/* Input FIFO size */
		if (!incount)
			incount = 16;

		for (i = 0; i < incount; i++)
			if (input_opened)
			{
				midi_input_intr(my_dev, pas_read(0x178A));
			} else
				pas_read(0x178A);	/* Flush */
	}
	if (stat & (0x08 | 0x10))
	{
		spin_lock(&pas_lock);/* called in irq context */

		while (qlen && dump_to_midi(tmp_queue[qhead]))
		{
			qlen--;
			qhead++;
		}

		spin_unlock(&pas_lock);
	}
	if (stat & 0x40)
	{
		printk(KERN_WARNING "MIDI output overrun %x,%x\n", pas_read(0x1B89), stat);
	}
	pas_write(stat, 0x1B88);	/* Acknowledge interrupts */
}
