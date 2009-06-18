/* Common capabilities, needed by capability.o and root_plug.o
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
#include <linux/security.h>
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

#ifdef CONFIG_ANDROID_PARANOID_NETWORK
#include <linux/android_aid.h>
#endif

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
static void warn_setuid_and_fcaps_mixed(char *fname)
{
	static int warned;
	if (!warned) {
		printk(KERN_INFO "warning: `%s' has both setuid-root and"
			" effective capabilities. Therefore not raising all"
			" capabilities.\n", fname);
		warned = 1;
	}
}

int cap_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	NETLINK_CB(skb).eff_cap = current_cap();
	return 0;
}

int cap_netlink_recv(struct sk_buff *skb, int cap)
{
	if (!cap_raised(NETLINK_CB(skb).eff_cap, cap))
		return -EPERM;
	return 0;
}
EXPORT_SYMBOL(cap_netlink_recv);

/**
 * cap_capable - Determine whether a task has a particular effective capability
 * @tsk: The task to query
 * @cred: The credentials to use
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
int cap_capable(struct task_struct *tsk, const struct cred *cred, int cap,
		int audit)
{
#ifdef CONFIG_ANDROID_PARANOID_NETWORK
	if (cap == CAP_NET_RAW && in_egroup_p(AID_NET_RAW))
		return 0;
	if (cap == CAP_NET_ADMIN && in_egroup_p(AID_NET_ADMIN))
		return 0;
#endif
	return cap_raised(cred->cap_effective, cap) ? 0 : -EPERM;
}

/**
 * cap_settime - Determine whether the current process may set the system clock
 * @ts: The time to set
 * @tz: The timezone to set
 *
 * Determine whether the current process may set the system clock and timezone
 * information, returning 0 if permission granted, -ve if denied.
 */
int cap_settime(struct timespec *ts, struct timezone *tz)
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
 * Determine whether a process may access another, returning 0 if permission
 * granted, -ve if denied.
 */
int cap_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	int ret = 0;

	rcu_read_lock();
	if (!cap_issubset(__task_cred(child)->cap_permitted,
			  current_cred()->cap_permitted) &&
	    !capable(CAP_SYS_PTRACE))
		ret = -EPERM;
	rcu_read_unlock();
	return ret;
}

/**
 * cap_ptrace_traceme - Determine whether another process may trace the current
 * @parent: The task proposed to be the tracer
 *
 * Determine whether the nominated task is permitted to trace the current
 * process, returning 0 if permission is granted, -ve if denied.
 */
int cap_ptrace_traceme(struct task_struct *parent)
{
	int ret = 0;

	rcu_read_lock();
	if (!cap_issubset(current_cred()->cap_permitted,
			  __task_cred(parent)->cap_permitted) &&
	    !has_capability(parent, CAP_SYS_PTRACE))
		ret = -EPERM;
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
#ifdef CONFIG_SECURITY_FILE_CAPABILITIES

	/* they are so limited unless the current task has the CAP_SETPCAP
	 * capability
	 */
	if (cap_capable(current, current_cred(), CAP_SETPCAP,
			SECURITY_CAP_AUDIT) == 0)
		return 0;
#endif
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

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES

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
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode->i_op->getxattr)
	       return 0;

	error = inode->i_op->getxattr(dentry, XATTR_NAME_CAPS, NULL, 0);
	if (error <= 0)
		return 0;
	return 1;
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
	struct inode *inode = dentry->d_inode;

	if (!inode->i_op->removexattr)
	       return 0;

	return inode->i_op->removexattr(dentry, XATTR_NAME_CAPS);
}

/*
 * Calculate the new process capability sets from the capability sets attached
 * to a file.
 */
static inline int bprm_caps_from_vfs_caps(struct cpu_vfs_cap_data *caps,
					  struct linux_binprm *bprm,
					  bool *effective)
{
	struct cred *new = bprm->cred;
	unsigned i;
	int ret = 0;

	if (caps->magic_etc & VFS_CAP_FLAGS_EFFECTIVE)
		*effective = true;

	CAP_FOR_EACH_U32(i) {
		__u32 permitted = caps->permitted.cap[i];
		__u32 inheritable = caps->inheritable.cap[i];

		/*
		 * pP' = (X & fP) | (pI & fI)
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
	struct inode *inode = dentry->d_inode;
	__u32 magic_etc;
	unsigned tocopy, i;
	int size;
	struct vfs_cap_data caps;

	memset(cpu_caps, 0, sizeof(struct cpu_vfs_cap_data));

	if (!inode || !inode->i_op->getxattr)
		return -ENODATA;

	size = inode->i_op->getxattr((struct dentry *)dentry, XATTR_NAME_CAPS, &caps,
				   XATTR_CAPS_SZ);
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

	return 0;
}

/*
 * Attempt to get the on-exec apply capability sets for an executable file from
 * its xattrs and, if present, apply them to the proposed credentials being
 * constructed by execve().
 */
static int get_file_caps(struct linux_binprm *bprm, bool *effective)
{
	struct dentry *dentry;
	int rc = 0;
	struct cpu_vfs_cap_data vcaps;

	bprm_clear_caps(bprm);

	if (!file_caps_enabled)
		return 0;

	if (bprm->file->f_vfsmnt->mnt_flags & MNT_NOSUID)
		return 0;

	dentry = dget(bprm->file->f_dentry);

	rc = get_vfs_caps_from_disk(dentry, &vcaps);
	if (rc < 0) {
		if (rc == -EINVAL)
			printk(KERN_NOTICE "%s: get_vfs_caps_from_disk returned %d for %s\n",
				__func__, rc, bprm->filename);
		else if (rc == -ENODATA)
			rc = 0;
		goto out;
	}

	rc = bprm_caps_from_vfs_caps(&vcaps, bprm, effective);
	if (rc == -EINVAL)
		printk(KERN_NOTICE "%s: cap_from_disk returned %d for %s\n",
		       __func__, rc, bprm->filename);

out:
	dput(dentry);
	if (rc)
		bprm_clear_caps(bprm);

	return rc;
}

#else
int cap_inode_need_killpriv(struct dentry *dentry)
{
	return 0;
}

int cap_inode_killpriv(struct dentry *dentry)
{
	return 0;
}

int get_vfs_caps_from_disk(const struct dentry *dentry, struct cpu_vfs_cap_data *cpu_caps)
{
	memset(cpu_caps, 0, sizeof(struct cpu_vfs_cap_data));
 	return -ENODATA;
}

static inline int get_file_caps(struct linux_binprm *bprm, bool *effective)
{
	bprm_clear_caps(bprm);
	return 0;
}
#endif

/*
 * Determine whether a exec'ing process's new permitted capabilities should be
 * limited to just what it already has.
 *
 * This prevents processes that are being ptraced from gaining access to
 * CAP_SETPCAP, unless the process they're tracing already has it, and the
 * binary they're executing has filecaps that elevate it.
 *
 *  Returns 1 if they should be limited, 0 if they are not.
 */
static inline int cap_limit_ptraced_target(void)
{
#ifndef CONFIG_SECURITY_FILE_CAPABILITIES
	if (capable(CAP_SETPCAP))
		return 0;
#endif
	return 1;
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
	bool effective;
	int ret;

	effective = false;
	ret = get_file_caps(bprm, &effective);
	if (ret < 0)
		return ret;

	if (!issecure(SECURE_NOROOT)) {
		/*
		 * If the legacy file capability is set, then don't set privs
		 * for a setuid root binary run by a non-root user.  Do set it
		 * for a root user just to cause least surprise to an admin.
		 */
		if (effective && new->uid != 0 && new->euid == 0) {
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
		if (new->euid == 0 || new->uid == 0) {
			/* pP' = (cap_bset & ~0) | (pI & ~0) */
			new->cap_permitted = cap_combine(old->cap_bset,
							 old->cap_inheritable);
		}
		if (new->euid == 0)
			effective = true;
	}
skip:

	/* Don't let someone trace a set[ug]id/setpcap binary with the revised
	 * credentials unless they have the appropriate permit
	 */
	if ((new->euid != old->uid ||
	     new->egid != old->gid ||
	     !cap_issubset(new->cap_permitted, old->cap_permitted)) &&
	    bprm->unsafe & ~LSM_UNSAFE_PTRACE_CAP) {
		/* downgrade; they get no more than they had, and maybe less */
		if (!capable(CAP_SETUID)) {
			new->euid = new->uid;
			new->egid = new->gid;
		}
		if (cap_limit_ptraced_target())
			new->cap_permitted = cap_intersect(new->cap_permitted,
							   old->cap_permitted);
	}

	new->suid = new->fsuid = new->euid;
	new->sgid = new->fsgid = new->egid;

	/* For init, we want to retain the capabilities set in the initial
	 * task.  Thus we skip the usual capability rules
	 */
	if (!is_global_init(current)) {
		if (effective)
			new->cap_effective = new->cap_permitted;
		else
			cap_clear(new->cap_effective);
	}
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
	if (!cap_isclear(new->cap_effective)) {
		if (!cap_issubset(CAP_FULL_SET, new->cap_effective) ||
		    new->euid != 0 || new->uid != 0 ||
		    issecure(SECURE_NOROOT)) {
			ret = audit_log_bprm_fcaps(bprm, new, old);
			if (ret < 0)
				return ret;
		}
	}

	new->securebits &= ~issecure_mask(SECURE_KEEP_CAPS);
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

	if (cred->uid != 0) {
		if (bprm->cap_effective)
			return 1;
		if (!cap_isclear(cred->cap_permitted))
			return 1;
	}

	return (cred->euid != cred->uid ||
		cred->egid != cred->gid);
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
		     sizeof(XATTR_SECURITY_PREFIX) - 1)  &&
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
		     sizeof(XATTR_SECURITY_PREFIX) - 1)  &&
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
	if ((old->uid == 0 || old->euid == 0 || old->suid == 0) &&
	    (new->uid != 0 && new->euid != 0 && new->suid != 0) &&
	    !issecure(SECURE_KEEP_CAPS)) {
		cap_clear(new->cap_permitted);
		cap_clear(new->cap_effective);
	}
	if (old->euid == 0 && new->euid != 0)
		cap_clear(new->cap_effective);
	if (old->euid != 0 && new->euid == 0)
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
			if (old->fsuid == 0 && new->fsuid != 0)
				new->cap_effective =
					cap_drop_fs_set(new->cap_effective);

			if (old->fsuid != 0 && new->fsuid == 0)
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

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES
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
	int is_subset;

	rcu_read_lock();
	is_subset = cap_issubset(__task_cred(p)->cap_permitted,
				 current_cred()->cap_permitted);
	rcu_read_unlock();

	if (!is_subset && !capable(CAP_SYS_NICE))
		return -EPERM;
	return 0;
}

/**
 * cap_task_setscheduler - Detemine if scheduler policy change is permitted
 * @p: The task to affect
 * @policy: The policy to effect
 * @lp: The parameters to the scheduling policy
 *
 * Detemine if the requested scheduler policy change is permitted for the
 * specified task, returning 0 if permission is granted, -ve if denied.
 */
int cap_task_setscheduler(struct task_struct *p, int policy,
			   struct sched_param *lp)
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
static long cap_prctl_drop(struct cred *new, unsigned long cap)
{
	if (!capable(CAP_SETPCAP))
		return -EPERM;
	if (!cap_valid(cap))
		return -EINVAL;

	cap_lower(new->cap_bset, cap);
	return 0;
}

#else
int cap_task_setscheduler (struct task_struct *p, int policy,
			   struct sched_param *lp)
{
	return 0;
}
int cap_task_setioprio (struct task_struct *p, int ioprio)
{
	return 0;
}
int cap_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}
#endif

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
	struct cred *new;
	long error = 0;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	switch (option) {
	case PR_CAPBSET_READ:
		error = -EINVAL;
		if (!cap_valid(arg2))
			goto error;
		error = !!cap_raised(new->cap_bset, arg2);
		goto no_change;

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES
	case PR_CAPBSET_DROP:
		error = cap_prctl_drop(new, arg2);
		if (error < 0)
			goto error;
		goto changed;

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
		error = -EPERM;
		if ((((new->securebits & SECURE_ALL_LOCKS) >> 1)
		     & (new->securebits ^ arg2))			/*[1]*/
		    || ((new->securebits & SECURE_ALL_LOCKS & ~arg2))	/*[2]*/
		    || (arg2 & ~(SECURE_ALL_LOCKS | SECURE_ALL_BITS))	/*[3]*/
		    || (cap_capable(current, current_cred(), CAP_SETPCAP,
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
			goto error;
		new->securebits = arg2;
		goto changed;

	case PR_GET_SECUREBITS:
		error = new->securebits;
		goto no_change;

#endif /* def CONFIG_SECURITY_FILE_CAPABILITIES */

	case PR_GET_KEEPCAPS:
		if (issecure(SECURE_KEEP_CAPS))
			error = 1;
		goto no_change;

	case PR_SET_KEEPCAPS:
		error = -EINVAL;
		if (arg2 > 1) /* Note, we rely on arg2 being unsigned here */
			goto error;
		error = -EPERM;
		if (issecure(SECURE_KEEP_CAPS_LOCKED))
			goto error;
		if (arg2)
			new->securebits |= issecure_mask(SECURE_KEEP_CAPS);
		else
			new->securebits &= ~issecure_mask(SECURE_KEEP_CAPS);
		goto changed;

	default:
		/* No functionality available - continue with default */
		error = -ENOSYS;
		goto error;
	}

	/* Functionality provided */
changed:
	return commit_creds(new);

no_change:
error:
	abort_creds(new);
	return error;
}

/**
 * cap_syslog - Determine whether syslog function is permitted
 * @type: Function requested
 *
 * Determine whether the current process is permitted to use a particular
 * syslog function, returning 0 if permission is granted, -ve if not.
 */
int cap_syslog(int type)
{
	if ((type != 3 && type != 10) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/**
 * cap_vm_enough_memory - Determine whether a new virtual mapping is permitted
 * @mm: The VM space in which the new mapping is to be made
 * @pages: The size of the mapping
 *
 * Determine whether the allocation of a new virtual mapping by the current
 * task is permitted, returning 0 if permission is granted, -ve if not.
 */
int cap_vm_enough_memory(struct mm_struct *mm, long pages)
{
	int cap_sys_admin = 0;

	if (cap_capable(current, current_cred(), CAP_SYS_ADMIN,
			SECURITY_CAP_NOAUDIT) == 0)
		cap_sys_admin = 1;
	return __vm_enough_memory(mm, pages, cap_sys_admin);
}

/*
 * cap_file_mmap - check if able to map given addr
 * @file: unused
 * @reqprot: unused
 * @prot: unused
 * @flags: unused
 * @addr: address attempting to be mapped
 * @addr_only: unused
 *
 * If the process is attempting to map memory below mmap_min_addr they need
 * CAP_SYS_RAWIO.  The other parameters to this function are unused by the
 * capability security module.  Returns 0 if this mapping should be allowed
 * -EPERM if not.
 */
int cap_file_mmap(struct file *file, unsigned long reqprot,
		  unsigned long prot, unsigned long flags,
		  unsigned long addr, unsigned long addr_only)
{
	int ret = 0;

	if (addr < dac_mmap_min_addr) {
		ret = cap_capable(current, current_cred(), CAP_SYS_RAWIO,
				  SECURITY_CAP_AUDIT);
		/* set PF_SUPERPRIV if it turns out we allow the low mmap */
		if (ret == 0)
			current->flags |= PF_SUPERPRIV;
	}
	return ret;
}
