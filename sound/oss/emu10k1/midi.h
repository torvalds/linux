/*     
 **********************************************************************
 *     midi.h
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

#ifndef _MIDI_H
#define _MIDI_H

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define MIDIIN_STATE_STARTED 0x00000001
#define MIDIIN_STATE_STOPPED 0x00000002

#define MIDIIN_BUFLEN 1024

struct emu10k1_mididevice
{
	struct emu10k1_card *card;
	u32 mistate;
	wait_queue_head_t oWait;
	wait_queue_head_t iWait;
	s8 iBuf[MIDIIN_BUFLEN];
	u16 ird, iwr, icnt;
	struct list_head mid_hdrs;
};

/* uncomment next line to use midi port on Audigy drive */
//#define USE_AUDIGY_DRIVE_MIDI

#ifdef USE_AUDIGY_DRIVE_MIDI
#define A_MUDATA	A_MUDATA2
#define A_MUCMD		A_MUCMD2
#define A_MUSTAT	A_MUCMD2
#define A_IPR_MIDITRANSBUFEMPTY	A_IPR_MIDITRANSBUFEMPTY2
#define A_IPR_MIDIRECVBUFEMPTY	A_IPR_MIDIRECVBUFEMPTY2
#define A_INTE_MIDITXENABLE	A_INTE_MIDITXENABLE2
#define A_INTE_MIDIRXENABLE	A_INTE_MIDIRXENABLE2
#else
#define A_MUDATA	A_MUDATA1
#define A_MUCMD		A_MUCMD1
#define A_MUSTAT	A_MUCMD1
#define A_IPR_MIDITRANSBUFEMPTY	A_IPR_MIDITRANSBUFEMPTY1
#define A_IPR_MIDIRECVBUFEMPTY	A_IPR_MIDIRECVBUFEMPTY1
#define A_INTE_MIDITXENABLE	A_INTE_MIDITXENABLE1
#define A_INTE_MIDIRXENABLE	A_INTE_MIDIRXENABLE1
#endif


#endif /* _MIDI_H */

