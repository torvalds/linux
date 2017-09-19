/** @file
 * IPRT - Runtime Init/Term.
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

#ifndef ___iprt_initterm_h
#define ___iprt_initterm_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt    IPRT C/C++ APIs
 * @{
 */

/** @defgroup grp_rt_initterm  RTInit/RTTerm - Initialization and Termination
 *
 * APIs for initializing and terminating the IPRT, optionally it can also
 * convert input arguments to UTF-8 (in ring-3).
 *
 * @sa RTOnce, RTOnceEx.
 *
 * @{
 */

#ifdef IN_RING3
/** @name RTR3Init flags (RTR3INIT_XXX).
 * @{ */
/** Try initialize SUPLib. */
#define RTR3INIT_FLAGS_SUPLIB       RT_BIT(0)
/** Initializing IPRT from a DLL. */
#define RTR3INIT_FLAGS_DLL          RT_BIT(1)
/** We are sharing a process space, so we need to behave. */
#define RTR3INIT_FLAGS_UNOBTRUSIVE  RT_BIT(2)
/** The caller ensures that the argument bector is UTF-8. */
#define RTR3INIT_FLAGS_UTF8_ARGV    RT_BIT(3)
/** Indicates that this is a standalone application without any additional
 * shared libraries in the application directory. Mainly windows loader mess. */
#define RTR3INIT_FLAGS_STANDALONE_APP RT_BIT(4)
/** @} */

/** @name RTR3InitEx version
 * @{ */
/** Version 1. */
#define RTR3INIT_VER_1              UINT32_C(1)
/** The current version. */
#define RTR3INIT_VER_CUR            RTR3INIT_VER_1
/** @} */

/**
 * Initializes the runtime library.
 *
 * @returns iprt status code.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 */
RTR3DECL(int) RTR3InitExeNoArguments(uint32_t fFlags);

/**
 * Initializes the runtime library.
 *
 * @returns iprt status code.
 * @param   cArgs           Pointer to the argument count.
 * @param   ppapszArgs      Pointer to the argument vector pointer.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 */
RTR3DECL(int) RTR3InitExe(int cArgs, char ***ppapszArgs, uint32_t fFlags);

/**
 * Initializes the runtime library.
 *
 * @returns iprt status code.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 */
RTR3DECL(int) RTR3InitDll(uint32_t fFlags);

/**
 * Initializes the runtime library and possibly also SUPLib too.
 *
 * Avoid this interface, it's not considered stable.
 *
 * @returns IPRT status code.
 * @param   iVersion        The interface version. Must be 0 atm.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 * @param   cArgs           Pointer to the argument count.
 * @param   ppapszArgs      Pointer to the argument vector pointer. NULL
 *                          allowed if @a cArgs is 0.
 * @param   pszProgramPath  The program path.  Pass NULL if we're to figure it
 *                          out ourselves.
 */
RTR3DECL(int) RTR3InitEx(uint32_t iVersion, uint32_t fFlags, int cArgs, char ***ppapszArgs, const char *pszProgramPath);

/**
 * Terminates the runtime library.
 */
RTR3DECL(void) RTR3Term(void);

/**
 * Is IPRT succesfully initialized?
 *
 * @returns true/false.
 */
RTR3DECL(bool) RTR3InitIsInitialized(void);

/**
 * Are we running in unobtrusive mode?
 * @returns true/false.
 */
RTR3DECL(bool) RTR3InitIsUnobtrusive(void);
#endif /* IN_RING3 */


#ifdef IN_RING0
/**
 * Initializes the ring-0 driver runtime library.
 *
 * @returns iprt status code.
 * @param   fReserved       Flags reserved for the future.
 */
RTR0DECL(int) RTR0Init(unsigned fReserved);

/**
 * Terminates the ring-0 driver runtime library.
 */
RTR0DECL(void) RTR0Term(void);

/**
 * Forcibily terminates the ring-0 driver runtime library.
 *
 * This should be used when statically linking the IPRT.  Module using dynamic
 * linking shall use RTR0Term.  If you're not sure, use RTR0Term!
 */
RTR0DECL(void) RTR0TermForced(void);
#endif

#ifdef IN_RC
/**
 * Initializes the raw-mode context runtime library.
 *
 * @returns iprt status code.
 *
 * @param   u64ProgramStartNanoTS  The startup timestamp.
 */
RTRCDECL(int) RTRCInit(uint64_t u64ProgramStartNanoTS);

/**
 * Terminates the raw-mode context runtime library.
 */
RTRCDECL(void) RTRCTerm(void);
#endif


/**
 * Termination reason.
 */
typedef enum RTTERMREASON
{
    /** Normal exit. iStatus contains the exit code. */
    RTTERMREASON_EXIT = 1,
    /** Any abnormal exit. iStatus is 0 and has no meaning. */
    RTTERMREASON_ABEND,
    /** Killed by a signal. The iStatus contains the signal number. */
    RTTERMREASON_SIGNAL,
    /** The IPRT module is being unloaded. iStatus is 0 and has no meaning. */
    RTTERMREASON_UNLOAD
} RTTERMREASON;

/** Whether lazy clean up is Okay or not.
 * When the process is exiting, it is a waste of time to for instance free heap
 * memory or close open files. OTOH, when the runtime is unloaded from the
 * process, it is important to release absolutely all resources to prevent
 * resource leaks. */
#define RTTERMREASON_IS_LAZY_CLEANUP_OK(enmReason)  ((enmReason) != RTTERMREASON_UNLOAD)


/**
 * IPRT termination callback function.
 *
 * @param   enmReason           The cause of the termination.
 * @param   iStatus             The meaning of this depends on enmReason.
 * @param   pvUser              User argument passed to RTTermRegisterCallback.
 */
typedef DECLCALLBACK(void) FNRTTERMCALLBACK(RTTERMREASON enmReason, int32_t iStatus, void *pvUser);
/** Pointer to an IPRT termination callback function. */
typedef FNRTTERMCALLBACK *PFNRTTERMCALLBACK;


/**
 * Registers a termination callback.
 *
 * This is intended for performing clean up during IPRT termination. Frequently
 * paired with lazy initialization thru RTOnce.
 *
 * The callbacks are called in LIFO order.
 *
 * @returns IPRT status code.
 *
 * @param   pfnCallback         The callback function.
 * @param   pvUser              The user argument for the callback.
 *
 * @remarks May need to acquire a fast mutex or critical section, so use with
 *          some care in ring-0 context.
 *
 * @remarks Be very careful using this from code that may be unloaded before
 *          IPRT terminates. Unlike some atexit and on_exit implementations,
 *          IPRT will not automatically unregister callbacks when a module gets
 *          unloaded.
 */
RTDECL(int) RTTermRegisterCallback(PFNRTTERMCALLBACK pfnCallback, void *pvUser);

/**
 * Deregister a termination callback.
 *
 * @returns VINF_SUCCESS if found, VERR_NOT_FOUND if the callback/pvUser pair
 *          wasn't found.
 *
 * @param   pfnCallback         The callback function.
 * @param   pvUser              The user argument for the callback.
 */
RTDECL(int) RTTermDeregisterCallback(PFNRTTERMCALLBACK pfnCallback, void *pvUser);

/**
 * Runs the termination callback queue.
 *
 * Normally called by an internal IPRT termination function, but may also be
 * called by external code immediately prior to terminating IPRT if it is in a
 * better position to state the termination reason and/or status.
 *
 * @param   enmReason           The reason why it's called.
 * @param   iStatus             The associated exit status or signal number.
 */
RTDECL(void) RTTermRunCallbacks(RTTERMREASON enmReason, int32_t iStatus);

/** @} */

/** @} */

RT_C_DECLS_END


#endif

