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

static int load_asic(struct echoaudio *chip);
static int dsp_set_digital_mode(struct echoaudio *chip, u8 mode);
static int set_digital_mode(struct echoaudio *chip, u8 mode);
static int check_asic_status(struct echoaudio *chip);
static int set_sample_rate(struct echoaudio *chip, u32 rate);
static int set_input_clock(struct echoaudio *chip, u16 clock);
static int set_professional_spdif(struct echoaudio *chip, char prof);
static int set_phantom_power(struct echoaudio *chip, char on);
static int write_control_reg(struct echoaudio *chip, u32 ctl, u32 frq,
			     char force);

#include <linux/interrupt.h>

static int init_hw(struct echoaudio *chip, u16 device_id, u16 subdevice_id)
{
	int err;

	local_irq_enable();
	DE_INIT(("init_hw() - Echo3G\n"));
	if (snd_BUG_ON((subdevice_id & 0xfff0) != ECHO3G))
		return -ENODEV;

	if ((err = init_dsp_comm_page(chip))) {
		DE_INIT(("init_hw - could not initialize DSP comm page\n"));
		return err;
	}

	chip->comm_page->e3g_frq_register =
		cpu_to_le32((E3G_MAGIC_NUMBER / 48000) - 2);
	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = TRUE;
	chip->has_midi = TRUE;
	chip->dsp_code_to_load = &card_fw[FW_ECHO3G_DSP];

	/* Load the DSP code and the ASIC on the PCI card and get
	what type of external box is attached */
	err = load_firmware(chip);

	if (err < 0) {
		return err;
	} else if (err == E3G_GINA3G_BOX_TYPE) {
		chip->input_clock_types =	ECHO_CLOCK_BIT_INTERNAL |
						ECHO_CLOCK_BIT_SPDIF |
						ECHO_CLOCK_BIT_ADAT;
		chip->card_name = "Gina3G";
		chip->px_digital_out = chip->bx_digital_out = 6;
		chip->px_analog_in = chip->bx_analog_in = 14;
		chip->px_digital_in = chip->bx_digital_in = 16;
		chip->px_num = chip->bx_num = 24;
		chip->has_phantom_power = TRUE;
		chip->hasnt_input_nominal_level = TRUE;
	} else if (err == E3G_LAYLA3G_BOX_TYPE) {
		chip->input_clock_types =	ECHO_CLOCK_BIT_INTERNAL |
						ECHO_CLOCK_BIT_SPDIF |
						ECHO_CLOCK_BIT_ADAT |
						ECHO_CLOCK_BIT_WORD;
		chip->card_name = "Layla3G";
		chip->px_digital_out = chip->bx_digital_out = 8;
		chip->px_analog_in = chip->bx_analog_in = 16;
		chip->px_digital_in = chip->bx_digital_in = 24;
		chip->px_num = chip->bx_num = 32;
	} else {
		return -ENODEV;
	}

	chip->digital_modes =	ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_RCA |
				ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_OPTICAL |
				ECHOCAPS_HAS_DIGITAL_MODE_ADAT;
	chip->digital_mode =	DIGITAL_MODE_SPDIF_RCA;
	chip->professional_spdif = FALSE;
	chip->non_audio_spdif = FALSE;
	chip->bad_board = FALSE;

	if ((err = init_line_levels(chip)) < 0)
		return err;
	err = set_digital_mode(chip, DIGITAL_MODE_SPDIF_RCA);
	if (err < 0)
		return err;
	err = set_phantom_power(chip, 0);
	if (err < 0)
		return err;
	err = set_professional_spdif(chip, TRUE);

	DE_INIT(("init_hw done\n"));
	return err;
}



static int set_phantom_power(struct echoaudio *chip, char on)
{
	u32 control_reg = le32_to_cpu(chip->comm_page->control_register);

	if (on)
		control_reg |= E3G_PHANTOM_POWER;
	else
		control_reg &= ~E3G_PHANTOM_POWER;

	chip->phantom_power = on;
	return write_control_reg(chip, control_reg,
				 le32_to_cpu(chip->comm_page->e3g_frq_register),
				 0);
}
