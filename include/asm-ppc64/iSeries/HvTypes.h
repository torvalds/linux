/*
 * HvTypes.h
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
#ifndef _HVTYPES_H
#define _HVTYPES_H

//===========================================================================
//                                                             Header File Id
// Name______________:  HvTypes.H
//
// Description_______:
//
//	General typedefs for the hypervisor.
//
// Declared Class(es):
//
//===========================================================================

#include <asm/types.h>

//-------------------------------------------------------------------
// Typedefs
//-------------------------------------------------------------------
typedef u8	HvLpIndex;
typedef u16	HvLpInstanceId;
typedef u64     HvLpTOD;
typedef u64     HvLpSystemSerialNum;
typedef u8      HvLpDeviceSerialNum[12];
typedef u16     HvLpSanHwSet;
typedef u16     HvLpBus;
typedef u16     HvLpBoard;
typedef u16     HvLpCard;
typedef u8      HvLpDeviceType[4];
typedef u8      HvLpDeviceModel[3];
typedef u64     HvIoToken;
typedef u8      HvLpName[8];
typedef u32	HvIoId;
typedef u64	HvRealMemoryIndex;
typedef u32     HvLpIndexMap;		// Must hold HvMaxArchitectedLps bits!!!
typedef u16	HvLpVrmIndex;
typedef u32	HvXmGenerationId;
typedef u8	HvLpBusPool;			
typedef u8	HvLpSharedPoolIndex;		
typedef u16	HvLpSharedProcUnitsX100;
typedef u8	HvLpVirtualLanIndex;
typedef u16	HvLpVirtualLanIndexMap;	// Must hold HvMaxArchitectedVirtualLans bits!!!
typedef u16	HvBusNumber;		// Hypervisor Bus Number
typedef u8	HvSubBusNumber;		// Hypervisor SubBus Number
typedef u8	HvAgentId;		// Hypervisor DevFn


#define HVMAXARCHITECTEDLPS 32
#define HVMAXARCHITECTEDVIRTUALLANS 16
#define HVMAXARCHITECTEDVIRTUALDISKS 32
#define HVMAXARCHITECTEDVIRTUALCDROMS 8
#define HVMAXARCHITECTEDVIRTUALTAPES 8
#define HVCHUNKSIZE 256 * 1024
#define HVPAGESIZE 4 * 1024
#define HVLPMINMEGSPRIMARY 256
#define HVLPMINMEGSSECONDARY 64
#define HVCHUNKSPERMEG 4
#define HVPAGESPERMEG 256
#define HVPAGESPERCHUNK 64
 
#define HvMaxArchitectedLps 		((HvLpIndex)HVMAXARCHITECTEDLPS)
#define HvMaxArchitectedVirtualLans	((HvLpVirtualLanIndex)16)
#define HvLpIndexInvalid		((HvLpIndex)0xff)

//--------------------------------------------------------------------
// Enums for the sub-components under PLIC
// Used in HvCall  and HvPrimaryCall
//--------------------------------------------------------------------
enum   HvCallCompIds
{
	HvCallCompId = 0,
	HvCallCpuCtlsCompId = 1,
	HvCallCfgCompId = 2,
	HvCallEventCompId = 3,
	HvCallHptCompId = 4,
	HvCallPciCompId = 5,
	HvCallSlmCompId = 6,
	HvCallSmCompId = 7,
	HvCallSpdCompId = 8,
	HvCallXmCompId = 9,
	HvCallRioCompId = 10, 
	HvCallRsvd3CompId = 11,
	HvCallRsvd2CompId = 12,
	HvCallRsvd1CompId = 13,
	HvCallMaxCompId = 14,
	HvPrimaryCallCompId = 0,    
	HvPrimaryCallCfgCompId = 1,
	HvPrimaryCallPciCompId = 2,    
	HvPrimaryCallSmCompId = 3,
	HvPrimaryCallSpdCompId = 4,
	HvPrimaryCallXmCompId = 5,
	HvPrimaryCallRioCompId = 6, 
	HvPrimaryCallRsvd7CompId = 7,
	HvPrimaryCallRsvd6CompId = 8,
	HvPrimaryCallRsvd5CompId = 9,
	HvPrimaryCallRsvd4CompId = 10,
	HvPrimaryCallRsvd3CompId = 11,
	HvPrimaryCallRsvd2CompId = 12,
	HvPrimaryCallRsvd1CompId = 13,
	HvPrimaryCallMaxCompId = HvCallMaxCompId     
};

struct HvLpBufferList {
	u64 addr;
	u64 len;
};

#endif /* _HVTYPES_H */
