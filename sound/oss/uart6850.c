/*
 * sound/oss/uart6850.c
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 * Extended by Alan Cox for Red Hat Software. Now a loadable MIDI driver.
 * 28/4/97 - (C) Copyright Alan Cox. Released under the GPL version 2.
 *
 * Alan Cox:		Updated for new modular code. Removed snd_* irq handling. Now
 *			uses native linux resources
 * Christoph Hellwig:	Adapted to module_init/module_exit
 * Jeff Garzik:		Made it work again, in theory
 *			FIXME: If the request_irq() succeeds, the probe succeeds. Ug.
 *
 *	Status: Testing required (no shit -jgarzik)
 *
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
/* Mon Nov 22 22:38:35 MET 1993 marco@driq.home.usn.nl:
 *      added 6850 support, used with COVOX SoundMaster II and custom cards.
 */

#include "sound_config.h"

static int uart6850_base = 0x330;

static int *uart6850_osp;

#define	DATAPORT   (uart6850_base)
#define	COMDPORT   (uart6850_base+1)
#define	STATPORT   (uart6850_base+1)

static int uart6850_status(void)
{
	return inb(STATPORT);
}

#define input_avail()		(uart6850_status()&INPUT_AVAIL)
#define output_ready()		(uart6850_status()&OUTPUT_READY)

static void uart6850_cmd(unsigned char cmd)
{
	outb(cmd, COMDPORT);
}

static int uart6850_read(void)
{
	return inb(DATAPORT);
}

static void uart6850_write(unsigned char byte)
{
	outb(byte, DATAPORT);
}

#define	OUTPUT_READY	0x02	/* Mask for data ready Bit */
#define	INPUT_AVAIL	0x01	/* Mask for Data Send Ready Bit */

#define	UART_RESET	0x95
#define	UART_MODE_ON	0x03

static int uart6850_opened;
static int uart6850_irq;
static int uart6850_detected;
static int my_dev;
static DEFINE_SPINLOCK(lock);

static void (*midi_input_intr) (int dev, unsigned char data);
static void poll_uart6850(unsigned long dummy);


static DEFINE_TIMER(uart6850_timer, poll_uart6850, 0, 0);

static void uart6850_input_loop(void)
{
	int count = 10;

	while (count)
	{
		/*
		 * Not timed out
		 */
		if (input_avail())
		{
			unsigned char c = uart6850_read();
			count = 100;
			if (uart6850_opened & OPEN_READ)
				midi_input_intr(my_dev, c);
		}
		else
		{
			while (!input_avail() && count)
				count--;
		}
	}
}

static irqreturn_t m6850intr(int irq, void *dev_id)
{
	if (input_avail())
		uart6850_input_loop();
	return IRQ_HANDLED;
}

/*
 *	It looks like there is no input interrupts in the UART mode. Let's try
 *	polling.
 */

static void poll_uart6850(unsigned long dummy)
{
	unsigned long flags;

	if (!(uart6850_opened & OPEN_READ))
		return;		/* Device has been closed */

	spin_lock_irqsave(&lock,flags);
	if (input_avail())
		uart6850_input_loop();

	uart6850_timer.expires = 1 + jiffies;
	add_timer(&uart6850_timer);
	
	/*
	 *	Come back later
	 */

	spin_unlock_irqrestore(&lock,flags);
}

static int uart6850_open(int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
	if (uart6850_opened)
	{
/*		  printk("Midi6850: Midi busy\n");*/
		  return -EBUSY;
	}

	uart6850_cmd(UART_RESET);
	uart6850_input_loop();
	midi_input_intr = input;
	uart6850_opened = mode;
	poll_uart6850(0);	/*
				 * Enable input polling
				 */

	return 0;
}

static void uart6850_close(int dev)
{
	uart6850_cmd(UART_MODE_ON);
	del_timer(&uart6850_timer);
	uart6850_opened = 0;
}

static int uart6850_out(int dev, unsigned char midi_byte)
{
	int timeout;
	unsigned long flags;

	/*
	 * Test for input since pending input seems to block the output.
	 */

	spin_lock_irqsave(&lock,flags);

	if (input_avail())
		uart6850_input_loop();

	spin_unlock_irqrestore(&lock,flags);

	/*
	 * Sometimes it takes about 13000 loops before the output becomes ready
	 * (After reset). Normally it takes just about 10 loops.
	 */

	for (timeout = 30000; timeout > 0 && !output_ready(); timeout--);	/*
										 * Wait
										 */
	if (!output_ready())
	{
		printk(KERN_WARNING "Midi6850: Timeout\n");
		return 0;
	}
	uart6850_write(midi_byte);
	return 1;
}

static inline int uart6850_command(int dev, unsigned char *midi_byte)
{
	return 1;
}

static inline int uart6850_start_read(int dev)
{
	return 0;
}

static inline int uart6850_end_read(int dev)
{
	return 0;
}

static inline void uart6850_kick(int dev)
{
}

static inline int uart6850_buffer_status(int dev)
{
	return 0;		/*
				 * No data in buffers
				 */
}

#define MIDI_SYNTH_NAME	"6850 UART Midi"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations uart6850_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"6850 UART", 0, 0, SNDCARD_UART6850},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= uart6850_open,
	.close		= uart6850_close,
	.outputc	= uart6850_out,
	.start_read	= uart6850_start_read,
	.end_read	= uart6850_end_read,
	.kick		= uart6850_kick,
	.command	= uart6850_command,
	.buffer_status	= uart6850_buffer_status
};


static void __init attach_uart6850(struct address_info *hw_config)
{
	int ok, timeout;
	unsigned long   flags;

	if (!uart6850_detected)
		return;

	if ((my_dev = sound_alloc_mididev()) == -1)
	{
		printk(KERN_INFO "uart6850: Too many midi devices detected\n");
		return;
	}
	uart6850_base = hw_config->io_base;
	uart6850_osp = hw_config->osp;
	uart6850_irq = hw_config->irq;

	spin_lock_irqsave(&lock,flags);

	for (timeout = 30000; timeout > 0 && !output_ready(); timeout--);	/*
										 * Wait
										 */
	uart6850_cmd(UART_MODE_ON);
	ok = 1;
	spin_unlock_irqrestore(&lock,flags);

	conf_printf("6850 Midi Interface", hw_config);

	std_midi_synth.midi_dev = my_dev;
	hw_config->slots[4] = my_dev;
	midi_devs[my_dev] = &uart6850_operations;
	sequencer_init();
}

static inline int reset_uart6850(void)
{
	uart6850_read();
	return 1;		/*
				 * OK
				 */
}

static int __init probe_uart6850(struct address_info *hw_config)
{
	int ok;

	uart6850_osp = hw_config->osp;
	uart6850_base = hw_config->io_base;
	uart6850_irq = hw_config->irq;

	if (request_irq(uart6850_irq, m6850intr, 0, "MIDI6850", NULL) < 0)
		return 0;

	ok = reset_uart6850();
	uart6850_detected = ok;
	return ok;
}

static void __exit unload_uart6850(struct address_info *hw_config)
{
	free_irq(hw_config->irq, NULL);
	sound_unload_mididev(hw_config->slots[4]);
}

static struct address_info cfg_mpu;

static int __initdata io = -1;
static int __initdata irq = -1;

module_param(io, int, 0);
module_param(irq, int, 0);

static int __init init_uart6850(void)
{
	cfg_mpu.io_base = io;
	cfg_mpu.irq = irq;

	if (cfg_mpu.io_base == -1 || cfg_mpu.irq == -1) {
		printk(KERN_INFO "uart6850: irq and io must be set.\n");
		return -EINVAL;
	}

	if (probe_uart6850(&cfg_mpu))
		return -ENODEV;
	attach_uart6850(&cfg_mpu);

	return 0;
}

static void __exit cleanup_uart6850(void)
{
	unload_uart6850(&cfg_mpu);
}

module_init(init_uart6850);
module_exit(cleanup_uart6850);

#ifndef MODULE
static int __init setup_uart6850(char *str)
{
	/* io, irq */
	int ints[3];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io = ints[1];
	irq = ints[2];

	return 1;
}
__setup("uart6850=", setup_uart6850);
#endif
MODULE_LICENSE("GPL");
