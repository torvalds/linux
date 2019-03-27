/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CALLB_H
#define	_SYS_CALLB_H

#include <sys/kcondvar.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * definitions of callback classes (c_class)
 *
 * Callbacks belong in the same class if (1) their callback routines
 * do the same kind of processing (ideally, using the same callback function)
 * and (2) they can/should be executed at the same time in a cpr
 * suspend/resume operation.
 *
 * Note: The DAEMON class, in particular, is for stopping kernel threads
 * and nothing else.  The CALLB_* macros below should be used to deal
 * with kernel threads, and the callback function should be callb_generic_cpr.
 * Another idiosyncrasy of the DAEMON class is that if a suspend operation
 * fails, some of the callback functions may be called with the RESUME
 * code which were never called with SUSPEND.  Not a problem currently,
 * but see bug 4201851.
 */
#define	CB_CL_CPR_DAEMON	0
#define	CB_CL_CPR_VM		1
#define	CB_CL_CPR_CALLOUT	2
#define	CB_CL_CPR_OBP		3
#define	CB_CL_CPR_FB		4
#define	CB_CL_PANIC		5
#define	CB_CL_CPR_RPC		6
#define	CB_CL_CPR_PROMPRINTF	7
#define	CB_CL_UADMIN		8
#define	CB_CL_CPR_PM		9
#define	CB_CL_HALT		10
#define	CB_CL_CPR_DMA		11
#define	CB_CL_CPR_POST_USER	12
#define	CB_CL_UADMIN_PRE_VFS    13
#define	CB_CL_MDBOOT		CB_CL_UADMIN
#define	CB_CL_ENTER_DEBUGGER	14
#define	CB_CL_CPR_POST_KERNEL	15
#define	CB_CL_CPU_DEEP_IDLE	16
#define	NCBCLASS		17 /* CHANGE ME if classes are added/removed */

/*
 * CB_CL_CPR_DAEMON class specific definitions are given below:
 */

/*
 * code for CPR callb_execute_class
 */
#define	CB_CODE_CPR_CHKPT	0
#define	CB_CODE_CPR_RESUME	1

typedef	void *		callb_id_t;
/*
 * Per kernel thread structure for CPR daemon callbacks.
 * Must be protected by either a existing lock in the daemon or
 * a new lock created for such a purpose.
 */
typedef struct callb_cpr {
	kmutex_t	*cc_lockp;	/* lock to protect this struct */
	char		cc_events;	/* various events for CPR */
	callb_id_t	cc_id;		/* callb id address */
	kcondvar_t	cc_callb_cv;	/* cv for callback waiting */
	kcondvar_t	cc_stop_cv;	/* cv to checkpoint block */
} callb_cpr_t;

/*
 * cc_events definitions
 */
#define	CALLB_CPR_START		1	/* a checkpoint request's started */
#define	CALLB_CPR_SAFE		2	/* thread is safe for CPR */
#define	CALLB_CPR_ALWAYS_SAFE	4	/* thread is ALWAYS safe for CPR */

/*
 * Used when checking that all kernel threads are stopped.
 */
#define	CALLB_MAX_RETRY		3	/* when waiting for kthread to sleep */
#define	CALLB_THREAD_DELAY	10	/* ticks allowed to reach sleep */
#define	CPR_KTHREAD_TIMEOUT_SEC	90	/* secs before callback times out -- */
					/* due to pwr mgmt of disks, make -- */
					/* big enough for worst spinup time */

#ifdef  _KERNEL
/*
 *
 * CALLB_CPR_INIT macro is used by kernel threads to add their entry to
 * the callback table and perform other initialization.  It automatically
 * adds the thread as being in the callback class CB_CL_CPR_DAEMON.
 *
 *	cp    - ptr to the callb_cpr_t structure for this kernel thread
 *
 *	lockp - pointer to mutex protecting the callb_cpr_t stuct
 *
 *	func  - pointer to the callback function for this kernel thread.
 *		It has the prototype boolean_t <func>(void *arg, int code)
 *		where: arg	- ptr to the callb_cpr_t structure
 *		       code	- not used for this type of callback
 *		returns: B_TRUE if successful; B_FALSE if unsuccessful.
 *
 *	name  - a string giving the name of the kernel thread
 *
 * Note: lockp is the lock to protect the callb_cpr_t (cp) structure
 * later on.  No lock held is needed for this initialization.
 */
#define	CALLB_CPR_INIT(cp, lockp, func, name)	{			\
		strlcpy(curthread->td_name, (name),			\
		    sizeof(curthread->td_name));			\
		bzero((caddr_t)(cp), sizeof (callb_cpr_t));		\
		(cp)->cc_lockp = lockp;					\
		(cp)->cc_id = callb_add(func, (void *)(cp),		\
			CB_CL_CPR_DAEMON, name);			\
		cv_init(&(cp)->cc_callb_cv, NULL, CV_DEFAULT, NULL);	\
		cv_init(&(cp)->cc_stop_cv, NULL, CV_DEFAULT, NULL);	\
	}

#ifndef __lock_lint
#define	CALLB_CPR_ASSERT(cp)	ASSERT(MUTEX_HELD((cp)->cc_lockp));
#else
#define	CALLB_CPR_ASSERT(cp)
#endif
/*
 * Some threads (like the idle threads) do not adhere to the callback
 * protocol and are always considered safe.  Such threads must never exit.
 * They register their presence by calling this macro during their
 * initialization.
 *
 * Args:
 *	t	- thread pointer of the client kernel thread
 *	name	- a string giving the name of the kernel thread
 */
#define	CALLB_CPR_INIT_SAFE(t, name) {					\
		(void) callb_add_thread(callb_generic_cpr_safe,		\
		(void *) &callb_cprinfo_safe, CB_CL_CPR_DAEMON,		\
		    name, t);						\
	}
/*
 * The lock to protect cp's content must be held before
 * calling the following two macros.
 *
 * Any code region between CALLB_CPR_SAFE_BEGIN and CALLB_CPR_SAFE_END
 * is safe for checkpoint/resume.
 */
#define	CALLB_CPR_SAFE_BEGIN(cp) { 			\
		CALLB_CPR_ASSERT(cp)			\
		(cp)->cc_events |= CALLB_CPR_SAFE;	\
		if ((cp)->cc_events & CALLB_CPR_START)	\
			cv_signal(&(cp)->cc_callb_cv);	\
	}
#define	CALLB_CPR_SAFE_END(cp, lockp) {				\
		CALLB_CPR_ASSERT(cp)				\
		while ((cp)->cc_events & CALLB_CPR_START)	\
			cv_wait(&(cp)->cc_stop_cv, lockp);	\
		(cp)->cc_events &= ~CALLB_CPR_SAFE;		\
	}
/*
 * cv_destroy is nop right now but may be needed in the future.
 */
#define	CALLB_CPR_EXIT(cp) {				\
		CALLB_CPR_ASSERT(cp)			\
		(cp)->cc_events |= CALLB_CPR_SAFE;	\
		if ((cp)->cc_events & CALLB_CPR_START)	\
			cv_signal(&(cp)->cc_callb_cv);	\
		mutex_exit((cp)->cc_lockp);		\
		(void) callb_delete((cp)->cc_id);	\
		cv_destroy(&(cp)->cc_callb_cv);		\
		cv_destroy(&(cp)->cc_stop_cv);		\
	}

extern callb_cpr_t callb_cprinfo_safe;
extern callb_id_t callb_add(boolean_t  (*)(void *, int), void *, int, char *);
extern callb_id_t callb_add_thread(boolean_t (*)(void *, int),
    void *, int, char *, kthread_id_t);
extern int	callb_delete(callb_id_t);
extern void	callb_execute(callb_id_t, int);
extern void	*callb_execute_class(int, int);
extern boolean_t callb_generic_cpr(void *, int);
extern boolean_t callb_generic_cpr_safe(void *, int);
extern boolean_t callb_is_stopped(kthread_id_t, caddr_t *);
extern void	callb_lock_table(void);
extern void	callb_unlock_table(void);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CALLB_H */
