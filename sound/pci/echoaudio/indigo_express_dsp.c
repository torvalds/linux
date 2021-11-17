/************************************************************************

This file is part of Echo Digital Audio's generic driver library.
Copyright Echo Digital Audio Corporation (c) 1998 - 2005
All rights reserved
www.echoaudio.com

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*************************************************************************

 Translation from C++ and adaptation for use in ALSA-Driver
 were made by Giuliano Pochini <pochini@shiny.it>

*************************************************************************/

static int set_sample_rate(struct echoaudio *chip, u32 rate)
{
	u32 clock, control_reg, old_control_reg;

	if (wait_handshake(chip))
		return -EIO;

	old_control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg = old_control_reg & ~INDIGO_EXPRESS_CLOCK_MASK;

	switch (rate) {
	case 32000:
		clock = INDIGO_EXPRESS_32000;
		break;
	case 44100:
		clock = INDIGO_EXPRESS_44100;
		break;
	case 48000:
		clock = INDIGO_EXPRESS_48000;
		break;
	case 64000:
		clock = INDIGO_EXPRESS_32000|INDIGO_EXPRESS_DOUBLE_SPEED;
		break;
	case 88200:
		clock = INDIGO_EXPRESS_44100|INDIGO_EXPRESS_DOUBLE_SPEED;
		break;
	case 96000:
		clock = INDIGO_EXPRESS_48000|INDIGO_EXPRESS_DOUBLE_SPEED;
		break;
	default:
		return -EINVAL;
	}

	control_reg |= clock;
	if (control_reg != old_control_reg) {
		dev_dbg(chip->card->dev,
			"set_sample_rate: %d clock %d\n", rate, clock);
		chip->comm_page->control_register = cpu_to_le32(control_reg);
		chip->sample_rate = rate;
		clear_handshake(chip);
		return send_vector(chip, DSP_VC_UPDATE_CLOCKS);
	}
	return 0;
}



/* This function routes the sound from a virtual channel to a real output */
static int set_vmixer_gain(struct echoaudio *chip, u16 output, u16 pipe,
			   int gain)
{
	int index;

	if (snd_BUG_ON(pipe >= num_pipes_out(chip) ||
		       output >= num_busses_out(chip)))
		return -EINVAL;

	if (wait_handshake(chip))
		return -EIO;

	chip->vmixer_gain[output][pipe] = gain;
	index = output * num_pipes_out(chip) + pipe;
	chip->comm_page->vmixer[index] = gain;

	dev_dbg(chip->card->dev,
		"set_vmixer_gain: pipe %d, out %d = %d\n", pipe, output, gain);
	return 0;
}



/* Tell the DSP to read and update virtual mixer levels in comm page. */
static int update_vmixer_level(struct echoaudio *chip)
{
	if (wait_handshake(chip))
		return -EIO;
	clear_handshake(chip);
	return send_vector(chip, DSP_VC_SET_VMIXER_GAIN);
}



static u32 detect_input_clocks(const struct echoaudio *chip)
{
	return ECHO_CLOCK_BIT_INTERNAL;
}



/* The IndigoIO has no ASIC. Just do nothing */
static int load_asic(struct echoaudio *chip)
{
	return 0;
}
