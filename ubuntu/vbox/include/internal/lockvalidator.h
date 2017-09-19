/* $Id: lockvalidator.h $ */
/** @file
 * IPRT - Internal RTLockValidator header.
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

#ifndef ___iprt_internal_lockvalidator_h
#define ___iprt_internal_lockvalidator_h

#include <iprt/types.h>
#include <iprt/lockvalidator.h>

RT_C_DECLS_BEGIN


/**
 * Record used only on the lock stack for recording the stack and source
 * position of a recursive lock acquisition.
 */
typedef struct RTLOCKVALRECNEST
{
    RTLOCKVALRECCORE                Core;
    /** The recursion level at this point in the stack. */
    uint32_t                        cRecursion;
    /** Pointer to the next record on the stack. */
    PRTLOCKVALRECUNION volatile     pDown;
    /** Pointer to the first recursion. */
    PRTLOCKVALRECUNION volatile     pRec;
    /** Pointer to the next free record when in the
     *  RTLOCKVALPERTHREAD::pFreeNestRecs list. */
    struct RTLOCKVALRECNEST        *pNextFree;
    /** The source position. */
    RTLOCKVALSRCPOS                 SrcPos;
} RTLOCKVALRECNEST;
/** Pointer to a recursion record. */
typedef RTLOCKVALRECNEST *PRTLOCKVALRECNEST;


/**
 * Record union for simplifying internal processing.
 */
typedef union RTLOCKVALRECUNION
{
    RTLOCKVALRECCORE                Core;
    RTLOCKVALRECEXCL                Excl;
    RTLOCKVALRECSHRD                Shared;
    RTLOCKVALRECSHRDOWN             ShrdOwner;
    RTLOCKVALRECNEST                Nest;
} RTLOCKVALRECUNION;


/**
 * Per thread data for the lock validator.
 *
 * This is part of the RTTHREADINT structure.
 */
typedef struct RTLOCKVALPERTHREAD
{
    /** Where we are blocking. */
    RTLOCKVALSRCPOS                 SrcPos;
    /** Top of the lock stack. */
    PRTLOCKVALRECUNION volatile     pStackTop;
    /** List of free recursion (nesting) record. */
    PRTLOCKVALRECNEST               pFreeNestRecs;
    /** What we're blocking on.
     * The lock validator sets this, RTThreadUnblock clears it. */
    PRTLOCKVALRECUNION volatile     pRec;
    /** The state in which pRec that goes with pRec.
     * RTThreadUnblocking uses this to figure out when to clear pRec. */
    RTTHREADSTATE volatile          enmRecState;
    /** The thread is running inside the lock validator. */
    bool volatile                   fInValidator;
    /** Reserved for alignment purposes. */
    bool                            afReserved[3];
    /** Number of registered write locks, mutexes and critsects that this thread owns. */
    int32_t volatile                cWriteLocks;
    /** Number of registered read locks that this thread owns, nesting included. */
    int32_t volatile                cReadLocks;
    /** Bitmap indicating which entires are free (set) and allocated (clear). */
    uint32_t volatile               bmFreeShrdOwners;
    /** Reserved for alignment purposes. */
    uint32_t                        u32Reserved;
    /** Statically allocated shared owner records */
    RTLOCKVALRECSHRDOWN             aShrdOwners[32];
} RTLOCKVALPERTHREAD;


DECLHIDDEN(void)    rtLockValidatorInitPerThread(RTLOCKVALPERTHREAD *pPerThread);
DECLHIDDEN(void)    rtLockValidatorDeletePerThread(RTLOCKVALPERTHREAD *pPerThread);
DECLHIDDEN(void)    rtLockValidatorSerializeDestructEnter(void);
DECLHIDDEN(void)    rtLockValidatorSerializeDestructLeave(void);

RT_C_DECLS_END

#endif

