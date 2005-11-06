/*
 * HvCallEvent.h
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
/*
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from the OS.
 */
#ifndef _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H
#define _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H

#include <asm/iseries/hv_call_sc.h>
#include <asm/iseries/hv_types.h>
#include <asm/abs_addr.h>

struct HvLpEvent;

typedef u8 HvLpEvent_Type;
typedef u8 HvLpEvent_AckInd;
typedef u8 HvLpEvent_AckType;

struct	HvCallEvent_PackedParms {
	u8		xAckType:1;
	u8		xAckInd:1;
	u8		xRsvd:1;
	u8		xTargetLp:5;
	u8		xType;
	u16		xSubtype;
	HvLpInstanceId	xSourceInstId;
	HvLpInstanceId	xTargetInstId;
};

typedef u8 HvLpDma_Direction;
typedef u8 HvLpDma_AddressType;

struct	HvCallEvent_PackedDmaParms {
	u8		xDirection:1;
	u8		xLocalAddrType:1;
	u8		xRemoteAddrType:1;
	u8		xRsvd1:5;
	HvLpIndex	xRemoteLp;
	u8		xType;
	u8		xRsvd2;
	HvLpInstanceId	xLocalInstId;
	HvLpInstanceId	xRemoteInstId;
};

typedef u64 HvLpEvent_Rc;
typedef u64 HvLpDma_Rc;

#define HvCallEventAckLpEvent				HvCallEvent +  0
#define HvCallEventCancelLpEvent			HvCallEvent +  1
#define HvCallEventCloseLpEventPath			HvCallEvent +  2
#define HvCallEventDmaBufList				HvCallEvent +  3
#define HvCallEventDmaSingle				HvCallEvent +  4
#define HvCallEventDmaToSp				HvCallEvent +  5
#define HvCallEventGetOverflowLpEvents			HvCallEvent +  6
#define HvCallEventGetSourceLpInstanceId		HvCallEvent +  7
#define HvCallEventGetTargetLpInstanceId		HvCallEvent +  8
#define HvCallEventOpenLpEventPath			HvCallEvent +  9
#define HvCallEventSetLpEventStack			HvCallEvent + 10
#define HvCallEventSignalLpEvent			HvCallEvent + 11
#define HvCallEventSignalLpEventParms			HvCallEvent + 12
#define HvCallEventSetInterLpQueueIndex			HvCallEvent + 13
#define HvCallEventSetLpEventQueueInterruptProc		HvCallEvent + 14
#define HvCallEventRouter15				HvCallEvent + 15

static inline void HvCallEvent_getOverflowLpEvents(u8 queueIndex)
{
	HvCall1(HvCallEventGetOverflowLpEvents, queueIndex);
}

static inline void HvCallEvent_setInterLpQueueIndex(u8 queueIndex)
{
	HvCall1(HvCallEventSetInterLpQueueIndex, queueIndex);
}

static inline void HvCallEvent_setLpEventStack(u8 queueIndex,
		char *eventStackAddr, u32 eventStackSize)
{
	u64 abs_addr;

	abs_addr = virt_to_abs(eventStackAddr);
	HvCall3(HvCallEventSetLpEventStack, queueIndex, abs_addr,
			eventStackSize);
}

static inline void HvCallEvent_setLpEventQueueInterruptProc(u8 queueIndex,
		u16 lpLogicalProcIndex)
{
	HvCall2(HvCallEventSetLpEventQueueInterruptProc, queueIndex,
			lpLogicalProcIndex);
}

static inline HvLpEvent_Rc HvCallEvent_signalLpEvent(struct HvLpEvent *event)
{
	u64 abs_addr;

#ifdef DEBUG_SENDEVENT
	printk("HvCallEvent_signalLpEvent: *event = %016lx\n ",
			(unsigned long)event);
#endif
	abs_addr = virt_to_abs(event);
	return HvCall1(HvCallEventSignalLpEvent, abs_addr);
}

static inline HvLpEvent_Rc HvCallEvent_signalLpEventFast(HvLpIndex targetLp,
		HvLpEvent_Type type, u16 subtype, HvLpEvent_AckInd ackInd,
		HvLpEvent_AckType ackType, HvLpInstanceId sourceInstanceId,
		HvLpInstanceId targetInstanceId, u64 correlationToken,
		u64 eventData1, u64 eventData2, u64 eventData3,
		u64 eventData4, u64 eventData5)
{
	/* Pack the misc bits into a single Dword to pass to PLIC */
	union {
		struct HvCallEvent_PackedParms	parms;
		u64		dword;
	} packed;
	packed.parms.xAckType	= ackType;
	packed.parms.xAckInd	= ackInd;
	packed.parms.xRsvd	= 0;
	packed.parms.xTargetLp	= targetLp;
	packed.parms.xType	= type;
	packed.parms.xSubtype	= subtype;
	packed.parms.xSourceInstId	= sourceInstanceId;
	packed.parms.xTargetInstId	= targetInstanceId;

	return HvCall7(HvCallEventSignalLpEventParms, packed.dword,
			correlationToken, eventData1, eventData2,
			eventData3, eventData4, eventData5);
}

static inline HvLpEvent_Rc HvCallEvent_ackLpEvent(struct HvLpEvent *event)
{
	u64 abs_addr;

	abs_addr = virt_to_abs(event);
	return HvCall1(HvCallEventAckLpEvent, abs_addr);
}

static inline HvLpEvent_Rc HvCallEvent_cancelLpEvent(struct HvLpEvent *event)
{
	u64 abs_addr;

	abs_addr = virt_to_abs(event);
	return HvCall1(HvCallEventCancelLpEvent, abs_addr);
}

static inline HvLpInstanceId HvCallEvent_getSourceLpInstanceId(
		HvLpIndex targetLp, HvLpEvent_Type type)
{
	return HvCall2(HvCallEventGetSourceLpInstanceId, targetLp, type);
}

static inline HvLpInstanceId HvCallEvent_getTargetLpInstanceId(
		HvLpIndex targetLp, HvLpEvent_Type type)
{
	return HvCall2(HvCallEventGetTargetLpInstanceId, targetLp, type);
}

static inline void HvCallEvent_openLpEventPath(HvLpIndex targetLp,
		HvLpEvent_Type type)
{
	HvCall2(HvCallEventOpenLpEventPath, targetLp, type);
}

static inline void HvCallEvent_closeLpEventPath(HvLpIndex targetLp,
		HvLpEvent_Type type)
{
	HvCall2(HvCallEventCloseLpEventPath, targetLp, type);
}

static inline HvLpDma_Rc HvCallEvent_dmaBufList(HvLpEvent_Type type,
		HvLpIndex remoteLp, HvLpDma_Direction direction,
		HvLpInstanceId localInstanceId,
		HvLpInstanceId remoteInstanceId,
		HvLpDma_AddressType localAddressType,
		HvLpDma_AddressType remoteAddressType,
		/* Do these need to be converted to absolute addresses? */
		u64 localBufList, u64 remoteBufList, u32 transferLength)
{
	/* Pack the misc bits into a single Dword to pass to PLIC */
	union {
		struct HvCallEvent_PackedDmaParms	parms;
		u64		dword;
	} packed;

	packed.parms.xDirection		= direction;
	packed.parms.xLocalAddrType	= localAddressType;
	packed.parms.xRemoteAddrType	= remoteAddressType;
	packed.parms.xRsvd1		= 0;
	packed.parms.xRemoteLp		= remoteLp;
	packed.parms.xType		= type;
	packed.parms.xRsvd2		= 0;
	packed.parms.xLocalInstId	= localInstanceId;
	packed.parms.xRemoteInstId	= remoteInstanceId;

	return HvCall4(HvCallEventDmaBufList, packed.dword, localBufList,
			remoteBufList, transferLength);
}

static inline HvLpDma_Rc HvCallEvent_dmaSingle(HvLpEvent_Type type,
		HvLpIndex remoteLp, HvLpDma_Direction direction,
		HvLpInstanceId localInstanceId,
		HvLpInstanceId remoteInstanceId,
		HvLpDma_AddressType localAddressType,
		HvLpDma_AddressType remoteAddressType,
		u64 localAddrOrTce, u64 remoteAddrOrTce, u32 transferLength)
{
	/* Pack the misc bits into a single Dword to pass to PLIC */
	union {
		struct HvCallEvent_PackedDmaParms	parms;
		u64		dword;
	} packed;

	packed.parms.xDirection		= direction;
	packed.parms.xLocalAddrType	= localAddressType;
	packed.parms.xRemoteAddrType	= remoteAddressType;
	packed.parms.xRsvd1		= 0;
	packed.parms.xRemoteLp		= remoteLp;
	packed.parms.xType		= type;
	packed.parms.xRsvd2		= 0;
	packed.parms.xLocalInstId	= localInstanceId;
	packed.parms.xRemoteInstId	= remoteInstanceId;

	return (HvLpDma_Rc)HvCall4(HvCallEventDmaSingle, packed.dword,
			localAddrOrTce, remoteAddrOrTce, transferLength);
}

static inline HvLpDma_Rc HvCallEvent_dmaToSp(void *local, u32 remote,
		u32 length, HvLpDma_Direction dir)
{
	u64 abs_addr;

	abs_addr = virt_to_abs(local);
	return HvCall4(HvCallEventDmaToSp, abs_addr, remote, length, dir);
}

#endif /* _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H */
