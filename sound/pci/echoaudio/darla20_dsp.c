/***************************************************************************

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


static int init_hw(struct echoaudio *chip, u16 device_id, u16 subdevice_id)
{
	int err;

	if (snd_BUG_ON((subdevice_id & 0xfff0) != DARLA20))
		return -ENODEV;

	if ((err = init_dsp_comm_page(chip))) {
		dev_err(chip->card->dev,
			"init_hw: could not initialize DSP comm page\n");
		return err;
	}

	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = TRUE;
	chip->dsp_code_to_load = FW_DARLA20_DSP;
	chip->spdif_status = GD_SPDIF_STATUS_UNDEF;
	chip->clock_state = GD_CLOCK_UNDEF;
	/* Since this card has no ASIC, mark it as loaded so everything
	   works OK */
	chip->asic_loaded = TRUE;
	chip->input_clock_types = ECHO_CLOCK_BIT_INTERNAL;

	if ((err = load_firmware(chip)) < 0)
		return err;
	chip->bad_board = FALSE;

	return err;
}



static int set_mixer_defaults(struct echoaudio *chip)
{
	return init_line_levels(chip);
}



/* The Darla20 has no external clock sources */
static u32 detect_input_clocks(const struct echoaudio *chip)
{
	return ECHO_CLOCK_BIT_INTERNAL;
}



/* The Darla20 has no ASIC. Just do nothing */
static int load_asic(struct echoaudio *chip)
{
	return 0;
}



static int set_sample_rate(struct echoaudio *chip, u32 rate)
{
	u8 clock_state, spdif_status;

	if (wait_handshake(chip))
		return -EIO;

	switch (rate) {
	case 44100:
		clock_state = GD_CLOCK_44;
		spdif_status = GD_SPDIF_STATUS_44;
		break;
	case 48000:
		clock_state = GD_CLOCK_48;
		spdif_status = GD_SPDIF_STATUS_48;
		break;
	default:
		clock_state = GD_CLOCK_NOCHANGE;
		spdif_status = GD_SPDIF_STATUS_NOCHANGE;
		break;
	}

	if (chip->clock_state == clock_state)
		clock_state = GD_CLOCK_NOCHANGE;
	if (spdif_status == chip->spdif_status)
		spdif_status = GD_SPDIF_STATUS_NOCHANGE;

	chip->comm_page->sample_rate = cpu_to_le32(rate);
	chip->comm_page->gd_clock_state = clock_state;
	chip->comm_page->gd_spdif_status = spdif_status;
	chip->comm_page->gd_resampler_state = 3;	/* magic number - should always be 3 */

	/* Save the new audio state if it changed */
	if (clock_state != GD_CLOCK_NOCHANGE)
		chip->clock_state = clock_state;
	if (spdif_status != GD_SPDIF_STATUS_NOCHANGE)
		chip->spdif_status = spdif_status;
	chip->sample_rate = rate;

	clear_handshake(chip);
	return send_vector(chip, DSP_VC_SET_GD_AUDIO_STATE);
}
