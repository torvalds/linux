/*
 **********************************************************************
 *     isblive_mid.h
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#ifndef _ICARDMIDI_H
#define _ICARDMIDI_H

/* MIDI defines */
#define MIDI_DATA_FIRST                 0x00
#define MIDI_DATA_LAST                  0x7F
#define MIDI_STATUS_FIRST               0x80
#define MIDI_STATUS_LAST                0xFF

/* Channel status bytes */
#define MIDI_STATUS_CHANNEL_FIRST       0x80
#define MIDI_STATUS_CHANNEL_LAST        0xE0
#define MIDI_STATUS_CHANNEL_MASK        0xF0

/* Channel voice messages */
#define MIDI_VOICE_NOTE_OFF             0x80
#define MIDI_VOICE_NOTE_ON              0x90
#define MIDI_VOICE_POLY_PRESSURE        0xA0
#define MIDI_VOICE_CONTROL_CHANGE       0xB0
#define MIDI_VOICE_PROGRAM_CHANGE       0xC0
#define MIDI_VOICE_CHANNEL_PRESSURE     0xD0
#define MIDI_VOICE_PITCH_BEND           0xE0

/* Channel mode messages */
#define MIDI_MODE_CHANNEL               MIDI_VOICE_CONTROL_CHANGE

/* System status bytes */
#define MIDI_STATUS_SYSTEM_FIRST        0xF0
#define MIDI_STATUS_SYSTEM_LAST         0xFF

/* System exclusive messages */
#define MIDI_SYSEX_BEGIN                0xF0
#define MIDI_SYSEX_EOX                  0xF7

/* System common messages */
#define MIDI_COMMON_TCQF                0xF1	/* Time code quarter frame  */
#define MIDI_COMMON_SONG_POSITION       0xF2
#define MIDI_COMMON_SONG_SELECT         0xF3
#define MIDI_COMMON_UNDEFINED_F4        0xF4
#define MIDI_COMMON_UNDEFINED_F5        0xF5
#define MIDI_COMMON_TUNE_REQUEST        0xF6

/* System real-time messages */
#define MIDI_RTIME_TIMING_CLOCK         0xF8
#define MIDI_RTIME_UNDEFINED_F9         0xF9
#define MIDI_RTIME_START                0xFA
#define MIDI_RTIME_CONTINUE             0xFB
#define MIDI_RTIME_STOP                 0xFC
#define MIDI_RTIME_UNDEFINED_FD         0xFD
#define MIDI_RTIME_ACTIVE_SENSING       0xFE
#define MIDI_RTIME_SYSTEM_RESET         0xFF

/* Flags for flags parm of midiOutCachePatches(), midiOutCacheDrumPatches() */
#define MIDI_CACHE_ALL                  1
#define MIDI_CACHE_BESTFIT              2
#define MIDI_CACHE_QUERY                3
#define MIDI_UNCACHE                    4

/* Event declarations for MPU IRQ Callbacks */
#define ICARDMIDI_INLONGDATA            0x00000001 /* MIM_LONGDATA */
#define ICARDMIDI_INLONGERROR           0x00000002 /* MIM_LONGERROR */
#define ICARDMIDI_OUTLONGDATA           0x00000004 /* MOM_DONE for MPU OUT buffer */
#define ICARDMIDI_INDATA                0x00000010 /* MIM_DATA */
#define ICARDMIDI_INDATAERROR           0x00000020 /* MIM_ERROR */

/* Declaration for flags in CARDMIDIBUFFERHDR */
/* Make it the same as MHDR_DONE, MHDR_INQUEUE in mmsystem.h */
#define MIDIBUF_DONE                    0x00000001
#define MIDIBUF_INQUEUE                 0x00000004

/* Declaration for msg parameter in midiCallbackFn */
#define ICARDMIDI_OUTBUFFEROK           0x00000001
#define ICARDMIDI_INMIDIOK              0x00000002

/* Declaration for technology in struct midi_caps */
#define MT_MIDIPORT                     0x00000001	/* In original MIDIOUTCAPS structure */
#define MT_FMSYNTH                      0x00000004	/* In original MIDIOUTCAPS structure */
#define MT_AWESYNTH                     0x00001000
#define MT_PCISYNTH                     0x00002000
#define MT_PCISYNTH64                   0x00004000
#define CARDMIDI_AWEMASK                0x0000F000

enum LocalErrorCode
{
        CTSTATUS_NOTENABLED = 0x7000,
        CTSTATUS_READY,
        CTSTATUS_BUSY,
        CTSTATUS_DATAAVAIL,
        CTSTATUS_NODATA,
        CTSTATUS_NEXT_BYTE
};

/* MIDI data block header */
struct midi_hdr
{
	u8 *reserved;		/* Pointer to original locked data block */
	u32 bufferlength;	/* Length of data in data block */
	u32 bytesrecorded;	/* Used for input only */
	u32 user;		/* For client's use */
	u32 flags;		/* Assorted flags (see defines) */
	struct list_head list;	/* Reserved for driver */
	u8 *data;		/* Second copy of first pointer */
};

/* Enumeration for SetControl */
enum
{
	MIDIOBJVOLUME = 0x1,
	MIDIQUERYACTIVEINST
};

struct midi_queue
{
	struct midi_queue  *next;
	u32 qtype;            /* 0 = short message, 1 = long data */
	u32 length;
	u32 sizeLeft;
	u8 *midibyte;
	unsigned long refdata;
};

struct midi_openinfo
{
	u32     cbsize;
	u32     flags;
	unsigned long  refdata;
	u32     streamid;
};

int emu10k1_midi_callback(unsigned long , unsigned long, unsigned long *);

#endif /* _ICARDMIDI_H */
