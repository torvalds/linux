/*
 * ItLpRegSave.h
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
#ifndef _ITLPREGSAVE_H
#define _ITLPREGSAVE_H

//=====================================================================================
//
//	This control block contains the data that is shared between PLIC
//	and the OS
//    
//

struct ItLpRegSave
{
	u32	xDesc;		// Eye catcher  "LpRS" ebcdic	000-003
	u16	xSize;		// Size of this class		004-005
	u8	xInUse;         // Area is live                 006-007
	u8	xRsvd1[9]; 	// Reserved			007-00F

	u8      xFixedRegSave[352]; // Fixed Register Save Area 010-16F 
	u32	xCTRL;		// Control Register		170-173
	u32	xDEC;		// Decrementer			174-177    
	u32	xFPSCR;		// FP Status and Control Reg	178-17B
	u32	xPVR;		// Processor Version Number	17C-17F
    
	u64	xMMCR0;		// Monitor Mode Control Reg 0	180-187
	u32	xPMC1;		// Perf Monitor Counter 1	188-18B
	u32	xPMC2;		// Perf Monitor Counter 2	18C-18F
	u32	xPMC3;		// Perf Monitor Counter 3	190-193
	u32	xPMC4;		// Perf Monitor Counter 4	194-197
	u32	xPIR;		// Processor ID Reg		198-19B
    
	u32	xMMCR1;		// Monitor Mode Control Reg 1	19C-19F
	u32	xMMCRA;		// Monitor Mode Control Reg A	1A0-1A3
	u32	xPMC5;		// Perf Monitor Counter 5	1A4-1A7
	u32	xPMC6;		// Perf Monitor Counter 6	1A8-1AB
	u32	xPMC7;		// Perf Monitor Counter 7	1AC-1AF
	u32	xPMC8;		// Perf Monitor Counter 8	1B0-1B3
	u32	xTSC;		// Thread Switch Control	1B4-1B7
	u32	xTST;		// Thread Switch Timeout	1B8-1BB
	u32	xRsvd;          // Reserved                     1BC-1BF

	u64	xACCR;		// Address Compare Control Reg	1C0-1C7
	u64	xIMR;		// Instruction Match Register	1C8-1CF    
	u64	xSDR1;		// Storage Description Reg 1	1D0-1D7    
	u64	xSPRG0;		// Special Purpose Reg General0	1D8-1DF
	u64	xSPRG1;		// Special Purpose Reg General1	1E0-1E7
	u64	xSPRG2;		// Special Purpose Reg General2	1E8-1EF
	u64	xSPRG3;		// Special Purpose Reg General3	1F0-1F7
	u64	xTB;		// Time Base Register		1F8-1FF
   
	u64	xFPR[32];	// Floating Point Registers	200-2FF

	u64	xMSR;		// Machine State Register  	300-307
	u64	xNIA;		// Next Instruction Address	308-30F

	u64	xDABR;		// Data Address Breakpoint Reg	310-317
	u64	xIABR;		// Inst Address Breakpoint Reg	318-31F

	u64	xHID0;		// HW Implementation Dependent0	320-327

	u64	xHID4;		// HW Implementation Dependent4	328-32F
	u64	xSCOMd;		// SCON Data Reg (SPRG4)       	330-337
	u64	xSCOMc;		// SCON Command Reg (SPRG5)    	338-33F
	u64	xSDAR;		// Sample Data Address Register	340-347
	u64	xSIAR;		// Sample Inst Address Register	348-34F

	u8	xRsvd3[176];	// Reserved			350-3FF
};

#endif /* _ITLPREGSAVE_H */
