/*
 * IoHriProcessorVpd.h
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
#ifndef _IOHRIPROCESSORVPD_H
#define _IOHRIPROCESSORVPD_H

#include <asm/types.h>

/*
 * This struct maps Processor Vpd that is DMAd to SLIC by CSP
 */
struct IoHriProcessorVpd {
	u8	xFormat;		// VPD format indicator		x00-x00
	u8	xProcStatus:8;		// Processor State		x01-x01
	u8	xSecondaryThreadCount;	// Secondary thread cnt		x02-x02
	u8	xSrcType:1;		// Src Type			x03-x03
	u8	xSrcSoft:1;		// Src stay soft		...
	u8	xSrcParable:1;		// Src parable			...
	u8	xRsvd1:5;		// Reserved			...
	u16	xHvPhysicalProcIndex;	// Hypervisor physical proc index04-x05
	u16	xRsvd2;			// Reserved			x06-x07
	u32	xHwNodeId;		// Hardware node id		x08-x0B
	u32	xHwProcId;		// Hardware processor id	x0C-x0F

	u32	xTypeNum;		// Card Type/CCIN number	x10-x13
	u32	xModelNum;		// Model/Feature number		x14-x17
	u64	xSerialNum;		// Serial number		x18-x1F
	char	xPartNum[12];		// Book Part or FPU number	x20-x2B
	char	xMfgID[4];		// Manufacturing ID		x2C-x2F

	u32	xProcFreq;		// Processor Frequency		x30-x33
	u32	xTimeBaseFreq;		// Time Base Frequency		x34-x37

	u32	xChipEcLevel;		// Chip EC Levels		x38-x3B
	u32	xProcIdReg;		// PIR SPR value		x3C-x3F
	u32	xPVR;			// PVR value			x40-x43
	u8	xRsvd3[12];		// Reserved			x44-x4F

	u32	xInstCacheSize;		// Instruction cache size in KB	x50-x53
	u32	xInstBlockSize;		// Instruction cache block size	x54-x57
	u32	xDataCacheOperandSize;	// Data cache operand size	x58-x5B
	u32	xInstCacheOperandSize;	// Inst cache operand size	x5C-x5F

	u32	xDataL1CacheSizeKB;	// L1 data cache size in KB	x60-x63
	u32	xDataL1CacheLineSize;	// L1 data cache block size	x64-x67
	u64	xRsvd4;			// Reserved			x68-x6F

	u32	xDataL2CacheSizeKB;	// L2 data cache size in KB	x70-x73
	u32	xDataL2CacheLineSize;	// L2 data cache block size	x74-x77
	u64	xRsvd5;			// Reserved			x78-x7F

	u32	xDataL3CacheSizeKB;	// L3 data cache size in KB	x80-x83
	u32	xDataL3CacheLineSize;	// L3 data cache block size	x84-x87
	u64	xRsvd6;			// Reserved			x88-x8F

	u64	xFruLabel;		// Card Location Label		x90-x97
	u8	xSlotsOnCard;		// Slots on card (0=no slots)	x98-x98
	u8	xPartLocFlag;		// Location flag (0-pluggable 1-imbedded) x99-x99
	u16	xSlotMapIndex;		// Index in slot map table	x9A-x9B
	u8	xSmartCardPortNo;	// Smart card port number	x9C-x9C
	u8	xRsvd7;			// Reserved			x9D-x9D
	u16	xFrameIdAndRackUnit;	// Frame ID and rack unit adr	x9E-x9F

	u8	xRsvd8[24];		// Reserved			xA0-xB7

	char	xProcSrc[72];		// CSP format SRC		xB8-xFF
};

extern struct IoHriProcessorVpd	xIoHriProcessorVpd[];

#endif /* _IOHRIPROCESSORVPD_H */
