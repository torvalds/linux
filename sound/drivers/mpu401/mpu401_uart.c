/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of MPU-401 in UART mode
 *
 *  MPU-401 supports UART mode which is not capable generate transmit
 *  interrupts thus output is done via polling. Also, if irq < 0, then
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

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <sound/core.h>
#include <sound/mpu401.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Routines for control of MPU-401 in UART mode");
MODULE_LICENSE("GPL");

static void snd_mpu401_uart_input_read(struct snd_mpu401 * mpu);
static void snd_mpu401_uart_output_write(struct snd_mpu401 * mpu);

/*

 */

#define snd_mpu401_input_avail(mpu)	(!(mpu->read(mpu, MPU401C(mpu)) & 0x80))
#define snd_mpu401_output_ready(mpu)	(!(mpu->read(mpu, MPU401C(mpu)) & 0x40))

#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		0xfe

/* Build in lowlevel io */
static void mpu401_write_port(struct snd_mpu401 *mpu, unsigned char data, unsigned long addr)
{
	outb(data, addr);
}

static unsigned char mpu401_read_port(struct snd_mpu401 *mpu, unsigned long addr)
{
	return inb(addr);
}

static void mpu401_write_mmio(struct snd_mpu401 *mpu, unsigned char data, unsigned long addr)
{
	writeb(data, (void __iomem *)addr);
}

static unsigned char mpu401_read_mmio(struct snd_mpu401 *mpu, unsigned long addr)
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
		snd_printk("cmd: clear rx timeout (status = 0x%x)\n", mpu->read(mpu, MPU401C(mpu)));
#endif
}

static void _snd_mpu401_uart_interrupt(struct snd_mpu401 *mpu)
{
	spin_lock(&mpu->input_lock);
	if (test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode)) {
		snd_mpu401_uart_input_read(mpu);
	} else {
		snd_mpu401_uart_clear_rx(mpu);
	}
	spin_unlock(&mpu->input_lock);
 	/* ok. for better Tx performance try do some output when input is done */
	if (test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode) &&
	    test_bit(MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode)) {
		spin_lock(&mpu->output_lock);
		snd_mpu401_uart_output_write(mpu);
		spin_unlock(&mpu->output_lock);
	}
}

/**
 * snd_mpu401_uart_interrupt - generic MPU401-UART interrupt handler
 * @irq: the irq number
 * @dev_id: mpu401 instance
 * @regs: the reigster
 *
 * Processes the interrupt for MPU401-UART i/o.
 */
irqreturn_t snd_mpu401_uart_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct snd_mpu401 *mpu = dev_id;
	
	if (mpu == NULL)
		return IRQ_NONE;
	_snd_mpu401_uart_interrupt(mpu);
	return IRQ_HANDLED;
}

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
	mpu->timer_invoked |= input ? MPU401_MODE_INPUT_TIMER : MPU401_MODE_OUTPUT_TIMER;
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
		mpu->timer_invoked &= input ? ~MPU401_MODE_INPUT_TIMER : ~MPU401_MODE_OUTPUT_TIMER;
		if (! mpu->timer_invoked)
			del_timer(&mpu->timer);
	}
	spin_unlock_irqrestore (&mpu->timer_lock, flags);
}

/*

 */

static void snd_mpu401_uart_cmd(struct snd_mpu401 * mpu, unsigned char cmd, int ack)
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
		for (timeout = 1000; timeout > 0 && !snd_mpu401_output_ready(mpu); timeout--)
			udelay(10);
#ifdef CONFIG_SND_DEBUG
		if (!timeout)
			snd_printk("cmd: tx timeout (status = 0x%x)\n", mpu->read(mpu, MPU401C(mpu)));
#endif
	}
	mpu->write(mpu, cmd, MPU401C(mpu));
	if (ack) {
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
	} else {
		ok = 1;
	}
	spin_unlock_irqrestore(&mpu->input_lock, flags);
	if (! ok)
		snd_printk("cmd: 0x%x failed at 0x%lx (status = 0x%x, data = 0x%x)\n", cmd, mpu->port, mpu->read(mpu, MPU401C(mpu)), mpu->read(mpu, MPU401D(mpu)));
	// snd_printk("cmd: 0x%x at 0x%lx (status = 0x%x, data = 0x%x)\n", cmd, mpu->port, mpu->read(mpu, MPU401C(mpu)), mpu->read(mpu, MPU401D(mpu)));
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
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 1);
		snd_mpu401_uart_cmd(mpu, MPU401_ENTER_UART, 1);
	}
	mpu->substream_input = substream;
	set_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	return 0;
}

static int snd_mpu401_uart_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;
	int err;

	mpu = substream->rmidi->private_data;
	if (mpu->open_output && (err = mpu->open_output(mpu)) < 0)
		return err;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode)) {
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 1);
		snd_mpu401_uart_cmd(mpu, MPU401_ENTER_UART, 1);
	}
	mpu->substream_output = substream;
	set_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	return 0;
}

static int snd_mpu401_uart_input_close(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;

	mpu = substream->rmidi->private_data;
	clear_bit(MPU401_MODE_BIT_INPUT, &mpu->mode);
	mpu->substream_input = NULL;
	if (! test_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode))
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_input)
		mpu->close_input(mpu);
	return 0;
}

static int snd_mpu401_uart_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_mpu401 *mpu;

	mpu = substream->rmidi->private_data;
	clear_bit(MPU401_MODE_BIT_OUTPUT, &mpu->mode);
	mpu->substream_output = NULL;
	if (! test_bit(MPU401_MODE_BIT_INPUT, &mpu->mode))
		snd_mpu401_uart_cmd(mpu, MPU401_RESET, 0);
	if (mpu->close_output)
		mpu->close_output(mpu);
	return 0;
}

/*
 * trigger input callback
 */
static void snd_mpu401_uart_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_mpu401 *mpu;
	int max = 64;

	mpu = substream->rmidi->private_data;
	if (up) {
		if (! test_and_set_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode)) {
			/* first time - flush FIFO */
			while (max-- > 0)
				mpu->read(mpu, MPU401D(mpu));
			if (mpu->irq < 0)
				snd_mpu401_uart_add_timer(mpu, 1);
		}
		
		/* read data in advance */
		spin_lock_irqsave(&mpu->input_lock, flags);
		snd_mpu401_uart_input_read(mpu);
		spin_unlock_irqrestore(&mpu->input_lock, flags);
	} else {
		if (mpu->irq < 0)
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
		if (snd_mpu401_input_avail(mpu)) {
			byte = mpu->read(mpu, MPU401D(mpu));
			if (test_bit(MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode))
				snd_rawmidi_receive(mpu->substream_input, &byte, 1);
		} else {
			break; /* input not available */
		}
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
	int max = 256, timeout;

	do {
		if (snd_rawmidi_transmit_peek(mpu->substream_output, &byte, 1) == 1) {
			for (timeout = 100; timeout > 0; timeout--) {
				if (snd_mpu401_output_ready(mpu)) {
					mpu->write(mpu, byte, MPU401D(mpu));
					snd_rawmidi_transmit_ack(mpu->substream_output, 1);
					break;
				}
			}
			if (timeout == 0)
				break;	/* Tx FIFO full - try again later */
		} else {
			snd_mpu401_uart_remove_timer (mpu, 0);
			break;	/* no other data - leave the tx loop */
		}
	} while (--max > 0);
}

/*
 * output trigger callback
 */
static void snd_mpu401_uart_output_trigger(struct snd_rawmidi_substream *substream, int up)
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
		snd_mpu401_uart_add_timer(mpu, 0);

		/* output pending data */
		spin_lock_irqsave(&mpu->output_lock, flags);
		snd_mpu401_uart_output_write(mpu);
		spin_unlock_irqrestore(&mpu->output_lock, flags);
	} else {
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
	if (mpu->irq_flags && mpu->irq >= 0)
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
 * @integrated: non-zero if the port was already reserved by the chip
 * @irq: the irq number, -1 if no interrupt for mpu
 * @irq_flags: the irq request flags (SA_XXX), 0 if irq was already reserved.
 * @rrawmidi: the pointer to store the new rawmidi instance
 *
 * Creates a new MPU-401 instance.
 *
 * Note that the rawmidi instance is returned on the rrawmidi argument,
 * not the mpu401 instance itself.  To access to the mpu401 instance,
 * cast from rawmidi->private_data (with struct snd_mpu401 magic-cast).
 *
 * Returns zero if successful, or a negative error code.
 */
int snd_mpu401_uart_new(struct snd_card *card, int device,
			unsigned short hardware,
			unsigned long port, int integrated,
			int irq, int irq_flags,
			struct snd_rawmidi ** rrawmidi)
{
	struct snd_mpu401 *mpu;
	struct snd_rawmidi *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(card, "MPU-401U", device, 1, 1, &rmidi)) < 0)
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
	if (!integrated) {
		int res_size = hardware == MPU401_HW_PC98II ? 4 : 2;
		if ((mpu->res = request_region(port, res_size, "MPU401 UART")) == NULL) {
			snd_printk(KERN_ERR "mpu401_uart: unable to grab port 0x%lx size %d\n", port, res_size);
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
	}
	switch (hardware) {
	case MPU401_HW_AUREAL:
		mpu->write = mpu401_write_mmio;
		mpu->read = mpu401_read_mmio;
		break;
	default:
		mpu->write = mpu401_write_port;
		mpu->read = mpu401_read_port;
		break;
	}
	mpu->port = port;
	if (hardware == MPU401_HW_PC98II)
		mpu->cport = port + 2;
	else
		mpu->cport = port + 1;
	if (irq >= 0 && irq_flags) {
		if (request_irq(irq, snd_mpu401_uart_interrupt, irq_flags, "MPU401 UART", (void *) mpu)) {
			snd_printk(KERN_ERR "mpu401_uart: unable to grab IRQ %d\n", irq);
			snd_device_free(card, rmidi);
			return -EBUSY;
		}
	}
	mpu->irq = irq;
	mpu->irq_flags = irq_flags;
	if (card->shortname[0])
		snprintf(rmidi->name, sizeof(rmidi->name), "%s MIDI", card->shortname);
	else
		sprintf(rmidi->name, "MPU-401 MIDI %d-%d", card->number, device);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_mpu401_uart_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_mpu401_uart_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;
	mpu->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}

EXPORT_SYMBOL(snd_mpu401_uart_interrupt);
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
