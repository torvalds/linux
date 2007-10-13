/*
 * File:         include/asm-blackfin/cplb.h
 * Based on:     include/asm-blackfin/mach-bf537/bf537.h
 * Author:       Robin Getz <rgetz@blackfin.uclinux.org>
 *
 * Created:      2000
 * Description:  Common CPLB definitions for CPLB init
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _CPLB_H
#define _CPLB_H

#include <asm/blackfin.h>
#include <asm/mach/anomaly.h>

#define SDRAM_IGENERIC    (CPLB_L1_CHBL | CPLB_USER_RD | CPLB_VALID | CPLB_PORTPRIO)
#define SDRAM_IKERNEL     (SDRAM_IGENERIC | CPLB_LOCK)
#define L1_IMEMORY        (               CPLB_USER_RD | CPLB_VALID | CPLB_LOCK)
#define SDRAM_INON_CHBL   (               CPLB_USER_RD | CPLB_VALID)

/*Use the menuconfig cache policy here - CONFIG_BFIN_WT/CONFIG_BFIN_WB*/

#if ANOMALY_05000158
#define ANOMALY_05000158_WORKAROUND             0x200
#else
#define ANOMALY_05000158_WORKAROUND             0x0
#endif

#define CPLB_COMMON	(CPLB_DIRTY | CPLB_SUPV_WR | CPLB_USER_WR | CPLB_USER_RD | CPLB_VALID | ANOMALY_05000158_WORKAROUND)

#ifdef CONFIG_BFIN_WB         /*Write Back Policy */
#define SDRAM_DGENERIC   (CPLB_L1_CHBL | CPLB_COMMON)
#else                           /*Write Through */
#define SDRAM_DGENERIC   (CPLB_L1_CHBL | CPLB_WT | CPLB_L1_AOW  | CPLB_COMMON)
#endif

#define L1_DMEMORY       (CPLB_LOCK | CPLB_COMMON)
#define L2_MEMORY        (CPLB_COMMON)
#define SDRAM_DNON_CHBL  (CPLB_COMMON)
#define SDRAM_EBIU       (CPLB_COMMON)
#define SDRAM_OOPS       (CPLB_VALID | ANOMALY_05000158_WORKAROUND | CPLB_LOCK | CPLB_DIRTY)

#define SIZE_1K 0x00000400      /* 1K */
#define SIZE_4K 0x00001000      /* 4K */
#define SIZE_1M 0x00100000      /* 1M */
#define SIZE_4M 0x00400000      /* 4M */

#define MAX_CPLBS (16 * 2)

#define ASYNC_MEMORY_CPLB_COVERAGE	((ASYNC_BANK0_SIZE + ASYNC_BANK1_SIZE + \
				 ASYNC_BANK2_SIZE + ASYNC_BANK3_SIZE) / SIZE_4M)

/*
* Number of required data CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 16 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Data Memory
* possibly 1 for L2 Data Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
* 1 for ASYNC Memory
*/


#define MAX_SWITCH_D_CPLBS (((CONFIG_MEM_SIZE / 4) + 16 + 1 + 1 + 1 \
				 + ASYNC_MEMORY_CPLB_COVERAGE) * 2)

/*
* Number of required instruction CPLB switchtable entries
* MEMSIZE / 4 (we mostly install 4M page size CPLBs
* approx 12 for smaller 1MB page size CPLBs for allignment purposes
* 1 for L1 Instruction Memory
* possibly 1 for L2 Instruction Memory
* 1 for CONFIG_DEBUG_HUNT_FOR_ZERO
*/

#define MAX_SWITCH_I_CPLBS (((CONFIG_MEM_SIZE / 4) + 12 + 1 + 1 + 1) * 2)


#define CPLB_ENABLE_ICACHE_P	0
#define CPLB_ENABLE_DCACHE_P	1
#define CPLB_ENABLE_DCACHE2_P	2
#define CPLB_ENABLE_CPLBS_P	3	/* Deprecated! */
#define CPLB_ENABLE_ICPLBS_P	4
#define CPLB_ENABLE_DCPLBS_P	5

#define CPLB_ENABLE_ICACHE	(1<<CPLB_ENABLE_ICACHE_P)
#define CPLB_ENABLE_DCACHE	(1<<CPLB_ENABLE_DCACHE_P)
#define CPLB_ENABLE_DCACHE2	(1<<CPLB_ENABLE_DCACHE2_P)
#define CPLB_ENABLE_CPLBS	(1<<CPLB_ENABLE_CPLBS_P)
#define CPLB_ENABLE_ICPLBS	(1<<CPLB_ENABLE_ICPLBS_P)
#define CPLB_ENABLE_DCPLBS	(1<<CPLB_ENABLE_DCPLBS_P)
#define CPLB_ENABLE_ANY_CPLBS	CPLB_ENABLE_CPLBS | \
				CPLB_ENABLE_ICPLBS | \
				CPLB_ENABLE_DCPLBS

#define CPLB_RELOADED		0x0000
#define CPLB_NO_UNLOCKED	0x0001
#define CPLB_NO_ADDR_MATCH	0x0002
#define CPLB_PROT_VIOL		0x0003
#define CPLB_UNKNOWN_ERR	0x0004

#define CPLB_DEF_CACHE		CPLB_L1_CHBL | CPLB_WT
#define CPLB_CACHE_ENABLED	CPLB_L1_CHBL | CPLB_DIRTY

#define CPLB_I_PAGE_MGMT	CPLB_LOCK | CPLB_VALID
#define CPLB_D_PAGE_MGMT	CPLB_LOCK | CPLB_ALL_ACCESS | CPLB_VALID
#define CPLB_DNOCACHE		CPLB_ALL_ACCESS | CPLB_VALID
#define CPLB_DDOCACHE		CPLB_DNOCACHE | CPLB_DEF_CACHE
#define CPLB_INOCACHE   	CPLB_USER_RD | CPLB_VALID
#define CPLB_IDOCACHE   	CPLB_INOCACHE | CPLB_L1_CHBL

#endif				/* _CPLB_H */
