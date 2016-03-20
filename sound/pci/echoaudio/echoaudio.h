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

 ****************************************************************************

 Translation from C++ and adaptation for use in ALSA-Driver
 were made by Giuliano Pochini <pochini@shiny.it>

 ****************************************************************************


   Here's a block diagram of how most of the cards work:

                  +-----------+
           record |           |<-------------------- Inputs
          <-------|           |        |
     PCI          | Transport |        |
     bus          |  engine   |       \|/
          ------->|           |    +-------+
            play  |           |--->|monitor|-------> Outputs
                  +-----------+    | mixer |
                                   +-------+

   The lines going to and from the PCI bus represent "pipes".  A pipe performs
   audio transport - moving audio data to and from buffers on the host via
   bus mastering.

   The inputs and outputs on the right represent input and output "busses."
   A bus is a physical, real connection to the outside world.  An example
   of a bus would be the 1/4" analog connectors on the back of Layla or
   an RCA S/PDIF connector.

   For most cards, there is a one-to-one correspondence between outputs
   and busses; that is, each individual pipe is hard-wired to a single bus.

   Cards that work this way are Darla20, Gina20, Layla20, Darla24, Gina24,
   Layla24, Mona, and Indigo.


   Mia has a feature called "virtual outputs."


                  +-----------+
           record |           |<----------------------------- Inputs
          <-------|           |                  |
     PCI          | Transport |                  |
     bus          |  engine   |                 \|/
          ------->|           |   +------+   +-------+
            play  |           |-->|vmixer|-->|monitor|-------> Outputs
                  +-----------+   +------+   | mixer |
                                             +-------+


   Obviously, the difference here is the box labeled "vmixer."  Vmixer is
   short for "virtual output mixer."  For Mia, pipes are *not* hard-wired
   to a single bus; the vmixer lets you mix any pipe to any bus in any
   combination.

   Note, however, that the left-hand side of the diagram is unchanged.
   Transport works exactly the same way - the difference is in the mixer stage.


   Pipes and busses are numbered starting at zero.



   Pipe index
   ==========

   A number of calls in CEchoGals refer to a "pipe index".  A pipe index is
   a unique number for a pipe that unambiguously refers to a playback or record
   pipe.  Pipe indices are numbered starting with analog outputs, followed by
   digital outputs, then analog inputs, then digital inputs.

   Take Gina24 as an example:

   Pipe index

   0-7            Analog outputs (0 .. FirstDigitalBusOut-1)
   8-15           Digital outputs (FirstDigitalBusOut .. NumBussesOut-1)
   16-17          Analog inputs
   18-25          Digital inputs


   You get the pipe index by calling CEchoGals::OpenAudio; the other transport
   functions take the pipe index as a parameter.  If you need a pipe index for
   some other reason, use the handy Makepipe_index method.


   Some calls take a CChannelMask parameter; CChannelMask is a handy way to
   group pipe indices.



   Digital mode switch
   ===================

   Some cards (right now, Gina24, Layla24, and Mona) have a Digital Mode Switch
   or DMS.  Cards with a DMS can be set to one of three mutually exclusive
   digital modes: S/PDIF RCA, S/PDIF optical, or ADAT optical.

   This may create some confusion since ADAT optical is 8 channels wide and
   S/PDIF is only two channels wide.  Gina24, Layla24, and Mona handle this
   by acting as if they always have 8 digital outs and ins.  If you are in
   either S/PDIF mode, the last 6 channels don't do anything - data sent
   out these channels is thrown away and you will always record zeros.

   Note that with Gina24, Layla24, and Mona, sample rates above 50 kHz are
   only available if you have the card configured for S/PDIF optical or S/PDIF
   RCA.



   Double speed mode
   =================

   Some of the cards support 88.2 kHz and 96 kHz sampling (Darla24, Gina24,
   Layla24, Mona, Mia, and Indigo).  For these cards, the driver sometimes has
   to worry about "double speed mode"; double speed mode applies whenever the
   sampling rate is above 50 kHz.

   For instance, Mona and Layla24 support word clock sync.  However, they
   actually support two different word clock modes - single speed (below
   50 kHz) and double speed (above 50 kHz).  The hardware detects if a single
   or double speed word clock signal is present; the generic code uses that
   information to determine which mode to use.

   The generic code takes care of all this for you.
*/


#ifndef _ECHOAUDIO_H_
#define _ECHOAUDIO_H_


#include "echoaudio_dsp.h"



/***********************************************************************

	PCI configuration space

***********************************************************************/

/*
 * PCI vendor ID and device IDs for the hardware
 */
#define VENDOR_ID		0x1057
#define DEVICE_ID_56301		0x1801
#define DEVICE_ID_56361		0x3410
#define SUBVENDOR_ID		0xECC0


/*
 * Valid Echo PCI subsystem card IDs
 */
#define DARLA20			0x0010
#define GINA20			0x0020
#define LAYLA20			0x0030
#define DARLA24			0x0040
#define GINA24			0x0050
#define LAYLA24			0x0060
#define MONA			0x0070
#define MIA			0x0080
#define INDIGO			0x0090
#define INDIGO_IO		0x00a0
#define INDIGO_DJ		0x00b0
#define DC8			0x00c0
#define INDIGO_IOX		0x00d0
#define INDIGO_DJX		0x00e0
#define ECHO3G			0x0100


/************************************************************************

	Array sizes and so forth

***********************************************************************/

/*
 * Sizes
 */
#define ECHO_MAXAUDIOINPUTS	32	/* Max audio input channels */
#define ECHO_MAXAUDIOOUTPUTS	32	/* Max audio output channels */
#define ECHO_MAXAUDIOPIPES	32	/* Max number of input and output
					 * pipes */
#define E3G_MAX_OUTPUTS		16
#define ECHO_MAXMIDIJACKS	1	/* Max MIDI ports */
#define ECHO_MIDI_QUEUE_SZ 	512	/* Max MIDI input queue entries */
#define ECHO_MTC_QUEUE_SZ	32	/* Max MIDI time code input queue
					 * entries */

/*
 * MIDI activity indicator timeout
 */
#define MIDI_ACTIVITY_TIMEOUT_USEC	200000


/****************************************************************************
 
   Clocks

*****************************************************************************/

/*
 * Clock numbers
 */
#define ECHO_CLOCK_INTERNAL		0
#define ECHO_CLOCK_WORD			1
#define ECHO_CLOCK_SUPER		2
#define ECHO_CLOCK_SPDIF		3
#define ECHO_CLOCK_ADAT			4
#define ECHO_CLOCK_ESYNC		5
#define ECHO_CLOCK_ESYNC96		6
#define ECHO_CLOCK_MTC			7
#define ECHO_CLOCK_NUMBER		8
#define ECHO_CLOCKS			0xffff

/*
 * Clock bit numbers - used to report capabilities and whatever clocks
 * are being detected dynamically.
 */
#define ECHO_CLOCK_BIT_INTERNAL		(1 << ECHO_CLOCK_INTERNAL)
#define ECHO_CLOCK_BIT_WORD		(1 << ECHO_CLOCK_WORD)
#define ECHO_CLOCK_BIT_SUPER		(1 << ECHO_CLOCK_SUPER)
#define ECHO_CLOCK_BIT_SPDIF		(1 << ECHO_CLOCK_SPDIF)
#define ECHO_CLOCK_BIT_ADAT		(1 << ECHO_CLOCK_ADAT)
#define ECHO_CLOCK_BIT_ESYNC		(1 << ECHO_CLOCK_ESYNC)
#define ECHO_CLOCK_BIT_ESYNC96		(1 << ECHO_CLOCK_ESYNC96)
#define ECHO_CLOCK_BIT_MTC		(1<<ECHO_CLOCK_MTC)


/***************************************************************************

   Digital modes

****************************************************************************/

/*
 * Digital modes for Mona, Layla24, and Gina24
 */
#define DIGITAL_MODE_NONE			0xFF
#define DIGITAL_MODE_SPDIF_RCA			0
#define DIGITAL_MODE_SPDIF_OPTICAL		1
#define DIGITAL_MODE_ADAT			2
#define DIGITAL_MODE_SPDIF_CDROM		3
#define DIGITAL_MODES				4

/*
 * Digital mode capability masks
 */
#define ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_RCA	(1 << DIGITAL_MODE_SPDIF_RCA)
#define ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_OPTICAL	(1 << DIGITAL_MODE_SPDIF_OPTICAL)
#define ECHOCAPS_HAS_DIGITAL_MODE_ADAT		(1 << DIGITAL_MODE_ADAT)
#define ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_CDROM	(1 << DIGITAL_MODE_SPDIF_CDROM)


#define EXT_3GBOX_NC			0x01	/* 3G box not connected */
#define EXT_3GBOX_NOT_SET		0x02	/* 3G box not detected yet */


#define ECHOGAIN_MUTED		(-128)	/* Minimum possible gain */
#define ECHOGAIN_MINOUT		(-128)	/* Min output gain (dB) */
#define ECHOGAIN_MAXOUT		(6)	/* Max output gain (dB) */
#define ECHOGAIN_MININP		(-50)	/* Min input gain (0.5 dB) */
#define ECHOGAIN_MAXINP		(50)	/* Max input gain (0.5 dB) */

#define PIPE_STATE_STOPPED	0	/* Pipe has been reset */
#define PIPE_STATE_PAUSED	1	/* Pipe has been stopped */
#define PIPE_STATE_STARTED	2	/* Pipe has been started */
#define PIPE_STATE_PENDING	3	/* Pipe has pending start */



struct audiopipe {
	volatile u32 *dma_counter;	/* Commpage register that contains
					 * the current dma position
					 * (lower 32 bits only)
					 */
	u32 last_counter;		/* The last position, which is used
					 * to compute...
					 */
	u32 position;			/* ...the number of bytes tranferred
					 * by the DMA engine, modulo the
					 * buffer size
					 */
	short index;			/* Index of the first channel or <0
					 * if hw is not configured yet
					 */
	short interleave;
	struct snd_dma_buffer sgpage;	/* Room for the scatter-gather list */
	struct snd_pcm_hardware hw;
	struct snd_pcm_hw_constraint_list constr;
	short sglist_head;
	char state;			/* pipe state */
};


struct audioformat {
	u8 interleave;			/* How the data is arranged in memory:
					 * mono = 1, stereo = 2, ...
					 */
	u8 bits_per_sample;		/* 8, 16, 24, 32 (24 bits left aligned) */
	char mono_to_stereo;		/* Only used if interleave is 1 and
					 * if this is an output pipe.
					 */
	char data_are_bigendian;	/* 1 = big endian, 0 = little endian */
};


struct echoaudio {
	spinlock_t lock;
	struct snd_pcm_substream *substream[DSP_MAXPIPES];
	int last_period[DSP_MAXPIPES];
	struct mutex mode_mutex;
	u16 num_digital_modes, digital_mode_list[6];
	u16 num_clock_sources, clock_source_list[10];
	atomic_t opencount;
	struct snd_kcontrol *clock_src_ctl;
	struct snd_pcm *analog_pcm, *digital_pcm;
	struct snd_card *card;
	const char *card_name;
	struct pci_dev *pci;
	unsigned long dsp_registers_phys;
	struct resource *iores;
	struct snd_dma_buffer commpage_dma_buf;
	int irq;
#ifdef ECHOCARD_HAS_MIDI
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *midi_in, *midi_out;
#endif
	struct timer_list timer;
	char tinuse;				/* Timer in use */
	char midi_full;				/* MIDI output buffer is full */
	char can_set_rate;
	char rate_set;

	/* This stuff is used mainly by the lowlevel code */
	struct comm_page *comm_page;	/* Virtual address of the memory
					 * seen by DSP
					 */
	u32 pipe_alloc_mask;		/* Bitmask of allocated pipes */
	u32 pipe_cyclic_mask;		/* Bitmask of pipes with cyclic
					 * buffers
					 */
	u32 sample_rate;		/* Card sample rate in Hz */
	u8 digital_mode;		/* Current digital mode
					 * (see DIGITAL_MODE_*)
					 */
	u8 spdif_status;		/* Gina20, Darla20, Darla24 - only */
	u8 clock_state;			/* Gina20, Darla20, Darla24 - only */
	u8 input_clock;			/* Currently selected sample clock
					 * source
					 */
	u8 output_clock;		/* Layla20 only */
	char meters_enabled;		/* VU-meters status */
	char asic_loaded;		/* Set true when ASIC loaded */
	char bad_board;			/* Set true if DSP won't load */
	char professional_spdif;	/* 0 = consumer; 1 = professional */
	char non_audio_spdif;		/* 3G - only */
	char digital_in_automute;	/* Gina24, Layla24, Mona - only */
	char has_phantom_power;
	char hasnt_input_nominal_level;	/* Gina3G */
	char phantom_power;		/* Gina3G - only */
	char has_midi;
	char midi_input_enabled;

#ifdef ECHOCARD_ECHO3G
	/* External module -dependent pipe and bus indexes */
	char px_digital_out, px_analog_in, px_digital_in, px_num;
	char bx_digital_out, bx_analog_in, bx_digital_in, bx_num;
#endif

	char nominal_level[ECHO_MAXAUDIOPIPES];	/* True == -10dBV
						 * False == +4dBu */
	s8 input_gain[ECHO_MAXAUDIOINPUTS];	/* Input level -50..+50
						 * unit is 0.5dB */
	s8 output_gain[ECHO_MAXAUDIOOUTPUTS];	/* Output level -128..+6 dB
						 * (-128=muted) */
	s8 monitor_gain[ECHO_MAXAUDIOOUTPUTS][ECHO_MAXAUDIOINPUTS];
		/* -128..+6 dB */
	s8 vmixer_gain[ECHO_MAXAUDIOOUTPUTS][ECHO_MAXAUDIOOUTPUTS];
		/* -128..+6 dB */

	u16 digital_modes;		/* Bitmask of supported modes
					 * (see ECHOCAPS_HAS_DIGITAL_MODE_*) */
	u16 input_clock_types;		/* Suppoted input clock types */
	u16 output_clock_types;		/* Suppoted output clock types -
					 * Layla20 only */
	u16 device_id, subdevice_id;
	u16 *dsp_code;			/* Current DSP code loaded,
					 * NULL if nothing loaded */
	short dsp_code_to_load;		/* DSP code to load */
	short asic_code;		/* Current ASIC code */
	u32 comm_page_phys;			/* Physical address of the
						 * memory seen by DSP */
	volatile u32 __iomem *dsp_registers;	/* DSP's register base */
	u32 active_mask;			/* Chs. active mask or
						 * punks out */
#ifdef CONFIG_PM_SLEEP
	const struct firmware *fw_cache[8];	/* Cached firmwares */
#endif

#ifdef ECHOCARD_HAS_MIDI
	u16 mtc_state;				/* State for MIDI input parsing state machine */
	u8 midi_buffer[MIDI_IN_BUFFER_SIZE];
#endif
};


static int init_dsp_comm_page(struct echoaudio *chip);
static int init_line_levels(struct echoaudio *chip);
static int free_pipes(struct echoaudio *chip, struct audiopipe *pipe);
static int load_firmware(struct echoaudio *chip);
static int wait_handshake(struct echoaudio *chip);
static int send_vector(struct echoaudio *chip, u32 command);
static int get_firmware(const struct firmware **fw_entry,
			struct echoaudio *chip, const short fw_index);
static void free_firmware(const struct firmware *fw_entry,
			  struct echoaudio *chip);

#ifdef ECHOCARD_HAS_MIDI
static int enable_midi_input(struct echoaudio *chip, char enable);
static void snd_echo_midi_output_trigger(
			struct snd_rawmidi_substream *substream, int up);
static int midi_service_irq(struct echoaudio *chip);
static int snd_echo_midi_create(struct snd_card *card,
				struct echoaudio *chip);
#endif


static inline void clear_handshake(struct echoaudio *chip)
{
	chip->comm_page->handshake = 0;
}

static inline u32 get_dsp_register(struct echoaudio *chip, u32 index)
{
	return readl(&chip->dsp_registers[index]);
}

static inline void set_dsp_register(struct echoaudio *chip, u32 index,
				    u32 value)
{
	writel(value, &chip->dsp_registers[index]);
}


/* Pipe and bus indexes. PX_* and BX_* are defined as chip->px_* and chip->bx_*
for 3G cards because they depend on the external box. They are integer
constants for all other cards.
Never use those defines directly, use the following functions instead. */

static inline int px_digital_out(const struct echoaudio *chip)
{
	return PX_DIGITAL_OUT;
}

static inline int px_analog_in(const struct echoaudio *chip)
{
	return PX_ANALOG_IN;
}

static inline int px_digital_in(const struct echoaudio *chip)
{
	return PX_DIGITAL_IN;
}

static inline int px_num(const struct echoaudio *chip)
{
	return PX_NUM;
}

static inline int bx_digital_out(const struct echoaudio *chip)
{
	return BX_DIGITAL_OUT;
}

static inline int bx_analog_in(const struct echoaudio *chip)
{
	return BX_ANALOG_IN;
}

static inline int bx_digital_in(const struct echoaudio *chip)
{
	return BX_DIGITAL_IN;
}

static inline int bx_num(const struct echoaudio *chip)
{
	return BX_NUM;
}

static inline int num_pipes_out(const struct echoaudio *chip)
{
	return px_analog_in(chip);
}

static inline int num_pipes_in(const struct echoaudio *chip)
{
	return px_num(chip) - px_analog_in(chip);
}

static inline int num_busses_out(const struct echoaudio *chip)
{
	return bx_analog_in(chip);
}

static inline int num_busses_in(const struct echoaudio *chip)
{
	return bx_num(chip) - bx_analog_in(chip);
}

static inline int num_analog_busses_out(const struct echoaudio *chip)
{
	return bx_digital_out(chip);
}

static inline int num_analog_busses_in(const struct echoaudio *chip)
{
	return bx_digital_in(chip) - bx_analog_in(chip);
}

static inline int num_digital_busses_out(const struct echoaudio *chip)
{
	return num_busses_out(chip) - num_analog_busses_out(chip);
}

static inline int num_digital_busses_in(const struct echoaudio *chip)
{
	return num_busses_in(chip) - num_analog_busses_in(chip);
}

/* The monitor array is a one-dimensional array; compute the offset
 * into the array */
static inline int monitor_index(const struct echoaudio *chip, int out, int in)
{
	return out * num_busses_in(chip) + in;
}


#ifndef pci_device
#define pci_device(chip) (&chip->pci->dev)
#endif


#endif /* _ECHOAUDIO_H_ */
