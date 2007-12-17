/*
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

/* This file contains the class for HV events in the system. */

#ifndef _ASM_POWERPC_ISERIES_HV_LP_EVENT_H
#define _ASM_POWERPC_ISERIES_HV_LP_EVENT_H

#include <asm/types.h>
#include <asm/ptrace.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_call_event.h>

/*
 * HvLpEvent is the structure for Lp Event messages passed between
 * partitions through PLIC.
 */

struct HvLpEvent {
	u8	flags;			/* Event flags		      x00-x00 */
	u8	xType;			/* Type of message	      x01-x01 */
	u16	xSubtype;		/* Subtype for event	      x02-x03 */
	u8	xSourceLp;		/* Source LP		      x04-x04 */
	u8	xTargetLp;		/* Target LP		      x05-x05 */
	u8	xSizeMinus1;		/* Size of Derived class - 1  x06-x06 */
	u8	xRc;			/* RC for Ack flows	      x07-x07 */
	u16	xSourceInstanceId;	/* Source sides instance id   x08-x09 */
	u16	xTargetInstanceId;	/* Target sides instance id   x0A-x0B */
	union {
		u32	xSubtypeData;	/* Data usable by the subtype x0C-x0F */
		u16	xSubtypeDataShort[2];	/* Data as 2 shorts */
		u8	xSubtypeDataChar[4];	/* Data as 4 chars */
	} x;

	u64	xCorrelationToken;	/* Unique value for source/type x10-x17 */
};

typedef void (*LpEventHandler)(struct HvLpEvent *);

/* Register a handler for an event type - returns 0 on success */
extern int HvLpEvent_registerHandler(HvLpEvent_Type eventType,
		LpEventHandler hdlr);

/*
 * Unregister a handler for an event type
 *
 * This call will sleep until the handler being removed is guaranteed to
 * be no longer executing on any CPU. Do not call with locks held.
 *
 *  returns 0 on success
 *  Unregister will fail if there are any paths open for the type
 */
extern int HvLpEvent_unregisterHandler(HvLpEvent_Type eventType);

/*
 * Open an Lp Event Path for an event type
 * returns 0 on success
 * openPath will fail if there is no handler registered for the event type.
 * The lpIndex specified is the partition index for the target partition
 * (for VirtualIo, VirtualLan and SessionMgr) other types specify zero)
 */
extern int HvLpEvent_openPath(HvLpEvent_Type eventType, HvLpIndex lpIndex);

/*
 * Close an Lp Event Path for a type and partition
 * returns 0 on success
 */
extern int HvLpEvent_closePath(HvLpEvent_Type eventType, HvLpIndex lpIndex);

#define HvLpEvent_Type_Hypervisor 0
#define HvLpEvent_Type_MachineFac 1
#define HvLpEvent_Type_SessionMgr 2
#define HvLpEvent_Type_SpdIo      3
#define HvLpEvent_Type_VirtualBus 4
#define HvLpEvent_Type_PciIo      5
#define HvLpEvent_Type_RioIo      6
#define HvLpEvent_Type_VirtualLan 7
#define HvLpEvent_Type_VirtualIo  8
#define HvLpEvent_Type_NumTypes   9

#define HvLpEvent_Rc_Good 0
#define HvLpEvent_Rc_BufferNotAvailable 1
#define HvLpEvent_Rc_Cancelled 2
#define HvLpEvent_Rc_GenericError 3
#define HvLpEvent_Rc_InvalidAddress 4
#define HvLpEvent_Rc_InvalidPartition 5
#define HvLpEvent_Rc_InvalidSize 6
#define HvLpEvent_Rc_InvalidSubtype 7
#define HvLpEvent_Rc_InvalidSubtypeData 8
#define HvLpEvent_Rc_InvalidType 9
#define HvLpEvent_Rc_PartitionDead 10
#define HvLpEvent_Rc_PathClosed 11
#define HvLpEvent_Rc_SubtypeError 12

#define HvLpEvent_Function_Ack 0
#define HvLpEvent_Function_Int 1

#define HvLpEvent_AckInd_NoAck 0
#define HvLpEvent_AckInd_DoAck 1

#define HvLpEvent_AckType_ImmediateAck 0
#define HvLpEvent_AckType_DeferredAck 1

#define HV_LP_EVENT_INT			0x01
#define HV_LP_EVENT_DO_ACK		0x02
#define HV_LP_EVENT_DEFERRED_ACK	0x04
#define HV_LP_EVENT_VALID		0x80

#define HvLpDma_Direction_LocalToRemote 0
#define HvLpDma_Direction_RemoteToLocal 1

#define HvLpDma_AddressType_TceIndex 0
#define HvLpDma_AddressType_RealAddress 1

#define HvLpDma_Rc_Good 0
#define HvLpDma_Rc_Error 1
#define HvLpDma_Rc_PartitionDead 2
#define HvLpDma_Rc_PathClosed 3
#define HvLpDma_Rc_InvalidAddress 4
#define HvLpDma_Rc_InvalidLength 5

static inline int hvlpevent_is_valid(struct HvLpEvent *h)
{
	return h->flags & HV_LP_EVENT_VALID;
}

static inline void hvlpevent_invalidate(struct HvLpEvent *h)
{
	h->flags &= ~ HV_LP_EVENT_VALID;
}

static inline int hvlpevent_is_int(struct HvLpEvent *h)
{
	return h->flags & HV_LP_EVENT_INT;
}

static inline int hvlpevent_is_ack(struct HvLpEvent *h)
{
	return !hvlpevent_is_int(h);
}

static inline int hvlpevent_need_ack(struct HvLpEvent *h)
{
	return h->flags & HV_LP_EVENT_DO_ACK;
}

#endif /* _ASM_POWERPC_ISERIES_HV_LP_EVENT_H */
