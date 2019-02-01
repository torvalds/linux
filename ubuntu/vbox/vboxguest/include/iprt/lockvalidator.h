/** @file
 * IPRT - Lock Validator.
 */

/*
 * Copyright (C) 2009-2019 Oracle Corporation
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

#ifndef IPRT_INCLUDED_lockvalidator_h
#define IPRT_INCLUDED_lockvalidator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/stdarg.h>


/** @defgroup grp_rtlockval     RTLockValidator - Lock Validator
 * @ingroup grp_rt
 * @{
 */

RT_C_DECLS_BEGIN

/** Pointer to a record union.
 * @internal  */
typedef union RTLOCKVALRECUNION *PRTLOCKVALRECUNION;

/**
 * Source position.
 */
typedef struct RTLOCKVALSRCPOS
{
    /** The file where the lock was taken. */
    R3R0PTRTYPE(const char * volatile)  pszFile;
    /** The function where the lock was taken. */
    R3R0PTRTYPE(const char * volatile)  pszFunction;
    /** Some ID indicating where the lock was taken, typically an address. */
    RTHCUINTPTR volatile                uId;
    /** The line number in the file. */
    uint32_t volatile                   uLine;
#if HC_ARCH_BITS == 64
    uint32_t                            u32Padding; /**< Alignment padding. */
#endif
} RTLOCKVALSRCPOS;
AssertCompileSize(RTLOCKVALSRCPOS, HC_ARCH_BITS == 32 ? 16 : 32);
/* The pointer types are defined in iprt/types.h. */

/** @def RTLOCKVALSRCPOS_INIT
 * Initializer for a RTLOCKVALSRCPOS variable.
 *
 * @param   pszFile         The file name.  Optional (NULL).
 * @param   uLine           The line number in that file. Optional (0).
 * @param   pszFunction     The function.  Optional (NULL).
 * @param   uId             Some location ID, normally the return address.
 *                          Optional (NULL).
 */
#if HC_ARCH_BITS == 64
# define RTLOCKVALSRCPOS_INIT(pszFile, uLine, pszFunction, uId) \
    { (pszFile), (pszFunction), (uId), (uLine), 0 }
#else
# define RTLOCKVALSRCPOS_INIT(pszFile, uLine, pszFunction, uId) \
    { (pszFile), (pszFunction), (uId), (uLine) }
#endif

/** @def RTLOCKVALSRCPOS_INIT_DEBUG_API
 * Initializer for a RTLOCKVALSRCPOS variable in a typicial debug API
 * variant.  Assumes RT_SRC_POS_DECL and RTHCUINTPTR uId as arguments.
 */
#define RTLOCKVALSRCPOS_INIT_DEBUG_API()  \
    RTLOCKVALSRCPOS_INIT(pszFile, iLine, pszFunction, uId)

/** @def RTLOCKVALSRCPOS_INIT_NORMAL_API
 * Initializer for a RTLOCKVALSRCPOS variable in a normal API
 * variant.  Assumes iprt/asm.h is included.
 */
#define RTLOCKVALSRCPOS_INIT_NORMAL_API() \
    RTLOCKVALSRCPOS_INIT(__FILE__, __LINE__, __PRETTY_FUNCTION__, (uintptr_t)ASMReturnAddress())

/** @def RTLOCKVALSRCPOS_INIT_POS_NO_ID
 * Initializer for a RTLOCKVALSRCPOS variable when no @c uId is present.
 * Assumes iprt/asm.h is included.
 */
#define RTLOCKVALSRCPOS_INIT_POS_NO_ID() \
    RTLOCKVALSRCPOS_INIT(pszFile, iLine, pszFunction, (uintptr_t)ASMReturnAddress())


/**
 * Lock validator record core.
 */
typedef struct RTLOCKVALRECORE
{
    /** The magic value indicating the record type. */
    uint32_t volatile                   u32Magic;
} RTLOCKVALRECCORE;
/** Pointer to a lock validator record core. */
typedef RTLOCKVALRECCORE *PRTLOCKVALRECCORE;
/** Pointer to a const lock validator record core. */
typedef RTLOCKVALRECCORE const *PCRTLOCKVALRECCORE;


/**
 * Record recording the exclusive ownership of a lock.
 *
 * This is typically part of the per-lock data structure when compiling with
 * the lock validator.
 */
typedef struct RTLOCKVALRECEXCL
{
    /** Record core with RTLOCKVALRECEXCL_MAGIC as the magic value. */
    RTLOCKVALRECCORE                    Core;
    /** Whether it's enabled or not. */
    bool                                fEnabled;
    /** Reserved. */
    bool                                afReserved[3];
    /** Source position where the lock was taken. */
    RTLOCKVALSRCPOS                     SrcPos;
    /** The current owner thread. */
    RTTHREAD volatile                   hThread;
    /** Pointer to the lock record below us. Only accessed by the owner. */
    R3R0PTRTYPE(PRTLOCKVALRECUNION)     pDown;
    /** Recursion count */
    uint32_t                            cRecursion;
    /** The lock sub-class. */
    uint32_t volatile                   uSubClass;
    /** The lock class. */
    RTLOCKVALCLASS                      hClass;
    /** Pointer to the lock. */
    RTHCPTR                             hLock;
    /** Pointer to the next sibling record.
     * This is used to find the read side of a read-write lock.  */
    R3R0PTRTYPE(PRTLOCKVALRECUNION)     pSibling;
    /** The lock name.
     * @remarks The bytes beyond 32 are for better size alignment and can be
     *          taken and used for other purposes if it becomes necessary. */
    char                                szName[32 + (HC_ARCH_BITS == 32 ? 12 : 8)];
} RTLOCKVALRECEXCL;
AssertCompileSize(RTLOCKVALRECEXCL, HC_ARCH_BITS == 32 ? 0x60 : 0x80);
/* The pointer type is defined in iprt/types.h. */

/**
 * For recording the one ownership share.
 */
typedef struct RTLOCKVALRECSHRDOWN
{
    /** Record core with RTLOCKVALRECSHRDOWN_MAGIC as the magic value. */
    RTLOCKVALRECCORE                    Core;
    /** Recursion count */
    uint16_t                            cRecursion;
    /** Static (true) or dynamic (false) allocated record. */
    bool                                fStaticAlloc;
    /** Reserved. */
    bool                                fReserved;
    /** The current owner thread. */
    RTTHREAD volatile                   hThread;
    /** Pointer to the lock record below us. Only accessed by the owner. */
    R3R0PTRTYPE(PRTLOCKVALRECUNION)     pDown;
    /** Pointer back to the shared record. */
    R3R0PTRTYPE(PRTLOCKVALRECSHRD)      pSharedRec;
#if HC_ARCH_BITS == 32
    /** Reserved. */
    RTHCPTR                             pvReserved;
#endif
    /** Source position where the lock was taken. */
    RTLOCKVALSRCPOS                     SrcPos;
} RTLOCKVALRECSHRDOWN;
AssertCompileSize(RTLOCKVALRECSHRDOWN, HC_ARCH_BITS == 32 ? 24 + 16 : 32 + 32);
/** Pointer to a RTLOCKVALRECSHRDOWN. */
typedef RTLOCKVALRECSHRDOWN *PRTLOCKVALRECSHRDOWN;

/**
 * Record recording the shared ownership of a lock.
 *
 * This is typically part of the per-lock data structure when compiling with
 * the lock validator.
 */
typedef struct RTLOCKVALRECSHRD
{
    /** Record core with RTLOCKVALRECSHRD_MAGIC as the magic value. */
    RTLOCKVALRECCORE                    Core;
    /** The lock sub-class. */
    uint32_t volatile                   uSubClass;
    /** The lock class. */
    RTLOCKVALCLASS                      hClass;
    /** Pointer to the lock. */
    RTHCPTR                             hLock;
    /** Pointer to the next sibling record.
     * This is used to find the write side of a read-write lock.  */
    R3R0PTRTYPE(PRTLOCKVALRECUNION)     pSibling;

    /** The number of entries in the table.
     * Updated before inserting and after removal. */
    uint32_t volatile                   cEntries;
    /** The index of the last entry (approximately). */
    uint32_t volatile                   iLastEntry;
    /** The max table size. */
    uint32_t volatile                   cAllocated;
    /** Set if the table is being reallocated, clear if not.
     * This is used together with rtLockValidatorSerializeDetectionEnter to make
     * sure there is exactly one thread doing the reallocation and that nobody is
     * using the table at that point. */
    bool volatile                       fReallocating;
    /** Whether it's enabled or not. */
    bool                                fEnabled;
    /** Set if event semaphore signaller, clear if read-write semaphore. */
    bool                                fSignaller;
    /** Alignment padding. */
    bool                                fPadding;
    /** Pointer to a table containing pointers to records of all the owners. */
    R3R0PTRTYPE(PRTLOCKVALRECSHRDOWN volatile *) papOwners;

    /** The lock name.
     * @remarks The bytes beyond 32 are for better size alignment and can be
     *          taken and used for other purposes if it becomes necessary. */
    char                                szName[32 + (HC_ARCH_BITS == 32 ? 8 : 8)];
} RTLOCKVALRECSHRD;
AssertCompileSize(RTLOCKVALRECSHRD, HC_ARCH_BITS == 32 ? 0x50 : 0x60);


/**
 * Makes the two records siblings.
 *
 * @returns VINF_SUCCESS on success, VERR_SEM_LV_INVALID_PARAMETER if either of
 *          the records are invalid.
 * @param   pRec1               Record 1.
 * @param   pRec2               Record 2.
 */
RTDECL(int) RTLockValidatorRecMakeSiblings(PRTLOCKVALRECCORE pRec1, PRTLOCKVALRECCORE pRec2);

/**
 * Initialize a lock validator record.
 *
 * Use RTLockValidatorRecExclDelete to deinitialize it.
 *
 * @param   pRec                The record.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(void) RTLockValidatorRecExclInit(PRTLOCKVALRECEXCL pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass, void *hLock,
                                        bool fEnabled, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(6, 7);
/**
 * Initialize a lock validator record.
 *
 * Use RTLockValidatorRecExclDelete to deinitialize it.
 *
 * @param   pRec                The record.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   va                  Format string arguments.
 */
RTDECL(void) RTLockValidatorRecExclInitV(PRTLOCKVALRECEXCL pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass, void *hLock,
                                         bool fEnabled, const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(6, 0);
/**
 * Uninitialize a lock validator record previously initialized by
 * RTLockRecValidatorInit.
 *
 * @param   pRec                The record.  Must be valid.
 */
RTDECL(void) RTLockValidatorRecExclDelete(PRTLOCKVALRECEXCL pRec);

/**
 * Create and initialize a lock validator record.
 *
 * Use RTLockValidatorRecExclDestroy to deinitialize and destroy the returned
 * record.
 *
 * @return VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   ppRec               Where to return the record pointer.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int)  RTLockValidatorRecExclCreate(PRTLOCKVALRECEXCL *ppRec, RTLOCKVALCLASS hClass, uint32_t uSubClass, void *hLock,
                                          bool fEnabled, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(6, 7);

/**
 * Create and initialize a lock validator record.
 *
 * Use RTLockValidatorRecExclDestroy to deinitialize and destroy the returned
 * record.
 *
 * @return VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   ppRec               Where to return the record pointer.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   va                  Format string arguments.
 */
RTDECL(int)  RTLockValidatorRecExclCreateV(PRTLOCKVALRECEXCL *ppRec, RTLOCKVALCLASS hClass, uint32_t uSubClass, void *hLock,
                                           bool fEnabled, const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(6, 0);

/**
 * Deinitialize and destroy a record created by RTLockValidatorRecExclCreate.
 *
 * @param   ppRec               Pointer to the record pointer.  Will be set to
 *                              NULL.
 */
RTDECL(void) RTLockValidatorRecExclDestroy(PRTLOCKVALRECEXCL *ppRec);

/**
 * Sets the sub-class of the record.
 *
 * It is recommended to try make sure that nobody is using this class while
 * changing the value.
 *
 * @returns The old sub-class.  RTLOCKVAL_SUB_CLASS_INVALID is returns if the
 *          lock validator isn't compiled in or either of the parameters are
 *          invalid.
 * @param   pRec                The validator record.
 * @param   uSubClass           The new sub-class value.
 */
RTDECL(uint32_t) RTLockValidatorRecExclSetSubClass(PRTLOCKVALRECEXCL pRec, uint32_t uSubClass);

/**
 * Record the specified thread as lock owner and increment the write lock count.
 *
 * This function is typically called after acquiring the lock.  It accounts for
 * recursions so it can be used instead of RTLockValidatorRecExclRecursion.  Use
 * RTLockValidatorRecExclReleaseOwner to reverse the effect.
 *
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The handle of the calling thread.  If not known,
 *                              pass NIL_RTTHREAD and we'll figure it out.
 * @param   pSrcPos             The source position of the lock operation.
 * @param   fFirstRecursion     Set if it is the first recursion, clear if not
 *                              sure.
 */
RTDECL(void) RTLockValidatorRecExclSetOwner(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                            PCRTLOCKVALSRCPOS pSrcPos, bool fFirstRecursion);

/**
 * Check the exit order and release (unset) the ownership.
 *
 * This is called by routines implementing releasing an exclusive lock,
 * typically before getting down to the final lock releasing.  Can be used for
 * recursive releasing instead of RTLockValidatorRecExclUnwind.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_RELEASE_ORDER if the order is wrong.  Will have
 *          done all necessary whining and breakpointing before returning.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   fFinalRecursion     Set if it's the final recursion, clear if not
 *                              sure.
 */
RTDECL(int) RTLockValidatorRecExclReleaseOwner(PRTLOCKVALRECEXCL pRec, bool fFinalRecursion);

/**
 * Clear the lock ownership and decrement the write lock count.
 *
 * This is only for special cases where we wish to drop lock validation
 * recording.  See RTLockValidatorRecExclCheckAndRelease.
 *
 * @param   pRec                The validator record.
 */
RTDECL(void) RTLockValidatorRecExclReleaseOwnerUnchecked(PRTLOCKVALRECEXCL pRec);

/**
 * Checks and records a lock recursion.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_NESTED if the semaphore class forbids recursion.  Gone
 *          thru the motions.
 * @retval  VERR_SEM_LV_WRONG_ORDER if the locking order is wrong.  Gone thru
 *          the motions.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   pSrcPos             The source position of the lock operation.
 */
RTDECL(int) RTLockValidatorRecExclRecursion(PRTLOCKVALRECEXCL pRec, PCRTLOCKVALSRCPOS pSrcPos);

/**
 * Checks and records a lock unwind (releasing one recursion).
 *
 * This should be coupled with called to RTLockValidatorRecExclRecursion.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_RELEASE_ORDER if the release order is wrong.  Gone
 *          thru the motions.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 */
RTDECL(int) RTLockValidatorRecExclUnwind(PRTLOCKVALRECEXCL pRec);

/**
 * Checks and records a mixed recursion.
 *
 * An example of a mixed recursion is a writer requesting read access to a
 * SemRW.
 *
 * This should be coupled with called to RTLockValidatorRecExclUnwindMixed.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_NESTED if the semaphore class forbids recursion.  Gone
 *          thru the motions.
 * @retval  VERR_SEM_LV_WRONG_ORDER if the locking order is wrong.  Gone thru
 *          the motions.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record it to accounted it to.
 * @param   pRecMixed           The validator record it came in on.
 * @param   pSrcPos             The source position of the lock operation.
 */
RTDECL(int) RTLockValidatorRecExclRecursionMixed(PRTLOCKVALRECEXCL pRec, PRTLOCKVALRECCORE pRecMixed, PCRTLOCKVALSRCPOS pSrcPos);

/**
 * Checks and records the unwinding of a mixed recursion.
 *
 * This should be coupled with called to RTLockValidatorRecExclRecursionMixed.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_RELEASE_ORDER if the release order is wrong.  Gone
 *          thru the motions.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record it was accounted to.
 * @param   pRecMixed           The validator record it came in on.
 */
RTDECL(int) RTLockValidatorRecExclUnwindMixed(PRTLOCKVALRECEXCL pRec, PRTLOCKVALRECCORE pRecMixed);

/**
 * Check the exclusive locking order.
 *
 * This is called by routines implementing exclusive lock acquisition.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_ORDER if the order is wrong.  Will have done all
 *          necessary whining and breakpointing before returning.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The handle of the calling thread.  If not known,
 *                              pass NIL_RTTHREAD and we'll figure it out.
 * @param   pSrcPos             The source position of the lock operation.
 * @param   cMillies            The timeout, in milliseconds.
 */
RTDECL(int)  RTLockValidatorRecExclCheckOrder(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                              PCRTLOCKVALSRCPOS pSrcPos, RTMSINTERVAL cMillies);

/**
 * Do deadlock detection before blocking on exclusive access to a lock and
 * change the thread state.
 *
 * @retval  VINF_SUCCESS - thread is in the specified sleep state.
 * @retval  VERR_SEM_LV_DEADLOCK if blocking would deadlock.  Gone thru the
 *          motions.
 * @retval  VERR_SEM_LV_NESTED if the semaphore isn't recursive and hThread is
 *          already the owner.  Gone thru the motions.
 * @retval  VERR_SEM_LV_ILLEGAL_UPGRADE if it's a deadlock on the same lock.
 *          The caller must handle any legal upgrades without invoking this
 *          function (for now).
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record we're blocking on.
 * @param   hThreadSelf         The current thread.  Shall not be NIL_RTTHREAD!
 * @param   pSrcPos             The source position of the lock operation.
 * @param   fRecursiveOk        Whether it's ok to recurse.
 * @param   cMillies            The timeout, in milliseconds.
 * @param   enmSleepState       The sleep state to enter on successful return.
 * @param   fReallySleeping     Is it really going to sleep now or not.  Use
 *                              false before calls to other IPRT synchronization
 *                              methods.
 */
RTDECL(int) RTLockValidatorRecExclCheckBlocking(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                                PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                RTTHREADSTATE enmSleepState, bool fReallySleeping);

/**
 * RTLockValidatorRecExclCheckOrder and RTLockValidatorRecExclCheckBlocking
 * baked into one call.
 *
 * @returns Any of the statuses returned by the two APIs.
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The current thread.  Shall not be NIL_RTTHREAD!
 * @param   pSrcPos             The source position of the lock operation.
 * @param   fRecursiveOk        Whether it's ok to recurse.
 * @param   cMillies            The timeout, in milliseconds.
 * @param   enmSleepState       The sleep state to enter on successful return.
 * @param   fReallySleeping     Is it really going to sleep now or not.  Use
 *                              false before calls to other IPRT synchronization
 *                              methods.
 */
RTDECL(int) RTLockValidatorRecExclCheckOrderAndBlocking(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                                        PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                        RTTHREADSTATE enmSleepState, bool fReallySleeping);

/**
 * Initialize a lock validator record for a shared lock.
 *
 * Use RTLockValidatorRecSharedDelete to deinitialize it.
 *
 * @param   pRec                The shared lock record.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fSignaller          Set if event semaphore signaller logic should be
 *                              applied to this record, clear if read-write
 *                              semaphore logic should be used.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(void) RTLockValidatorRecSharedInit(PRTLOCKVALRECSHRD pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                          void *hLock, bool fSignaller, bool fEnabled,
                                          const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(7, 8);

/**
 * Initialize a lock validator record for a shared lock.
 *
 * Use RTLockValidatorRecSharedDelete to deinitialize it.
 *
 * @param   pRec                The shared lock record.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   hLock               The lock handle.
 * @param   fSignaller          Set if event semaphore signaller logic should be
 *                              applied to this record, clear if read-write
 *                              semaphore logic should be used.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   va                  Format string arguments.
 */
RTDECL(void) RTLockValidatorRecSharedInitV(PRTLOCKVALRECSHRD pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                           void *hLock, bool fSignaller, bool fEnabled,
                                           const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(7, 0);

/**
 * Uninitialize a lock validator record previously initialized by
 * RTLockValidatorRecSharedInit.
 *
 * @param   pRec                The shared lock record.  Must be valid.
 */
RTDECL(void) RTLockValidatorRecSharedDelete(PRTLOCKVALRECSHRD pRec);

/**
 * Create and initialize a lock validator record for a shared lock.
 *
 * Use RTLockValidatorRecSharedDestroy to deinitialize and destroy the returned
 * record.
 *
 * @returns IPRT status code.
 * @param   ppRec               Where to return the record pointer.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   pvLock              The lock handle or address.
 * @param   fSignaller          Set if event semaphore signaller logic should be
 *                              applied to this record, clear if read-write
 *                              semaphore logic should be used.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int) RTLockValidatorRecSharedCreate(PRTLOCKVALRECSHRD *ppRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                           void *pvLock, bool fSignaller, bool fEnabled,
                                           const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(7, 8);

/**
 * Create and initialize a lock validator record for a shared lock.
 *
 * Use RTLockValidatorRecSharedDestroy to deinitialize and destroy the returned
 * record.
 *
 * @returns IPRT status code.
 * @param   ppRec               Where to return the record pointer.
 * @param   hClass              The class (no reference consumed). If NIL, the
 *                              no lock order validation will be performed on
 *                              this lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order inside the same class.  If you don't know,
 *                              then pass RTLOCKVAL_SUB_CLASS_NONE.
 * @param   pvLock              The lock handle or address.
 * @param   fSignaller          Set if event semaphore signaller logic should be
 *                              applied to this record, clear if read-write
 *                              semaphore logic should be used.
 * @param   fEnabled            Pass @c false to explicitly disable lock
 *                              validation, otherwise @c true.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL). Max length is 32 bytes.
 * @param   va                  Format string arguments.
 */
RTDECL(int) RTLockValidatorRecSharedCreateV(PRTLOCKVALRECSHRD *ppRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                            void *pvLock, bool fSignaller, bool fEnabled,
                                            const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(7, 0);

/**
 * Deinitialize and destroy a record created by RTLockValidatorRecSharedCreate.
 *
 * @param   ppRec               Pointer to the record pointer.  Will be set to
 *                              NULL.
 */
RTDECL(void) RTLockValidatorRecSharedDestroy(PRTLOCKVALRECSHRD *ppRec);

/**
 * Sets the sub-class of the record.
 *
 * It is recommended to try make sure that nobody is using this class while
 * changing the value.
 *
 * @returns The old sub-class.  RTLOCKVAL_SUB_CLASS_INVALID is returns if the
 *          lock validator isn't compiled in or either of the parameters are
 *          invalid.
 * @param   pRec                The validator record.
 * @param   uSubClass           The new sub-class value.
 */
RTDECL(uint32_t) RTLockValidatorRecSharedSetSubClass(PRTLOCKVALRECSHRD pRec, uint32_t uSubClass);

/**
 * Check the shared locking order.
 *
 * This is called by routines implementing shared lock acquisition.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_ORDER if the order is wrong.  Will have done all
 *          necessary whining and breakpointing before returning.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The handle of the calling thread.  If not known,
 *                              pass NIL_RTTHREAD and we'll figure it out.
 * @param   pSrcPos             The source position of the lock operation.
 * @param   cMillies            Intended sleep time in milliseconds.
 */
RTDECL(int)  RTLockValidatorRecSharedCheckOrder(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                                PCRTLOCKVALSRCPOS pSrcPos, RTMSINTERVAL cMillies);

/**
 * Do deadlock detection before blocking on shared access to a lock and change
 * the thread state.
 *
 * @retval  VINF_SUCCESS - thread is in the specified sleep state.
 * @retval  VERR_SEM_LV_DEADLOCK if blocking would deadlock.  Gone thru the
 *          motions.
 * @retval  VERR_SEM_LV_NESTED if the semaphore isn't recursive and hThread is
 *          already the owner.  Gone thru the motions.
 * @retval  VERR_SEM_LV_ILLEGAL_UPGRADE if it's a deadlock on the same lock.
 *          The caller must handle any legal upgrades without invoking this
 *          function (for now).
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record we're blocking on.
 * @param   hThreadSelf         The current thread.  Shall not be NIL_RTTHREAD!
 * @param   pSrcPos             The source position of the lock operation.
 * @param   fRecursiveOk        Whether it's ok to recurse.
 * @param   cMillies            Intended sleep time in milliseconds.
 * @param   enmSleepState       The sleep state to enter on successful return.
 * @param   fReallySleeping     Is it really going to sleep now or not.  Use
 *                              false before calls to other IPRT synchronization
 *                              methods.
 */
RTDECL(int) RTLockValidatorRecSharedCheckBlocking(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                                  PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                  RTTHREADSTATE enmSleepState, bool fReallySleeping);

/**
 * RTLockValidatorRecSharedCheckOrder and RTLockValidatorRecSharedCheckBlocking
 * baked into one call.
 *
 * @returns Any of the statuses returned by the two APIs.
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The current thread.  Shall not be NIL_RTTHREAD!
 * @param   pSrcPos             The source position of the lock operation.
 * @param   fRecursiveOk        Whether it's ok to recurse.
 * @param   cMillies            Intended sleep time in milliseconds.
 * @param   enmSleepState       The sleep state to enter on successful return.
 * @param   fReallySleeping     Is it really going to sleep now or not.  Use
 *                              false before calls to other IPRT synchronization
 *                              methods.
 */
RTDECL(int) RTLockValidatorRecSharedCheckOrderAndBlocking(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                                          PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                          RTTHREADSTATE enmSleepState, bool fReallySleeping);

/**
 * Removes all current owners and makes hThread the only owner.
 *
 * @param   pRec                The validator record.
 * @param   hThread             The thread handle of the owner.  NIL_RTTHREAD is
 *                              an alias for the current thread.
 * @param   pSrcPos             The source position of the lock operation.
 */
RTDECL(void) RTLockValidatorRecSharedResetOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread, PCRTLOCKVALSRCPOS pSrcPos);

/**
 * Adds an owner to a shared locking record.
 *
 * Takes recursion into account.  This function is typically called after
 * acquiring the lock in shared mode.
 *
 * @param   pRec                The validator record.
 * @param   hThread             The thread handle of the owner.  NIL_RTTHREAD is
 *                              an alias for the current thread.
 * @param   pSrcPos             The source position of the lock operation.
 */
RTDECL(void) RTLockValidatorRecSharedAddOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread, PCRTLOCKVALSRCPOS pSrcPos);

/**
 * Removes an owner from a shared locking record.
 *
 * Takes recursion into account.  This function is typically called before
 * releasing the lock.
 *
 * @param   pRec                The validator record.
 * @param   hThread             The thread handle of the owner.  NIL_RTTHREAD is
 *                              an alias for the current thread.
 */
RTDECL(void) RTLockValidatorRecSharedRemoveOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread);

/**
 * Checks if the specified thread is one of the owners.
 *
 * @returns true if it is, false if not.
 *
 * @param   pRec                The validator record.
 * @param   hThread             The thread handle of the owner.  NIL_RTTHREAD is
 *                              an alias for the current thread.
 */
RTDECL(bool) RTLockValidatorRecSharedIsOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread);

/**
 * Check the exit order and release (unset) the shared ownership.
 *
 * This is called by routines implementing releasing the read/write lock.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_WRONG_RELEASE_ORDER if the order is wrong.  Will have
 *          done all necessary whining and breakpointing before returning.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The handle of the calling thread.  NIL_RTTHREAD
 *                              is an alias for the current thread.
 */
RTDECL(int) RTLockValidatorRecSharedCheckAndRelease(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf);

/**
 * Check the signaller of an event.
 *
 * This is called by routines implementing releasing the event semaphore (both
 * kinds).
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_LV_NOT_SIGNALLER if the thread is not in the record.  Will
 *          have done all necessary whining and breakpointing before returning.
 * @retval  VERR_SEM_LV_INVALID_PARAMETER if the input is invalid.
 *
 * @param   pRec                The validator record.
 * @param   hThreadSelf         The handle of the calling thread.  NIL_RTTHREAD
 *                              is an alias for the current thread.
 */
RTDECL(int) RTLockValidatorRecSharedCheckSignaller(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf);

/**
 * Gets the number of write locks and critical sections the specified
 * thread owns.
 *
 * This number does not include any nested lock/critect entries.
 *
 * Note that it probably will return 0 for non-strict builds since
 * release builds doesn't do unnecessary diagnostic counting like this.
 *
 * @returns Number of locks on success (0+) and VERR_INVALID_HANDLER on failure
 * @param   Thread          The thread we're inquiring about.
 * @remarks Will only work for strict builds.
 */
RTDECL(int32_t) RTLockValidatorWriteLockGetCount(RTTHREAD Thread);

/**
 * Works the THREADINT::cWriteLocks member, mostly internal.
 *
 * @param   Thread      The current thread.
 */
RTDECL(void) RTLockValidatorWriteLockInc(RTTHREAD Thread);

/**
 * Works the THREADINT::cWriteLocks member, mostly internal.
 *
 * @param   Thread      The current thread.
 */
RTDECL(void) RTLockValidatorWriteLockDec(RTTHREAD Thread);

/**
 * Gets the number of read locks the specified thread owns.
 *
 * Note that nesting read lock entry will be included in the
 * total sum. And that it probably will return 0 for non-strict
 * builds since release builds doesn't do unnecessary diagnostic
 * counting like this.
 *
 * @returns Number of read locks on success (0+) and VERR_INVALID_HANDLER on failure
 * @param   Thread          The thread we're inquiring about.
 */
RTDECL(int32_t) RTLockValidatorReadLockGetCount(RTTHREAD Thread);

/**
 * Works the THREADINT::cReadLocks member.
 *
 * @param   Thread      The current thread.
 */
RTDECL(void) RTLockValidatorReadLockInc(RTTHREAD Thread);

/**
 * Works the THREADINT::cReadLocks member.
 *
 * @param   Thread      The current thread.
 */
RTDECL(void) RTLockValidatorReadLockDec(RTTHREAD Thread);

/**
 * Query which lock the specified thread is waiting on.
 *
 * @returns The lock handle value or NULL.
 * @param   hThread     The thread in question.
 */
RTDECL(void *) RTLockValidatorQueryBlocking(RTTHREAD hThread);

/**
 * Checks if the thread is running in the lock validator after it has entered a
 * block state.
 *
 * @returns true if it is, false if it isn't.
 * @param   hThread     The thread in question.
 */
RTDECL(bool) RTLockValidatorIsBlockedThreadInValidator(RTTHREAD hThread);

/**
 * Checks if the calling thread is holding a lock in the specified class.
 *
 * @returns true if it holds a lock in the specific class, false if it
 *          doesn't.
 *
 * @param   hCurrentThread      The current thread.  Pass NIL_RTTHREAD if you're
 *                              lazy.
 * @param   hClass              The class.
 */
RTDECL(bool) RTLockValidatorHoldsLocksInClass(RTTHREAD hCurrentThread, RTLOCKVALCLASS hClass);

/**
 * Checks if the calling thread is holding a lock in the specified sub-class.
 *
 * @returns true if it holds a lock in the specific sub-class, false if it
 *          doesn't.
 *
 * @param   hCurrentThread      The current thread.  Pass NIL_RTTHREAD if you're
 *                              lazy.
 * @param   hClass              The class.
 * @param   uSubClass           The new sub-class value.
 */
RTDECL(bool) RTLockValidatorHoldsLocksInSubClass(RTTHREAD hCurrentThread, RTLOCKVALCLASS hClass, uint32_t uSubClass);



/**
 * Creates a new lock validator class, all properties specified.
 *
 * @returns IPRT status code
 * @param   phClass             Where to return the class handle.
 * @param   pSrcPos             The source position of the create call.
 * @param   fAutodidact         Whether the class should be allowed to teach
 *                              itself new locking order rules (true), or if the
 *                              user will teach it all it needs to know (false).
 * @param   fRecursionOk        Whether to allow lock recursion or not.
 * @param   fStrictReleaseOrder Enforce strict lock release order or not.
 * @param   cMsMinDeadlock      Used to raise the sleep interval at which
 *                              deadlock detection kicks in.  Minimum is 1 ms,
 *                              while RT_INDEFINITE_WAIT will disable it.
 * @param   cMsMinOrder         Used to raise the sleep interval at which lock
 *                              order validation kicks in.  Minimum is 1 ms,
 *                              while RT_INDEFINITE_WAIT will disable it.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   ...                 Format string arguments.
 *
 * @remarks The properties can be modified after creation by the
 *          RTLockValidatorClassSet* methods.
 */
RTDECL(int) RTLockValidatorClassCreateEx(PRTLOCKVALCLASS phClass, PCRTLOCKVALSRCPOS pSrcPos,
                                         bool fAutodidact, bool fRecursionOk, bool fStrictReleaseOrder,
                                         RTMSINTERVAL cMsMinDeadlock, RTMSINTERVAL cMsMinOrder,
                                         const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(8, 9);

/**
 * Creates a new lock validator class, all properties specified.
 *
 * @returns IPRT status code
 * @param   phClass             Where to return the class handle.
 * @param   pSrcPos             The source position of the create call.
 * @param   fAutodidact         Whether the class should be allowed to teach
 *                              itself new locking order rules (true), or if the
 *                              user will teach it all it needs to know (false).
 * @param   fRecursionOk        Whether to allow lock recursion or not.
 * @param   fStrictReleaseOrder Enforce strict lock release order or not.
 * @param   cMsMinDeadlock      Used to raise the sleep interval at which
 *                              deadlock detection kicks in.  Minimum is 1 ms,
 *                              while RT_INDEFINITE_WAIT will disable it.
 * @param   cMsMinOrder         Used to raise the sleep interval at which lock
 *                              order validation kicks in.  Minimum is 1 ms,
 *                              while RT_INDEFINITE_WAIT will disable it.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   va                  Format string arguments.
 *
 * @remarks The properties can be modified after creation by the
 *          RTLockValidatorClassSet* methods.
 */
RTDECL(int) RTLockValidatorClassCreateExV(PRTLOCKVALCLASS phClass, PCRTLOCKVALSRCPOS pSrcPos,
                                          bool fAutodidact, bool fRecursionOk, bool fStrictReleaseOrder,
                                          RTMSINTERVAL cMsMinDeadlock, RTMSINTERVAL cMsMinOrder,
                                          const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(8, 0);

/**
 * Creates a new lock validator class.
 *
 * @returns IPRT status code
 * @param   phClass             Where to return the class handle.
 * @param   fAutodidact         Whether the class should be allowed to teach
 *                              itself new locking order rules (true), or if the
 *                              user will teach it all it needs to know (false).
 * @param   SRC_POS             The source position where call is being made from.
 *                              Use RT_SRC_POS when possible.  Optional.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int) RTLockValidatorClassCreate(PRTLOCKVALCLASS phClass, bool fAutodidact, RT_SRC_POS_DECL,
                                       const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(6, 7);

/**
 * Creates a new lock validator class with a reference that is consumed by the
 * first call to RTLockValidatorClassRetain.
 *
 * This is tailored for use in the parameter list of a semaphore constructor.
 *
 * @returns Class handle with a reference that is automatically consumed by the
 *          first retainer.  NIL_RTLOCKVALCLASS if we run into trouble.
 *
 * @param   SRC_POS             The source position where call is being made from.
 *                              Use RT_SRC_POS when possible.  Optional.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(RTLOCKVALCLASS) RTLockValidatorClassCreateUnique(RT_SRC_POS_DECL,
                                                        const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(4, 5);

/**
 * Finds a class for the specified source position.
 *
 * @returns A handle to the class (not retained!) or NIL_RTLOCKVALCLASS.
 * @param   pSrcPos             The source position.
 */
RTDECL(RTLOCKVALCLASS) RTLockValidatorClassFindForSrcPos(PRTLOCKVALSRCPOS pSrcPos);

/**
 * Finds or creates a class given the source position.
 *
 * @returns Class handle (not retained!) or NIL_RTLOCKVALCLASS.
 * @param   SRC_POS             The source position where call is being made from.
 *                              Use RT_SRC_POS when possible.  Optional.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(RTLOCKVALCLASS) RTLockValidatorClassForSrcPos(RT_SRC_POS_DECL,
                                                     const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(4, 5);

/**
 * Retains a reference to a lock validator class.
 *
 * @returns New reference count; UINT32_MAX if the handle is invalid.
 * @param   hClass              Handle to the class.
 */
RTDECL(uint32_t) RTLockValidatorClassRetain(RTLOCKVALCLASS hClass);

/**
 * Releases a reference to a lock validator class.
 *
 * @returns New reference count. 0 if hClass is NIL_RTLOCKVALCLASS. UINT32_MAX
 *          if the handle is invalid.
 * @param   hClass              Handle to the class.
 */
RTDECL(uint32_t) RTLockValidatorClassRelease(RTLOCKVALCLASS hClass);

/**
 * Teaches the class @a hClass that locks in the class @a hPriorClass can be
 * held when taking a lock of class @a hClass
 *
 * @returns IPRT status.
 * @param   hClass              Handle to the pupil class.
 * @param   hPriorClass         Handle to the class that can be held prior to
 *                              taking a lock in the pupil class.  (No reference
 *                              is consumed.)
 */
RTDECL(int) RTLockValidatorClassAddPriorClass(RTLOCKVALCLASS hClass, RTLOCKVALCLASS hPriorClass);

/**
 * Enables or disables the strict release order enforcing.
 *
 * @returns IPRT status.
 * @param   hClass              Handle to the class to change.
 * @param   fEnabled            Enable it (true) or disable it (false).
 */
RTDECL(int) RTLockValidatorClassEnforceStrictReleaseOrder(RTLOCKVALCLASS hClass, bool fEnabled);

/**
 * Enables / disables the lock validator for new locks.
 *
 * @returns The old setting.
 * @param   fEnabled    The new setting.
 */
RTDECL(bool) RTLockValidatorSetEnabled(bool fEnabled);

/**
 * Is the lock validator enabled?
 *
 * @returns True if enabled, false if not.
 */
RTDECL(bool) RTLockValidatorIsEnabled(void);

/**
 * Controls whether the lock validator should be quiet or noisy (default).
 *
 * @returns The old setting.
 * @param   fQuiet              The new setting.
 */
RTDECL(bool) RTLockValidatorSetQuiet(bool fQuiet);

/**
 * Is the lock validator quiet or noisy?
 *
 * @returns True if it is quiet, false if noisy.
 */
RTDECL(bool) RTLockValidatorIsQuiet(void);

/**
 * Makes the lock validator panic (default) or not.
 *
 * @returns The old setting.
 * @param   fPanic              The new setting.
 */
RTDECL(bool) RTLockValidatorSetMayPanic(bool fPanic);

/**
 * Can the lock validator cause panic.
 *
 * @returns True if it can, false if not.
 */
RTDECL(bool) RTLockValidatorMayPanic(void);


RT_C_DECLS_END

/** @} */

#endif /* !IPRT_INCLUDED_lockvalidator_h */

