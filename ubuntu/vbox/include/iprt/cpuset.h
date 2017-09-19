/** @file
 * IPRT - CPU Set.
 */

/*
 * Copyright (C) 2008-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_cpuset_h
#define ___iprt_cpuset_h

#include <iprt/types.h>
#include <iprt/mp.h> /* RTMpCpuIdToSetIndex */
#include <iprt/asm.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_cpuset RTCpuSet - CPU Set
 * @ingroup grp_rt
 * @{
 */


/**
 * Clear all CPUs.
 *
 * @returns pSet.
 * @param   pSet    Pointer to the set.
 */
DECLINLINE(PRTCPUSET) RTCpuSetEmpty(PRTCPUSET pSet)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pSet->bmSet); i++)
        pSet->bmSet[i] = 0;
    return pSet;
}


/**
 * Set all CPUs.
 *
 * @returns pSet.
 * @param   pSet    Pointer to the set.
 */
DECLINLINE(PRTCPUSET) RTCpuSetFill(PRTCPUSET pSet)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pSet->bmSet); i++)
        pSet->bmSet[i] = UINT64_MAX;
    return pSet;
}


/**
 * Copies one set to another.
 *
 * @param   pDst    Pointer to the destination set.
 * @param   pSrc    Pointer to the source set.
 */
DECLINLINE(void) RTCpuSetCopy(PRTCPUSET pDst, PRTCPUSET pSrc)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pDst->bmSet); i++)
        pDst->bmSet[i] = pSrc->bmSet[i];
}


/**
 * ANDs the given CPU set with another.
 *
 * @returns pSet.
 * @param   pSet          Pointer to the set.
 * @param   pAndMaskSet   Pointer to the AND-mask set.
 */
DECLINLINE(PRTCPUSET) RTCpuSetAnd(PRTCPUSET pSet, PRTCPUSET pAndMaskSet)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pSet->bmSet); i++)
        ASMAtomicAndU64((volatile uint64_t *)&pSet->bmSet[i], pAndMaskSet->bmSet[i]);
    return pSet;
}


/**
 * Adds a CPU given by its identifier to the set.
 *
 * @returns 0 on success, -1 if idCpu isn't valid.
 * @param   pSet    Pointer to the set.
 * @param   idCpu   The identifier of the CPU to add.
 * @remarks The modification is atomic.
 */
DECLINLINE(int) RTCpuSetAdd(PRTCPUSET pSet, RTCPUID idCpu)
{
    int iCpu = RTMpCpuIdToSetIndex(idCpu);
    if (RT_LIKELY(iCpu >= 0))
    {
        ASMAtomicBitSet(pSet, iCpu);
        return 0;
    }
    return -1;
}


/**
 * Adds a CPU given by its identifier to the set.
 *
 * @returns 0 on success, -1 if iCpu isn't valid.
 * @param   pSet    Pointer to the set.
 * @param   iCpu    The index of the CPU to add.
 * @remarks The modification is atomic.
 */
DECLINLINE(int) RTCpuSetAddByIndex(PRTCPUSET pSet, int iCpu)
{
    if (RT_LIKELY((unsigned)iCpu < RTCPUSET_MAX_CPUS))
    {
        ASMAtomicBitSet(pSet, iCpu);
        return 0;
    }
    return -1;
}


/**
 * Removes a CPU given by its identifier from the set.
 *
 * @returns 0 on success, -1 if idCpu isn't valid.
 * @param   pSet    Pointer to the set.
 * @param   idCpu   The identifier of the CPU to delete.
 * @remarks The modification is atomic.
 */
DECLINLINE(int) RTCpuSetDel(PRTCPUSET pSet, RTCPUID idCpu)
{
    int iCpu = RTMpCpuIdToSetIndex(idCpu);
    if (RT_LIKELY(iCpu >= 0))
    {
        ASMAtomicBitClear(pSet, iCpu);
        return 0;
    }
    return -1;
}


/**
 * Removes a CPU given by its index from the set.
 *
 * @returns 0 on success, -1 if iCpu isn't valid.
 * @param   pSet    Pointer to the set.
 * @param   iCpu    The index of the CPU to delete.
 * @remarks The modification is atomic.
 */
DECLINLINE(int) RTCpuSetDelByIndex(PRTCPUSET pSet, int iCpu)
{
    if (RT_LIKELY((unsigned)iCpu < RTCPUSET_MAX_CPUS))
    {
        ASMAtomicBitClear(pSet, iCpu);
        return 0;
    }
    return -1;
}


/**
 * Checks if a CPU given by its identifier is a member of the set.
 *
 * @returns true / false accordingly.
 * @param   pSet    Pointer to the set.
 * @param   idCpu   The identifier of the CPU to look for.
 * @remarks The test is atomic.
 */
DECLINLINE(bool) RTCpuSetIsMember(PCRTCPUSET pSet, RTCPUID idCpu)
{
    int iCpu = RTMpCpuIdToSetIndex(idCpu);
    if (RT_LIKELY(iCpu >= 0))
        return ASMBitTest((volatile void *)pSet, iCpu);
    return false;
}


/**
 * Checks if a CPU given by its index is a member of the set.
 *
 * @returns true / false accordingly.
 * @param   pSet    Pointer to the set.
 * @param   iCpu    The index of the CPU in the set.
 * @remarks The test is atomic.
 */
DECLINLINE(bool) RTCpuSetIsMemberByIndex(PCRTCPUSET pSet, int iCpu)
{
    if (RT_LIKELY((unsigned)iCpu < RTCPUSET_MAX_CPUS))
        return ASMBitTest((volatile void *)pSet, iCpu);
    return false;
}


/**
 * Checks if the two sets match or not.
 *
 * @returns true / false accordingly.
 * @param   pSet1       The first set.
 * @param   pSet2       The second set.
 */
DECLINLINE(bool) RTCpuSetIsEqual(PCRTCPUSET pSet1, PCRTCPUSET pSet2)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pSet1->bmSet); i++)
        if (pSet1->bmSet[i] != pSet2->bmSet[i])
            return false;
    return true;
}


/**
 * Checks if the CPU set is empty or not.
 *
 * @returns true / false accordingly.
 * @param   pSet    Pointer to the set.
 */
DECLINLINE(bool) RTCpuSetIsEmpty(PRTCPUSET pSet)
{
    size_t i;
    for (i = 0; i < RT_ELEMENTS(pSet->bmSet); i++)
        if (pSet->bmSet[i])
            return false;
    return true;
}


/**
 * Converts the CPU set to a 64-bit mask.
 *
 * @returns The mask.
 * @param   pSet    Pointer to the set.
 * @remarks Use with extreme care as it may lose information!
 */
DECLINLINE(uint64_t) RTCpuSetToU64(PCRTCPUSET pSet)
{
    return pSet->bmSet[0];
}


/**
 * Initializes the CPU set from a 64-bit mask.
 *
 * @param   pSet    Pointer to the set.
 * @param   fMask   The mask.
 */
DECLINLINE(PRTCPUSET) RTCpuSetFromU64(PRTCPUSET pSet, uint64_t fMask)
{
    size_t i;

    pSet->bmSet[0] = fMask;
    for (i = 1; i < RT_ELEMENTS(pSet->bmSet); i++)
        pSet->bmSet[i] = 0;

    return pSet;
}


/**
 * Count the CPUs in the set.
 *
 * @returns CPU count.
 * @param   pSet    Pointer to the set.
 */
DECLINLINE(int) RTCpuSetCount(PCRTCPUSET pSet)
{
    int         cCpus = 0;
    size_t      i;

    for (i = 0; i < RT_ELEMENTS(pSet->bmSet); i++)
    {
        uint64_t u64 = pSet->bmSet[i];
        if (u64 != 0)
        {
            unsigned iCpu = 64;
            while (iCpu-- > 0)
            {
                if (u64 & 1)
                    cCpus++;
                u64 >>= 1;
            }
        }
    }
    return cCpus;
}


/**
 * Get the highest set index.
 *
 * @returns The higest set index, -1 if all bits are clear.
 * @param   pSet    Pointer to the set.
 */
DECLINLINE(int) RTCpuLastIndex(PCRTCPUSET pSet)
{
    size_t i = RT_ELEMENTS(pSet->bmSet);
    while (i-- > 0)
    {
        uint64_t u64 = pSet->bmSet[i];
        if (u64)
        {
            /* There are more efficient ways to do this in asm.h... */
            unsigned iBit;
            for (iBit = 63; iBit > 0; iBit--)
            {
                if (u64 & RT_BIT_64(63))
                    break;
                u64 <<= 1;
            }
            return (int)i * 64 + iBit;
        }
    }
    return 0;
}


/** @} */

RT_C_DECLS_END

#endif

