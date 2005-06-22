/*
 * HvCallCfg.h
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
#ifndef _HVCALLCFG_H
#define _HVCALLCFG_H

#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvTypes.h>

enum HvCallCfg_ReqQual {
	HvCallCfg_Cur	= 0,
	HvCallCfg_Init	= 1,
	HvCallCfg_Max	= 2,
	HvCallCfg_Min	= 3
};

#define HvCallCfgGetLps					HvCallCfg +  0
#define HvCallCfgGetActiveLpMap				HvCallCfg +  1
#define HvCallCfgGetLpVrmIndex				HvCallCfg +  2
#define HvCallCfgGetLpMinSupportedPlicVrmIndex		HvCallCfg +  3
#define HvCallCfgGetLpMinCompatablePlicVrmIndex		HvCallCfg +  4
#define HvCallCfgGetLpVrmName				HvCallCfg +  5
#define HvCallCfgGetSystemPhysicalProcessors		HvCallCfg +  6
#define HvCallCfgGetPhysicalProcessors			HvCallCfg +  7
#define HvCallCfgGetSystemMsChunks			HvCallCfg +  8
#define HvCallCfgGetMsChunks				HvCallCfg +  9
#define HvCallCfgGetInteractivePercentage		HvCallCfg + 10
#define HvCallCfgIsBusDedicated				HvCallCfg + 11
#define HvCallCfgGetBusOwner				HvCallCfg + 12
#define HvCallCfgGetBusAllocation			HvCallCfg + 13
#define HvCallCfgGetBusUnitOwner			HvCallCfg + 14
#define HvCallCfgGetBusUnitAllocation			HvCallCfg + 15
#define HvCallCfgGetVirtualBusPool			HvCallCfg + 16
#define HvCallCfgGetBusUnitInterruptProc		HvCallCfg + 17
#define HvCallCfgGetConfiguredBusUnitsForIntProc	HvCallCfg + 18
#define HvCallCfgGetRioSanBusPool			HvCallCfg + 19
#define HvCallCfgGetSharedPoolIndex			HvCallCfg + 20
#define HvCallCfgGetSharedProcUnits			HvCallCfg + 21
#define HvCallCfgGetNumProcsInSharedPool		HvCallCfg + 22
#define HvCallCfgRouter23				HvCallCfg + 23
#define HvCallCfgRouter24				HvCallCfg + 24
#define HvCallCfgRouter25				HvCallCfg + 25
#define HvCallCfgRouter26				HvCallCfg + 26
#define HvCallCfgRouter27				HvCallCfg + 27
#define HvCallCfgGetMinRuntimeMsChunks			HvCallCfg + 28
#define HvCallCfgSetMinRuntimeMsChunks			HvCallCfg + 29
#define HvCallCfgGetVirtualLanIndexMap			HvCallCfg + 30
#define HvCallCfgGetLpExecutionMode			HvCallCfg + 31
#define HvCallCfgGetHostingLpIndex			HvCallCfg + 32

static inline HvLpIndex	HvCallCfg_getBusOwner(u64 busIndex)
{
	return HvCall1(HvCallCfgGetBusOwner, busIndex);
}

static inline HvLpVirtualLanIndexMap HvCallCfg_getVirtualLanIndexMap(
		HvLpIndex lp)
{
	/*
	 * This is a new function in V5R1 so calls to this on older
	 * hypervisors will return -1
	 */
	u64 retVal = HvCall1(HvCallCfgGetVirtualLanIndexMap, lp);
	if (retVal == -1)
		retVal = 0;
	return retVal;
}

static inline u64 HvCallCfg_getMsChunks(HvLpIndex lp,
		enum HvCallCfg_ReqQual qual)
{
	return HvCall2(HvCallCfgGetMsChunks, lp, qual);
}

static inline u64 HvCallCfg_getSystemPhysicalProcessors(void)
{
	return HvCall0(HvCallCfgGetSystemPhysicalProcessors);
}

static inline u64 HvCallCfg_getPhysicalProcessors(HvLpIndex lp,
		enum HvCallCfg_ReqQual qual)
{
	return HvCall2(HvCallCfgGetPhysicalProcessors, lp, qual);
}

static inline HvLpSharedPoolIndex HvCallCfg_getSharedPoolIndex(HvLpIndex lp)
{
	return HvCall1(HvCallCfgGetSharedPoolIndex, lp);

}

static inline u64 HvCallCfg_getSharedProcUnits(HvLpIndex lp,
		enum HvCallCfg_ReqQual qual)
{
	return HvCall2(HvCallCfgGetSharedProcUnits, lp, qual);

}

static inline u64 HvCallCfg_getNumProcsInSharedPool(HvLpSharedPoolIndex sPI)
{
	return (u16)HvCall1(HvCallCfgGetNumProcsInSharedPool, sPI);

}

static inline HvLpIndex	HvCallCfg_getHostingLpIndex(HvLpIndex lp)
{
	return HvCall1(HvCallCfgGetHostingLpIndex, lp);
}

#endif /* _HVCALLCFG_H */
