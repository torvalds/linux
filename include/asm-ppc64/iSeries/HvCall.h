/*
 * HvCall.h
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

//===========================================================================
//
//	This file contains the "hypervisor call" interface which is used to
//	drive the hypervisor from the OS.
//
//===========================================================================
#ifndef _HVCALL_H
#define _HVCALL_H

//-------------------------------------------------------------------
// Standard Includes
//-------------------------------------------------------------------
#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/paca.h>

/*
enum HvCall_ReturnCode
{
	HvCall_Good		= 0,
	HvCall_Partial		= 1,
	HvCall_NotOwned		= 2,
	HvCall_NotFreed		= 3,
	HvCall_UnspecifiedError	= 4
};

enum HvCall_TypeOfSIT
{
	HvCall_ReduceOnly	= 0,
	HvCall_Unconditional	= 1
};

enum HvCall_TypeOfYield
{
	HvCall_YieldTimed	= 0,	// Yield until specified time
	HvCall_YieldToActive	= 1,	// Yield until all active procs have run
	HvCall_YieldToProc	= 2	// Yield until the specified processor has run
};

enum HvCall_InterruptMasks
{
	HvCall_MaskIPI		= 0x00000001,
	HvCall_MaskLpEvent	= 0x00000002,
	HvCall_MaskLpProd	= 0x00000004,
	HvCall_MaskTimeout	= 0x00000008
};

enum HvCall_VaryOffChunkRc
{
	HvCall_VaryOffSucceeded		= 0,
	HvCall_VaryOffWithdrawn		= 1,
	HvCall_ChunkInLoadArea		= 2,
	HvCall_ChunkInHPT		= 3,
	HvCall_ChunkNotAccessible	= 4,
	HvCall_ChunkInUse		= 5
};
*/

/* Type of yield for HvCallBaseYieldProcessor */
#define HvCall_YieldTimed 	0	// Yield until specified time (tb)
#define HvCall_YieldToActive	1	// Yield until all active procs have run
#define HvCall_YieldToProc	2	// Yield until the specified processor has run

/* interrupt masks for setEnabledInterrupts */
#define HvCall_MaskIPI		0x00000001
#define HvCall_MaskLpEvent	0x00000002
#define HvCall_MaskLpProd	0x00000004
#define HvCall_MaskTimeout	0x00000008

/* Log buffer formats                       */
#define HvCall_LogBuffer_ASCII          0
#define HvCall_LogBuffer_EBCDIC         1

#define HvCallBaseAckDeferredInts			HvCallBase +  0
#define HvCallBaseCpmPowerOff				HvCallBase +  1
#define HvCallBaseGetHwPatch				HvCallBase +  2
#define HvCallBaseReIplSpAttn				HvCallBase +  3
#define HvCallBaseSetASR				HvCallBase +  4
#define HvCallBaseSetASRAndRfi				HvCallBase +  5 
#define HvCallBaseSetIMR				HvCallBase +  6
#define HvCallBaseSendIPI				HvCallBase +  7
#define HvCallBaseTerminateMachine			HvCallBase +  8
#define HvCallBaseTerminateMachineSrc			HvCallBase +  9
#define HvCallBaseProcessPlicInterrupts			HvCallBase + 10
#define HvCallBaseIsPrimaryCpmOrMsdIpl			HvCallBase + 11
#define HvCallBaseSetVirtualSIT				HvCallBase + 12
#define HvCallBaseVaryOffThisProcessor			HvCallBase + 13
#define HvCallBaseVaryOffMemoryChunk			HvCallBase + 14
#define HvCallBaseVaryOffInteractivePercentage		HvCallBase + 15
#define HvCallBaseSendLpProd				HvCallBase + 16
#define HvCallBaseSetEnabledInterrupts			HvCallBase + 17
#define HvCallBaseYieldProcessor			HvCallBase + 18
#define HvCallBaseVaryOffSharedProcUnits		HvCallBase + 19
#define HvCallBaseSetVirtualDecr			HvCallBase + 20
#define HvCallBaseClearLogBuffer			HvCallBase + 21
#define HvCallBaseGetLogBufferCodePage			HvCallBase + 22
#define HvCallBaseGetLogBufferFormat			HvCallBase + 23
#define HvCallBaseGetLogBufferLength			HvCallBase + 24
#define HvCallBaseReadLogBuffer 			HvCallBase + 25
#define HvCallBaseSetLogBufferFormatAndCodePage		HvCallBase + 26
#define HvCallBaseWriteLogBuffer               		HvCallBase + 27
#define HvCallBaseRouter28				HvCallBase + 28
#define HvCallBaseRouter29				HvCallBase + 29
#define HvCallBaseRouter30				HvCallBase + 30
#define HvCallBaseSetDebugBus 				HvCallBase + 31

#define HvCallCcSetDABR  				HvCallCc + 7

//=====================================================================================
static inline void		HvCall_setVirtualDecr(void)
{
	/* Ignore any error return codes - most likely means that the target value for the
	 * LP has been increased and this vary off would bring us below the new target. */
	HvCall0(HvCallBaseSetVirtualDecr);
}
//=====================================================================
static inline void		HvCall_yieldProcessor(unsigned typeOfYield, u64 yieldParm)
{
	HvCall2( HvCallBaseYieldProcessor, typeOfYield, yieldParm );
}
//=====================================================================
static inline void		HvCall_setEnabledInterrupts(u64 enabledInterrupts)
{
	HvCall1(HvCallBaseSetEnabledInterrupts,enabledInterrupts);
}

//=====================================================================
static inline void		HvCall_clearLogBuffer(HvLpIndex lpindex)
{
	HvCall1(HvCallBaseClearLogBuffer,lpindex);
}

//=====================================================================
static inline u32  		HvCall_getLogBufferCodePage(HvLpIndex lpindex)
{
	u32 retVal = HvCall1(HvCallBaseGetLogBufferCodePage,lpindex);
	return retVal;
}

//=====================================================================
static inline int  		HvCall_getLogBufferFormat(HvLpIndex lpindex)
{
	int retVal = HvCall1(HvCallBaseGetLogBufferFormat,lpindex);
	return retVal;
}

//=====================================================================
static inline u32  		HvCall_getLogBufferLength(HvLpIndex lpindex)
{
	u32 retVal = HvCall1(HvCallBaseGetLogBufferLength,lpindex);
	return retVal;
}

//=====================================================================
static inline void  		HvCall_setLogBufferFormatAndCodepage(int format, u32 codePage)
{
	HvCall2(HvCallBaseSetLogBufferFormatAndCodePage,format, codePage);
}

//=====================================================================
int HvCall_readLogBuffer(HvLpIndex lpindex, void *buffer, u64 bufLen);
void HvCall_writeLogBuffer(const void *buffer, u64 bufLen);

//=====================================================================
static inline void		HvCall_sendIPI(struct paca_struct * targetPaca)
{
	HvCall1( HvCallBaseSendIPI, targetPaca->paca_index );
}

//=====================================================================
static inline void		HvCall_terminateMachineSrc(void)
{
	HvCall0( HvCallBaseTerminateMachineSrc );
}

static inline void HvCall_setDABR(unsigned long val)
{
	HvCall1(HvCallCcSetDABR, val);
}

static inline void HvCall_setDebugBus(unsigned long val)
{
	HvCall1(HvCallBaseSetDebugBus, val);
}

#endif /* _HVCALL_H */
