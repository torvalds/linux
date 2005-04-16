/*     
 **********************************************************************
 *     cardwo.h -- header file for card wave out functions
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

#ifndef _CARDWO_H
#define _CARDWO_H

#include "icardwav.h"
#include "audio.h"
#include "voicemgr.h"
#include "timer.h"

/* setting this to other than a power of two may break some applications */
#define WAVEOUT_MAXBUFSIZE	MAXBUFSIZE

#define WAVEOUT_DEFAULTFRAGLEN	20 /* Time to play a fragment in ms (latency) */
#define WAVEOUT_DEFAULTBUFLEN	500 /* Time to play the entire buffer in ms */

#define WAVEOUT_MINFRAGSHIFT	6 /* Minimum fragment size in bytes is 2^6 */
#define WAVEOUT_MINFRAGS	3 /* _don't_ go bellow 3, it would break silence filling */
#define WAVEOUT_MAXVOICES	6

struct waveout_buffer {
	u16 ossfragshift;
	u32 numfrags;
	u32 fragment_size;	/* in bytes units */
	u32 size;		/* in bytes units */
	u32 pages;		/* buffer size in page units*/
	u32 silence_pos;	/* software cursor position (including silence bytes) */
	u32 hw_pos;		/* hardware cursor position */
	u32 free_bytes;		/* free bytes available on the buffer (not including silence bytes) */
	u8 fill_silence;
	u32 silence_bytes;      /* silence bytes on the buffer */
};

struct woinst 
{
	u8 state;
	u8 num_voices;
	struct emu_voice voice[WAVEOUT_MAXVOICES];
	struct emu_timer timer;
	struct wave_format format;
	struct waveout_buffer buffer;
	wait_queue_head_t wait_queue;
	u8 mmapped;
	u32 total_copied;	/* total number of bytes written() to the buffer (excluding silence) */
	u32 total_played;	/* total number of bytes played including silence */
	u32 blocks;
	u8 device;
	spinlock_t lock;
};

int emu10k1_waveout_open(struct emu10k1_wavedevice *);
void emu10k1_waveout_close(struct emu10k1_wavedevice *);
void emu10k1_waveout_start(struct emu10k1_wavedevice *);
void emu10k1_waveout_stop(struct emu10k1_wavedevice *);
void emu10k1_waveout_getxfersize(struct woinst*, u32 *);
void emu10k1_waveout_xferdata(struct woinst*, u8 __user *, u32 *);
void emu10k1_waveout_fillsilence(struct woinst*);
int emu10k1_waveout_setformat(struct emu10k1_wavedevice*, struct wave_format*);
void emu10k1_waveout_update(struct woinst*);

#endif /* _CARDWO_H */
