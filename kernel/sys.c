// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/kernel/sys.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/ksm.h>
#include <linux/perf_event.h>
#include <linux/resource.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/posix-timers.h>
#include <linux/security.h>
#include <linux/random.h>
#include <linux/suspend.h>
#include <linux/tty.h>
#include <linux/signal.h>
#include <linux/cn_proc.h>
#include <linux/getcpu.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/seccomp.h>
#include <linux/cpu.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/gfp.h>
#include <linux/syscore_ops.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/syscall_user_dispatch.h>

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>
#include <linux/user_namespace.h>
#include <linux/time_namespace.h>
#include <linux/binfmts.h>

#include <linux/sched.h>
#include <linux/sched/autogroup.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/stat.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/rcupdate.h>
#include <linux/uidgid.h>
#include <linux/cred.h>

#include <linux/nospec.h>

#include <linux/kmsg_dump.h>
/* Move somewhere else to avoid recompiling? */
#include <generated/utsrelease.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/unistd.h>

#include "uid16.h"

#ifndef SET_UNALIGN_CTL
# define SET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_UNALIGN_CTL
# define GET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEMU_CTL
# define SET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEMU_CTL
# define GET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEXC_CTL
# define SET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEXC_CTL
# define GET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_ENDIAN
# define GET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef SET_ENDIAN
# define SET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef GET_TSC_CTL
# define GET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef SET_TSC_CTL
# define SET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef GET_FP_MODE
# define GET_FP_MODE(a)		(-EINVAL)
#endif
#ifndef SET_FP_MODE
# define SET_FP_MODE(a,b)	(-EINVAL)
#endif
#ifndef SVE_SET_VL
# define SVE_SET_VL(a)		(-EINVAL)
#endif
#ifndef SVE_GET_VL
# define SVE_GET_VL()		(-EINVAL)
#endif
#ifndef SME_SET_VL
# define SME_SET_VL(a)		(-EINVAL)
#endif
#ifndef SME_GET_VL
# define SME_GET_VL()		(-EINVAL)
#endif
#ifndef PAC_RESET_KEYS
# define PAC_RESET_KEYS(a, b)	(-EINVAL)
#endif
#ifndef PAC_SET_ENABLED_KEYS
# define PAC_SET_ENABLED_KEYS(a, b, c)	(-EINVAL)
#endif
#ifndef PAC_GET_ENABLED_KEYS
# define PAC_GET_ENABLED_KEYS(a)	(-EINVAL)
#endif
#ifndef SET_TAGGED_ADDR_CTRL
# define SET_TAGGED_ADDR_CTRL(a)	(-EINVAL)
#endif
#ifndef GET_TAGGED_ADDR_CTRL
# define GET_TAGGED_ADDR_CTRL()		(-EINVAL)
#endif
#ifndef RISCV_V_SET_CONTROL
# define RISCV_V_SET_CONTROL(a)		(-EINVAL)
#endif
#ifndef RISCV_V_GET_CONTROL
# define RISCV_V_GET_CONTROL()		(-EINVAL)
#endif

/*
 * this is where the system-wide overflow UID and GID are defined, for
 * architectures that now have 32-bit UID/GID but didn't in the past
 */

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);

/*
 * the same as above, but for filesystems which can only store a 16-bit
 * UID and GID. as such, this is needed on all architectures
 */

int fs_overflowuid = DEFAULT_FS_OVERFLOWUID;
int fs_overflowgid = DEFAULT_FS_OVERFLOWGID;

EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/*
 * Returns true if current's euid is same as p's uid or euid,
 * or has CAP_SYS_NICE to p's user_ns.
 *
 * Called with rcu_read_lock, creds are safe
 */
static bool set_one_prio_perm(struct task_struct *p)
{
	const struct cred *cred = current_cred(), *pcred = __task_cred(p);

	if (uid_eq(pcred->uid,  cred->euid) ||
	    uid_eq(pcred->euid, cred->euid))
		return true;
	if (ns_capable(pcred->user_ns, CAP_SYS_NICE))
		return true;
	return false;
}

/*
 * set the priority of a task
 * - the caller must hold the RCU read lock
 */
static int set_one_prio(struct task_struct *p, int niceval, int error)
{
	int no_nice;

	if (!set_one_prio_perm(p)) {
		error = -EPERM;
		goto out;
	}
	if (niceval < task_nice(p) && !can_nice(p, niceval)) {
		error = -EACCES;
		goto out;
	}
	no_nice = security_task_setnice(p, niceval);
	if (no_nice) {
		error = no_nice;
		goto out;
	}
	if (error == -ESRCH)
		error = 0;
	set_user_nice(p, niceval);
out:
	return error;
}

SYSCALL_DEFINE3(setpriority, int, which, int, who, int, niceval)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	const struct cred *cred = current_cred();
	int error = -EINVAL;
	struct pid *pgrp;
	kuid_t uid;

	if (which > PRIO_USER || which < PRIO_PROCESS)
		goto out;

	/* normalize: avoid signed division (rounding problems) */
	error = -ESRCH;
	if (niceval < MIN_NICE)
		niceval = MIN_NICE;
	if (niceval > MAX_NICE)
		niceval = MAX_NICE;

	rcu_read_lock();
	switch (which) {
	case PRIO_PROCESS:
		if (who)
			p = find_task_by_vpid(who);
		else
			p = current;
		if (p)
			error = set_one_prio(p, niceval, error);
		break;
	case PRIO_PGRP:
		if (who)
			pgrp = find_vpid(who);
		else
			pgrp = task_pgrp(current);
		read_lock(&tasklist_lock);
		do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
			error = set_one_prio(p, niceval, error);
		} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
		read_unlock(&tasklist_lock);
		break;
	case PRIO_USER:
		uid = make_kuid(cred->user_ns, who);
		user = cred->user;
		if (!who)
			uid = cred->uid;
		else if (!uid_eq(uid, cred->uid)) {
			user = find_user(uid);
			if (!user)
				goto out_unlock;	/* No processes for this user */
		}
		for_each_process_thread(g, p) {
			if (uid_eq(task_uid(p), uid) && task_pid_vnr(p))
				error = set_one_prio(p, niceval, error);
		}
		if (!uid_eq(uid, cred->uid))
			free_uid(user);		/* For find_user() */
		break;
	}
out_unlock:
	rcu_read_unlock();
out:
	return error;
}

/*
 * Ugh. To avoid negative return values, "getpriority()" will
 * not return the normal nice-value, but a negated value that
 * has been offset by 20 (ie it returns 40..1 instead of -20..19)
 * to stay compatible.
 */
SYSCALL_DEFINE2(getpriority, int, which, int, who)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	const struct cred *cred = current_cred();
	long niceval, retval = -ESRCH;
	struct pid *pgrp;
	kuid_t uid;

	if (which > PRIO_USER || which < PRIO_PROCESS)
		return -EINVAL;

	rcu_read_lock();
	switch (which) {
	case PRIO_PROCESS:
		if (who)
			p = find_task_by_vpid(who);
		else
			p = current;
		if (p) {
			niceval = nice_to_rlimit(task_nice(p));
			if (niceval > retval)
				retval = niceval;
		}
		break;
	case PRIO_PGRP:
		if (who)
			pgrp = find_vpid(who);
		else
			pgrp = task_pgrp(current);
		read_lock(&tasklist_lock);
		do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
			niceval = nice_to_rlimit(task_nice(p));
			if (niceval > retval)
				retval = niceval;
		} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
		read_unlock(&tasklist_lock);
		break;
	case PRIO_USER:
		uid = make_kuid(cred->user_ns, who);
		user = cred->user;
		if (!who)
			uid = cred->uid;
		else if (!uid_eq(uid, cred->uid)) {
			user = find_user(uid);
			if (!user)
				goto out_unlock;	/* No processes for this user */
		}
		for_each_process_thread(g, p) {
			if (uid_eq(task_uid(p), uid) && task_pid_vnr(p)) {
				niceval = nice_to_rlimit(task_nice(p));
				if (niceval > retval)
					retval = niceval;
			}
		}
		if (!uid_eq(uid, cred->uid))
			free_uid(user);		/* for find_user() */
		break;
	}
out_unlock:
	rcu_read_unlock();

	return retval;
}

/*
 * Unprivileged users may change the real gid to the effective gid
 * or vice versa.  (BSD-style)
 *
 * If you set the real gid at all, or set the effective gid to a value not
 * equal to the real gid, then the saved gid is set to the new effective gid.
 *
 * This makes it possible for a setgid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX with saved IDs.
 *
 * SMP: There are not races, the GIDs are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */
#ifdef CONFIG_MULTIUSER
long __sys_setregid(gid_t rgid, gid_t egid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t krgid, kegid;

	krgid = make_kgid(ns, rgid);
	kegid = make_kgid(ns, egid);

	if ((rgid != (gid_t) -1) && !gid_valid(krgid))
		return -EINVAL;
	if ((egid != (gid_t) -1) && !gid_valid(kegid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (rgid != (gid_t) -1) {
		if (gid_eq(old->gid, krgid) ||
		    gid_eq(old->egid, krgid) ||
		    ns_capable_setid(old->user_ns, CAP_SETGID))
			new->gid = krgid;
		else
			goto error;
	}
	if (egid != (gid_t) -1) {
		if (gid_eq(old->gid, kegid) ||
		    gid_eq(old->egid, kegid) ||
		    gid_eq(old->sgid, kegid) ||
		    ns_capable_setid(old->user_ns, CAP_SETGID))
			new->egid = kegid;
		else
			goto error;
	}

	if (rgid != (gid_t) -1 ||
	    (egid != (gid_t) -1 && !gid_eq(kegid, old->gid)))
		new->sgid = new->egid;
	new->fsgid = new->egid;

	retval = security_task_fix_setgid(new, old, LSM_SETID_RE);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE2(setregid, gid_t, rgid, gid_t, egid)
{
	return __sys_setregid(rgid, egid);
}

/*
 * setgid() is implemented like SysV w/ SAVED_IDS
 *
 * SMP: Same implicit races as above.
 */
long __sys_setgid(gid_t gid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t kgid;

	kgid = make_kgid(ns, gid);
	if (!gid_valid(kgid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ns_capable_setid(old->user_ns, CAP_SETGID))
		new->gid = new->egid = new->sgid = new->fsgid = kgid;
	else if (gid_eq(kgid, old->gid) || gid_eq(kgid, old->sgid))
		new->egid = new->fsgid = kgid;
	else
		goto error;

	retval = security_task_fix_setgid(new, old, LSM_SETID_ID);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE1(setgid, gid_t, gid)
{
	return __sys_setgid(gid);
}

/*
 * change the user struct in a credentials set to match the new UID
 */
static int set_user(struct cred *new)
{
	struct user_struct *new_user;

	new_user = alloc_uid(new->uid);
	if (!new_user)
		return -EAGAIN;

	free_uid(new->user);
	new->user = new_user;
	return 0;
}

static void flag_nproc_exceeded(struct cred *new)
{
	if (new->ucounts == current_ucounts())
		return;

	/*
	 * We don't fail in case of NPROC limit excess here because too many
	 * poorly written programs don't check set*uid() return code, assuming
	 * it never fails if called by root.  We may still enforce NPROC limit
	 * for programs doing set*uid()+execve() by harmlessly deferring the
	 * failure to the execve() stage.
	 */
	if (is_rlimit_overlimit(new->ucounts, UCOUNT_RLIMIT_NPROC, rlimit(RLIMIT_NPROC)) &&
			new->user != INIT_USER)
		current->flags |= PF_NPROC_EXCEEDED;
	else
		current->flags &= ~PF_NPROC_EXCEEDED;
}

/*
 * Unprivileged users may change the real uid to the effective uid
 * or vice versa.  (BSD-style)
 *
 * If you set the real uid at all, or set the effective uid to a value not
 * equal to the real uid, then the saved uid is set to the new effective uid.
 *
 * This makes it possible for a setuid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX with saved IDs.
 */
long __sys_setreuid(uid_t ruid, uid_t euid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kruid, keuid;

	kruid = make_kuid(ns, ruid);
	keuid = make_kuid(ns, euid);

	if ((ruid != (uid_t) -1) && !uid_valid(kruid))
		return -EINVAL;
	if ((euid != (uid_t) -1) && !uid_valid(keuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ruid != (uid_t) -1) {
		new->uid = kruid;
		if (!uid_eq(old->uid, kruid) &&
		    !uid_eq(old->euid, kruid) &&
		    !ns_capable_setid(old->user_ns, CAP_SETUID))
			goto error;
	}

	if (euid != (uid_t) -1) {
		new->euid = keuid;
		if (!uid_eq(old->uid, keuid) &&
		    !uid_eq(old->euid, keuid) &&
		    !uid_eq(old->suid, keuid) &&
		    !ns_capable_setid(old->user_ns, CAP_SETUID))
			goto error;
	}

	if (!uid_eq(new->uid, old->uid)) {
		retval = set_user(new);
		if (retval < 0)
			goto error;
	}
	if (ruid != (uid_t) -1 ||
	    (euid != (uid_t) -1 && !uid_eq(keuid, old->uid)))
		new->suid = new->euid;
	new->fsuid = new->euid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_RE);
	if (retval < 0)
		goto error;

	retval = set_cred_ucounts(new);
	if (retval < 0)
		goto error;

	flag_nproc_exceeded(new);
	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE2(setreuid, uid_t, ruid, uid_t, euid)
{
	return __sys_setreuid(ruid, euid);
}

/*
 * setuid() is implemented like SysV with SAVED_IDS
 *
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX committee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.
 */
long __sys_setuid(uid_t uid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kuid;

	kuid = make_kuid(ns, uid);
	if (!uid_valid(kuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ns_capable_setid(old->user_ns, CAP_SETUID)) {
		new->suid = new->uid = kuid;
		if (!uid_eq(kuid, old->uid)) {
			retval = set_user(new);
			if (retval < 0)
				goto error;
		}
	} else if (!uid_eq(kuid, old->uid) && !uid_eq(kuid, new->suid)) {
		goto error;
	}

	new->fsuid = new->euid = kuid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_ID);
	if (retval < 0)
		goto error;

	retval = set_cred_ucounts(new);
	if (retval < 0)
		goto error;

	flag_nproc_exceeded(new);
	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE1(setuid, uid_t, uid)
{
	return __sys_setuid(uid);
}


/*
 * This function implements a generic ability to update ruid, euid,
 * and suid.  This allows you to implement the 4.4 compatible seteuid().
 */
long __sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kruid, keuid, ksuid;
	bool ruid_new, euid_new, suid_new;

	kruid = make_kuid(ns, ruid);
	keuid = make_kuid(ns, euid);
	ksuid = make_kuid(ns, suid);

	if ((ruid != (uid_t) -1) && !uid_valid(kruid))
		return -EINVAL;

	if ((euid != (uid_t) -1) && !uid_valid(keuid))
		return -EINVAL;

	if ((suid != (uid_t) -1) && !uid_valid(ksuid))
		return -EINVAL;

	old = current_cred();

	/* check for no-op */
	if ((ruid == (uid_t) -1 || uid_eq(kruid, old->uid)) &&
	    (euid == (uid_t) -1 || (uid_eq(keuid, old->euid) &&
				    uid_eq(keuid, old->fsuid))) &&
	    (suid == (uid_t) -1 || uid_eq(ksuid, old->suid)))
		return 0;

	ruid_new = ruid != (uid_t) -1        && !uid_eq(kruid, old->uid) &&
		   !uid_eq(kruid, old->euid) && !uid_eq(kruid, old->suid);
	euid_new = euid != (uid_t) -1        && !uid_eq(keuid, old->uid) &&
		   !uid_eq(keuid, old->euid) && !uid_eq(keuid, old->suid);
	suid_new = suid != (uid_t) -1        && !uid_eq(ksuid, old->uid) &&
		   !uid_eq(ksuid, old->euid) && !uid_eq(ksuid, old->suid);
	if ((ruid_new || euid_new || suid_new) &&
	    !ns_capable_setid(old->user_ns, CAP_SETUID))
		return -EPERM;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (ruid != (uid_t) -1) {
		new->uid = kruid;
		if (!uid_eq(kruid, old->uid)) {
			retval = set_user(new);
			if (retval < 0)
				goto error;
		}
	}
	if (euid != (uid_t) -1)
		new->euid = keuid;
	if (suid != (uid_t) -1)
		new->suid = ksuid;
	new->fsuid = new->euid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_RES);
	if (retval < 0)
		goto error;

	retval = set_cred_ucounts(new);
	if (retval < 0)
		goto error;

	flag_nproc_exceeded(new);
	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE3(setresuid, uid_t, ruid, uid_t, euid, uid_t, suid)
{
	return __sys_setresuid(ruid, euid, suid);
}

SYSCALL_DEFINE3(getresuid, uid_t __user *, ruidp, uid_t __user *, euidp, uid_t __user *, suidp)
{
	const struct cred *cred = current_cred();
	int retval;
	uid_t ruid, euid, suid;

	ruid = from_kuid_munged(cred->user_ns, cred->uid);
	euid = from_kuid_munged(cred->user_ns, cred->euid);
	suid = from_kuid_munged(cred->user_ns, cred->suid);

	retval = put_user(ruid, ruidp);
	if (!retval) {
		retval = put_user(euid, euidp);
		if (!retval)
			return put_user(suid, suidp);
	}
	return retval;
}

/*
 * Same as above, but for rgid, egid, sgid.
 */
long __sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t krgid, kegid, ksgid;
	bool rgid_new, egid_new, sgid_new;

	krgid = make_kgid(ns, rgid);
	kegid = make_kgid(ns, egid);
	ksgid = make_kgid(ns, sgid);

	if ((rgid != (gid_t) -1) && !gid_valid(krgid))
		return -EINVAL;
	if ((egid != (gid_t) -1) && !gid_valid(kegid))
		return -EINVAL;
	if ((sgid != (gid_t) -1) && !gid_valid(ksgid))
		return -EINVAL;

	old = current_cred();

	/* check for no-op */
	if ((rgid == (gid_t) -1 || gid_eq(krgid, old->gid)) &&
	    (egid == (gid_t) -1 || (gid_eq(kegid, old->egid) &&
				    gid_eq(kegid, old->fsgid))) &&
	    (sgid == (gid_t) -1 || gid_eq(ksgid, old->sgid)))
		return 0;

	rgid_new = rgid != (gid_t) -1        && !gid_eq(krgid, old->gid) &&
		   !gid_eq(krgid, old->egid) && !gid_eq(krgid, old->sgid);
	egid_new = egid != (gid_t) -1        && !gid_eq(kegid, old->gid) &&
		   !gid_eq(kegid, old->egid) && !gid_eq(kegid, old->sgid);
	sgid_new = sgid != (gid_t) -1        && !gid_eq(ksgid, old->gid) &&
		   !gid_eq(ksgid, old->egid) && !gid_eq(ksgid, old->sgid);
	if ((rgid_new || egid_new || sgid_new) &&
	    !ns_capable_setid(old->user_ns, CAP_SETGID))
		return -EPERM;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (rgid != (gid_t) -1)
		new->gid = krgid;
	if (egid != (gid_t) -1)
		new->egid = kegid;
	if (sgid != (gid_t) -1)
		new->sgid = ksgid;
	new->fsgid = new->egid;

	retval = security_task_fix_setgid(new, old, LSM_SETID_RES);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE3(setresgid, gid_t, rgid, gid_t, egid, gid_t, sgid)
{
	return __sys_setresgid(rgid, egid, sgid);
}

SYSCALL_DEFINE3(getresgid, gid_t __user *, rgidp, gid_t __user *, egidp, gid_t __user *, sgidp)
{
	const struct cred *cred = current_cred();
	int retval;
	gid_t rgid, egid, sgid;

	rgid = from_kgid_munged(cred->user_ns, cred->gid);
	egid = from_kgid_munged(cred->user_ns, cred->egid);
	sgid = from_kgid_munged(cred->user_ns, cred->sgid);

	retval = put_user(rgid, rgidp);
	if (!retval) {
		retval = put_user(egid, egidp);
		if (!retval)
			retval = put_user(sgid, sgidp);
	}

	return retval;
}


/*
 * "setfsuid()" sets the fsuid - the uid used for filesystem checks. This
 * is used for "access()" and for the NFS daemon (letting nfsd stay at
 * whatever uid it wants to). It normally shadows "euid", except when
 * explicitly set by setfsuid() or for access..
 */
long __sys_setfsuid(uid_t uid)
{
	const struct cred *old;
	struct cred *new;
	uid_t old_fsuid;
	kuid_t kuid;

	old = current_cred();
	old_fsuid = from_kuid_munged(old->user_ns, old->fsuid);

	kuid = make_kuid(old->user_ns, uid);
	if (!uid_valid(kuid))
		return old_fsuid;

	new = prepare_creds();
	if (!new)
		return old_fsuid;

	if (uid_eq(kuid, old->uid)  || uid_eq(kuid, old->euid)  ||
	    uid_eq(kuid, old->suid) || uid_eq(kuid, old->fsuid) ||
	    ns_capable_setid(old->user_ns, CAP_SETUID)) {
		if (!uid_eq(kuid, old->fsuid)) {
			new->fsuid = kuid;
			if (security_task_fix_setuid(new, old, LSM_SETID_FS) == 0)
				goto change_okay;
		}
	}

	abort_creds(new);
	return old_fsuid;

change_okay:
	commit_creds(new);
	return old_fsuid;
}

SYSCALL_DEFINE1(setfsuid, uid_t, uid)
{
	return __sys_setfsuid(uid);
}

/*
 * Samma på svenska..
 */
long __sys_setfsgid(gid_t gid)
{
	const struct cred *old;
	struct cred *new;
	gid_t old_fsgid;
	kgid_t kgid;

	old = current_cred();
	old_fsgid = from_kgid_munged(old->user_ns, old->fsgid);

	kgid = make_kgid(old->user_ns, gid);
	if (!gid_valid(kgid))
		return old_fsgid;

	new = prepare_creds();
	if (!new)
		return old_fsgid;

	if (gid_eq(kgid, old->gid)  || gid_eq(kgid, old->egid)  ||
	    gid_eq(kgid, old->sgid) || gid_eq(kgid, old->fsgid) ||
	    ns_capable_setid(old->user_ns, CAP_SETGID)) {
		if (!gid_eq(kgid, old->fsgid)) {
			new->fsgid = kgid;
			if (security_task_fix_setgid(new,old,LSM_SETID_FS) == 0)
				goto change_okay;
		}
	}

	abort_creds(new);
	return old_fsgid;

change_okay:
	commit_creds(new);
	return old_fsgid;
}

SYSCALL_DEFINE1(setfsgid, gid_t, gid)
{
	return __sys_setfsgid(gid);
}
#endif /* CONFIG_MULTIUSER */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
SYSCALL_DEFINE0(getpid)
{
	return task_tgid_vnr(current);
}

/* Thread ID - the internal kernel "pid" */
SYSCALL_DEFINE0(gettid)
{
	return task_pid_vnr(current);
}

/*
 * Accessing ->real_parent is not SMP-safe, it could
 * change from under us. However, we can use a stale
 * value of ->real_parent under rcu_read_lock(), see
 * release_task()->call_rcu(delayed_put_task_struct).
 */
SYSCALL_DEFINE0(getppid)
{
	int pid;

	rcu_read_lock();
	pid = task_tgid_vnr(rcu_dereference(current->real_parent));
	rcu_read_unlock();

	return pid;
}

SYSCALL_DEFINE0(getuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_uid());
}

SYSCALL_DEFINE0(geteuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_euid());
}

SYSCALL_DEFINE0(getgid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_gid());
}

SYSCALL_DEFINE0(getegid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_egid());
}

static void do_sys_times(struct tms *tms)
{
	u64 tgutime, tgstime, cutime, cstime;

	thread_group_cputime_adjusted(current, &tgutime, &tgstime);
	cutime = current->signal->cutime;
	cstime = current->signal->cstime;
	tms->tms_utime = nsec_to_clock_t(tgutime);
	tms->tms_stime = nsec_to_clock_t(tgstime);
	tms->tms_cutime = nsec_to_clock_t(cutime);
	tms->tms_cstime = nsec_to_clock_t(cstime);
}

SYSCALL_DEFINE1(times, struct tms __user *, tbuf)
{
	if (tbuf) {
		struct tms tmp;

		do_sys_times(&tmp);
		if (copy_to_user(tbuf, &tmp, sizeof(struct tms)))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return (long) jiffies_64_to_clock_t(get_jiffies_64());
}

#ifdef CONFIG_COMPAT
static compat_clock_t clock_t_to_compat_clock_t(clock_t x)
{
	return compat_jiffies_to_clock_t(clock_t_to_jiffies(x));
}

COMPAT_SYSCALL_DEFINE1(times, struct compat_tms __user *, tbuf)
{
	if (tbuf) {
		struct tms tms;
		struct compat_tms tmp;

		do_sys_times(&tms);
		/* Convert our struct tms to the compat version. */
		tmp.tms_utime = clock_t_to_compat_clock_t(tms.tms_utime);
		tmp.tms_stime = clock_t_to_compat_clock_t(tms.tms_stime);
		tmp.tms_cutime = clock_t_to_compat_clock_t(tms.tms_cutime);
		tmp.tms_cstime = clock_t_to_compat_clock_t(tms.tms_cstime);
		if (copy_to_user(tbuf, &tmp, sizeof(tmp)))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return compat_jiffies_to_clock_t(jiffies);
}
#endif

/*
 * This needs some heavy checking ...
 * I just haven't the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 *
 * !PF_FORKNOEXEC check to conform completely to POSIX.
 */
SYSCALL_DEFINE2(setpgid, pid_t, pid, pid_t, pgid)
{
	struct task_struct *p;
	struct task_struct *group_leader = current->group_leader;
	struct pid *pgrp;
	int err;

	if (!pid)
		pid = task_pid_vnr(group_leader);
	if (!pgid)
		pgid = pid;
	if (pgid < 0)
		return -EINVAL;
	rcu_read_lock();

	/* From this point forward we keep holding onto the tasklist lock
	 * so that our parent does not change from under us. -DaveM
	 */
	write_lock_irq(&tasklist_lock);

	err = -ESRCH;
	p = find_task_by_vpid(pid);
	if (!p)
		goto out;

	err = -EINVAL;
	if (!thread_group_leader(p))
		goto out;

	if (same_thread_group(p->real_parent, group_leader)) {
		err = -EPERM;
		if (task_session(p) != task_session(group_leader))
			goto out;
		err = -EACCES;
		if (!(p->flags & PF_FORKNOEXEC))
			goto out;
	} else {
		err = -ESRCH;
		if (p != group_leader)
			goto out;
	}

	err = -EPERM;
	if (p->signal->leader)
		goto out;

	pgrp = task_pid(p);
	if (pgid != pid) {
		struct task_struct *g;

		pgrp = find_vpid(pgid);
		g = pid_task(pgrp, PIDTYPE_PGID);
		if (!g || task_session(g) != task_session(group_leader))
			goto out;
	}

	err = security_task_setpgid(p, pgid);
	if (err)
		goto out;

	if (task_pgrp(p) != pgrp)
		change_pid(p, PIDTYPE_PGID, pgrp);

	err = 0;
out:
	/* All paths lead to here, thus we are safe. -DaveM */
	write_unlock_irq(&tasklist_lock);
	rcu_read_unlock();
	return err;
}

static int do_getpgid(pid_t pid)
{
	struct task_struct *p;
	struct pid *grp;
	int retval;

	rcu_read_lock();
	if (!pid)
		grp = task_pgrp(current);
	else {
		retval = -ESRCH;
		p = find_task_by_vpid(pid);
		if (!p)
			goto out;
		grp = task_pgrp(p);
		if (!grp)
			goto out;

		retval = security_task_getpgid(p);
		if (retval)
			goto out;
	}
	retval = pid_vnr(grp);
out:
	rcu_read_unlock();
	return retval;
}

SYSCALL_DEFINE1(getpgid, pid_t, pid)
{
	return do_getpgid(pid);
}

#ifdef __ARCH_WANT_SYS_GETPGRP

SYSCALL_DEFINE0(getpgrp)
{
	return do_getpgid(0);
}

#endif

SYSCALL_DEFINE1(getsid, pid_t, pid)
{
	struct task_struct *p;
	struct pid *sid;
	int retval;

	rcu_read_lock();
	if (!pid)
		sid = task_session(current);
	else {
		retval = -ESRCH;
		p = find_task_by_vpid(pid);
		if (!p)
			goto out;
		sid = task_session(p);
		if (!sid)
			goto out;

		retval = security_task_getsid(p);
		if (retval)
			goto out;
	}
	retval = pid_vnr(sid);
out:
	rcu_read_unlock();
	return retval;
}

static void set_special_pids(struct pid *pid)
{
	struct task_struct *curr = current->group_leader;

	if (task_session(curr) != pid)
		change_pid(curr, PIDTYPE_SID, pid);

	if (task_pgrp(curr) != pid)
		change_pid(curr, PIDTYPE_PGID, pid);
}

int ksys_setsid(void)
{
	struct task_struct *group_leader = current->group_leader;
	struct pid *sid = task_pid(group_leader);
	pid_t session = pid_vnr(sid);
	int err = -EPERM;

	write_lock_irq(&tasklist_lock);
	/* Fail if I am already a session leader */
	if (group_leader->signal->leader)
		goto out;

	/* Fail if a process group id already exists that equals the
	 * proposed session id.
	 */
	if (pid_task(sid, PIDTYPE_PGID))
		goto out;

	group_leader->signal->leader = 1;
	set_special_pids(sid);

	proc_clear_tty(group_leader);

	err = session;
out:
	write_unlock_irq(&tasklist_lock);
	if (err > 0) {
		proc_sid_connector(group_leader);
		sched_autogroup_create_attach(group_leader);
	}
	return err;
}

SYSCALL_DEFINE0(setsid)
{
	return ksys_setsid();
}

DECLARE_RWSEM(uts_sem);

#ifdef COMPAT_UTS_MACHINE
#define override_architecture(name) \
	(personality(current->personality) == PER_LINUX32 && \
	 copy_to_user(name->machine, COMPAT_UTS_MACHINE, \
		      sizeof(COMPAT_UTS_MACHINE)))
#else
#define override_architecture(name)	0
#endif

/*
 * Work around broken programs that cannot handle "Linux 3.0".
 * Instead we map 3.x to 2.6.40+x, so e.g. 3.0 would be 2.6.40
 * And we map 4.x and later versions to 2.6.60+x, so 4.0/5.0/6.0/... would be
 * 2.6.60.
 */
static int override_release(char __user *release, size_t len)
{
	int ret = 0;

	if (current->personality & UNAME26) {
		const char *rest = UTS_RELEASE;
		char buf[65] = { 0 };
		int ndots = 0;
		unsigned v;
		size_t copy;

		while (*rest) {
			if (*rest == '.' && ++ndots >= 3)
				break;
			if (!isdigit(*rest) && *rest != '.')
				break;
			rest++;
		}
		v = LINUX_VERSION_PATCHLEVEL + 60;
		copy = clamp_t(size_t, len, 1, sizeof(buf));
		copy = scnprintf(buf, copy, "2.6.%u%s", v, rest);
		ret = copy_to_user(release, buf, copy + 1);
	}
	return ret;
}

SYSCALL_DEFINE1(newuname, struct new_utsname __user *, name)
{
	struct new_utsname tmp;

	down_read(&uts_sem);
	memcpy(&tmp, utsname(), sizeof(tmp));
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	if (override_architecture(name))
		return -EFAULT;
	return 0;
}

#ifdef __ARCH_WANT_SYS_OLD_UNAME
/*
 * Old cruft
 */
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name)
{
	struct old_utsname tmp;

	if (!name)
		return -EFAULT;

	down_read(&uts_sem);
	memcpy(&tmp, utsname(), sizeof(tmp));
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	if (override_architecture(name))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE1(olduname, struct oldold_utsname __user *, name)
{
	struct oldold_utsname tmp;

	if (!name)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	down_read(&uts_sem);
	memcpy(&tmp.sysname, &utsname()->sysname, __OLD_UTS_LEN);
	memcpy(&tmp.nodename, &utsname()->nodename, __OLD_UTS_LEN);
	memcpy(&tmp.release, &utsname()->release, __OLD_UTS_LEN);
	memcpy(&tmp.version, &utsname()->version, __OLD_UTS_LEN);
	memcpy(&tmp.machine, &utsname()->machine, __OLD_UTS_LEN);
	up_read(&uts_sem);
	if (copy_to_user(name, &tmp, sizeof(tmp)))
		return -EFAULT;

	if (override_architecture(name))
		return -EFAULT;
	if (override_release(name->release, sizeof(name->release)))
		return -EFAULT;
	return 0;
}
#endif

SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!ns_capable(current->nsproxy->uts_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		struct new_utsname *u;

		add_device_randomness(tmp, len);
		down_write(&uts_sem);
		u = utsname();
		memcpy(u->nodename, tmp, len);
		memset(u->nodename + len, 0, sizeof(u->nodename) - len);
		errno = 0;
		uts_proc_notify(UTS_PROC_HOSTNAME);
		up_write(&uts_sem);
	}
	return errno;
}

#ifdef __ARCH_WANT_SYS_GETHOSTNAME

SYSCALL_DEFINE2(gethostname, char __user *, name, int, len)
{
	int i;
	struct new_utsname *u;
	char tmp[__NEW_UTS_LEN + 1];

	if (len < 0)
		return -EINVAL;
	down_read(&uts_sem);
	u = utsname();
	i = 1 + strlen(u->nodename);
	if (i > len)
		i = len;
	memcpy(tmp, u->nodename, i);
	up_read(&uts_sem);
	if (copy_to_user(name, tmp, i))
		return -EFAULT;
	return 0;
}

#endif

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!ns_capable(current->nsproxy->uts_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;

	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		struct new_utsname *u;

		add_device_randomness(tmp, len);
		down_write(&uts_sem);
		u = utsname();
		memcpy(u->domainname, tmp, len);
		memset(u->domainname + len, 0, sizeof(u->domainname) - len);
		errno = 0;
		uts_proc_notify(UTS_PROC_DOMAINNAME);
		up_write(&uts_sem);
	}
	return errno;
}

/* make sure you are allowed to change @tsk limits before calling this */
static int do_prlimit(struct task_struct *tsk, unsigned int resource,
		      struct rlimit *new_rlim, struct rlimit *old_rlim)
{
	struct rlimit *rlim;
	int retval = 0;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	resource = array_index_nospec(resource, RLIM_NLIMITS);

	if (new_rlim) {
		if (new_rlim->rlim_cur > new_rlim->rlim_max)
			return -EINVAL;
		if (resource == RLIMIT_NOFILE &&
				new_rlim->rlim_max > sysctl_nr_open)
			return -EPERM;
	}

	/* Holding a refcount on tsk protects tsk->signal from disappearing. */
	rlim = tsk->signal->rlim + resource;
	task_lock(tsk->group_leader);
	if (new_rlim) {
		/*
		 * Keep the capable check against init_user_ns until cgroups can
		 * contain all limits.
		 */
		if (new_rlim->rlim_max > rlim->rlim_max &&
				!capable(CAP_SYS_RESOURCE))
			retval = -EPERM;
		if (!retval)
			retval = security_task_setrlimit(tsk, resource, new_rlim);
	}
	if (!retval) {
		if (old_rlim)
			*old_rlim = *rlim;
		if (new_rlim)
			*rlim = *new_rlim;
	}
	task_unlock(tsk->group_leader);

	/*
	 * RLIMIT_CPU handling. Arm the posix CPU timer if the limit is not
	 * infinite. In case of RLIM_INFINITY the posix CPU timer code
	 * ignores the rlimit.
	 */
	if (!retval && new_rlim && resource == RLIMIT_CPU &&
	    new_rlim->rlim_cur != RLIM_INFINITY &&
	    IS_ENABLED(CONFIG_POSIX_TIMERS)) {
		/*
		 * update_rlimit_cpu can fail if the task is exiting, but there
		 * may be other tasks in the thread group that are not exiting,
		 * and they need their cpu timers adjusted.
		 *
		 * The group_leader is the last task to be released, so if we
		 * cannot update_rlimit_cpu on it, then the entire process is
		 * exiting and we do not need to update at all.
		 */
		update_rlimit_cpu(tsk->group_leader, new_rlim->rlim_cur);
	}

	return retval;
}

SYSCALL_DEFINE2(getrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit value;
	int ret;

	ret = do_prlimit(current, resource, NULL, &value);
	if (!ret)
		ret = copy_to_user(rlim, &value, sizeof(*rlim)) ? -EFAULT : 0;

	return ret;
}

#ifdef CONFIG_COMPAT

COMPAT_SYSCALL_DEFINE2(setrlimit, unsigned int, resource,
		       struct compat_rlimit __user *, rlim)
{
	struct rlimit r;
	struct compat_rlimit r32;

	if (copy_from_user(&r32, rlim, sizeof(struct compat_rlimit)))
		return -EFAULT;

	if (r32.rlim_cur == COMPAT_RLIM_INFINITY)
		r.rlim_cur = RLIM_INFINITY;
	else
		r.rlim_cur = r32.rlim_cur;
	if (r32.rlim_max == COMPAT_RLIM_INFINITY)
		r.rlim_max = RLIM_INFINITY;
	else
		r.rlim_max = r32.rlim_max;
	return do_prlimit(current, resource, &r, NULL);
}

COMPAT_SYSCALL_DEFINE2(getrlimit, unsigned int, resource,
		       struct compat_rlimit __user *, rlim)
{
	struct rlimit r;
	int ret;

	ret = do_prlimit(current, resource, NULL, &r);
	if (!ret) {
		struct compat_rlimit r32;
		if (r.rlim_cur > COMPAT_RLIM_INFINITY)
			r32.rlim_cur = COMPAT_RLIM_INFINITY;
		else
			r32.rlim_cur = r.rlim_cur;
		if (r.rlim_max > COMPAT_RLIM_INFINITY)
			r32.rlim_max = COMPAT_RLIM_INFINITY;
		else
			r32.rlim_max = r.rlim_max;

		if (copy_to_user(rlim, &r32, sizeof(struct compat_rlimit)))
			return -EFAULT;
	}
	return ret;
}

#endif

#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT

/*
 *	Back compatibility for getrlimit. Needed for some apps.
 */
SYSCALL_DEFINE2(old_getrlimit, unsigned int, resource,
		struct rlimit __user *, rlim)
{
	struct rlimit x;
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	resource = array_index_nospec(resource, RLIM_NLIMITS);
	task_lock(current->group_leader);
	x = current->signal->rlim[resource];
	task_unlock(current->group_leader);
	if (x.rlim_cur > 0x7FFFFFFF)
		x.rlim_cur = 0x7FFFFFFF;
	if (x.rlim_max > 0x7FFFFFFF)
		x.rlim_max = 0x7FFFFFFF;
	return copy_to_user(rlim, &x, sizeof(x)) ? -EFAULT : 0;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(old_getrlimit, unsigned int, resource,
		       struct compat_rlimit __user *, rlim)
{
	struct rlimit r;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	resource = array_index_nospec(resource, RLIM_NLIMITS);
	task_lock(current->group_leader);
	r = current->signal->rlim[resource];
	task_unlock(current->group_leader);
	if (r.rlim_cur > 0x7FFFFFFF)
		r.rlim_cur = 0x7FFFFFFF;
	if (r.rlim_max > 0x7FFFFFFF)
		r.rlim_max = 0x7FFFFFFF;

	if (put_user(r.rlim_cur, &rlim->rlim_cur) ||
	    put_user(r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	return 0;
}
#endif

#endif

static inline bool rlim64_is_infinity(__u64 rlim64)
{
#if BITS_PER_LONG < 64
	return rlim64 >= ULONG_MAX;
#else
	return rlim64 == RLIM64_INFINITY;
#endif
}

static void rlim_to_rlim64(const struct rlimit *rlim, struct rlimit64 *rlim64)
{
	if (rlim->rlim_cur == RLIM_INFINITY)
		rlim64->rlim_cur = RLIM64_INFINITY;
	else
		rlim64->rlim_cur = rlim->rlim_cur;
	if (rlim->rlim_max == RLIM_INFINITY)
		rlim64->rlim_max = RLIM64_INFINITY;
	else
		rlim64->rlim_max = rlim->rlim_max;
}

static void rlim64_to_rlim(const struct rlimit64 *rlim64, struct rlimit *rlim)
{
	if (rlim64_is_infinity(rlim64->rlim_cur))
		rlim->rlim_cur = RLIM_INFINITY;
	else
		rlim->rlim_cur = (unsigned long)rlim64->rlim_cur;
	if (rlim64_is_infinity(rlim64->rlim_max))
		rlim->rlim_max = RLIM_INFINITY;
	else
		rlim->rlim_max = (unsigned long)rlim64->rlim_max;
}

/* rcu lock must be held */
static int check_prlimit_permission(struct task_struct *task,
				    unsigned int flags)
{
	const struct cred *cred = current_cred(), *tcred;
	bool id_match;

	if (current == task)
		return 0;

	tcred = __task_cred(task);
	id_match = (uid_eq(cred->uid, tcred->euid) &&
		    uid_eq(cred->uid, tcred->suid) &&
		    uid_eq(cred->uid, tcred->uid)  &&
		    gid_eq(cred->gid, tcred->egid) &&
		    gid_eq(cred->gid, tcred->sgid) &&
		    gid_eq(cred->gid, tcred->gid));
	if (!id_match && !ns_capable(tcred->user_ns, CAP_SYS_RESOURCE))
		return -EPERM;

	return security_task_prlimit(cred, tcred, flags);
}

SYSCALL_DEFINE4(prlimit64, pid_t, pid, unsigned int, resource,
		const struct rlimit64 __user *, new_rlim,
		struct rlimit64 __user *, old_rlim)
{
	struct rlimit64 old64, new64;
	struct rlimit old, new;
	struct task_struct *tsk;
	unsigned int checkflags = 0;
	int ret;

	if (old_rlim)
		checkflags |= LSM_PRLIMIT_READ;

	if (new_rlim) {
		if (copy_from_user(&new64, new_rlim, sizeof(new64)))
			return -EFAULT;
		rlim64_to_rlim(&new64, &new);
		checkflags |= LSM_PRLIMIT_WRITE;
	}

	rcu_read_lock();
	tsk = pid ? find_task_by_vpid(pid) : current;
	if (!tsk) {
		rcu_read_unlock();
		return -ESRCH;
	}
	ret = check_prlimit_permission(tsk, checkflags);
	if (ret) {
		rcu_read_unlock();
		return ret;
	}
	get_task_struct(tsk);
	rcu_read_unlock();

	ret = do_prlimit(tsk, resource, new_rlim ? &new : NULL,
			old_rlim ? &old : NULL);

	if (!ret && old_rlim) {
		rlim_to_rlim64(&old, &old64);
		if (copy_to_user(old_rlim, &old64, sizeof(old64)))
			ret = -EFAULT;
	}

	put_task_struct(tsk);
	return ret;
}

SYSCALL_DEFINE2(setrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit new_rlim;

	if (copy_from_user(&new_rlim, rlim, sizeof(*rlim)))
		return -EFAULT;
	return do_prlimit(current, resource, &new_rlim, NULL);
}

/*
 * It would make sense to put struct rusage in the task_struct,
 * except that would make the task_struct be *really big*.  After
 * task_struct gets moved into malloc'ed memory, it would
 * make sense to do this.  It will make moving the rest of the information
 * a lot simpler!  (Which we're not doing right now because we're not
 * measuring them yet).
 *
 * When sampling multiple threads for RUSAGE_SELF, under SMP we might have
 * races with threads incrementing their own counters.  But since word
 * reads are atomic, we either get new values or old values and we don't
 * care which for the sums.  We always take the siglock to protect reading
 * the c* fields from p->signal from races with exit.c updating those
 * fields when reaping, so a sample either gets all the additions of a
 * given child after it's reaped, or none so this sample is before reaping.
 *
 * Locking:
 * We need to take the siglock for CHILDEREN, SELF and BOTH
 * for  the cases current multithreaded, non-current single threaded
 * non-current multithreaded.  Thread traversal is now safe with
 * the siglock held.
 * Strictly speaking, we donot need to take the siglock if we are current and
 * single threaded,  as no one else can take our signal_struct away, no one
 * else can  reap the  children to update signal->c* counters, and no one else
 * can race with the signal-> fields. If we do not take any lock, the
 * signal-> fields could be read out of order while another thread was just
 * exiting. So we should  place a read memory barrier when we avoid the lock.
 * On the writer side,  write memory barrier is implied in  __exit_signal
 * as __exit_signal releases  the siglock spinlock after updating the signal->
 * fields. But we don't do this yet to keep things simple.
 *
 */

static void accumulate_thread_rusage(struct task_struct *t, struct rusage *r)
{
	r->ru_nvcsw += t->nvcsw;
	r->ru_nivcsw += t->nivcsw;
	r->ru_minflt += t->min_flt;
	r->ru_majflt += t->maj_flt;
	r->ru_inblock += task_io_get_inblock(t);
	r->ru_oublock += task_io_get_oublock(t);
}

void getrusage(struct task_struct *p, int who, struct rusage *r)
{
	struct task_struct *t;
	unsigned long flags;
	u64 tgutime, tgstime, utime, stime;
	unsigned long maxrss = 0;
	struct signal_struct *sig = p->signal;

	memset((char *)r, 0, sizeof (*r));
	utime = stime = 0;

	if (who == RUSAGE_THREAD) {
		task_cputime_adjusted(current, &utime, &stime);
		accumulate_thread_rusage(p, r);
		maxrss = sig->maxrss;
		goto out;
	}

	if (!lock_task_sighand(p, &flags))
		return;

	switch (who) {
	case RUSAGE_BOTH:
	case RUSAGE_CHILDREN:
		utime = sig->cutime;
		stime = sig->cstime;
		r->ru_nvcsw = sig->cnvcsw;
		r->ru_nivcsw = sig->cnivcsw;
		r->ru_minflt = sig->cmin_flt;
		r->ru_majflt = sig->cmaj_flt;
		r->ru_inblock = sig->cinblock;
		r->ru_oublock = sig->coublock;
		maxrss = sig->cmaxrss;

		if (who == RUSAGE_CHILDREN)
			break;
		fallthrough;

	case RUSAGE_SELF:
		thread_group_cputime_adjusted(p, &tgutime, &tgstime);
		utime += tgutime;
		stime += tgstime;
		r->ru_nvcsw += sig->nvcsw;
		r->ru_nivcsw += sig->nivcsw;
		r->ru_minflt += sig->min_flt;
		r->ru_majflt += sig->maj_flt;
		r->ru_inblock += sig->inblock;
		r->ru_oublock += sig->oublock;
		if (maxrss < sig->maxrss)
			maxrss = sig->maxrss;
		__for_each_thread(sig, t)
			accumulate_thread_rusage(t, r);
		break;

	default:
		BUG();
	}
	unlock_task_sighand(p, &flags);

out:
	r->ru_utime = ns_to_kernel_old_timeval(utime);
	r->ru_stime = ns_to_kernel_old_timeval(stime);

	if (who != RUSAGE_CHILDREN) {
		struct mm_struct *mm = get_task_mm(p);

		if (mm) {
			setmax_mm_hiwater_rss(&maxrss, mm);
			mmput(mm);
		}
	}
	r->ru_maxrss = maxrss * (PAGE_SIZE / 1024); /* convert pages to KBs */
}

SYSCALL_DEFINE2(getrusage, int, who, struct rusage __user *, ru)
{
	struct rusage r;

	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN &&
	    who != RUSAGE_THREAD)
		return -EINVAL;

	getrusage(current, who, &r);
	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(getrusage, int, who, struct compat_rusage __user *, ru)
{
	struct rusage r;

	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN &&
	    who != RUSAGE_THREAD)
		return -EINVAL;

	getrusage(current, who, &r);
	return put_compat_rusage(&r, ru);
}
#endif

SYSCALL_DEFINE1(umask, int, mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}

static int prctl_set_mm_exe_file(struct mm_struct *mm, unsigned int fd)
{
	struct fd exe;
	struct inode *inode;
	int err;

	exe = fdget(fd);
	if (!exe.file)
		return -EBADF;

	inode = file_inode(exe.file);

	/*
	 * Because the original mm->exe_file points to executable file, make
	 * sure that this one is executable as well, to avoid breaking an
	 * overall picture.
	 */
	err = -EACCES;
	if (!S_ISREG(inode->i_mode) || path_noexec(&exe.file->f_path))
		goto exit;

	err = file_permission(exe.file, MAY_EXEC);
	if (err)
		goto exit;

	err = replace_mm_exe_file(mm, exe.file);
exit:
	fdput(exe);
	return err;
}

/*
 * Check arithmetic relations of passed addresses.
 *
 * WARNING: we don't require any capability here so be very careful
 * in what is allowed for modification from userspace.
 */
static int validate_prctl_map_addr(struct prctl_mm_map *prctl_map)
{
	unsigned long mmap_max_addr = TASK_SIZE;
	int error = -EINVAL, i;

	static const unsigned char offsets[] = {
		offsetof(struct prctl_mm_map, start_code),
		offsetof(struct prctl_mm_map, end_code),
		offsetof(struct prctl_mm_map, start_data),
		offsetof(struct prctl_mm_map, end_data),
		offsetof(struct prctl_mm_map, start_brk),
		offsetof(struct prctl_mm_map, brk),
		offsetof(struct prctl_mm_map, start_stack),
		offsetof(struct prctl_mm_map, arg_start),
		offsetof(struct prctl_mm_map, arg_end),
		offsetof(struct prctl_mm_map, env_start),
		offsetof(struct prctl_mm_map, env_end),
	};

	/*
	 * Make sure the members are not somewhere outside
	 * of allowed address space.
	 */
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		u64 val = *(u64 *)((char *)prctl_map + offsets[i]);

		if ((unsigned long)val >= mmap_max_addr ||
		    (unsigned long)val < mmap_min_addr)
			goto out;
	}

	/*
	 * Make sure the pairs are ordered.
	 */
#define __prctl_check_order(__m1, __op, __m2)				\
	((unsigned long)prctl_map->__m1 __op				\
	 (unsigned long)prctl_map->__m2) ? 0 : -EINVAL
	error  = __prctl_check_order(start_code, <, end_code);
	error |= __prctl_check_order(start_data,<=, end_data);
	error |= __prctl_check_order(start_brk, <=, brk);
	error |= __prctl_check_order(arg_start, <=, arg_end);
	error |= __prctl_check_order(env_start, <=, env_end);
	if (error)
		goto out;
#undef __prctl_check_order

	error = -EINVAL;

	/*
	 * Neither we should allow to override limits if they set.
	 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), prctl_map->brk,
			      prctl_map->start_brk, prctl_map->end_data,
			      prctl_map->start_data))
			goto out;

	error = 0;
out:
	return error;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
static int prctl_set_mm_map(int opt, const void __user *addr, unsigned long data_size)
{
	struct prctl_mm_map prctl_map = { .exe_fd = (u32)-1, };
	unsigned long user_auxv[AT_VECTOR_SIZE];
	struct mm_struct *mm = current->mm;
	int error;

	BUILD_BUG_ON(sizeof(user_auxv) != sizeof(mm->saved_auxv));
	BUILD_BUG_ON(sizeof(struct prctl_mm_map) > 256);

	if (opt == PR_SET_MM_MAP_SIZE)
		return put_user((unsigned int)sizeof(prctl_map),
				(unsigned int __user *)addr);

	if (data_size != sizeof(prctl_map))
		return -EINVAL;

	if (copy_from_user(&prctl_map, addr, sizeof(prctl_map)))
		return -EFAULT;

	error = validate_prctl_map_addr(&prctl_map);
	if (error)
		return error;

	if (prctl_map.auxv_size) {
		/*
		 * Someone is trying to cheat the auxv vector.
		 */
		if (!prctl_map.auxv ||
				prctl_map.auxv_size > sizeof(mm->saved_auxv))
			return -EINVAL;

		memset(user_auxv, 0, sizeof(user_auxv));
		if (copy_from_user(user_auxv,
				   (const void __user *)prctl_map.auxv,
				   prctl_map.auxv_size))
			return -EFAULT;

		/* Last entry must be AT_NULL as specification requires */
		user_auxv[AT_VECTOR_SIZE - 2] = AT_NULL;
		user_auxv[AT_VECTOR_SIZE - 1] = AT_NULL;
	}

	if (prctl_map.exe_fd != (u32)-1) {
		/*
		 * Check if the current user is checkpoint/restore capable.
		 * At the time of this writing, it checks for CAP_SYS_ADMIN
		 * or CAP_CHECKPOINT_RESTORE.
		 * Note that a user with access to ptrace can masquerade an
		 * arbitrary program as any executable, even setuid ones.
		 * This may have implications in the tomoyo subsystem.
		 */
		if (!checkpoint_restore_ns_capable(current_user_ns()))
			return -EPERM;

		error = prctl_set_mm_exe_file(mm, prctl_map.exe_fd);
		if (error)
			return error;
	}

	/*
	 * arg_lock protects concurrent updates but we still need mmap_lock for
	 * read to exclude races with sys_brk.
	 */
	mmap_read_lock(mm);

	/*
	 * We don't validate if these members are pointing to
	 * real present VMAs because application may have correspond
	 * VMAs already unmapped and kernel uses these members for statistics
	 * output in procfs mostly, except
	 *
	 *  - @start_brk/@brk which are used in do_brk_flags but kernel lookups
	 *    for VMAs when updating these members so anything wrong written
	 *    here cause kernel to swear at userspace program but won't lead
	 *    to any problem in kernel itself
	 */

	spin_lock(&mm->arg_lock);
	mm->start_code	= prctl_map.start_code;
	mm->end_code	= prctl_map.end_code;
	mm->start_data	= prctl_map.start_data;
	mm->end_data	= prctl_map.end_data;
	mm->start_brk	= prctl_map.start_brk;
	mm->brk		= prctl_map.brk;
	mm->start_stack	= prctl_map.start_stack;
	mm->arg_start	= prctl_map.arg_start;
	mm->arg_end	= prctl_map.arg_end;
	mm->env_start	= prctl_map.env_start;
	mm->env_end	= prctl_map.env_end;
	spin_unlock(&mm->arg_lock);

	/*
	 * Note this update of @saved_auxv is lockless thus
	 * if someone reads this member in procfs while we're
	 * updating -- it may get partly updated results. It's
	 * known and acceptable trade off: we leave it as is to
	 * not introduce additional locks here making the kernel
	 * more complex.
	 */
	if (prctl_map.auxv_size)
		memcpy(mm->saved_auxv, user_auxv, sizeof(user_auxv));

	mmap_read_unlock(mm);
	return 0;
}
#endif /* CONFIG_CHECKPOINT_RESTORE */

static int prctl_set_auxv(struct mm_struct *mm, unsigned long addr,
			  unsigned long len)
{
	/*
	 * This doesn't move the auxiliary vector itself since it's pinned to
	 * mm_struct, but it permits filling the vector with new values.  It's
	 * up to the caller to provide sane values here, otherwise userspace
	 * tools which use this vector might be unhappy.
	 */
	unsigned long user_auxv[AT_VECTOR_SIZE] = {};

	if (len > sizeof(user_auxv))
		return -EINVAL;

	if (copy_from_user(user_auxv, (const void __user *)addr, len))
		return -EFAULT;

	/* Make sure the last entry is always AT_NULL */
	user_auxv[AT_VECTOR_SIZE - 2] = 0;
	user_auxv[AT_VECTOR_SIZE - 1] = 0;

	BUILD_BUG_ON(sizeof(user_auxv) != sizeof(mm->saved_auxv));

	task_lock(current);
	memcpy(mm->saved_auxv, user_auxv, len);
	task_unlock(current);

	return 0;
}

static int prctl_set_mm(int opt, unsigned long addr,
			unsigned long arg4, unsigned long arg5)
{
	struct mm_struct *mm = current->mm;
	struct prctl_mm_map prctl_map = {
		.auxv = NULL,
		.auxv_size = 0,
		.exe_fd = -1,
	};
	struct vm_area_struct *vma;
	int error;

	if (arg5 || (arg4 && (opt != PR_SET_MM_AUXV &&
			      opt != PR_SET_MM_MAP &&
			      opt != PR_SET_MM_MAP_SIZE)))
		return -EINVAL;

#ifdef CONFIG_CHECKPOINT_RESTORE
	if (opt == PR_SET_MM_MAP || opt == PR_SET_MM_MAP_SIZE)
		return prctl_set_mm_map(opt, (const void __user *)addr, arg4);
#endif

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	if (opt == PR_SET_MM_EXE_FILE)
		return prctl_set_mm_exe_file(mm, (unsigned int)addr);

	if (opt == PR_SET_MM_AUXV)
		return prctl_set_auxv(mm, addr, arg4);

	if (addr >= TASK_SIZE || addr < mmap_min_addr)
		return -EINVAL;

	error = -EINVAL;

	/*
	 * arg_lock protects concurrent updates of arg boundaries, we need
	 * mmap_lock for a) concurrent sys_brk, b) finding VMA for addr
	 * validation.
	 */
	mmap_read_lock(mm);
	vma = find_vma(mm, addr);

	spin_lock(&mm->arg_lock);
	prctl_map.start_code	= mm->start_code;
	prctl_map.end_code	= mm->end_code;
	prctl_map.start_data	= mm->start_data;
	prctl_map.end_data	= mm->end_data;
	prctl_map.start_brk	= mm->start_brk;
	prctl_map.brk		= mm->brk;
	prctl_map.start_stack	= mm->start_stack;
	prctl_map.arg_start	= mm->arg_start;
	prctl_map.arg_end	= mm->arg_end;
	prctl_map.env_start	= mm->env_start;
	prctl_map.env_end	= mm->env_end;

	switch (opt) {
	case PR_SET_MM_START_CODE:
		prctl_map.start_code = addr;
		break;
	case PR_SET_MM_END_CODE:
		prctl_map.end_code = addr;
		break;
	case PR_SET_MM_START_DATA:
		prctl_map.start_data = addr;
		break;
	case PR_SET_MM_END_DATA:
		prctl_map.end_data = addr;
		break;
	case PR_SET_MM_START_STACK:
		prctl_map.start_stack = addr;
		break;
	case PR_SET_MM_START_BRK:
		prctl_map.start_brk = addr;
		break;
	case PR_SET_MM_BRK:
		prctl_map.brk = addr;
		break;
	case PR_SET_MM_ARG_START:
		prctl_map.arg_start = addr;
		break;
	case PR_SET_MM_ARG_END:
		prctl_map.arg_end = addr;
		break;
	case PR_SET_MM_ENV_START:
		prctl_map.env_start = addr;
		break;
	case PR_SET_MM_ENV_END:
		prctl_map.env_end = addr;
		break;
	default:
		goto out;
	}

	error = validate_prctl_map_addr(&prctl_map);
	if (error)
		goto out;

	switch (opt) {
	/*
	 * If command line arguments and environment
	 * are placed somewhere else on stack, we can
	 * set them up here, ARG_START/END to setup
	 * command line arguments and ENV_START/END
	 * for environment.
	 */
	case PR_SET_MM_START_STACK:
	case PR_SET_MM_ARG_START:
	case PR_SET_MM_ARG_END:
	case PR_SET_MM_ENV_START:
	case PR_SET_MM_ENV_END:
		if (!vma) {
			error = -EFAULT;
			goto out;
		}
	}

	mm->start_code	= prctl_map.start_code;
	mm->end_code	= prctl_map.end_code;
	mm->start_data	= prctl_map.start_data;
	mm->end_data	= prctl_map.end_data;
	mm->start_brk	= prctl_map.start_brk;
	mm->brk		= prctl_map.brk;
	mm->start_stack	= prctl_map.start_stack;
	mm->arg_start	= prctl_map.arg_start;
	mm->arg_end	= prctl_map.arg_end;
	mm->env_start	= prctl_map.env_start;
	mm->env_end	= prctl_map.env_end;

	error = 0;
out:
	spin_unlock(&mm->arg_lock);
	mmap_read_unlock(mm);
	return error;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
static int prctl_get_tid_address(struct task_struct *me, int __user * __user *tid_addr)
{
	return put_user(me->clear_child_tid, tid_addr);
}
#else
static int prctl_get_tid_address(struct task_struct *me, int __user * __user *tid_addr)
{
	return -EINVAL;
}
#endif

static int propagate_has_child_subreaper(struct task_struct *p, void *data)
{
	/*
	 * If task has has_child_subreaper - all its descendants
	 * already have these flag too and new descendants will
	 * inherit it on fork, skip them.
	 *
	 * If we've found child_reaper - skip descendants in
	 * it's subtree as they will never get out pidns.
	 */
	if (p->signal->has_child_subreaper ||
	    is_child_reaper(task_pid(p)))
		return 0;

	p->signal->has_child_subreaper = 1;
	return 1;
}

int __weak arch_prctl_spec_ctrl_get(struct task_struct *t, unsigned long which)
{
	return -EINVAL;
}

int __weak arch_prctl_spec_ctrl_set(struct task_struct *t, unsigned long which,
				    unsigned long ctrl)
{
	return -EINVAL;
}

#define PR_IO_FLUSHER (PF_MEMALLOC_NOIO | PF_LOCAL_THROTTLE)

#ifdef CONFIG_ANON_VMA_NAME

#define ANON_VMA_NAME_MAX_LEN		80
#define ANON_VMA_NAME_INVALID_CHARS	"\\`$[]"

static inline bool is_valid_name_char(char ch)
{
	/* printable ascii characters, excluding ANON_VMA_NAME_INVALID_CHARS */
	return ch > 0x1f && ch < 0x7f &&
		!strchr(ANON_VMA_NAME_INVALID_CHARS, ch);
}

static int prctl_set_vma(unsigned long opt, unsigned long addr,
			 unsigned long size, unsigned long arg)
{
	struct mm_struct *mm = current->mm;
	const char __user *uname;
	struct anon_vma_name *anon_name = NULL;
	int error;

	switch (opt) {
	case PR_SET_VMA_ANON_NAME:
		uname = (const char __user *)arg;
		if (uname) {
			char *name, *pch;

			name = strndup_user(uname, ANON_VMA_NAME_MAX_LEN);
			if (IS_ERR(name))
				return PTR_ERR(name);

			for (pch = name; *pch != '\0'; pch++) {
				if (!is_valid_name_char(*pch)) {
					kfree(name);
					return -EINVAL;
				}
			}
			/* anon_vma has its own copy */
			anon_name = anon_vma_name_alloc(name);
			kfree(name);
			if (!anon_name)
				return -ENOMEM;

		}

		mmap_write_lock(mm);
		error = madvise_set_anon_name(mm, addr, size, anon_name);
		mmap_write_unlock(mm);
		anon_vma_name_put(anon_name);
		break;
	default:
		error = -EINVAL;
	}

	return error;
}

#else /* CONFIG_ANON_VMA_NAME */
static int prctl_set_vma(unsigned long opt, unsigned long start,
			 unsigned long size, unsigned long arg)
{
	return -EINVAL;
}
#endif /* CONFIG_ANON_VMA_NAME */

static inline int prctl_set_mdwe(unsigned long bits, unsigned long arg3,
				 unsigned long arg4, unsigned long arg5)
{
	if (arg3 || arg4 || arg5)
		return -EINVAL;

	if (bits & ~(PR_MDWE_REFUSE_EXEC_GAIN))
		return -EINVAL;

	if (bits & PR_MDWE_REFUSE_EXEC_GAIN)
		set_bit(MMF_HAS_MDWE, &current->mm->flags);
	else if (test_bit(MMF_HAS_MDWE, &current->mm->flags))
		return -EPERM; /* Cannot unset the flag */

	return 0;
}

static inline int prctl_get_mdwe(unsigned long arg2, unsigned long arg3,
				 unsigned long arg4, unsigned long arg5)
{
	if (arg2 || arg3 || arg4 || arg5)
		return -EINVAL;

	return test_bit(MMF_HAS_MDWE, &current->mm->flags) ?
		PR_MDWE_REFUSE_EXEC_GAIN : 0;
}

static int prctl_get_auxv(void __user *addr, unsigned long len)
{
	struct mm_struct *mm = current->mm;
	unsigned long size = min_t(unsigned long, sizeof(mm->saved_auxv), len);

	if (size && copy_to_user(addr, mm->saved_auxv, size))
		return -EFAULT;
	return sizeof(mm->saved_auxv);
}

SYSCALL_DEFINE5(prctl, int, option, unsigned long, arg2, unsigned long, arg3,
		unsigned long, arg4, unsigned long, arg5)
{
	struct task_struct *me = current;
	unsigned char comm[sizeof(me->comm)];
	long error;

	error = security_task_prctl(option, arg2, arg3, arg4, arg5);
	if (error != -ENOSYS)
		return error;

	error = 0;
	switch (option) {
	case PR_SET_PDEATHSIG:
		if (!valid_signal(arg2)) {
			error = -EINVAL;
			break;
		}
		me->pdeath_signal = arg2;
		break;
	case PR_GET_PDEATHSIG:
		error = put_user(me->pdeath_signal, (int __user *)arg2);
		break;
	case PR_GET_DUMPABLE:
		error = get_dumpable(me->mm);
		break;
	case PR_SET_DUMPABLE:
		if (arg2 != SUID_DUMP_DISABLE && arg2 != SUID_DUMP_USER) {
			error = -EINVAL;
			break;
		}
		set_dumpable(me->mm, arg2);
		break;

	case PR_SET_UNALIGN:
		error = SET_UNALIGN_CTL(me, arg2);
		break;
	case PR_GET_UNALIGN:
		error = GET_UNALIGN_CTL(me, arg2);
		break;
	case PR_SET_FPEMU:
		error = SET_FPEMU_CTL(me, arg2);
		break;
	case PR_GET_FPEMU:
		error = GET_FPEMU_CTL(me, arg2);
		break;
	case PR_SET_FPEXC:
		error = SET_FPEXC_CTL(me, arg2);
		break;
	case PR_GET_FPEXC:
		error = GET_FPEXC_CTL(me, arg2);
		break;
	case PR_GET_TIMING:
		error = PR_TIMING_STATISTICAL;
		break;
	case PR_SET_TIMING:
		if (arg2 != PR_TIMING_STATISTICAL)
			error = -EINVAL;
		break;
	case PR_SET_NAME:
		comm[sizeof(me->comm) - 1] = 0;
		if (strncpy_from_user(comm, (char __user *)arg2,
				      sizeof(me->comm) - 1) < 0)
			return -EFAULT;
		set_task_comm(me, comm);
		proc_comm_connector(me);
		break;
	case PR_GET_NAME:
		get_task_comm(comm, me);
		if (copy_to_user((char __user *)arg2, comm, sizeof(comm)))
			return -EFAULT;
		break;
	case PR_GET_ENDIAN:
		error = GET_ENDIAN(me, arg2);
		break;
	case PR_SET_ENDIAN:
		error = SET_ENDIAN(me, arg2);
		break;
	case PR_GET_SECCOMP:
		error = prctl_get_seccomp();
		break;
	case PR_SET_SECCOMP:
		error = prctl_set_seccomp(arg2, (char __user *)arg3);
		break;
	case PR_GET_TSC:
		error = GET_TSC_CTL(arg2);
		break;
	case PR_SET_TSC:
		error = SET_TSC_CTL(arg2);
		break;
	case PR_TASK_PERF_EVENTS_DISABLE:
		error = perf_event_task_disable();
		break;
	case PR_TASK_PERF_EVENTS_ENABLE:
		error = perf_event_task_enable();
		break;
	case PR_GET_TIMERSLACK:
		if (current->timer_slack_ns > ULONG_MAX)
			error = ULONG_MAX;
		else
			error = current->timer_slack_ns;
		break;
	case PR_SET_TIMERSLACK:
		if (arg2 <= 0)
			current->timer_slack_ns =
					current->default_timer_slack_ns;
		else
			current->timer_slack_ns = arg2;
		break;
	case PR_MCE_KILL:
		if (arg4 | arg5)
			return -EINVAL;
		switch (arg2) {
		case PR_MCE_KILL_CLEAR:
			if (arg3 != 0)
				return -EINVAL;
			current->flags &= ~PF_MCE_PROCESS;
			break;
		case PR_MCE_KILL_SET:
			current->flags |= PF_MCE_PROCESS;
			if (arg3 == PR_MCE_KILL_EARLY)
				current->flags |= PF_MCE_EARLY;
			else if (arg3 == PR_MCE_KILL_LATE)
				current->flags &= ~PF_MCE_EARLY;
			else if (arg3 == PR_MCE_KILL_DEFAULT)
				current->flags &=
						~(PF_MCE_EARLY|PF_MCE_PROCESS);
			else
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PR_MCE_KILL_GET:
		if (arg2 | arg3 | arg4 | arg5)
			return -EINVAL;
		if (current->flags & PF_MCE_PROCESS)
			error = (current->flags & PF_MCE_EARLY) ?
				PR_MCE_KILL_EARLY : PR_MCE_KILL_LATE;
		else
			error = PR_MCE_KILL_DEFAULT;
		break;
	case PR_SET_MM:
		error = prctl_set_mm(arg2, arg3, arg4, arg5);
		break;
	case PR_GET_TID_ADDRESS:
		error = prctl_get_tid_address(me, (int __user * __user *)arg2);
		break;
	case PR_SET_CHILD_SUBREAPER:
		me->signal->is_child_subreaper = !!arg2;
		if (!arg2)
			break;

		walk_process_tree(me, propagate_has_child_subreaper, NULL);
		break;
	case PR_GET_CHILD_SUBREAPER:
		error = put_user(me->signal->is_child_subreaper,
				 (int __user *)arg2);
		break;
	case PR_SET_NO_NEW_PRIVS:
		if (arg2 != 1 || arg3 || arg4 || arg5)
			return -EINVAL;

		task_set_no_new_privs(current);
		break;
	case PR_GET_NO_NEW_PRIVS:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		return task_no_new_privs(current) ? 1 : 0;
	case PR_GET_THP_DISABLE:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = !!test_bit(MMF_DISABLE_THP, &me->mm->flags);
		break;
	case PR_SET_THP_DISABLE:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		if (mmap_write_lock_killable(me->mm))
			return -EINTR;
		if (arg2)
			set_bit(MMF_DISABLE_THP, &me->mm->flags);
		else
			clear_bit(MMF_DISABLE_THP, &me->mm->flags);
		mmap_write_unlock(me->mm);
		break;
	case PR_MPX_ENABLE_MANAGEMENT:
	case PR_MPX_DISABLE_MANAGEMENT:
		/* No longer implemented: */
		return -EINVAL;
	case PR_SET_FP_MODE:
		error = SET_FP_MODE(me, arg2);
		break;
	case PR_GET_FP_MODE:
		error = GET_FP_MODE(me);
		break;
	case PR_SVE_SET_VL:
		error = SVE_SET_VL(arg2);
		break;
	case PR_SVE_GET_VL:
		error = SVE_GET_VL();
		break;
	case PR_SME_SET_VL:
		error = SME_SET_VL(arg2);
		break;
	case PR_SME_GET_VL:
		error = SME_GET_VL();
		break;
	case PR_GET_SPECULATION_CTRL:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		error = arch_prctl_spec_ctrl_get(me, arg2);
		break;
	case PR_SET_SPECULATION_CTRL:
		if (arg4 || arg5)
			return -EINVAL;
		error = arch_prctl_spec_ctrl_set(me, arg2, arg3);
		break;
	case PR_PAC_RESET_KEYS:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		error = PAC_RESET_KEYS(me, arg2);
		break;
	case PR_PAC_SET_ENABLED_KEYS:
		if (arg4 || arg5)
			return -EINVAL;
		error = PAC_SET_ENABLED_KEYS(me, arg2, arg3);
		break;
	case PR_PAC_GET_ENABLED_KEYS:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = PAC_GET_ENABLED_KEYS(me);
		break;
	case PR_SET_TAGGED_ADDR_CTRL:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		error = SET_TAGGED_ADDR_CTRL(arg2);
		break;
	case PR_GET_TAGGED_ADDR_CTRL:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = GET_TAGGED_ADDR_CTRL();
		break;
	case PR_SET_IO_FLUSHER:
		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (arg3 || arg4 || arg5)
			return -EINVAL;

		if (arg2 == 1)
			current->flags |= PR_IO_FLUSHER;
		else if (!arg2)
			current->flags &= ~PR_IO_FLUSHER;
		else
			return -EINVAL;
		break;
	case PR_GET_IO_FLUSHER:
		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;

		error = (current->flags & PR_IO_FLUSHER) == PR_IO_FLUSHER;
		break;
	case PR_SET_SYSCALL_USER_DISPATCH:
		error = set_syscall_user_dispatch(arg2, arg3, arg4,
						  (char __user *) arg5);
		break;
#ifdef CONFIG_SCHED_CORE
	case PR_SCHED_CORE:
		error = sched_core_share_pid(arg2, arg3, arg4, arg5);
		break;
#endif
	case PR_SET_MDWE:
		error = prctl_set_mdwe(arg2, arg3, arg4, arg5);
		break;
	case PR_GET_MDWE:
		error = prctl_get_mdwe(arg2, arg3, arg4, arg5);
		break;
	case PR_SET_VMA:
		error = prctl_set_vma(arg2, arg3, arg4, arg5);
		break;
	case PR_GET_AUXV:
		if (arg4 || arg5)
			return -EINVAL;
		error = prctl_get_auxv((void __user *)arg2, arg3);
		break;
#ifdef CONFIG_KSM
	case PR_SET_MEMORY_MERGE:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		if (mmap_write_lock_killable(me->mm))
			return -EINTR;

		if (arg2)
			error = ksm_enable_merge_any(me->mm);
		else
			error = ksm_disable_merge_any(me->mm);
		mmap_write_unlock(me->mm);
		break;
	case PR_GET_MEMORY_MERGE:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;

		error = !!test_bit(MMF_VM_MERGE_ANY, &me->mm->flags);
		break;
#endif
	case PR_RISCV_V_SET_CONTROL:
		error = RISCV_V_SET_CONTROL(arg2);
		break;
	case PR_RISCV_V_GET_CONTROL:
		error = RISCV_V_GET_CONTROL();
		break;
	default:
		error = -EINVAL;
		break;
	}
	return error;
}

SYSCALL_DEFINE3(getcpu, unsigned __user *, cpup, unsigned __user *, nodep,
		struct getcpu_cache __user *, unused)
{
	int err = 0;
	int cpu = raw_smp_processor_id();

	if (cpup)
		err |= put_user(cpu, cpup);
	if (nodep)
		err |= put_user(cpu_to_node(cpu), nodep);
	return err ? -EFAULT : 0;
}

/**
 * do_sysinfo - fill in sysinfo struct
 * @info: pointer to buffer to fill
 */
static int do_sysinfo(struct sysinfo *info)
{
	unsigned long mem_total, sav_total;
	unsigned int mem_unit, bitcount;
	struct timespec64 tp;

	memset(info, 0, sizeof(struct sysinfo));

	ktime_get_boottime_ts64(&tp);
	timens_add_boottime(&tp);
	info->uptime = tp.tv_sec + (tp.tv_nsec ? 1 : 0);

	get_avenrun(info->loads, 0, SI_LOAD_SHIFT - FSHIFT);

	info->procs = nr_threads;

	si_meminfo(info);
	si_swapinfo(info);

	/*
	 * If the sum of all the available memory (i.e. ram + swap)
	 * is less than can be stored in a 32 bit unsigned long then
	 * we can be binary compatible with 2.2.x kernels.  If not,
	 * well, in that case 2.2.x was broken anyways...
	 *
	 *  -Erik Andersen <andersee@debian.org>
	 */

	mem_total = info->totalram + info->totalswap;
	if (mem_total < info->totalram || mem_total < info->totalswap)
		goto out;
	bitcount = 0;
	mem_unit = info->mem_unit;
	while (mem_unit > 1) {
		bitcount++;
		mem_unit >>= 1;
		sav_total = mem_total;
		mem_total <<= 1;
		if (mem_total < sav_total)
			goto out;
	}

	/*
	 * If mem_total did not overflow, multiply all memory values by
	 * info->mem_unit and set it to 1.  This leaves things compatible
	 * with 2.2.x, and also retains compatibility with earlier 2.4.x
	 * kernels...
	 */

	info->mem_unit = 1;
	info->totalram <<= bitcount;
	info->freeram <<= bitcount;
	info->sharedram <<= bitcount;
	info->bufferram <<= bitcount;
	info->totalswap <<= bitcount;
	info->freeswap <<= bitcount;
	info->totalhigh <<= bitcount;
	info->freehigh <<= bitcount;

out:
	return 0;
}

SYSCALL_DEFINE1(sysinfo, struct sysinfo __user *, info)
{
	struct sysinfo val;

	do_sysinfo(&val);

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_COMPAT
struct compat_sysinfo {
	s32 uptime;
	u32 loads[3];
	u32 totalram;
	u32 freeram;
	u32 sharedram;
	u32 bufferram;
	u32 totalswap;
	u32 freeswap;
	u16 procs;
	u16 pad;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[20-2*sizeof(u32)-sizeof(int)];
};

COMPAT_SYSCALL_DEFINE1(sysinfo, struct compat_sysinfo __user *, info)
{
	struct sysinfo s;
	struct compat_sysinfo s_32;

	do_sysinfo(&s);

	/* Check to see if any memory value is too large for 32-bit and scale
	 *  down if needed
	 */
	if (upper_32_bits(s.totalram) || upper_32_bits(s.totalswap)) {
		int bitcount = 0;

		while (s.mem_unit < PAGE_SIZE) {
			s.mem_unit <<= 1;
			bitcount++;
		}

		s.totalram >>= bitcount;
		s.freeram >>= bitcount;
		s.sharedram >>= bitcount;
		s.bufferram >>= bitcount;
		s.totalswap >>= bitcount;
		s.freeswap >>= bitcount;
		s.totalhigh >>= bitcount;
		s.freehigh >>= bitcount;
	}

	memset(&s_32, 0, sizeof(s_32));
	s_32.uptime = s.uptime;
	s_32.loads[0] = s.loads[0];
	s_32.loads[1] = s.loads[1];
	s_32.loads[2] = s.loads[2];
	s_32.totalram = s.totalram;
	s_32.freeram = s.freeram;
	s_32.sharedram = s.sharedram;
	s_32.bufferram = s.bufferram;
	s_32.totalswap = s.totalswap;
	s_32.freeswap = s.freeswap;
	s_32.procs = s.procs;
	s_32.totalhigh = s.totalhigh;
	s_32.freehigh = s.freehigh;
	s_32.mem_unit = s.mem_unit;
	if (copy_to_user(info, &s_32, sizeof(s_32)))
		return -EFAULT;
	return 0;
}
#endif /* CONFIG_COMPAT */
