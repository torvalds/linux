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

#ifndef _ECHO_DSP_
#define _ECHO_DSP_


/**** Echogals: Darla20, Gina20, Layla20, and Darla24 ****/
#if defined(ECHOGALS_FAMILY)

#define NUM_ASIC_TESTS		5
#define READ_DSP_TIMEOUT	1000000L	/* one second */

/**** Echo24: Gina24, Layla24, Mona, Mia, Mia-midi ****/
#elif defined(ECHO24_FAMILY)

#define DSP_56361			/* Some Echo24 cards use the 56361 DSP */
#define READ_DSP_TIMEOUT	100000L		/* .1 second */

/**** 3G: Gina3G, Layla3G ****/
#elif defined(ECHO3G_FAMILY)

#define DSP_56361
#define READ_DSP_TIMEOUT 	100000L		/* .1 second */
#define MIN_MTC_1X_RATE		32000

/**** Indigo: Indigo, Indigo IO, Indigo DJ ****/
#elif defined(INDIGO_FAMILY)

#define DSP_56361
#define READ_DSP_TIMEOUT	100000L		/* .1 second */

#else

#error No family is defined

#endif



/*
 *
 *  Max inputs and outputs
 *
 */

#define DSP_MAXAUDIOINPUTS		16	/* Max audio input channels */
#define DSP_MAXAUDIOOUTPUTS		16	/* Max audio output channels */
#define DSP_MAXPIPES			32	/* Max total pipes (input + output) */


/*
 *
 * These are the offsets for the memory-mapped DSP registers; the DSP base
 * address is treated as the start of a u32 array.
 */

#define CHI32_CONTROL_REG		4
#define CHI32_STATUS_REG		5
#define CHI32_VECTOR_REG		6
#define CHI32_DATA_REG			7


/*
 *
 * Interesting bits within the DSP registers
 *
 */

#define CHI32_VECTOR_BUSY		0x00000001
#define CHI32_STATUS_REG_HF3		0x00000008
#define CHI32_STATUS_REG_HF4		0x00000010
#define CHI32_STATUS_REG_HF5		0x00000020
#define CHI32_STATUS_HOST_READ_FULL	0x00000004
#define CHI32_STATUS_HOST_WRITE_EMPTY	0x00000002
#define CHI32_STATUS_IRQ		0x00000040


/* 
 *
 * DSP commands sent via slave mode; these are sent to the DSP by write_dsp()
 *
 */

#define DSP_FNC_SET_COMMPAGE_ADDR		0x02
#define DSP_FNC_LOAD_LAYLA_ASIC			0xa0
#define DSP_FNC_LOAD_GINA24_ASIC		0xa0
#define DSP_FNC_LOAD_MONA_PCI_CARD_ASIC		0xa0
#define DSP_FNC_LOAD_LAYLA24_PCI_CARD_ASIC	0xa0
#define DSP_FNC_LOAD_MONA_EXTERNAL_ASIC		0xa1
#define DSP_FNC_LOAD_LAYLA24_EXTERNAL_ASIC	0xa1
#define DSP_FNC_LOAD_3G_ASIC			0xa0


/*
 *
 * Defines to handle the MIDI input state engine; these are used to properly
 * extract MIDI time code bytes and their timestamps from the MIDI input stream.
 *
 */

#define MIDI_IN_STATE_NORMAL	0
#define MIDI_IN_STATE_TS_HIGH	1
#define MIDI_IN_STATE_TS_LOW	2
#define MIDI_IN_STATE_F1_DATA 	3
#define MIDI_IN_SKIP_DATA	(-1)


/*----------------------------------------------------------------------------

Setting the sample rates on Layla24 is somewhat schizophrenic.

For standard rates, it works exactly like Mona and Gina24.  That is, for
8, 11.025, 16, 22.05, 32, 44.1, 48, 88.2, and 96 kHz, you just set the
appropriate bits in the control register and write the control register.

In order to support MIDI time code sync (and possibly SMPTE LTC sync in
the future), Layla24 also has "continuous sample rate mode".  In this mode,
Layla24 can generate any sample rate between 25 and 50 kHz inclusive, or
50 to 100 kHz inclusive for double speed mode.

To use continuous mode:

-Set the clock select bits in the control register to 0xe (see the #define
 below)

-Set double-speed mode if you want to use sample rates above 50 kHz

-Write the control register as you would normally

-Now, you need to set the frequency register. First, you need to determine the
 value for the frequency register.  This is given by the following formula:

frequency_reg = (LAYLA24_MAGIC_NUMBER / sample_rate) - 2

Note the #define below for the magic number

-Wait for the DSP handshake
-Write the frequency_reg value to the .SampleRate field of the comm page
-Send the vector command SET_LAYLA24_FREQUENCY_REG (see vmonkey.h)

Once you have set the control register up for continuous mode, you can just
write the frequency register to change the sample rate.  This could be
used for MIDI time code sync. For MTC sync, the control register is set for
continuous mode.  The driver then just keeps writing the
SET_LAYLA24_FREQUENCY_REG command.

-----------------------------------------------------------------------------*/

#define LAYLA24_MAGIC_NUMBER			677376000
#define LAYLA24_CONTINUOUS_CLOCK		0x000e


/*
 *
 * DSP vector commands
 *
 */

#define DSP_VC_RESET				0x80ff

#ifndef DSP_56361

#define DSP_VC_ACK_INT				0x8073
#define DSP_VC_SET_VMIXER_GAIN			0x0000	/* Not used, only for compile */
#define DSP_VC_START_TRANSFER			0x0075	/* Handshke rqd. */
#define DSP_VC_METERS_ON			0x0079
#define DSP_VC_METERS_OFF			0x007b
#define DSP_VC_UPDATE_OUTVOL			0x007d	/* Handshke rqd. */
#define DSP_VC_UPDATE_INGAIN			0x007f	/* Handshke rqd. */
#define DSP_VC_ADD_AUDIO_BUFFER			0x0081	/* Handshke rqd. */
#define DSP_VC_TEST_ASIC			0x00eb
#define DSP_VC_UPDATE_CLOCKS			0x00ef	/* Handshke rqd. */
#define DSP_VC_SET_LAYLA_SAMPLE_RATE		0x00f1	/* Handshke rqd. */
#define DSP_VC_SET_GD_AUDIO_STATE		0x00f1	/* Handshke rqd. */
#define DSP_VC_WRITE_CONTROL_REG		0x00f1	/* Handshke rqd. */
#define DSP_VC_MIDI_WRITE			0x00f5	/* Handshke rqd. */
#define DSP_VC_STOP_TRANSFER			0x00f7	/* Handshke rqd. */
#define DSP_VC_UPDATE_FLAGS			0x00fd	/* Handshke rqd. */
#define DSP_VC_GO_COMATOSE			0x00f9

#else /* !DSP_56361 */

/* Vector commands for families that use either the 56301 or 56361 */
#define DSP_VC_ACK_INT				0x80F5
#define DSP_VC_SET_VMIXER_GAIN			0x00DB	/* Handshke rqd. */
#define DSP_VC_START_TRANSFER			0x00DD	/* Handshke rqd. */
#define DSP_VC_METERS_ON			0x00EF
#define DSP_VC_METERS_OFF			0x00F1
#define DSP_VC_UPDATE_OUTVOL			0x00E3	/* Handshke rqd. */
#define DSP_VC_UPDATE_INGAIN			0x00E5	/* Handshke rqd. */
#define DSP_VC_ADD_AUDIO_BUFFER			0x00E1	/* Handshke rqd. */
#define DSP_VC_TEST_ASIC			0x00ED
#define DSP_VC_UPDATE_CLOCKS			0x00E9	/* Handshke rqd. */
#define DSP_VC_SET_LAYLA24_FREQUENCY_REG	0x00E9	/* Handshke rqd. */
#define DSP_VC_SET_LAYLA_SAMPLE_RATE		0x00EB	/* Handshke rqd. */
#define DSP_VC_SET_GD_AUDIO_STATE		0x00EB	/* Handshke rqd. */
#define DSP_VC_WRITE_CONTROL_REG		0x00EB	/* Handshke rqd. */
#define DSP_VC_MIDI_WRITE			0x00E7	/* Handshke rqd. */
#define DSP_VC_STOP_TRANSFER			0x00DF	/* Handshke rqd. */
#define DSP_VC_UPDATE_FLAGS			0x00FB	/* Handshke rqd. */
#define DSP_VC_GO_COMATOSE			0x00d9

#endif /* !DSP_56361 */


/*
 *
 * Timeouts
 *
 */

#define HANDSHAKE_TIMEOUT		20000	/* send_vector command timeout (20ms) */
#define VECTOR_BUSY_TIMEOUT		100000	/* 100ms */
#define MIDI_OUT_DELAY_USEC		2000	/* How long to wait after MIDI fills up */


/*
 *
 * Flags for .Flags field in the comm page
 *
 */

#define DSP_FLAG_MIDI_INPUT		0x0001	/* Enable MIDI input */
#define DSP_FLAG_SPDIF_NONAUDIO		0x0002	/* Sets the "non-audio" bit
						 * in the S/PDIF out status
						 * bits.  Clear this flag for
						 * audio data;
						 * set it for AC3 or WMA or
						 * some such */
#define DSP_FLAG_PROFESSIONAL_SPDIF	0x0008	/* 1 Professional, 0 Consumer */


/*
 *
 * Clock detect bits reported by the DSP for Gina20, Layla20, Darla24, and Mia
 *
 */

#define GLDM_CLOCK_DETECT_BIT_WORD	0x0002
#define GLDM_CLOCK_DETECT_BIT_SUPER	0x0004
#define GLDM_CLOCK_DETECT_BIT_SPDIF	0x0008
#define GLDM_CLOCK_DETECT_BIT_ESYNC	0x0010


/*
 *
 * Clock detect bits reported by the DSP for Gina24, Mona, and Layla24
 *
 */

#define GML_CLOCK_DETECT_BIT_WORD96	0x0002
#define GML_CLOCK_DETECT_BIT_WORD48	0x0004
#define GML_CLOCK_DETECT_BIT_SPDIF48	0x0008
#define GML_CLOCK_DETECT_BIT_SPDIF96	0x0010
#define GML_CLOCK_DETECT_BIT_WORD	(GML_CLOCK_DETECT_BIT_WORD96 | GML_CLOCK_DETECT_BIT_WORD48)
#define GML_CLOCK_DETECT_BIT_SPDIF	(GML_CLOCK_DETECT_BIT_SPDIF48 | GML_CLOCK_DETECT_BIT_SPDIF96)
#define GML_CLOCK_DETECT_BIT_ESYNC	0x0020
#define GML_CLOCK_DETECT_BIT_ADAT	0x0040


/*
 *
 * Layla clock numbers to send to DSP
 *
 */

#define LAYLA20_CLOCK_INTERNAL		0
#define LAYLA20_CLOCK_SPDIF		1
#define LAYLA20_CLOCK_WORD		2
#define LAYLA20_CLOCK_SUPER		3


/*
 *
 * Gina/Darla clock states
 *
 */

#define GD_CLOCK_NOCHANGE		0
#define GD_CLOCK_44			1
#define GD_CLOCK_48			2
#define GD_CLOCK_SPDIFIN		3
#define GD_CLOCK_UNDEF			0xff


/*
 *
 * Gina/Darla S/PDIF status bits
 *
 */

#define GD_SPDIF_STATUS_NOCHANGE	0
#define GD_SPDIF_STATUS_44		1
#define GD_SPDIF_STATUS_48		2
#define GD_SPDIF_STATUS_UNDEF		0xff


/*
 *
 * Layla20 output clocks
 *
 */

#define LAYLA20_OUTPUT_CLOCK_SUPER	0
#define LAYLA20_OUTPUT_CLOCK_WORD	1


/****************************************************************************

   Magic constants for the Darla24 hardware

 ****************************************************************************/

#define GD24_96000	0x0
#define GD24_48000	0x1
#define GD24_44100	0x2
#define GD24_32000	0x3
#define GD24_22050	0x4
#define GD24_16000	0x5
#define GD24_11025	0x6
#define GD24_8000	0x7
#define GD24_88200	0x8
#define GD24_EXT_SYNC	0x9


/*
 *
 * Return values from the DSP when ASIC is loaded
 *
 */

#define ASIC_ALREADY_LOADED	0x1
#define ASIC_NOT_LOADED		0x0


/*
 *
 * DSP Audio formats
 *
 * These are the audio formats that the DSP can transfer
 * via input and output pipes.  LE means little-endian,
 * BE means big-endian.
 *
 * DSP_AUDIOFORM_MS_8   
 *
 *    8-bit mono unsigned samples.  For playback,
 *    mono data is duplicated out the left and right channels
 *    of the output bus.  The "MS" part of the name
 *    means mono->stereo.
 *
 * DSP_AUDIOFORM_MS_16LE
 *
 *    16-bit signed little-endian mono samples.  Playback works
 *    like the previous code.
 *
 * DSP_AUDIOFORM_MS_24LE
 *
 *    24-bit signed little-endian mono samples.  Data is packed
 *    three bytes per sample; if you had two samples 0x112233 and 0x445566
 *    they would be stored in memory like this: 33 22 11 66 55 44.
 *
 * DSP_AUDIOFORM_MS_32LE
 * 
 *    24-bit signed little-endian mono samples in a 32-bit 
 *    container.  In other words, each sample is a 32-bit signed 
 *    integer, where the actual audio data is left-justified 
 *    in the 32 bits and only the 24 most significant bits are valid.
 *
 * DSP_AUDIOFORM_SS_8
 * DSP_AUDIOFORM_SS_16LE
 * DSP_AUDIOFORM_SS_24LE
 * DSP_AUDIOFORM_SS_32LE
 *
 *    Like the previous ones, except now with stereo interleaved
 *    data.  "SS" means stereo->stereo.
 *
 * DSP_AUDIOFORM_MM_32LE
 *
 *    Similar to DSP_AUDIOFORM_MS_32LE, except that the mono
 *    data is not duplicated out both the left and right outputs.
 *    This mode is used by the ASIO driver.  Here, "MM" means
 *    mono->mono.
 *
 * DSP_AUDIOFORM_MM_32BE
 *
 *    Just like DSP_AUDIOFORM_MM_32LE, but now the data is
 *    in big-endian format.
 *
 */

#define DSP_AUDIOFORM_MS_8	0	/* 8 bit mono */
#define DSP_AUDIOFORM_MS_16LE	1	/* 16 bit mono */
#define DSP_AUDIOFORM_MS_24LE	2	/* 24 bit mono */
#define DSP_AUDIOFORM_MS_32LE	3	/* 32 bit mono */
#define DSP_AUDIOFORM_SS_8	4	/* 8 bit stereo */
#define DSP_AUDIOFORM_SS_16LE	5	/* 16 bit stereo */
#define DSP_AUDIOFORM_SS_24LE	6	/* 24 bit stereo */
#define DSP_AUDIOFORM_SS_32LE	7	/* 32 bit stereo */
#define DSP_AUDIOFORM_MM_32LE	8	/* 32 bit mono->mono little-endian */
#define DSP_AUDIOFORM_MM_32BE	9	/* 32 bit mono->mono big-endian */
#define DSP_AUDIOFORM_SS_32BE	10	/* 32 bit stereo big endian */
#define DSP_AUDIOFORM_INVALID	0xFF	/* Invalid audio format */


/*
 *
 * Super-interleave is defined as interleaving by 4 or more.  Darla20 and Gina20
 * do not support super interleave.
 *
 * 16 bit, 24 bit, and 32 bit little endian samples are supported for super 
 * interleave.  The interleave factor must be even.  16 - way interleave is the 
 * current maximum, so you can interleave by 4, 6, 8, 10, 12, 14, and 16.
 *
 * The actual format code is derived by taking the define below and or-ing with
 * the interleave factor.  So, 32 bit interleave by 6 is 0x86 and
 * 16 bit interleave by 16 is (0x40 | 0x10) = 0x50.
 *
 */

#define DSP_AUDIOFORM_SUPER_INTERLEAVE_16LE	0x40
#define DSP_AUDIOFORM_SUPER_INTERLEAVE_24LE	0xc0
#define DSP_AUDIOFORM_SUPER_INTERLEAVE_32LE	0x80


/*
 *
 * Gina24, Mona, and Layla24 control register defines
 *
 */

#define GML_CONVERTER_ENABLE	0x0010
#define GML_SPDIF_PRO_MODE	0x0020	/* Professional S/PDIF == 1,
					   consumer == 0 */
#define GML_SPDIF_SAMPLE_RATE0	0x0040
#define GML_SPDIF_SAMPLE_RATE1	0x0080
#define GML_SPDIF_TWO_CHANNEL	0x0100	/* 1 == two channels,
					   0 == one channel */
#define GML_SPDIF_NOT_AUDIO	0x0200
#define GML_SPDIF_COPY_PERMIT	0x0400
#define GML_SPDIF_24_BIT	0x0800	/* 1 == 24 bit, 0 == 20 bit */
#define GML_ADAT_MODE		0x1000	/* 1 == ADAT mode, 0 == S/PDIF mode */
#define GML_SPDIF_OPTICAL_MODE	0x2000	/* 1 == optical mode, 0 == RCA mode */
#define GML_SPDIF_CDROM_MODE	0x3000	/* 1 == CDROM mode,
					 * 0 == RCA or optical mode */
#define GML_DOUBLE_SPEED_MODE	0x4000	/* 1 == double speed,
					   0 == single speed */

#define GML_DIGITAL_IN_AUTO_MUTE 0x800000

#define GML_96KHZ		(0x0 | GML_DOUBLE_SPEED_MODE)
#define GML_88KHZ		(0x1 | GML_DOUBLE_SPEED_MODE)
#define GML_48KHZ		0x2
#define GML_44KHZ		0x3
#define GML_32KHZ		0x4
#define GML_22KHZ		0x5
#define GML_16KHZ		0x6
#define GML_11KHZ		0x7
#define GML_8KHZ		0x8
#define GML_SPDIF_CLOCK		0x9
#define GML_ADAT_CLOCK		0xA
#define GML_WORD_CLOCK		0xB
#define GML_ESYNC_CLOCK		0xC
#define GML_ESYNCx2_CLOCK	0xD

#define GML_CLOCK_CLEAR_MASK		0xffffbff0
#define GML_SPDIF_RATE_CLEAR_MASK	(~(GML_SPDIF_SAMPLE_RATE0|GML_SPDIF_SAMPLE_RATE1))
#define GML_DIGITAL_MODE_CLEAR_MASK	0xffffcfff
#define GML_SPDIF_FORMAT_CLEAR_MASK	0xfffff01f


/*
 *
 * Mia sample rate and clock setting constants
 *
 */

#define MIA_32000	0x0040
#define MIA_44100	0x0042
#define MIA_48000	0x0041
#define MIA_88200	0x0142
#define MIA_96000	0x0141

#define MIA_SPDIF	0x00000044
#define MIA_SPDIF96	0x00000144

#define MIA_MIDI_REV	1	/* Must be Mia rev 1 for MIDI support */


/*
 *
 * 3G register bits
 *
 */

#define E3G_CONVERTER_ENABLE	0x0010
#define E3G_SPDIF_PRO_MODE	0x0020	/* Professional S/PDIF == 1,
					   consumer == 0 */
#define E3G_SPDIF_SAMPLE_RATE0	0x0040
#define E3G_SPDIF_SAMPLE_RATE1	0x0080
#define E3G_SPDIF_TWO_CHANNEL	0x0100	/* 1 == two channels,
					   0 == one channel */
#define E3G_SPDIF_NOT_AUDIO	0x0200
#define E3G_SPDIF_COPY_PERMIT	0x0400
#define E3G_SPDIF_24_BIT	0x0800	/* 1 == 24 bit, 0 == 20 bit */
#define E3G_DOUBLE_SPEED_MODE	0x4000	/* 1 == double speed,
					   0 == single speed */
#define E3G_PHANTOM_POWER	0x8000	/* 1 == phantom power on,
					   0 == phantom power off */

#define E3G_96KHZ		(0x0 | E3G_DOUBLE_SPEED_MODE)
#define E3G_88KHZ		(0x1 | E3G_DOUBLE_SPEED_MODE)
#define E3G_48KHZ		0x2
#define E3G_44KHZ		0x3
#define E3G_32KHZ		0x4
#define E3G_22KHZ		0x5
#define E3G_16KHZ		0x6
#define E3G_11KHZ		0x7
#define E3G_8KHZ		0x8
#define E3G_SPDIF_CLOCK		0x9
#define E3G_ADAT_CLOCK		0xA
#define E3G_WORD_CLOCK		0xB
#define E3G_CONTINUOUS_CLOCK	0xE

#define E3G_ADAT_MODE		0x1000
#define E3G_SPDIF_OPTICAL_MODE	0x2000

#define E3G_CLOCK_CLEAR_MASK		0xbfffbff0
#define E3G_DIGITAL_MODE_CLEAR_MASK	0xffffcfff
#define E3G_SPDIF_FORMAT_CLEAR_MASK	0xfffff01f

/* Clock detect bits reported by the DSP */
#define E3G_CLOCK_DETECT_BIT_WORD96	0x0001
#define E3G_CLOCK_DETECT_BIT_WORD48	0x0002
#define E3G_CLOCK_DETECT_BIT_SPDIF48	0x0004
#define E3G_CLOCK_DETECT_BIT_ADAT	0x0004
#define E3G_CLOCK_DETECT_BIT_SPDIF96	0x0008
#define E3G_CLOCK_DETECT_BIT_WORD	(E3G_CLOCK_DETECT_BIT_WORD96|E3G_CLOCK_DETECT_BIT_WORD48)
#define E3G_CLOCK_DETECT_BIT_SPDIF	(E3G_CLOCK_DETECT_BIT_SPDIF48|E3G_CLOCK_DETECT_BIT_SPDIF96)

/* Frequency control register */
#define E3G_MAGIC_NUMBER		677376000
#define E3G_FREQ_REG_DEFAULT		(E3G_MAGIC_NUMBER / 48000 - 2)
#define E3G_FREQ_REG_MAX		0xffff

/* 3G external box types */
#define E3G_GINA3G_BOX_TYPE		0x00
#define E3G_LAYLA3G_BOX_TYPE		0x10
#define E3G_ASIC_NOT_LOADED		0xffff
#define E3G_BOX_TYPE_MASK		0xf0

/* Indigo express control register values */
#define INDIGO_EXPRESS_32000		0x02
#define INDIGO_EXPRESS_44100		0x01
#define INDIGO_EXPRESS_48000		0x00
#define INDIGO_EXPRESS_DOUBLE_SPEED	0x10
#define INDIGO_EXPRESS_QUAD_SPEED	0x04
#define INDIGO_EXPRESS_CLOCK_MASK	0x17


/*
 *
 * Gina20 & Layla20 have input gain controls for the analog inputs;
 * this is the magic number for the hardware that gives you 0 dB at -10.
 *
 */

#define GL20_INPUT_GAIN_MAGIC_NUMBER	0xC8


/*
 *
 * Defines how much time must pass between DSP load attempts
 *
 */

#define DSP_LOAD_ATTEMPT_PERIOD		1000000L	/* One second */


/*
 *
 * Size of arrays for the comm page.  MAX_PLAY_TAPS and MAX_REC_TAPS are
 * no longer used, but the sizes must still be right for the DSP to see
 * the comm page correctly.
 *
 */

#define MONITOR_ARRAY_SIZE	0x180
#define VMIXER_ARRAY_SIZE	0x40
#define MIDI_OUT_BUFFER_SIZE	32
#define MIDI_IN_BUFFER_SIZE	256
#define MAX_PLAY_TAPS		168
#define MAX_REC_TAPS		192
#define DSP_MIDI_OUT_FIFO_SIZE	64


/* sg_entry is a single entry for the scatter-gather list.  The array of struct
sg_entry struct is read by the DSP, so all values must be little-endian. */

#define MAX_SGLIST_ENTRIES 512

struct sg_entry {
	__le32 addr;
	__le32 size;
};


/****************************************************************************

  The comm page.  This structure is read and written by the DSP; the
  DSP code is a firm believer in the byte offsets written in the comments
  at the end of each line.  This structure should not be changed.

  Any reads from or writes to this structure should be in little-endian format.

 ****************************************************************************/

struct comm_page {		/*				Base	Length*/
	__le32 comm_size;	/* size of this object		0x000	4 */
	__le32 flags;		/* See Appendix A below		0x004	4 */
	__le32 unused;		/* Unused entry			0x008	4 */
	__le32 sample_rate;	/* Card sample rate in Hz	0x00c	4 */
	__le32 handshake;	/* DSP command handshake	0x010	4 */
	__le32 cmd_start;	/* Chs. to start mask		0x014	4 */
	__le32 cmd_stop;	/* Chs. to stop mask		0x018	4 */
	__le32 cmd_reset;	/* Chs. to reset mask		0x01c	4 */
	__le16 audio_format[DSP_MAXPIPES];	/* Chs. audio format	0x020	32*2 */
	struct sg_entry sglist_addr[DSP_MAXPIPES];
				/* Chs. Physical sglist addrs	0x060	32*8 */
	__le32 position[DSP_MAXPIPES];
				/* Positions for ea. ch.	0x160	32*4 */
	s8 vu_meter[DSP_MAXPIPES];
				/* VU meters			0x1e0	32*1 */
	s8 peak_meter[DSP_MAXPIPES];
				/* Peak meters			0x200	32*1 */
	s8 line_out_level[DSP_MAXAUDIOOUTPUTS];
				/* Output gain			0x220	16*1 */
	s8 line_in_level[DSP_MAXAUDIOINPUTS];
				/* Input gain			0x230	16*1 */
	s8 monitors[MONITOR_ARRAY_SIZE];
				/* Monitor map			0x240	0x180 */
	__le32 play_coeff[MAX_PLAY_TAPS];
			/* Gina/Darla play filters - obsolete	0x3c0	168*4 */
	__le32 rec_coeff[MAX_REC_TAPS];
			/* Gina/Darla record filters - obsolete	0x660	192*4 */
	__le16 midi_input[MIDI_IN_BUFFER_SIZE];
			/* MIDI input data transfer buffer	0x960	256*2 */
	u8 gd_clock_state;	/* Chg Gina/Darla clock state	0xb60	1 */
	u8 gd_spdif_status;	/* Chg. Gina/Darla S/PDIF state	0xb61	1 */
	u8 gd_resampler_state;	/* Should always be 3		0xb62	1 */
	u8 filler2;		/*				0xb63	1 */
	__le32 nominal_level_mask;	/* -10 level enable mask	0xb64	4 */
	__le16 input_clock;	/* Chg. Input clock state	0xb68	2 */
	__le16 output_clock;	/* Chg. Output clock state	0xb6a	2 */
	__le32 status_clocks;	/* Current Input clock state	0xb6c	4 */
	__le32 ext_box_status;	/* External box status		0xb70	4 */
	__le32 cmd_add_buffer;	/* Pipes to add (obsolete)	0xb74	4 */
	__le32 midi_out_free_count;
			/* # of bytes free in MIDI output FIFO	0xb78	4 */
	__le32 unused2;		/* Cyclic pipes			0xb7c	4 */
	__le32 control_register;
			/* Mona, Gina24, Layla24, 3G ctrl reg	0xb80	4 */
	__le32 e3g_frq_register;	/* 3G frequency register	0xb84	4 */
	u8 filler[24];		/* filler			0xb88	24*1 */
	s8 vmixer[VMIXER_ARRAY_SIZE];
				/* Vmixer levels		0xba0	64*1 */
	u8 midi_output[MIDI_OUT_BUFFER_SIZE];
				/* MIDI output data		0xbe0	32*1 */
};

#endif /* _ECHO_DSP_ */
