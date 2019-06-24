// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for the GF1 MIDI interface - like UART 6850
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>

static void snd_gf1_interrupt_midi_in(struct snd_gus_card * gus)
{
	int count;
	unsigned char stat, data, byte;
	unsigned long flags;

	count = 10;
	while (count) {
		spin_lock_irqsave(&gus->uart_cmd_lock, flags);
		stat = snd_gf1_uart_stat(gus);
		if (!(stat & 0x01)) {	/* data in Rx FIFO? */
			spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
			count--;
			continue;
		}
		count = 100;	/* arm counter to new value */
		data = snd_gf1_uart_get(gus);
		if (!(gus->gf1.uart_cmd & 0x80)) {
			spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
			continue;
		}			
		if (stat & 0x10) {	/* framing error */
			gus->gf1.uart_framing++;
			spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
			continue;
		}
		byte = snd_gf1_uart_get(gus);
		spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
		snd_rawmidi_receive(gus->midi_substream_input, &byte, 1);
		if (stat & 0x20) {
			gus->gf1.uart_overrun++;
		}
	}
}

static void snd_gf1_interrupt_midi_out(struct snd_gus_card * gus)
{
	char byte;
	unsigned long flags;

	/* try unlock output */
	if (snd_gf1_uart_stat(gus) & 0x01)
		snd_gf1_interrupt_midi_in(gus);

	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (snd_gf1_uart_stat(gus) & 0x02) {	/* Tx FIFO free? */
		if (snd_rawmidi_transmit(gus->midi_substream_output, &byte, 1) != 1) {	/* no other bytes or error */
			snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd & ~0x20); /* disable Tx interrupt */
		} else {
			snd_gf1_uart_put(gus, byte);
		}
	}
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
}

static void snd_gf1_uart_reset(struct snd_gus_card * gus, int close)
{
	snd_gf1_uart_cmd(gus, 0x03);	/* reset */
	if (!close && gus->uart_enable) {
		udelay(160);
		snd_gf1_uart_cmd(gus, 0x00);	/* normal operations */
	}
}

static int snd_gf1_uart_output_open(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_gus_card *gus;

	gus = substream->rmidi->private_data;
	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (!(gus->gf1.uart_cmd & 0x80)) {	/* input active? */
		snd_gf1_uart_reset(gus, 0);
	}
	gus->gf1.interrupt_handler_midi_out = snd_gf1_interrupt_midi_out;
	gus->midi_substream_output = substream;
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
#if 0
	snd_printk(KERN_DEBUG "write init - cmd = 0x%x, stat = 0x%x\n", gus->gf1.uart_cmd, snd_gf1_uart_stat(gus));
#endif
	return 0;
}

static int snd_gf1_uart_input_open(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_gus_card *gus;
	int i;

	gus = substream->rmidi->private_data;
	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (gus->gf1.interrupt_handler_midi_out != snd_gf1_interrupt_midi_out) {
		snd_gf1_uart_reset(gus, 0);
	}
	gus->gf1.interrupt_handler_midi_in = snd_gf1_interrupt_midi_in;
	gus->midi_substream_input = substream;
	if (gus->uart_enable) {
		for (i = 0; i < 1000 && (snd_gf1_uart_stat(gus) & 0x01); i++)
			snd_gf1_uart_get(gus);	/* clean Rx */
		if (i >= 1000)
			snd_printk(KERN_ERR "gus midi uart init read - cleanup error\n");
	}
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
#if 0
	snd_printk(KERN_DEBUG
		   "read init - enable = %i, cmd = 0x%x, stat = 0x%x\n",
		   gus->uart_enable, gus->gf1.uart_cmd, snd_gf1_uart_stat(gus));
	snd_printk(KERN_DEBUG
		   "[0x%x] reg (ctrl/status) = 0x%x, reg (data) = 0x%x "
		   "(page = 0x%x)\n",
		   gus->gf1.port + 0x100, inb(gus->gf1.port + 0x100),
		   inb(gus->gf1.port + 0x101), inb(gus->gf1.port + 0x102));
#endif
	return 0;
}

static int snd_gf1_uart_output_close(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_gus_card *gus;

	gus = substream->rmidi->private_data;
	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (gus->gf1.interrupt_handler_midi_in != snd_gf1_interrupt_midi_in)
		snd_gf1_uart_reset(gus, 1);
	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_MIDI_OUT);
	gus->midi_substream_output = NULL;
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
	return 0;
}

static int snd_gf1_uart_input_close(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_gus_card *gus;

	gus = substream->rmidi->private_data;
	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (gus->gf1.interrupt_handler_midi_out != snd_gf1_interrupt_midi_out)
		snd_gf1_uart_reset(gus, 1);
	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_MIDI_IN);
	gus->midi_substream_input = NULL;
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
	return 0;
}

static void snd_gf1_uart_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_gus_card *gus;
	unsigned long flags;

	gus = substream->rmidi->private_data;

	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (up) {
		if ((gus->gf1.uart_cmd & 0x80) == 0)
			snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd | 0x80); /* enable Rx interrupts */
	} else {
		if (gus->gf1.uart_cmd & 0x80)
			snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd & ~0x80); /* disable Rx interrupts */
	}
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
}

static void snd_gf1_uart_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_gus_card *gus;
	char byte;
	int timeout;

	gus = substream->rmidi->private_data;

	spin_lock_irqsave(&gus->uart_cmd_lock, flags);
	if (up) {
		if ((gus->gf1.uart_cmd & 0x20) == 0) {
			spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
			/* wait for empty Rx - Tx is probably unlocked */
			timeout = 10000;
			while (timeout-- > 0 && snd_gf1_uart_stat(gus) & 0x01);
			/* Tx FIFO free? */
			spin_lock_irqsave(&gus->uart_cmd_lock, flags);
			if (gus->gf1.uart_cmd & 0x20) {
				spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
				return;
			}
			if (snd_gf1_uart_stat(gus) & 0x02) {
				if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
					return;
				}
				snd_gf1_uart_put(gus, byte);
			}
			snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd | 0x20);	/* enable Tx interrupt */
		}
	} else {
		if (gus->gf1.uart_cmd & 0x20)
			snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd & ~0x20);
	}
	spin_unlock_irqrestore(&gus->uart_cmd_lock, flags);
}

static const struct snd_rawmidi_ops snd_gf1_uart_output =
{
	.open =		snd_gf1_uart_output_open,
	.close =	snd_gf1_uart_output_close,
	.trigger =	snd_gf1_uart_output_trigger,
};

static const struct snd_rawmidi_ops snd_gf1_uart_input =
{
	.open =		snd_gf1_uart_input_open,
	.close =	snd_gf1_uart_input_close,
	.trigger =	snd_gf1_uart_input_trigger,
};

int snd_gf1_rawmidi_new(struct snd_gus_card *gus, int device)
{
	struct snd_rawmidi *rmidi;
	int err;

	if ((err = snd_rawmidi_new(gus->card, "GF1", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, gus->interwave ? "AMD InterWave" : "GF1");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_gf1_uart_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_gf1_uart_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = gus;
	gus->midi_uart = rmidi;
	return err;
}
