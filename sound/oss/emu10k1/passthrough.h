/*
 **********************************************************************
 *     passthrough.h -- Emu10k1 digital passthrough header file
 *     Copyright (C) 2001  Juha Yrjölä <jyrjola@cc.hut.fi>
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     May 15, 2001	    Juha Yrjölä     base code release
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

#ifndef _PASSTHROUGH_H
#define _PASSTHROUGH_H

#include "audio.h"

/* number of 16-bit stereo samples in XTRAM buffer */
#define PT_SAMPLES 0x8000
#define PT_BLOCKSAMPLES 0x400
#define PT_BLOCKSIZE (PT_BLOCKSAMPLES*4)
#define PT_BLOCKSIZE_LOG2 12
#define PT_BLOCKCOUNT (PT_SAMPLES/PT_BLOCKSAMPLES)
#define PT_INITPTR (PT_SAMPLES/2-1)

#define PT_STATE_INACTIVE 0
#define PT_STATE_ACTIVATED 1
#define PT_STATE_PLAYING 2

/* passthrough struct */
struct pt_data
{
	u8	selected, state, spcs_to_use;
	int	intr_gpr, enable_gpr, pos_gpr;
	u32	blocks_played, blocks_copied, old_spcs[3];
	u32	playptr, copyptr;
	u32	prepend_size;
	u8	*buf;
	u8	ac3data;

	char	*patch_name, *intr_gpr_name, *enable_gpr_name, *pos_gpr_name;

	wait_queue_head_t wait;
	spinlock_t lock;
};

/*
  Passthrough can be done in two methods:

  Method 1 : tram
     In original emu10k1, we couldn't bypass the sample rate converters. Even at 48kHz
     (the internal sample rate of the emu10k1) the samples would get messed up.
     To over come this, samples are copied into the tram and a special dsp patch copies
     the samples out and generates interrupts when a block has finnished playing.

  Method 2 : Interpolator bypass

     Creative fixed the sample rate convert problem in emu10k1 rev 7 and higher
     (including the emu10k2 (audigy)). This allows us to use the regular, and much simpler
     playback method. 


  In both methods, dsp code is used to mux audio and passthrough. This ensures that the spdif
  doesn't receive audio and pasthrough data at the same time. The spdif flag SPCS_NOTAUDIODATA
  is set to tell 

 */

// emu10k1 revs greater than or equal to 7 can use method2

#define USE_PT_METHOD2  (card->is_audigy)
#define USE_PT_METHOD1	!USE_PT_METHOD2

ssize_t emu10k1_pt_write(struct file *file, const char __user *buf, size_t count);

int emu10k1_pt_setup(struct emu10k1_wavedevice *wave_dev);
void emu10k1_pt_stop(struct emu10k1_card *card);
void emu10k1_pt_waveout_update(struct emu10k1_wavedevice *wave_dev);

#endif /* _PASSTHROUGH_H */
