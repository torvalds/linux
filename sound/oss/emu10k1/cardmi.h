/*
 **********************************************************************
 *     sblive_mi.h
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up
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

#ifndef _CARDMI_H
#define _CARDMI_H

#include "icardmid.h"
#include <linux/interrupt.h>

typedef enum
{
	STIN_PARSE = 0,
	STIN_3BYTE,                     /* 0x80, 0x90, 0xA0, 0xB0, 0xE0 */
	STIN_3BYTE_KEY,                 /* Byte 1 */
	STIN_3BYTE_VEL,                 /* Byte 1 */
	STIN_2BYTE,                     /* 0xC0, 0xD0 */
	STIN_2BYTE_KEY,                 /* Byte 1 */
	STIN_SYS_COMMON_2,              /* 0xF1, 0xF3  */
	STIN_SYS_COMMON_2_KEY,
	STIN_SYS_COMMON_3,              /* 0xF2 */
	STIN_SYS_COMMON_3_KEY,
	STIN_SYS_COMMON_3_VEL,
	STIN_SYS_EX_NORM,               /* 0xF0, Normal mode */
	STIN_SYS_REAL
} midi_in_state;


/* flags for card MIDI in object */
#define FLAGS_MIDM_STARTED          0x00001000      // Data has started to come in after Midm Start
#define MIDIIN_MAX_BUFFER_SIZE      200             // Definition for struct emu10k1_mpuin

struct midi_data
{
	u8 data;
	u32 timein;
};

struct emu10k1_mpuin
{
	spinlock_t        lock;
	struct midi_queue *firstmidiq;
	struct midi_queue *lastmidiq;
	unsigned          qhead, qtail;
	struct midi_data  midiq[MIDIIN_MAX_BUFFER_SIZE];
	struct tasklet_struct tasklet;
	struct midi_openinfo    openinfo;

	/* For MIDI state machine */
	u8              status;        /* For MIDI running status */
	u8              fstatus;       /* For 0xFn status only */
	midi_in_state   curstate;
	midi_in_state   laststate;
	u32             timestart;
	u32             timein;
	u8              data;
};

int emu10k1_mpuin_open(struct emu10k1_card *, struct midi_openinfo *);
int emu10k1_mpuin_close(struct emu10k1_card *);
int emu10k1_mpuin_add_buffer(struct emu10k1_mpuin *, struct midi_hdr *);
int emu10k1_mpuin_start(struct emu10k1_card *);
int emu10k1_mpuin_stop(struct emu10k1_card *);
int emu10k1_mpuin_reset(struct emu10k1_card *);

int emu10k1_mpuin_irqhandler(struct emu10k1_card *);
void emu10k1_mpuin_bh(unsigned long);

#endif  /* _CARDMI_H */
