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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/callb.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kobj.h>
#include <sys/systm.h>	/* for delay() */
#include <sys/taskq.h>  /* For TASKQ_NAMELEN */
#include <sys/kernel.h>

#define	CB_MAXNAME	TASKQ_NAMELEN

/*
 * The callb mechanism provides generic event scheduling/echoing.
 * A callb function is registered and called on behalf of the event.
 */
typedef struct callb {
	struct callb	*c_next; 	/* next in class or on freelist */
	kthread_id_t	c_thread;	/* ptr to caller's thread struct */
	char		c_flag;		/* info about the callb state */
	uchar_t		c_class;	/* this callb's class */
	kcondvar_t	c_done_cv;	/* signal callb completion */
	boolean_t	(*c_func)();	/* cb function: returns true if ok */
	void		*c_arg;		/* arg to c_func */
	char		c_name[CB_MAXNAME+1]; /* debug:max func name length */
} callb_t;

/*
 * callb c_flag bitmap definitions
 */
#define	CALLB_FREE		0x0
#define	CALLB_TAKEN		0x1
#define	CALLB_EXECUTING		0x2

/*
 * Basic structure for a callb table.
 * All callbs are organized into different class groups described
 * by ct_class array.
 * The callbs within a class are single-linked and normally run by a
 * serial execution.
 */
typedef struct callb_table {
	kmutex_t ct_lock;		/* protect all callb states */
	callb_t	*ct_freelist; 		/* free callb structures */
	int	ct_busy;		/* != 0 prevents additions */
	kcondvar_t ct_busy_cv;		/* to wait for not busy    */
	int	ct_ncallb; 		/* num of callbs allocated */
	callb_t	*ct_first_cb[NCBCLASS];	/* ptr to 1st callb in a class */
} callb_table_t;

int callb_timeout_sec = CPR_KTHREAD_TIMEOUT_SEC;

static callb_id_t callb_add_common(boolean_t (*)(void *, int),
    void *, int, char *, kthread_id_t);

static callb_table_t callb_table;	/* system level callback table */
static callb_table_t *ct = &callb_table;
static kmutex_t	callb_safe_mutex;
callb_cpr_t	callb_cprinfo_safe = {
	&callb_safe_mutex, CALLB_CPR_ALWAYS_SAFE, 0, 0, 0 };

/*
 * Init all callb tables in the system.
 */
void
callb_init(void *dummy __unused)
{
	callb_table.ct_busy = 0;	/* mark table open for additions */
	mutex_init(&callb_safe_mutex, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&callb_table.ct_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
callb_fini(void *dummy __unused)
{
	callb_t *cp;
	int i;

	mutex_enter(&ct->ct_lock);
	for (i = 0; i < 16; i++) {
		while ((cp = ct->ct_freelist) != NULL) {
			ct->ct_freelist = cp->c_next;
			ct->ct_ncallb--;
			kmem_free(cp, sizeof (callb_t));
		}
		if (ct->ct_ncallb == 0)
			break;
		/* Not all callbacks finished, waiting for the rest. */
		mutex_exit(&ct->ct_lock);
		tsleep(ct, 0, "callb", hz / 4);
		mutex_enter(&ct->ct_lock);
	}
	if (ct->ct_ncallb > 0)
		printf("%s: Leaked %d callbacks!\n", __func__, ct->ct_ncallb);
	mutex_exit(&ct->ct_lock);
	mutex_destroy(&callb_safe_mutex);
	mutex_destroy(&callb_table.ct_lock);
}

/*
 * callout_add() is called to register func() be called later.
 */
static callb_id_t
callb_add_common(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name, kthread_id_t t)
{
	callb_t *cp;

	ASSERT(class < NCBCLASS);

	mutex_enter(&ct->ct_lock);
	while (ct->ct_busy)
		cv_wait(&ct->ct_busy_cv, &ct->ct_lock);
	if ((cp = ct->ct_freelist) == NULL) {
		ct->ct_ncallb++;
		cp = (callb_t *)kmem_zalloc(sizeof (callb_t), KM_SLEEP);
	}
	ct->ct_freelist = cp->c_next;
	cp->c_thread = t;
	cp->c_func = func;
	cp->c_arg = arg;
	cp->c_class = (uchar_t)class;
	cp->c_flag |= CALLB_TAKEN;
#ifdef DEBUG
	if (strlen(name) > CB_MAXNAME)
		cmn_err(CE_WARN, "callb_add: name of callback function '%s' "
		    "too long -- truncated to %d chars",
		    name, CB_MAXNAME);
#endif
	(void) strncpy(cp->c_name, name, CB_MAXNAME);
	cp->c_name[CB_MAXNAME] = '\0';

	/*
	 * Insert the new callb at the head of its class list.
	 */
	cp->c_next = ct->ct_first_cb[class];
	ct->ct_first_cb[class] = cp;

	mutex_exit(&ct->ct_lock);
	return ((callb_id_t)cp);
}

/*
 * The default function to add an entry to the callback table.  Since
 * it uses curthread as the thread identifier to store in the table,
 * it should be used for the normal case of a thread which is calling
 * to add ITSELF to the table.
 */
callb_id_t
callb_add(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name)
{
	return (callb_add_common(func, arg, class, name, curthread));
}

/*
 * A special version of callb_add() above for use by threads which
 * might be adding an entry to the table on behalf of some other
 * thread (for example, one which is constructed but not yet running).
 * In this version the thread id is an argument.
 */
callb_id_t
callb_add_thread(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name, kthread_id_t t)
{
	return (callb_add_common(func, arg, class, name, t));
}

/*
 * callout_delete() is called to remove an entry identified by id
 * that was originally placed there by a call to callout_add().
 * return -1 if fail to delete a callb entry otherwise return 0.
 */
int
callb_delete(callb_id_t id)
{
	callb_t **pp;
	callb_t *me = (callb_t *)id;

	mutex_enter(&ct->ct_lock);

	for (;;) {
		pp = &ct->ct_first_cb[me->c_class];
		while (*pp != NULL && *pp != me)
			pp = &(*pp)->c_next;

#ifdef DEBUG
		if (*pp != me) {
			cmn_err(CE_WARN, "callb delete bogus entry 0x%p",
			    (void *)me);
			mutex_exit(&ct->ct_lock);
			return (-1);
		}
#endif /* DEBUG */

		/*
		 * It is not allowed to delete a callb in the middle of
		 * executing otherwise, the callb_execute() will be confused.
		 */
		if (!(me->c_flag & CALLB_EXECUTING))
			break;

		cv_wait(&me->c_done_cv, &ct->ct_lock);
	}
	/* relink the class list */
	*pp = me->c_next;

	/* clean up myself and return the free callb to the head of freelist */
	me->c_flag = CALLB_FREE;
	me->c_next = ct->ct_freelist;
	ct->ct_freelist = me;

	mutex_exit(&ct->ct_lock);
	return (0);
}

/*
 * class:	indicates to execute all callbs in the same class;
 * code:	optional argument for the callb functions.
 * return:	 = 0: success
 *		!= 0: ptr to string supplied when callback was registered
 */
void *
callb_execute_class(int class, int code)
{
	callb_t *cp;
	void *ret = NULL;

	ASSERT(class < NCBCLASS);

	mutex_enter(&ct->ct_lock);

	for (cp = ct->ct_first_cb[class];
	    cp != NULL && ret == 0; cp = cp->c_next) {
		while (cp->c_flag & CALLB_EXECUTING)
			cv_wait(&cp->c_done_cv, &ct->ct_lock);
		/*
		 * cont if the callb is deleted while we're sleeping
		 */
		if (cp->c_flag == CALLB_FREE)
			continue;
		cp->c_flag |= CALLB_EXECUTING;

#ifdef CALLB_DEBUG
		printf("callb_execute: name=%s func=%p arg=%p\n",
		    cp->c_name, (void *)cp->c_func, (void *)cp->c_arg);
#endif /* CALLB_DEBUG */

		mutex_exit(&ct->ct_lock);
		/* If callback function fails, pass back client's name */
		if (!(*cp->c_func)(cp->c_arg, code))
			ret = cp->c_name;
		mutex_enter(&ct->ct_lock);

		cp->c_flag &= ~CALLB_EXECUTING;
		cv_broadcast(&cp->c_done_cv);
	}
	mutex_exit(&ct->ct_lock);
	return (ret);
}

/*
 * callers make sure no recursive entries to this func.
 * dp->cc_lockp is registered by callb_add to protect callb_cpr_t structure.
 *
 * When calling to stop a kernel thread (code == CB_CODE_CPR_CHKPT) we
 * use a cv_timedwait() in case the kernel thread is blocked.
 *
 * Note that this is a generic callback handler for daemon CPR and
 * should NOT be changed to accommodate any specific requirement in a daemon.
 * Individual daemons that require changes to the handler shall write
 * callback routines in their own daemon modules.
 */
boolean_t
callb_generic_cpr(void *arg, int code)
{
	callb_cpr_t *cp = (callb_cpr_t *)arg;
	clock_t ret = 0;			/* assume success */

	mutex_enter(cp->cc_lockp);

	switch (code) {
	case CB_CODE_CPR_CHKPT:
		cp->cc_events |= CALLB_CPR_START;
#ifdef CPR_NOT_THREAD_SAFE
		while (!(cp->cc_events & CALLB_CPR_SAFE))
			/* cv_timedwait() returns -1 if it times out. */
			if ((ret = cv_reltimedwait(&cp->cc_callb_cv,
			    cp->cc_lockp, (callb_timeout_sec * hz),
			    TR_CLOCK_TICK)) == -1)
				break;
#endif
		break;

	case CB_CODE_CPR_RESUME:
		cp->cc_events &= ~CALLB_CPR_START;
		cv_signal(&cp->cc_stop_cv);
		break;
	}
	mutex_exit(cp->cc_lockp);
	return (ret != -1);
}

/*
 * The generic callback function associated with kernel threads which
 * are always considered safe.
 */
/* ARGSUSED */
boolean_t
callb_generic_cpr_safe(void *arg, int code)
{
	return (B_TRUE);
}
/*
 * Prevent additions to callback table.
 */
void
callb_lock_table(void)
{
	mutex_enter(&ct->ct_lock);
	ASSERT(ct->ct_busy == 0);
	ct->ct_busy = 1;
	mutex_exit(&ct->ct_lock);
}

/*
 * Allow additions to callback table.
 */
void
callb_unlock_table(void)
{
	mutex_enter(&ct->ct_lock);
	ASSERT(ct->ct_busy != 0);
	ct->ct_busy = 0;
	cv_broadcast(&ct->ct_busy_cv);
	mutex_exit(&ct->ct_lock);
}

#ifdef illumos
/*
 * Return a boolean value indicating whether a particular kernel thread is
 * stopped in accordance with the cpr callback protocol.  If returning
 * false, also return a pointer to the thread name via the 2nd argument.
 */
boolean_t
callb_is_stopped(kthread_id_t tp, caddr_t *thread_name)
{
	callb_t *cp;
	boolean_t ret_val;

	mutex_enter(&ct->ct_lock);

	for (cp = ct->ct_first_cb[CB_CL_CPR_DAEMON];
	    cp != NULL && tp != cp->c_thread; cp = cp->c_next)
		;

	ret_val = (cp != NULL);
	if (ret_val) {
		/*
		 * We found the thread in the callback table and have
		 * provisionally set the return value to true.  Now
		 * see if it is marked "safe" and is sleeping or stopped.
		 */
		callb_cpr_t *ccp = (callb_cpr_t *)cp->c_arg;

		*thread_name = cp->c_name;	/* in case not stopped */
		mutex_enter(ccp->cc_lockp);

		if (ccp->cc_events & CALLB_CPR_SAFE) {
			int retry;

			mutex_exit(ccp->cc_lockp);
			for (retry = 0; retry < CALLB_MAX_RETRY; retry++) {
				thread_lock(tp);
				if (tp->t_state & (TS_SLEEP | TS_STOPPED)) {
					thread_unlock(tp);
					break;
				}
				thread_unlock(tp);
				delay(CALLB_THREAD_DELAY);
			}
			ret_val = retry < CALLB_MAX_RETRY;
		} else {
			ret_val =
			    (ccp->cc_events & CALLB_CPR_ALWAYS_SAFE) != 0;
			mutex_exit(ccp->cc_lockp);
		}
	} else {
		/*
		 * Thread not found in callback table.  Make the best
		 * attempt to identify the thread in the error message.
		 */
		ulong_t offset;
		char *sym = kobj_getsymname((uintptr_t)tp->t_startpc,
		    &offset);

		*thread_name = sym ? sym : "*unknown*";
	}

	mutex_exit(&ct->ct_lock);
	return (ret_val);
}
#endif	/* illumos */

SYSINIT(sol_callb, SI_SUB_DRIVERS, SI_ORDER_FIRST, callb_init, NULL);
SYSUNINIT(sol_callb, SI_SUB_DRIVERS, SI_ORDER_FIRST, callb_fini, NULL);
