/*
 * sound/uart401.c
 *
 * MPU-401 UART driver (formerly uart401_midi.c)
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 *	Alan Cox		Reformatted, removed sound_mem usage, use normal Linux
 *				interrupt allocation. Protect against bogus unload
 *				Fixed to allow IRQ > 15
 *	Christoph Hellwig	Adapted to module_init/module_exit
 *	Arnaldo C. de Melo	got rid of check_region
 *
 * Status:
 *		Untested
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "mpu401.h"

typedef struct uart401_devc
{
	int             base;
	int             irq;
	int            *osp;
	void            (*midi_input_intr) (int dev, unsigned char data);
	int             opened, disabled;
	volatile unsigned char input_byte;
	int             my_dev;
	int             share_irq;
	spinlock_t	lock;
}
uart401_devc;

#define	DATAPORT   (devc->base)
#define	COMDPORT   (devc->base+1)
#define	STATPORT   (devc->base+1)

static int uart401_status(uart401_devc * devc)
{
	return inb(STATPORT);
}

#define input_avail(devc) (!(uart401_status(devc)&INPUT_AVAIL))
#define output_ready(devc)	(!(uart401_status(devc)&OUTPUT_READY))

static void uart401_cmd(uart401_devc * devc, unsigned char cmd)
{
	outb((cmd), COMDPORT);
}

static int uart401_read(uart401_devc * devc)
{
	return inb(DATAPORT);
}

static void uart401_write(uart401_devc * devc, unsigned char byte)
{
	outb((byte), DATAPORT);
}

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	MPU_RESET	0xFF
#define	UART_MODE_ON	0x3F

static int      reset_uart401(uart401_devc * devc);
static void     enter_uart_mode(uart401_devc * devc);

static void uart401_input_loop(uart401_devc * devc)
{
	int work_limit=30000;
	
	while (input_avail(devc) && --work_limit)
	{
		unsigned char   c = uart401_read(devc);

		if (c == MPU_ACK)
			devc->input_byte = c;
		else if (devc->opened & OPEN_READ && devc->midi_input_intr)
			devc->midi_input_intr(devc->my_dev, c);
	}
	if(work_limit==0)
		printk(KERN_WARNING "Too much work in interrupt on uart401 (0x%X). UART jabbering ??\n", devc->base);
}

irqreturn_t uart401intr(int irq, void *dev_id, struct pt_regs *dummy)
{
	uart401_devc *devc = dev_id;

	if (devc == NULL)
	{
		printk(KERN_ERR "uart401: bad devc\n");
		return IRQ_NONE;
	}

	if (input_avail(devc))
		uart401_input_loop(devc);
	return IRQ_HANDLED;
}

static int
uart401_open(int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
)
{
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	if (devc->opened)
		return -EBUSY;

	/* Flush the UART */
	
	while (input_avail(devc))
		uart401_read(devc);

	devc->midi_input_intr = input;
	devc->opened = mode;
	enter_uart_mode(devc);
	devc->disabled = 0;

	return 0;
}

static void uart401_close(int dev)
{
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	reset_uart401(devc);
	devc->opened = 0;
}

static int uart401_out(int dev, unsigned char midi_byte)
{
	int timeout;
	unsigned long flags;
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	if (devc->disabled)
		return 1;
	/*
	 * Test for input since pending input seems to block the output.
	 */

	spin_lock_irqsave(&devc->lock,flags);	
	if (input_avail(devc))
		uart401_input_loop(devc);

	spin_unlock_irqrestore(&devc->lock,flags);

	/*
	 * Sometimes it takes about 13000 loops before the output becomes ready
	 * (After reset). Normally it takes just about 10 loops.
	 */

	for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);

	if (!output_ready(devc))
	{
		  printk(KERN_WARNING "uart401: Timeout - Device not responding\n");
		  devc->disabled = 1;
		  reset_uart401(devc);
		  enter_uart_mode(devc);
		  return 1;
	}
	uart401_write(devc, midi_byte);
	return 1;
}

static inline int uart401_start_read(int dev)
{
	return 0;
}

static inline int uart401_end_read(int dev)
{
	return 0;
}

static inline void uart401_kick(int dev)
{
}

static inline int uart401_buffer_status(int dev)
{
	return 0;
}

#define MIDI_SYNTH_NAME	"MPU-401 UART"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static const struct midi_operations uart401_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"MPU-401 (UART) MIDI", 0, 0, SNDCARD_MPU401},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= uart401_open,
	.close		= uart401_close,
	.outputc	= uart401_out,
	.start_read	= uart401_start_read,
	.end_read	= uart401_end_read,
	.kick		= uart401_kick,
	.buffer_status	= uart401_buffer_status,
};

static void enter_uart_mode(uart401_devc * devc)
{
	int ok, timeout;
	unsigned long flags;

	spin_lock_irqsave(&devc->lock,flags);	
	for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);

	devc->input_byte = 0;
	uart401_cmd(devc, UART_MODE_ON);

	ok = 0;
	for (timeout = 50000; timeout > 0 && !ok; timeout--)
		if (devc->input_byte == MPU_ACK)
			ok = 1;
		else if (input_avail(devc))
			if (uart401_read(devc) == MPU_ACK)
				ok = 1;

	spin_unlock_irqrestore(&devc->lock,flags);
}

static int reset_uart401(uart401_devc * devc)
{
	int ok, timeout, n;

	/*
	 * Send the RESET command. Try again if no success at the first time.
	 */

	ok = 0;

	for (n = 0; n < 2 && !ok; n++)
	{
		for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);
		devc->input_byte = 0;
		uart401_cmd(devc, MPU_RESET);

		/*
		 * Wait at least 25 msec. This method is not accurate so let's make the
		 * loop bit longer. Cannot sleep since this is called during boot.
		 */

		for (timeout = 50000; timeout > 0 && !ok; timeout--)
		{
			if (devc->input_byte == MPU_ACK)	/* Interrupt */
				ok = 1;
			else if (input_avail(devc))
			{
				if (uart401_read(devc) == MPU_ACK)
					ok = 1;
			}
		}
	}


	if (ok)
	{
		DEB(printk("Reset UART401 OK\n"));
	}
	else
		DDB(printk("Reset UART401 failed - No hardware detected.\n"));

	if (ok)
		uart401_input_loop(devc);	/*
						 * Flush input before enabling interrupts
						 */

	return ok;
}

int probe_uart401(struct address_info *hw_config, struct module *owner)
{
	uart401_devc *devc;
	char *name = "MPU-401 (UART) MIDI";
	int ok = 0;
	unsigned long flags;

	DDB(printk("Entered probe_uart401()\n"));

	/* Default to "not found" */
	hw_config->slots[4] = -1;

	if (!request_region(hw_config->io_base, 4, "MPU-401 UART")) {
		printk(KERN_INFO "uart401: could not request_region(%d, 4)\n", hw_config->io_base);
		return 0;
	}

	devc = kmalloc(sizeof(uart401_devc), GFP_KERNEL);
	if (!devc) {
		printk(KERN_WARNING "uart401: Can't allocate memory\n");
		goto cleanup_region;
	}

	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->osp = hw_config->osp;
	devc->midi_input_intr = NULL;
	devc->opened = 0;
	devc->input_byte = 0;
	devc->my_dev = 0;
	devc->share_irq = 0;
	spin_lock_init(&devc->lock);

	spin_lock_irqsave(&devc->lock,flags);	
	ok = reset_uart401(devc);
	spin_unlock_irqrestore(&devc->lock,flags);

	if (!ok)
		goto cleanup_devc;

	if (hw_config->name)
		name = hw_config->name;

	if (devc->irq < 0) {
		devc->share_irq = 1;
		devc->irq *= -1;
	} else
		devc->share_irq = 0;

	if (!devc->share_irq)
		if (request_irq(devc->irq, uart401intr, 0, "MPU-401 UART", devc) < 0) {
			printk(KERN_WARNING "uart401: Failed to allocate IRQ%d\n", devc->irq);
			devc->share_irq = 1;
		}
	devc->my_dev = sound_alloc_mididev();
	enter_uart_mode(devc);

	if (devc->my_dev == -1) {
		printk(KERN_INFO "uart401: Too many midi devices detected\n");
		goto cleanup_irq;
	}
	conf_printf(name, hw_config);
	midi_devs[devc->my_dev] = kmalloc(sizeof(struct midi_operations), GFP_KERNEL);
	if (!midi_devs[devc->my_dev]) {
		printk(KERN_ERR "uart401: Failed to allocate memory\n");
		goto cleanup_unload_mididev;
	}
	memcpy(midi_devs[devc->my_dev], &uart401_operations, sizeof(struct midi_operations));

	if (owner)
		midi_devs[devc->my_dev]->owner = owner;
	
	midi_devs[devc->my_dev]->devc = devc;
	midi_devs[devc->my_dev]->converter = kmalloc(sizeof(struct synth_operations), GFP_KERNEL);
	if (!midi_devs[devc->my_dev]->converter) {
		printk(KERN_WARNING "uart401: Failed to allocate memory\n");
		goto cleanup_midi_devs;
	}
	memcpy(midi_devs[devc->my_dev]->converter, &std_midi_synth, sizeof(struct synth_operations));
	strcpy(midi_devs[devc->my_dev]->info.name, name);
	midi_devs[devc->my_dev]->converter->id = "UART401";
	midi_devs[devc->my_dev]->converter->midi_dev = devc->my_dev;

	if (owner)
		midi_devs[devc->my_dev]->converter->owner = owner;

	hw_config->slots[4] = devc->my_dev;
	sequencer_init();
	devc->opened = 0;
	return 1;
cleanup_midi_devs:
	kfree(midi_devs[devc->my_dev]);
cleanup_unload_mididev:
	sound_unload_mididev(devc->my_dev);
cleanup_irq:
	if (!devc->share_irq)
		free_irq(devc->irq, devc);
cleanup_devc:
	kfree(devc);
cleanup_region:
	release_region(hw_config->io_base, 4);
	return 0;
}

void unload_uart401(struct address_info *hw_config)
{
	uart401_devc *devc;
	int n=hw_config->slots[4];
	
	/* Not set up */
	if(n==-1 || midi_devs[n]==NULL)
		return;
		
	/* Not allocated (erm ??) */
	
	devc = midi_devs[hw_config->slots[4]]->devc;
	if (devc == NULL)
		return;

	reset_uart401(devc);
	release_region(hw_config->io_base, 4);

	if (!devc->share_irq)
		free_irq(devc->irq, devc);
	if (devc)
	{
		kfree(midi_devs[devc->my_dev]->converter);
		kfree(midi_devs[devc->my_dev]);
		kfree(devc);
		devc = NULL;
	}
	/* This kills midi_devs[x] */
	sound_unload_mididev(hw_config->slots[4]);
}

EXPORT_SYMBOL(probe_uart401);
EXPORT_SYMBOL(unload_uart401);
EXPORT_SYMBOL(uart401intr);

static struct address_info cfg_mpu;

static int io = -1;
static int irq = -1;

module_param(io, int, 0444);
module_param(irq, int, 0444);


static int __init init_uart401(void)
{
	cfg_mpu.irq = irq;
	cfg_mpu.io_base = io;

	/* Can be loaded either for module use or to provide functions
	   to others */
	if (cfg_mpu.io_base != -1 && cfg_mpu.irq != -1) {
		printk(KERN_INFO "MPU-401 UART driver Copyright (C) Hannu Savolainen 1993-1997");
		if (!probe_uart401(&cfg_mpu, THIS_MODULE))
			return -ENODEV;
	}

	return 0;
}

static void __exit cleanup_uart401(void)
{
	if (cfg_mpu.io_base != -1 && cfg_mpu.irq != -1)
		unload_uart401(&cfg_mpu);
}

module_init(init_uart401);
module_exit(cleanup_uart401);

#ifndef MODULE
static int __init setup_uart401(char *str)
{
	/* io, irq */
	int ints[3];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);

	io = ints[1];
	irq = ints[2];
	
	return 1;
}

__setup("uart401=", setup_uart401);
#endif
MODULE_LICENSE("GPL");
