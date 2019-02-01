/* $Id: RTStrNICmpAscii.cpp $ */
/** @file
 * IPRT - RTStrNICmpAscii.
 */

/*
 * Copyright (C) 2010-2019 Oracle Corporation
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

#include <iprt/assert.h>
#include <iprt/ctype.h>


RTDECL(int) RTStrNICmpAscii(const char *psz1, const char *psz2, size_t cchMax)
{
    if (cchMax == 0)
        return 0;
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    for (;;)
    {
        RTUNICP uc1;
        int rc = RTStrGetCpNEx(&psz1, &cchMax, &uc1);
        if (RT_SUCCESS(rc))
        {
            unsigned char uch2 = *psz2++; Assert(uch2 < 0x80);

            /* compare */
            int iDiff = uc1 - uch2;
            if (iDiff)
            {
                if (uc1 >= 0x80)
                    return 1;

                iDiff = RT_C_TO_LOWER(uc1) - RT_C_TO_LOWER(uch2); /* Return lower cased diff! */
                if (iDiff)
                    return iDiff;
            }

            if (uch2 && cchMax)
            { /* likely */ }
            else
                return 0;
        }
        /* Hit some bad encoding, continue in case sensitive mode. */
        else
            return RTStrNCmp(psz1 - 1, psz2, cchMax + 1);
    }
}
RT_EXPORT_SYMBOL(RTStrNICmpAscii);

