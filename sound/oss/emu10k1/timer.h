/*
 **********************************************************************
 *     timer.h
 *     Copyright (C) 1999, 2000 Creative Labs, Inc.
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


#ifndef _TIMER_H
#define _TIMER_H

#include <linux/sched.h>
#include <linux/interrupt.h>
#include "hwaccess.h"

struct emu_timer 
{
	struct list_head list;
	struct tasklet_struct tasklet;
	u8 state; 
	u16 count;				/* current number of interrupts */
	u16 count_max;				/* number of interrupts needed to schedule the bh */
	u16 delay;                              /* timer delay */
};

void emu10k1_timer_install(struct emu10k1_card *, struct emu_timer *, u16);
void emu10k1_timer_uninstall(struct emu10k1_card *, struct emu_timer *);
void emu10k1_timer_enable(struct emu10k1_card *, struct emu_timer *);
void emu10k1_timer_disable(struct emu10k1_card *, struct emu_timer *);

#define TIMER_STOPPED 			0xffff 
#define TIMER_STATE_INSTALLED 		0x01
#define TIMER_STATE_ACTIVE		0x02
#define TIMER_STATE_UNINSTALLED 	0x04

#endif /* _TIMER_H */
