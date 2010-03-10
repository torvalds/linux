/****************************************************************************

   Copyright Echo Digital Audio Corporation (c) 1998 - 2004
   All rights reserved
   www.echoaudio.com

   This file is part of Echo Digital Audio's generic driver library.

   Echo Digital Audio's generic driver library is free software;
   you can redistribute it and/or modify it under the terms of
   the GNU General Public License as published by the Free Software
   Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA  02111-1307, USA.

   *************************************************************************

 Translation from C++ and adaptation for use in ALSA-Driver
 were made by Giuliano Pochini <pochini@shiny.it>

****************************************************************************/


static int set_vmixer_gain(struct echoaudio *chip, u16 output, u16 pipe,
			   int gain);
static int update_vmixer_level(struct echoaudio *chip);


static int init_hw(struct echoaudio *chip, u16 device_id, u16 subdevice_id)
{
	int err;

	DE_INIT(("init_hw() - Indigo DJ\n"));
	if (snd_BUG_ON((subdevice_id & 0xfff0) != INDIGO_DJ))
		return -ENODEV;

	if ((err = init_dsp_comm_page(chip))) {
		DE_INIT(("init_hw - could not initialize DSP comm page\n"));
		return err;
	}

	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = TRUE;
	chip->dsp_code_to_load = FW_INDIGO_DJ_DSP;
	/* Since this card has no ASIC, mark it as loaded so everything
	   works OK */
	chip->asic_loaded = TRUE;
	chip->input_clock_types = ECHO_CLOCK_BIT_INTERNAL;

	if ((err = load_firmware(chip)) < 0)
		return err;
	chip->bad_board = FALSE;

	DE_INIT(("init_hw done\n"));
	return err;
}



static int set_mixer_defaults(struct echoaudio *chip)
{
	return init_line_levels(chip);
}



static u32 detect_input_clocks(const struct echoaudio *chip)
{
	return ECHO_CLOCK_BIT_INTERNAL;
}



/* The IndigoDJ has no ASIC. Just do nothing */
static int load_asic(struct echoaudio *chip)
{
	return 0;
}



static int set_sample_rate(struct echoaudio *chip, u32 rate)
{
	u32 control_reg;

	switch (rate) {
	case 96000:
		control_reg = MIA_96000;
		break;
	case 88200:
		control_reg = MIA_88200;
		break;
	case 48000:
		control_reg = MIA_48000;
		break;
	case 44100:
		control_reg = MIA_44100;
		break;
	case 32000:
		control_reg = MIA_32000;
		break;
	default:
		DE_ACT(("set_sample_rate: %d invalid!\n", rate));
		return -EINVAL;
	}

	/* Set the control register if it has changed */
	if (control_reg != le32_to_cpu(chip->comm_page->control_register)) {
		if (wait_handshake(chip))
			return -EIO;

		chip->comm_page->sample_rate = cpu_to_le32(rate);	/* ignored by the DSP */
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

	DE_ACT(("set_vmixer_gain: pipe %d, out %d = %d\n", pipe, output, gain));
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

