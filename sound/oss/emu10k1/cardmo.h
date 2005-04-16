/*
 **********************************************************************
 *     cardmo.h
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox	    cleaned up
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

#ifndef _CARDMO_H
#define _CARDMO_H

#include "icardmid.h"
#include <linux/interrupt.h>

#define CARDMIDIOUT_STATE_DEFAULT    0x00000000
#define CARDMIDIOUT_STATE_SUSPEND    0x00000001

struct emu10k1_mpuout
{
	u32			status;
	u32			state;
	volatile int		intr;
	struct midi_queue	*firstmidiq;
	struct midi_queue	*lastmidiq;
	u8			laststatus;
	struct tasklet_struct 	tasklet;
	spinlock_t		lock;
	struct midi_openinfo	openinfo;
};

int emu10k1_mpuout_open(struct emu10k1_card *, struct midi_openinfo *);
int emu10k1_mpuout_close(struct emu10k1_card *);
int emu10k1_mpuout_add_buffer(struct emu10k1_card *, struct midi_hdr *);

int emu10k1_mpuout_irqhandler(struct emu10k1_card *);
void emu10k1_mpuout_bh(unsigned long);

#endif  /* _CARDMO_H */
