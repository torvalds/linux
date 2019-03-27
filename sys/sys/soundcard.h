/*
 * soundcard.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright by Hannu Savolainen 1993 / 4Front Technologies 1993-2006
 * Modified for the new FreeBSD sound driver by Luigi Rizzo, 1997
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Unless coordinating changes with 4Front Technologies, do NOT make any
 * modifications to ioctl commands, types, etc. that would break
 * compatibility with the OSS API.
 */

#ifndef _SYS_SOUNDCARD_H_
#define _SYS_SOUNDCARD_H_
 /*
  * If you make modifications to this file, please contact me before
  * distributing the modified version. There is already enough
  * diversity in the world.
  *
  * Regards,
  * Hannu Savolainen
  * hannu@voxware.pp.fi
  *
  **********************************************************************
  * PS.	The Hacker's Guide to VoxWare available from
  *     nic.funet.fi:pub/Linux/ALPHA/sound. The file is
  *	snd-sdk-doc-0.1.ps.gz (gzipped postscript). It contains
  *	some useful information about programming with VoxWare.
  *	(NOTE! The pub/Linux/ALPHA/ directories are hidden. You have
  *	to cd inside them before the files are accessible.)
  **********************************************************************
  */

/*
 * SOUND_VERSION is only used by the voxware driver. Hopefully apps
 * should not depend on it, but rather look at the capabilities
 * of the driver in the kernel!
 */
#define SOUND_VERSION  301
#define VOXWARE		/* does this have any use ? */

/*
 * Supported card ID numbers (Should be somewhere else? We keep
 * them here just for compativility with the old driver, but these
 * constants are of little or no use).
 */

#define SNDCARD_ADLIB          1
#define SNDCARD_SB             2
#define SNDCARD_PAS            3
#define SNDCARD_GUS            4
#define SNDCARD_MPU401         5
#define SNDCARD_SB16           6
#define SNDCARD_SB16MIDI       7
#define SNDCARD_UART6850       8
#define SNDCARD_GUS16          9
#define SNDCARD_MSS            10
#define SNDCARD_PSS            11
#define SNDCARD_SSCAPE         12
#define SNDCARD_PSS_MPU        13
#define SNDCARD_PSS_MSS        14
#define SNDCARD_SSCAPE_MSS     15
#define SNDCARD_TRXPRO         16
#define SNDCARD_TRXPRO_SB      17
#define SNDCARD_TRXPRO_MPU     18
#define SNDCARD_MAD16          19
#define SNDCARD_MAD16_MPU      20
#define SNDCARD_CS4232         21
#define SNDCARD_CS4232_MPU     22
#define SNDCARD_MAUI           23
#define SNDCARD_PSEUDO_MSS     24
#define SNDCARD_AWE32          25
#define SNDCARD_NSS            26
#define SNDCARD_UART16550      27
#define SNDCARD_OPL            28

#include <sys/types.h>
#include <machine/endian.h>
#ifndef _IOWR
#include <sys/ioccom.h>
#endif  /* !_IOWR */

/*
 * The first part of this file contains the new FreeBSD sound ioctl
 * interface. Tries to minimize the number of different ioctls, and
 * to be reasonably general.
 *
 * 970821: some of the new calls have not been implemented yet.
 */

/*
 * the following three calls extend the generic file descriptor
 * interface. AIONWRITE is the dual of FIONREAD, i.e. returns the max
 * number of bytes for a write operation to be non-blocking.
 *
 * AIOGSIZE/AIOSSIZE are used to change the behaviour of the device,
 * from a character device (default) to a block device. In block mode,
 * (not to be confused with blocking mode) the main difference for the
 * application is that select() will return only when a complete
 * block can be read/written to the device, whereas in character mode
 * select will return true when one byte can be exchanged. For audio
 * devices, character mode makes select almost useless since one byte
 * will always be ready by the next sample time (which is often only a
 * handful of microseconds away).
 * Use a size of 0 or 1 to return to character mode.
 */
#define	AIONWRITE   _IOR('A', 10, int)   /* get # bytes to write */
struct snd_size {
    int play_size;
    int rec_size;
};
#define	AIOGSIZE    _IOR('A', 11, struct snd_size)/* read current blocksize */
#define	AIOSSIZE    _IOWR('A', 11, struct snd_size)  /* sets blocksize */

/*
 * The following constants define supported audio formats. The
 * encoding follows voxware conventions, i.e. 1 bit for each supported
 * format. We extend it by using bit 31 (RO) to indicate full-duplex
 * capability, and bit 29 (RO) to indicate that the card supports/
 * needs different formats on capture & playback channels.
 * Bit 29 (RW) is used to indicate/ask stereo.
 *
 * The number of bits required to store the sample is:
 *  o  4 bits for the IDA ADPCM format,
 *  o  8 bits for 8-bit formats, mu-law and A-law,
 *  o  16 bits for the 16-bit formats, and
 *  o  32 bits for the 24/32-bit formats.
 *  o  undefined for the MPEG audio format.
 */

#define AFMT_QUERY	0x00000000	/* Return current format */
#define AFMT_MU_LAW	0x00000001	/* Logarithmic mu-law */
#define AFMT_A_LAW	0x00000002	/* Logarithmic A-law */
#define AFMT_IMA_ADPCM	0x00000004	/* A 4:1 compressed format where 16-bit
					 * squence represented using the
					 * the average 4 bits per sample */
#define AFMT_U8		0x00000008	/* Unsigned 8-bit */
#define AFMT_S16_LE	0x00000010	/* Little endian signed 16-bit */
#define AFMT_S16_BE	0x00000020	/* Big endian signed 16-bit */
#define AFMT_S8		0x00000040	/* Signed 8-bit */
#define AFMT_U16_LE	0x00000080	/* Little endian unsigned 16-bit */
#define AFMT_U16_BE	0x00000100	/* Big endian unsigned 16-bit */
#define AFMT_MPEG	0x00000200	/* MPEG MP2/MP3 audio */
#define AFMT_AC3	0x00000400	/* Dolby Digital AC3 */

/*
 * 32-bit formats below used for 24-bit audio data where the data is stored
 * in the 24 most significant bits and the least significant bits are not used
 * (should be set to 0).
 */
#define AFMT_S32_LE	0x00001000	/* Little endian signed 32-bit */
#define AFMT_S32_BE	0x00002000	/* Big endian signed 32-bit */
#define AFMT_U32_LE	0x00004000	/* Little endian unsigned 32-bit */
#define AFMT_U32_BE	0x00008000	/* Big endian unsigned 32-bit */
#define AFMT_S24_LE	0x00010000	/* Little endian signed 24-bit */
#define AFMT_S24_BE	0x00020000	/* Big endian signed 24-bit */
#define AFMT_U24_LE	0x00040000	/* Little endian unsigned 24-bit */
#define AFMT_U24_BE	0x00080000	/* Big endian unsigned 24-bit */

/* Machine dependent AFMT_* definitions. */
#if BYTE_ORDER == LITTLE_ENDIAN
#define AFMT_S16_NE	AFMT_S16_LE
#define AFMT_S24_NE	AFMT_S24_LE
#define AFMT_S32_NE	AFMT_S32_LE
#define AFMT_U16_NE	AFMT_U16_LE
#define AFMT_U24_NE	AFMT_U24_LE
#define AFMT_U32_NE	AFMT_U32_LE
#define AFMT_S16_OE	AFMT_S16_BE
#define AFMT_S24_OE	AFMT_S24_BE
#define AFMT_S32_OE	AFMT_S32_BE
#define AFMT_U16_OE	AFMT_U16_BE
#define AFMT_U24_OE	AFMT_U24_BE
#define AFMT_U32_OE	AFMT_U32_BE
#else
#define AFMT_S16_OE	AFMT_S16_LE
#define AFMT_S24_OE	AFMT_S24_LE
#define AFMT_S32_OE	AFMT_S32_LE
#define AFMT_U16_OE	AFMT_U16_LE
#define AFMT_U24_OE	AFMT_U24_LE
#define AFMT_U32_OE	AFMT_U32_LE
#define AFMT_S16_NE	AFMT_S16_BE
#define AFMT_S24_NE	AFMT_S24_BE
#define AFMT_S32_NE	AFMT_S32_BE
#define AFMT_U16_NE	AFMT_U16_BE
#define AFMT_U24_NE	AFMT_U24_BE
#define AFMT_U32_NE	AFMT_U32_BE
#endif

#define AFMT_STEREO	0x10000000	/* can do/want stereo	*/

/*
 * the following are really capabilities
 */
#define AFMT_WEIRD	0x20000000	/* weird hardware...	*/
    /*
     * AFMT_WEIRD reports that the hardware might need to operate
     * with different formats in the playback and capture
     * channels when operating in full duplex.
     * As an example, SoundBlaster16 cards only support U8 in one
     * direction and S16 in the other one, and applications should
     * be aware of this limitation.
     */
#define AFMT_FULLDUPLEX	0x80000000	/* can do full duplex	*/

/*
 * The following structure is used to get/set format and sampling rate.
 * While it would be better to have things such as stereo, bits per
 * sample, endiannes, etc split in different variables, it turns out
 * that formats are not that many, and not all combinations are possible.
 * So we followed the Voxware approach of associating one bit to each
 * format.
 */

typedef struct _snd_chan_param {
    u_long	play_rate;	/* sampling rate			*/
    u_long	rec_rate;	/* sampling rate			*/
    u_long	play_format;	/* everything describing the format	*/
    u_long	rec_format;	/* everything describing the format	*/
} snd_chan_param;
#define	AIOGFMT    _IOR('f', 12, snd_chan_param)   /* get format */
#define	AIOSFMT    _IOWR('f', 12, snd_chan_param)  /* sets format */

/*
 * The following structure is used to get/set the mixer setting.
 * Up to 32 mixers are supported, each one with up to 32 channels.
 */
typedef struct _snd_mix_param {
    u_char	subdev;	/* which output				*/
    u_char	line;	/* which input				*/
    u_char	left,right; /* volumes, 0..255, 0 = mute	*/
} snd_mix_param ;

/* XXX AIOGMIX, AIOSMIX not implemented yet */
#define AIOGMIX	_IOWR('A', 13, snd_mix_param)	/* return mixer status */
#define AIOSMIX	_IOWR('A', 14, snd_mix_param)	/* sets mixer status   */

/*
 * channel specifiers used in AIOSTOP and AIOSYNC
 */
#define	AIOSYNC_PLAY	0x1	/* play chan */
#define	AIOSYNC_CAPTURE	0x2	/* capture chan */
/* AIOSTOP stop & flush a channel, returns the residual count */
#define	AIOSTOP	_IOWR ('A', 15, int)

/* alternate method used to notify the sync condition */
#define	AIOSYNC_SIGNAL	0x100
#define	AIOSYNC_SELECT	0x200

/* what the 'pos' field refers to */
#define AIOSYNC_READY	0x400
#define AIOSYNC_FREE	0x800

typedef struct _snd_sync_parm {
    long chan ; /* play or capture channel, plus modifier */
    long pos;
} snd_sync_parm;
#define	AIOSYNC	_IOWR ('A', 15, snd_sync_parm)	/* misc. synchronization */

/*
 * The following is used to return device capabilities. If the structure
 * passed to the ioctl is zeroed, default values are returned for rate
 * and formats, a bitmap of available mixers is returned, and values
 * (inputs, different levels) for the first one are returned.
 *
 * If  formats, mixers, inputs are instantiated, then detailed info
 * are returned depending on the call.
 */
typedef struct _snd_capabilities {
    u_long	rate_min, rate_max;	/* min-max sampling rate */
    u_long	formats;
    u_long	bufsize; /* DMA buffer size */
    u_long	mixers; /* bitmap of available mixers */
    u_long	inputs; /* bitmap of available inputs (per mixer) */
    u_short	left, right;	/* how many levels are supported */
} snd_capabilities;
#define AIOGCAP	_IOWR('A', 15, snd_capabilities)	/* get capabilities */

/*
 * here is the old (Voxware) ioctl interface
 */

/*
 * IOCTL Commands for /dev/sequencer
 */

#define SNDCTL_SEQ_RESET	_IO  ('Q', 0)
#define SNDCTL_SEQ_SYNC		_IO  ('Q', 1)
#define SNDCTL_SYNTH_INFO	_IOWR('Q', 2, struct synth_info)
#define SNDCTL_SEQ_CTRLRATE	_IOWR('Q', 3, int) /* Set/get timer res.(hz) */
#define SNDCTL_SEQ_GETOUTCOUNT	_IOR ('Q', 4, int)
#define SNDCTL_SEQ_GETINCOUNT	_IOR ('Q', 5, int)
#define SNDCTL_SEQ_PERCMODE	_IOW ('Q', 6, int)
#define SNDCTL_FM_LOAD_INSTR	_IOW ('Q', 7, struct sbi_instrument)	/* Valid for FM only */
#define SNDCTL_SEQ_TESTMIDI	_IOW ('Q', 8, int)
#define SNDCTL_SEQ_RESETSAMPLES	_IOW ('Q', 9, int)
#define SNDCTL_SEQ_NRSYNTHS	_IOR ('Q',10, int)
#define SNDCTL_SEQ_NRMIDIS	_IOR ('Q',11, int)
#define SNDCTL_MIDI_INFO	_IOWR('Q',12, struct midi_info)
#define SNDCTL_SEQ_THRESHOLD	_IOW ('Q',13, int)
#define SNDCTL_SEQ_TRESHOLD	SNDCTL_SEQ_THRESHOLD	/* there was once a typo */
#define SNDCTL_SYNTH_MEMAVL	_IOWR('Q',14, int) /* in=dev#, out=memsize */
#define SNDCTL_FM_4OP_ENABLE	_IOW ('Q',15, int) /* in=dev# */
#define SNDCTL_PMGR_ACCESS	_IOWR('Q',16, struct patmgr_info)
#define SNDCTL_SEQ_PANIC	_IO  ('Q',17)
#define SNDCTL_SEQ_OUTOFBAND	_IOW ('Q',18, struct seq_event_rec)
#define SNDCTL_SEQ_GETTIME	_IOR ('Q',19, int)

struct seq_event_rec {
	u_char arr[8];
};

#define SNDCTL_TMR_TIMEBASE	_IOWR('T', 1, int)
#define SNDCTL_TMR_START	_IO  ('T', 2)
#define SNDCTL_TMR_STOP		_IO  ('T', 3)
#define SNDCTL_TMR_CONTINUE	_IO  ('T', 4)
#define SNDCTL_TMR_TEMPO	_IOWR('T', 5, int)
#define SNDCTL_TMR_SOURCE	_IOWR('T', 6, int)
#   define TMR_INTERNAL		0x00000001
#   define TMR_EXTERNAL		0x00000002
#	define TMR_MODE_MIDI	0x00000010
#	define TMR_MODE_FSK	0x00000020
#	define TMR_MODE_CLS	0x00000040
#	define TMR_MODE_SMPTE	0x00000080
#define SNDCTL_TMR_METRONOME	_IOW ('T', 7, int)
#define SNDCTL_TMR_SELECT	_IOW ('T', 8, int)

/*
 *	Endian aware patch key generation algorithm.
 */

#if defined(_AIX) || defined(AIX)
#  define _PATCHKEY(id) (0xfd00|id)
#else
#  define _PATCHKEY(id) ((id<<8)|0xfd)
#endif

/*
 *	Sample loading mechanism for internal synthesizers (/dev/sequencer)
 *	The following patch_info structure has been designed to support
 *	Gravis UltraSound. It tries to be universal format for uploading
 *	sample based patches but is probably too limited.
 */

struct patch_info {
/*		u_short key;		 Use GUS_PATCH here */
	short key;		 /* Use GUS_PATCH here */
#define GUS_PATCH	_PATCHKEY(0x04)
#define OBSOLETE_GUS_PATCH	_PATCHKEY(0x02)

	short device_no;	/* Synthesizer number */
	short instr_no;		/* Midi pgm# */

	u_long mode;
/*
 * The least significant byte has the same format than the GUS .PAT
 * files
 */
#define WAVE_16_BITS	0x01	/* bit 0 = 8 or 16 bit wave data. */
#define WAVE_UNSIGNED	0x02	/* bit 1 = Signed - Unsigned data. */
#define WAVE_LOOPING	0x04	/* bit 2 = looping enabled-1. */
#define WAVE_BIDIR_LOOP	0x08	/* bit 3 = Set is bidirectional looping. */
#define WAVE_LOOP_BACK	0x10	/* bit 4 = Set is looping backward. */
#define WAVE_SUSTAIN_ON	0x20	/* bit 5 = Turn sustaining on. (Env. pts. 3)*/
#define WAVE_ENVELOPES	0x40	/* bit 6 = Enable envelopes - 1 */
				/* 	(use the env_rate/env_offs fields). */
/* Linux specific bits */
#define WAVE_VIBRATO	0x00010000	/* The vibrato info is valid */
#define WAVE_TREMOLO	0x00020000	/* The tremolo info is valid */
#define WAVE_SCALE	0x00040000	/* The scaling info is valid */
/* Other bits must be zeroed */

	long len;	/* Size of the wave data in bytes */
	long loop_start, loop_end; /* Byte offsets from the beginning */

/*
 * The base_freq and base_note fields are used when computing the
 * playback speed for a note. The base_note defines the tone frequency
 * which is heard if the sample is played using the base_freq as the
 * playback speed.
 *
 * The low_note and high_note fields define the minimum and maximum note
 * frequencies for which this sample is valid. It is possible to define
 * more than one samples for an instrument number at the same time. The
 * low_note and high_note fields are used to select the most suitable one.
 *
 * The fields base_note, high_note and low_note should contain
 * the note frequency multiplied by 1000. For example value for the
 * middle A is 440*1000.
 */

	u_int base_freq;
	u_long base_note;
	u_long high_note;
	u_long low_note;
	int panning;	/* -128=left, 127=right */
	int detuning;

/*	New fields introduced in version 1.99.5	*/

       /* Envelope. Enabled by mode bit WAVE_ENVELOPES	*/
	u_char	env_rate[ 6 ];	 /* GUS HW ramping rate */
	u_char	env_offset[ 6 ]; /* 255 == 100% */

	/*
	 * The tremolo, vibrato and scale info are not supported yet.
	 * Enable by setting the mode bits WAVE_TREMOLO, WAVE_VIBRATO or
	 * WAVE_SCALE
	 */

	u_char	tremolo_sweep;
	u_char	tremolo_rate;
	u_char	tremolo_depth;

	u_char	vibrato_sweep;
	u_char	vibrato_rate;
	u_char	vibrato_depth;

	int		scale_frequency;
	u_int	scale_factor;		/* from 0 to 2048 or 0 to 2 */

	int		volume;
	int		spare[4];
	char data[1];	/* The waveform data starts here */
};

struct sysex_info {
	short key;		/* Use GUS_PATCH here */
#define SYSEX_PATCH	_PATCHKEY(0x05)
#define MAUI_PATCH	_PATCHKEY(0x06)
	short device_no;	/* Synthesizer number */
	long len;	/* Size of the sysex data in bytes */
	u_char data[1];	/* Sysex data starts here */
};

/*
 * Patch management interface (/dev/sequencer, /dev/patmgr#)
 * Don't use these calls if you want to maintain compatibility with
 * the future versions of the driver.
 */

#define PS_NO_PATCHES		0	/* No patch support on device */
#define	PS_MGR_NOT_OK		1	/* Plain patch support (no mgr) */
#define	PS_MGR_OK		2	/* Patch manager supported */
#define	PS_MANAGED		3	/* Patch manager running */

#define SNDCTL_PMGR_IFACE		_IOWR('P', 1, struct patmgr_info)

/*
 * The patmgr_info is a fixed size structure which is used for two
 * different purposes. The intended use is for communication between
 * the application using /dev/sequencer and the patch manager daemon
 * associated with a synthesizer device (ioctl(SNDCTL_PMGR_ACCESS)).
 *
 * This structure is also used with ioctl(SNDCTL_PGMR_IFACE) which allows
 * a patch manager daemon to read and write device parameters. This
 * ioctl available through /dev/sequencer also. Avoid using it since it's
 * extremely hardware dependent. In addition access trough /dev/sequencer
 * may confuse the patch manager daemon.
 */

struct patmgr_info {	/* Note! size must be < 4k since kmalloc() is used */
	  u_long key;	/* Don't worry. Reserved for communication
	  			   between the patch manager and the driver. */
#define PM_K_EVENT		1 /* Event from the /dev/sequencer driver */
#define PM_K_COMMAND		2 /* Request from an application */
#define PM_K_RESPONSE		3 /* From patmgr to application */
#define PM_ERROR		4 /* Error returned by the patmgr */
	  int device;
	  int command;

/*
 * Commands 0x000 to 0xfff reserved for patch manager programs
 */
#define PM_GET_DEVTYPE	1	/* Returns type of the patch mgr interface of dev */
#define		PMTYPE_FM2	1	/* 2 OP fm */
#define		PMTYPE_FM4	2	/* Mixed 4 or 2 op FM (OPL-3) */
#define		PMTYPE_WAVE	3	/* Wave table synthesizer (GUS) */
#define PM_GET_NRPGM	2	/* Returns max # of midi programs in parm1 */
#define PM_GET_PGMMAP	3	/* Returns map of loaded midi programs in data8 */
#define PM_GET_PGM_PATCHES 4	/* Return list of patches of a program (parm1) */
#define PM_GET_PATCH	5	/* Return patch header of patch parm1 */
#define PM_SET_PATCH	6	/* Set patch header of patch parm1 */
#define PM_READ_PATCH	7	/* Read patch (wave) data */
#define PM_WRITE_PATCH	8	/* Write patch (wave) data */

/*
 * Commands 0x1000 to 0xffff are for communication between the patch manager
 * and the client
 */
#define _PM_LOAD_PATCH	0x100

/*
 * Commands above 0xffff reserved for device specific use
 */

	long parm1;
	long parm2;
	long parm3;

	union {
		u_char data8[4000];
		u_short data16[2000];
		u_long data32[1000];
		struct patch_info patch;
	} data;
};

/*
 * When a patch manager daemon is present, it will be informed by the
 * driver when something important happens. For example when the
 * /dev/sequencer is opened or closed. A record with key == PM_K_EVENT is
 * returned. The command field contains the event type:
 */
#define PM_E_OPENED		1	/* /dev/sequencer opened */
#define PM_E_CLOSED		2	/* /dev/sequencer closed */
#define PM_E_PATCH_RESET	3	/* SNDCTL_RESETSAMPLES called */
#define PM_E_PATCH_LOADED	4	/* A patch has been loaded by appl */

/*
 * /dev/sequencer input events.
 *
 * The data written to the /dev/sequencer is a stream of events. Events
 * are records of 4 or 8 bytes. The first byte defines the size.
 * Any number of events can be written with a write call. There
 * is a set of macros for sending these events. Use these macros if you
 * want to maximize portability of your program.
 *
 * Events SEQ_WAIT, SEQ_MIDIPUTC and SEQ_ECHO. Are also input events.
 * (All input events are currently 4 bytes long. Be prepared to support
 * 8 byte events also. If you receive any event having first byte >= 128,
 * it's a 8 byte event.
 *
 * The events are documented at the end of this file.
 *
 * Normal events (4 bytes)
 * There is also a 8 byte version of most of the 4 byte events. The
 * 8 byte one is recommended.
 */
#define SEQ_NOTEOFF		0
#define SEQ_FMNOTEOFF		SEQ_NOTEOFF	/* Just old name */
#define SEQ_NOTEON		1
#define	SEQ_FMNOTEON		SEQ_NOTEON
#define SEQ_WAIT		TMR_WAIT_ABS
#define SEQ_PGMCHANGE		3
#define SEQ_FMPGMCHANGE		SEQ_PGMCHANGE
#define SEQ_SYNCTIMER		TMR_START
#define SEQ_MIDIPUTC		5
#define SEQ_DRUMON		6	/*** OBSOLETE ***/
#define SEQ_DRUMOFF		7	/*** OBSOLETE ***/
#define SEQ_ECHO		TMR_ECHO	/* For synching programs with output */
#define SEQ_AFTERTOUCH		9
#define SEQ_CONTROLLER		10

/*
 *	Midi controller numbers
 *
 * Controllers 0 to 31 (0x00 to 0x1f) and 32 to 63 (0x20 to 0x3f)
 * are continuous controllers.
 * In the MIDI 1.0 these controllers are sent using two messages.
 * Controller numbers 0 to 31 are used to send the MSB and the
 * controller numbers 32 to 63 are for the LSB. Note that just 7 bits
 * are used in MIDI bytes.
 */

#define	CTL_BANK_SELECT		0x00
#define	CTL_MODWHEEL		0x01
#define CTL_BREATH		0x02
/*	undefined		0x03 */
#define CTL_FOOT		0x04
#define CTL_PORTAMENTO_TIME	0x05
#define CTL_DATA_ENTRY		0x06
#define CTL_MAIN_VOLUME		0x07
#define CTL_BALANCE		0x08
/*	undefined		0x09 */
#define CTL_PAN			0x0a
#define CTL_EXPRESSION		0x0b
/*	undefined		0x0c - 0x0f */
#define CTL_GENERAL_PURPOSE1	0x10
#define CTL_GENERAL_PURPOSE2	0x11
#define CTL_GENERAL_PURPOSE3	0x12
#define CTL_GENERAL_PURPOSE4	0x13
/*	undefined		0x14 - 0x1f */

/*	undefined		0x20 */

/*
 * The controller numbers 0x21 to 0x3f are reserved for the
 * least significant bytes of the controllers 0x00 to 0x1f.
 * These controllers are not recognised by the driver.
 *
 * Controllers 64 to 69 (0x40 to 0x45) are on/off switches.
 * 0=OFF and 127=ON (intermediate values are possible)
 */
#define CTL_DAMPER_PEDAL	0x40
#define CTL_SUSTAIN		CTL_DAMPER_PEDAL	/* Alias */
#define CTL_HOLD		CTL_DAMPER_PEDAL	/* Alias */
#define CTL_PORTAMENTO		0x41
#define CTL_SOSTENUTO		0x42
#define CTL_SOFT_PEDAL		0x43
/*	undefined		0x44 */
#define CTL_HOLD2		0x45
/*	undefined		0x46 - 0x4f */

#define CTL_GENERAL_PURPOSE5	0x50
#define CTL_GENERAL_PURPOSE6	0x51
#define CTL_GENERAL_PURPOSE7	0x52
#define CTL_GENERAL_PURPOSE8	0x53
/*	undefined		0x54 - 0x5a */
#define CTL_EXT_EFF_DEPTH	0x5b
#define CTL_TREMOLO_DEPTH	0x5c
#define CTL_CHORUS_DEPTH	0x5d
#define CTL_DETUNE_DEPTH	0x5e
#define CTL_CELESTE_DEPTH	CTL_DETUNE_DEPTH /* Alias for the above one */
#define CTL_PHASER_DEPTH	0x5f
#define CTL_DATA_INCREMENT	0x60
#define CTL_DATA_DECREMENT	0x61
#define CTL_NONREG_PARM_NUM_LSB	0x62
#define CTL_NONREG_PARM_NUM_MSB	0x63
#define CTL_REGIST_PARM_NUM_LSB	0x64
#define CTL_REGIST_PARM_NUM_MSB	0x65
/*	undefined		0x66 - 0x78 */
/*	reserved		0x79 - 0x7f */

/* Pseudo controllers (not midi compatible) */
#define CTRL_PITCH_BENDER	255
#define CTRL_PITCH_BENDER_RANGE	254
#define CTRL_EXPRESSION		253	/* Obsolete */
#define CTRL_MAIN_VOLUME	252	/* Obsolete */

#define SEQ_BALANCE		11
#define SEQ_VOLMODE             12

/*
 * Volume mode decides how volumes are used
 */

#define VOL_METHOD_ADAGIO	1
#define VOL_METHOD_LINEAR	2

/*
 * Note! SEQ_WAIT, SEQ_MIDIPUTC and SEQ_ECHO are used also as
 *	 input events.
 */

/*
 * Event codes 0xf0 to 0xfc are reserved for future extensions.
 */

#define SEQ_FULLSIZE		0xfd	/* Long events */
/*
 * SEQ_FULLSIZE events are used for loading patches/samples to the
 * synthesizer devices. These events are passed directly to the driver
 * of the associated synthesizer device. There is no limit to the size
 * of the extended events. These events are not queued but executed
 * immediately when the write() is called (execution can take several
 * seconds of time).
 *
 * When a SEQ_FULLSIZE message is written to the device, it must
 * be written using exactly one write() call. Other events cannot
 * be mixed to the same write.
 *
 * For FM synths (YM3812/OPL3) use struct sbi_instrument and write
 * it to the /dev/sequencer. Don't write other data together with
 * the instrument structure Set the key field of the structure to
 * FM_PATCH. The device field is used to route the patch to the
 * corresponding device.
 *
 * For Gravis UltraSound use struct patch_info. Initialize the key field
 * to GUS_PATCH.
 */
#define SEQ_PRIVATE	0xfe	/* Low level HW dependent events (8 bytes) */
#define SEQ_EXTENDED	0xff	/* Extended events (8 bytes) OBSOLETE */

/*
 * Record for FM patches
 */

typedef u_char sbi_instr_data[32];

struct sbi_instrument {
	u_short	key;	/* FM_PATCH or OPL3_PATCH */
#define FM_PATCH	_PATCHKEY(0x01)
#define OPL3_PATCH	_PATCHKEY(0x03)
	short		device;		/* Synth# (0-4)	*/
	int 		channel;	/* Program# to be initialized  */
	sbi_instr_data	operators;	/* Reg. settings for operator cells
					 * (.SBI format)	*/
};

struct synth_info {	/* Read only */
	char	name[30];
	int	device;		/* 0-N. INITIALIZE BEFORE CALLING */
	int	synth_type;
#define SYNTH_TYPE_FM			0
#define SYNTH_TYPE_SAMPLE		1
#define SYNTH_TYPE_MIDI			2	/* Midi interface */

	int	synth_subtype;
#define FM_TYPE_ADLIB			0x00
#define FM_TYPE_OPL3			0x01
#define MIDI_TYPE_MPU401		0x401

#define SAMPLE_TYPE_BASIC		0x10
#define SAMPLE_TYPE_GUS			SAMPLE_TYPE_BASIC
#define SAMPLE_TYPE_AWE32		0x20

	int	perc_mode;	/* No longer supported */
	int	nr_voices;
	int	nr_drums;	/* Obsolete field */
	int	instr_bank_size;
	u_long	capabilities;
#define SYNTH_CAP_PERCMODE	0x00000001 /* No longer used */
#define SYNTH_CAP_OPL3		0x00000002 /* Set if OPL3 supported */
#define SYNTH_CAP_INPUT		0x00000004 /* Input (MIDI) device */
	int	dummies[19];	/* Reserve space */
};

struct sound_timer_info {
	char name[32];
	int caps;
};

struct midi_info {
	char		name[30];
	int		device;		/* 0-N. INITIALIZE BEFORE CALLING */
	u_long	capabilities;	/* To be defined later */
	int		dev_type;
	int		dummies[18];	/* Reserve space */
};

/*
 * ioctl commands for the /dev/midi##
 */
typedef struct {
	u_char cmd;
	char nr_args, nr_returns;
	u_char data[30];
} mpu_command_rec;

#define SNDCTL_MIDI_PRETIME	_IOWR('m', 0, int)
#define SNDCTL_MIDI_MPUMODE	_IOWR('m', 1, int)
#define SNDCTL_MIDI_MPUCMD	_IOWR('m', 2, mpu_command_rec)
#define MIOSPASSTHRU		_IOWR('m', 3, int)
#define MIOGPASSTHRU		_IOWR('m', 4, int)

/*
 * IOCTL commands for /dev/dsp and /dev/audio
 */

#define SNDCTL_DSP_HALT		_IO  ('P', 0)
#define SNDCTL_DSP_RESET	SNDCTL_DSP_HALT
#define SNDCTL_DSP_SYNC		_IO  ('P', 1)
#define SNDCTL_DSP_SPEED	_IOWR('P', 2, int)
#define SNDCTL_DSP_STEREO	_IOWR('P', 3, int)
#define SNDCTL_DSP_GETBLKSIZE	_IOR('P', 4, int)
#define SNDCTL_DSP_SETBLKSIZE   _IOW('P', 4, int)
#define SNDCTL_DSP_SETFMT	_IOWR('P',5, int) /* Selects ONE fmt*/

/*
 * SOUND_PCM_WRITE_CHANNELS is not that different
 * from SNDCTL_DSP_STEREO
 */
#define SOUND_PCM_WRITE_CHANNELS	_IOWR('P', 6, int)
#define SNDCTL_DSP_CHANNELS	SOUND_PCM_WRITE_CHANNELS
#define SOUND_PCM_WRITE_FILTER	_IOWR('P', 7, int)
#define SNDCTL_DSP_POST		_IO  ('P', 8)

/*
 * SNDCTL_DSP_SETBLKSIZE and the following two calls mostly do
 * the same thing, i.e. set the block size used in DMA transfers.
 */
#define SNDCTL_DSP_SUBDIVIDE	_IOWR('P', 9, int)
#define SNDCTL_DSP_SETFRAGMENT	_IOWR('P',10, int)


#define SNDCTL_DSP_GETFMTS	_IOR ('P',11, int) /* Returns a mask */
/*
 * Buffer status queries.
 */
typedef struct audio_buf_info {
    int fragments;	/* # of avail. frags (partly used ones not counted) */
    int fragstotal;	/* Total # of fragments allocated */
    int fragsize;	/* Size of a fragment in bytes */

    int bytes;	/* Avail. space in bytes (includes partly used fragments) */
		/* Note! 'bytes' could be more than fragments*fragsize */
} audio_buf_info;

#define SNDCTL_DSP_GETOSPACE	_IOR ('P',12, audio_buf_info)
#define SNDCTL_DSP_GETISPACE	_IOR ('P',13, audio_buf_info)

/*
 * SNDCTL_DSP_NONBLOCK is the same (but less powerful, since the
 * action cannot be undone) of FIONBIO. The same can be achieved
 * by opening the device with O_NDELAY
 */
#define SNDCTL_DSP_NONBLOCK	_IO  ('P',14)

#define SNDCTL_DSP_GETCAPS	_IOR ('P',15, int)
#	define PCM_CAP_REVISION		0x000000ff	/* Bits for revision level (0 to 255) */
#	define PCM_CAP_DUPLEX		0x00000100	/* Full duplex record/playback */
#	define PCM_CAP_REALTIME		0x00000200	/* Not in use */
#	define PCM_CAP_BATCH		0x00000400	/* Device has some kind of */
							/* internal buffers which may */
							/* cause some delays and */
							/* decrease precision of timing */
#	define PCM_CAP_COPROC		0x00000800	/* Has a coprocessor */
							/* Sometimes it's a DSP */
							/* but usually not */
#	define PCM_CAP_TRIGGER		0x00001000	/* Supports SETTRIGGER */
#	define PCM_CAP_MMAP		0x00002000	/* Supports mmap() */
#	define PCM_CAP_MULTI		0x00004000	/* Supports multiple open */
#	define PCM_CAP_BIND		0x00008000	/* Supports binding to front/rear/center/lfe */
#   	define PCM_CAP_INPUT		0x00010000	/* Supports recording */
#   	define PCM_CAP_OUTPUT		0x00020000	/* Supports playback */
#	define PCM_CAP_VIRTUAL		0x00040000	/* Virtual device */
/* 0x00040000 and 0x00080000 reserved for future use */

/* Analog/digital control capabilities */
#	define PCM_CAP_ANALOGOUT	0x00100000
#	define PCM_CAP_ANALOGIN		0x00200000
#	define PCM_CAP_DIGITALOUT	0x00400000
#	define PCM_CAP_DIGITALIN	0x00800000
#	define PCM_CAP_ADMASK		0x00f00000
/*
 * NOTE! (capabilities & PCM_CAP_ADMASK)==0 means just that the
 * digital/analog interface control features are not supported by the 
 * device/driver. However the device still supports analog, digital or
 * both inputs/outputs (depending on the device). See the OSS Programmer's
 * Guide for full details.
 */
#	define PCM_CAP_SPECIAL		0x01000000	/* Not for ordinary "multimedia" use */
#	define PCM_CAP_SHADOW		0x00000000	/* OBSOLETE */

/*
 * Preferred channel usage. These bits can be used to
 * give recommendations to the application. Used by few drivers.
 * For example if ((caps & DSP_CH_MASK) == DSP_CH_MONO) means that
 * the device works best in mono mode. However it doesn't necessarily mean
 * that the device cannot be used in stereo. These bits should only be used
 * by special applications such as multi track hard disk recorders to find
 * out the initial setup. However the user should be able to override this
 * selection.
 *
 * To find out which modes are actually supported the application should 
 * try to select them using SNDCTL_DSP_CHANNELS.
 */
#	define DSP_CH_MASK		0x06000000	/* Mask */
#	define DSP_CH_ANY		0x00000000	/* No preferred mode */
#	define DSP_CH_MONO		0x02000000
#	define DSP_CH_STEREO		0x04000000
#	define DSP_CH_MULTI		0x06000000	/* More than two channels */

#	define PCM_CAP_HIDDEN		0x08000000	/* Hidden device */
#	define PCM_CAP_FREERATE		0x10000000
#	define PCM_CAP_MODEM		0x20000000	/* Modem device */
#	define PCM_CAP_DEFAULT		0x40000000	/* "Default" device */

/*
 * The PCM_CAP_* capability names were known as DSP_CAP_* prior OSS 4.0
 * so it's necessary to define the older names too.
 */
#define DSP_CAP_ADMASK		PCM_CAP_ADMASK
#define DSP_CAP_ANALOGIN	PCM_CAP_ANALOGIN
#define DSP_CAP_ANALOGOUT	PCM_CAP_ANALOGOUT
#define DSP_CAP_BATCH		PCM_CAP_BATCH
#define DSP_CAP_BIND		PCM_CAP_BIND
#define DSP_CAP_COPROC		PCM_CAP_COPROC
#define DSP_CAP_DEFAULT		PCM_CAP_DEFAULT
#define DSP_CAP_DIGITALIN	PCM_CAP_DIGITALIN
#define DSP_CAP_DIGITALOUT	PCM_CAP_DIGITALOUT
#define DSP_CAP_DUPLEX		PCM_CAP_DUPLEX
#define DSP_CAP_FREERATE	PCM_CAP_FREERATE
#define DSP_CAP_HIDDEN		PCM_CAP_HIDDEN
#define DSP_CAP_INPUT		PCM_CAP_INPUT
#define DSP_CAP_MMAP		PCM_CAP_MMAP
#define DSP_CAP_MODEM		PCM_CAP_MODEM
#define DSP_CAP_MULTI		PCM_CAP_MULTI
#define DSP_CAP_OUTPUT		PCM_CAP_OUTPUT
#define DSP_CAP_REALTIME	PCM_CAP_REALTIME
#define DSP_CAP_REVISION	PCM_CAP_REVISION
#define DSP_CAP_SHADOW		PCM_CAP_SHADOW
#define DSP_CAP_TRIGGER		PCM_CAP_TRIGGER
#define DSP_CAP_VIRTUAL		PCM_CAP_VIRTUAL

/*
 * What do these function do ?
 */
#define SNDCTL_DSP_GETTRIGGER	_IOR ('P',16, int)
#define SNDCTL_DSP_SETTRIGGER	_IOW ('P',16, int)
#define PCM_ENABLE_INPUT	0x00000001
#define PCM_ENABLE_OUTPUT	0x00000002

typedef struct count_info {
	int bytes;	/* Total # of bytes processed */
	int blocks;	/* # of fragment transitions since last time */
	int ptr;	/* Current DMA pointer value */
} count_info;

/*
 * GETIPTR and GETISPACE are not that different... same for out.
 */
#define SNDCTL_DSP_GETIPTR	_IOR ('P',17, count_info)
#define SNDCTL_DSP_GETOPTR	_IOR ('P',18, count_info)

typedef struct buffmem_desc {
	caddr_t buffer;
	int size;
} buffmem_desc;

#define SNDCTL_DSP_MAPINBUF	_IOR ('P', 19, buffmem_desc)
#define SNDCTL_DSP_MAPOUTBUF	_IOR ('P', 20, buffmem_desc)
#define SNDCTL_DSP_SETSYNCRO	_IO  ('P', 21)
#define SNDCTL_DSP_SETDUPLEX	_IO  ('P', 22)
#define SNDCTL_DSP_GETODELAY	_IOR ('P', 23, int)

/*
 * I guess these are the readonly version of the same
 * functions that exist above as SNDCTL_DSP_...
 */
#define SOUND_PCM_READ_RATE	_IOR ('P', 2, int)
#define SOUND_PCM_READ_CHANNELS	_IOR ('P', 6, int)
#define SOUND_PCM_READ_BITS	_IOR ('P', 5, int)
#define SOUND_PCM_READ_FILTER	_IOR ('P', 7, int)

/*
 * ioctl calls to be used in communication with coprocessors and
 * DSP chips.
 */

typedef struct copr_buffer {
	int command;	/* Set to 0 if not used */
	int flags;
#define CPF_NONE		0x0000
#define CPF_FIRST		0x0001	/* First block */
#define CPF_LAST		0x0002	/* Last block */
	int len;
	int offs;	/* If required by the device (0 if not used) */

	u_char data[4000]; /* NOTE! 4000 is not 4k */
} copr_buffer;

typedef struct copr_debug_buf {
	int command;	/* Used internally. Set to 0 */
	int parm1;
	int parm2;
	int flags;
	int len;	/* Length of data in bytes */
} copr_debug_buf;

typedef struct copr_msg {
	int len;
	u_char data[4000];
} copr_msg;

#define SNDCTL_COPR_RESET       _IO  ('C',  0)
#define SNDCTL_COPR_LOAD	_IOWR('C',  1, copr_buffer)
#define SNDCTL_COPR_RDATA	_IOWR('C',  2, copr_debug_buf)
#define SNDCTL_COPR_RCODE	_IOWR('C',  3, copr_debug_buf)
#define SNDCTL_COPR_WDATA	_IOW ('C',  4, copr_debug_buf)
#define SNDCTL_COPR_WCODE	_IOW ('C',  5, copr_debug_buf)
#define SNDCTL_COPR_RUN		_IOWR('C',  6, copr_debug_buf)
#define SNDCTL_COPR_HALT	_IOWR('C',  7, copr_debug_buf)
#define SNDCTL_COPR_SENDMSG	_IOW ('C',  8, copr_msg)
#define SNDCTL_COPR_RCVMSG	_IOR ('C',  9, copr_msg)

/*
 * IOCTL commands for /dev/mixer
 */

/*
 * Mixer devices
 *
 * There can be up to 20 different analog mixer channels. The
 * SOUND_MIXER_NRDEVICES gives the currently supported maximum.
 * The SOUND_MIXER_READ_DEVMASK returns a bitmask which tells
 * the devices supported by the particular mixer.
 */

#define SOUND_MIXER_NRDEVICES	25
#define SOUND_MIXER_VOLUME	0	/* Master output level */
#define SOUND_MIXER_BASS	1	/* Treble level of all output channels */
#define SOUND_MIXER_TREBLE	2	/* Bass level of all output channels */
#define SOUND_MIXER_SYNTH	3	/* Volume of synthesier input */
#define SOUND_MIXER_PCM		4	/* Output level for the audio device */
#define SOUND_MIXER_SPEAKER	5	/* Output level for the PC speaker
					 * signals */
#define SOUND_MIXER_LINE	6	/* Volume level for the line in jack */
#define SOUND_MIXER_MIC		7	/* Volume for the signal coming from
					 * the microphone jack */
#define SOUND_MIXER_CD		8	/* Volume level for the input signal
					 * connected to the CD audio input */
#define SOUND_MIXER_IMIX	9	/* Recording monitor. It controls the
					 * output volume of the selected
					 * recording sources while recording */
#define SOUND_MIXER_ALTPCM	10	/* Volume of the alternative codec
					 * device */
#define SOUND_MIXER_RECLEV	11	/* Global recording level */
#define SOUND_MIXER_IGAIN	12	/* Input gain */
#define SOUND_MIXER_OGAIN	13	/* Output gain */
/*
 * The AD1848 codec and compatibles have three line level inputs
 * (line, aux1 and aux2). Since each card manufacturer have assigned
 * different meanings to these inputs, it's inpractical to assign
 * specific meanings (line, cd, synth etc.) to them.
 */
#define SOUND_MIXER_LINE1	14	/* Input source 1  (aux1) */
#define SOUND_MIXER_LINE2	15	/* Input source 2  (aux2) */
#define SOUND_MIXER_LINE3	16	/* Input source 3  (line) */
#define SOUND_MIXER_DIGITAL1    17      /* Digital (input) 1 */
#define SOUND_MIXER_DIGITAL2    18      /* Digital (input) 2 */
#define SOUND_MIXER_DIGITAL3    19      /* Digital (input) 3 */
#define SOUND_MIXER_PHONEIN     20      /* Phone input */
#define SOUND_MIXER_PHONEOUT    21      /* Phone output */
#define SOUND_MIXER_VIDEO       22      /* Video/TV (audio) in */
#define SOUND_MIXER_RADIO       23      /* Radio in */
#define SOUND_MIXER_MONITOR     24      /* Monitor (usually mic) volume */


/*
 * Some on/off settings (SOUND_SPECIAL_MIN - SOUND_SPECIAL_MAX)
 * Not counted to SOUND_MIXER_NRDEVICES, but use the same number space
 */
#define SOUND_ONOFF_MIN		28
#define SOUND_ONOFF_MAX		30
#define SOUND_MIXER_MUTE	28	/* 0 or 1 */
#define SOUND_MIXER_ENHANCE	29	/* Enhanced stereo (0, 40, 60 or 80) */
#define SOUND_MIXER_LOUD	30	/* 0 or 1 */

/* Note!	Number 31 cannot be used since the sign bit is reserved */
#define SOUND_MIXER_NONE        31

#define SOUND_DEVICE_LABELS	{ \
	"Vol  ", "Bass ", "Trebl", "Synth", "Pcm  ", "Spkr ", "Line ", \
	"Mic  ", "CD   ", "Mix  ", "Pcm2 ", "Rec  ", "IGain", "OGain", \
	"Line1", "Line2", "Line3", "Digital1", "Digital2", "Digital3", \
	"PhoneIn", "PhoneOut", "Video", "Radio", "Monitor"}

#define SOUND_DEVICE_NAMES	{ \
	"vol", "bass", "treble", "synth", "pcm", "speaker", "line", \
	"mic", "cd", "mix", "pcm2", "rec", "igain", "ogain", \
	"line1", "line2", "line3", "dig1", "dig2", "dig3", \
	"phin", "phout", "video", "radio", "monitor"}

/*	Device bitmask identifiers	*/

#define SOUND_MIXER_RECSRC	0xff	/* 1 bit per recording source */
#define SOUND_MIXER_DEVMASK	0xfe	/* 1 bit per supported device */
#define SOUND_MIXER_RECMASK	0xfd	/* 1 bit per supp. recording source */
#define SOUND_MIXER_CAPS	0xfc
#define SOUND_CAP_EXCL_INPUT	0x00000001	/* Only 1 rec. src at a time */
#define SOUND_MIXER_STEREODEVS	0xfb	/* Mixer channels supporting stereo */

/*	Device mask bits	*/

#define SOUND_MASK_VOLUME	(1 << SOUND_MIXER_VOLUME)
#define SOUND_MASK_BASS		(1 << SOUND_MIXER_BASS)
#define SOUND_MASK_TREBLE	(1 << SOUND_MIXER_TREBLE)
#define SOUND_MASK_SYNTH	(1 << SOUND_MIXER_SYNTH)
#define SOUND_MASK_PCM		(1 << SOUND_MIXER_PCM)
#define SOUND_MASK_SPEAKER	(1 << SOUND_MIXER_SPEAKER)
#define SOUND_MASK_LINE		(1 << SOUND_MIXER_LINE)
#define SOUND_MASK_MIC		(1 << SOUND_MIXER_MIC)
#define SOUND_MASK_CD		(1 << SOUND_MIXER_CD)
#define SOUND_MASK_IMIX		(1 << SOUND_MIXER_IMIX)
#define SOUND_MASK_ALTPCM	(1 << SOUND_MIXER_ALTPCM)
#define SOUND_MASK_RECLEV	(1 << SOUND_MIXER_RECLEV)
#define SOUND_MASK_IGAIN	(1 << SOUND_MIXER_IGAIN)
#define SOUND_MASK_OGAIN	(1 << SOUND_MIXER_OGAIN)
#define SOUND_MASK_LINE1	(1 << SOUND_MIXER_LINE1)
#define SOUND_MASK_LINE2	(1 << SOUND_MIXER_LINE2)
#define SOUND_MASK_LINE3	(1 << SOUND_MIXER_LINE3)
#define SOUND_MASK_DIGITAL1     (1 << SOUND_MIXER_DIGITAL1)
#define SOUND_MASK_DIGITAL2     (1 << SOUND_MIXER_DIGITAL2)
#define SOUND_MASK_DIGITAL3     (1 << SOUND_MIXER_DIGITAL3)
#define SOUND_MASK_PHONEIN      (1 << SOUND_MIXER_PHONEIN)
#define SOUND_MASK_PHONEOUT     (1 << SOUND_MIXER_PHONEOUT)
#define SOUND_MASK_RADIO        (1 << SOUND_MIXER_RADIO)
#define SOUND_MASK_VIDEO        (1 << SOUND_MIXER_VIDEO)
#define SOUND_MASK_MONITOR      (1 << SOUND_MIXER_MONITOR)

/* Obsolete macros */
#define SOUND_MASK_MUTE		(1 << SOUND_MIXER_MUTE)
#define SOUND_MASK_ENHANCE	(1 << SOUND_MIXER_ENHANCE)
#define SOUND_MASK_LOUD		(1 << SOUND_MIXER_LOUD)

#define MIXER_READ(dev)		_IOR('M', dev, int)
#define SOUND_MIXER_READ_VOLUME		MIXER_READ(SOUND_MIXER_VOLUME)
#define SOUND_MIXER_READ_BASS		MIXER_READ(SOUND_MIXER_BASS)
#define SOUND_MIXER_READ_TREBLE		MIXER_READ(SOUND_MIXER_TREBLE)
#define SOUND_MIXER_READ_SYNTH		MIXER_READ(SOUND_MIXER_SYNTH)
#define SOUND_MIXER_READ_PCM		MIXER_READ(SOUND_MIXER_PCM)
#define SOUND_MIXER_READ_SPEAKER	MIXER_READ(SOUND_MIXER_SPEAKER)
#define SOUND_MIXER_READ_LINE		MIXER_READ(SOUND_MIXER_LINE)
#define SOUND_MIXER_READ_MIC		MIXER_READ(SOUND_MIXER_MIC)
#define SOUND_MIXER_READ_CD		MIXER_READ(SOUND_MIXER_CD)
#define SOUND_MIXER_READ_IMIX		MIXER_READ(SOUND_MIXER_IMIX)
#define SOUND_MIXER_READ_ALTPCM		MIXER_READ(SOUND_MIXER_ALTPCM)
#define SOUND_MIXER_READ_RECLEV		MIXER_READ(SOUND_MIXER_RECLEV)
#define SOUND_MIXER_READ_IGAIN		MIXER_READ(SOUND_MIXER_IGAIN)
#define SOUND_MIXER_READ_OGAIN		MIXER_READ(SOUND_MIXER_OGAIN)
#define SOUND_MIXER_READ_LINE1		MIXER_READ(SOUND_MIXER_LINE1)
#define SOUND_MIXER_READ_LINE2		MIXER_READ(SOUND_MIXER_LINE2)
#define SOUND_MIXER_READ_LINE3		MIXER_READ(SOUND_MIXER_LINE3)
#define SOUND_MIXER_READ_DIGITAL1	MIXER_READ(SOUND_MIXER_DIGITAL1)
#define SOUND_MIXER_READ_DIGITAL2	MIXER_READ(SOUND_MIXER_DIGITAL2)
#define SOUND_MIXER_READ_DIGITAL3	MIXER_READ(SOUND_MIXER_DIGITAL3)
#define SOUND_MIXER_READ_PHONEIN      	MIXER_READ(SOUND_MIXER_PHONEIN)
#define SOUND_MIXER_READ_PHONEOUT	MIXER_READ(SOUND_MIXER_PHONEOUT)
#define SOUND_MIXER_READ_RADIO		MIXER_READ(SOUND_MIXER_RADIO)
#define SOUND_MIXER_READ_VIDEO		MIXER_READ(SOUND_MIXER_VIDEO)
#define SOUND_MIXER_READ_MONITOR	MIXER_READ(SOUND_MIXER_MONITOR)

/* Obsolete macros */
#define SOUND_MIXER_READ_MUTE		MIXER_READ(SOUND_MIXER_MUTE)
#define SOUND_MIXER_READ_ENHANCE	MIXER_READ(SOUND_MIXER_ENHANCE)
#define SOUND_MIXER_READ_LOUD		MIXER_READ(SOUND_MIXER_LOUD)

#define SOUND_MIXER_READ_RECSRC		MIXER_READ(SOUND_MIXER_RECSRC)
#define SOUND_MIXER_READ_DEVMASK	MIXER_READ(SOUND_MIXER_DEVMASK)
#define SOUND_MIXER_READ_RECMASK	MIXER_READ(SOUND_MIXER_RECMASK)
#define SOUND_MIXER_READ_STEREODEVS	MIXER_READ(SOUND_MIXER_STEREODEVS)
#define SOUND_MIXER_READ_CAPS		MIXER_READ(SOUND_MIXER_CAPS)

#define MIXER_WRITE(dev)		_IOWR('M', dev, int)
#define SOUND_MIXER_WRITE_VOLUME	MIXER_WRITE(SOUND_MIXER_VOLUME)
#define SOUND_MIXER_WRITE_BASS		MIXER_WRITE(SOUND_MIXER_BASS)
#define SOUND_MIXER_WRITE_TREBLE	MIXER_WRITE(SOUND_MIXER_TREBLE)
#define SOUND_MIXER_WRITE_SYNTH		MIXER_WRITE(SOUND_MIXER_SYNTH)
#define SOUND_MIXER_WRITE_PCM		MIXER_WRITE(SOUND_MIXER_PCM)
#define SOUND_MIXER_WRITE_SPEAKER	MIXER_WRITE(SOUND_MIXER_SPEAKER)
#define SOUND_MIXER_WRITE_LINE		MIXER_WRITE(SOUND_MIXER_LINE)
#define SOUND_MIXER_WRITE_MIC		MIXER_WRITE(SOUND_MIXER_MIC)
#define SOUND_MIXER_WRITE_CD		MIXER_WRITE(SOUND_MIXER_CD)
#define SOUND_MIXER_WRITE_IMIX		MIXER_WRITE(SOUND_MIXER_IMIX)
#define SOUND_MIXER_WRITE_ALTPCM	MIXER_WRITE(SOUND_MIXER_ALTPCM)
#define SOUND_MIXER_WRITE_RECLEV	MIXER_WRITE(SOUND_MIXER_RECLEV)
#define SOUND_MIXER_WRITE_IGAIN		MIXER_WRITE(SOUND_MIXER_IGAIN)
#define SOUND_MIXER_WRITE_OGAIN		MIXER_WRITE(SOUND_MIXER_OGAIN)
#define SOUND_MIXER_WRITE_LINE1		MIXER_WRITE(SOUND_MIXER_LINE1)
#define SOUND_MIXER_WRITE_LINE2		MIXER_WRITE(SOUND_MIXER_LINE2)
#define SOUND_MIXER_WRITE_LINE3		MIXER_WRITE(SOUND_MIXER_LINE3)
#define SOUND_MIXER_WRITE_DIGITAL1	MIXER_WRITE(SOUND_MIXER_DIGITAL1)
#define SOUND_MIXER_WRITE_DIGITAL2	MIXER_WRITE(SOUND_MIXER_DIGITAL2)
#define SOUND_MIXER_WRITE_DIGITAL3	MIXER_WRITE(SOUND_MIXER_DIGITAL3)
#define SOUND_MIXER_WRITE_PHONEIN      	MIXER_WRITE(SOUND_MIXER_PHONEIN)
#define SOUND_MIXER_WRITE_PHONEOUT	MIXER_WRITE(SOUND_MIXER_PHONEOUT)
#define SOUND_MIXER_WRITE_RADIO		MIXER_WRITE(SOUND_MIXER_RADIO)
#define SOUND_MIXER_WRITE_VIDEO		MIXER_WRITE(SOUND_MIXER_VIDEO)
#define SOUND_MIXER_WRITE_MONITOR	MIXER_WRITE(SOUND_MIXER_MONITOR)

#define SOUND_MIXER_WRITE_MUTE		MIXER_WRITE(SOUND_MIXER_MUTE)
#define SOUND_MIXER_WRITE_ENHANCE	MIXER_WRITE(SOUND_MIXER_ENHANCE)
#define SOUND_MIXER_WRITE_LOUD		MIXER_WRITE(SOUND_MIXER_LOUD)

#define SOUND_MIXER_WRITE_RECSRC	MIXER_WRITE(SOUND_MIXER_RECSRC)

typedef struct mixer_info {
  char id[16];
  char name[32];
  int  modify_counter;
  int fillers[10];
} mixer_info;

#define SOUND_MIXER_INFO		_IOR('M', 101, mixer_info)

#define LEFT_CHN	0
#define RIGHT_CHN	1

/*
 * Level 2 event types for /dev/sequencer
 */

/*
 * The 4 most significant bits of byte 0 specify the class of
 * the event:
 *
 *	0x8X = system level events,
 *	0x9X = device/port specific events, event[1] = device/port,
 *		The last 4 bits give the subtype:
 *			0x02	= Channel event (event[3] = chn).
 *			0x01	= note event (event[4] = note).
 *			(0x01 is not used alone but always with bit 0x02).
 *	       event[2] = MIDI message code (0x80=note off etc.)
 *
 */

#define EV_SEQ_LOCAL		0x80
#define EV_TIMING		0x81
#define EV_CHN_COMMON		0x92
#define EV_CHN_VOICE		0x93
#define EV_SYSEX		0x94
/*
 * Event types 200 to 220 are reserved for application use.
 * These numbers will not be used by the driver.
 */

/*
 * Events for event type EV_CHN_VOICE
 */

#define MIDI_NOTEOFF		0x80
#define MIDI_NOTEON		0x90
#define MIDI_KEY_PRESSURE	0xA0

/*
 * Events for event type EV_CHN_COMMON
 */

#define MIDI_CTL_CHANGE		0xB0
#define MIDI_PGM_CHANGE		0xC0
#define MIDI_CHN_PRESSURE	0xD0
#define MIDI_PITCH_BEND		0xE0

#define MIDI_SYSTEM_PREFIX	0xF0

/*
 * Timer event types
 */
#define TMR_WAIT_REL		1	/* Time relative to the prev time */
#define TMR_WAIT_ABS		2	/* Absolute time since TMR_START */
#define TMR_STOP		3
#define TMR_START		4
#define TMR_CONTINUE		5
#define TMR_TEMPO		6
#define TMR_ECHO		8
#define TMR_CLOCK		9	/* MIDI clock */
#define TMR_SPP			10	/* Song position pointer */
#define TMR_TIMESIG		11	/* Time signature */

/*
 *	Local event types
 */
#define LOCL_STARTAUDIO		1

#if !defined(_KERNEL) || defined(USE_SEQ_MACROS)
/*
 *	Some convenience macros to simplify programming of the
 *	/dev/sequencer interface
 *
 *	These macros define the API which should be used when possible.
 */

#ifndef USE_SIMPLE_MACROS
void seqbuf_dump(void);	/* This function must be provided by programs */

/* Sample seqbuf_dump() implementation:
 *
 *	SEQ_DEFINEBUF (2048);	-- Defines a buffer for 2048 bytes
 *
 *	int seqfd;		-- The file descriptor for /dev/sequencer.
 *
 *	void
 *	seqbuf_dump ()
 *	{
 *	  if (_seqbufptr)
 *	    if (write (seqfd, _seqbuf, _seqbufptr) == -1)
 *	      {
 *		perror ("write /dev/sequencer");
 *		exit (-1);
 *	      }
 *	  _seqbufptr = 0;
 *	}
 */

#define SEQ_DEFINEBUF(len)		\
	u_char _seqbuf[len]; int _seqbuflen = len;int _seqbufptr = 0
#define SEQ_USE_EXTBUF()		\
	extern u_char _seqbuf[]; \
	extern int _seqbuflen;extern int _seqbufptr
#define SEQ_DECLAREBUF()		SEQ_USE_EXTBUF()
#define SEQ_PM_DEFINES			struct patmgr_info _pm_info
#define _SEQ_NEEDBUF(len)		\
	if ((_seqbufptr+(len)) > _seqbuflen) \
		seqbuf_dump()
#define _SEQ_ADVBUF(len)		_seqbufptr += len
#define SEQ_DUMPBUF			seqbuf_dump
#else
/*
 * This variation of the sequencer macros is used just to format one event
 * using fixed buffer.
 *
 * The program using the macro library must define the following macros before
 * using this library.
 *
 * #define _seqbuf 		 name of the buffer (u_char[])
 * #define _SEQ_ADVBUF(len)	 If the applic needs to know the exact
 *				 size of the event, this macro can be used.
 *				 Otherwise this must be defined as empty.
 * #define _seqbufptr		 Define the name of index variable or 0 if
 *				 not required.
 */
#define _SEQ_NEEDBUF(len)	/* empty */
#endif

#define PM_LOAD_PATCH(dev, bank, pgm)	\
	(SEQ_DUMPBUF(), _pm_info.command = _PM_LOAD_PATCH, \
	_pm_info.device=dev, _pm_info.data.data8[0]=pgm, \
	_pm_info.parm1 = bank, _pm_info.parm2 = 1, \
	ioctl(seqfd, SNDCTL_PMGR_ACCESS, &_pm_info))
#define PM_LOAD_PATCHES(dev, bank, pgm) \
	(SEQ_DUMPBUF(), _pm_info.command = _PM_LOAD_PATCH, \
	_pm_info.device=dev, bcopy( pgm, _pm_info.data.data8,  128), \
	_pm_info.parm1 = bank, _pm_info.parm2 = 128, \
	ioctl(seqfd, SNDCTL_PMGR_ACCESS, &_pm_info))

#define SEQ_VOLUME_MODE(dev, mode)	{ \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr] = SEQ_EXTENDED;\
	_seqbuf[_seqbufptr+1] = SEQ_VOLMODE;\
	_seqbuf[_seqbufptr+2] = (dev);\
	_seqbuf[_seqbufptr+3] = (mode);\
	_seqbuf[_seqbufptr+4] = 0;\
	_seqbuf[_seqbufptr+5] = 0;\
	_seqbuf[_seqbufptr+6] = 0;\
	_seqbuf[_seqbufptr+7] = 0;\
	_SEQ_ADVBUF(8);}

/*
 * Midi voice messages
 */

#define _CHN_VOICE(dev, event, chn, note, parm)  { \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr] = EV_CHN_VOICE;\
	_seqbuf[_seqbufptr+1] = (dev);\
	_seqbuf[_seqbufptr+2] = (event);\
	_seqbuf[_seqbufptr+3] = (chn);\
	_seqbuf[_seqbufptr+4] = (note);\
	_seqbuf[_seqbufptr+5] = (parm);\
	_seqbuf[_seqbufptr+6] = (0);\
	_seqbuf[_seqbufptr+7] = 0;\
	_SEQ_ADVBUF(8);}

#define SEQ_START_NOTE(dev, chn, note, vol) \
		_CHN_VOICE(dev, MIDI_NOTEON, chn, note, vol)

#define SEQ_STOP_NOTE(dev, chn, note, vol) \
		_CHN_VOICE(dev, MIDI_NOTEOFF, chn, note, vol)

#define SEQ_KEY_PRESSURE(dev, chn, note, pressure) \
		_CHN_VOICE(dev, MIDI_KEY_PRESSURE, chn, note, pressure)

/*
 * Midi channel messages
 */

#define _CHN_COMMON(dev, event, chn, p1, p2, w14) { \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr] = EV_CHN_COMMON;\
	_seqbuf[_seqbufptr+1] = (dev);\
	_seqbuf[_seqbufptr+2] = (event);\
	_seqbuf[_seqbufptr+3] = (chn);\
	_seqbuf[_seqbufptr+4] = (p1);\
	_seqbuf[_seqbufptr+5] = (p2);\
	*(short *)&_seqbuf[_seqbufptr+6] = (w14);\
	_SEQ_ADVBUF(8);}
/*
 * SEQ_SYSEX permits sending of sysex messages. (It may look that it permits
 * sending any MIDI bytes but it's absolutely not possible. Trying to do
 * so _will_ cause problems with MPU401 intelligent mode).
 *
 * Sysex messages are sent in blocks of 1 to 6 bytes. Longer messages must be
 * sent by calling SEQ_SYSEX() several times (there must be no other events
 * between them). First sysex fragment must have 0xf0 in the first byte
 * and the last byte (buf[len-1] of the last fragment must be 0xf7. No byte
 * between these sysex start and end markers cannot be larger than 0x7f. Also
 * lengths of each fragments (except the last one) must be 6.
 *
 * Breaking the above rules may work with some MIDI ports but is likely to
 * cause fatal problems with some other devices (such as MPU401).
 */
#define SEQ_SYSEX(dev, buf, len) { \
	int i, l=(len); if (l>6)l=6;\
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr] = EV_SYSEX;\
	for(i=0;i<l;i++)_seqbuf[_seqbufptr+i+1] = (buf)[i];\
	for(i=l;i<6;i++)_seqbuf[_seqbufptr+i+1] = 0xff;\
	_SEQ_ADVBUF(8);}

#define SEQ_CHN_PRESSURE(dev, chn, pressure) \
	_CHN_COMMON(dev, MIDI_CHN_PRESSURE, chn, pressure, 0, 0)

#define SEQ_SET_PATCH(dev, chn, patch) \
	_CHN_COMMON(dev, MIDI_PGM_CHANGE, chn, patch, 0, 0)

#define SEQ_CONTROL(dev, chn, controller, value) \
	_CHN_COMMON(dev, MIDI_CTL_CHANGE, chn, controller, 0, value)

#define SEQ_BENDER(dev, chn, value) \
	_CHN_COMMON(dev, MIDI_PITCH_BEND, chn, 0, 0, value)


#define SEQ_V2_X_CONTROL(dev, voice, controller, value)	{ \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr] = SEQ_EXTENDED;\
	_seqbuf[_seqbufptr+1] = SEQ_CONTROLLER;\
	_seqbuf[_seqbufptr+2] = (dev);\
	_seqbuf[_seqbufptr+3] = (voice);\
	_seqbuf[_seqbufptr+4] = (controller);\
	*(short *)&_seqbuf[_seqbufptr+5] = (value);\
	_seqbuf[_seqbufptr+7] = 0;\
	_SEQ_ADVBUF(8);}

/*
 * The following 5 macros are incorrectly implemented and obsolete.
 * Use SEQ_BENDER and SEQ_CONTROL (with proper controller) instead.
 */

#define SEQ_PITCHBEND(dev, voice, value) \
	SEQ_V2_X_CONTROL(dev, voice, CTRL_PITCH_BENDER, value)
#define SEQ_BENDER_RANGE(dev, voice, value) \
	SEQ_V2_X_CONTROL(dev, voice, CTRL_PITCH_BENDER_RANGE, value)
#define SEQ_EXPRESSION(dev, voice, value) \
	SEQ_CONTROL(dev, voice, CTL_EXPRESSION, value*128)
#define SEQ_MAIN_VOLUME(dev, voice, value) \
	SEQ_CONTROL(dev, voice, CTL_MAIN_VOLUME, (value*16383)/100)
#define SEQ_PANNING(dev, voice, pos) \
	SEQ_CONTROL(dev, voice, CTL_PAN, (pos+128) / 2)

/*
 * Timing and synchronization macros
 */

#define _TIMER_EVENT(ev, parm)		{ \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr+0] = EV_TIMING; \
	_seqbuf[_seqbufptr+1] = (ev); \
	_seqbuf[_seqbufptr+2] = 0;\
	_seqbuf[_seqbufptr+3] = 0;\
	*(u_int *)&_seqbuf[_seqbufptr+4] = (parm); \
	_SEQ_ADVBUF(8); \
	}

#define SEQ_START_TIMER()		_TIMER_EVENT(TMR_START, 0)
#define SEQ_STOP_TIMER()		_TIMER_EVENT(TMR_STOP, 0)
#define SEQ_CONTINUE_TIMER()		_TIMER_EVENT(TMR_CONTINUE, 0)
#define SEQ_WAIT_TIME(ticks)		_TIMER_EVENT(TMR_WAIT_ABS, ticks)
#define SEQ_DELTA_TIME(ticks)		_TIMER_EVENT(TMR_WAIT_REL, ticks)
#define SEQ_ECHO_BACK(key)		_TIMER_EVENT(TMR_ECHO, key)
#define SEQ_SET_TEMPO(value)		_TIMER_EVENT(TMR_TEMPO, value)
#define SEQ_SONGPOS(pos)		_TIMER_EVENT(TMR_SPP, pos)
#define SEQ_TIME_SIGNATURE(sig)		_TIMER_EVENT(TMR_TIMESIG, sig)

/*
 * Local control events
 */

#define _LOCAL_EVENT(ev, parm)		{ \
	_SEQ_NEEDBUF(8);\
	_seqbuf[_seqbufptr+0] = EV_SEQ_LOCAL; \
	_seqbuf[_seqbufptr+1] = (ev); \
	_seqbuf[_seqbufptr+2] = 0;\
	_seqbuf[_seqbufptr+3] = 0;\
	*(u_int *)&_seqbuf[_seqbufptr+4] = (parm); \
	_SEQ_ADVBUF(8); \
	}

#define SEQ_PLAYAUDIO(devmask)		_LOCAL_EVENT(LOCL_STARTAUDIO, devmask)
/*
 * Events for the level 1 interface only
 */

#define SEQ_MIDIOUT(device, byte)	{ \
	_SEQ_NEEDBUF(4);\
	_seqbuf[_seqbufptr] = SEQ_MIDIPUTC;\
	_seqbuf[_seqbufptr+1] = (byte);\
	_seqbuf[_seqbufptr+2] = (device);\
	_seqbuf[_seqbufptr+3] = 0;\
	_SEQ_ADVBUF(4);}

/*
 * Patch loading.
 */
#define SEQ_WRPATCH(patchx, len)	{ \
	if (_seqbufptr) seqbuf_dump(); \
	if (write(seqfd, (char*)(patchx), len)==-1) \
	   perror("Write patch: /dev/sequencer"); \
	}

#define SEQ_WRPATCH2(patchx, len)	\
	( seqbuf_dump(), write(seqfd, (char*)(patchx), len) )

#endif

/*
 * Here I have moved all the aliases for ioctl names.
 */

#define SNDCTL_DSP_SAMPLESIZE	SNDCTL_DSP_SETFMT
#define SOUND_PCM_WRITE_BITS	SNDCTL_DSP_SETFMT
#define SOUND_PCM_SETFMT	SNDCTL_DSP_SETFMT

#define SOUND_PCM_WRITE_RATE	SNDCTL_DSP_SPEED
#define SOUND_PCM_POST		SNDCTL_DSP_POST
#define SOUND_PCM_RESET		SNDCTL_DSP_RESET
#define SOUND_PCM_SYNC		SNDCTL_DSP_SYNC
#define SOUND_PCM_SUBDIVIDE	SNDCTL_DSP_SUBDIVIDE
#define SOUND_PCM_SETFRAGMENT	SNDCTL_DSP_SETFRAGMENT
#define SOUND_PCM_GETFMTS	SNDCTL_DSP_GETFMTS
#define SOUND_PCM_GETOSPACE	SNDCTL_DSP_GETOSPACE
#define SOUND_PCM_GETISPACE	SNDCTL_DSP_GETISPACE
#define SOUND_PCM_NONBLOCK	SNDCTL_DSP_NONBLOCK
#define SOUND_PCM_GETCAPS	SNDCTL_DSP_GETCAPS
#define SOUND_PCM_GETTRIGGER	SNDCTL_DSP_GETTRIGGER
#define SOUND_PCM_SETTRIGGER	SNDCTL_DSP_SETTRIGGER
#define SOUND_PCM_SETSYNCRO	SNDCTL_DSP_SETSYNCRO
#define SOUND_PCM_GETIPTR	SNDCTL_DSP_GETIPTR
#define SOUND_PCM_GETOPTR	SNDCTL_DSP_GETOPTR
#define SOUND_PCM_MAPINBUF	SNDCTL_DSP_MAPINBUF
#define SOUND_PCM_MAPOUTBUF	SNDCTL_DSP_MAPOUTBUF

/***********************************************************************/

/**
 * XXX OSSv4 defines -- some bits taken straight out of the new
 * sys/soundcard.h bundled with recent OSS releases.
 *
 * NB:  These macros and structures will be reorganized and inserted
 * 	in appropriate places throughout this file once the code begins
 * 	to take shape.
 *
 * @todo reorganize layout more like the 4Front version
 * @todo ask about maintaining __SIOWR vs. _IOWR ioctl cmd defines
 */

/**
 * @note The @c OSSV4_EXPERIMENT macro is meant to wrap new development code
 * in the sound system relevant to adopting 4Front's OSSv4 specification.
 * Users should not enable this!  Really!
 */
#if 0
# define OSSV4_EXPERIMENT 1
#else
# undef OSSV4_EXPERIMENT
#endif

#ifdef SOUND_VERSION
# undef SOUND_VERSION
# define SOUND_VERSION	0x040000
#endif	/* !SOUND_VERSION */

#define OSS_LONGNAME_SIZE	64
#define OSS_LABEL_SIZE		16
#define OSS_DEVNODE_SIZE        32
typedef char oss_longname_t[OSS_LONGNAME_SIZE];
typedef char oss_label_t[OSS_LABEL_SIZE];
typedef char oss_devnode_t[OSS_DEVNODE_SIZE];

typedef struct audio_errinfo
{
	int		play_underruns;
	int		rec_overruns;
	unsigned int	play_ptradjust;
	unsigned int	rec_ptradjust;
	int		play_errorcount;
	int		rec_errorcount;
	int		play_lasterror;
	int		rec_lasterror;
	long		play_errorparm;
	long		rec_errorparm;
	int		filler[16];
} audio_errinfo;

#define SNDCTL_DSP_GETPLAYVOL           _IOR ('P', 24, int)
#define SNDCTL_DSP_SETPLAYVOL           _IOWR('P', 24, int)
#define SNDCTL_DSP_GETERROR             _IOR ('P', 25, audio_errinfo)


/*
 ****************************************************************************
 * Sync groups for audio devices
 */
typedef struct oss_syncgroup
{
  int id;
  int mode;
  int filler[16];
} oss_syncgroup;

#define SNDCTL_DSP_SYNCGROUP            _IOWR('P', 28, oss_syncgroup)
#define SNDCTL_DSP_SYNCSTART            _IOW ('P', 29, int)

/*
 **************************************************************************
 * "cooked" mode enables software based conversions for sample rate, sample
 * format (bits) and number of channels (mono/stereo). These conversions are
 * required with some devices that support only one sample rate or just stereo
 * to let the applications to use other formats. The cooked mode is enabled by
 * default. However it's necessary to disable this mode when mmap() is used or
 * when very deterministic timing is required. SNDCTL_DSP_COOKEDMODE is an
 * optional call introduced in OSS 3.9.6f. It's _error return must be ignored_
 * since normally this call will return erno=EINVAL.
 *
 * SNDCTL_DSP_COOKEDMODE must be called immediately after open before doing
 * anything else. Otherwise the call will not have any effect.
 */
#define SNDCTL_DSP_COOKEDMODE           _IOW ('P', 30, int)

/*
 **************************************************************************
 * SNDCTL_DSP_SILENCE and SNDCTL_DSP_SKIP are new calls in OSS 3.99.0
 * that can be used to implement pause/continue during playback (no effect
 * on recording).
 */
#define SNDCTL_DSP_SILENCE              _IO  ('P', 31)
#define SNDCTL_DSP_SKIP                 _IO  ('P', 32)

/*
 ****************************************************************************
 * Abort transfer (reset) functions for input and output
 */
#define SNDCTL_DSP_HALT_INPUT		_IO  ('P', 33)
#define SNDCTL_DSP_RESET_INPUT	SNDCTL_DSP_HALT_INPUT	/* Old name */
#define SNDCTL_DSP_HALT_OUTPUT		_IO  ('P', 34)
#define SNDCTL_DSP_RESET_OUTPUT	SNDCTL_DSP_HALT_OUTPUT	/* Old name */

/*
 ****************************************************************************
 * Low water level control
 */
#define SNDCTL_DSP_LOW_WATER		_IOW ('P', 34, int)

/** @todo Get rid of OSS_NO_LONG_LONG references? */

/*
 ****************************************************************************
 * 64 bit pointer support. Only available in environments that support
 * the 64 bit (long long) integer type.
 */
#ifndef OSS_NO_LONG_LONG
typedef struct
{
  long long samples;
  int fifo_samples;
  int filler[32];		/* For future use */
} oss_count_t;

#define SNDCTL_DSP_CURRENT_IPTR		_IOR ('P', 35, oss_count_t)
#define SNDCTL_DSP_CURRENT_OPTR		_IOR ('P', 36, oss_count_t)
#endif

/*
 ****************************************************************************
 * Interface for selecting recording sources and playback output routings.
 */
#define SNDCTL_DSP_GET_RECSRC_NAMES     _IOR ('P', 37, oss_mixer_enuminfo)
#define SNDCTL_DSP_GET_RECSRC           _IOR ('P', 38, int)
#define SNDCTL_DSP_SET_RECSRC           _IOWR('P', 38, int)

#define SNDCTL_DSP_GET_PLAYTGT_NAMES    _IOR ('P', 39, oss_mixer_enuminfo)
#define SNDCTL_DSP_GET_PLAYTGT          _IOR ('P', 40, int)
#define SNDCTL_DSP_SET_PLAYTGT          _IOWR('P', 40, int)
#define SNDCTL_DSP_GETRECVOL            _IOR ('P', 41, int)
#define SNDCTL_DSP_SETRECVOL            _IOWR('P', 41, int)

/*
 ***************************************************************************
 * Some calls for setting the channel assignment with multi channel devices
 * (see the manual for details).                                                 */
#define SNDCTL_DSP_GET_CHNORDER         _IOR ('P', 42, unsigned long long)
#define SNDCTL_DSP_SET_CHNORDER         _IOWR('P', 42, unsigned long long)
#       define CHID_UNDEF       0
#       define CHID_L           1
#       define CHID_R           2
#       define CHID_C           3
#       define CHID_LFE         4
#       define CHID_LS          5
#       define CHID_RS          6
#       define CHID_LR          7
#       define CHID_RR          8
#define CHNORDER_UNDEF          0x0000000000000000ULL
#define CHNORDER_NORMAL         0x0000000087654321ULL

#define MAX_PEAK_CHANNELS	128
typedef unsigned short oss_peaks_t[MAX_PEAK_CHANNELS];
#define SNDCTL_DSP_GETIPEAKS		_IOR('P', 43, oss_peaks_t)
#define SNDCTL_DSP_GETOPEAKS		_IOR('P', 44, oss_peaks_t)
#define SNDCTL_DSP_POLICY               _IOW('P', 45, int)    /* See the manual */

/*
 ****************************************************************************
 * Few ioctl calls that are not official parts of OSS. They have been used
 * by few freeware implementations of OSS.
 */
#define SNDCTL_DSP_GETCHANNELMASK	_IOWR('P', 64, int)
#define SNDCTL_DSP_BIND_CHANNEL		_IOWR('P', 65, int)
#define DSP_BIND_QUERY			0x00000000
#define DSP_BIND_FRONT			0x00000001
#define DSP_BIND_SURR			0x00000002
#define DSP_BIND_CENTER_LFE		0x00000004
#define DSP_BIND_HANDSET		0x00000008
#define DSP_BIND_MIC			0x00000010
#define DSP_BIND_MODEM1			0x00000020
#define DSP_BIND_MODEM2			0x00000040
#define DSP_BIND_I2S			0x00000080
#define DSP_BIND_SPDIF			0x00000100
#define DSP_BIND_REAR			0x00000200

/*
 * OSS_SYSIFO is obsolete. Use SNDCTL_SYSINFO insteads.
 */
#define OSS_GETVERSION                  _IOR ('M', 118, int)

/**
 * @brief	Argument for SNDCTL_SYSINFO ioctl.
 *
 * For use w/ the SNDCTL_SYSINFO ioctl available on audio (/dev/dsp*),
 * mixer, and MIDI devices.
 */
typedef struct oss_sysinfo
{
	char	product[32];	/* For example OSS/Free, OSS/Linux or
				   OSS/Solaris */
	char	version[32];	/* For example 4.0a */
	int	versionnum;	/* See OSS_GETVERSION */
	char	options[128];	/* Reserved */

	int	numaudios;	/* # of audio/dsp devices */
	int	openedaudio[8];	/* Bit mask telling which audio devices
				   are busy */

	int	numsynths;	/* # of availavle synth devices */
	int	nummidis;	/* # of available MIDI ports */
	int	numtimers;	/* # of available timer devices */
	int	nummixers;	/* # of mixer devices */

	int	openedmidi[8];	/* Bit mask telling which midi devices
				   are busy */
	int	numcards;	/* Number of sound cards in the system */
	int	numaudioengines;	/* Number of audio engines in the system */
	char	license[16];	/* For example "GPL" or "CDDL" */
	char	revision_info[256];	/* For internal use */
	int	filler[172];	/* For future expansion (set to -1) */
} oss_sysinfo;

typedef struct oss_mixext
{
  int dev;			/* Mixer device number */
  int ctrl;			/* Controller number */
  int type;			/* Entry type */
#	define MIXT_DEVROOT	 0	/* Device root entry */
#	define MIXT_GROUP	 1	/* Controller group */
#	define MIXT_ONOFF	 2	/* OFF (0) or ON (1) */
#	define MIXT_ENUM	 3	/* Enumerated (0 to maxvalue) */
#	define MIXT_MONOSLIDER	 4	/* Mono slider (0 to 100) */
#	define MIXT_STEREOSLIDER 5	/* Stereo slider (dual 0 to 100) */
#	define MIXT_MESSAGE	 6	/* (Readable) textual message */
#	define MIXT_MONOVU	 7	/* VU meter value (mono) */
#	define MIXT_STEREOVU	 8	/* VU meter value (stereo) */
#	define MIXT_MONOPEAK	 9	/* VU meter peak value (mono) */
#	define MIXT_STEREOPEAK	10	/* VU meter peak value (stereo) */
#	define MIXT_RADIOGROUP	11	/* Radio button group */
#	define MIXT_MARKER	12	/* Separator between normal and extension entries */
#	define MIXT_VALUE	13	/* Decimal value entry */
#	define MIXT_HEXVALUE	14	/* Hexadecimal value entry */
#	define MIXT_MONODB	15	/* Mono atten. slider (0 to -144) */
#	define MIXT_STEREODB	16	/* Stereo atten. slider (dual 0 to -144) */
#	define MIXT_SLIDER	17	/* Slider (mono) with full integer range */
#	define MIXT_3D		18

  /* Possible value range (minvalue to maxvalue) */
  /* Note that maxvalue may also be smaller than minvalue */
  int maxvalue;
  int minvalue;

  int flags;
#	define MIXF_READABLE	0x00000001	/* Has readable value */
#	define MIXF_WRITEABLE	0x00000002	/* Has writeable value */
#	define MIXF_POLL	0x00000004	/* May change itself */
#	define MIXF_HZ		0x00000008	/* Herz scale */
#	define MIXF_STRING	0x00000010	/* Use dynamic extensions for value */
#	define MIXF_DYNAMIC	0x00000010	/* Supports dynamic extensions */
#	define MIXF_OKFAIL	0x00000020	/* Interpret value as 1=OK, 0=FAIL */
#	define MIXF_FLAT	0x00000040	/* Flat vertical space requirements */
#	define MIXF_LEGACY	0x00000080	/* Legacy mixer control group */
  char id[16];			/* Mnemonic ID (mainly for internal use) */
  int parent;			/* Entry# of parent (group) node (-1 if root) */

  int dummy;			/* Internal use */

  int timestamp;

  char data[64];		/* Misc data (entry type dependent) */
  unsigned char enum_present[32];	/* Mask of allowed enum values */
  int control_no;		/* SOUND_MIXER_VOLUME..SOUND_MIXER_MIDI */
  /* (-1 means not indicated) */

/*
 * The desc field is reserved for internal purposes of OSS. It should not be 
 * used by applications.
 */
  unsigned int desc;
#define MIXEXT_SCOPE_MASK			0x0000003f
#define MIXEXT_SCOPE_OTHER			0x00000000
#define MIXEXT_SCOPE_INPUT			0x00000001
#define MIXEXT_SCOPE_OUTPUT			0x00000002
#define MIXEXT_SCOPE_MONITOR			0x00000003
#define MIXEXT_SCOPE_RECSWITCH			0x00000004

  char extname[32];
  int update_counter;
  int filler[7];
} oss_mixext;

typedef struct oss_mixext_root
{
  char id[16];
  char name[48];
} oss_mixext_root;

typedef struct oss_mixer_value
{
  int dev;
  int ctrl;
  int value;
  int flags;			/* Reserved for future use. Initialize to 0 */
  int timestamp;		/* Must be set to oss_mixext.timestamp */
  int filler[8];		/* Reserved for future use. Initialize to 0 */
} oss_mixer_value;

#define OSS_ENUM_MAXVALUE       255
typedef struct oss_mixer_enuminfo
{
	int	dev;
	int	ctrl;
	int	nvalues;
	int	version;                  /* Read the manual */
	short	strindex[OSS_ENUM_MAXVALUE];
	char	strings[3000];
} oss_mixer_enuminfo;

#define OPEN_READ       PCM_ENABLE_INPUT
#define OPEN_WRITE      PCM_ENABLE_OUTPUT
#define OPEN_READWRITE  (OPEN_READ|OPEN_WRITE)

/**
 * @brief	Argument for SNDCTL_AUDIOINFO ioctl.
 *
 * For use w/ the SNDCTL_AUDIOINFO ioctl available on audio (/dev/dsp*)
 * devices.
 */
typedef struct oss_audioinfo
{
	int	dev;		/* Audio device number */
	char	name[64];
	int	busy;		/* 0, OPEN_READ, OPEN_WRITE or OPEN_READWRITE */
	int	pid;
	int	caps;		/* DSP_CAP_INPUT, DSP_CAP_OUTPUT */
	int	iformats;
	int	oformats;
	int	magic;		/* Reserved for internal use */
	char 	cmd[64];	/* Command using the device (if known) */
	int	card_number;
	int	port_number;
	int	mixer_dev;
	int	real_device;	/* Obsolete field. Replaced by devnode */
	int	enabled;	/* 1=enabled, 0=device not ready at this
				   moment */
	int	flags;		/* For internal use only - no practical
				   meaning */
	int	min_rate;	/* Sample rate limits */
	int	max_rate;
	int	min_channels;	/* Number of channels supported */
	int	max_channels;
	int	binding;	/* DSP_BIND_FRONT, etc. 0 means undefined */
	int	rate_source;
	char	handle[32];
	#define OSS_MAX_SAMPLE_RATES	20	/* Cannot be changed  */
	unsigned int nrates;
	unsigned int rates[OSS_MAX_SAMPLE_RATES]; /* Please read the manual before using these */
	oss_longname_t	song_name;	/* Song name (if given) */
	oss_label_t	label;		/* Device label (if given) */
	int		latency;	/* In usecs, -1=unknown */
	oss_devnode_t	devnode;	/* Device special file name (inside
					   /dev) */
	int next_play_engine;
	int next_rec_engine;
	int filler[184];
} oss_audioinfo;

typedef struct oss_mixerinfo
{
  int dev;
  char id[16];
  char name[32];
  int modify_counter;
  int card_number;
  int port_number;
  char handle[32];
  int magic;			/* Reserved */
  int enabled;			/* Reserved */
  int caps;
#define MIXER_CAP_VIRTUAL				0x00000001
  int flags;			/* Reserved */
  int nrext;
  /*
   * The priority field can be used to select the default (motherboard)
   * mixer device. The mixer with the highest priority is the
   * most preferred one. -2 or less means that this device cannot be used
   * as the default mixer.
   */
  int priority;
  int filler[254];		/* Reserved */
} oss_mixerinfo;

typedef struct oss_midi_info
{
  int dev;			/* Midi device number */
  char name[64];
  int busy;			/* 0, OPEN_READ, OPEN_WRITE or OPEN_READWRITE */
  int pid;
  char cmd[64];			/* Command using the device (if known) */
  int caps;
#define MIDI_CAP_MPU401		0x00000001	/**** OBSOLETE ****/
#define MIDI_CAP_INPUT		0x00000002
#define MIDI_CAP_OUTPUT		0x00000004
#define MIDI_CAP_INOUT		(MIDI_CAP_INPUT|MIDI_CAP_OUTPUT)
#define MIDI_CAP_VIRTUAL	0x00000008	/* Pseudo device */
#define MIDI_CAP_MTCINPUT	0x00000010	/* Supports SNDCTL_MIDI_MTCINPUT */
#define MIDI_CAP_CLIENT		0x00000020	/* Virtual client side device */
#define MIDI_CAP_SERVER		0x00000040	/* Virtual server side device */
#define MIDI_CAP_INTERNAL	0x00000080	/* Internal (synth) device */
#define MIDI_CAP_EXTERNAL	0x00000100	/* external (MIDI port) device */
#define MIDI_CAP_PTOP		0x00000200	/* Point to point link to one device */
#define MIDI_CAP_MTC		0x00000400	/* MTC/SMPTE (control) device */
  int magic;			/* Reserved for internal use */
  int card_number;
  int port_number;
  int enabled;			/* 1=enabled, 0=device not ready at this moment */
  int flags;			/* For internal use only - no practical meaning */
  char handle[32];
  oss_longname_t song_name;	/* Song name (if known) */
  oss_label_t label;		/* Device label (if given) */
  int latency;			/* In usecs, -1=unknown */
  int filler[244];
} oss_midi_info;

typedef struct oss_card_info
{
  int card;
  char shortname[16];
  char longname[128];
  int flags;
  char hw_info[400];
  int intr_count, ack_count;
  int filler[154];
} oss_card_info;

#define SNDCTL_SYSINFO          _IOR ('X', 1, oss_sysinfo)
#define OSS_SYSINFO             SNDCTL_SYSINFO /* Old name */

#define SNDCTL_MIX_NRMIX	_IOR ('X', 2, int)
#define SNDCTL_MIX_NREXT	_IOWR('X', 3, int)
#define SNDCTL_MIX_EXTINFO	_IOWR('X', 4, oss_mixext)
#define SNDCTL_MIX_READ		_IOWR('X', 5, oss_mixer_value)
#define SNDCTL_MIX_WRITE	_IOWR('X', 6, oss_mixer_value)

#define SNDCTL_AUDIOINFO	_IOWR('X', 7, oss_audioinfo)
#define SNDCTL_MIX_ENUMINFO	_IOWR('X', 8, oss_mixer_enuminfo)
#define SNDCTL_MIDIINFO		_IOWR('X', 9, oss_midi_info)
#define SNDCTL_MIXERINFO	_IOWR('X',10, oss_mixerinfo)
#define SNDCTL_CARDINFO		_IOWR('X',11, oss_card_info)
#define SNDCTL_ENGINEINFO	_IOWR('X',12, oss_audioinfo)
#define SNDCTL_AUDIOINFO_EX	_IOWR('X',13, oss_audioinfo)

/*
 * Few more "globally" available ioctl calls.
 */
#define SNDCTL_SETSONG          _IOW ('Y', 2, oss_longname_t)
#define SNDCTL_GETSONG          _IOR ('Y', 2, oss_longname_t)
#define SNDCTL_SETNAME          _IOW ('Y', 3, oss_longname_t)
#define SNDCTL_SETLABEL         _IOW ('Y', 4, oss_label_t)
#define SNDCTL_GETLABEL         _IOR ('Y', 4, oss_label_t)

#endif	/* !_SYS_SOUNDCARD_H_ */
