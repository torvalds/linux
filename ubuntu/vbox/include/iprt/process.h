/** @file
 * IPRT - Process Management.
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

#ifndef ___iprt_process_h
#define ___iprt_process_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_process    RTProc - Process Management
 * @ingroup grp_rt
 * @{
 */


/**
 * Process priority.
 *
 * The process priority is used to select how scheduling properties
 * are assigned to the different thread types (see THREADTYPE).
 *
 * In addition to using the policy assigned to the process at startup (DEFAULT)
 * it is possible to change the process priority at runtime. This allows for
 * a GUI, resource manager or admin to adjust the general priority of a task
 * without upsetting the fine-tuned priority of the threads within.
 */
typedef enum RTPROCPRIORITY
{
    /** Invalid priority. */
    RTPROCPRIORITY_INVALID = 0,
    /** Default priority.
     * Derive the scheduling policy from the priority of the RTR3Init()
     * and RTProcSetPriority() callers and the rights the process have
     * to alter its own priority.
     */
    RTPROCPRIORITY_DEFAULT,
    /** Flat priority.
     * Assumes a scheduling policy which puts the process at the default priority
     * and with all thread at the same priority.
     */
    RTPROCPRIORITY_FLAT,
    /** Low priority.
     * Assumes a scheduling policy which puts the process mostly below the
     * default priority of the host OS.
     */
    RTPROCPRIORITY_LOW,
    /** Normal priority.
     * Assume a scheduling policy which shares the CPU resources fairly with
     * other processes running with the default priority of the host OS.
     */
    RTPROCPRIORITY_NORMAL,
    /** High priority.
     * Assumes a scheduling policy which puts the task above the default
     * priority of the host OS. This policy might easily cause other tasks
     * in the system to starve.
     */
    RTPROCPRIORITY_HIGH,
    /** Last priority, used for validation. */
    RTPROCPRIORITY_LAST
} RTPROCPRIORITY;


/**
 * Get the current process identifier.
 *
 * @returns Process identifier.
 */
RTDECL(RTPROCESS) RTProcSelf(void);


#ifdef IN_RING0
/**
 * Get the current process handle.
 *
 * @returns Ring-0 process handle.
 */
RTR0DECL(RTR0PROCESS) RTR0ProcHandleSelf(void);
#endif


#ifdef IN_RING3

/**
 * Attempts to alter the priority of the current process.
 *
 * @returns iprt status code.
 * @param   enmPriority     The new priority.
 */
RTR3DECL(int) RTProcSetPriority(RTPROCPRIORITY enmPriority);

/**
 * Gets the current priority of this process.
 *
 * @returns The priority (see RTPROCPRIORITY).
 */
RTR3DECL(RTPROCPRIORITY) RTProcGetPriority(void);

/**
 * Create a child process.
 *
 * @returns iprt status code.
 * @param   pszExec     Executable image to use to create the child process.
 * @param   papszArgs   Pointer to an array of arguments to the child. The array terminated by an entry containing NULL.
 * @param   Env         Handle to the environment block for the child.
 * @param   fFlags      Flags, one of the RTPROC_FLAGS_* defines.
 * @param   pProcess    Where to store the process identifier on successful return.
 *                      The content is not changed on failure. NULL is allowed.
 */
RTR3DECL(int)   RTProcCreate(const char *pszExec, const char * const *papszArgs, RTENV Env, unsigned fFlags, PRTPROCESS pProcess);


/**
 * Create a child process.
 *
 * @returns IPRT status code.
 *
 * @param   pszExec     Executable image to use to create the child process.
 * @param   papszArgs   Pointer to an array of arguments to the child.  The
 *                      array terminated by an entry containing NULL.
 * @param   hEnv        Handle to the environment block for the child.  Pass
 *                      RTENV_DEFAULT to use the environment of the current
 *                      process.
 * @param   fFlags      Flags, one of the RTPROC_FLAGS_* defines.
 * @param   phStdIn     The standard in handle to assign the new process. Pass
 *                      NULL to use the same as the current process.  If the
 *                      handle is NIL, we'll close the standard input of the
 *                      guest.
 * @param   phStdOut    The standard out handle to assign the new process.  Pass
 *                      NULL to use the same as the current process.  If the
 *                      handle is NIL, we'll close the standard output of the
 *                      guest.
 * @param   phStdErr    The standard error handle to assign the new process.  Pass
 *                      NULL to use the same as the current process.  If the
 *                      handle is NIL, we'll close the standard error of the
 *                      guest.
 * @param   pszAsUser   User to run the process as.  Pass NULL to use the same
 *                      user as the current process.
 *                      Windows: Use user\@domain (UPN, User Principal Name)
 *                      format to specify a domain.
 * @param   pszPassword Password to use to authenticate @a pszAsUser.  Must be
 *                      NULL wif pszAsUser is NULL.  Whether this is actually
 *                      used or not depends on the platform.
 * @param   phProcess   Where to store the process handle on successful return.
 *                      The content is not changed on failure.  NULL is allowed.
 *
 * @remarks The handles does not have to be created as inheritable, but it
 *          doesn't hurt if they are as it may avoid race conditions on some
 *          platforms.
 *
 * @remarks The as-user feature isn't supported/implemented on all platforms and
 *          will cause a-yet-to-be-determined-error-status on these.
 */
RTR3DECL(int)   RTProcCreateEx(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                               PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr, const char *pszAsUser,
                               const char *pszPassword, PRTPROCESS phProcess);

/** @name RTProcCreate and RTProcCreateEx flags
 * @{ */
/** Detach the child process from the parents process tree and process group,
 * session or/and console (depends on the platform what's done applicable).
 *
 * The new process will not be a direct decendent of the parent and it will not
 * be possible to wait for it, i.e. @a phProcess shall be NULL. */
#define RTPROC_FLAGS_DETACHED               RT_BIT(0)
/** Don't show the started process.
 * This is a Windows (and maybe OS/2) concept, do not use on other platforms. */
#define RTPROC_FLAGS_HIDDEN                 RT_BIT(1)
/** Use special code path for starting child processes from a service (daemon).
 * This is a windows concept for dealing with the so called "Session 0"
 * isolation which was introduced with Windows Vista. Do not use on other
 * platforms. */
#define RTPROC_FLAGS_SERVICE                RT_BIT(2)
/** Suppress changing the process contract id for the child process
 * on Solaris.  Without this flag the contract id is always changed, as that's
 * the more frequently used case. */
#define RTPROC_FLAGS_SAME_CONTRACT          RT_BIT(3)
/** Load user profile data when executing a process.
 * This redefines the meaning of RTENV_DEFAULT to the profile environment.
 * @remarks On non-windows platforms, the resulting environment maybe very
 *          different from what you see in your shell.  Among other reasons,
 *          we cannot run shell profile scripts which typically sets up the
 *          environment. */
#define RTPROC_FLAGS_PROFILE                RT_BIT(4)
/** Create process without a console window.
 * This is a Windows (and OS/2) concept, do not use on other platforms. */
#define RTPROC_FLAGS_NO_WINDOW              RT_BIT(5)
/** Search the PATH for the executable.  */
#define RTPROC_FLAGS_SEARCH_PATH            RT_BIT(6)
/** Don't quote and escape arguments on Windows and similar platforms where a
 * command line is passed to the child process instead of an argument vector,
 * just join up argv with a space between each.  Ignored on platforms
 * passing argument the vector. */
#define RTPROC_FLAGS_UNQUOTED_ARGS          RT_BIT(7)
/** Consider hEnv an environment change record to be applied to RTENV_DEFAULT.
 * If hEnv is RTENV_DEFAULT, the flag has no effect. */
#define RTPROC_FLAGS_ENV_CHANGE_RECORD      RT_BIT(8)
/** Valid flag mask. */
#define RTPROC_FLAGS_VALID_MASK             UINT32_C(0x1ff)
/** @}  */


/**
 * Process exit reason.
 */
typedef enum RTPROCEXITREASON
{
    /** Normal exit. iStatus contains the exit code. */
    RTPROCEXITREASON_NORMAL = 1,
    /** Any abnormal exit. iStatus is undefined. */
    RTPROCEXITREASON_ABEND,
    /** Killed by a signal. The iStatus field contains the signal number. */
    RTPROCEXITREASON_SIGNAL
} RTPROCEXITREASON;

/**
 * Process exit status.
 */
typedef struct RTPROCSTATUS
{
    /** The process exit status if the exit was a normal one. */
    int                 iStatus;
    /** The reason the process terminated. */
    RTPROCEXITREASON    enmReason;
} RTPROCSTATUS;
/** Pointer to a process exit status structure. */
typedef RTPROCSTATUS *PRTPROCSTATUS;
/** Pointer to a const process exit status structure. */
typedef const RTPROCSTATUS *PCRTPROCSTATUS;


/** Flags for RTProcWait().
 * @{ */
/** Block indefinitly waiting for the process to exit. */
#define RTPROCWAIT_FLAGS_BLOCK      0
/** Don't block, just check if the process have exited. */
#define RTPROCWAIT_FLAGS_NOBLOCK    1
/** @} */

/**
 * Waits for a process, resumes on interruption.
 *
 * @returns VINF_SUCCESS when the status code for the process was collected and
 *          put in *pProcStatus.
 * @returns VERR_PROCESS_NOT_FOUND if the specified process wasn't found.
 * @returns VERR_PROCESS_RUNNING when the RTPROCWAIT_FLAGS_NOBLOCK and the
 *          process haven't exited yet.
 *
 * @param   Process         The process to wait for.
 * @param   fFlags          The wait flags, any of the RTPROCWAIT_FLAGS_ \#defines.
 * @param   pProcStatus     Where to store the exit status on success.
 *                          Optional.
 */
RTR3DECL(int) RTProcWait(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus);

/**
 * Waits for a process, returns on interruption.
 *
 * @returns VINF_SUCCESS when the status code for the process was collected and
 *          put in *pProcStatus.
 * @returns VERR_PROCESS_NOT_FOUND if the specified process wasn't found.
 * @returns VERR_PROCESS_RUNNING when the RTPROCWAIT_FLAGS_NOBLOCK and the
 *          process haven't exited yet.
 * @returns VERR_INTERRUPTED when the wait was interrupted by the arrival of a
 *          signal or other async event.
 *
 * @param   Process         The process to wait for.
 * @param   fFlags          The wait flags, any of the RTPROCWAIT_FLAGS_ \#defines.
 * @param   pProcStatus     Where to store the exit status on success.
 *                          Optional.
 */
RTR3DECL(int) RTProcWaitNoResume(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus);

/**
 * Terminates (kills) a running process.
 *
 * @returns IPRT status code.
 * @param   Process     The process to terminate.
 */
RTR3DECL(int) RTProcTerminate(RTPROCESS Process);

/**
 * Gets the processor affinity mask of the current process.
 *
 * @returns The affinity mask.
 */
RTR3DECL(uint64_t) RTProcGetAffinityMask(void);

/**
 * Gets the short process name.
 *
 * @returns Pointer to read-only name string.
 */
RTR3DECL(const char *) RTProcShortName(void);

/**
 * Gets the path to the executable image of the current process.
 *
 * @returns pszExecPath on success. NULL on buffer overflow or other errors.
 *
 * @param   pszExecPath     Where to store the path.
 * @param   cbExecPath      The size of the buffer.
 */
RTR3DECL(char *) RTProcGetExecutablePath(char *pszExecPath, size_t cbExecPath);

/**
 * Daemonize the current process, making it a background process.
 *
 * The way this work is that it will spawn a detached / backgrounded /
 * daemonized / call-it-what-you-want process that isn't a direct child of the
 * current process.  The spawned will have the same arguments a the caller,
 * except that the @a pszDaemonizedOpt is appended to prevent that the new
 * process calls this API again.
 *
 * The new process will have the standard handles directed to/from the
 * bitbucket.
 *
 * @returns IPRT status code.  On success it is normal for the caller to exit
 *          the process by returning from main().
 *
 * @param   papszArgs           The argument vector of the calling process.
 * @param   pszDaemonizedOpt    The daemonized option.  This is appended to the
 *                              end of the parameter list of the daemonized process.
 */
RTR3DECL(int)   RTProcDaemonize(const char * const *papszArgs, const char *pszDaemonizedOpt);

/**
 * Daemonize the current process, making it a background process. The current
 * process will exit if daemonizing is successful.
 *
 * @returns IPRT status code.   On success it will only return in the child
 *          process, the parent will exit.  On failure, it will return in the
 *          parent process and no child has been spawned.
 *
 * @param   fNoChDir    Pass false to change working directory to "/".
 * @param   fNoClose    Pass false to redirect standard file streams to the null device.
 * @param   pszPidfile  Path to a file to write the process id of the daemon
 *                      process to. Daemonizing will fail if this file already
 *                      exists or cannot be written. May be NULL.
 */
RTR3DECL(int)   RTProcDaemonizeUsingFork(bool fNoChDir, bool fNoClose, const char *pszPidfile);

/**
 * Check if the given process is running on the system.
 *
 * This check is case sensitive on most systems, except for Windows, OS/2 and
 * Darwin.
 *
 * @returns true if the process is running & false otherwise.
 * @param   pszName     Process name to search for. If no path is given only the
 *                      filename part of the running process set will be
 *                      matched. If a path is specified, the full path will be
 *                      matched.
 */
RTR3DECL(bool)  RTProcIsRunningByName(const char *pszName);

/**
 * Queries the parent process ID.
 *
 * @returns IPRT status code
 * @param   hProcess     The process to query the parent of.
 * @param   phParent     Where to return the parent process ID.
 */
RTR3DECL(int) RTProcQueryParent(RTPROCESS hProcess, PRTPROCESS phParent);

/**
 * Query the username of the given process.
 *
 * @returns IPRT status code.
 * @retval VERR_BUFFER_OVERFLOW if the given buffer size is to small for the username.
 * @param   hProcess     The process handle to query the username for.
 *                       NIL_PROCESS is an alias for the current process.
 * @param   pszUser      Where to store the user name on success.
 * @param   cbUser       The size of the user name buffer.
 * @param   pcbUser      Where to store the username length on success
 *                       or the required buffer size if VERR_BUFFER_OVERFLOW
 *                       is returned.
 */
RTR3DECL(int)   RTProcQueryUsername(RTPROCESS hProcess, char *pszUser, size_t cbUser, size_t *pcbUser);

/**
 * Query the username of the given process allocating the string for the username.
 *
 * @returns IPRT status code.
 * @param   hProcess     The process handle to query the username for.
 * @param   ppszUser     Where to store the pointer to the string containing
 *                       the username on success. Free with RTStrFree().
 */
RTR3DECL(int)   RTProcQueryUsernameA(RTPROCESS hProcess, char **ppszUser);

#endif /* IN_RING3 */

/** @} */

RT_C_DECLS_END

#endif

