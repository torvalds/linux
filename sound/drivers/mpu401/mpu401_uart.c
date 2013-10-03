/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of MPU-401 in UART mode
 *
 *  MPU-401 supports UART mode which is not capable generate transmit
 *  interrupts thus output is done via polling. Without interrupt,
 *  input is done also via polling. Do not expect good performance.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *   13-03-2003:
 *      Added support for different kind of hardware I/O. Build in choices
 *      are port and mmio. For other kind of I/O, set mpu->read and
 *      mpu->write to your own I/O functions.
 *
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <sound/core.h>
#include <sound/mpu401.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for control of MPU-401 in UART mode");
MODULE_LICENSE("GPL");

static void snd_mpu401_uart_input_read(struct snd_mpu401 * mpu);
static void snd_mpu401_uart_output_write(struct snd_mpu401 * mpu);

/*

 */

#define snd_mpu401_input_avail(mpu) \
	(!(mpu->read(mpu, MPU401C(mpu)) & MPU401_RX_EMPTY))
#define snd_mpu401_output_ready(mpu) \
	(!(mpu->read(mpu, MPU401C(mpu)) & MPU401_TX_FULL))

/* Build in lowlevel io */
static void mpu401_write_port(struct snd_mpu401 *mpu, unsigned char data,
			      unsigned long addr)
{
	outb(data, addr);
}

static unsigned char mpu401_read_port(struct snd_mpu401 *mpu,
				      unsigned long addr)
{
	return inb(addr);
}

static void mpu401_write_mmio(struct snd_mpu401 *mpu, unsigned char data,
			      unsigned long addr)
{
	writeb(data, (void __iomem *)addr);
}

static unsigned char mpu401_read_mmio(struct snd_mpu401 *mpu,
				      unsigned long addr)
{
	return readb((void __iomem *)addr);
}
/*  */

static void snd_mpu401_uart_clear_rx(struct snd_mpu401 *mpu)
{
	int timeout = 100000;
	for (; timeout > 0 && snd_mpu401_input_avail(mpu); timeout--)
		mpu->read(mpu, MPU401D(mpu));
#ifdef CONFIG_SND_DEBUG
	if (timeout <= 0)
		snd_printk(KERN_ERR "cmd: clear rx timeout (status = 0x%x)\n",
			   mpu->read(mpu, MPU401C(mpu)));
#endif
}

static void uart_interrupt_tx(struct snd_mpu401 *mpu)
{
	unsigned long flags;

	if (test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode) &&
	    test_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode)) {
		spin_lock_irqsave(&mpu->output_lock, flags);
		snd_mpu401_uart_output_write(mpu);
		spin_unlock_irqrestore(&mpu->output_lock, flags);
	}
}

static void _snd_mpu401_uart_interrupt(struct snd_mpu401 *mpu)
{
	unsigned long flags;

	if (mpu->info_flags & MPU401_INFO_INPUT) {
		spin_lock_irqsave(&mpu->input_lock, flags);
		if (test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode))
			snd_mpu401_uart_input_read(mpu);
		else
			snd_mpu401_uart_clear_rx(mpu);
		spin_unlock_irqrestore(&mpu->input_lock, flags);
	}
	if (! (mpu->info_flags & MPU401_INFO_TX_IRQ))
		/* ok. for better Tx performance try do some output
		   when input is done */
		uart_interrupt_tx(mpu);
}

/**
 * snd_mpu401_uart_interrupt - generic MPU401-UART interrupt handler
 * @irq: the irq number
 * @dev_id: mpu401 instance
 *
 * Processes the interrupt for MPU401-UART i/o.
 *
 * Return: %IRQ_HANDLED if the interrupt was handled. %IRQ_NONE otherwise.
 */
irqreturn_t snd_mpu401_uart_interrupt(int irq, void *dev_id)
{
	struct snd_mpu401 *mpu = dev_id;
	
	if (mpu == NULL)
		return IRQ_NONE;
	_snd_mpu401_uart_interrupt(mpu);
	return IRQ_HANDLED;
}

EXPORT_SYMBOL(snd_mpu401_uart_interrupt);

/**
 * snd_mpu401_uart_interrupt_tx - generic MPU401-UART transmit irq handler
 * @irq: the irq number
 * @dev_id: mpu401 instance
 *
 * Processes the interrupt for MPU401-UART output.
 *
 * Return: %IRQ_HANDLED if the interrupt was handled. %IRQ_NONE otherwise.
 */
irqreturn_t snd_mpu401_uart_interrupt_tx(int irq, void *dev_id)
{
	struct snd_mpu401 *mpu = dev_id;
	
	if (mpu == NULL)
		return IRQ_NONE;
	uart_interrupt_tx(mpu);
	return IRQ_HANDLED;
}

EXPORT_SYMBOL(snd_mpu401_uart_interrupt_tx);

/*
 * timer callback
 * reprogram the timer and call the interrupt job
 */
static void snd_mpu401_uart_timer(unsigned long data)
{
	struct snd_mpu401 *mpu = (struct snd_mpu401 *)data;
	unsigned long flags;

	spin_lock_irqsave(&mpu->timer_lock, flags);
	/*mpu->mode |= MPU401_MODE_TIMER;*/
	mpu->timer.expires = 1 + jiffies;
	add_timer(&mpu->timer);
	spin_unlock_irqrestore(&mpu->timer_lock, flags);
	if (mpu->rmidi)
		_snd_mpu401_uart_interrupt(mpu);
}

/*
 * initialize the timer callback if not programmed yet
 */
static void snd_mpu401_uart_add_timer (struct snd_mpu401 *mpu, int input)
{
	unsigned long flags;

	spin_lock_irqsave (&mpu->timer_lock, flags);
	if (mpu->timer_invoked == 0) {
		init_timer(&mpu->timer);
		mpu->timer.data = (unsigned long)mpu;
		mpu->timer.function = snd_mpu401_uart_timer;
		mpu->timer.expires = 1 + jiffies;
		add_timer(&mpu->timer);
	} 
	mpu->timer_invoked |= input ? MPU401_MODE_INPUT_TIMER :
		MPU401_MODE_OUTPUT_TIMER;
	spin_unlock_irqrestore (&mpu->timer_lock, flags);
}

/*
 * remove the timer callback if still active
 */
static void snd_mpu401_uart_remove_timer (struct snd_mpu401 *mpu, int input)
{
	unsigned long flags;

	spin_lock_irqsave (&mpu->timer_lock, flags);
	if (mpu->timer_invoked) {
		mpu->timer_invoked &= input ? ~MPU401_MODE_INPUT_TIMER :
			~MPU401_MODE_OUTPUT_TIMER;
		if (! mpu->timer_invoked)
			del_timer(&mpu->timer);
	}
	spin_unlock_irqrestore (&mpu->timer_lock, flags);
}

/*
 * send a UART command
 * return zero if successful, non-zero for some errors
 */

static int snd_mpu401_uart_cmd(struct snd_mpu401 * mpu, unsigned char cmd,
			       int ack)
{
	unsigned long flags;
	int timeout, ok;

	spin_lock_irqsave(&mpu->input_lock, flags);
	if (mpu->hardware != MPU401_HW_TRID4DWAVE) {
		mpu->write(mpu, 0x00, MPU401D(mpu));
		/*snd_mpu401_uart_clear_rx(mpu);*/
	}
	/* ok. standard MPU-401 initialization */
	if (mpu->hardware != MPU401_HW_SB) {
		for (timeout = 1000; timeout > 0 &&
			     !snd_mpu401_output_ready(mpu); timeout--)
			udelay(10);
#ifdef CONFIG_SND_DEBUG
		if (!timeout)
			snd_printk(KERN_ERR "cmd: tx timeout (status = 0x%x)\n",
				   mpu->read(mpu, MPU401C(mpu)));
#endif
	}
	mpu->write(mpu, cmd, MPU401C(mpu));
	if (ack && !(mpu->info_flags & MPU401_INFO_NO_ACK)) {
		ok = 0;
		timeout = 10000;
		while (!ok && timeout-- > 0) {
			if (snd_mpu401_input_avail(mpu)) {
				if (mpu->read(mpu, MPU401D(mpu)) == MPU401_ACK)
					ok = 1;
			}
		}
		if (!ok && mpu->read(mpu, MPU401D(mpu)) == MPU401_ACK)
			ok = 1;
	} else
		ok = 1;
	spin_unlock_irqrestore(&mpu->input_lock, flags);
	if (!ok) {
		snd_printk(KERN_ERR "cmd: 0x%x failed at 0x%lx "
			   "(status = 0x%x, data = 0x%x)\n", cmd, mpu->port,
			   mpu->read(mpu, MPU401C(mpu)),
			   mpu->read(mpu, MPU401D(mpu)));
		return 1;
	}
	return 0;
}

static int snd_mpu401_do_reset(struct snd_mpu401 *mpu)
{
	if (snd_mpu401_uart_cmd(mpu, MPU401_RESET, 1))
		return -EIO;
	if (snd_mpu401_uart_cmd(mpu, MPU401_ENTER_UART, 0))
		return -EIO;
	return 0;
}

/*
 * input/output open/close - protected by open_mutex in rawmidi.c
 */
static int snd_mpu401_uart_input_open(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;
	int err;

	mpu = substream->rmidi->private_data;
	if (mpu->open_input && (err = mpu->open_input(mpu)) < 0)
		return err;
	if (! test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode)) {
		if (snd_mpu401_do_reset(mpu) < 0)
			goto error_out;
	}
	mpu->substream_input = substream;
	set_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	return 0;

error_out:
	if (mpu->open_input && mpu->close_input)
		mpu->close_input(mpu);
	return -EIO;
}

static int snd_mpu401_uart_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;
	int err;

	mpu = substream->rmidi->private_data;
	if (mpu->open_output && (err = mpu->open_output(mpu)) < 0)
		return err;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode)) {
		if (snd_mpu401_do_reset(mpu) < 0)
			goto error_out;
	}
	mpu->substream_output = substream;
	set_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	return 0;

error_out:
	if (mpu->open_output && mpu->close_output)
		mpu->close_output(mpu);
	return -EIO;
}

static int snd_mpu401_uart_input_close(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;
	int err = 0;

	mpu = substream->rmidi->private_data;
	clear_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	mpu->substream_input = NULL;
	if (! test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode))
		err = snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_input)
		mpu->close_input(mpu);
	if (err)
		return -EIO;
	return 0;
}

static int snd_mpu401_uart_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;
	int err = 0;

	mpu = substream->rmidi->private_data;
	clear_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	mpu->substream_output = NULL;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode))
		err = snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_output)
		mpu->close_output(mpu);
	if (err)
		return -EIO;
	return 0;
}

/*
 * trigger input callback
 */
static void
snd_mpu401_uart_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_mpu401 *mpu;
	int max = 64;

	mpu = substream->rmidi->private_data;
	if (up) {
		if (! test_and_set_bit(MPU401_MODE_BIT_INPUT_TRIGGER,
				       &mpu->mode)) {
			/* first time - flush FIFO */
			while (max-- > 0)
				mpu->read(mpu, MPU401D(mpu));
			if (mpu->info_flags & MPU401_INFO_USE_TIMER)
				snd_mpu401_uart_add_timer(mpu, 1);
		}
		
		/* read data in advance */
		spin_lock_irqsave(&mpu->input_lock, flags);
		snd_mpu401_uart_input_read(mpu);
		spin_unlock_irqrestore(&mpu->input_lock, flags);
	} else {
		if (mpu->info_flags & MPU401_INFO_USE_TIMER)
			snd_mpu401_uart_remove_timer(mpu, 1);
		clear_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode);
	}

}

/*
 * transfer input pending data
 * call with input_lock spinlock held
 */
static void snd_mpu401_uart_input_read(struct snd_mpu401 * mpu)
{
	int max = 128;
	unsigned char byte;

	while (max-- > 0) {
		if (! snd_mpu401_input_avail(mpu))
			break; /* input not available */
		byte = mpu->read(mpu, MPU401D(mpu));
		if (test_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode))
			snd_rawmidi_receive(mpu->substream_input, &byte, 1);
	}
}

/*
 *  Tx FIFO sizes:
 *    CS4237B			- 16 bytes
 *    AudioDrive ES1688         - 12 bytes
 *    S3 SonicVibes             -  8 bytes
 *    SoundBlaster AWE 64       -  2 bytes (ugly hardware)
 */

/*
 * write output pending bytes
 * call with output_lock spinlock held
 */
static void snd_mpu401_uart_output_write(struct snd_mpu401 * mpu)
{
	unsigned char byte;
	int max = 256;

	do {
		if (snd_rawmidi_transmit_peek(mpu->substream_output,
					      &byte, 1) == 1) {
			/*
			 * Try twice because there is hardware that insists on
			 * setting the output busy bit after each write.
			 */
			if (!snd_mpu401_output_ready(mpu) &&
			    !snd_mpu401_output_ready(mpu))
				break;	/* Tx FIFO full - try again later */
			mpu->write(mpu, byte, MPU401D(mpu));
			snd_rawmidi_transmit_ack(mpu->substream_output, 1);
		} else {
			snd_mpu401_uart_remove_timer (mpu, 0);
			break;	/* no other data - leave the tx loop */
		}
	} while (--max > 0);
}

/*
 * output trigger callback
 */
static void
snd_mpu401_uart_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_mpu401 *mpu;

	mpu = substream->rmidi->private_data;
	if (up) {
		set_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);

		/* try to add the timer at each output trigger,
		 * since the output timer might have been removed in
		 * snd_mpu401_uart_output_write().
		 */
		if (! (mpu->info_flags & MPU401_INFO_TX_IRQ))
			snd_mpu401_uart_add_timer(mpu, 0);

		/* output pending data */
		spin_lock_irqsave(&mpu->output_lock, flags);
		snd_mpu401_uart_output_write(mpu);
		spin_unlock_irqrestore(&mpu->output_lock, flags);
	} else {
		if (! (mpu->info_flags & MPU401_INFO_TX_IRQ))
			snd_mpu401_uart_remove_timer(mpu, 0);
		clear_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);
	}
}

/*

 */

static struct snd_rawmidi_ops snd_mpu401_uart_output =
{
	.open =		snd_mpu401_uart_output_open,
	.close =	snd_mpu401_uart_output_close,
	.trigger =	snd_mpu401_uart_output_trigger,
};

static struct snd_rawmidi_ops snd_mpu401_uart_input =
{
	.open =		snd_mpu401_uart_input_open,
	.close =	snd_mpu401_uart_input_close,
	.trigger =	snd_mpu401_uart_input_trigger,
};

static void snd_mpu401_uart_free(struct snd_rawmidi *rmidi)
{
	struct snd_mpu401 *mpu = rmidi->private_data;
	if (mpu->irq >= 0)
		free_irq(mpu->irq, (void *) mpu);
	release_and_free_resource(mpu->res);
	kfree(mpu);
}

/**
 * snd_mpu401_uart_new - create an MPU401-UART instance
 * @card: the card instance
 * @device: the device index, zero-based
 * @hardware: the hardware type, MPU401_HW_XXXX
 * @port: the base address of MPU401 port
 * @info_flags: bitflags MPU401_INFO_XXX
 * @irq: the ISA irq number, -1 if not to be allocated
 * @rrawmidi: the pointer to store the new rawmidi instance
 *
 * Creates a new MPU-401 instance.
 *
 * Note that the rawmidi instance is returned on the rrawmidi argument,
 * not the mpu401 instance itself.  To access to the mpu401 instance,
 * cast from rawmidi->private_data (with struct snd_mpu401 magic-cast).
 *
 * Return: Zero if successful, or a negative error code.
 */
int snd_mpu401_uart_new(struct snd_card *card, int device,
			unsigned short hardware,
			unsigned long port,
			unsigned int info_flags,
			int irq,
			struct snd_rawmidi ** rrawmidi)
{
	struct snd_mpu401 *mpu;
	struct snd_rawmidi *rmidi;
	int in_enable, out_enable;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if (! (info_flags & (MPU401_INFO_INPUT | MPU401_INFO_OUTPUT)))
		info_flags |= MPU401_INFO_INPUT | MPU401_INFO_OUTPUT;
	in_enable = (info_flags & MPU401_INFO_INPUT) ? 1 : 0;
	out_enable = (info_flags & MPU401_INFO_OUTPUT) ? 1 : 0;
	if ((err = snd_rawmidi_new(card, "MPU-401U", device,
				   out_enable, in_enable, &rmidi)) < 0)
		return err;
	mpu = kzalloc(sizeof(*mpu), GFP_KERNEL);
	if (mpu == NULL) {
		snd_printk(KERN_ERR "mpu401_uart: cannot allocate\n");
		snd_device_free(card, rmidi);
		return -ENOMEM;
	}
	rmidi->private_data = mpu;
	rmidi->private_free = snd_mpu401_uart_free;
	spin_lock_init(&mpu->input_lock);
	spin_lock_init(&mpu->output_lock);
	spin_lock_init(&mpu->timer_lock);
	mpu->hardware = hardware;
	mpu->irq = -1;
	if (! (info_flags & MPU401_INFO_INTEGRATED)) {
		int res_size = hardware == MPU401_HW_PC98II ? 4 : 2;
		mpu->res = request_region(port, res_size, "MPU401 UART");
		if (mpu->res == NULL) {
			snd_printk(KERN_ERR "mpu401_uart: "
				   "unable to grab port 0x%lx size %d\n",
				   port, res_size);
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
	}
	if (info_flags & MPU401_INFO_MMIO) {
		mpu->write = mpu401_write_mmio;
		mpu->read = mpu401_read_mmio;
	} else {
		mpu->write = mpu401_write_port;
		mpu->read = mpu401_read_port;
	}
	mpu->port = port;
	if (hardware == MPU401_HW_PC98II)
		mpu->cport = port + 2;
	else
		mpu->cport = port + 1;
	if (irq >= 0) {
		if (request_irq(irq, snd_mpu401_uart_interrupt, 0,
				"MPU401 UART", (void *) mpu)) {
			snd_printk(KERN_ERR "mpu401_uart: "
				   "unable to grab IRQ %d\n", irq);
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
	}
	if (irq < 0 && !(info_flags & MPU401_INFO_IRQ_HOOK))
		info_flags |= MPU401_INFO_USE_TIMER;
	mpu->info_flags = info_flags;
	mpu->irq = irq;
	if (card->shortname[0])
		snprintf(rmidi->name, sizeof(rmidi->name), "%s MIDI",
			 card->shortname);
	else
		sprintf(rmidi->name, "MPU-401 MIDI %d-%d",card->number, device);
	if (out_enable) {
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &snd_mpu401_uart_output);
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	}
	if (in_enable) {
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &snd_mpu401_uart_input);
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
		if (out_enable)
			rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;
	}
	mpu->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}

EXPORT_SYMBOL(snd_mpu401_uart_new);

/*
 *  INIT part
 */

static int __init alsa_mpu401_uart_init(void)
{
	return 0;
}

static void __exit alsa_mpu401_uart_exit(void)
{
}

module_init(alsa_mpu401_uart_init)
module_exit(alsa_mpu401_uart_exit)
