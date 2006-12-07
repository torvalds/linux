/*
 **********************************************************************
 *     cardwi.h -- header file for card wave input functions
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
#ifndef _CARDWI_H
#define _CARDWI_H

#include "icardwav.h"
#include "audio.h"
#include "timer.h"

struct wavein_buffer {
	u16 ossfragshift;
        u32 fragment_size;
        u32 numfrags;
	u32 hw_pos;		/* hardware cursor position */
	u32 pos;		/* software cursor position */
	u32 bytestocopy;	/* bytes of recorded data available */
	u32 size;
	u32 pages;
	u32 sizereg;
	u32 sizeregval;
        u32 addrreg;
        u32 idxreg;
        u32 adcctl;
	void *addr;
	u8 cov;
	dma_addr_t dma_handle;	
};

struct wiinst
{
	u8 state;
	struct emu_timer timer;
	struct wave_format format;
	struct wavein_buffer buffer;
	wait_queue_head_t wait_queue;
	u8 mmapped;
	u32 total_recorded;	/* total bytes read() from device */
	u32 blocks;
	spinlock_t lock;
	u8 recsrc;
	u16 fxwc;
};

#define WAVEIN_MAXBUFSIZE	65536
#define WAVEIN_MINBUFSIZE	368

#define WAVEIN_DEFAULTFRAGLEN	100 
#define WAVEIN_DEFAULTBUFLEN	1000

#define WAVEIN_MINFRAGSHIFT	8 
#define WAVEIN_MINFRAGS		2

int emu10k1_wavein_open(struct emu10k1_wavedevice *);
void emu10k1_wavein_close(struct emu10k1_wavedevice *);
void emu10k1_wavein_start(struct emu10k1_wavedevice *);
void emu10k1_wavein_stop(struct emu10k1_wavedevice *);
void emu10k1_wavein_getxfersize(struct wiinst *, u32 *);
int emu10k1_wavein_xferdata(struct wiinst *, u8 __user *, u32 *);
int emu10k1_wavein_setformat(struct emu10k1_wavedevice *, struct wave_format *);
void emu10k1_wavein_update(struct emu10k1_card *, struct wiinst *);


#endif /* _CARDWI_H */
