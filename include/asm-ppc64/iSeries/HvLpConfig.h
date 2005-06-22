/*
 * HvLpConfig.h
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
#ifndef _HVLPCONFIG_H
#define _HVLPCONFIG_H

/*
 * This file contains the interface to the LPAR configuration data
 * to determine which resources should be allocated to each partition.
 */

#include <asm/iSeries/HvCallCfg.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/ItLpNaca.h>

extern HvLpIndex HvLpConfig_getLpIndex_outline(void);

static inline HvLpIndex	HvLpConfig_getLpIndex(void)
{
	return itLpNaca.xLpIndex;
}

static inline HvLpIndex	HvLpConfig_getPrimaryLpIndex(void)
{
	return itLpNaca.xPrimaryLpIndex;
}

static inline u64 HvLpConfig_getMsChunks(void)
{
	return HvCallCfg_getMsChunks(HvLpConfig_getLpIndex(), HvCallCfg_Cur);
}

static inline u64 HvLpConfig_getSystemPhysicalProcessors(void)
{
	return HvCallCfg_getSystemPhysicalProcessors();
}

static inline u64 HvLpConfig_getNumProcsInSharedPool(HvLpSharedPoolIndex sPI)
{
	return HvCallCfg_getNumProcsInSharedPool(sPI);
}

static inline u64 HvLpConfig_getPhysicalProcessors(void)
{
	return HvCallCfg_getPhysicalProcessors(HvLpConfig_getLpIndex(),
			HvCallCfg_Cur);
}

static inline HvLpSharedPoolIndex HvLpConfig_getSharedPoolIndex(void)
{
	return HvCallCfg_getSharedPoolIndex(HvLpConfig_getLpIndex());
}

static inline u64 HvLpConfig_getSharedProcUnits(void)
{
	return HvCallCfg_getSharedProcUnits(HvLpConfig_getLpIndex(),
			HvCallCfg_Cur);
}

static inline u64 HvLpConfig_getMaxSharedProcUnits(void)
{
	return HvCallCfg_getSharedProcUnits(HvLpConfig_getLpIndex(),
			HvCallCfg_Max);
}

static inline u64 HvLpConfig_getMaxPhysicalProcessors(void)
{
	return HvCallCfg_getPhysicalProcessors(HvLpConfig_getLpIndex(),
			HvCallCfg_Max);
}

static inline HvLpVirtualLanIndexMap HvLpConfig_getVirtualLanIndexMap(void)
{
	return HvCallCfg_getVirtualLanIndexMap(HvLpConfig_getLpIndex_outline());
}

static inline HvLpVirtualLanIndexMap HvLpConfig_getVirtualLanIndexMapForLp(
		HvLpIndex lp)
{
	return HvCallCfg_getVirtualLanIndexMap(lp);
}

static inline int HvLpConfig_doLpsCommunicateOnVirtualLan(HvLpIndex lp1,
		HvLpIndex lp2)
{
	HvLpVirtualLanIndexMap virtualLanIndexMap1 =
		HvCallCfg_getVirtualLanIndexMap(lp1);
	HvLpVirtualLanIndexMap virtualLanIndexMap2 =
		HvCallCfg_getVirtualLanIndexMap(lp2);
	return ((virtualLanIndexMap1 & virtualLanIndexMap2) != 0);
}

static inline HvLpIndex HvLpConfig_getHostingLpIndex(HvLpIndex lp)
{
	return HvCallCfg_getHostingLpIndex(lp);
}

#endif /* _HVLPCONFIG_H */
