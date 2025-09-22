/*	$OpenBSD: kern_prot.c,v 1.83 2024/10/08 09:05:40 claudio Exp $	*/
/*	$NetBSD: kern_prot.c,v 1.33 1996/02/09 18:59:42 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
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

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/pool.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <machine/tcb.h>

inline void
crset(struct ucred *newcr, const struct ucred *cr)
{
	KASSERT(cr->cr_refcnt.r_refs > 0);
	memcpy(
	    (char *)newcr    + offsetof(struct ucred, cr_startcopy),
	    (const char *)cr + offsetof(struct ucred, cr_startcopy),
	    sizeof(*cr)      - offsetof(struct ucred, cr_startcopy));
}

int
sys_getpid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_p->ps_pid;
	return (0);
}

int
sys_getthrid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_tid + THREAD_PID_OFFSET;
	return (0);
}

int
sys_getppid(struct proc *p, void *v, register_t *retval)
{

	mtx_enter(&p->p_p->ps_mtx);
	*retval = p->p_p->ps_ppid;
	mtx_leave(&p->p_p->ps_mtx);
	return (0);
}

/* Get process group ID; note that POSIX getpgrp takes no parameter */
int
sys_getpgrp(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_p->ps_pgrp->pg_id;
	return (0);
}

/*
 * SysVR.4 compatible getpgid()
 */
int
sys_getpgid(struct proc *curp, void *v, register_t *retval)
{
	struct sys_getpgid_args /* {
		syscallarg(pid_t) pid;
	} */ *uap = v;
	struct process *targpr = curp->p_p;

	if (SCARG(uap, pid) == 0 || SCARG(uap, pid) == targpr->ps_pid)
		goto found;
	if ((targpr = prfind(SCARG(uap, pid))) == NULL)
		return (ESRCH);
	if (targpr->ps_session != curp->p_p->ps_session)
		return (EPERM);
found:
	*retval = targpr->ps_pgid;
	return (0);
}

int
sys_getsid(struct proc *curp, void *v, register_t *retval)
{
	struct sys_getsid_args /* {
		syscallarg(pid_t) pid;
	} */ *uap = v;
	struct process *targpr = curp->p_p;

	if (SCARG(uap, pid) == 0 || SCARG(uap, pid) == targpr->ps_pid)
		goto found;
	if ((targpr = prfind(SCARG(uap, pid))) == NULL)
		return (ESRCH);
	if (targpr->ps_session != curp->p_p->ps_session)
		return (EPERM);
found:
	/* Skip exiting processes */
	if (targpr->ps_pgrp->pg_session->s_leader == NULL)
		return (ESRCH);
	*retval = targpr->ps_pgrp->pg_session->s_leader->ps_pid;
	return (0);
}

int
sys_getuid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_ucred->cr_ruid;
	return (0);
}

int
sys_geteuid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_ucred->cr_uid;
	return (0);
}

int
sys_issetugid(struct proc *p, void *v, register_t *retval)
{
	if (p->p_p->ps_flags & PS_SUGIDEXEC)
		*retval = 1;
	else
		*retval = 0;
	return (0);
}

int
sys_getgid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_ucred->cr_rgid;
	return (0);
}

/*
 * Get effective group ID.  The "egid" is groups[0], and could be obtained
 * via getgroups.  This syscall exists because it is somewhat painful to do
 * correctly in a library function.
 */
int
sys_getegid(struct proc *p, void *v, register_t *retval)
{

	*retval = p->p_ucred->cr_gid;
	return (0);
}

int
sys_getgroups(struct proc *p, void *v, register_t *retval)
{
	struct sys_getgroups_args /* {
		syscallarg(int) gidsetsize;
		syscallarg(gid_t *) gidset;
	} */ *uap = v;
	struct ucred *uc = p->p_ucred;
	int ngrp;
	int error;

	if ((ngrp = SCARG(uap, gidsetsize)) == 0) {
		*retval = uc->cr_ngroups;
		return (0);
	}
	if (ngrp < uc->cr_ngroups)
		return (EINVAL);
	ngrp = uc->cr_ngroups;
	error = copyout(uc->cr_groups, SCARG(uap, gidset),
	    ngrp * sizeof(gid_t));
	if (error)
		return (error);
	*retval = ngrp;
	return (0);
}

int
sys_setsid(struct proc *p, void *v, register_t *retval)
{
	struct session *newsess;
	struct pgrp *newpgrp;
	struct process *pr = p->p_p;
	pid_t pid = pr->ps_pid;

	newsess = pool_get(&session_pool, PR_WAITOK);
	newpgrp = pool_get(&pgrp_pool, PR_WAITOK);

	if (pr->ps_pgid == pid || pgfind(pid) != NULL) {
		pool_put(&pgrp_pool, newpgrp);
		pool_put(&session_pool, newsess);
		return (EPERM);
	} else {
		enternewpgrp(pr, newpgrp, newsess);
		*retval = pid;
		return (0);
	}
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
int
sys_setpgid(struct proc *curp, void *v, register_t *retval)
{
	struct sys_setpgid_args /* {
		syscallarg(pid_t) pid;
		syscallarg(pid_t) pgid;
	} */ *uap = v;
	struct process *curpr = curp->p_p;
	struct process *targpr;		/* target process */
	struct pgrp *pgrp, *newpgrp;	/* target pgrp */
	pid_t pid, pgid;
	int error;

	pid = SCARG(uap, pid);
	pgid = SCARG(uap, pgid);

	if (pgid < 0)
		return (EINVAL);

	newpgrp = pool_get(&pgrp_pool, PR_WAITOK);

	if (pid != 0 && pid != curpr->ps_pid) {
		if ((targpr = prfind(pid)) == NULL ||
		    !inferior(targpr, curpr)) {
			error = ESRCH;
			goto out;
		}
		if (targpr->ps_session != curpr->ps_session) {
			error = EPERM;
			goto out;
		}
		if (targpr->ps_flags & PS_EXEC) {
			error = EACCES;
			goto out;
		}
	} else
		targpr = curpr;
	if (SESS_LEADER(targpr)) {
		error = EPERM;
		goto out;
	}
	if (pgid == 0)
		pgid = targpr->ps_pid;

	error = 0;
	if ((pgrp = pgfind(pgid)) == NULL) {
		/* can only create a new process group with pgid == pid */
		if (pgid != targpr->ps_pid)
			error = EPERM;
		else {
			enternewpgrp(targpr, newpgrp, NULL);
			newpgrp = NULL;
		}
	} else if (pgrp != targpr->ps_pgrp) {		/* anything to do? */
		if (pgid != targpr->ps_pid &&
		    pgrp->pg_session != curpr->ps_session)
			error = EPERM;
		else
			enterthispgrp(targpr, pgrp);
	}
 out:
	if (newpgrp != NULL)
		pool_put(&pgrp_pool, newpgrp);
	return (error);
}

int
sys_getresuid(struct proc *p, void *v, register_t *retval)
{
	struct sys_getresuid_args /* {
		syscallarg(uid_t *) ruid;
		syscallarg(uid_t *) euid;
		syscallarg(uid_t *) suid;
	} */ *uap = v;
	struct ucred *uc = p->p_ucred;
	uid_t *ruid, *euid, *suid;
	int error1 = 0, error2 = 0, error3 = 0;

	ruid = SCARG(uap, ruid);
	euid = SCARG(uap, euid);
	suid = SCARG(uap, suid);

	if (ruid != NULL)
		error1 = copyout(&uc->cr_ruid, ruid, sizeof(*ruid));
	if (euid != NULL)
		error2 = copyout(&uc->cr_uid, euid, sizeof(*euid));
	if (suid != NULL)
		error3 = copyout(&uc->cr_svuid, suid, sizeof(*suid));

	return (error1 ? error1 : error2 ? error2 : error3);
}

int
sys_setresuid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setresuid_args /* {
		syscallarg(uid_t) ruid;
		syscallarg(uid_t) euid;
		syscallarg(uid_t) suid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	uid_t ruid, euid, suid;
	int error;

	ruid = SCARG(uap, ruid);
	euid = SCARG(uap, euid);
	suid = SCARG(uap, suid);

	/*
	 * make permission checks against the thread's ucred,
	 * but the actual changes will be to the process's ucred
	 */
	pruc = pr->ps_ucred;
	if ((ruid == (uid_t)-1 || ruid == pruc->cr_ruid) &&
	    (euid == (uid_t)-1 || euid == pruc->cr_uid) &&
	    (suid == (uid_t)-1 || suid == pruc->cr_svuid))
		return (0);			/* no change */

	/*
	 * Any of the real, effective, and saved uids may be changed
	 * to the current value of one of the three (root is not limited).
	 */
	if (ruid != (uid_t)-1 &&
	    ruid != uc->cr_ruid &&
	    ruid != uc->cr_uid &&
	    ruid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	if (euid != (uid_t)-1 &&
	    euid != uc->cr_ruid &&
	    euid != uc->cr_uid &&
	    euid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	if (suid != (uid_t)-1 &&
	    suid != uc->cr_ruid &&
	    suid != uc->cr_uid &&
	    suid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	/*
	 * Note that unlike the other set*uid() calls, each
	 * uid type is set independently of the others.
	 */
	if (ruid != (uid_t)-1)
		newcred->cr_ruid = ruid;
	if (euid != (uid_t)-1)
		newcred->cr_uid = euid;
	if (suid != (uid_t)-1)
		newcred->cr_svuid = suid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);

	/* now that we can sleep, transfer proc count to new user */
	if (ruid != (uid_t)-1 && ruid != pruc->cr_ruid) {
		chgproccnt(pruc->cr_ruid, -1);
		chgproccnt(ruid, 1);
	}
	crfree(pruc);

	return (0);
}

int
sys_getresgid(struct proc *p, void *v, register_t *retval)
{
	struct sys_getresgid_args /* {
		syscallarg(gid_t *) rgid;
		syscallarg(gid_t *) egid;
		syscallarg(gid_t *) sgid;
	} */ *uap = v;
	struct ucred *uc = p->p_ucred;
	gid_t *rgid, *egid, *sgid;
	int error1 = 0, error2 = 0, error3 = 0;

	rgid = SCARG(uap, rgid);
	egid = SCARG(uap, egid);
	sgid = SCARG(uap, sgid);

	if (rgid != NULL)
		error1 = copyout(&uc->cr_rgid, rgid, sizeof(*rgid));
	if (egid != NULL)
		error2 = copyout(&uc->cr_gid, egid, sizeof(*egid));
	if (sgid != NULL)
		error3 = copyout(&uc->cr_svgid, sgid, sizeof(*sgid));

	return (error1 ? error1 : error2 ? error2 : error3);
}

int
sys_setresgid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setresgid_args /* {
		syscallarg(gid_t) rgid;
		syscallarg(gid_t) egid;
		syscallarg(gid_t) sgid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	gid_t rgid, egid, sgid;
	int error;

	rgid = SCARG(uap, rgid);
	egid = SCARG(uap, egid);
	sgid = SCARG(uap, sgid);

	/*
	 * make permission checks against the thread's ucred,
	 * but the actual changes will be to the process's ucred
	 */
	pruc = pr->ps_ucred;
	if ((rgid == (gid_t)-1 || rgid == pruc->cr_rgid) &&
	    (egid == (gid_t)-1 || egid == pruc->cr_gid) &&
	    (sgid == (gid_t)-1 || sgid == pruc->cr_svgid))
		return (0);			/* no change */

	/*
	 * Any of the real, effective, and saved gids may be changed
	 * to the current value of one of the three (root is not limited).
	 */
	if (rgid != (gid_t)-1 &&
	    rgid != uc->cr_rgid &&
	    rgid != uc->cr_gid &&
	    rgid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	if (egid != (gid_t)-1 &&
	    egid != uc->cr_rgid &&
	    egid != uc->cr_gid &&
	    egid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	if (sgid != (gid_t)-1 &&
	    sgid != uc->cr_rgid &&
	    sgid != uc->cr_gid &&
	    sgid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	/*
	 * Note that unlike the other set*gid() calls, each
	 * gid type is set independently of the others.
	 */
	if (rgid != (gid_t)-1)
		newcred->cr_rgid = rgid;
	if (egid != (gid_t)-1)
		newcred->cr_gid = egid;
	if (sgid != (gid_t)-1)
		newcred->cr_svgid = sgid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
	crfree(pruc);
	return (0);
}

int
sys_setregid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setregid_args /* {
		syscallarg(gid_t) rgid;
		syscallarg(gid_t) egid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	gid_t rgid, egid;
	int error;

	rgid = SCARG(uap, rgid);
	egid = SCARG(uap, egid);

	/*
	 * make permission checks against the thread's ucred,
	 * but the actual changes will be to the process's ucred
	 *
	 * The saved gid check here is complicated: we reset the
	 * saved gid to the real gid if the real gid is specified
	 * *and* either it's changing _or_ the saved gid won't equal
	 * the effective gid.  So, the svgid *won't* change when
	 * the rgid isn't specified or when the rgid isn't changing
	 * and the svgid equals the requested egid.
	 */
	pruc = pr->ps_ucred;
	if ((rgid == (gid_t)-1 || rgid == pruc->cr_rgid) &&
	    (egid == (gid_t)-1 || egid == pruc->cr_gid) &&
	    (rgid == (gid_t)-1 || (rgid == pruc->cr_rgid &&
	    pruc->cr_svgid == (egid != (gid_t)-1 ? egid : pruc->cr_gid))))
		return (0);			/* no change */

	/*
	 * Any of the real, effective, and saved gids may be changed
	 * to the current value of one of the three (root is not limited).
	 */
	if (rgid != (gid_t)-1 &&
	    rgid != uc->cr_rgid &&
	    rgid != uc->cr_gid &&
	    rgid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	if (egid != (gid_t)-1 &&
	    egid != uc->cr_rgid &&
	    egid != uc->cr_gid &&
	    egid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	if (rgid != (gid_t)-1)
		newcred->cr_rgid = rgid;
	if (egid != (gid_t)-1)
		newcred->cr_gid = egid;

	/*
	 * The saved gid presents a bit of a dilemma, as it did not
	 * exist when setregid(2) was conceived.  We only set the saved
	 * gid when the real gid is specified and either its value would
	 * change, or where the saved and effective gids are different.
	 */
	if (rgid != (gid_t)-1 && (rgid != pruc->cr_rgid ||
	    pruc->cr_svgid != (egid != (gid_t)-1 ? egid : pruc->cr_gid)))
		newcred->cr_svgid = rgid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
	crfree(pruc);
	return (0);
}

int
sys_setreuid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setreuid_args /* {
		syscallarg(uid_t) ruid;
		syscallarg(uid_t) euid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	uid_t ruid, euid;
	int error;

	ruid = SCARG(uap, ruid);
	euid = SCARG(uap, euid);

	/*
	 * make permission checks against the thread's ucred,
	 * but the actual changes will be to the process's ucred
	 *
	 * The saved uid check here is complicated: we reset the
	 * saved uid to the real uid if the real uid is specified
	 * *and* either it's changing _or_ the saved uid won't equal
	 * the effective uid.  So, the svuid *won't* change when
	 * the ruid isn't specified or when the ruid isn't changing
	 * and the svuid equals the requested euid.
	 */
	pruc = pr->ps_ucred;
	if ((ruid == (uid_t)-1 || ruid == pruc->cr_ruid) &&
	    (euid == (uid_t)-1 || euid == pruc->cr_uid) &&
	    (ruid == (uid_t)-1 || (ruid == pruc->cr_ruid &&
	    pruc->cr_svuid == (euid != (uid_t)-1 ? euid : pruc->cr_uid))))
		return (0);			/* no change */

	/*
	 * Any of the real, effective, and saved uids may be changed
	 * to the current value of one of the three (root is not limited).
	 */
	if (ruid != (uid_t)-1 &&
	    ruid != uc->cr_ruid &&
	    ruid != uc->cr_uid &&
	    ruid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	if (euid != (uid_t)-1 &&
	    euid != uc->cr_ruid &&
	    euid != uc->cr_uid &&
	    euid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	if (ruid != (uid_t)-1)
		newcred->cr_ruid = ruid;
	if (euid != (uid_t)-1)
		newcred->cr_uid = euid;

	/*
	 * The saved uid presents a bit of a dilemma, as it did not
	 * exist when setreuid(2) was conceived.  We only set the saved
	 * uid when the real uid is specified and either its value would
	 * change, or where the saved and effective uids are different.
	 */
	if (ruid != (uid_t)-1 && (ruid != pruc->cr_ruid ||
	    pruc->cr_svuid != (euid != (uid_t)-1 ? euid : pruc->cr_uid)))
		newcred->cr_svuid = ruid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);

	/* now that we can sleep, transfer proc count to new user */
	if (ruid != (uid_t)-1 && ruid != pruc->cr_ruid) {
		chgproccnt(pruc->cr_ruid, -1);
		chgproccnt(ruid, 1);
	}
	crfree(pruc);

	return (0);
}

int
sys_setuid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setuid_args /* {
		syscallarg(uid_t) uid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	uid_t uid;
	int did_real, error;

	uid = SCARG(uap, uid);

	pruc = pr->ps_ucred;
	if (pruc->cr_uid == uid &&
	    pruc->cr_ruid == uid &&
	    pruc->cr_svuid == uid)
		return (0);

	if (uid != uc->cr_ruid &&
	    uid != uc->cr_svuid &&
	    uid != uc->cr_uid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	/*
	 * Everything's okay, do it.
	 */
	if (uid == pruc->cr_uid || suser(p) == 0) {
		did_real = 1;
		newcred->cr_ruid = uid;
		newcred->cr_svuid = uid;
	} else
		did_real = 0;
	newcred->cr_uid = uid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);

	/*
	 * Transfer proc count to new user.
	 */
	if (did_real && uid != pruc->cr_ruid) {
		chgproccnt(pruc->cr_ruid, -1);
		chgproccnt(uid, 1);
	}
	crfree(pruc);

	return (0);
}

int
sys_seteuid(struct proc *p, void *v, register_t *retval)
{
	struct sys_seteuid_args /* {
		syscallarg(uid_t) euid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	uid_t euid;
	int error;

	euid = SCARG(uap, euid);

	if (pr->ps_ucred->cr_uid == euid)
		return (0);

	if (euid != uc->cr_ruid && euid != uc->cr_svuid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);
	newcred->cr_uid = euid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
	crfree(pruc);
	return (0);
}

int
sys_setgid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setgid_args /* {
		syscallarg(gid_t) gid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	gid_t gid;
	int error;

	gid = SCARG(uap, gid);

	pruc = pr->ps_ucred;
	if (pruc->cr_gid == gid &&
	    pruc->cr_rgid == gid &&
	    pruc->cr_svgid == gid)
		return (0);

	if (gid != uc->cr_rgid &&
	    gid != uc->cr_svgid &&
	    gid != uc->cr_gid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);

	if (gid == pruc->cr_gid || suser(p) == 0) {
		newcred->cr_rgid = gid;
		newcred->cr_svgid = gid;
	}
	newcred->cr_gid = gid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
	crfree(pruc);
	return (0);
}

int
sys_setegid(struct proc *p, void *v, register_t *retval)
{
	struct sys_setegid_args /* {
		syscallarg(gid_t) egid;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred, *uc = p->p_ucred;
	gid_t egid;
	int error;

	egid = SCARG(uap, egid);

	if (pr->ps_ucred->cr_gid == egid)
		return (0);

	if (egid != uc->cr_rgid && egid != uc->cr_svgid &&
	    (error = suser(p)))
		return (error);

	/*
	 * Copy credentials so other references do not see our changes.
	 * ps_ucred may change during the crget().
	 */
	newcred = crget();
	pruc = pr->ps_ucred;
	crset(newcred, pruc);
	newcred->cr_gid = egid;
	pr->ps_ucred = newcred;
	atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
	crfree(pruc);
	return (0);
}

int
sys_setgroups(struct proc *p, void *v, register_t *retval)
{
	struct sys_setgroups_args /* {
		syscallarg(int) gidsetsize;
		syscallarg(const gid_t *) gidset;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct ucred *pruc, *newcred;
	gid_t groups[NGROUPS_MAX];
	int ngrp;
	int error;

	if ((error = suser(p)) != 0)
		return (error);
	ngrp = SCARG(uap, gidsetsize);
	if (ngrp > NGROUPS_MAX || ngrp < 0)
		return (EINVAL);
	error = copyin(SCARG(uap, gidset), groups, ngrp * sizeof(gid_t));
	if (error == 0) {
		newcred = crget();
		pruc = pr->ps_ucred;
		crset(newcred, pruc);
		memcpy(newcred->cr_groups, groups, ngrp * sizeof(gid_t));
		newcred->cr_ngroups = ngrp;
		pr->ps_ucred = newcred;
		atomic_setbits_int(&p->p_p->ps_flags, PS_SUGID);
		crfree(pruc);
	}
	return (error);
}

/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid_t gid, struct ucred *cred)
{
	gid_t *gp;
	gid_t *egp;

	if (cred->cr_gid == gid)
		return (1);
	egp = &(cred->cr_groups[cred->cr_ngroups]);
	for (gp = cred->cr_groups; gp < egp; gp++)
		if (*gp == gid)
			return (1);
	return (0);
}

/*
 * Test whether this process has special user powers.
 * Returns 0 or error.
 */
int
suser(struct proc *p)
{
	struct ucred *cred = p->p_ucred;

	if (cred->cr_uid == 0)
		return (0);
	return (EPERM);
}

/*
 * replacement for old suser, for callers who don't have a process
 */
int
suser_ucred(struct ucred *cred)
{
	if (cred->cr_uid == 0)
		return (0);
	return (EPERM);
}

/*
 * Allocate a zeroed cred structure.
 */
struct ucred *
crget(void)
{
	struct ucred *cr;

	cr = pool_get(&ucred_pool, PR_WAITOK|PR_ZERO);
	refcnt_init(&cr->cr_refcnt);
	return (cr);
}

/*
 * Increment the reference count of a cred structure.
 * Returns the passed structure.
 */
struct ucred *
crhold(struct ucred *cr)
{
	refcnt_take(&cr->cr_refcnt);
	return (cr);
}

/*
 * Free a cred structure.
 * Throws away space when ref count gets to 0.
 */
void
crfree(struct ucred *cr)
{
	if (refcnt_rele(&cr->cr_refcnt))
		pool_put(&ucred_pool, cr);
}

/*
 * Copy cred structure to a new one and free the old one.
 */
struct ucred *
crcopy(struct ucred *cr)
{
	struct ucred *newcr;

	if (!refcnt_shared(&cr->cr_refcnt))
		return (cr);
	newcr = crget();
	*newcr = *cr;
	crfree(cr);
	refcnt_init(&newcr->cr_refcnt);
	return (newcr);
}

/*
 * Dup cred struct to a new held one.
 */
struct ucred *
crdup(struct ucred *cr)
{
	struct ucred *newcr;

	newcr = crget();
	*newcr = *cr;
	refcnt_init(&newcr->cr_refcnt);
	return (newcr);
}

/*
 * Convert the userspace xucred to a kernel ucred
 */
int
crfromxucred(struct ucred *cr, const struct xucred *xcr)
{
	if (xcr->cr_ngroups < 0 || xcr->cr_ngroups > NGROUPS_MAX)
		return (EINVAL);
	refcnt_init(&cr->cr_refcnt);
	cr->cr_uid = xcr->cr_uid;
	cr->cr_gid = xcr->cr_gid;
	cr->cr_ngroups = xcr->cr_ngroups;
	memcpy(cr->cr_groups, xcr->cr_groups,
	    sizeof(cr->cr_groups[0]) * xcr->cr_ngroups);
	return (0);
}

/*
 * Get login name, if available.
 */
int
sys_getlogin_r(struct proc *p, void *v, register_t *retval)
{
	struct sys_getlogin_r_args /* {
		syscallarg(char *) namebuf;
		syscallarg(size_t) namelen;
	} */ *uap = v;
	size_t namelen = SCARG(uap, namelen);
	struct session *s = p->p_p->ps_pgrp->pg_session;
	int error;

	if (namelen > sizeof(s->s_login))
		namelen = sizeof(s->s_login);
	error = copyoutstr(s->s_login, SCARG(uap, namebuf), namelen, NULL);
	if (error == ENAMETOOLONG)
		error = ERANGE;
	*retval = error;
	return (0);
}

/*
 * Set login name.
 */
int
sys_setlogin(struct proc *p, void *v, register_t *retval)
{
	struct sys_setlogin_args /* {
		syscallarg(const char *) namebuf;
	} */ *uap = v;
	struct session *s = p->p_p->ps_pgrp->pg_session;
	char buf[sizeof(s->s_login)];
	int error;

	if ((error = suser(p)) != 0)
		return (error);
	error = copyinstr(SCARG(uap, namebuf), buf, sizeof(buf), NULL);
	if (error == 0)
		strlcpy(s->s_login, buf, sizeof(s->s_login));
	else if (error == ENAMETOOLONG)
		error = EINVAL;
	return (error);
}

/*
 * Check if a process is allowed to raise its privileges.
 */
int
proc_cansugid(struct proc *p)
{
	/* ptrace(2)d processes shouldn't. */
	if ((p->p_p->ps_flags & PS_TRACED) != 0)
		return (0);

	/* processes with shared filedescriptors shouldn't. */
	if (p->p_fd->fd_refcnt > 1)
		return (0);

	/* Allow. */
	return (1);
}

/*
 * Set address of the proc's thread-control-block
 */
int
sys___set_tcb(struct proc *p, void *v, register_t *retval)
{
	struct sys___set_tcb_args /* {
		syscallarg(void *) tcb;
	} */ *uap = v;
	void *tcb = SCARG(uap, tcb);

#ifdef TCB_INVALID
	if (TCB_INVALID(tcb))
		return EINVAL;
#endif /* TCB_INVALID */
	TCB_SET(p, tcb);
	return (0);
}

/*
 * Get address of the proc's thread-control-block
 */
int
sys___get_tcb(struct proc *p, void *v, register_t *retval)
{
	*retval = (register_t)TCB_GET(p);
	return (0);
}

int
sys_getthrname(struct proc *curp, void *v, register_t *retval)
{
	struct sys_getthrname_args /* {
		syscallarg(pid_t) tid;
		syscallarg(char *) name;
		syscallarg(size_t) len;
	} */ *uap = v;
	struct proc *p;
	size_t len;
	int tid = SCARG(uap, tid);
	int error;

	p = tid ? tfind_user(tid, curp->p_p) : curp;
	if (p == NULL)
                return ESRCH;

	len = SCARG(uap, len);
	if (len > sizeof(p->p_name))
		len = sizeof(p->p_name);
	error = copyoutstr(p->p_name, SCARG(uap, name), len, NULL);
	if (error == ENAMETOOLONG)
		error = ERANGE;
	*retval = error;
	return 0;
}

int
sys_setthrname(struct proc *curp, void *v, register_t *retval)
{
	struct sys_setthrname_args /* {
		syscallarg(pid_t) tid;
		syscallarg(const char *) name;
	} */ *uap = v;
	struct proc *p;
	char buf[sizeof p->p_name];
	int tid = SCARG(uap, tid);
	int error;

	p = tid ? tfind_user(tid, curp->p_p) : curp;
	if (p == NULL)
                return ESRCH;

	error = copyinstr(SCARG(uap, name), buf, sizeof buf, NULL);
	if (error == 0)
		strlcpy(p->p_name, buf, sizeof(p->p_name));
	else if (error == ENAMETOOLONG)
		error = EINVAL;
	*retval = error;
	return 0;
}

/*
 * Refresh the thread's reference to the process's credentials
 */
void
dorefreshcreds(struct process *pr, struct proc *p)
{
	struct ucred *uc = p->p_ucred;

	KERNEL_LOCK();		/* XXX should be PROCESS_RLOCK(pr) */
	if (uc != pr->ps_ucred) {
		p->p_ucred = pr->ps_ucred;
		crhold(p->p_ucred);
		crfree(uc);
	}
	KERNEL_UNLOCK();
}
