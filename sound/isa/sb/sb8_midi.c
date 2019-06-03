// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of SoundBlaster cards - MIDI interface
 *
 * --
 *
 * Sun May  9 22:54:38 BST 1999 George David Morrison <gdm@gedamo.demon.co.uk>
 *   Fixed typo in snd_sb8dsp_midi_new_device which prevented midi from 
 *   working.
 *
 * Sun May 11 12:34:56 UTC 2003 Clemens Ladisch <clemens@ladisch.de>
 *   Added full duplex UART mode for DSP version 2.0 and later.
 */

#include <linux/io.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/sb.h>


irqreturn_t snd_sb8dsp_midi_interrupt(struct snd_sb *chip)
{
	struct snd_rawmidi *rmidi;
	int max = 64;
	char byte;

	if (!chip)
		return IRQ_NONE;
	
	rmidi = chip->rmidi;
	if (!rmidi) {
		inb(SBP(chip, DATA_AVAIL));	/* ack interrupt */
		return IRQ_NONE;
	}

	spin_lock(&chip->midi_input_lock);
	while (max-- > 0) {
		if (inb(SBP(chip, DATA_AVAIL)) & 0x80) {
			byte = inb(SBP(chip, READ));
			if (chip->open & SB_OPEN_MIDI_INPUT_TRIGGER) {
				snd_rawmidi_receive(chip->midi_substream_input, &byte, 1);
			}
		}
	}
	spin_unlock(&chip->midi_input_lock);
	return IRQ_HANDLED;
}

static int snd_sb8dsp_midi_input_open(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip;
	unsigned int valid_open_flags;

	chip = substream->rmidi->private_data;
	valid_open_flags = chip->hardware >= SB_HW_20
		? SB_OPEN_MIDI_OUTPUT | SB_OPEN_MIDI_OUTPUT_TRIGGER : 0;
	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->open & ~valid_open_flags) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	chip->open |= SB_OPEN_MIDI_INPUT;
	chip->midi_substream_input = substream;
	if (!(chip->open & SB_OPEN_MIDI_OUTPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
		if (chip->hardware >= SB_HW_20)
			snd_sbdsp_command(chip, SB_DSP_MIDI_UART_IRQ);
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_output_open(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip;
	unsigned int valid_open_flags;

	chip = substream->rmidi->private_data;
	valid_open_flags = chip->hardware >= SB_HW_20
		? SB_OPEN_MIDI_INPUT | SB_OPEN_MIDI_INPUT_TRIGGER : 0;
	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->open & ~valid_open_flags) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	chip->open |= SB_OPEN_MIDI_OUTPUT;
	chip->midi_substream_output = substream;
	if (!(chip->open & SB_OPEN_MIDI_INPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
		if (chip->hardware >= SB_HW_20)
			snd_sbdsp_command(chip, SB_DSP_MIDI_UART_IRQ);
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_input_close(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip;

	chip = substream->rmidi->private_data;
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->open &= ~(SB_OPEN_MIDI_INPUT | SB_OPEN_MIDI_INPUT_TRIGGER);
	chip->midi_substream_input = NULL;
	if (!(chip->open & SB_OPEN_MIDI_OUTPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_output_close(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip;

	chip = substream->rmidi->private_data;
	del_timer_sync(&chip->midi_timer);
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->open &= ~(SB_OPEN_MIDI_OUTPUT | SB_OPEN_MIDI_OUTPUT_TRIGGER);
	chip->midi_substream_output = NULL;
	if (!(chip->open & SB_OPEN_MIDI_INPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static void snd_sb8dsp_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_sb *chip;

	chip = substream->rmidi->private_data;
	spin_lock_irqsave(&chip->open_lock, flags);
	if (up) {
		if (!(chip->open & SB_OPEN_MIDI_INPUT_TRIGGER)) {
			if (chip->hardware < SB_HW_20)
				snd_sbdsp_command(chip, SB_DSP_MIDI_INPUT_IRQ);
			chip->open |= SB_OPEN_MIDI_INPUT_TRIGGER;
		}
	} else {
		if (chip->open & SB_OPEN_MIDI_INPUT_TRIGGER) {
			if (chip->hardware < SB_HW_20)
				snd_sbdsp_command(chip, SB_DSP_MIDI_INPUT_IRQ);
			chip->open &= ~SB_OPEN_MIDI_INPUT_TRIGGER;
		}
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);
}

static void snd_sb8dsp_midi_output_write(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip;
	char byte;
	int max = 32;

	/* how big is Tx FIFO? */
	chip = substream->rmidi->private_data;
	while (max-- > 0) {
		spin_lock_irqsave(&chip->open_lock, flags);
		if (snd_rawmidi_transmit_peek(substream, &byte, 1) != 1) {
			chip->open &= ~SB_OPEN_MIDI_OUTPUT_TRIGGER;
			del_timer(&chip->midi_timer);
			spin_unlock_irqrestore(&chip->open_lock, flags);
			break;
		}
		if (chip->hardware >= SB_HW_20) {
			int timeout = 8;
			while ((inb(SBP(chip, STATUS)) & 0x80) != 0 && --timeout > 0)
				;
			if (timeout == 0) {
				/* Tx FIFO full - try again later */
				spin_unlock_irqrestore(&chip->open_lock, flags);
				break;
			}
			outb(byte, SBP(chip, WRITE));
		} else {
			snd_sbdsp_command(chip, SB_DSP_MIDI_OUTPUT);
			snd_sbdsp_command(chip, byte);
		}
		snd_rawmidi_transmit_ack(substream, 1);
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
}

static void snd_sb8dsp_midi_output_timer(struct timer_list *t)
{
	struct snd_sb *chip = from_timer(chip, t, midi_timer);
	struct snd_rawmidi_substream *substream = chip->midi_substream_output;
	unsigned long flags;

	spin_lock_irqsave(&chip->open_lock, flags);
	mod_timer(&chip->midi_timer, 1 + jiffies);
	spin_unlock_irqrestore(&chip->open_lock, flags);	
	snd_sb8dsp_midi_output_write(substream);
}

static void snd_sb8dsp_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_sb *chip;

	chip = substream->rmidi->private_data;
	spin_lock_irqsave(&chip->open_lock, flags);
	if (up) {
		if (!(chip->open & SB_OPEN_MIDI_OUTPUT_TRIGGER)) {
			mod_timer(&chip->midi_timer, 1 + jiffies);
			chip->open |= SB_OPEN_MIDI_OUTPUT_TRIGGER;
		}
	} else {
		if (chip->open & SB_OPEN_MIDI_OUTPUT_TRIGGER) {
			chip->open &= ~SB_OPEN_MIDI_OUTPUT_TRIGGER;
		}
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);

	if (up)
		snd_sb8dsp_midi_output_write(substream);
}

static const struct snd_rawmidi_ops snd_sb8dsp_midi_output =
{
	.open =		snd_sb8dsp_midi_output_open,
	.close =	snd_sb8dsp_midi_output_close,
	.trigger =	snd_sb8dsp_midi_output_trigger,
};

static const struct snd_rawmidi_ops snd_sb8dsp_midi_input =
{
	.open =		snd_sb8dsp_midi_input_open,
	.close =	snd_sb8dsp_midi_input_close,
	.trigger =	snd_sb8dsp_midi_input_trigger,
};

int snd_sb8dsp_midi(struct snd_sb *chip, int device)
{
	struct snd_rawmidi *rmidi;
	int err;

	if ((err = snd_rawmidi_new(chip->card, "SB8 MIDI", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "SB8 MIDI");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_sb8dsp_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_sb8dsp_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT;
	if (chip->hardware >= SB_HW_20)
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = chip;
	timer_setup(&chip->midi_timer, snd_sb8dsp_midi_output_timer, 0);
	chip->rmidi = rmidi;
	return 0;
}
