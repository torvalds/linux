/* $Id: RTStrNCmp.cpp $ */
/** @file
 * IPRT - RTStrNCmp.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
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
#include <iprt/string.h>
#include "internal/iprt.h"


RTDECL(int) RTStrNCmp(const char *psz1, const char *psz2, size_t cchMax)
{
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

#ifdef RT_OS_SOLARIS
    /* Solaris: tstUtf8 found to fail for some RTSTR_MAX on testboxsh1:
       solaris.amd64 v5.10 (Generic_142901-12 (Assembled 30 March 2009)). */
    while (cchMax-- > 0)
    {
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        if (ch1 != ch2)
            return ch1 > ch2 ? 1 : -1;
        else if (ch1 == 0)
            break;
    }
    return 0;
#else
    return strncmp(psz1, psz2, cchMax);
#endif
}
RT_EXPORT_SYMBOL(RTStrNCmp);

