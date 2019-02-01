/* $Id: strformattype.cpp $ */
/** @file
 * IPRT - IPRT String Formatter Extensions, Dynamic Types.
 */

/*
 * Copyright (C) 2008-2019 Oracle Corporation
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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_STRING
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/stdarg.h>
#include "internal/string.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_STRICT
# define RTSTRFORMATTYPE_WITH_LOCKING
#endif
#ifdef RTSTRFORMATTYPE_WITH_LOCKING
# define RTSTRFORMATTYPE_LOCK_OFFSET    0x7fff0000
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Description of a registered formatting type.
 *
 * In GC we'll be using offsets instead of pointers just to try avoid having to
 * do the bothersome relocating. This of course assumes that all the relevant
 * code stays within the same mapping.
 */
typedef struct RTSTRDYNFMT
{
    /** The length of the type. */
    uint8_t             cchType;
    /** The type name. */
    char                szType[47];
    /** The handler function.
     * In GC the offset is relative to g_aTypes[0], so that &g_aTypes[0] + offHandler
     * gives the actual address. */
#ifdef IN_RC
    int32_t             offHandler;
#else
    PFNRTSTRFORMATTYPE  pfnHandler;
#endif
    /** Callback argument. */
    void * volatile     pvUser;
#if ARCH_BITS == 32
    /** Size alignment padding. */
    char                abPadding[8];
#endif
} RTSTRDYNFMT;
AssertCompileSizeAlignment(RTSTRDYNFMT, 32);
typedef RTSTRDYNFMT *PRTSTRDYNFMT;
typedef RTSTRDYNFMT const *PCRTSTRDYNFMT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The registered types, sorted for binary lookup.
 * We use a static array here because it avoids RTMemAlloc dependencies+leaks. */
static RTSTRDYNFMT      g_aTypes[64];
/** The number of registered types. */
static uint32_t         g_cTypes = 0;
#ifdef RTSTRFORMATTYPE_WITH_LOCKING
/** This is just a thing we assert/spin on.
 * Zero == unlocked, negative == write locked, positive == read locked.
 *
 * The user should do all the serialization and we'll smack his fingers in
 * strict builds if he doesn't. */
static int32_t volatile g_i32Spinlock = 0;
#endif


/**
 * Locks the stuff for updating.
 *
 * Mostly for check that the caller is doing his job.
 */
DECLINLINE(void) rtstrFormatTypeWriteLock(void)
{
#if defined(RTSTRFORMATTYPE_WITH_LOCKING)
    if (RT_UNLIKELY(!ASMAtomicCmpXchgS32(&g_i32Spinlock, -RTSTRFORMATTYPE_LOCK_OFFSET, 0)))
    {
        unsigned volatile i;

        AssertFailed();
        for (i = 0;; i++)
            if (    !g_i32Spinlock
                &&  ASMAtomicCmpXchgS32(&g_i32Spinlock, -RTSTRFORMATTYPE_LOCK_OFFSET, 0))
                break;
    }
#endif
}


/**
 * Undoing rtstrFormatTypeWriteLock.
 */
DECLINLINE(void) rtstrFormatTypeWriteUnlock(void)
{
#if defined(RTSTRFORMATTYPE_WITH_LOCKING)
    Assert(g_i32Spinlock < 0);
    ASMAtomicAddS32(&g_i32Spinlock, RTSTRFORMATTYPE_LOCK_OFFSET);
#endif
}


/**
 * Locks the stuff for reading.
 *
 * This is just cheap stuff to make sure the caller is doing the right thing.
 */
DECLINLINE(void) rtstrFormatTypeReadLock(void)
{
#if defined(RTSTRFORMATTYPE_WITH_LOCKING)
    if (RT_UNLIKELY(ASMAtomicIncS32(&g_i32Spinlock) < 0))
    {
        unsigned volatile i;

        AssertFailed();
        for (i = 0;; i++)
            if (ASMAtomicUoReadS32(&g_i32Spinlock) > 0)
                break;
    }
#endif
}


/**
 * Undoing rtstrFormatTypeReadLock.
 */
DECLINLINE(void) rtstrFormatTypeReadUnlock(void)
{
#if defined(RTSTRFORMATTYPE_WITH_LOCKING)
    Assert(g_i32Spinlock > 0);
    ASMAtomicDecS32(&g_i32Spinlock);
#endif
}


/**
 * Compares a type string with a type entry, the string doesn't need to be terminated.
 *
 * @returns Same as memcmp.
 * @param   pszType     The type string, doesn't need to be terminated.
 * @param   cchType     The number of chars in @a pszType to compare.
 * @param   pType       The type entry to compare with.
 */
DECLINLINE(int) rtstrFormatTypeCompare(const char *pszType, size_t cchType, PCRTSTRDYNFMT pType)
{
    size_t cch = RT_MIN(cchType, pType->cchType);
    int iDiff = memcmp(pszType, pType->szType, cch);
    if (!iDiff)
    {
        if (cchType == pType->cchType)
            return 0;
        iDiff = cchType < pType->cchType ? -1 : 1;
    }
    return iDiff;
}


/**
 * Looks up a type entry.
 *
 * @returns The type index, -1 on failure.
 * @param   pszType     The type to look up. This doesn't have to be terminated.
 * @param   cchType     The length of the type.
 */
DECLINLINE(int32_t) rtstrFormatTypeLookup(const char *pszType, size_t cchType)
{
    /*
     * Lookup the type - binary search.
     */
    int32_t iStart = 0;
    int32_t iEnd   = g_cTypes - 1;
    int32_t i      = iEnd / 2;
    for (;;)
    {
        int iDiff = rtstrFormatTypeCompare(pszType, cchType, &g_aTypes[i]);
        if (!iDiff)
            return i;
        if (iEnd == iStart)
            break;
        if (iDiff < 0)
            iEnd = i - 1;
        else
            iStart = i + 1;
        if (iEnd < iStart)
            break;
        i = iStart + (iEnd - iStart) / 2;
    }
    return -1;
}


/**
 * Register a format handler for a type.
 *
 * The format handler is used to handle '%R[type]' format types, where the argument
 * in the vector is a pointer value (a bit restrictive, but keeps it simple).
 *
 * The caller must ensure that no other thread will be making use of any of
 * the dynamic formatting type facilities simultaneously with this call.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ALREADY_EXISTS if the type has already been registered.
 * @retval  VERR_TOO_MANY_OPEN_FILES if all the type slots has been allocated already.
 *
 * @param   pszType         The type name.
 * @param   pfnHandler      The handler address. See FNRTSTRFORMATTYPE for details.
 * @param   pvUser          The user argument to pass to the handler. See RTStrFormatTypeSetUser
 *                          for how to update this later.
 */
RTDECL(int) RTStrFormatTypeRegister(const char *pszType, PFNRTSTRFORMATTYPE pfnHandler, void *pvUser)
{
    int rc;
    size_t cchType;
    uint32_t cTypes;

    /*
     * Validate input.
     */
    AssertPtr(pfnHandler);
    AssertPtr(pszType);
    cchType = strlen(pszType);
    AssertReturn(cchType < RT_SIZEOFMEMB(RTSTRDYNFMT, szType), VERR_INVALID_PARAMETER);

    /*
     * Try add it.
     */
    rtstrFormatTypeWriteLock();

    /* check that there are empty slots. */
    cTypes = g_cTypes;
    if (cTypes < RT_ELEMENTS(g_aTypes))
    {
        /* find where to insert it. */
        uint32_t i = 0;
        rc = VINF_SUCCESS;
        while (i < cTypes)
        {
            int iDiff = rtstrFormatTypeCompare(pszType, cchType, &g_aTypes[i]);
            if (!iDiff)
            {
                rc = VERR_ALREADY_EXISTS;
                break;
            }
            if (iDiff < 0)
                break;
            i++;
        }
        if (RT_SUCCESS(rc))
        {
            /* make room. */
            uint32_t cToMove = cTypes - i;
            if (cToMove)
                memmove(&g_aTypes[i + 1], &g_aTypes[i], cToMove * sizeof(g_aTypes[i]));

            /* insert the new entry. */
            memset(&g_aTypes[i], 0, sizeof(g_aTypes[i]));
            memcpy(&g_aTypes[i].szType[0], pszType, cchType + 1);
            g_aTypes[i].cchType = (uint8_t)cchType;
            g_aTypes[i].pvUser = pvUser;
#ifdef IN_RC
            g_aTypes[i].offHandler = (intptr_t)pfnHandler - (intptr_t)&g_aTypes[0];
#else
            g_aTypes[i].pfnHandler = pfnHandler;
#endif
            ASMAtomicIncU32(&g_cTypes);
            rc = VINF_SUCCESS;
        }
    }
    else
        rc = VERR_TOO_MANY_OPEN_FILES; /** @todo fix error code */

    rtstrFormatTypeWriteUnlock();

    return rc;
}
RT_EXPORT_SYMBOL(RTStrFormatTypeRegister);


/**
 * Deregisters a format type.
 *
 * The caller must ensure that no other thread will be making use of any of
 * the dynamic formatting type facilities simultaneously with this call.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 *
 * @param   pszType     The type to deregister.
 */
RTDECL(int) RTStrFormatTypeDeregister(const char *pszType)
{
    int32_t i;

    /*
     * Validate input.
     */
    AssertPtr(pszType);

    /*
     * Locate the entry and remove it.
     */
    rtstrFormatTypeWriteLock();
    i = rtstrFormatTypeLookup(pszType, strlen(pszType));
    if (i >= 0)
    {
        const uint32_t cTypes = g_cTypes;
        int32_t cToMove = cTypes - i - 1;
        if (cToMove > 0)
            memmove(&g_aTypes[i], &g_aTypes[i + 1], cToMove * sizeof(g_aTypes[i]));
        memset(&g_aTypes[cTypes - 1], 0, sizeof(g_aTypes[0]));
        ASMAtomicDecU32(&g_cTypes);
    }
    rtstrFormatTypeWriteUnlock();

    Assert(i >= 0);
    return i >= 0
         ? VINF_SUCCESS
         : VERR_FILE_NOT_FOUND; /** @todo fix status code */
}
RT_EXPORT_SYMBOL(RTStrFormatTypeDeregister);


/**
 * Sets the user argument for a type.
 *
 * This can be used if a user argument needs relocating in GC.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 *
 * @param   pszType     The type to update.
 * @param   pvUser      The new user argument value.
 */
RTDECL(int) RTStrFormatTypeSetUser(const char *pszType, void *pvUser)
{
    int32_t i;

    /*
     * Validate input.
     */
    AssertPtr(pszType);

    /*
     * Locate the entry and update it.
     */
    rtstrFormatTypeReadLock();

    i = rtstrFormatTypeLookup(pszType, strlen(pszType));
    if (i >= 0)
        ASMAtomicWritePtr(&g_aTypes[i].pvUser, pvUser);

    rtstrFormatTypeReadUnlock();

    Assert(i >= 0);
    return i >= 0
         ? VINF_SUCCESS
         : VERR_FILE_NOT_FOUND; /** @todo fix status code */
}
RT_EXPORT_SYMBOL(RTStrFormatTypeSetUser);


/**
 * Formats a type using a registered callback handler.
 *
 * This will handle %R[type].
 *
 * @returns The number of bytes formatted.
 * @param   pfnOutput       Pointer to output function.
 * @param   pvArgOutput     Argument for the output function.
 * @param   ppszFormat      Pointer to the format string pointer. Advance this till the char
 *                          after the format specifier.
 * @param   pArgs           Pointer to the argument list. Use this to fetch the arguments.
 * @param   cchWidth        Format Width. -1 if not specified.
 * @param   cchPrecision    Format Precision. -1 if not specified.
 * @param   fFlags          Flags (RTSTR_NTFS_*).
 * @param   chArgSize       The argument size specifier, 'l' or 'L'.
 */
DECLHIDDEN(size_t) rtstrFormatType(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, const char **ppszFormat,
                                   va_list *pArgs, int cchWidth, int cchPrecision, unsigned fFlags, char chArgSize)
{
    size_t      cch;
    int32_t     i;
    char const *pszTypeEnd;
    char const *pszType;
    char        ch;
    void       *pvValue = va_arg(*pArgs, void *);
    NOREF(chArgSize);

    /*
     * Parse out the type.
     */
    pszType = *ppszFormat + 2;
    *ppszFormat = pszType;
    Assert(pszType[-1] == '[');
    Assert(pszType[-2] == 'R');
    pszTypeEnd = pszType;
    while ((ch = *pszTypeEnd) != ']')
    {
        AssertReturn(ch != '\0', 0);
        AssertReturn(ch != '%', 0);
        AssertReturn(ch != '[', 0);
        pszTypeEnd++;
    }
    *ppszFormat = pszTypeEnd + 1;

    /*
     * Locate the entry and call the handler.
     */
    rtstrFormatTypeReadLock();

    i = rtstrFormatTypeLookup(pszType, pszTypeEnd - pszType);
    if (RT_LIKELY(i >= 0))
    {
#ifdef IN_RC
        PFNRTSTRFORMATTYPE pfnHandler = (PFNRTSTRFORMATTYPE)((intptr_t)&g_aTypes[0] + g_aTypes[i].offHandler);
#else
        PFNRTSTRFORMATTYPE pfnHandler = g_aTypes[i].pfnHandler;
#endif
        void *pvUser = ASMAtomicReadPtr(&g_aTypes[i].pvUser);

        rtstrFormatTypeReadUnlock();

        cch = pfnHandler(pfnOutput, pvArgOutput, g_aTypes[i].szType, pvValue, cchWidth, cchPrecision, fFlags, pvUser);
    }
    else
    {
        rtstrFormatTypeReadUnlock();

        cch  = pfnOutput(pvArgOutput, RT_STR_TUPLE("<missing:%R["));
        cch += pfnOutput(pvArgOutput, pszType, pszTypeEnd - pszType);
        cch += pfnOutput(pvArgOutput, RT_STR_TUPLE("]>"));
    }

    return cch;
}

