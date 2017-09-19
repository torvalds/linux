/* $Id: alloc.cpp $ */
/** @file
 * IPRT - Memory Allocation.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifndef RTMEM_NO_WRAP_TO_EF_APIS
# define RTMEM_NO_WRAP_TO_EF_APIS
#endif
#include <iprt/mem.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/string.h>



RTDECL(void *) RTMemDupTag(const void *pvSrc, size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemAllocTag(cb, pszTag);
    if (pvDst)
        memcpy(pvDst, pvSrc, cb);
    return pvDst;
}
RT_EXPORT_SYMBOL(RTMemDupTag);


RTDECL(void *) RTMemDupExTag(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemAllocTag(cbSrc + cbExtra, pszTag);
    if (pvDst)
    {
        memcpy(pvDst, pvSrc, cbSrc);
        memset((uint8_t *)pvDst + cbSrc, 0, cbExtra);
    }
    return pvDst;
}
RT_EXPORT_SYMBOL(RTMemDupExTag);

