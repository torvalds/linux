/* $Id: alloc-r0drv.h $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver.
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

#ifndef ___r0drv_alloc_r0drv_h
#define ___r0drv_alloc_r0drv_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/mem.h>
#include "internal/magics.h"

RT_C_DECLS_BEGIN

/**
 * Header which heading all memory blocks.
 */
typedef struct RTMEMHDR
{
    /** Magic (RTMEMHDR_MAGIC). */
    uint32_t    u32Magic;
    /** Block flags (RTMEMHDR_FLAG_*). */
    uint32_t    fFlags;
    /** The actual size of the block, header not included. */
    uint32_t    cb;
    /** The requested allocation size. */
    uint32_t    cbReq;
} RTMEMHDR, *PRTMEMHDR;


/** @name RTMEMHDR::fFlags.
 * @{ */
/** Clear the allocated memory. */
#define RTMEMHDR_FLAG_ZEROED        RT_BIT(0)
/** Executable flag. */
#define RTMEMHDR_FLAG_EXEC          RT_BIT(1)
/** Use allocation method suitable for any context. */
#define RTMEMHDR_FLAG_ANY_CTX_ALLOC RT_BIT(2)
/** Use allocation method which allow for freeing in any context. */
#define RTMEMHDR_FLAG_ANY_CTX_FREE  RT_BIT(3)
/** Both alloc and free in any context (or we're just darn lazy). */
#define RTMEMHDR_FLAG_ANY_CTX       (RTMEMHDR_FLAG_ANY_CTX_ALLOC | RTMEMHDR_FLAG_ANY_CTX_FREE)
/** Indicate that it was allocated by rtR0MemAllocExTag. */
#define RTMEMHDR_FLAG_ALLOC_EX      RT_BIT(4)
#ifdef RT_OS_LINUX
/** Linux: Allocated using vm_area hacks. */
# define RTMEMHDR_FLAG_EXEC_VM_AREA RT_BIT(29)
/** Linux: Allocated from the special heap for executable memory. */
# define RTMEMHDR_FLAG_EXEC_HEAP    RT_BIT(30)
/** Linux: Allocated by kmalloc() instead of vmalloc(). */
# define RTMEMHDR_FLAG_KMALLOC      RT_BIT(31)
#endif
/** @} */


/**
 * Heap allocation back end for ring-0.
 *
 * @returns IPRT status code.  VERR_NO_MEMORY suffices for RTMEMHDR_FLAG_EXEC,
 *          the caller will change it to VERR_NO_EXEC_MEMORY when appropriate.
 *
 * @param   cb          The amount of memory requested by the user.  This does
 *                      not include the header.
 * @param   fFlags      The allocation flags and more.  These should be
 *                      assigned to RTMEMHDR::fFlags together with any flags
 *                      the backend might be using.
 * @param   ppHdr       Where to return the memory header on success.
 */
DECLHIDDEN(int)     rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr);

/**
 * Free memory allocated by rtR0MemAllocEx.
 * @param   pHdr        The memory block to free.  (Never NULL.)
 */
DECLHIDDEN(void)    rtR0MemFree(PRTMEMHDR pHdr);

RT_C_DECLS_END
#endif

