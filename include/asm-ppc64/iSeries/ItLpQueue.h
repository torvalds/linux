/*
 * ItLpQueue.h
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ITLPQUEUE_H
#define _ITLPQUEUE_H

/*
 *	This control block defines the simple LP queue structure that is
 *	shared between the hypervisor (PLIC) and the OS in order to send
 *	events to an LP.
 */

#include <asm/types.h>
#include <asm/ptrace.h>

struct HvLpEvent;

#define ITMaxLpQueues	8

#define NotUsed		0	// Queue will not be used by PLIC
#define DedicatedIo	1	// Queue dedicated to IO processor specified
#define DedicatedLp	2	// Queue dedicated to LP specified
#define Shared		3	// Queue shared for both IO and LP

#define LpEventStackSize	4096
#define LpEventMaxSize		256
#define LpEventAlign		64

struct hvlpevent_queue {
/*
 * The xSlicCurEventPtr is the pointer to the next event stack entry
 * that will become valid.  The OS must peek at this entry to determine
 * if it is valid.  PLIC will set the valid indicator as the very last
 * store into that entry.
 *
 * When the OS has completed processing of the event then it will mark
 * the event as invalid so that PLIC knows it can store into that event
 * location again.
 *
 * If the event stack fills and there are overflow events, then PLIC
 * will set the xPlicOverflowIntPending flag in which case the OS will
 * have to fetch the additional LP events once they have drained the
 * event stack.
 *
 * The first 16-bytes are known by both the OS and PLIC.  The remainder
 * of the cache line is for use by the OS.
 */
	u8	xPlicOverflowIntPending;// 0x00 Overflow events are pending
	u8	xPlicStatus;		// 0x01 DedicatedIo or DedicatedLp or NotUsed
	u16	xSlicLogicalProcIndex;	// 0x02 Logical Proc Index for correlation
	u8	xPlicRsvd[12];		// 0x04
	char	*xSlicCurEventPtr;	// 0x10
	char	*xSlicLastValidEventPtr; // 0x18
	char	*xSlicEventStackPtr;	// 0x20
	u8	xIndex;			// 0x28 unique sequential index.
	u8	xSlicRsvd[3];		// 0x29-2b
	spinlock_t	lock;
};

extern struct hvlpevent_queue hvlpevent_queue;

extern int hvlpevent_is_pending(void);
extern void process_hvlpevents(struct pt_regs *);
extern void setup_hvlpevent_queue(void);

#endif /* _ITLPQUEUE_H */
