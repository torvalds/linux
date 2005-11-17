/*
 * Driver for Digigram VX soundcards
 *
 * IEC958 stuff
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/vx_core.h>
#include "vx_cmd.h"


/*
 * vx_modify_board_clock - tell the board that its clock has been modified
 * @sync: DSP needs to resynchronize its FIFO
 */
static int vx_modify_board_clock(struct vx_core *chip, int sync)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_MODIFY_CLOCK);
	/* Ask the DSP to resynchronize its FIFO. */
	if (sync)
		rmh.Cmd[0] |= CMD_MODIFY_CLOCK_S_BIT;
	return vx_send_msg(chip, &rmh);
}

/*
 * vx_modify_board_inputs - resync audio inputs
 */
static int vx_modify_board_inputs(struct vx_core *chip)
{
	struct vx_rmh rmh;

	vx_init_rmh(&rmh, CMD_RESYNC_AUDIO_INPUTS);
        rmh.Cmd[0] |= 1 << 0; /* reference: AUDIO 0 */
	return vx_send_msg(chip, &rmh);
}

/*
 * vx_read_one_cbit - read one bit from UER config
 * @index: the bit index
 * returns 0 or 1.
 */
static int vx_read_one_cbit(struct vx_core *chip, int index)
{
	unsigned long flags;
	int val;
	spin_lock_irqsave(&chip->lock, flags);
	if (chip->type >= VX_TYPE_VXPOCKET) {
		vx_outb(chip, CSUER, 1); /* read */
		vx_outb(chip, RUER, index & XX_UER_CBITS_OFFSET_MASK);
		val = (vx_inb(chip, RUER) >> 7) & 0x01;
	} else {
		vx_outl(chip, CSUER, 1); /* read */
		vx_outl(chip, RUER, index & XX_UER_CBITS_OFFSET_MASK);
		val = (vx_inl(chip, RUER) >> 7) & 0x01;
	}
	spin_unlock_irqrestore(&chip->lock, flags);
	return val;
}

/*
 * vx_write_one_cbit - write one bit to UER config
 * @index: the bit index
 * @val: bit value, 0 or 1
 */
static void vx_write_one_cbit(struct vx_core *chip, int index, int val)
{
	unsigned long flags;
	val = !!val;	/* 0 or 1 */
	spin_lock_irqsave(&chip->lock, flags);
	if (vx_is_pcmcia(chip)) {
		vx_outb(chip, CSUER, 0); /* write */
		vx_outb(chip, RUER, (val << 7) | (index & XX_UER_CBITS_OFFSET_MASK));
	} else {
		vx_outl(chip, CSUER, 0); /* write */
		vx_outl(chip, RUER, (val << 7) | (index & XX_UER_CBITS_OFFSET_MASK));
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

/*
 * vx_read_uer_status - read the current UER status
 * @mode: pointer to store the UER mode, VX_UER_MODE_XXX
 *
 * returns the frequency of UER, or 0 if not sync,
 * or a negative error code.
 */
static int vx_read_uer_status(struct vx_core *chip, int *mode)
{
	int val, freq;

	/* Default values */
	freq = 0;

	/* Read UER status */
	if (vx_is_pcmcia(chip))
	    val = vx_inb(chip, CSUER);
	else
	    val = vx_inl(chip, CSUER);
	if (val < 0)
		return val;
	/* If clock is present, read frequency */
	if (val & VX_SUER_CLOCK_PRESENT_MASK) {
		switch (val & VX_SUER_FREQ_MASK) {
		case VX_SUER_FREQ_32KHz_MASK:
			freq = 32000;
			break;
		case VX_SUER_FREQ_44KHz_MASK:
			freq = 44100;
			break;
		case VX_SUER_FREQ_48KHz_MASK:
			freq = 48000;
			break;
		}
        }
	if (val & VX_SUER_DATA_PRESENT_MASK)
		/* bit 0 corresponds to consumer/professional bit */
		*mode = vx_read_one_cbit(chip, 0) ?
			VX_UER_MODE_PROFESSIONAL : VX_UER_MODE_CONSUMER;
	else
		*mode = VX_UER_MODE_NOT_PRESENT;

	return freq;
}


/*
 * compute the sample clock value from frequency
 *
 * The formula is as follows:
 *
 *    HexFreq = (dword) ((double) ((double) 28224000 / (double) Frequency))
 *    switch ( HexFreq & 0x00000F00 )
 *    case 0x00000100: ;
 *    case 0x00000200:
 *    case 0x00000300: HexFreq -= 0x00000201 ;
 *    case 0x00000400:
 *    case 0x00000500:
 *    case 0x00000600:
 *    case 0x00000700: HexFreq = (dword) (((double) 28224000 / (double) (Frequency*2)) - 1)
 *    default        : HexFreq = (dword) ((double) 28224000 / (double) (Frequency*4)) - 0x000001FF
 */

static int vx_calc_clock_from_freq(struct vx_core *chip, int freq)
{
	int hexfreq;

	snd_assert(freq > 0, return 0);

	hexfreq = (28224000 * 10) / freq;
	hexfreq = (hexfreq + 5) / 10;

	/* max freq = 55125 Hz */
	snd_assert(hexfreq > 0x00000200, return 0);

	if (hexfreq <= 0x03ff)
		return hexfreq - 0x00000201;
	if (hexfreq <= 0x07ff) 
		return (hexfreq / 2) - 1;
	if (hexfreq <= 0x0fff)
		return (hexfreq / 4) + 0x000001ff;

	return 0x5fe; 	/* min freq = 6893 Hz */
}


/*
 * vx_change_clock_source - change the clock source
 * @source: the new source
 */
static void vx_change_clock_source(struct vx_core *chip, int source)
{
	unsigned long flags;

	/* we mute DAC to prevent clicks */
	vx_toggle_dac_mute(chip, 1);
	spin_lock_irqsave(&chip->lock, flags);
	chip->ops->set_clock_source(chip, source);
	chip->clock_source = source;
	spin_unlock_irqrestore(&chip->lock, flags);
	/* unmute */
	vx_toggle_dac_mute(chip, 0);
}


/*
 * set the internal clock
 */
void vx_set_internal_clock(struct vx_core *chip, unsigned int freq)
{
	int clock;
	unsigned long flags;
	/* Get real clock value */
	clock = vx_calc_clock_from_freq(chip, freq);
	snd_printdd(KERN_DEBUG "set internal clock to 0x%x from freq %d\n", clock, freq);
	spin_lock_irqsave(&chip->lock, flags);
	if (vx_is_pcmcia(chip)) {
		vx_outb(chip, HIFREQ, (clock >> 8) & 0x0f);
		vx_outb(chip, LOFREQ, clock & 0xff);
	} else {
		vx_outl(chip, HIFREQ, (clock >> 8) & 0x0f);
		vx_outl(chip, LOFREQ, clock & 0xff);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}


/*
 * set the iec958 status bits
 * @bits: 32-bit status bits
 */
void vx_set_iec958_status(struct vx_core *chip, unsigned int bits)
{
	int i;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return;

	for (i = 0; i < 32; i++)
		vx_write_one_cbit(chip, i, bits & (1 << i));
}


/*
 * vx_set_clock - change the clock and audio source if necessary
 */
int vx_set_clock(struct vx_core *chip, unsigned int freq)
{
	int src_changed = 0;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return 0;

	/* change the audio source if possible */
	vx_sync_audio_source(chip);

	if (chip->clock_mode == VX_CLOCK_MODE_EXTERNAL ||
	    (chip->clock_mode == VX_CLOCK_MODE_AUTO &&
	     chip->audio_source == VX_AUDIO_SRC_DIGITAL)) {
		if (chip->clock_source != UER_SYNC) {
			vx_change_clock_source(chip, UER_SYNC);
			mdelay(6);
			src_changed = 1;
		}
	} else if (chip->clock_mode == VX_CLOCK_MODE_INTERNAL ||
		   (chip->clock_mode == VX_CLOCK_MODE_AUTO &&
		    chip->audio_source != VX_AUDIO_SRC_DIGITAL)) {
		if (chip->clock_source != INTERNAL_QUARTZ) {
			vx_change_clock_source(chip, INTERNAL_QUARTZ);
			src_changed = 1;
		}
		if (chip->freq == freq)
			return 0;
		vx_set_internal_clock(chip, freq);
		if (src_changed)
			vx_modify_board_inputs(chip);
	}
	if (chip->freq == freq)
		return 0;
	chip->freq = freq;
	vx_modify_board_clock(chip, 1);
	return 0;
}


/*
 * vx_change_frequency - called from interrupt handler
 */
int vx_change_frequency(struct vx_core *chip)
{
	int freq;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return 0;

	if (chip->clock_source == INTERNAL_QUARTZ)
		return 0;
	/*
	 * Read the real UER board frequency
	 */
	freq = vx_read_uer_status(chip, &chip->uer_detected);
	if (freq < 0)
		return freq;
	/*
	 * The frequency computed by the DSP is good and
	 * is different from the previous computed.
	 */
	if (freq == 48000 || freq == 44100 || freq == 32000)
		chip->freq_detected = freq;

	return 0;
}
