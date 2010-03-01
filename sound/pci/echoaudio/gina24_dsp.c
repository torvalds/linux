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


static int write_control_reg(struct echoaudio *chip, u32 value, char force);
static int set_input_clock(struct echoaudio *chip, u16 clock);
static int set_professional_spdif(struct echoaudio *chip, char prof);
static int set_digital_mode(struct echoaudio *chip, u8 mode);
static int load_asic_generic(struct echoaudio *chip, u32 cmd, short asic);
static int check_asic_status(struct echoaudio *chip);


static int init_hw(struct echoaudio *chip, u16 device_id, u16 subdevice_id)
{
	int err;

	DE_INIT(("init_hw() - Gina24\n"));
	if (snd_BUG_ON((subdevice_id & 0xfff0) != GINA24))
		return -ENODEV;

	if ((err = init_dsp_comm_page(chip))) {
		DE_INIT(("init_hw - could not initialize DSP comm page\n"));
		return err;
	}

	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = TRUE;
	chip->input_clock_types =
		ECHO_CLOCK_BIT_INTERNAL | ECHO_CLOCK_BIT_SPDIF |
		ECHO_CLOCK_BIT_ESYNC | ECHO_CLOCK_BIT_ESYNC96 |
		ECHO_CLOCK_BIT_ADAT;

	/* Gina24 comes in both '301 and '361 flavors */
	if (chip->device_id == DEVICE_ID_56361) {
		chip->dsp_code_to_load = FW_GINA24_361_DSP;
		chip->digital_modes =
			ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_RCA |
			ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_OPTICAL |
			ECHOCAPS_HAS_DIGITAL_MODE_ADAT;
	} else {
		chip->dsp_code_to_load = FW_GINA24_301_DSP;
		chip->digital_modes =
			ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_RCA |
			ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_OPTICAL |
			ECHOCAPS_HAS_DIGITAL_MODE_ADAT |
			ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_CDROM;
	}

	if ((err = load_firmware(chip)) < 0)
		return err;
	chip->bad_board = FALSE;

	DE_INIT(("init_hw done\n"));
	return err;
}



static int set_mixer_defaults(struct echoaudio *chip)
{
	chip->digital_mode = DIGITAL_MODE_SPDIF_RCA;
	chip->professional_spdif = FALSE;
	chip->digital_in_automute = TRUE;
	return init_line_levels(chip);
}



static u32 detect_input_clocks(const struct echoaudio *chip)
{
	u32 clocks_from_dsp, clock_bits;

	/* Map the DSP clock detect bits to the generic driver clock
	   detect bits */
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	clock_bits = ECHO_CLOCK_BIT_INTERNAL;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_SPDIF)
		clock_bits |= ECHO_CLOCK_BIT_SPDIF;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_ADAT)
		clock_bits |= ECHO_CLOCK_BIT_ADAT;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_ESYNC)
		clock_bits |= ECHO_CLOCK_BIT_ESYNC | ECHO_CLOCK_BIT_ESYNC96;

	return clock_bits;
}



/* Gina24 has an ASIC on the PCI card which must be loaded for anything
interesting to happen. */
static int load_asic(struct echoaudio *chip)
{
	u32 control_reg;
	int err;
	short asic;

	if (chip->asic_loaded)
		return 1;

	/* Give the DSP a few milliseconds to settle down */
	mdelay(10);

	/* Pick the correct ASIC for '301 or '361 Gina24 */
	if (chip->device_id == DEVICE_ID_56361)
		asic = FW_GINA24_361_ASIC;
	else
		asic = FW_GINA24_301_ASIC;

	err = load_asic_generic(chip, DSP_FNC_LOAD_GINA24_ASIC, asic);
	if (err < 0)
		return err;

	chip->asic_code = asic;

	/* Now give the new ASIC a little time to set up */
	mdelay(10);
	/* See if it worked */
	err = check_asic_status(chip);

	/* Set up the control register if the load succeeded -
	   48 kHz, internal clock, S/PDIF RCA mode */
	if (!err) {
		control_reg = GML_CONVERTER_ENABLE | GML_48KHZ;
		err = write_control_reg(chip, control_reg, TRUE);
	}
	DE_INIT(("load_asic() done\n"));
	return err;
}



static int set_sample_rate(struct echoaudio *chip, u32 rate)
{
	u32 control_reg, clock;

	if (snd_BUG_ON(rate >= 50000 &&
		       chip->digital_mode == DIGITAL_MODE_ADAT))
		return -EINVAL;

	/* Only set the clock for internal mode. */
	if (chip->input_clock != ECHO_CLOCK_INTERNAL) {
		DE_ACT(("set_sample_rate: Cannot set sample rate - "
			"clock not set to CLK_CLOCKININTERNAL\n"));
		/* Save the rate anyhow */
		chip->comm_page->sample_rate = cpu_to_le32(rate);
		chip->sample_rate = rate;
		return 0;
	}

	clock = 0;

	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= GML_CLOCK_CLEAR_MASK & GML_SPDIF_RATE_CLEAR_MASK;

	switch (rate) {
	case 96000:
		clock = GML_96KHZ;
		break;
	case 88200:
		clock = GML_88KHZ;
		break;
	case 48000:
		clock = GML_48KHZ | GML_SPDIF_SAMPLE_RATE1;
		break;
	case 44100:
		clock = GML_44KHZ;
		/* Professional mode ? */
		if (control_reg & GML_SPDIF_PRO_MODE)
			clock |= GML_SPDIF_SAMPLE_RATE0;
		break;
	case 32000:
		clock = GML_32KHZ | GML_SPDIF_SAMPLE_RATE0 |
			GML_SPDIF_SAMPLE_RATE1;
		break;
	case 22050:
		clock = GML_22KHZ;
		break;
	case 16000:
		clock = GML_16KHZ;
		break;
	case 11025:
		clock = GML_11KHZ;
		break;
	case 8000:
		clock = GML_8KHZ;
		break;
	default:
		DE_ACT(("set_sample_rate: %d invalid!\n", rate));
		return -EINVAL;
	}

	control_reg |= clock;

	chip->comm_page->sample_rate = cpu_to_le32(rate);	/* ignored by the DSP */
	chip->sample_rate = rate;
	DE_ACT(("set_sample_rate: %d clock %d\n", rate, clock));

	return write_control_reg(chip, control_reg, FALSE);
}



static int set_input_clock(struct echoaudio *chip, u16 clock)
{
	u32 control_reg, clocks_from_dsp;

	DE_ACT(("set_input_clock:\n"));

	/* Mask off the clock select bits */
	control_reg = le32_to_cpu(chip->comm_page->control_register) &
		GML_CLOCK_CLEAR_MASK;
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	switch (clock) {
	case ECHO_CLOCK_INTERNAL:
		DE_ACT(("Set Gina24 clock to INTERNAL\n"));
		chip->input_clock = ECHO_CLOCK_INTERNAL;
		return set_sample_rate(chip, chip->sample_rate);
	case ECHO_CLOCK_SPDIF:
		if (chip->digital_mode == DIGITAL_MODE_ADAT)
			return -EAGAIN;
		DE_ACT(("Set Gina24 clock to SPDIF\n"));
		control_reg |= GML_SPDIF_CLOCK;
		if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_SPDIF96)
			control_reg |= GML_DOUBLE_SPEED_MODE;
		else
			control_reg &= ~GML_DOUBLE_SPEED_MODE;
		break;
	case ECHO_CLOCK_ADAT:
		if (chip->digital_mode != DIGITAL_MODE_ADAT)
			return -EAGAIN;
		DE_ACT(("Set Gina24 clock to ADAT\n"));
		control_reg |= GML_ADAT_CLOCK;
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		break;
	case ECHO_CLOCK_ESYNC:
		DE_ACT(("Set Gina24 clock to ESYNC\n"));
		control_reg |= GML_ESYNC_CLOCK;
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		break;
	case ECHO_CLOCK_ESYNC96:
		DE_ACT(("Set Gina24 clock to ESYNC96\n"));
		control_reg |= GML_ESYNC_CLOCK | GML_DOUBLE_SPEED_MODE;
		break;
	default:
		DE_ACT(("Input clock 0x%x not supported for Gina24\n", clock));
		return -EINVAL;
	}

	chip->input_clock = clock;
	return write_control_reg(chip, control_reg, TRUE);
}



static int dsp_set_digital_mode(struct echoaudio *chip, u8 mode)
{
	u32 control_reg;
	int err, incompatible_clock;

	/* Set clock to "internal" if it's not compatible with the new mode */
	incompatible_clock = FALSE;
	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
	case DIGITAL_MODE_SPDIF_CDROM:
	case DIGITAL_MODE_SPDIF_RCA:
		if (chip->input_clock == ECHO_CLOCK_ADAT)
			incompatible_clock = TRUE;
		break;
	case DIGITAL_MODE_ADAT:
		if (chip->input_clock == ECHO_CLOCK_SPDIF)
			incompatible_clock = TRUE;
		break;
	default:
		DE_ACT(("Digital mode not supported: %d\n", mode));
		return -EINVAL;
	}

	spin_lock_irq(&chip->lock);

	if (incompatible_clock) {	/* Switch to 48KHz, internal */
		chip->sample_rate = 48000;
		set_input_clock(chip, ECHO_CLOCK_INTERNAL);
	}

	/* Clear the current digital mode */
	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= GML_DIGITAL_MODE_CLEAR_MASK;

	/* Tweak the control reg */
	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
		control_reg |= GML_SPDIF_OPTICAL_MODE;
		break;
	case DIGITAL_MODE_SPDIF_CDROM:
		/* '361 Gina24 cards do not have the S/PDIF CD-ROM mode */
		if (chip->device_id == DEVICE_ID_56301)
			control_reg |= GML_SPDIF_CDROM_MODE;
		break;
	case DIGITAL_MODE_SPDIF_RCA:
		/* GML_SPDIF_OPTICAL_MODE bit cleared */
		break;
	case DIGITAL_MODE_ADAT:
		control_reg |= GML_ADAT_MODE;
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		break;
	}

	err = write_control_reg(chip, control_reg, TRUE);
	spin_unlock_irq(&chip->lock);
	if (err < 0)
		return err;
	chip->digital_mode = mode;

	DE_ACT(("set_digital_mode to %d\n", chip->digital_mode));
	return incompatible_clock;
}
