/** @file
 *
 * VBox Host Guest Shared Memory Interface (HGSMI).
 * Memory allocator.
 */

/*
 * Copyright (C) 2014-2016 Oracle Corporation
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


#ifndef ___VBox_HGSMI_HGSMIMemAlloc_h
#define ___VBox_HGSMI_HGSMIMemAlloc_h

#include <VBox/HGSMI/HGSMIDefs.h>
#include <iprt/list.h>


/* Descriptor. */
#define HGSMI_MA_DESC_OFFSET_MASK UINT32_C(0xFFFFFFE0)
#define HGSMI_MA_DESC_FREE_MASK   UINT32_C(0x00000010)
#define HGSMI_MA_DESC_ORDER_MASK  UINT32_C(0x0000000F)

#define HGSMI_MA_DESC_OFFSET(d)  ((d) & HGSMI_MA_DESC_OFFSET_MASK)
#define HGSMI_MA_DESC_IS_FREE(d) (((d) & HGSMI_MA_DESC_FREE_MASK) != 0)
#define HGSMI_MA_DESC_ORDER(d)   ((d) & HGSMI_MA_DESC_ORDER_MASK)

#define HGSMI_MA_DESC_ORDER_BASE UINT32_C(5)

#define HGSMI_MA_BLOCK_SIZE_MIN (UINT32_C(1) << (HGSMI_MA_DESC_ORDER_BASE + 0))
#define HGSMI_MA_BLOCK_SIZE_MAX (UINT32_C(1) << (HGSMI_MA_DESC_ORDER_BASE + HGSMI_MA_DESC_ORDER_MASK))

/* HGSMI_MA_DESC_ORDER_BASE must correspond to HGSMI_MA_DESC_OFFSET_MASK. */
AssertCompile((~HGSMI_MA_DESC_OFFSET_MASK + 1) == HGSMI_MA_BLOCK_SIZE_MIN);


typedef struct HGSMIMABLOCK
{
    RTLISTNODE nodeBlock;
    RTLISTNODE nodeFree;
    HGSMIOFFSET descriptor;
} HGSMIMABLOCK;

typedef struct HGSMIMADATA
{
    HGSMIAREA area;
    HGSMIENV env;
    HGSMISIZE cbMaxBlock;

    uint32_t cBlocks;                                           /* How many blocks in the listBlocks. */
    RTLISTANCHOR listBlocks;                                    /* All memory blocks, sorted. */
    RTLISTANCHOR aListFreeBlocks[HGSMI_MA_DESC_ORDER_MASK + 1]; /* For free blocks of each order. */
} HGSMIMADATA;

RT_C_DECLS_BEGIN

int HGSMIMAInit(HGSMIMADATA *pMA, const HGSMIAREA *pArea,
                HGSMIOFFSET *paDescriptors, uint32_t cDescriptors, HGSMISIZE cbMaxBlock,
                const HGSMIENV *pEnv);
void HGSMIMAUninit(HGSMIMADATA *pMA);

void *HGSMIMAAlloc(HGSMIMADATA *pMA, HGSMISIZE cb);
void HGSMIMAFree(HGSMIMADATA *pMA, void *pv);

HGSMIMABLOCK *HGSMIMASearchOffset(HGSMIMADATA *pMA, HGSMIOFFSET off);

uint32_t HGSMIPopCnt32(uint32_t u32);

DECLINLINE(HGSMISIZE) HGSMIMAOrder2Size(HGSMIOFFSET order)
{
    return (UINT32_C(1) << (HGSMI_MA_DESC_ORDER_BASE + order));
}

DECLINLINE(HGSMIOFFSET) HGSMIMASize2Order(HGSMISIZE cb)
{
    HGSMIOFFSET order = HGSMIPopCnt32(cb - 1) - HGSMI_MA_DESC_ORDER_BASE;
    Assert(HGSMIMAOrder2Size(order) == cb);
    return order;
}

RT_C_DECLS_END

#endif /* !___VBox_HGSMI_HGSMIMemAlloc_h */
