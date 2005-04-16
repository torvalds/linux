/*
 * IoHriMainStore.h
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

#ifndef _IOHRIMAINSTORE_H
#define _IOHRIMAINSTORE_H

/* Main Store Vpd for Condor,iStar,sStar */
struct IoHriMainStoreSegment4 {    
	u8	msArea0Exists:1;
	u8	msArea1Exists:1;
	u8	msArea2Exists:1;
	u8	msArea3Exists:1;
	u8	reserved1:4;
	u8	reserved2;

	u8	msArea0Functional:1;
	u8	msArea1Functional:1;
	u8	msArea2Functional:1;
	u8	msArea3Functional:1;
	u8	reserved3:4;
	u8	reserved4;

	u32	totalMainStore;

	u64	msArea0Ptr;
	u64	msArea1Ptr;
	u64	msArea2Ptr;
	u64	msArea3Ptr;

	u32	cardProductionLevel;

	u32	msAdrHole;

	u8	msArea0HasRiserVpd:1;
	u8	msArea1HasRiserVpd:1;
	u8	msArea2HasRiserVpd:1;
	u8	msArea3HasRiserVpd:1;
	u8	reserved5:4;	
	u8	reserved6;
	u16	reserved7;

	u8	reserved8[28];

	u64	nonInterleavedBlocksStartAdr;
	u64	nonInterleavedBlocksEndAdr;
};

/* Main Store VPD for Power4 */
struct IoHriMainStoreChipInfo1 {
	u32	chipMfgID	__attribute((packed));
	char	chipECLevel[4]	__attribute((packed));
};

struct IoHriMainStoreVpdIdData {
	char	typeNumber[4];
	char	modelNumber[4];
	char	partNumber[12];
	char	serialNumber[12];
};

struct IoHriMainStoreVpdFruData {
	char	fruLabel[8]	__attribute((packed));
	u8	numberOfSlots	__attribute((packed));
	u8	pluggingType	__attribute((packed));
	u16	slotMapIndex	__attribute((packed));
};

struct IoHriMainStoreAdrRangeBlock {
	void *	blockStart      __attribute((packed));
	void *	blockEnd        __attribute((packed));
	u32	blockProcChipId __attribute((packed));
};

#define MaxAreaAdrRangeBlocks 4

struct IoHriMainStoreArea4 {
	u32	msVpdFormat			__attribute((packed));
	u8	containedVpdType		__attribute((packed));
	u8	reserved1			__attribute((packed));
	u16	reserved2			__attribute((packed));

	u64	msExists			__attribute((packed));
	u64	msFunctional			__attribute((packed));

	u32	memorySize			__attribute((packed));
	u32	procNodeId			__attribute((packed));

	u32	numAdrRangeBlocks		__attribute((packed));
	struct IoHriMainStoreAdrRangeBlock xAdrRangeBlock[MaxAreaAdrRangeBlocks] __attribute((packed));

	struct IoHriMainStoreChipInfo1	chipInfo0	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo1	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo2	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo3	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo4	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo5	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo6	__attribute((packed));
	struct IoHriMainStoreChipInfo1	chipInfo7	__attribute((packed));

	void *   msRamAreaArray			__attribute((packed));
	u32	msRamAreaArrayNumEntries	__attribute((packed));
	u32	msRamAreaArrayEntrySize		__attribute((packed));

	u32	numaDimmExists			__attribute((packed));
	u32	numaDimmFunctional		__attribute((packed));
	void *	numaDimmArray			__attribute((packed));
	u32	numaDimmArrayNumEntries		__attribute((packed));
	u32	numaDimmArrayEntrySize		__attribute((packed));

	struct IoHriMainStoreVpdIdData  idData	__attribute((packed));

	u64	powerData			__attribute((packed));
	u64	cardAssemblyPartNum		__attribute((packed));
	u64	chipSerialNum			__attribute((packed));

	u64	reserved3			__attribute((packed));
	char	reserved4[16]			__attribute((packed));

	struct IoHriMainStoreVpdFruData fruData	__attribute((packed));

	u8	vpdPortNum			__attribute((packed));
	u8	reserved5			__attribute((packed));
	u8	frameId				__attribute((packed));
	u8	rackUnit			__attribute((packed));
	char	asciiKeywordVpd[256]		__attribute((packed));
	u32	reserved6			__attribute((packed));
};


struct IoHriMainStoreSegment5 {    
	u16	reserved1;
	u8	reserved2;
	u8	msVpdFormat;

	u32	totalMainStore;
	u64	maxConfiguredMsAdr;

	struct IoHriMainStoreArea4*	msAreaArray;
	u32	msAreaArrayNumEntries;
	u32	msAreaArrayEntrySize;

	u32	msAreaExists;    
	u32	msAreaFunctional;

	u64	reserved3;
};



#endif // _IOHRIMAINSTORE_H

