/* Common capabilities, needed by capability.o.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/capability.h>
#include <linux/audit.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/lsm_hooks.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/ptrace.h>
#include <linux/xattr.h>
#include <linux/hugetlb.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/prctl.h>
#include <linux/securebits.h>
#include <linux/user_namespace.h>
#include <linux/binfmts.h>
#include <linux/personality.h>

/*
 * If a non-root user executes a setuid-root binary in
 * !secure(SECURE_NOROOT) mode, then we raise capabilities.
 * However if fE is also set, then the intent is for only
 * the file capabilities to be applied, and the setuid-root
 * bit is left on either to change the uid (plausible) or
 * to get full privilege on a kernel without file capabilities
 * support.  So in that case we do not raise capabilities.
 *
 * Warn if that happens, once per boot.
 */
static void warn_setuid_and_fcaps_mixed(const char *fname)
{
	static int warned;
	if (!warned) {
		printk(KERN_INFO "warning: `%s' has both setuid-root and"
			" effective capabilities. Therefore not raising all"
			" capabilities.\n", fname);
		warned = 1;
	}
}

/**
 * cap_capable - Determine whether a task has a particular effective capability
 * @cred: The credentials to use
 * @ns:  The user namespace in which we need the capability
 * @cap: The capability to check for
 * @audit: Whether to write an audit message or not
 *
 * Determine whether the nominated task has the specified capability amongst
 * its effective set, returning 0 if it does, -ve if it does not.
 *
 * NOTE WELL: cap_has_capability() cannot be used like the kernel's capable()
 * and has_capability() functions.  That is, it has the reverse semantics:
 * cap_has_capability() returns 0 when a task has a capability, but the
 * kernel's capable() and has_capability() returns 1 for this case.
 */
int cap_capable(const struct cred *cred, struct user_namespace *targ_ns,
		int cap, int audit)
{
	struct user_namespace *ns = targ_ns;

	/* See if cred has the capability in the target user namespace
	 * by examining the target user namespace and all of the target
	 * user namespace's parents.
	 */
	for (;;) {
		/* Do we have the necessary capabilities? */
		if (ns == cred->user_ns)
			return cap_raised(cred->cap_effective, cap) ? 0 : -EPERM;

		/*
		 * If we're already at a lower level than we're looking for,
		 * we're done searching.
		 */
		if (ns->level <= cred->user_ns->level)
			return -EPERM;

		/* 
		 * The owner of the user namespace in the parent of the
		 * user namespace has all caps.
		 */
		if ((ns->parent == cred->user_ns) && uid_eq(ns->owner, cred->euid))
			return 0;

		/*
		 * If you have a capability in a parent user ns, then you have
		 * it over all children user namespaces as well.
		 */
		ns = ns->parent;
	}

	/* We never get here */
}

/**
 * cap_settime - Determine whether the current process may set the system clock
 * @ts: The time to set
 * @tz: The timezone to set
 *
 * Determine whether the current process may set the system clock and timezone
 * information, returning 0 if permission granted, -ve if denied.
 */
int cap_settime(const struct timespec64 *ts, const struct timezone *tz)
{
	if (!capable(CAP_SYS_TIME))
		return -EPERM;
	return 0;
}

/**
 * cap_ptrace_access_check - Determine whether the current process may access
 *			   another
 * @child: The process to be accessed
 * @mode: The mode of attachment.
 *
 * If we are in the same or an ancestor user_ns and have all the target
 * task's capabilities, then ptrace access is allowed.
 * If we have the ptrace capability to the target user_ns, then ptrace
 * access is allowed.
 * Else denied.
 *
 * Determine whether a process may access another, returning 0 if permission
 * granted, -ve if denied.
 */
int cap_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	int ret = 0;
	const struct cred *cred, *child_cred;
	const kernel_cap_t *caller_caps;

	rcu_read_lock();
	cred = current_cred();
	child_cred = __task_cred(child);
	if (mode & PTRACE_MODE_FSCREDS)
		caller_caps = &cred->cap_effective;
	else
		caller_caps = &cred->cap_permitted;
	if (cred->user_ns == child_cred->user_ns &&
	    cap_issubset(child_cred->cap_permitted, *caller_caps))
		goto out;
	if (ns_capable(child_cred->user_ns, CAP_SYS_PTRACE))
		goto out;
	ret = -EPERM;
out:
	rcu_read_unlock();
	return ret;
}

/**
 * cap_ptrace_traceme - Determine whether another process may trace the current
 * @parent: The task proposed to be the tracer
 *
 * If parent is in the same or an ancestor user_ns and has all current's
 * capabilities, then ptrace access is allowed.
 * If parent has the ptrace capability to current's user_ns, then ptrace
 * access is allowed.
 * Else denied.
 *
 * Determine whether the nominated task is permitted to trace the current
 * process, returning 0 if permission is granted, -ve if denied.
 */
int cap_ptrace_traceme(struct task_struct *parent)
{
	int ret = 0;
	const struct cred *cred, *child_cred;

	rcu_read_lock();
	cred = __task_cred(parent);
	child_cred = current_cred();
	if (cred->user_ns == child_cred->user_ns &&
	    cap_issubset(child_cred->cap_permitted, cred->cap_permitted))
		goto out;
	if (has_ns_capability(parent, child_cred->user_ns, CAP_SYS_PTRACE))
		goto out;
	ret = -EPERM;
out:
	rcu_read_unlock();
	return ret;
}

/**
 * cap_capget - Retrieve a task's capability sets
 * @target: The task from which to retrieve the capability sets
 * @effective: The place to record the effective set
 * @inheritable: The place to record the inheritable set
 * @permitted: The place to record the permitted set
 *
 * This function retrieves the capabilities of the nominated task and returns
 * them to the caller.
 */
int cap_capget(struct task_struct *target, kernel_cap_t *effective,
	       kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	const struct cred *cred;

	/* Derived from kernel/capability.c:sys_capget. */
	rcu_read_lock();
	cred = __task_cred(target);
	*effective   = cred->cap_effective;
	*inheritable = cred->cap_inheritable;
	*permitted   = cred->cap_permitted;
	rcu_read_unlock();
	return 0;
}

/*
 * Determine whether the inheritable capabilities are limited to the old
 * permitted set.  Returns 1 if they are limited, 0 if they are not.
 */
static inline int cap_inh_is_capped(void)
{

	/* they are so limited unless the current task has the CAP_SETPCAP
	 * capability
	 */
	if (cap_capable(current_cred(), current_cred()->user_ns,
			CAP_SETPCAP, SECURITY_CAP_AUDIT) == 0)
		return 0;
	return 1;
}

/**
 * cap_capset - Validate and apply proposed changes to current's capabilities
 * @new: The proposed new credentials; alterations should be made here
 * @old: The current task's current credentials
 * @effective: A pointer to the proposed new effective capabilities set
 * @inheritable: A pointer to the proposed new inheritable capabilities set
 * @permitted: A pointer to the proposed new permitted capabilities set
 *
 * This function validates and applies a proposed mass change to the current
 * process's capability sets.  The changes are made to the proposed new
 * credentials, and assuming no error, will be committed by the caller of LSM.
 */
int cap_capset(struct cred *new,
	       const struct cred *old,
	       const kernel_cap_t *effective,
	       const kernel_cap_t *inheritable,
	       const kernel_cap_t *permitted)
{
	if (cap_inh_is_capped() &&
	    !cap_issubset(*inheritable,
			  cap_combine(old->cap_inheritable,
				      old->cap_permitted)))
		/* incapable of using this inheritable set */
		return -EPERM;

	if (!cap_issubset(*inheritable,
			  cap_combine(old->cap_inheritable,
				      old->cap_bset)))
		/* no new pI capabilities outside bounding set */
		return -EPERM;

	/* verify restrictions on target's new Permitted set */
	if (!cap_issubset(*permitted, old->cap_permitted))
		return -EPERM;

	/* verify the _new_Effective_ is a subset of the _new_Permitted_ */
	if (!cap_issubset(*effective, *permitted))
		return -EPERM;

	new->cap_effective   = *effective;
	new->cap_inheritable = *inheritable;
	new->cap_permitted   = *permitted;

	/*
	 * Mask off ambient bits that are no longer both permitted and
	 * inheritable.
	 */
	new->cap_ambient = cap_intersect(new->cap_ambient,
					 cap_intersect(*permitted,
						       *inheritable));
	if (WARN_ON(!cap_ambient_invariant_ok(new)))
		return -EINVAL;
	return 0;
}

/*
 * Clear proposed capability sets for execve().
 */
static inline void bprm_clear_caps(struct linux_binprm *bprm)
{
	cap_clear(bprm->cred->cap_permitted);
	bprm->cap_effective = false;
}

/**
 * cap_inode_need_killpriv - Determine if inode change affects privileges
 * @dentry: The inode/dentry in being changed with change marked ATTR_KILL_PRIV
 *
 * Determine if an inode having a change applied that's marked ATTR_KILL_PRIV
 * affects the security markings on that inode, and if it is, should
 * inode_killpriv() be invoked or the change rejected?
 *
 * Returns 0 if granted; +ve if granted, but inode_killpriv() is required; and
 * -ve to deny the change.
 */
int cap_inode_need_killpriv(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	int error;

	error = __vfs_getxattr(dentry, inode, XATTR_NAME_CAPS, NULL, 0);
	return error > 0;
}

/**
 * cap_inode_killpriv - Erase the security markings on an inode
 * @dentry: The inode/dentry to alter
 *
 * Erase the privilege-enhancing security markings on an inode.
 *
 * Returns 0 if successful, -ve on error.
 */
int cap_inode_killpriv(struct dentry *dentry)
{
	int error;

	error = __vfs_removexattr(dentry, XATTR_NAME_CAPS);
	if (error == -EOPNOTSUPP)
		error = 0;
	return error;
}

/*
 * Calculate the new process capability sets from the capability sets attached
 * to a file.
 */
static inline int bprm_caps_from_vfs_caps(struct cpu_vfs_cap_data *caps,
					  struct linux_binprm *bprm,
					  bool *effective,
					  bool *has_cap)
{
	struct cred *new = bprm->cred;
	unsigned i;
	int ret = 0;

	if (caps->magic_etc & VFS_CAP_FLAGS_EFFECTIVE)
		*effective = true;

	if (caps->magic_etc & VFS_CAP_REVISION_MASK)
		*has_cap = true;

	CAP_FOR_EACH_U32(i) {
		__u32 permitted = caps->permitted.cap[i];
		__u32 inheritable = caps->inheritable.cap[i];

		/*
		 * pP' = (X & fP) | (pI & fI)
		 * The addition of pA' is handled later.
		 */
		new->cap_permitted.cap[i] =
			(new->cap_bset.cap[i] & permitted) |
			(new->cap_inheritable.cap[i] & inheritable);

		if (permitted & ~new->cap_permitted.cap[i])
			/* insufficient to execute correctly */
			ret = -EPERM;
	}

	/*
	 * For legacy apps, with no internal support for recognizing they
	 * do not have enough capabilities, we return an error if they are
	 * missing some "forced" (aka file-permitted) capabilities.
	 */
	return *effective ? ret : 0;
}

/*
 * Extract the on-exec-apply capability sets for an executable file.
 */
int get_vfs_caps_from_disk(const struct dentry *dentry, struct cpu_vfs_cap_data *cpu_caps)
{
	struct inode *inode = d_backing_inode(dentry);
	__u32 magic_etc;
	unsigned tocopy, i;
	int size;
	struct vfs_cap_data caps;

	memset(cpu_caps, 0, sizeof(struct cpu_vfs_cap_data));

	if (!inode)
		return -ENODATA;

	size = __vfs_getxattr((struct dentry *)dentry, inode,
			      XATTR_NAME_CAPS, &caps, XATTR_CAPS_SZ);
	if (size == -ENODATA || size == -EOPNOTSUPP)
		/* no data, that's ok */
		return -ENODATA;
	if (size < 0)
		return size;

	if (size < sizeof(magic_etc))
		return -EINVAL;

	cpu_caps->magic_etc = magic_etc = le32_to_cpu(caps.magic_etc);

	switch (magic_etc & VFS_CAP_REVISION_MASK) {
	case VFS_CAP_REVISION_1:
		if (size != XATTR_CAPS_SZ_1)
			return -EINVAL;
		tocopy = VFS_CAP_U32_1;
		break;
	case VFS_CAP_REVISION_2:
		if (size != XATTR_CAPS_SZ_2)
			return -EINVAL;
		tocopy = VFS_CAP_U32_2;
		break;
	default:
		return -EINVAL;
	}

	CAP_FOR_EACH_U32(i) {
		if (i >= tocopy)
			break;
		cpu_caps->permitted.cap[i] = le32_to_cpu(caps.data[i].permitted);
		cpu_caps->inheritable.cap[i] = le32_to_cpu(caps.data[i].inheritable);
	}

	cpu_caps->permitted.cap[CAP_LAST_U32] &= CAP_LAST_U32_VALID_MASK;
	cpu_caps->inheritable.cap[CAP_LAST_U32] &= CAP_LAST_U32_VALID_MASK;

	return 0;
}

/*
 * Attempt to get the on-exec apply capability sets for an executable file from
 * its xattrs and, if present, apply them to the proposed credentials being
 * constructed by execve().
 */
static int get_file_caps(struct linux_binprm *bprm, bool *effective, bool *has_cap)
{
	int rc = 0;
	struct cpu_vfs_cap_data vcaps;

	bprm_clear_caps(bprm);

	if (!file_caps_enabled)
		return 0;

	if (!mnt_may_suid(bprm->file->f_path.mnt))
		return 0;

	/*
	 * This check is redundant with mnt_may_suid() but is kept to make
	 * explicit that capability bits are limited to s_user_ns and its
	 * descendants.
	 */
	if (!current_in_userns(bprm->file->f_path.mnt->mnt_sb->s_user_ns))
		return 0;

	rc = get_vfs_caps_from_disk(bprm->file->f_path.dentry, &vcaps);
	if (rc < 0) {
		if (rc == -EINVAL)
			printk(KERN_NOTICE "%s: get_vfs_caps_from_disk returned %d for %s\n",
				__func__, rc, bprm->filename);
		else if (rc == -ENODATA)
			rc = 0;
		goto out;
	}

	rc = bprm_caps_from_vfs_caps(&vcaps, bprm, effective, has_cap);
	if (rc == -EINVAL)
		printk(KERN_NOTICE "%s: cap_from_disk returned %d for %s\n",
		       __func__, rc, bprm->filename);

out:
	if (rc)
		bprm_clear_caps(bprm);

	return rc;
}

/**
 * cap_bprm_set_creds - Set up the proposed credentials for execve().
 * @bprm: The execution parameters, including the proposed creds
 *
 * Set up the proposed credentials for a new execution context being
 * constructed by execve().  The proposed creds in @bprm->cred is altered,
 * which won't take effect immediately.  Returns 0 if successful, -ve on error.
 */
int cap_bprm_set_creds(struct linux_binprm *bprm)
{
	const struct cred *old = current_cred();
	struct cred *new = bprm->cred;
	bool effective, has_cap = false, is_setid;
	int ret;
	kuid_t root_uid;

	if (WARN_ON(!cap_ambient_invariant_ok(old)))
		return -EPERM;

	effective = false;
	ret = get_file_caps(bprm, &effective, &has_cap);
	if (ret < 0)
		return ret;

	root_uid = make_kuid(new->user_ns, 0);

	if (!issecure(SECURE_NOROOT)) {
		/*
		 * If the legacy file capability is set, then don't set privs
		 * for a setuid root binary run by a non-root user.  Do set it
		 * for a root user just to cause least surprise to an admin.
		 */
		if (has_cap && !uid_eq(new->uid, root_uid) && uid_eq(new->euid, root_uid)) {
			warn_setuid_and_fcaps_mixed(bprm->filename);
			goto skip;
		}
		/*
		 * To support inheritance of root-permissions and suid-root
		 * executables under compatibility mode, we override the
		 * capability sets for the file.
		 *
		 * If only the real uid is 0, we do not set the effective bit.
		 */
		if (uid_eq(new->euid, root_uid) || uid_eq(new->uid, root_uid)) {
			/* pP' = (cap_bset & ~0) | (pI & ~0) */
			new->cap_permitted = cap_combine(old->cap_bset,
							 old->cap_inheritable);
		}
		if (uid_eq(new->euid, root_uid))
			effective = true;
	}
skip:

	/* if we have fs caps, clear dangerous personality flags */
	if (!cap_issubset(new->cap_permitted, old->cap_permitted))
		bprm->per_clear |= PER_CLEAR_ON_SETID;


	/* Don't let someone trace a set[ug]id/setpcap binary with the revised
	 * credentials unless they have the appropriate permit.
	 *
	 * In addition, if NO_NEW_PRIVS, then ensure we get no new privs.
	 */
	is_setid = !uid_eq(new->euid, old->uid) || !gid_eq(new->egid, old->gid);

	if ((is_setid ||
	     !cap_issubset(new->cap_permitted, old->cap_permitted)) &&
	    ((bprm->unsafe & ~LSM_UNSAFE_PTRACE) ||
	     !ptracer_capable(current, new->user_ns))) {
		/* downgrade; they get no more than they had, and maybe less */
		if (!ns_capable(new->user_ns, CAP_SETUID) ||
		    (bprm->unsafe & LSM_UNSAFE_NO_NEW_PRIVS)) {
			new->euid = new->uid;
			new->egid = new->gid;
		}
		new->cap_permitted = cap_intersect(new->cap_permitted,
						   old->cap_permitted);
	}

	new->suid = new->fsuid = new->euid;
	new->sgid = new->fsgid = new->egid;

	/* File caps or setid cancels ambient. */
	if (has_cap || is_setid)
		cap_clear(new->cap_ambient);

	/*
	 * Now that we've computed pA', update pP' to give:
	 *   pP' = (X & fP) | (pI & fI) | pA'
	 */
	new->cap_permitted = cap_combine(new->cap_permitted, new->cap_ambient);

	/*
	 * Set pE' = (fE ? pP' : pA').  Because pA' is zero if fE is set,
	 * this is the same as pE' = (fE ? pP' : 0) | pA'.
	 */
	if (effective)
		new->cap_effective = new->cap_permitted;
	else
		new->cap_effective = new->cap_ambient;

	if (WARN_ON(!cap_ambient_invariant_ok(new)))
		return -EPERM;

	bprm->cap_effective = effective;

	/*
	 * Audit candidate if current->cap_effective is set
	 *
	 * We do not bother to audit if 3 things are true:
	 *   1) cap_effective has all caps
	 *   2) we are root
	 *   3) root is supposed to have all caps (SECURE_NOROOT)
	 * Since this is just a normal root execing a process.
	 *
	 * Number 1 above might fail if you don't have a full bset, but I think
	 * that is interesting information to audit.
	 */
	if (!cap_issubset(new->cap_effective, new->cap_ambient)) {
		if (!cap_issubset(CAP_FULL_SET, new->cap_effective) ||
		    !uid_eq(new->euid, root_uid) || !uid_eq(new->uid, root_uid) ||
		    issecure(SECURE_NOROOT)) {
			ret = audit_log_bprm_fcaps(bprm, new, old);
			if (ret < 0)
				return ret;
		}
	}

	new->securebits &= ~issecure_mask(SECURE_KEEP_CAPS);

	if (WARN_ON(!cap_ambient_invariant_ok(new)))
		return -EPERM;

	return 0;
}

/**
 * cap_bprm_secureexec - Determine whether a secure execution is required
 * @bprm: The execution parameters
 *
 * Determine whether a secure execution is required, return 1 if it is, and 0
 * if it is not.
 *
 * The credentials have been committed by this point, and so are no longer
 * available through @bprm->cred.
 */
int cap_bprm_secureexec(struct linux_binprm *bprm)
{
	const struct cred *cred = current_cred();
	kuid_t root_uid = make_kuid(cred->user_ns, 0);

	if (!uid_eq(cred->uid, root_uid)) {
		if (bprm->cap_effective)
			return 1;
		if (!cap_issubset(cred->cap_permitted, cred->cap_ambient))
			return 1;
	}

	return (!uid_eq(cred->euid, cred->uid) ||
		!gid_eq(cred->egid, cred->gid));
}

/**
 * cap_inode_setxattr - Determine whether an xattr may be altered
 * @dentry: The inode/dentry being altered
 * @name: The name of the xattr to be changed
 * @value: The value that the xattr will be changed to
 * @size: The size of value
 * @flags: The replacement flag
 *
 * Determine whether an xattr may be altered or set on an inode, returning 0 if
 * permission is granted, -ve if denied.
 *
 * This is used to make sure security xattrs don't get updated or set by those
 * who aren't privileged to do so.
 */
int cap_inode_setxattr(struct dentry *dentry, const char *name,
		       const void *value, size_t size, int flags)
{
	if (!strcmp(name, XATTR_NAME_CAPS)) {
		if (!capable(CAP_SETFCAP))
			return -EPERM;
		return 0;
	}

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/**
 * cap_inode_removexattr - Determine whether an xattr may be removed
 * @dentry: The inode/dentry being altered
 * @name: The name of the xattr to be changed
 *
 * Determine whether an xattr may be removed from an inode, returning 0 if
 * permission is granted, -ve if denied.
 *
 * This is used to make sure security xattrs don't get removed by those who
 * aren't privileged to remove them.
 */
int cap_inode_removexattr(struct dentry *dentry, const char *name)
{
	if (!strcmp(name, XATTR_NAME_CAPS)) {
		if (!capable(CAP_SETFCAP))
			return -EPERM;
		return 0;
	}

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/*
 * cap_emulate_setxuid() fixes the effective / permitted capabilities of
 * a process after a call to setuid, setreuid, or setresuid.
 *
 *  1) When set*uiding _from_ one of {r,e,s}uid == 0 _to_ all of
 *  {r,e,s}uid != 0, the permitted and effective capabilities are
 *  cleared.
 *
 *  2) When set*uiding _from_ euid == 0 _to_ euid != 0, the effective
 *  capabilities of the process are cleared.
 *
 *  3) When set*uiding _from_ euid != 0 _to_ euid == 0, the effective
 *  capabilities are set to the permitted capabilities.
 *
 *  fsuid is handled elsewhere. fsuid == 0 and {r,e,s}uid!= 0 should
 *  never happen.
 *
 *  -astor
 *
 * cevans - New behaviour, Oct '99
 * A process may, via prctl(), elect to keep its capabilities when it
 * calls setuid() and switches away from uid==0. Both permitted and
 * effective sets will be retained.
 * Without this change, it was impossible for a daemon to drop only some
 * of its privilege. The call to setuid(!=0) would drop all privileges!
 * Keeping uid 0 is not an option because uid 0 owns too many vital
 * files..
 * Thanks to Olaf Kirch and Peter Benie for spotting this.
 */
static inline void cap_emulate_setxuid(struct cred *new, const struct cred *old)
{
	kuid_t root_uid = make_kuid(old->user_ns, 0);

	if ((uid_eq(old->uid, root_uid) ||
	     uid_eq(old->euid, root_uid) ||
	     uid_eq(old->suid, root_uid)) &&
	    (!uid_eq(new->uid, root_uid) &&
	     !uid_eq(new->euid, root_uid) &&
	     !uid_eq(new->suid, root_uid))) {
		if (!issecure(SECURE_KEEP_CAPS)) {
			cap_clear(new->cap_permitted);
			cap_clear(new->cap_effective);
		}

		/*
		 * Pre-ambient programs expect setresuid to nonroot followed
		 * by exec to drop capabilities.  We should make sure that
		 * this remains the case.
		 */
		cap_clear(new->cap_ambient);
	}
	if (uid_eq(old->euid, root_uid) && !uid_eq(new->euid, root_uid))
		cap_clear(new->cap_effective);
	if (!uid_eq(old->euid, root_uid) && uid_eq(new->euid, root_uid))
		new->cap_effective = new->cap_permitted;
}

/**
 * cap_task_fix_setuid - Fix up the results of setuid() call
 * @new: The proposed credentials
 * @old: The current task's current credentials
 * @flags: Indications of what has changed
 *
 * Fix up the results of setuid() call before the credential changes are
 * actually applied, returning 0 to grant the changes, -ve to deny them.
 */
int cap_task_fix_setuid(struct cred *new, const struct cred *old, int flags)
{
	switch (flags) {
	case LSM_SETID_RE:
	case LSM_SETID_ID:
	case LSM_SETID_RES:
		/* juggle the capabilities to follow [RES]UID changes unless
		 * otherwise suppressed */
		if (!issecure(SECURE_NO_SETUID_FIXUP))
			cap_emulate_setxuid(new, old);
		break;

	case LSM_SETID_FS:
		/* juggle the capabilties to follow FSUID changes, unless
		 * otherwise suppressed
		 *
		 * FIXME - is fsuser used for all CAP_FS_MASK capabilities?
		 *          if not, we might be a bit too harsh here.
		 */
		if (!issecure(SECURE_NO_SETUID_FIXUP)) {
			kuid_t root_uid = make_kuid(old->user_ns, 0);
			if (uid_eq(old->fsuid, root_uid) && !uid_eq(new->fsuid, root_uid))
				new->cap_effective =
					cap_drop_fs_set(new->cap_effective);

			if (!uid_eq(old->fsuid, root_uid) && uid_eq(new->fsuid, root_uid))
				new->cap_effective =
					cap_raise_fs_set(new->cap_effective,
							 new->cap_permitted);
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Rationale: code calling task_setscheduler, task_setioprio, and
 * task_setnice, assumes that
 *   . if capable(cap_sys_nice), then those actions should be allowed
 *   . if not capable(cap_sys_nice), but acting on your own processes,
 *   	then those actions should be allowed
 * This is insufficient now since you can call code without suid, but
 * yet with increased caps.
 * So we check for increased caps on the target process.
 */
static int cap_safe_nice(struct task_struct *p)
{
	int is_subset, ret = 0;

	rcu_read_lock();
	is_subset = cap_issubset(__task_cred(p)->cap_permitted,
				 current_cred()->cap_permitted);
	if (!is_subset && !ns_capable(__task_cred(p)->user_ns, CAP_SYS_NICE))
		ret = -EPERM;
	rcu_read_unlock();

	return ret;
}

/**
 * cap_task_setscheduler - Detemine if scheduler policy change is permitted
 * @p: The task to affect
 *
 * Detemine if the requested scheduler policy change is permitted for the
 * specified task, returning 0 if permission is granted, -ve if denied.
 */
int cap_task_setscheduler(struct task_struct *p)
{
	return cap_safe_nice(p);
}

/**
 * cap_task_ioprio - Detemine if I/O priority change is permitted
 * @p: The task to affect
 * @ioprio: The I/O priority to set
 *
 * Detemine if the requested I/O priority change is permitted for the specified
 * task, returning 0 if permission is granted, -ve if denied.
 */
int cap_task_setioprio(struct task_struct *p, int ioprio)
{
	return cap_safe_nice(p);
}

/**
 * cap_task_ioprio - Detemine if task priority change is permitted
 * @p: The task to affect
 * @nice: The nice value to set
 *
 * Detemine if the requested task priority change is permitted for the
 * specified task, returning 0 if permission is granted, -ve if denied.
 */
int cap_task_setnice(struct task_struct *p, int nice)
{
	return cap_safe_nice(p);
}

/*
 * Implement PR_CAPBSET_DROP.  Attempt to remove the specified capability from
 * the current task's bounding set.  Returns 0 on success, -ve on error.
 */
static int cap_prctl_drop(unsigned long cap)
{
	struct cred *new;

	if (!ns_capable(current_user_ns(), CAP_SETPCAP))
		return -EPERM;
	if (!cap_valid(cap))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	cap_lower(new->cap_bset, cap);
	return commit_creds(new);
}

/**
 * cap_task_prctl - Implement process control functions for this security module
 * @option: The process control function requested
 * @arg2, @arg3, @arg4, @arg5: The argument data for this function
 *
 * Allow process control functions (sys_prctl()) to alter capabilities; may
 * also deny access to other functions not otherwise implemented here.
 *
 * Returns 0 or +ve on success, -ENOSYS if this function is not implemented
 * here, other -ve on error.  If -ENOSYS is returned, sys_prctl() and other LSM
 * modules will consider performing the function.
 */
int cap_task_prctl(int option, unsigned long arg2, unsigned long arg3,
		   unsigned long arg4, unsigned long arg5)
{
	const struct cred *old = current_cred();
	struct cred *new;

	switch (option) {
	case PR_CAPBSET_READ:
		if (!cap_valid(arg2))
			return -EINVAL;
		return !!cap_raised(old->cap_bset, arg2);

	case PR_CAPBSET_DROP:
		return cap_prctl_drop(arg2);

	/*
	 * The next four prctl's remain to assist with transitioning a
	 * system from legacy UID=0 based privilege (when filesystem
	 * capabilities are not in use) to a system using filesystem
	 * capabilities only - as the POSIX.1e draft intended.
	 *
	 * Note:
	 *
	 *  PR_SET_SECUREBITS =
	 *      issecure_mask(SECURE_KEEP_CAPS_LOCKED)
	 *    | issecure_mask(SECURE_NOROOT)
	 *    | issecure_mask(SECURE_NOROOT_LOCKED)
	 *    | issecure_mask(SECURE_NO_SETUID_FIXUP)
	 *    | issecure_mask(SECURE_NO_SETUID_FIXUP_LOCKED)
	 *
	 * will ensure that the current process and all of its
	 * children will be locked into a pure
	 * capability-based-privilege environment.
	 */
	case PR_SET_SECUREBITS:
		if ((((old->securebits & SECURE_ALL_LOCKS) >> 1)
		     & (old->securebits ^ arg2))			/*[1]*/
		    || ((old->securebits & SECURE_ALL_LOCKS & ~arg2))	/*[2]*/
		    || (arg2 & ~(SECURE_ALL_LOCKS | SECURE_ALL_BITS))	/*[3]*/
		    || (cap_capable(current_cred(),
				    current_cred()->user_ns, CAP_SETPCAP,
				    SECURITY_CAP_AUDIT) != 0)		/*[4]*/
			/*
			 * [1] no changing of bits that are locked
			 * [2] no unlocking of locks
			 * [3] no setting of unsupported bits
			 * [4] doing anything requires privilege (go read about
			 *     the "sendmail capabilities bug")
			 */
		    )
			/* cannot change a locked bit */
			return -EPERM;

		new = prepare_creds();
		if (!new)
			return -ENOMEM;
		new->securebits = arg2;
		return commit_creds(new);

	case PR_GET_SECUREBITS:
		return old->securebits;

	case PR_GET_KEEPCAPS:
		return !!issecure(SECURE_KEEP_CAPS);

	case PR_SET_KEEPCAPS:
		if (arg2 > 1) /* Note, we rely on arg2 being unsigned here */
			return -EINVAL;
		if (issecure(SECURE_KEEP_CAPS_LOCKED))
			return -EPERM;

		new = prepare_creds();
		if (!new)
			return -ENOMEM;
		if (arg2)
			new->securebits |= issecure_mask(SECURE_KEEP_CAPS);
		else
			new->securebits &= ~issecure_mask(SECURE_KEEP_CAPS);
		return commit_creds(new);

	case PR_CAP_AMBIENT:
		if (arg2 == PR_CAP_AMBIENT_CLEAR_ALL) {
			if (arg3 | arg4 | arg5)
				return -EINVAL;

			new = prepare_creds();
			if (!new)
				return -ENOMEM;
			cap_clear(new->cap_ambient);
			return commit_creds(new);
		}

		if (((!cap_valid(arg3)) | arg4 | arg5))
			return -EINVAL;

		if (arg2 == PR_CAP_AMBIENT_IS_SET) {
			return !!cap_raised(current_cred()->cap_ambient, arg3);
		} else if (arg2 != PR_CAP_AMBIENT_RAISE &&
			   arg2 != PR_CAP_AMBIENT_LOWER) {
			return -EINVAL;
		} else {
			if (arg2 == PR_CAP_AMBIENT_RAISE &&
			    (!cap_raised(current_cred()->cap_permitted, arg3) ||
			     !cap_raised(current_cred()->cap_inheritable,
					 arg3) ||
			     issecure(SECURE_NO_CAP_AMBIENT_RAISE)))
				return -EPERM;

			new = prepare_creds();
			if (!new)
				return -ENOMEM;
			if (arg2 == PR_CAP_AMBIENT_RAISE)
				cap_raise(new->cap_ambient, arg3);
			else
				cap_lower(new->cap_ambient, arg3);
			return commit_creds(new);
		}

	default:
		/* No functionality available - continue with default */
		return -ENOSYS;
	}
}

/**
 * cap_vm_enough_memory - Determine whether a new virtual mapping is permitted
 * @mm: The VM space in which the new mapping is to be made
 * @pages: The size of the mapping
 *
 * Determine whether the allocation of a new virtual mapping by the current
 * task is permitted, returning 1 if permission is granted, 0 if not.
 */
int cap_vm_enough_memory(struct mm_struct *mm, long pages)
{
	int cap_sys_admin = 0;

	if (cap_capable(current_cred(), &init_user_ns, CAP_SYS_ADMIN,
			SECURITY_CAP_NOAUDIT) == 0)
		cap_sys_admin = 1;
	return cap_sys_admin;
}

/*
 * cap_mmap_addr - check if able to map given addr
 * @addr: address attempting to be mapped
 *
 * If the process is attempting to map memory below dac_mmap_min_addr they need
 * CAP_SYS_RAWIO.  The other parameters to this function are unused by the
 * capability security module.  Returns 0 if this mapping should be allowed
 * -EPERM if not.
 */
int cap_mmap_addr(unsigned long addr)
{
	int ret = 0;

	if (addr < dac_mmap_min_addr) {
		ret = cap_capable(current_cred(), &init_user_ns, CAP_SYS_RAWIO,
				  SECURITY_CAP_AUDIT);
		/* set PF_SUPERPRIV if it turns out we allow the low mmap */
		if (ret == 0)
			current->flags |= PF_SUPERPRIV;
	}
	return ret;
}

int cap_mmap_file(struct file *file, unsigned long reqprot,
		  unsigned long prot, unsigned long flags)
{
	return 0;
}

#ifdef CONFIG_SECURITY

struct security_hook_list capability_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(capable, cap_capable),
	LSM_HOOK_INIT(settime, cap_settime),
	LSM_HOOK_INIT(ptrace_access_check, cap_ptrace_access_check),
	LSM_HOOK_INIT(ptrace_traceme, cap_ptrace_traceme),
	LSM_HOOK_INIT(capget, cap_capget),
	LSM_HOOK_INIT(capset, cap_capset),
	LSM_HOOK_INIT(bprm_set_creds, cap_bprm_set_creds),
	LSM_HOOK_INIT(bprm_secureexec, cap_bprm_secureexec),
	LSM_HOOK_INIT(inode_need_killpriv, cap_inode_need_killpriv),
	LSM_HOOK_INIT(inode_killpriv, cap_inode_killpriv),
	LSM_HOOK_INIT(mmap_addr, cap_mmap_addr),
	LSM_HOOK_INIT(mmap_file, cap_mmap_file),
	LSM_HOOK_INIT(task_fix_setuid, cap_task_fix_setuid),
	LSM_HOOK_INIT(task_prctl, cap_task_prctl),
	LSM_HOOK_INIT(task_setscheduler, cap_task_setscheduler),
	LSM_HOOK_INIT(task_setioprio, cap_task_setioprio),
	LSM_HOOK_INIT(task_setnice, cap_task_setnice),
	LSM_HOOK_INIT(vm_enough_memory, cap_vm_enough_memory),
};

void __init capability_add_hooks(void)
{
	security_add_hooks(capability_hooks, ARRAY_SIZE(capability_hooks),
				"capability");
}

#endif /* CONFIG_SECURITY */
