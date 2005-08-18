/*
 * HvCallHpt.h
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
#ifndef _HVCALLHPT_H
#define _HVCALLHPT_H

/*
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from the OS.
 */

#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/mmu.h>

#define HvCallHptGetHptAddress		HvCallHpt +  0
#define HvCallHptGetHptPages		HvCallHpt +  1
#define HvCallHptSetPp			HvCallHpt +  5
#define HvCallHptSetSwBits		HvCallHpt +  6
#define HvCallHptUpdate			HvCallHpt +  7
#define HvCallHptInvalidateNoSyncICache	HvCallHpt +  8
#define HvCallHptGet			HvCallHpt + 11
#define HvCallHptFindNextValid		HvCallHpt + 12
#define HvCallHptFindValid		HvCallHpt + 13
#define HvCallHptAddValidate		HvCallHpt + 16
#define HvCallHptInvalidateSetSwBitsGet HvCallHpt + 18


static inline u64 HvCallHpt_getHptAddress(void)
{
	return HvCall0(HvCallHptGetHptAddress);
}

static inline u64 HvCallHpt_getHptPages(void)
{
	return HvCall0(HvCallHptGetHptPages);
}

static inline void HvCallHpt_setPp(u32 hpteIndex, u8 value)
{
	HvCall2(HvCallHptSetPp, hpteIndex, value);
}

static inline void HvCallHpt_setSwBits(u32 hpteIndex, u8 bitson, u8 bitsoff)
{
	HvCall3(HvCallHptSetSwBits, hpteIndex, bitson, bitsoff);
}

static inline void HvCallHpt_invalidateNoSyncICache(u32 hpteIndex)
{
	HvCall1(HvCallHptInvalidateNoSyncICache, hpteIndex);
}

static inline u64 HvCallHpt_invalidateSetSwBitsGet(u32 hpteIndex, u8 bitson,
		u8 bitsoff)
{
	u64 compressedStatus;

	compressedStatus = HvCall4(HvCallHptInvalidateSetSwBitsGet,
			hpteIndex, bitson, bitsoff, 1);
	HvCall1(HvCallHptInvalidateNoSyncICache, hpteIndex);
	return compressedStatus;
}

static inline u64 HvCallHpt_findValid(hpte_t *hpte, u64 vpn)
{
	return HvCall3Ret16(HvCallHptFindValid, hpte, vpn, 0, 0);
}

static inline u64 HvCallHpt_findNextValid(hpte_t *hpte, u32 hpteIndex,
		u8 bitson, u8 bitsoff)
{
	return HvCall3Ret16(HvCallHptFindNextValid, hpte, hpteIndex,
			bitson, bitsoff);
}

static inline void HvCallHpt_get(hpte_t *hpte, u32 hpteIndex)
{
	HvCall2Ret16(HvCallHptGet, hpte, hpteIndex, 0);
}

static inline void HvCallHpt_addValidate(u32 hpteIndex, u32 hBit, hpte_t *hpte)
{
	HvCall4(HvCallHptAddValidate, hpteIndex, hBit, hpte->v, hpte->r);
}

#endif /* _HVCALLHPT_H */
