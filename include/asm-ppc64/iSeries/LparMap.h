/*
 * LparMap.h
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
#ifndef _LPARMAP_H
#define _LPARMAP_H

#include <asm/types.h>

/* The iSeries hypervisor will set up mapping for one or more 
 * ESID/VSID pairs (in SLB/segment registers) and will set up
 * mappings of one or more ranges of pages to VAs.
 * We will have the hypervisor set up the ESID->VSID mapping
 * for the four kernel segments (C-F).  With shared processors,
 * the hypervisor will clear all segment registers and reload
 * these four whenever the processor is switched from one 
 * partition to another.
 */

/* The Vsid and Esid identified below will be used by the hypervisor
 * to set up a memory mapping for part of the load area before giving
 * control to the Linux kernel.  The load area is 64 MB, but this must
 * not attempt to map the whole load area.  The Hashed Page Table may
 * need to be located within the load area (if the total partition size
 * is 64 MB), but cannot be mapped.  Typically, this should specify
 * to map half (32 MB) of the load area.
 * 
 * The hypervisor will set up page table entries for the number of 
 * pages specified.
 *
 * In 32-bit mode, the hypervisor will load all four of the
 * segment registers (identified by the low-order four bits of the 
 * Esid field.  In 64-bit mode, the hypervisor will load one SLB
 * entry to map the Esid to the Vsid.
*/

// Hypervisor initially maps 32MB of the load area 
#define HvPagesToMap 8192

struct LparMap
{
	u64	  xNumberEsids;		// Number of ESID/VSID pairs (1)
	u64	  xNumberRanges;	// Number of VA ranges to map (1)
	u64	  xSegmentTableOffs;	// Page number within load area of seg table (0)
	u64	  xRsvd[5];		// Reserved (0)
	u64	  xKernelEsid;  	// Esid used to map kernel load (0x0C00000000)	
	u64	  xKernelVsid;		// Vsid used to map kernel load (0x0C00000000)
	u64	  xPages;		// Number of pages to be mapped	(8192)
	u64	  xOffset;		// Offset from start of load area (0)
	u64	  xVPN;			// Virtual Page Number (0x000C000000000000)
};

#endif /* _LPARMAP_H */
