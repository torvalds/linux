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



/* These functions are common for all "3G" cards */


static int check_asic_status(struct echoaudio *chip)
{
	u32 box_status;

	if (wait_handshake(chip))
		return -EIO;

	chip->comm_page->ext_box_status = cpu_to_le32(E3G_ASIC_NOT_LOADED);
	chip->asic_loaded = false;
	clear_handshake(chip);
	send_vector(chip, DSP_VC_TEST_ASIC);

	if (wait_handshake(chip)) {
		chip->dsp_code = NULL;
		return -EIO;
	}

	box_status = le32_to_cpu(chip->comm_page->ext_box_status);
	dev_dbg(chip->card->dev, "box_status=%x\n", box_status);
	if (box_status == E3G_ASIC_NOT_LOADED)
		return -ENODEV;

	chip->asic_loaded = true;
	return box_status & E3G_BOX_TYPE_MASK;
}



static inline u32 get_frq_reg(struct echoaudio *chip)
{
	return le32_to_cpu(chip->comm_page->e3g_frq_register);
}



/* Most configuration of 3G cards is accomplished by writing the control
register. write_control_reg sends the new control register value to the DSP. */
static int write_control_reg(struct echoaudio *chip, u32 ctl, u32 frq,
			     char force)
{
	__le32 ctl_reg, frq_reg;

	if (wait_handshake(chip))
		return -EIO;

	dev_dbg(chip->card->dev,
		"WriteControlReg: Setting 0x%x, 0x%x\n", ctl, frq);

	ctl_reg = cpu_to_le32(ctl);
	frq_reg = cpu_to_le32(frq);

	if (ctl_reg != chip->comm_page->control_register ||
	    frq_reg != chip->comm_page->e3g_frq_register || force) {
		chip->comm_page->e3g_frq_register = frq_reg;
		chip->comm_page->control_register = ctl_reg;
		clear_handshake(chip);
		return send_vector(chip, DSP_VC_WRITE_CONTROL_REG);
	}

	dev_dbg(chip->card->dev, "WriteControlReg: not written, no change\n");
	return 0;
}



/* Set the digital mode - currently for Gina24, Layla24, Mona, 3G */
static int set_digital_mode(struct echoaudio *chip, u8 mode)
{
	u8 previous_mode;
	int err, i, o;

	/* All audio channels must be closed before changing the digital mode */
	if (snd_BUG_ON(chip->pipe_alloc_mask))
		return -EAGAIN;

	if (snd_BUG_ON(!(chip->digital_modes & (1 << mode))))
		return -EINVAL;

	previous_mode = chip->digital_mode;
	err = dsp_set_digital_mode(chip, mode);

	/* If we successfully changed the digital mode from or to ADAT,
	 * then make sure all output, input and monitor levels are
	 * updated by the DSP comm object. */
	if (err >= 0 && previous_mode != mode &&
	    (previous_mode == DIGITAL_MODE_ADAT || mode == DIGITAL_MODE_ADAT)) {
		spin_lock_irq(&chip->lock);
		for (o = 0; o < num_busses_out(chip); o++)
			for (i = 0; i < num_busses_in(chip); i++)
				set_monitor_gain(chip, o, i,
						 chip->monitor_gain[o][i]);

#ifdef ECHOCARD_HAS_INPUT_GAIN
		for (i = 0; i < num_busses_in(chip); i++)
			set_input_gain(chip, i, chip->input_gain[i]);
		update_input_line_level(chip);
#endif

		for (o = 0; o < num_busses_out(chip); o++)
			set_output_gain(chip, o, chip->output_gain[o]);
		update_output_line_level(chip);
		spin_unlock_irq(&chip->lock);
	}

	return err;
}



static u32 set_spdif_bits(struct echoaudio *chip, u32 control_reg, u32 rate)
{
	control_reg &= E3G_SPDIF_FORMAT_CLEAR_MASK;

	switch (rate) {
	case 32000 :
		control_reg |= E3G_SPDIF_SAMPLE_RATE0 | E3G_SPDIF_SAMPLE_RATE1;
		break;
	case 44100 :
		if (chip->professional_spdif)
			control_reg |= E3G_SPDIF_SAMPLE_RATE0;
		break;
	case 48000 :
		control_reg |= E3G_SPDIF_SAMPLE_RATE1;
		break;
	}

	if (chip->professional_spdif)
		control_reg |= E3G_SPDIF_PRO_MODE;

	if (chip->non_audio_spdif)
		control_reg |= E3G_SPDIF_NOT_AUDIO;

	control_reg |= E3G_SPDIF_24_BIT | E3G_SPDIF_TWO_CHANNEL |
		E3G_SPDIF_COPY_PERMIT;

	return control_reg;
}



/* Set the S/PDIF output format */
static int set_professional_spdif(struct echoaudio *chip, char prof)
{
	u32 control_reg;

	control_reg = le32_to_cpu(chip->comm_page->control_register);
	chip->professional_spdif = prof;
	control_reg = set_spdif_bits(chip, control_reg, chip->sample_rate);
	return write_control_reg(chip, control_reg, get_frq_reg(chip), 0);
}



/* detect_input_clocks() returns a bitmask consisting of all the input clocks
currently connected to the hardware; this changes as the user connects and
disconnects clock inputs. You should use this information to determine which
clocks the user is allowed to select. */
static u32 detect_input_clocks(const struct echoaudio *chip)
{
	u32 clocks_from_dsp, clock_bits;

	/* Map the DSP clock detect bits to the generic driver clock
	 * detect bits */
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	clock_bits = ECHO_CLOCK_BIT_INTERNAL;

	if (clocks_from_dsp & E3G_CLOCK_DETECT_BIT_WORD)
		clock_bits |= ECHO_CLOCK_BIT_WORD;

	switch(chip->digital_mode) {
	case DIGITAL_MODE_SPDIF_RCA:
	case DIGITAL_MODE_SPDIF_OPTICAL:
		if (clocks_from_dsp & E3G_CLOCK_DETECT_BIT_SPDIF)
			clock_bits |= ECHO_CLOCK_BIT_SPDIF;
		break;
	case DIGITAL_MODE_ADAT:
		if (clocks_from_dsp & E3G_CLOCK_DETECT_BIT_ADAT)
			clock_bits |= ECHO_CLOCK_BIT_ADAT;
		break;
	}

	return clock_bits;
}



static int load_asic(struct echoaudio *chip)
{
	int box_type, err;

	if (chip->asic_loaded)
		return 0;

	/* Give the DSP a few milliseconds to settle down */
	mdelay(2);

	err = load_asic_generic(chip, DSP_FNC_LOAD_3G_ASIC, FW_3G_ASIC);
	if (err < 0)
		return err;

	chip->asic_code = FW_3G_ASIC;

	/* Now give the new ASIC some time to set up */
	msleep(1000);
	/* See if it worked */
	box_type = check_asic_status(chip);

	/* Set up the control register if the load succeeded -
	 * 48 kHz, internal clock, S/PDIF RCA mode */
	if (box_type >= 0) {
		err = write_control_reg(chip, E3G_48KHZ,
					E3G_FREQ_REG_DEFAULT, true);
		if (err < 0)
			return err;
	}

	return box_type;
}



static int set_sample_rate(struct echoaudio *chip, u32 rate)
{
	u32 control_reg, clock, base_rate, frq_reg;

	/* Only set the clock for internal mode. */
	if (chip->input_clock != ECHO_CLOCK_INTERNAL) {
		dev_warn(chip->card->dev,
			 "Cannot set sample rate - clock not set to CLK_CLOCKININTERNAL\n");
		/* Save the rate anyhow */
		chip->comm_page->sample_rate = cpu_to_le32(rate);
		chip->sample_rate = rate;
		set_input_clock(chip, chip->input_clock);
		return 0;
	}

	if (snd_BUG_ON(rate >= 50000 &&
		       chip->digital_mode == DIGITAL_MODE_ADAT))
		return -EINVAL;

	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= E3G_CLOCK_CLEAR_MASK;

	switch (rate) {
	case 96000:
		clock = E3G_96KHZ;
		break;
	case 88200:
		clock = E3G_88KHZ;
		break;
	case 48000:
		clock = E3G_48KHZ;
		break;
	case 44100:
		clock = E3G_44KHZ;
		break;
	case 32000:
		clock = E3G_32KHZ;
		break;
	default:
		clock = E3G_CONTINUOUS_CLOCK;
		if (rate > 50000)
			clock |= E3G_DOUBLE_SPEED_MODE;
		break;
	}

	control_reg |= clock;
	control_reg = set_spdif_bits(chip, control_reg, rate);

	base_rate = rate;
	if (base_rate > 50000)
		base_rate /= 2;
	if (base_rate < 32000)
		base_rate = 32000;

	frq_reg = E3G_MAGIC_NUMBER / base_rate - 2;
	if (frq_reg > E3G_FREQ_REG_MAX)
		frq_reg = E3G_FREQ_REG_MAX;

	chip->comm_page->sample_rate = cpu_to_le32(rate);	/* ignored by the DSP */
	chip->sample_rate = rate;
	dev_dbg(chip->card->dev,
		"SetSampleRate: %d clock %x\n", rate, control_reg);

	/* Tell the DSP about it - DSP reads both control reg & freq reg */
	return write_control_reg(chip, control_reg, frq_reg, 0);
}



/* Set the sample clock source to internal, S/PDIF, ADAT */
static int set_input_clock(struct echoaudio *chip, u16 clock)
{
	u32 control_reg, clocks_from_dsp;


	/* Mask off the clock select bits */
	control_reg = le32_to_cpu(chip->comm_page->control_register) &
		E3G_CLOCK_CLEAR_MASK;
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	switch (clock) {
	case ECHO_CLOCK_INTERNAL:
		chip->input_clock = ECHO_CLOCK_INTERNAL;
		return set_sample_rate(chip, chip->sample_rate);
	case ECHO_CLOCK_SPDIF:
		if (chip->digital_mode == DIGITAL_MODE_ADAT)
			return -EAGAIN;
		control_reg |= E3G_SPDIF_CLOCK;
		if (clocks_from_dsp & E3G_CLOCK_DETECT_BIT_SPDIF96)
			control_reg |= E3G_DOUBLE_SPEED_MODE;
		else
			control_reg &= ~E3G_DOUBLE_SPEED_MODE;
		break;
	case ECHO_CLOCK_ADAT:
		if (chip->digital_mode != DIGITAL_MODE_ADAT)
			return -EAGAIN;
		control_reg |= E3G_ADAT_CLOCK;
		control_reg &= ~E3G_DOUBLE_SPEED_MODE;
		break;
	case ECHO_CLOCK_WORD:
		control_reg |= E3G_WORD_CLOCK;
		if (clocks_from_dsp & E3G_CLOCK_DETECT_BIT_WORD96)
			control_reg |= E3G_DOUBLE_SPEED_MODE;
		else
			control_reg &= ~E3G_DOUBLE_SPEED_MODE;
		break;
	default:
		dev_err(chip->card->dev,
			"Input clock 0x%x not supported for Echo3G\n", clock);
		return -EINVAL;
	}

	chip->input_clock = clock;
	return write_control_reg(chip, control_reg, get_frq_reg(chip), 1);
}



static int dsp_set_digital_mode(struct echoaudio *chip, u8 mode)
{
	u32 control_reg;
	int err, incompatible_clock;

	/* Set clock to "internal" if it's not compatible with the new mode */
	incompatible_clock = false;
	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
	case DIGITAL_MODE_SPDIF_RCA:
		if (chip->input_clock == ECHO_CLOCK_ADAT)
			incompatible_clock = true;
		break;
	case DIGITAL_MODE_ADAT:
		if (chip->input_clock == ECHO_CLOCK_SPDIF)
			incompatible_clock = true;
		break;
	default:
		dev_err(chip->card->dev,
			"Digital mode not supported: %d\n", mode);
		return -EINVAL;
	}

	spin_lock_irq(&chip->lock);

	if (incompatible_clock) {
		chip->sample_rate = 48000;
		set_input_clock(chip, ECHO_CLOCK_INTERNAL);
	}

	/* Clear the current digital mode */
	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= E3G_DIGITAL_MODE_CLEAR_MASK;

	/* Tweak the control reg */
	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
		control_reg |= E3G_SPDIF_OPTICAL_MODE;
		break;
	case DIGITAL_MODE_SPDIF_RCA:
		/* E3G_SPDIF_OPTICAL_MODE bit cleared */
		break;
	case DIGITAL_MODE_ADAT:
		control_reg |= E3G_ADAT_MODE;
		control_reg &= ~E3G_DOUBLE_SPEED_MODE;	/* @@ useless */
		break;
	}

	err = write_control_reg(chip, control_reg, get_frq_reg(chip), 1);
	spin_unlock_irq(&chip->lock);
	if (err < 0)
		return err;
	chip->digital_mode = mode;

	dev_dbg(chip->card->dev, "set_digital_mode(%d)\n", chip->digital_mode);
	return incompatible_clock;
}
