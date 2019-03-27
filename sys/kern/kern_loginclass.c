/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Processes may set login class name using setloginclass(2).  This
 * is usually done through call to setusercontext(3), by programs
 * such as login(1), based on information from master.passwd(5).  Kernel
 * uses this information to enforce per-class resource limits.  Current
 * login class can be determined using id(1).  Login class is inherited
 * from the parent process during fork(2).  If not set, it defaults
 * to "default".
 *
 * Code in this file implements setloginclass(2) and getloginclass(2)
 * system calls, and maintains class name storage and retrieval.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/loginclass.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/racct.h>
#include <sys/rctl.h>
#include <sys/refcount.h>
#include <sys/rwlock.h>
#include <sys/sysproto.h>
#include <sys/systm.h>

static MALLOC_DEFINE(M_LOGINCLASS, "loginclass", "loginclass structures");

LIST_HEAD(, loginclass)	loginclasses;

/*
 * Lock protecting loginclasses list.
 */
static struct rwlock loginclasses_lock;
RW_SYSINIT(loginclasses_init, &loginclasses_lock, "loginclasses lock");

void
loginclass_hold(struct loginclass *lc)
{

	refcount_acquire(&lc->lc_refcount);
}

void
loginclass_free(struct loginclass *lc)
{

	if (refcount_release_if_not_last(&lc->lc_refcount))
		return;

	rw_wlock(&loginclasses_lock);
	if (!refcount_release(&lc->lc_refcount)) {
		rw_wunlock(&loginclasses_lock);
		return;
	}

	racct_destroy(&lc->lc_racct);
	LIST_REMOVE(lc, lc_next);
	rw_wunlock(&loginclasses_lock);

	free(lc, M_LOGINCLASS);
}

/*
 * Look up a loginclass struct for the parameter name.
 * loginclasses_lock must be locked.
 * Increase refcount on loginclass struct returned.
 */
static struct loginclass *
loginclass_lookup(const char *name)
{
	struct loginclass *lc;

	rw_assert(&loginclasses_lock, RA_LOCKED);
	LIST_FOREACH(lc, &loginclasses, lc_next)
		if (strcmp(name, lc->lc_name) == 0) {
			loginclass_hold(lc);
			break;
		}

	return (lc);
}

/*
 * Return loginclass structure with a corresponding name.  Not
 * performance critical, as it's used mainly by setloginclass(2),
 * which happens once per login session.  Caller has to use
 * loginclass_free() on the returned value when it's no longer
 * needed.
 */
struct loginclass *
loginclass_find(const char *name)
{
	struct loginclass *lc, *new_lc;

	if (name[0] == '\0' || strlen(name) >= MAXLOGNAME)
		return (NULL);

	lc = curthread->td_ucred->cr_loginclass;
	if (strcmp(name, lc->lc_name) == 0) {
		loginclass_hold(lc);
		return (lc);
	}

	rw_rlock(&loginclasses_lock);
	lc = loginclass_lookup(name);
	rw_runlock(&loginclasses_lock);
	if (lc != NULL)
		return (lc);

	new_lc = malloc(sizeof(*new_lc), M_LOGINCLASS, M_ZERO | M_WAITOK);
	racct_create(&new_lc->lc_racct);
	refcount_init(&new_lc->lc_refcount, 1);
	strcpy(new_lc->lc_name, name);

	rw_wlock(&loginclasses_lock);
	/*
	 * There's a chance someone created our loginclass while we
	 * were in malloc and not holding the lock, so we have to
	 * make sure we don't insert a duplicate loginclass.
	 */
	if ((lc = loginclass_lookup(name)) == NULL) {
		LIST_INSERT_HEAD(&loginclasses, new_lc, lc_next);
		rw_wunlock(&loginclasses_lock);
		lc = new_lc;
	} else {
		rw_wunlock(&loginclasses_lock);
		racct_destroy(&new_lc->lc_racct);
		free(new_lc, M_LOGINCLASS);
	}

	return (lc);
}

/*
 * Get login class name.
 */
#ifndef _SYS_SYSPROTO_H_
struct getloginclass_args {
	char	*namebuf;
	size_t	namelen;
};
#endif
/* ARGSUSED */
int
sys_getloginclass(struct thread *td, struct getloginclass_args *uap)
{
	struct loginclass *lc;
	size_t lcnamelen;

	lc = td->td_ucred->cr_loginclass;
	lcnamelen = strlen(lc->lc_name) + 1;
	if (lcnamelen > uap->namelen)
		return (ERANGE);
	return (copyout(lc->lc_name, uap->namebuf, lcnamelen));
}

/*
 * Set login class name.
 */
#ifndef _SYS_SYSPROTO_H_
struct setloginclass_args {
	const char	*namebuf;
};
#endif
/* ARGSUSED */
int
sys_setloginclass(struct thread *td, struct setloginclass_args *uap)
{
	struct proc *p = td->td_proc;
	int error;
	char lcname[MAXLOGNAME];
	struct loginclass *newlc;
	struct ucred *newcred, *oldcred;

	error = priv_check(td, PRIV_PROC_SETLOGINCLASS);
	if (error != 0)
		return (error);
	error = copyinstr(uap->namebuf, lcname, sizeof(lcname), NULL);
	if (error != 0)
		return (error);

	newlc = loginclass_find(lcname);
	if (newlc == NULL)
		return (EINVAL);
	newcred = crget();

	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);
	newcred->cr_loginclass = newlc;
	proc_set_cred(p, newcred);
#ifdef RACCT
	racct_proc_ucred_changed(p, oldcred, newcred);
	crhold(newcred);
#endif
	PROC_UNLOCK(p);
#ifdef RCTL
	rctl_proc_ucred_changed(p, newcred);
	crfree(newcred);
#endif
	loginclass_free(oldcred->cr_loginclass);
	crfree(oldcred);

	return (0);
}

void
loginclass_racct_foreach(void (*callback)(struct racct *racct,
    void *arg2, void *arg3), void (*pre)(void), void (*post)(void),
    void *arg2, void *arg3)
{
	struct loginclass *lc;

	rw_rlock(&loginclasses_lock);
	if (pre != NULL)
		(pre)();
	LIST_FOREACH(lc, &loginclasses, lc_next)
		(callback)(lc->lc_racct, arg2, arg3);
	if (post != NULL)
		(post)();
	rw_runlock(&loginclasses_lock);
}
