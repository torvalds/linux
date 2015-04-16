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

#if PAGE_SIZE < 4096
#error PAGE_SIZE is < 4k
#endif

static int restore_dsp_rettings(struct echoaudio *chip);


/* Some vector commands involve the DSP reading or writing data to and from the
comm page; if you send one of these commands to the DSP, it will complete the
command and then write a non-zero value to the Handshake field in the
comm page.  This function waits for the handshake to show up. */
static int wait_handshake(struct echoaudio *chip)
{
	int i;

	/* Wait up to 20ms for the handshake from the DSP */
	for (i = 0; i < HANDSHAKE_TIMEOUT; i++) {
		/* Look for the handshake value */
		barrier();
		if (chip->comm_page->handshake) {
			return 0;
		}
		udelay(1);
	}

	dev_err(chip->card->dev, "wait_handshake(): Timeout waiting for DSP\n");
	return -EBUSY;
}



/* Much of the interaction between the DSP and the driver is done via vector
commands; send_vector writes a vector command to the DSP.  Typically, this
causes the DSP to read or write fields in the comm page.
PCI posting is not required thanks to the handshake logic. */
static int send_vector(struct echoaudio *chip, u32 command)
{
	int i;

	wmb();	/* Flush all pending writes before sending the command */

	/* Wait up to 100ms for the "vector busy" bit to be off */
	for (i = 0; i < VECTOR_BUSY_TIMEOUT; i++) {
		if (!(get_dsp_register(chip, CHI32_VECTOR_REG) &
		      CHI32_VECTOR_BUSY)) {
			set_dsp_register(chip, CHI32_VECTOR_REG, command);
			/*if (i)  DE_ACT(("send_vector time: %d\n", i));*/
			return 0;
		}
		udelay(1);
	}

	dev_err(chip->card->dev, "timeout on send_vector\n");
	return -EBUSY;
}



/* write_dsp writes a 32-bit value to the DSP; this is used almost
exclusively for loading the DSP. */
static int write_dsp(struct echoaudio *chip, u32 data)
{
	u32 status, i;

	for (i = 0; i < 10000000; i++) {	/* timeout = 10s */
		status = get_dsp_register(chip, CHI32_STATUS_REG);
		if ((status & CHI32_STATUS_HOST_WRITE_EMPTY) != 0) {
			set_dsp_register(chip, CHI32_DATA_REG, data);
			wmb();			/* write it immediately */
			return 0;
		}
		udelay(1);
		cond_resched();
	}

	chip->bad_board = TRUE;		/* Set TRUE until DSP re-loaded */
	dev_dbg(chip->card->dev, "write_dsp: Set bad_board to TRUE\n");
	return -EIO;
}



/* read_dsp reads a 32-bit value from the DSP; this is used almost
exclusively for loading the DSP and checking the status of the ASIC. */
static int read_dsp(struct echoaudio *chip, u32 *data)
{
	u32 status, i;

	for (i = 0; i < READ_DSP_TIMEOUT; i++) {
		status = get_dsp_register(chip, CHI32_STATUS_REG);
		if ((status & CHI32_STATUS_HOST_READ_FULL) != 0) {
			*data = get_dsp_register(chip, CHI32_DATA_REG);
			return 0;
		}
		udelay(1);
		cond_resched();
	}

	chip->bad_board = TRUE;		/* Set TRUE until DSP re-loaded */
	dev_err(chip->card->dev, "read_dsp: Set bad_board to TRUE\n");
	return -EIO;
}



/****************************************************************************
	Firmware loading functions
 ****************************************************************************/

/* This function is used to read back the serial number from the DSP;
this is triggered by the SET_COMMPAGE_ADDR command.
Only some early Echogals products have serial numbers in the ROM;
the serial number is not used, but you still need to do this as
part of the DSP load process. */
static int read_sn(struct echoaudio *chip)
{
	int i;
	u32 sn[6];

	for (i = 0; i < 5; i++) {
		if (read_dsp(chip, &sn[i])) {
			dev_err(chip->card->dev,
				"Failed to read serial number\n");
			return -EIO;
		}
	}
	dev_dbg(chip->card->dev,
		"Read serial number %08x %08x %08x %08x %08x\n",
		 sn[0], sn[1], sn[2], sn[3], sn[4]);
	return 0;
}



#ifndef ECHOCARD_HAS_ASIC
/* This card has no ASIC, just return ok */
static inline int check_asic_status(struct echoaudio *chip)
{
	chip->asic_loaded = TRUE;
	return 0;
}

#endif /* !ECHOCARD_HAS_ASIC */



#ifdef ECHOCARD_HAS_ASIC

/* Load ASIC code - done after the DSP is loaded */
static int load_asic_generic(struct echoaudio *chip, u32 cmd, short asic)
{
	const struct firmware *fw;
	int err;
	u32 i, size;
	u8 *code;

	err = get_firmware(&fw, chip, asic);
	if (err < 0) {
		dev_warn(chip->card->dev, "Firmware not found !\n");
		return err;
	}

	code = (u8 *)fw->data;
	size = fw->size;

	/* Send the "Here comes the ASIC" command */
	if (write_dsp(chip, cmd) < 0)
		goto la_error;

	/* Write length of ASIC file in bytes */
	if (write_dsp(chip, size) < 0)
		goto la_error;

	for (i = 0; i < size; i++) {
		if (write_dsp(chip, code[i]) < 0)
			goto la_error;
	}

	free_firmware(fw, chip);
	return 0;

la_error:
	dev_err(chip->card->dev, "failed on write_dsp\n");
	free_firmware(fw, chip);
	return -EIO;
}

#endif /* ECHOCARD_HAS_ASIC */



#ifdef DSP_56361

/* Install the resident loader for 56361 DSPs;  The resident loader is on
the EPROM on the board for 56301 DSP. The resident loader is a tiny little
program that is used to load the real DSP code. */
static int install_resident_loader(struct echoaudio *chip)
{
	u32 address;
	int index, words, i;
	u16 *code;
	u32 status;
	const struct firmware *fw;

	/* 56361 cards only!  This check is required by the old 56301-based
	Mona and Gina24 */
	if (chip->device_id != DEVICE_ID_56361)
		return 0;

	/* Look to see if the resident loader is present.  If the resident
	loader is already installed, host flag 5 will be on. */
	status = get_dsp_register(chip, CHI32_STATUS_REG);
	if (status & CHI32_STATUS_REG_HF5) {
		dev_dbg(chip->card->dev,
			"Resident loader already installed; status is 0x%x\n",
			 status);
		return 0;
	}

	i = get_firmware(&fw, chip, FW_361_LOADER);
	if (i < 0) {
		dev_warn(chip->card->dev, "Firmware not found !\n");
		return i;
	}

	/* The DSP code is an array of 16 bit words.  The array is divided up
	into sections.  The first word of each section is the size in words,
	followed by the section type.
	Since DSP addresses and data are 24 bits wide, they each take up two
	16 bit words in the array.
	This is a lot like the other loader loop, but it's not a loop, you
	don't write the memory type, and you don't write a zero at the end. */

	/* Set DSP format bits for 24 bit mode */
	set_dsp_register(chip, CHI32_CONTROL_REG,
			 get_dsp_register(chip, CHI32_CONTROL_REG) | 0x900);

	code = (u16 *)fw->data;

	/* Skip the header section; the first word in the array is the size
	of the first section, so the first real section of code is pointed
	to by Code[0]. */
	index = code[0];

	/* Skip the section size, LRS block type, and DSP memory type */
	index += 3;

	/* Get the number of DSP words to write */
	words = code[index++];

	/* Get the DSP address for this block; 24 bits, so build from two words */
	address = ((u32)code[index] << 16) + code[index + 1];
	index += 2;

	/* Write the count to the DSP */
	if (write_dsp(chip, words)) {
		dev_err(chip->card->dev,
			"install_resident_loader: Failed to write word count!\n");
		goto irl_error;
	}
	/* Write the DSP address */
	if (write_dsp(chip, address)) {
		dev_err(chip->card->dev,
			"install_resident_loader: Failed to write DSP address!\n");
		goto irl_error;
	}
	/* Write out this block of code to the DSP */
	for (i = 0; i < words; i++) {
		u32 data;

		data = ((u32)code[index] << 16) + code[index + 1];
		if (write_dsp(chip, data)) {
			dev_err(chip->card->dev,
				"install_resident_loader: Failed to write DSP code\n");
			goto irl_error;
		}
		index += 2;
	}

	/* Wait for flag 5 to come up */
	for (i = 0; i < 200; i++) {	/* Timeout is 50us * 200 = 10ms */
		udelay(50);
		status = get_dsp_register(chip, CHI32_STATUS_REG);
		if (status & CHI32_STATUS_REG_HF5)
			break;
	}

	if (i == 200) {
		dev_err(chip->card->dev, "Resident loader failed to set HF5\n");
		goto irl_error;
	}

	dev_dbg(chip->card->dev, "Resident loader successfully installed\n");
	free_firmware(fw, chip);
	return 0;

irl_error:
	free_firmware(fw, chip);
	return -EIO;
}

#endif /* DSP_56361 */


static int load_dsp(struct echoaudio *chip, u16 *code)
{
	u32 address, data;
	int index, words, i;

	if (chip->dsp_code == code) {
		dev_warn(chip->card->dev, "DSP is already loaded!\n");
		return 0;
	}
	chip->bad_board = TRUE;		/* Set TRUE until DSP loaded */
	chip->dsp_code = NULL;		/* Current DSP code not loaded */
	chip->asic_loaded = FALSE;	/* Loading the DSP code will reset the ASIC */

	dev_dbg(chip->card->dev, "load_dsp: Set bad_board to TRUE\n");

	/* If this board requires a resident loader, install it. */
#ifdef DSP_56361
	if ((i = install_resident_loader(chip)) < 0)
		return i;
#endif

	/* Send software reset command */
	if (send_vector(chip, DSP_VC_RESET) < 0) {
		dev_err(chip->card->dev,
			"LoadDsp: send_vector DSP_VC_RESET failed, Critical Failure\n");
		return -EIO;
	}
	/* Delay 10us */
	udelay(10);

	/* Wait 10ms for HF3 to indicate that software reset is complete */
	for (i = 0; i < 1000; i++) {	/* Timeout is 10us * 1000 = 10ms */
		if (get_dsp_register(chip, CHI32_STATUS_REG) &
		    CHI32_STATUS_REG_HF3)
			break;
		udelay(10);
	}

	if (i == 1000) {
		dev_err(chip->card->dev,
			"load_dsp: Timeout waiting for CHI32_STATUS_REG_HF3\n");
		return -EIO;
	}

	/* Set DSP format bits for 24 bit mode now that soft reset is done */
	set_dsp_register(chip, CHI32_CONTROL_REG,
			 get_dsp_register(chip, CHI32_CONTROL_REG) | 0x900);

	/* Main loader loop */

	index = code[0];
	for (;;) {
		int block_type, mem_type;

		/* Total Block Size */
		index++;

		/* Block Type */
		block_type = code[index];
		if (block_type == 4)	/* We're finished */
			break;

		index++;

		/* Memory Type  P=0,X=1,Y=2 */
		mem_type = code[index++];

		/* Block Code Size */
		words = code[index++];
		if (words == 0)		/* We're finished */
			break;

		/* Start Address */
		address = ((u32)code[index] << 16) + code[index + 1];
		index += 2;

		if (write_dsp(chip, words) < 0) {
			dev_err(chip->card->dev,
				"load_dsp: failed to write number of DSP words\n");
			return -EIO;
		}
		if (write_dsp(chip, address) < 0) {
			dev_err(chip->card->dev,
				"load_dsp: failed to write DSP address\n");
			return -EIO;
		}
		if (write_dsp(chip, mem_type) < 0) {
			dev_err(chip->card->dev,
				"load_dsp: failed to write DSP memory type\n");
			return -EIO;
		}
		/* Code */
		for (i = 0; i < words; i++, index+=2) {
			data = ((u32)code[index] << 16) + code[index + 1];
			if (write_dsp(chip, data) < 0) {
				dev_err(chip->card->dev,
					"load_dsp: failed to write DSP data\n");
				return -EIO;
			}
		}
	}

	if (write_dsp(chip, 0) < 0) {	/* We're done!!! */
		dev_err(chip->card->dev,
			"load_dsp: Failed to write final zero\n");
		return -EIO;
	}
	udelay(10);

	for (i = 0; i < 5000; i++) {	/* Timeout is 100us * 5000 = 500ms */
		/* Wait for flag 4 - indicates that the DSP loaded OK */
		if (get_dsp_register(chip, CHI32_STATUS_REG) &
		    CHI32_STATUS_REG_HF4) {
			set_dsp_register(chip, CHI32_CONTROL_REG,
					 get_dsp_register(chip, CHI32_CONTROL_REG) & ~0x1b00);

			if (write_dsp(chip, DSP_FNC_SET_COMMPAGE_ADDR) < 0) {
				dev_err(chip->card->dev,
					"load_dsp: Failed to write DSP_FNC_SET_COMMPAGE_ADDR\n");
				return -EIO;
			}

			if (write_dsp(chip, chip->comm_page_phys) < 0) {
				dev_err(chip->card->dev,
					"load_dsp: Failed to write comm page address\n");
				return -EIO;
			}

			/* Get the serial number via slave mode.
			This is triggered by the SET_COMMPAGE_ADDR command.
			We don't actually use the serial number but we have to
			get it as part of the DSP init voodoo. */
			if (read_sn(chip) < 0) {
				dev_err(chip->card->dev,
					"load_dsp: Failed to read serial number\n");
				return -EIO;
			}

			chip->dsp_code = code;		/* Show which DSP code loaded */
			chip->bad_board = FALSE;	/* DSP OK */
			return 0;
		}
		udelay(100);
	}

	dev_err(chip->card->dev,
		"load_dsp: DSP load timed out waiting for HF4\n");
	return -EIO;
}



/* load_firmware takes care of loading the DSP and any ASIC code. */
static int load_firmware(struct echoaudio *chip)
{
	const struct firmware *fw;
	int box_type, err;

	if (snd_BUG_ON(!chip->comm_page))
		return -EPERM;

	/* See if the ASIC is present and working - only if the DSP is already loaded */
	if (chip->dsp_code) {
		if ((box_type = check_asic_status(chip)) >= 0)
			return box_type;
		/* ASIC check failed; force the DSP to reload */
		chip->dsp_code = NULL;
	}

	err = get_firmware(&fw, chip, chip->dsp_code_to_load);
	if (err < 0)
		return err;
	err = load_dsp(chip, (u16 *)fw->data);
	free_firmware(fw, chip);
	if (err < 0)
		return err;

	if ((box_type = load_asic(chip)) < 0)
		return box_type;	/* error */

	return box_type;
}



/****************************************************************************
	Mixer functions
 ****************************************************************************/

#if defined(ECHOCARD_HAS_INPUT_NOMINAL_LEVEL) || \
	defined(ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL)

/* Set the nominal level for an input or output bus (true = -10dBV, false = +4dBu) */
static int set_nominal_level(struct echoaudio *chip, u16 index, char consumer)
{
	if (snd_BUG_ON(index >= num_busses_out(chip) + num_busses_in(chip)))
		return -EINVAL;

	/* Wait for the handshake (OK even if ASIC is not loaded) */
	if (wait_handshake(chip))
		return -EIO;

	chip->nominal_level[index] = consumer;

	if (consumer)
		chip->comm_page->nominal_level_mask |= cpu_to_le32(1 << index);
	else
		chip->comm_page->nominal_level_mask &= ~cpu_to_le32(1 << index);

	return 0;
}

#endif /* ECHOCARD_HAS_*_NOMINAL_LEVEL */



/* Set the gain for a single physical output channel (dB). */
static int set_output_gain(struct echoaudio *chip, u16 channel, s8 gain)
{
	if (snd_BUG_ON(channel >= num_busses_out(chip)))
		return -EINVAL;

	if (wait_handshake(chip))
		return -EIO;

	/* Save the new value */
	chip->output_gain[channel] = gain;
	chip->comm_page->line_out_level[channel] = gain;
	return 0;
}



#ifdef ECHOCARD_HAS_MONITOR
/* Set the monitor level from an input bus to an output bus. */
static int set_monitor_gain(struct echoaudio *chip, u16 output, u16 input,
			    s8 gain)
{
	if (snd_BUG_ON(output >= num_busses_out(chip) ||
		    input >= num_busses_in(chip)))
		return -EINVAL;

	if (wait_handshake(chip))
		return -EIO;

	chip->monitor_gain[output][input] = gain;
	chip->comm_page->monitors[monitor_index(chip, output, input)] = gain;
	return 0;
}
#endif /* ECHOCARD_HAS_MONITOR */


/* Tell the DSP to read and update output, nominal & monitor levels in comm page. */
static int update_output_line_level(struct echoaudio *chip)
{
	if (wait_handshake(chip))
		return -EIO;
	clear_handshake(chip);
	return send_vector(chip, DSP_VC_UPDATE_OUTVOL);
}



/* Tell the DSP to read and update input levels in comm page */
static int update_input_line_level(struct echoaudio *chip)
{
	if (wait_handshake(chip))
		return -EIO;
	clear_handshake(chip);
	return send_vector(chip, DSP_VC_UPDATE_INGAIN);
}



/* set_meters_on turns the meters on or off.  If meters are turned on, the DSP
will write the meter and clock detect values to the comm page at about 30Hz */
static void set_meters_on(struct echoaudio *chip, char on)
{
	if (on && !chip->meters_enabled) {
		send_vector(chip, DSP_VC_METERS_ON);
		chip->meters_enabled = 1;
	} else if (!on && chip->meters_enabled) {
		send_vector(chip, DSP_VC_METERS_OFF);
		chip->meters_enabled = 0;
		memset((s8 *)chip->comm_page->vu_meter, ECHOGAIN_MUTED,
		       DSP_MAXPIPES);
		memset((s8 *)chip->comm_page->peak_meter, ECHOGAIN_MUTED,
		       DSP_MAXPIPES);
	}
}



/* Fill out an the given array using the current values in the comm page.
Meters are written in the comm page by the DSP in this order:
 Output busses
 Input busses
 Output pipes (vmixer cards only)

This function assumes there are no more than 16 in/out busses or pipes
Meters is an array [3][16][2] of long. */
static void get_audio_meters(struct echoaudio *chip, long *meters)
{
	int i, m, n;

	m = 0;
	n = 0;
	for (i = 0; i < num_busses_out(chip); i++, m++) {
		meters[n++] = chip->comm_page->vu_meter[m];
		meters[n++] = chip->comm_page->peak_meter[m];
	}
	for (; n < 32; n++)
		meters[n] = 0;

#ifdef ECHOCARD_ECHO3G
	m = E3G_MAX_OUTPUTS;	/* Skip unused meters */
#endif

	for (i = 0; i < num_busses_in(chip); i++, m++) {
		meters[n++] = chip->comm_page->vu_meter[m];
		meters[n++] = chip->comm_page->peak_meter[m];
	}
	for (; n < 64; n++)
		meters[n] = 0;

#ifdef ECHOCARD_HAS_VMIXER
	for (i = 0; i < num_pipes_out(chip); i++, m++) {
		meters[n++] = chip->comm_page->vu_meter[m];
		meters[n++] = chip->comm_page->peak_meter[m];
	}
#endif
	for (; n < 96; n++)
		meters[n] = 0;
}



static int restore_dsp_rettings(struct echoaudio *chip)
{
	int i, o, err;

	if ((err = check_asic_status(chip)) < 0)
		return err;

	/* Gina20/Darla20 only. Should be harmless for other cards. */
	chip->comm_page->gd_clock_state = GD_CLOCK_UNDEF;
	chip->comm_page->gd_spdif_status = GD_SPDIF_STATUS_UNDEF;
	chip->comm_page->handshake = 0xffffffff;

	/* Restore output busses */
	for (i = 0; i < num_busses_out(chip); i++) {
		err = set_output_gain(chip, i, chip->output_gain[i]);
		if (err < 0)
			return err;
	}

#ifdef ECHOCARD_HAS_VMIXER
	for (i = 0; i < num_pipes_out(chip); i++)
		for (o = 0; o < num_busses_out(chip); o++) {
			err = set_vmixer_gain(chip, o, i,
						chip->vmixer_gain[o][i]);
			if (err < 0)
				return err;
		}
	if (update_vmixer_level(chip) < 0)
		return -EIO;
#endif /* ECHOCARD_HAS_VMIXER */

#ifdef ECHOCARD_HAS_MONITOR
	for (o = 0; o < num_busses_out(chip); o++)
		for (i = 0; i < num_busses_in(chip); i++) {
			err = set_monitor_gain(chip, o, i,
						chip->monitor_gain[o][i]);
			if (err < 0)
				return err;
		}
#endif /* ECHOCARD_HAS_MONITOR */

#ifdef ECHOCARD_HAS_INPUT_GAIN
	for (i = 0; i < num_busses_in(chip); i++) {
		err = set_input_gain(chip, i, chip->input_gain[i]);
		if (err < 0)
			return err;
	}
#endif /* ECHOCARD_HAS_INPUT_GAIN */

	err = update_output_line_level(chip);
	if (err < 0)
		return err;

	err = update_input_line_level(chip);
	if (err < 0)
		return err;

	err = set_sample_rate(chip, chip->sample_rate);
	if (err < 0)
		return err;

	if (chip->meters_enabled) {
		err = send_vector(chip, DSP_VC_METERS_ON);
		if (err < 0)
			return err;
	}

#ifdef ECHOCARD_HAS_DIGITAL_MODE_SWITCH
	if (set_digital_mode(chip, chip->digital_mode) < 0)
		return -EIO;
#endif

#ifdef ECHOCARD_HAS_DIGITAL_IO
	if (set_professional_spdif(chip, chip->professional_spdif) < 0)
		return -EIO;
#endif

#ifdef ECHOCARD_HAS_PHANTOM_POWER
	if (set_phantom_power(chip, chip->phantom_power) < 0)
		return -EIO;
#endif

#ifdef ECHOCARD_HAS_EXTERNAL_CLOCK
	/* set_input_clock() also restores automute setting */
	if (set_input_clock(chip, chip->input_clock) < 0)
		return -EIO;
#endif

#ifdef ECHOCARD_HAS_OUTPUT_CLOCK_SWITCH
	if (set_output_clock(chip, chip->output_clock) < 0)
		return -EIO;
#endif

	if (wait_handshake(chip) < 0)
		return -EIO;
	clear_handshake(chip);
	if (send_vector(chip, DSP_VC_UPDATE_FLAGS) < 0)
		return -EIO;

	return 0;
}



/****************************************************************************
	Transport functions
 ****************************************************************************/

/* set_audio_format() sets the format of the audio data in host memory for
this pipe.  Note that _MS_ (mono-to-stereo) playback modes are not used by ALSA
but they are here because they are just mono while capturing */
static void set_audio_format(struct echoaudio *chip, u16 pipe_index,
			     const struct audioformat *format)
{
	u16 dsp_format;

	dsp_format = DSP_AUDIOFORM_SS_16LE;

	/* Look for super-interleave (no big-endian and 8 bits) */
	if (format->interleave > 2) {
		switch (format->bits_per_sample) {
		case 16:
			dsp_format = DSP_AUDIOFORM_SUPER_INTERLEAVE_16LE;
			break;
		case 24:
			dsp_format = DSP_AUDIOFORM_SUPER_INTERLEAVE_24LE;
			break;
		case 32:
			dsp_format = DSP_AUDIOFORM_SUPER_INTERLEAVE_32LE;
			break;
		}
		dsp_format |= format->interleave;
	} else if (format->data_are_bigendian) {
		/* For big-endian data, only 32 bit samples are supported */
		switch (format->interleave) {
		case 1:
			dsp_format = DSP_AUDIOFORM_MM_32BE;
			break;
#ifdef ECHOCARD_HAS_STEREO_BIG_ENDIAN32
		case 2:
			dsp_format = DSP_AUDIOFORM_SS_32BE;
			break;
#endif
		}
	} else if (format->interleave == 1 &&
		   format->bits_per_sample == 32 && !format->mono_to_stereo) {
		/* 32 bit little-endian mono->mono case */
		dsp_format = DSP_AUDIOFORM_MM_32LE;
	} else {
		/* Handle the other little-endian formats */
		switch (format->bits_per_sample) {
		case 8:
			if (format->interleave == 2)
				dsp_format = DSP_AUDIOFORM_SS_8;
			else
				dsp_format = DSP_AUDIOFORM_MS_8;
			break;
		default:
		case 16:
			if (format->interleave == 2)
				dsp_format = DSP_AUDIOFORM_SS_16LE;
			else
				dsp_format = DSP_AUDIOFORM_MS_16LE;
			break;
		case 24:
			if (format->interleave == 2)
				dsp_format = DSP_AUDIOFORM_SS_24LE;
			else
				dsp_format = DSP_AUDIOFORM_MS_24LE;
			break;
		case 32:
			if (format->interleave == 2)
				dsp_format = DSP_AUDIOFORM_SS_32LE;
			else
				dsp_format = DSP_AUDIOFORM_MS_32LE;
			break;
		}
	}
	dev_dbg(chip->card->dev,
		 "set_audio_format[%d] = %x\n", pipe_index, dsp_format);
	chip->comm_page->audio_format[pipe_index] = cpu_to_le16(dsp_format);
}



/* start_transport starts transport for a set of pipes.
The bits 1 in channel_mask specify what pipes to start. Only the bit of the
first channel must be set, regardless its interleave.
Same thing for pause_ and stop_ -trasport below. */
static int start_transport(struct echoaudio *chip, u32 channel_mask,
			   u32 cyclic_mask)
{

	if (wait_handshake(chip))
		return -EIO;

	chip->comm_page->cmd_start |= cpu_to_le32(channel_mask);

	if (chip->comm_page->cmd_start) {
		clear_handshake(chip);
		send_vector(chip, DSP_VC_START_TRANSFER);
		if (wait_handshake(chip))
			return -EIO;
		/* Keep track of which pipes are transporting */
		chip->active_mask |= channel_mask;
		chip->comm_page->cmd_start = 0;
		return 0;
	}

	dev_err(chip->card->dev, "start_transport: No pipes to start!\n");
	return -EINVAL;
}



static int pause_transport(struct echoaudio *chip, u32 channel_mask)
{

	if (wait_handshake(chip))
		return -EIO;

	chip->comm_page->cmd_stop |= cpu_to_le32(channel_mask);
	chip->comm_page->cmd_reset = 0;
	if (chip->comm_page->cmd_stop) {
		clear_handshake(chip);
		send_vector(chip, DSP_VC_STOP_TRANSFER);
		if (wait_handshake(chip))
			return -EIO;
		/* Keep track of which pipes are transporting */
		chip->active_mask &= ~channel_mask;
		chip->comm_page->cmd_stop = 0;
		chip->comm_page->cmd_reset = 0;
		return 0;
	}

	dev_warn(chip->card->dev, "pause_transport: No pipes to stop!\n");
	return 0;
}



static int stop_transport(struct echoaudio *chip, u32 channel_mask)
{

	if (wait_handshake(chip))
		return -EIO;

	chip->comm_page->cmd_stop |= cpu_to_le32(channel_mask);
	chip->comm_page->cmd_reset |= cpu_to_le32(channel_mask);
	if (chip->comm_page->cmd_reset) {
		clear_handshake(chip);
		send_vector(chip, DSP_VC_STOP_TRANSFER);
		if (wait_handshake(chip))
			return -EIO;
		/* Keep track of which pipes are transporting */
		chip->active_mask &= ~channel_mask;
		chip->comm_page->cmd_stop = 0;
		chip->comm_page->cmd_reset = 0;
		return 0;
	}

	dev_warn(chip->card->dev, "stop_transport: No pipes to stop!\n");
	return 0;
}



static inline int is_pipe_allocated(struct echoaudio *chip, u16 pipe_index)
{
	return (chip->pipe_alloc_mask & (1 << pipe_index));
}



/* Stops everything and turns off the DSP. All pipes should be already
stopped and unallocated. */
static int rest_in_peace(struct echoaudio *chip)
{

	/* Stops all active pipes (just to be sure) */
	stop_transport(chip, chip->active_mask);

	set_meters_on(chip, FALSE);

#ifdef ECHOCARD_HAS_MIDI
	enable_midi_input(chip, FALSE);
#endif

	/* Go to sleep */
	if (chip->dsp_code) {
		/* Make load_firmware do a complete reload */
		chip->dsp_code = NULL;
		/* Put the DSP to sleep */
		return send_vector(chip, DSP_VC_GO_COMATOSE);
	}
	return 0;
}



/* Fills the comm page with default values */
static int init_dsp_comm_page(struct echoaudio *chip)
{
	/* Check if the compiler added extra padding inside the structure */
	if (offsetof(struct comm_page, midi_output) != 0xbe0) {
		dev_err(chip->card->dev,
			"init_dsp_comm_page() - Invalid struct comm_page structure\n");
		return -EPERM;
	}

	/* Init all the basic stuff */
	chip->card_name = ECHOCARD_NAME;
	chip->bad_board = TRUE;	/* Set TRUE until DSP loaded */
	chip->dsp_code = NULL;	/* Current DSP code not loaded */
	chip->asic_loaded = FALSE;
	memset(chip->comm_page, 0, sizeof(struct comm_page));

	/* Init the comm page */
	chip->comm_page->comm_size =
		cpu_to_le32(sizeof(struct comm_page));
	chip->comm_page->handshake = 0xffffffff;
	chip->comm_page->midi_out_free_count =
		cpu_to_le32(DSP_MIDI_OUT_FIFO_SIZE);
	chip->comm_page->sample_rate = cpu_to_le32(44100);

	/* Set line levels so we don't blast any inputs on startup */
	memset(chip->comm_page->monitors, ECHOGAIN_MUTED, MONITOR_ARRAY_SIZE);
	memset(chip->comm_page->vmixer, ECHOGAIN_MUTED, VMIXER_ARRAY_SIZE);

	return 0;
}



/* This function initializes the chip structure with default values, ie. all
 * muted and internal clock source. Then it copies the settings to the DSP.
 * This MUST be called after the DSP is up and running !
 */
static int init_line_levels(struct echoaudio *chip)
{
	memset(chip->output_gain, ECHOGAIN_MUTED, sizeof(chip->output_gain));
	memset(chip->input_gain, ECHOGAIN_MUTED, sizeof(chip->input_gain));
	memset(chip->monitor_gain, ECHOGAIN_MUTED, sizeof(chip->monitor_gain));
	memset(chip->vmixer_gain, ECHOGAIN_MUTED, sizeof(chip->vmixer_gain));
	chip->input_clock = ECHO_CLOCK_INTERNAL;
	chip->output_clock = ECHO_CLOCK_WORD;
	chip->sample_rate = 44100;
	return restore_dsp_rettings(chip);
}



/* This is low level part of the interrupt handler.
It returns -1 if the IRQ is not ours, or N>=0 if it is, where N is the number
of midi data in the input queue. */
static int service_irq(struct echoaudio *chip)
{
	int st;

	/* Read the DSP status register and see if this DSP generated this interrupt */
	if (get_dsp_register(chip, CHI32_STATUS_REG) & CHI32_STATUS_IRQ) {
		st = 0;
#ifdef ECHOCARD_HAS_MIDI
		/* Get and parse midi data if present */
		if (chip->comm_page->midi_input[0])	/* The count is at index 0 */
			st = midi_service_irq(chip);	/* Returns how many midi bytes we received */
#endif
		/* Clear the hardware interrupt */
		chip->comm_page->midi_input[0] = 0;
		send_vector(chip, DSP_VC_ACK_INT);
		return st;
	}
	return -1;
}




/******************************************************************************
	Functions for opening and closing pipes
 ******************************************************************************/

/* allocate_pipes is used to reserve audio pipes for your exclusive use.
The call will fail if some pipes are already allocated. */
static int allocate_pipes(struct echoaudio *chip, struct audiopipe *pipe,
			  int pipe_index, int interleave)
{
	int i;
	u32 channel_mask;
	char is_cyclic;

	dev_dbg(chip->card->dev,
		"allocate_pipes: ch=%d int=%d\n", pipe_index, interleave);

	if (chip->bad_board)
		return -EIO;

	is_cyclic = 1;	/* This driver uses cyclic buffers only */

	for (channel_mask = i = 0; i < interleave; i++)
		channel_mask |= 1 << (pipe_index + i);
	if (chip->pipe_alloc_mask & channel_mask) {
		dev_err(chip->card->dev,
			"allocate_pipes: channel already open\n");
		return -EAGAIN;
	}

	chip->comm_page->position[pipe_index] = 0;
	chip->pipe_alloc_mask |= channel_mask;
	if (is_cyclic)
		chip->pipe_cyclic_mask |= channel_mask;
	pipe->index = pipe_index;
	pipe->interleave = interleave;
	pipe->state = PIPE_STATE_STOPPED;

	/* The counter register is where the DSP writes the 32 bit DMA
	position for a pipe.  The DSP is constantly updating this value as
	it moves data. The DMA counter is in units of bytes, not samples. */
	pipe->dma_counter = &chip->comm_page->position[pipe_index];
	*pipe->dma_counter = 0;
	return pipe_index;
}



static int free_pipes(struct echoaudio *chip, struct audiopipe *pipe)
{
	u32 channel_mask;
	int i;

	if (snd_BUG_ON(!is_pipe_allocated(chip, pipe->index)))
		return -EINVAL;
	if (snd_BUG_ON(pipe->state != PIPE_STATE_STOPPED))
		return -EINVAL;

	for (channel_mask = i = 0; i < pipe->interleave; i++)
		channel_mask |= 1 << (pipe->index + i);

	chip->pipe_alloc_mask &= ~channel_mask;
	chip->pipe_cyclic_mask &= ~channel_mask;
	return 0;
}



/******************************************************************************
	Functions for managing the scatter-gather list
******************************************************************************/

static int sglist_init(struct echoaudio *chip, struct audiopipe *pipe)
{
	pipe->sglist_head = 0;
	memset(pipe->sgpage.area, 0, PAGE_SIZE);
	chip->comm_page->sglist_addr[pipe->index].addr =
		cpu_to_le32(pipe->sgpage.addr);
	return 0;
}



static int sglist_add_mapping(struct echoaudio *chip, struct audiopipe *pipe,
				dma_addr_t address, size_t length)
{
	int head = pipe->sglist_head;
	struct sg_entry *list = (struct sg_entry *)pipe->sgpage.area;

	if (head < MAX_SGLIST_ENTRIES - 1) {
		list[head].addr = cpu_to_le32(address);
		list[head].size = cpu_to_le32(length);
		pipe->sglist_head++;
	} else {
		dev_err(chip->card->dev, "SGlist: too many fragments\n");
		return -ENOMEM;
	}
	return 0;
}



static inline int sglist_add_irq(struct echoaudio *chip, struct audiopipe *pipe)
{
	return sglist_add_mapping(chip, pipe, 0, 0);
}



static inline int sglist_wrap(struct echoaudio *chip, struct audiopipe *pipe)
{
	return sglist_add_mapping(chip, pipe, pipe->sgpage.addr, 0);
}
