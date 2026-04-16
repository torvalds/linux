// SPDX-License-Identifier: LGPL-2.1-or-later
/************************************************************************

This file is part of Echo Digital Audio's generic driver library.
Copyright Echo Digital Audio Corporation (c) 1998 - 2005
All rights reserved
www.echoaudio.com

 Translation from C++ and adaptation for use in ALSA-Driver
 were made by Giuliano Pochini <pochini@shiny.it>

*************************************************************************/

static int update_vmixer_level(struct echoaudio *chip);
static int set_vmixer_gain(struct echoaudio *chip, u16 output,
			   u16 pipe, int gain);


static int init_hw(struct echoaudio *chip, u16 device_id, u16 subdevice_id)
{
	int err;

	if (snd_BUG_ON((subdevice_id & 0xfff0) != INDIGO_IOX))
		return -ENODEV;

	err = init_dsp_comm_page(chip);
	if (err < 0) {
		dev_err(chip->card->dev,
			"init_hw - could not initialize DSP comm page\n");
		return err;
	}

	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = true;
	chip->dsp_code_to_load = FW_INDIGO_IOX_DSP;
	/* Since this card has no ASIC, mark it as loaded so everything
	   works OK */
	chip->asic_loaded = true;
	chip->input_clock_types = ECHO_CLOCK_BIT_INTERNAL;

	err = load_firmware(chip);
	if (err < 0)
		return err;
	chip->bad_board = false;

	return err;
}



static int set_mixer_defaults(struct echoaudio *chip)
{
	return init_line_levels(chip);
}
