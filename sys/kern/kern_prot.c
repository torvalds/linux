/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1990, 1991, 1993
 *	The Regents of the University of California.
 * (c) UNIX System Laboratories, Inc.
 * Copyright (c) 2000-2001 Robert N. M. Watson.
 * All rights reserved.
 *
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_prot.c	8.6 (Berkeley) 1/21/94
 */

/*
 * System calls related to processes and protection
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/acct.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/loginclass.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/refcount.h>
#include <sys/sx.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/jail.h>
#include <sys/pioctl.h>
#include <sys/racct.h>
#include <sys/rctl.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>

#ifdef REGRESSION
FEATURE(regression,
    "Kernel support for interfaces necessary for regression testing (SECURITY RISK!)");
#endif

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

static MALLOC_DEFINE(M_CRED, "cred", "credentials");

SYSCTL_NODE(_security, OID_AUTO, bsd, CTLFLAG_RW, 0, "BSD security policy");

static void crsetgroups_locked(struct ucred *cr, int ngrp,
    gid_t *groups);

#ifndef _SYS_SYSPROTO_H_
struct getpid_args {
	int	dummy;
};
#endif
/* ARGSUSED */
int
sys_getpid(struct thread *td, struct getpid_args *uap)
{
	struct proc *p = td->td_proc;

	td->td_retval[0] = p->p_pid;
#if defined(COMPAT_43)
	td->td_retval[1] = kern_getppid(td);
#endif
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getppid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_getppid(struct thread *td, struct getppid_args *uap)
{

	td->td_retval[0] = kern_getppid(td);
	return (0);
}

int
kern_getppid(struct thread *td)
{
	struct proc *p = td->td_proc;

	return (p->p_oppid);
}

/*
 * Get process group ID; note that POSIX getpgrp takes no parameter.
 */
#ifndef _SYS_SYSPROTO_H_
struct getpgrp_args {
        int     dummy;
};
#endif
int
sys_getpgrp(struct thread *td, struct getpgrp_args *uap)
{
	struct proc *p = td->td_proc;

	PROC_LOCK(p);
	td->td_retval[0] = p->p_pgrp->pg_id;
	PROC_UNLOCK(p);
	return (0);
}

/* Get an arbitrary pid's process group id */
#ifndef _SYS_SYSPROTO_H_
struct getpgid_args {
	pid_t	pid;
};
#endif
int
sys_getpgid(struct thread *td, struct getpgid_args *uap)
{
	struct proc *p;
	int error;

	if (uap->pid == 0) {
		p = td->td_proc;
		PROC_LOCK(p);
	} else {
		p = pfind(uap->pid);
		if (p == NULL)
			return (ESRCH);
		error = p_cansee(td, p);
		if (error) {
			PROC_UNLOCK(p);
			return (error);
		}
	}
	td->td_retval[0] = p->p_pgrp->pg_id;
	PROC_UNLOCK(p);
	return (0);
}

/*
 * Get an arbitrary pid's session id.
 */
#ifndef _SYS_SYSPROTO_H_
struct getsid_args {
	pid_t	pid;
};
#endif
int
sys_getsid(struct thread *td, struct getsid_args *uap)
{
	struct proc *p;
	int error;

	if (uap->pid == 0) {
		p = td->td_proc;
		PROC_LOCK(p);
	} else {
		p = pfind(uap->pid);
		if (p == NULL)
			return (ESRCH);
		error = p_cansee(td, p);
		if (error) {
			PROC_UNLOCK(p);
			return (error);
		}
	}
	td->td_retval[0] = p->p_session->s_sid;
	PROC_UNLOCK(p);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getuid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_getuid(struct thread *td, struct getuid_args *uap)
{

	td->td_retval[0] = td->td_ucred->cr_ruid;
#if defined(COMPAT_43)
	td->td_retval[1] = td->td_ucred->cr_uid;
#endif
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct geteuid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_geteuid(struct thread *td, struct geteuid_args *uap)
{

	td->td_retval[0] = td->td_ucred->cr_uid;
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getgid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_getgid(struct thread *td, struct getgid_args *uap)
{

	td->td_retval[0] = td->td_ucred->cr_rgid;
#if defined(COMPAT_43)
	td->td_retval[1] = td->td_ucred->cr_groups[0];
#endif
	return (0);
}

/*
 * Get effective group ID.  The "egid" is groups[0], and could be obtained
 * via getgroups.  This syscall exists because it is somewhat painful to do
 * correctly in a library function.
 */
#ifndef _SYS_SYSPROTO_H_
struct getegid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_getegid(struct thread *td, struct getegid_args *uap)
{

	td->td_retval[0] = td->td_ucred->cr_groups[0];
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
int
sys_getgroups(struct thread *td, struct getgroups_args *uap)
{
	struct ucred *cred;
	u_int ngrp;
	int error;

	cred = td->td_ucred;
	ngrp = cred->cr_ngroups;

	if (uap->gidsetsize == 0) {
		error = 0;
		goto out;
	}
	if (uap->gidsetsize < ngrp)
		return (EINVAL);

	error = copyout(cred->cr_groups, uap->gidset, ngrp * sizeof(gid_t));
out:
	td->td_retval[0] = ngrp;
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setsid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
sys_setsid(struct thread *td, struct setsid_args *uap)
{
	struct pgrp *pgrp;
	int error;
	struct proc *p = td->td_proc;
	struct pgrp *newpgrp;
	struct session *newsess;

	error = 0;
	pgrp = NULL;

	newpgrp = malloc(sizeof(struct pgrp), M_PGRP, M_WAITOK | M_ZERO);
	newsess = malloc(sizeof(struct session), M_SESSION, M_WAITOK | M_ZERO);

	sx_xlock(&proctree_lock);

	if (p->p_pgid == p->p_pid || (pgrp = pgfind(p->p_pid)) != NULL) {
		if (pgrp != NULL)
			PGRP_UNLOCK(pgrp);
		error = EPERM;
	} else {
		(void)enterpgrp(p, p->p_pid, newpgrp, newsess);
		td->td_retval[0] = p->p_pid;
		newpgrp = NULL;
		newsess = NULL;
	}

	sx_xunlock(&proctree_lock);

	if (newpgrp != NULL)
		free(newpgrp, M_PGRP);
	if (newsess != NULL)
		free(newsess, M_SESSION);

	return (error);
}

/*
 * set process group (setpgid/old setpgrp)
 *
 * caller does setpgid(targpid, targpgid)
 *
 * pid must be caller or child of caller (ESRCH)
 * if a child
 *	pid must be in same session (EPERM)
 *	pid can't have done an exec (EACCES)
 * if pgid != pid
 * 	there must exist some pid in same session having pgid (EPERM)
 * pid must not be session leader (EPERM)
 */
#ifndef _SYS_SYSPROTO_H_
struct setpgid_args {
	int	pid;		/* target process id */
	int	pgid;		/* target pgrp id */
};
#endif
/* ARGSUSED */
int
sys_setpgid(struct thread *td, struct setpgid_args *uap)
{
	struct proc *curp = td->td_proc;
	struct proc *targp;	/* target process */
	struct pgrp *pgrp;	/* target pgrp */
	int error;
	struct pgrp *newpgrp;

	if (uap->pgid < 0)
		return (EINVAL);

	error = 0;

	newpgrp = malloc(sizeof(struct pgrp), M_PGRP, M_WAITOK | M_ZERO);

	sx_xlock(&proctree_lock);
	if (uap->pid != 0 && uap->pid != curp->p_pid) {
		if ((targp = pfind(uap->pid)) == NULL) {
			error = ESRCH;
			goto done;
		}
		if (!inferior(targp)) {
			PROC_UNLOCK(targp);
			error = ESRCH;
			goto done;
		}
		if ((error = p_cansee(td, targp))) {
			PROC_UNLOCK(targp);
			goto done;
		}
		if (targp->p_pgrp == NULL ||
		    targp->p_session != curp->p_session) {
			PROC_UNLOCK(targp);
			error = EPERM;
			goto done;
		}
		if (targp->p_flag & P_EXEC) {
			PROC_UNLOCK(targp);
			error = EACCES;
			goto done;
		}
		PROC_UNLOCK(targp);
	} else
		targp = curp;
	if (SESS_LEADER(targp)) {
		error = EPERM;
		goto done;
	}
	if (uap->pgid == 0)
		uap->pgid = targp->p_pid;
	if ((pgrp = pgfind(uap->pgid)) == NULL) {
		if (uap->pgid == targp->p_pid) {
			error = enterpgrp(targp, uap->pgid, newpgrp,
			    NULL);
			if (error == 0)
				newpgrp = NULL;
		} else
			error = EPERM;
	} else {
		if (pgrp == targp->p_pgrp) {
			PGRP_UNLOCK(pgrp);
			goto done;
		}
		if (pgrp->pg_id != targp->p_pid &&
		    pgrp->pg_session != curp->p_session) {
			PGRP_UNLOCK(pgrp);
			error = EPERM;
			goto done;
		}
		PGRP_UNLOCK(pgrp);
		error = enterthispgrp(targp, pgrp);
	}
done:
	sx_xunlock(&proctree_lock);
	KASSERT((error == 0) || (newpgrp != NULL),
	    ("setpgid failed and newpgrp is NULL"));
	if (newpgrp != NULL)
		free(newpgrp, M_PGRP);
	return (error);
}

/*
 * Use the clause in B.4.2.2 that allows setuid/setgid to be 4.2/4.3BSD
 * compatible.  It says that setting the uid/gid to euid/egid is a special
 * case of "appropriate privilege".  Once the rules are expanded out, this
 * basically means that setuid(nnn) sets all three id's, in all permitted
 * cases unless _POSIX_SAVED_IDS is enabled.  In that case, setuid(getuid())
 * does not set the saved id - this is dangerous for traditional BSD
 * programs.  For this reason, we *really* do not want to set
 * _POSIX_SAVED_IDS and do not want to clear POSIX_APPENDIX_B_4_2_2.
 */
#define POSIX_APPENDIX_B_4_2_2

#ifndef _SYS_SYSPROTO_H_
struct setuid_args {
	uid_t	uid;
};
#endif
/* ARGSUSED */
int
sys_setuid(struct thread *td, struct setuid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	uid_t uid;
	struct uidinfo *uip;
	int error;

	uid = uap->uid;
	AUDIT_ARG_UID(uid);
	newcred = crget();
	uip = uifind(uid);
	PROC_LOCK(p);
	/*
	 * Copy credentials so other references do not see our changes.
	 */
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setuid(oldcred, uid);
	if (error)
		goto fail;
#endif

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setuid(geteuid()) is a special case of
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatible with traditional BSD
	 * semantics.  Basically, it means that "setuid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * Notes on the logic.  We do things in three steps.
	 * 1: We determine if the euid is going to change, and do EPERM
	 *    right away.  We unconditionally change the euid later if this
	 *    test is satisfied, simplifying that part of the logic.
	 * 2: We determine if the real and/or saved uids are going to
	 *    change.  Determined by compile options.
	 * 3: Change euid last. (after tests in #2 for "appropriate privs")
	 */
	if (uid != oldcred->cr_ruid &&		/* allow setuid(getuid()) */
#ifdef _POSIX_SAVED_IDS
	    uid != oldcred->cr_svuid &&		/* allow setuid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    uid != oldcred->cr_uid &&		/* allow setuid(geteuid()) */
#endif
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETUID)) != 0)
		goto fail;

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or uid == euid)
	 * If so, we are changing the real uid and/or saved uid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use the clause from B.4.2.2 */
	    uid == oldcred->cr_uid ||
#endif
	    /* We are using privs. */
	    priv_check_cred(oldcred, PRIV_CRED_SETUID) == 0)
#endif
	{
		/*
		 * Set the real uid and transfer proc count to new user.
		 */
		if (uid != oldcred->cr_ruid) {
			change_ruid(newcred, uip);
			setsugid(p);
		}
		/*
		 * Set saved uid
		 *
		 * XXX always set saved uid even if not _POSIX_SAVED_IDS, as
		 * the security of seteuid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (uid != oldcred->cr_svuid) {
			change_svuid(newcred, uid);
			setsugid(p);
		}
	}

	/*
	 * In all permitted cases, we are changing the euid.
	 */
	if (uid != oldcred->cr_uid) {
		change_euid(newcred, uip);
		setsugid(p);
	}
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
	uifree(uip);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	uifree(uip);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct seteuid_args {
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
sys_seteuid(struct thread *td, struct seteuid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	uid_t euid;
	struct uidinfo *euip;
	int error;

	euid = uap->euid;
	AUDIT_ARG_EUID(euid);
	newcred = crget();
	euip = uifind(euid);
	PROC_LOCK(p);
	/*
	 * Copy credentials so other references do not see our changes.
	 */
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_seteuid(oldcred, euid);
	if (error)
		goto fail;
#endif

	if (euid != oldcred->cr_ruid &&		/* allow seteuid(getuid()) */
	    euid != oldcred->cr_svuid &&	/* allow seteuid(saved uid) */
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETEUID)) != 0)
		goto fail;

	/*
	 * Everything's okay, do it.
	 */
	if (oldcred->cr_uid != euid) {
		change_euid(newcred, euip);
		setsugid(p);
	}
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	uifree(euip);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	uifree(euip);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setgid_args {
	gid_t	gid;
};
#endif
/* ARGSUSED */
int
sys_setgid(struct thread *td, struct setgid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	gid_t gid;
	int error;

	gid = uap->gid;
	AUDIT_ARG_GID(gid);
	newcred = crget();
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setgid(oldcred, gid);
	if (error)
		goto fail;
#endif

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setgid(getegid()) is a special case of
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatible with traditional BSD
	 * semantics.  Basically, it means that "setgid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * For notes on the logic here, see setuid() above.
	 */
	if (gid != oldcred->cr_rgid &&		/* allow setgid(getgid()) */
#ifdef _POSIX_SAVED_IDS
	    gid != oldcred->cr_svgid &&		/* allow setgid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    gid != oldcred->cr_groups[0] && /* allow setgid(getegid()) */
#endif
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETGID)) != 0)
		goto fail;

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or gid == egid)
	 * If so, we are changing the real uid and saved gid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* use the clause from B.4.2.2 */
	    gid == oldcred->cr_groups[0] ||
#endif
	    /* We are using privs. */
	    priv_check_cred(oldcred, PRIV_CRED_SETGID) == 0)
#endif
	{
		/*
		 * Set real gid
		 */
		if (oldcred->cr_rgid != gid) {
			change_rgid(newcred, gid);
			setsugid(p);
		}
		/*
		 * Set saved gid
		 *
		 * XXX always set saved gid even if not _POSIX_SAVED_IDS, as
		 * the security of setegid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (oldcred->cr_svgid != gid) {
			change_svgid(newcred, gid);
			setsugid(p);
		}
	}
	/*
	 * In all cases permitted cases, we are changing the egid.
	 * Copy credentials so other references do not see our changes.
	 */
	if (oldcred->cr_groups[0] != gid) {
		change_egid(newcred, gid);
		setsugid(p);
	}
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setegid_args {
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
sys_setegid(struct thread *td, struct setegid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	gid_t egid;
	int error;

	egid = uap->egid;
	AUDIT_ARG_EGID(egid);
	newcred = crget();
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setegid(oldcred, egid);
	if (error)
		goto fail;
#endif

	if (egid != oldcred->cr_rgid &&		/* allow setegid(getgid()) */
	    egid != oldcred->cr_svgid &&	/* allow setegid(saved gid) */
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETEGID)) != 0)
		goto fail;

	if (oldcred->cr_groups[0] != egid) {
		change_egid(newcred, egid);
		setsugid(p);
	}
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
/* ARGSUSED */
int
sys_setgroups(struct thread *td, struct setgroups_args *uap)
{
	gid_t smallgroups[XU_NGROUPS];
	gid_t *groups;
	u_int gidsetsize;
	int error;

	gidsetsize = uap->gidsetsize;
	if (gidsetsize > ngroups_max + 1)
		return (EINVAL);

	if (gidsetsize > XU_NGROUPS)
		groups = malloc(gidsetsize * sizeof(gid_t), M_TEMP, M_WAITOK);
	else
		groups = smallgroups;

	error = copyin(uap->gidset, groups, gidsetsize * sizeof(gid_t));
	if (error == 0)
		error = kern_setgroups(td, gidsetsize, groups);

	if (gidsetsize > XU_NGROUPS)
		free(groups, M_TEMP);
	return (error);
}

int
kern_setgroups(struct thread *td, u_int ngrp, gid_t *groups)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	int error;

	MPASS(ngrp <= ngroups_max + 1);
	AUDIT_ARG_GROUPSET(groups, ngrp);
	newcred = crget();
	crextend(newcred, ngrp);
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setgroups(oldcred, ngrp, groups);
	if (error)
		goto fail;
#endif

	error = priv_check_cred(oldcred, PRIV_CRED_SETGROUPS);
	if (error)
		goto fail;

	if (ngrp == 0) {
		/*
		 * setgroups(0, NULL) is a legitimate way of clearing the
		 * groups vector on non-BSD systems (which generally do not
		 * have the egid in the groups[0]).  We risk security holes
		 * when running non-BSD software if we do not do the same.
		 */
		newcred->cr_ngroups = 1;
	} else {
		crsetgroups_locked(newcred, ngrp, groups);
	}
	setsugid(p);
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setreuid_args {
	uid_t	ruid;
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
sys_setreuid(struct thread *td, struct setreuid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	uid_t euid, ruid;
	struct uidinfo *euip, *ruip;
	int error;

	euid = uap->euid;
	ruid = uap->ruid;
	AUDIT_ARG_EUID(euid);
	AUDIT_ARG_RUID(ruid);
	newcred = crget();
	euip = uifind(euid);
	ruip = uifind(ruid);
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setreuid(oldcred, ruid, euid);
	if (error)
		goto fail;
#endif

	if (((ruid != (uid_t)-1 && ruid != oldcred->cr_ruid &&
	      ruid != oldcred->cr_svuid) ||
	     (euid != (uid_t)-1 && euid != oldcred->cr_uid &&
	      euid != oldcred->cr_ruid && euid != oldcred->cr_svuid)) &&
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETREUID)) != 0)
		goto fail;

	if (euid != (uid_t)-1 && oldcred->cr_uid != euid) {
		change_euid(newcred, euip);
		setsugid(p);
	}
	if (ruid != (uid_t)-1 && oldcred->cr_ruid != ruid) {
		change_ruid(newcred, ruip);
		setsugid(p);
	}
	if ((ruid != (uid_t)-1 || newcred->cr_uid != newcred->cr_ruid) &&
	    newcred->cr_svuid != newcred->cr_uid) {
		change_svuid(newcred, newcred->cr_uid);
		setsugid(p);
	}
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
	uifree(ruip);
	uifree(euip);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	uifree(ruip);
	uifree(euip);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct setregid_args {
	gid_t	rgid;
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
sys_setregid(struct thread *td, struct setregid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	gid_t egid, rgid;
	int error;

	egid = uap->egid;
	rgid = uap->rgid;
	AUDIT_ARG_EGID(egid);
	AUDIT_ARG_RGID(rgid);
	newcred = crget();
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setregid(oldcred, rgid, egid);
	if (error)
		goto fail;
#endif

	if (((rgid != (gid_t)-1 && rgid != oldcred->cr_rgid &&
	    rgid != oldcred->cr_svgid) ||
	     (egid != (gid_t)-1 && egid != oldcred->cr_groups[0] &&
	     egid != oldcred->cr_rgid && egid != oldcred->cr_svgid)) &&
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETREGID)) != 0)
		goto fail;

	if (egid != (gid_t)-1 && oldcred->cr_groups[0] != egid) {
		change_egid(newcred, egid);
		setsugid(p);
	}
	if (rgid != (gid_t)-1 && oldcred->cr_rgid != rgid) {
		change_rgid(newcred, rgid);
		setsugid(p);
	}
	if ((rgid != (gid_t)-1 || newcred->cr_groups[0] != newcred->cr_rgid) &&
	    newcred->cr_svgid != newcred->cr_groups[0]) {
		change_svgid(newcred, newcred->cr_groups[0]);
		setsugid(p);
	}
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	crfree(newcred);
	return (error);
}

/*
 * setresuid(ruid, euid, suid) is like setreuid except control over the saved
 * uid is explicit.
 */
#ifndef _SYS_SYSPROTO_H_
struct setresuid_args {
	uid_t	ruid;
	uid_t	euid;
	uid_t	suid;
};
#endif
/* ARGSUSED */
int
sys_setresuid(struct thread *td, struct setresuid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	uid_t euid, ruid, suid;
	struct uidinfo *euip, *ruip;
	int error;

	euid = uap->euid;
	ruid = uap->ruid;
	suid = uap->suid;
	AUDIT_ARG_EUID(euid);
	AUDIT_ARG_RUID(ruid);
	AUDIT_ARG_SUID(suid);
	newcred = crget();
	euip = uifind(euid);
	ruip = uifind(ruid);
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setresuid(oldcred, ruid, euid, suid);
	if (error)
		goto fail;
#endif

	if (((ruid != (uid_t)-1 && ruid != oldcred->cr_ruid &&
	     ruid != oldcred->cr_svuid &&
	      ruid != oldcred->cr_uid) ||
	     (euid != (uid_t)-1 && euid != oldcred->cr_ruid &&
	    euid != oldcred->cr_svuid &&
	      euid != oldcred->cr_uid) ||
	     (suid != (uid_t)-1 && suid != oldcred->cr_ruid &&
	    suid != oldcred->cr_svuid &&
	      suid != oldcred->cr_uid)) &&
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETRESUID)) != 0)
		goto fail;

	if (euid != (uid_t)-1 && oldcred->cr_uid != euid) {
		change_euid(newcred, euip);
		setsugid(p);
	}
	if (ruid != (uid_t)-1 && oldcred->cr_ruid != ruid) {
		change_ruid(newcred, ruip);
		setsugid(p);
	}
	if (suid != (uid_t)-1 && oldcred->cr_svuid != suid) {
		change_svuid(newcred, suid);
		setsugid(p);
	}
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
	uifree(ruip);
	uifree(euip);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	uifree(ruip);
	uifree(euip);
	crfree(newcred);
	return (error);

}

/*
 * setresgid(rgid, egid, sgid) is like setregid except control over the saved
 * gid is explicit.
 */
#ifndef _SYS_SYSPROTO_H_
struct setresgid_args {
	gid_t	rgid;
	gid_t	egid;
	gid_t	sgid;
};
#endif
/* ARGSUSED */
int
sys_setresgid(struct thread *td, struct setresgid_args *uap)
{
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	gid_t egid, rgid, sgid;
	int error;

	egid = uap->egid;
	rgid = uap->rgid;
	sgid = uap->sgid;
	AUDIT_ARG_EGID(egid);
	AUDIT_ARG_RGID(rgid);
	AUDIT_ARG_SGID(sgid);
	newcred = crget();
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);

#ifdef MAC
	error = mac_cred_check_setresgid(oldcred, rgid, egid, sgid);
	if (error)
		goto fail;
#endif

	if (((rgid != (gid_t)-1 && rgid != oldcred->cr_rgid &&
	      rgid != oldcred->cr_svgid &&
	      rgid != oldcred->cr_groups[0]) ||
	     (egid != (gid_t)-1 && egid != oldcred->cr_rgid &&
	      egid != oldcred->cr_svgid &&
	      egid != oldcred->cr_groups[0]) ||
	     (sgid != (gid_t)-1 && sgid != oldcred->cr_rgid &&
	      sgid != oldcred->cr_svgid &&
	      sgid != oldcred->cr_groups[0])) &&
	    (error = priv_check_cred(oldcred, PRIV_CRED_SETRESGID)) != 0)
		goto fail;

	if (egid != (gid_t)-1 && oldcred->cr_groups[0] != egid) {
		change_egid(newcred, egid);
		setsugid(p);
	}
	if (rgid != (gid_t)-1 && oldcred->cr_rgid != rgid) {
		change_rgid(newcred, rgid);
		setsugid(p);
	}
	if (sgid != (gid_t)-1 && oldcred->cr_svgid != sgid) {
		change_svgid(newcred, sgid);
		setsugid(p);
	}
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);

fail:
	PROC_UNLOCK(p);
	crfree(newcred);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct getresuid_args {
	uid_t	*ruid;
	uid_t	*euid;
	uid_t	*suid;
};
#endif
/* ARGSUSED */
int
sys_getresuid(struct thread *td, struct getresuid_args *uap)
{
	struct ucred *cred;
	int error1 = 0, error2 = 0, error3 = 0;

	cred = td->td_ucred;
	if (uap->ruid)
		error1 = copyout(&cred->cr_ruid,
		    uap->ruid, sizeof(cred->cr_ruid));
	if (uap->euid)
		error2 = copyout(&cred->cr_uid,
		    uap->euid, sizeof(cred->cr_uid));
	if (uap->suid)
		error3 = copyout(&cred->cr_svuid,
		    uap->suid, sizeof(cred->cr_svuid));
	return (error1 ? error1 : error2 ? error2 : error3);
}

#ifndef _SYS_SYSPROTO_H_
struct getresgid_args {
	gid_t	*rgid;
	gid_t	*egid;
	gid_t	*sgid;
};
#endif
/* ARGSUSED */
int
sys_getresgid(struct thread *td, struct getresgid_args *uap)
{
	struct ucred *cred;
	int error1 = 0, error2 = 0, error3 = 0;

	cred = td->td_ucred;
	if (uap->rgid)
		error1 = copyout(&cred->cr_rgid,
		    uap->rgid, sizeof(cred->cr_rgid));
	if (uap->egid)
		error2 = copyout(&cred->cr_groups[0],
		    uap->egid, sizeof(cred->cr_groups[0]));
	if (uap->sgid)
		error3 = copyout(&cred->cr_svgid,
		    uap->sgid, sizeof(cred->cr_svgid));
	return (error1 ? error1 : error2 ? error2 : error3);
}

#ifndef _SYS_SYSPROTO_H_
struct issetugid_args {
	int dummy;
};
#endif
/* ARGSUSED */
int
sys_issetugid(struct thread *td, struct issetugid_args *uap)
{
	struct proc *p = td->td_proc;

	/*
	 * Note: OpenBSD sets a P_SUGIDEXEC flag set at execve() time,
	 * we use P_SUGID because we consider changing the owners as
	 * "tainting" as well.
	 * This is significant for procs that start as root and "become"
	 * a user without an exec - programs cannot know *everything*
	 * that libc *might* have put in their data segment.
	 */
	td->td_retval[0] = (p->p_flag & P_SUGID) ? 1 : 0;
	return (0);
}

int
sys___setugid(struct thread *td, struct __setugid_args *uap)
{
#ifdef REGRESSION
	struct proc *p;

	p = td->td_proc;
	switch (uap->flag) {
	case 0:
		PROC_LOCK(p);
		p->p_flag &= ~P_SUGID;
		PROC_UNLOCK(p);
		return (0);
	case 1:
		PROC_LOCK(p);
		p->p_flag |= P_SUGID;
		PROC_UNLOCK(p);
		return (0);
	default:
		return (EINVAL);
	}
#else /* !REGRESSION */

	return (ENOSYS);
#endif /* REGRESSION */
}

/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid_t gid, struct ucred *cred)
{
	int l;
	int h;
	int m;

	if (cred->cr_groups[0] == gid)
		return(1);

	/*
	 * If gid was not our primary group, perform a binary search
	 * of the supplemental groups.  This is possible because we
	 * sort the groups in crsetgroups().
	 */
	l = 1;
	h = cred->cr_ngroups;
	while (l < h) {
		m = l + ((h - l) / 2);
		if (cred->cr_groups[m] < gid)
			l = m + 1; 
		else
			h = m; 
	}
	if ((l < cred->cr_ngroups) && (cred->cr_groups[l] == gid))
		return (1);

	return (0);
}

/*
 * Test the active securelevel against a given level.  securelevel_gt()
 * implements (securelevel > level).  securelevel_ge() implements
 * (securelevel >= level).  Note that the logic is inverted -- these
 * functions return EPERM on "success" and 0 on "failure".
 *
 * Due to care taken when setting the securelevel, we know that no jail will
 * be less secure that its parent (or the physical system), so it is sufficient
 * to test the current jail only.
 *
 * XXXRW: Possibly since this has to do with privilege, it should move to
 * kern_priv.c.
 */
int
securelevel_gt(struct ucred *cr, int level)
{

	return (cr->cr_prison->pr_securelevel > level ? EPERM : 0);
}

int
securelevel_ge(struct ucred *cr, int level)
{

	return (cr->cr_prison->pr_securelevel >= level ? EPERM : 0);
}

/*
 * 'see_other_uids' determines whether or not visibility of processes
 * and sockets with credentials holding different real uids is possible
 * using a variety of system MIBs.
 * XXX: data declarations should be together near the beginning of the file.
 */
static int	see_other_uids = 1;
SYSCTL_INT(_security_bsd, OID_AUTO, see_other_uids, CTLFLAG_RW,
    &see_other_uids, 0,
    "Unprivileged processes may see subjects/objects with different real uid");

/*-
 * Determine if u1 "can see" the subject specified by u2, according to the
 * 'see_other_uids' policy.
 * Returns: 0 for permitted, ESRCH otherwise
 * Locks: none
 * References: *u1 and *u2 must not change during the call
 *             u1 may equal u2, in which case only one reference is required
 */
int
cr_canseeotheruids(struct ucred *u1, struct ucred *u2)
{

	if (!see_other_uids && u1->cr_ruid != u2->cr_ruid) {
		if (priv_check_cred(u1, PRIV_SEEOTHERUIDS) != 0)
			return (ESRCH);
	}
	return (0);
}

/*
 * 'see_other_gids' determines whether or not visibility of processes
 * and sockets with credentials holding different real gids is possible
 * using a variety of system MIBs.
 * XXX: data declarations should be together near the beginning of the file.
 */
static int	see_other_gids = 1;
SYSCTL_INT(_security_bsd, OID_AUTO, see_other_gids, CTLFLAG_RW,
    &see_other_gids, 0,
    "Unprivileged processes may see subjects/objects with different real gid");

/*
 * Determine if u1 can "see" the subject specified by u2, according to the
 * 'see_other_gids' policy.
 * Returns: 0 for permitted, ESRCH otherwise
 * Locks: none
 * References: *u1 and *u2 must not change during the call
 *             u1 may equal u2, in which case only one reference is required
 */
int
cr_canseeothergids(struct ucred *u1, struct ucred *u2)
{
	int i, match;
	
	if (!see_other_gids) {
		match = 0;
		for (i = 0; i < u1->cr_ngroups; i++) {
			if (groupmember(u1->cr_groups[i], u2))
				match = 1;
			if (match)
				break;
		}
		if (!match) {
			if (priv_check_cred(u1, PRIV_SEEOTHERGIDS) != 0)
				return (ESRCH);
		}
	}
	return (0);
}

/*
 * 'see_jail_proc' determines whether or not visibility of processes and
 * sockets with credentials holding different jail ids is possible using a
 * variety of system MIBs.
 *
 * XXX: data declarations should be together near the beginning of the file.
 */

static int	see_jail_proc = 1;
SYSCTL_INT(_security_bsd, OID_AUTO, see_jail_proc, CTLFLAG_RW,
    &see_jail_proc, 0,
    "Unprivileged processes may see subjects/objects with different jail ids");

/*-
 * Determine if u1 "can see" the subject specified by u2, according to the
 * 'see_jail_proc' policy.
 * Returns: 0 for permitted, ESRCH otherwise
 * Locks: none
 * References: *u1 and *u2 must not change during the call
 *             u1 may equal u2, in which case only one reference is required
 */
int
cr_canseejailproc(struct ucred *u1, struct ucred *u2)
{
	if (u1->cr_uid == 0)
		return (0);
	return (!see_jail_proc && u1->cr_prison != u2->cr_prison ? ESRCH : 0);
}

/*-
 * Determine if u1 "can see" the subject specified by u2.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: none
 * References: *u1 and *u2 must not change during the call
 *             u1 may equal u2, in which case only one reference is required
 */
int
cr_cansee(struct ucred *u1, struct ucred *u2)
{
	int error;

	if ((error = prison_check(u1, u2)))
		return (error);
#ifdef MAC
	if ((error = mac_cred_check_visible(u1, u2)))
		return (error);
#endif
	if ((error = cr_canseeotheruids(u1, u2)))
		return (error);
	if ((error = cr_canseeothergids(u1, u2)))
		return (error);
	if ((error = cr_canseejailproc(u1, u2)))
		return (error);
	return (0);
}

/*-
 * Determine if td "can see" the subject specified by p.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: Sufficient locks to protect p->p_ucred must be held.  td really
 *        should be curthread.
 * References: td and p must be valid for the lifetime of the call
 */
int
p_cansee(struct thread *td, struct proc *p)
{

	/* Wrap cr_cansee() for all functionality. */
	KASSERT(td == curthread, ("%s: td not curthread", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	return (cr_cansee(td->td_ucred, p->p_ucred));
}

/*
 * 'conservative_signals' prevents the delivery of a broad class of
 * signals by unprivileged processes to processes that have changed their
 * credentials since the last invocation of execve().  This can prevent
 * the leakage of cached information or retained privileges as a result
 * of a common class of signal-related vulnerabilities.  However, this
 * may interfere with some applications that expect to be able to
 * deliver these signals to peer processes after having given up
 * privilege.
 */
static int	conservative_signals = 1;
SYSCTL_INT(_security_bsd, OID_AUTO, conservative_signals, CTLFLAG_RW,
    &conservative_signals, 0, "Unprivileged processes prevented from "
    "sending certain signals to processes whose credentials have changed");
/*-
 * Determine whether cred may deliver the specified signal to proc.
 * Returns: 0 for permitted, an errno value otherwise.
 * Locks: A lock must be held for proc.
 * References: cred and proc must be valid for the lifetime of the call.
 */
int
cr_cansignal(struct ucred *cred, struct proc *proc, int signum)
{
	int error;

	PROC_LOCK_ASSERT(proc, MA_OWNED);
	/*
	 * Jail semantics limit the scope of signalling to proc in the
	 * same jail as cred, if cred is in jail.
	 */
	error = prison_check(cred, proc->p_ucred);
	if (error)
		return (error);
#ifdef MAC
	if ((error = mac_proc_check_signal(cred, proc, signum)))
		return (error);
#endif
	if ((error = cr_canseeotheruids(cred, proc->p_ucred)))
		return (error);
	if ((error = cr_canseeothergids(cred, proc->p_ucred)))
		return (error);

	/*
	 * UNIX signal semantics depend on the status of the P_SUGID
	 * bit on the target process.  If the bit is set, then additional
	 * restrictions are placed on the set of available signals.
	 */
	if (conservative_signals && (proc->p_flag & P_SUGID)) {
		switch (signum) {
		case 0:
		case SIGKILL:
		case SIGINT:
		case SIGTERM:
		case SIGALRM:
		case SIGSTOP:
		case SIGTTIN:
		case SIGTTOU:
		case SIGTSTP:
		case SIGHUP:
		case SIGUSR1:
		case SIGUSR2:
			/*
			 * Generally, permit job and terminal control
			 * signals.
			 */
			break;
		default:
			/* Not permitted without privilege. */
			error = priv_check_cred(cred, PRIV_SIGNAL_SUGID);
			if (error)
				return (error);
		}
	}

	/*
	 * Generally, the target credential's ruid or svuid must match the
	 * subject credential's ruid or euid.
	 */
	if (cred->cr_ruid != proc->p_ucred->cr_ruid &&
	    cred->cr_ruid != proc->p_ucred->cr_svuid &&
	    cred->cr_uid != proc->p_ucred->cr_ruid &&
	    cred->cr_uid != proc->p_ucred->cr_svuid) {
		error = priv_check_cred(cred, PRIV_SIGNAL_DIFFCRED);
		if (error)
			return (error);
	}

	return (0);
}

/*-
 * Determine whether td may deliver the specified signal to p.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: Sufficient locks to protect various components of td and p
 *        must be held.  td must be curthread, and a lock must be
 *        held for p.
 * References: td and p must be valid for the lifetime of the call
 */
int
p_cansignal(struct thread *td, struct proc *p, int signum)
{

	KASSERT(td == curthread, ("%s: td not curthread", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	if (td->td_proc == p)
		return (0);

	/*
	 * UNIX signalling semantics require that processes in the same
	 * session always be able to deliver SIGCONT to one another,
	 * overriding the remaining protections.
	 */
	/* XXX: This will require an additional lock of some sort. */
	if (signum == SIGCONT && td->td_proc->p_session == p->p_session)
		return (0);
	/*
	 * Some compat layers use SIGTHR and higher signals for
	 * communication between different kernel threads of the same
	 * process, so that they expect that it's always possible to
	 * deliver them, even for suid applications where cr_cansignal() can
	 * deny such ability for security consideration.  It should be
	 * pretty safe to do since the only way to create two processes
	 * with the same p_leader is via rfork(2).
	 */
	if (td->td_proc->p_leader != NULL && signum >= SIGTHR &&
	    signum < SIGTHR + 4 && td->td_proc->p_leader == p->p_leader)
		return (0);

	return (cr_cansignal(td->td_ucred, p, signum));
}

/*-
 * Determine whether td may reschedule p.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: Sufficient locks to protect various components of td and p
 *        must be held.  td must be curthread, and a lock must
 *        be held for p.
 * References: td and p must be valid for the lifetime of the call
 */
int
p_cansched(struct thread *td, struct proc *p)
{
	int error;

	KASSERT(td == curthread, ("%s: td not curthread", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	if (td->td_proc == p)
		return (0);
	if ((error = prison_check(td->td_ucred, p->p_ucred)))
		return (error);
#ifdef MAC
	if ((error = mac_proc_check_sched(td->td_ucred, p)))
		return (error);
#endif
	if ((error = cr_canseeotheruids(td->td_ucred, p->p_ucred)))
		return (error);
	if ((error = cr_canseeothergids(td->td_ucred, p->p_ucred)))
		return (error);
	if (td->td_ucred->cr_ruid != p->p_ucred->cr_ruid &&
	    td->td_ucred->cr_uid != p->p_ucred->cr_ruid) {
		error = priv_check(td, PRIV_SCHED_DIFFCRED);
		if (error)
			return (error);
	}
	return (0);
}

/*
 * Handle getting or setting the prison's unprivileged_proc_debug
 * value.
 */
static int
sysctl_unprivileged_proc_debug(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr;
	int error, val;

	val = prison_allow(req->td->td_ucred, PR_ALLOW_UNPRIV_DEBUG) != 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	pr = req->td->td_ucred->cr_prison;
	mtx_lock(&pr->pr_mtx);
	switch (val) {
	case 0:
		pr->pr_allow &= ~(PR_ALLOW_UNPRIV_DEBUG);
		break;
	case 1:
		pr->pr_allow |= PR_ALLOW_UNPRIV_DEBUG;
		break;
	default:
		error = EINVAL;
	}
	mtx_unlock(&pr->pr_mtx);

	return (error);
}

/*
 * The 'unprivileged_proc_debug' flag may be used to disable a variety of
 * unprivileged inter-process debugging services, including some procfs
 * functionality, ptrace(), and ktrace().  In the past, inter-process
 * debugging has been involved in a variety of security problems, and sites
 * not requiring the service might choose to disable it when hardening
 * systems.
 */
SYSCTL_PROC(_security_bsd, OID_AUTO, unprivileged_proc_debug,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_PRISON | CTLFLAG_SECURE, 0, 0,
    sysctl_unprivileged_proc_debug, "I",
    "Unprivileged processes may use process debugging facilities");

/*-
 * Determine whether td may debug p.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: Sufficient locks to protect various components of td and p
 *        must be held.  td must be curthread, and a lock must
 *        be held for p.
 * References: td and p must be valid for the lifetime of the call
 */
int
p_candebug(struct thread *td, struct proc *p)
{
	int credentialchanged, error, grpsubset, i, uidsubset;

	KASSERT(td == curthread, ("%s: td not curthread", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	if ((error = priv_check(td, PRIV_DEBUG_UNPRIV)))
		return (error);
	if (td->td_proc == p)
		return (0);
	if ((error = prison_check(td->td_ucred, p->p_ucred)))
		return (error);
#ifdef MAC
	if ((error = mac_proc_check_debug(td->td_ucred, p)))
		return (error);
#endif
	if ((error = cr_canseeotheruids(td->td_ucred, p->p_ucred)))
		return (error);
	if ((error = cr_canseeothergids(td->td_ucred, p->p_ucred)))
		return (error);

	/*
	 * Is p's group set a subset of td's effective group set?  This
	 * includes p's egid, group access list, rgid, and svgid.
	 */
	grpsubset = 1;
	for (i = 0; i < p->p_ucred->cr_ngroups; i++) {
		if (!groupmember(p->p_ucred->cr_groups[i], td->td_ucred)) {
			grpsubset = 0;
			break;
		}
	}
	grpsubset = grpsubset &&
	    groupmember(p->p_ucred->cr_rgid, td->td_ucred) &&
	    groupmember(p->p_ucred->cr_svgid, td->td_ucred);

	/*
	 * Are the uids present in p's credential equal to td's
	 * effective uid?  This includes p's euid, svuid, and ruid.
	 */
	uidsubset = (td->td_ucred->cr_uid == p->p_ucred->cr_uid &&
	    td->td_ucred->cr_uid == p->p_ucred->cr_svuid &&
	    td->td_ucred->cr_uid == p->p_ucred->cr_ruid);

	/*
	 * Has the credential of the process changed since the last exec()?
	 */
	credentialchanged = (p->p_flag & P_SUGID);

	/*
	 * If p's gids aren't a subset, or the uids aren't a subset,
	 * or the credential has changed, require appropriate privilege
	 * for td to debug p.
	 */
	if (!grpsubset || !uidsubset) {
		error = priv_check(td, PRIV_DEBUG_DIFFCRED);
		if (error)
			return (error);
	}

	if (credentialchanged) {
		error = priv_check(td, PRIV_DEBUG_SUGID);
		if (error)
			return (error);
	}

	/* Can't trace init when securelevel > 0. */
	if (p == initproc) {
		error = securelevel_gt(td->td_ucred, 0);
		if (error)
			return (error);
	}

	/*
	 * Can't trace a process that's currently exec'ing.
	 *
	 * XXX: Note, this is not a security policy decision, it's a
	 * basic correctness/functionality decision.  Therefore, this check
	 * should be moved to the caller's of p_candebug().
	 */
	if ((p->p_flag & P_INEXEC) != 0)
		return (EBUSY);

	/* Denied explicitely */
	if ((p->p_flag2 & P2_NOTRACE) != 0) {
		error = priv_check(td, PRIV_DEBUG_DENIED);
		if (error != 0)
			return (error);
	}

	return (0);
}

/*-
 * Determine whether the subject represented by cred can "see" a socket.
 * Returns: 0 for permitted, ENOENT otherwise.
 */
int
cr_canseesocket(struct ucred *cred, struct socket *so)
{
	int error;

	error = prison_check(cred, so->so_cred);
	if (error)
		return (ENOENT);
#ifdef MAC
	error = mac_socket_check_visible(cred, so);
	if (error)
		return (error);
#endif
	if (cr_canseeotheruids(cred, so->so_cred))
		return (ENOENT);
	if (cr_canseeothergids(cred, so->so_cred))
		return (ENOENT);

	return (0);
}

/*-
 * Determine whether td can wait for the exit of p.
 * Returns: 0 for permitted, an errno value otherwise
 * Locks: Sufficient locks to protect various components of td and p
 *        must be held.  td must be curthread, and a lock must
 *        be held for p.
 * References: td and p must be valid for the lifetime of the call

 */
int
p_canwait(struct thread *td, struct proc *p)
{
	int error;

	KASSERT(td == curthread, ("%s: td not curthread", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	if ((error = prison_check(td->td_ucred, p->p_ucred)))
		return (error);
#ifdef MAC
	if ((error = mac_proc_check_wait(td->td_ucred, p)))
		return (error);
#endif
#if 0
	/* XXXMAC: This could have odd effects on some shells. */
	if ((error = cr_canseeotheruids(td->td_ucred, p->p_ucred)))
		return (error);
#endif

	return (0);
}

/*
 * Allocate a zeroed cred structure.
 */
struct ucred *
crget(void)
{
	struct ucred *cr;

	cr = malloc(sizeof(*cr), M_CRED, M_WAITOK | M_ZERO);
	refcount_init(&cr->cr_ref, 1);
#ifdef AUDIT
	audit_cred_init(cr);
#endif
#ifdef MAC
	mac_cred_init(cr);
#endif
	cr->cr_groups = cr->cr_smallgroups;
	cr->cr_agroups =
	    sizeof(cr->cr_smallgroups) / sizeof(cr->cr_smallgroups[0]);
	return (cr);
}

/*
 * Claim another reference to a ucred structure.
 */
struct ucred *
crhold(struct ucred *cr)
{

	refcount_acquire(&cr->cr_ref);
	return (cr);
}

/*
 * Free a cred structure.  Throws away space when ref count gets to 0.
 */
void
crfree(struct ucred *cr)
{

	KASSERT(cr->cr_ref > 0, ("bad ucred refcount: %d", cr->cr_ref));
	KASSERT(cr->cr_ref != 0xdeadc0de, ("dangling reference to ucred"));
	if (refcount_release(&cr->cr_ref)) {
		/*
		 * Some callers of crget(), such as nfs_statfs(),
		 * allocate a temporary credential, but don't
		 * allocate a uidinfo structure.
		 */
		if (cr->cr_uidinfo != NULL)
			uifree(cr->cr_uidinfo);
		if (cr->cr_ruidinfo != NULL)
			uifree(cr->cr_ruidinfo);
		/*
		 * Free a prison, if any.
		 */
		if (cr->cr_prison != NULL)
			prison_free(cr->cr_prison);
		if (cr->cr_loginclass != NULL)
			loginclass_free(cr->cr_loginclass);
#ifdef AUDIT
		audit_cred_destroy(cr);
#endif
#ifdef MAC
		mac_cred_destroy(cr);
#endif
		if (cr->cr_groups != cr->cr_smallgroups)
			free(cr->cr_groups, M_CRED);
		free(cr, M_CRED);
	}
}

/*
 * Copy a ucred's contents from a template.  Does not block.
 */
void
crcopy(struct ucred *dest, struct ucred *src)
{

	KASSERT(dest->cr_ref == 1, ("crcopy of shared ucred"));
	bcopy(&src->cr_startcopy, &dest->cr_startcopy,
	    (unsigned)((caddr_t)&src->cr_endcopy -
		(caddr_t)&src->cr_startcopy));
	crsetgroups(dest, src->cr_ngroups, src->cr_groups);
	uihold(dest->cr_uidinfo);
	uihold(dest->cr_ruidinfo);
	prison_hold(dest->cr_prison);
	loginclass_hold(dest->cr_loginclass);
#ifdef AUDIT
	audit_cred_copy(src, dest);
#endif
#ifdef MAC
	mac_cred_copy(src, dest);
#endif
}

/*
 * Dup cred struct to a new held one.
 */
struct ucred *
crdup(struct ucred *cr)
{
	struct ucred *newcr;

	newcr = crget();
	crcopy(newcr, cr);
	return (newcr);
}

/*
 * Fill in a struct xucred based on a struct ucred.
 */
void
cru2x(struct ucred *cr, struct xucred *xcr)
{
	int ngroups;

	bzero(xcr, sizeof(*xcr));
	xcr->cr_version = XUCRED_VERSION;
	xcr->cr_uid = cr->cr_uid;

	ngroups = MIN(cr->cr_ngroups, XU_NGROUPS);
	xcr->cr_ngroups = ngroups;
	bcopy(cr->cr_groups, xcr->cr_groups,
	    ngroups * sizeof(*cr->cr_groups));
}

/*
 * Set initial process credentials.
 * Callers are responsible for providing the reference for provided credentials.
 */
void
proc_set_cred_init(struct proc *p, struct ucred *newcred)
{

	p->p_ucred = newcred;
}

/*
 * Change process credentials.
 * Callers are responsible for providing the reference for passed credentials
 * and for freeing old ones.
 *
 * Process has to be locked except when it does not have credentials (as it
 * should not be visible just yet) or when newcred is NULL (as this can be
 * only used when the process is about to be freed, at which point it should
 * not be visible anymore).
 */
struct ucred *
proc_set_cred(struct proc *p, struct ucred *newcred)
{
	struct ucred *oldcred;

	MPASS(p->p_ucred != NULL);
	if (newcred == NULL)
		MPASS(p->p_state == PRS_ZOMBIE);
	else
		PROC_LOCK_ASSERT(p, MA_OWNED);

	oldcred = p->p_ucred;
	p->p_ucred = newcred;
	if (newcred != NULL)
		PROC_UPDATE_COW(p);
	return (oldcred);
}

struct ucred *
crcopysafe(struct proc *p, struct ucred *cr)
{
	struct ucred *oldcred;
	int groups;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	oldcred = p->p_ucred;
	while (cr->cr_agroups < oldcred->cr_agroups) {
		groups = oldcred->cr_agroups;
		PROC_UNLOCK(p);
		crextend(cr, groups);
		PROC_LOCK(p);
		oldcred = p->p_ucred;
	}
	crcopy(cr, oldcred);

	return (oldcred);
}

/*
 * Extend the passed in credential to hold n items.
 */
void
crextend(struct ucred *cr, int n)
{
	int cnt;

	/* Truncate? */
	if (n <= cr->cr_agroups)
		return;

	/*
	 * We extend by 2 each time since we're using a power of two
	 * allocator until we need enough groups to fill a page.
	 * Once we're allocating multiple pages, only allocate as many
	 * as we actually need.  The case of processes needing a
	 * non-power of two number of pages seems more likely than
	 * a real world process that adds thousands of groups one at a
	 * time.
	 */
	if ( n < PAGE_SIZE / sizeof(gid_t) ) {
		if (cr->cr_agroups == 0)
			cnt = MINALLOCSIZE / sizeof(gid_t);
		else
			cnt = cr->cr_agroups * 2;

		while (cnt < n)
			cnt *= 2;
	} else
		cnt = roundup2(n, PAGE_SIZE / sizeof(gid_t));

	/* Free the old array. */
	if (cr->cr_groups != cr->cr_smallgroups)
		free(cr->cr_groups, M_CRED);

	cr->cr_groups = malloc(cnt * sizeof(gid_t), M_CRED, M_WAITOK | M_ZERO);
	cr->cr_agroups = cnt;
}

/*
 * Copy groups in to a credential, preserving any necessary invariants.
 * Currently this includes the sorting of all supplemental gids.
 * crextend() must have been called before hand to ensure sufficient
 * space is available.
 */
static void
crsetgroups_locked(struct ucred *cr, int ngrp, gid_t *groups)
{
	int i;
	int j;
	gid_t g;
	
	KASSERT(cr->cr_agroups >= ngrp, ("cr_ngroups is too small"));

	bcopy(groups, cr->cr_groups, ngrp * sizeof(gid_t));
	cr->cr_ngroups = ngrp;

	/*
	 * Sort all groups except cr_groups[0] to allow groupmember to
	 * perform a binary search.
	 *
	 * XXX: If large numbers of groups become common this should
	 * be replaced with shell sort like linux uses or possibly
	 * heap sort.
	 */
	for (i = 2; i < ngrp; i++) {
		g = cr->cr_groups[i];
		for (j = i-1; j >= 1 && g < cr->cr_groups[j]; j--)
			cr->cr_groups[j + 1] = cr->cr_groups[j];
		cr->cr_groups[j + 1] = g;
	}
}

/*
 * Copy groups in to a credential after expanding it if required.
 * Truncate the list to (ngroups_max + 1) if it is too large.
 */
void
crsetgroups(struct ucred *cr, int ngrp, gid_t *groups)
{

	if (ngrp > ngroups_max + 1)
		ngrp = ngroups_max + 1;

	crextend(cr, ngrp);
	crsetgroups_locked(cr, ngrp, groups);
}

/*
 * Get login name, if available.
 */
#ifndef _SYS_SYSPROTO_H_
struct getlogin_args {
	char	*namebuf;
	u_int	namelen;
};
#endif
/* ARGSUSED */
int
sys_getlogin(struct thread *td, struct getlogin_args *uap)
{
	char login[MAXLOGNAME];
	struct proc *p = td->td_proc;
	size_t len;

	if (uap->namelen > MAXLOGNAME)
		uap->namelen = MAXLOGNAME;
	PROC_LOCK(p);
	SESS_LOCK(p->p_session);
	len = strlcpy(login, p->p_session->s_login, uap->namelen) + 1;
	SESS_UNLOCK(p->p_session);
	PROC_UNLOCK(p);
	if (len > uap->namelen)
		return (ERANGE);
	return (copyout(login, uap->namebuf, len));
}

/*
 * Set login name.
 */
#ifndef _SYS_SYSPROTO_H_
struct setlogin_args {
	char	*namebuf;
};
#endif
/* ARGSUSED */
int
sys_setlogin(struct thread *td, struct setlogin_args *uap)
{
	struct proc *p = td->td_proc;
	int error;
	char logintmp[MAXLOGNAME];

	CTASSERT(sizeof(p->p_session->s_login) >= sizeof(logintmp));

	error = priv_check(td, PRIV_PROC_SETLOGIN);
	if (error)
		return (error);
	error = copyinstr(uap->namebuf, logintmp, sizeof(logintmp), NULL);
	if (error != 0) {
		if (error == ENAMETOOLONG)
			error = EINVAL;
		return (error);
	}
	AUDIT_ARG_LOGIN(logintmp);
	PROC_LOCK(p);
	SESS_LOCK(p->p_session);
	strcpy(p->p_session->s_login, logintmp);
	SESS_UNLOCK(p->p_session);
	PROC_UNLOCK(p);
	return (0);
}

void
setsugid(struct proc *p)
{

	PROC_LOCK_ASSERT(p, MA_OWNED);
	p->p_flag |= P_SUGID;
	if (!(p->p_pfsflags & PF_ISUGID))
		p->p_stops = 0;
}

/*-
 * Change a process's effective uid.
 * Side effects: newcred->cr_uid and newcred->cr_uidinfo will be modified.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_euid(struct ucred *newcred, struct uidinfo *euip)
{

	newcred->cr_uid = euip->ui_uid;
	uihold(euip);
	uifree(newcred->cr_uidinfo);
	newcred->cr_uidinfo = euip;
}

/*-
 * Change a process's effective gid.
 * Side effects: newcred->cr_gid will be modified.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_egid(struct ucred *newcred, gid_t egid)
{

	newcred->cr_groups[0] = egid;
}

/*-
 * Change a process's real uid.
 * Side effects: newcred->cr_ruid will be updated, newcred->cr_ruidinfo
 *               will be updated, and the old and new cr_ruidinfo proc
 *               counts will be updated.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_ruid(struct ucred *newcred, struct uidinfo *ruip)
{

	(void)chgproccnt(newcred->cr_ruidinfo, -1, 0);
	newcred->cr_ruid = ruip->ui_uid;
	uihold(ruip);
	uifree(newcred->cr_ruidinfo);
	newcred->cr_ruidinfo = ruip;
	(void)chgproccnt(newcred->cr_ruidinfo, 1, 0);
}

/*-
 * Change a process's real gid.
 * Side effects: newcred->cr_rgid will be updated.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_rgid(struct ucred *newcred, gid_t rgid)
{

	newcred->cr_rgid = rgid;
}

/*-
 * Change a process's saved uid.
 * Side effects: newcred->cr_svuid will be updated.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_svuid(struct ucred *newcred, uid_t svuid)
{

	newcred->cr_svuid = svuid;
}

/*-
 * Change a process's saved gid.
 * Side effects: newcred->cr_svgid will be updated.
 * References: newcred must be an exclusive credential reference for the
 *             duration of the call.
 */
void
change_svgid(struct ucred *newcred, gid_t svgid)
{

	newcred->cr_svgid = svgid;
}
