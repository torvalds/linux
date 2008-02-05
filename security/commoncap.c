/* Common capabilities, needed by capability.o and root_plug.o
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/capability.h>
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

/* Global security state */

unsigned securebits = SECUREBITS_DEFAULT; /* systemwide security settings */
EXPORT_SYMBOL(securebits);

int cap_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	NETLINK_CB(skb).eff_cap = current->cap_effective;
	return 0;
}

int cap_netlink_recv(struct sk_buff *skb, int cap)
{
	if (!cap_raised(NETLINK_CB(skb).eff_cap, cap))
		return -EPERM;
	return 0;
}

EXPORT_SYMBOL(cap_netlink_recv);

/*
 * NOTE WELL: cap_capable() cannot be used like the kernel's capable()
 * function.  That is, it has the reverse semantics: cap_capable()
 * returns 0 when a task has a capability, but the kernel's capable()
 * returns 1 for this case.
 */
int cap_capable (struct task_struct *tsk, int cap)
{
	/* Derived from include/linux/sched.h:capable. */
	if (cap_raised(tsk->cap_effective, cap))
		return 0;
	return -EPERM;
}

int cap_settime(struct timespec *ts, struct timezone *tz)
{
	if (!capable(CAP_SYS_TIME))
		return -EPERM;
	return 0;
}

int cap_ptrace (struct task_struct *parent, struct task_struct *child)
{
	/* Derived from arch/i386/kernel/ptrace.c:sys_ptrace. */
	if (!cap_issubset(child->cap_permitted, parent->cap_permitted) &&
	    !__capable(parent, CAP_SYS_PTRACE))
		return -EPERM;
	return 0;
}

int cap_capget (struct task_struct *target, kernel_cap_t *effective,
		kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	/* Derived from kernel/capability.c:sys_capget. */
	*effective = target->cap_effective;
	*inheritable = target->cap_inheritable;
	*permitted = target->cap_permitted;
	return 0;
}

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES

static inline int cap_block_setpcap(struct task_struct *target)
{
	/*
	 * No support for remote process capability manipulation with
	 * filesystem capability support.
	 */
	return (target != current);
}

static inline int cap_inh_is_capped(void)
{
	/*
	 * Return 1 if changes to the inheritable set are limited
	 * to the old permitted set. That is, if the current task
	 * does *not* possess the CAP_SETPCAP capability.
	 */
	return (cap_capable(current, CAP_SETPCAP) != 0);
}

#else /* ie., ndef CONFIG_SECURITY_FILE_CAPABILITIES */

static inline int cap_block_setpcap(struct task_struct *t) { return 0; }
static inline int cap_inh_is_capped(void) { return 1; }

#endif /* def CONFIG_SECURITY_FILE_CAPABILITIES */

int cap_capset_check (struct task_struct *target, kernel_cap_t *effective,
		      kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	if (cap_block_setpcap(target)) {
		return -EPERM;
	}
	if (cap_inh_is_capped()
	    && !cap_issubset(*inheritable,
			     cap_combine(target->cap_inheritable,
					 current->cap_permitted))) {
		/* incapable of using this inheritable set */
		return -EPERM;
	}
	if (!cap_issubset(*inheritable,
			   cap_combine(target->cap_inheritable,
				       current->cap_bset))) {
		/* no new pI capabilities outside bounding set */
		return -EPERM;
	}

	/* verify restrictions on target's new Permitted set */
	if (!cap_issubset (*permitted,
			   cap_combine (target->cap_permitted,
					current->cap_permitted))) {
		return -EPERM;
	}

	/* verify the _new_Effective_ is a subset of the _new_Permitted_ */
	if (!cap_issubset (*effective, *permitted)) {
		return -EPERM;
	}

	return 0;
}

void cap_capset_set (struct task_struct *target, kernel_cap_t *effective,
		     kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	target->cap_effective = *effective;
	target->cap_inheritable = *inheritable;
	target->cap_permitted = *permitted;
}

static inline void bprm_clear_caps(struct linux_binprm *bprm)
{
	cap_clear(bprm->cap_inheritable);
	cap_clear(bprm->cap_permitted);
	bprm->cap_effective = false;
}

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES

int cap_inode_need_killpriv(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode->i_op || !inode->i_op->getxattr)
	       return 0;

	error = inode->i_op->getxattr(dentry, XATTR_NAME_CAPS, NULL, 0);
	if (error <= 0)
		return 0;
	return 1;
}

int cap_inode_killpriv(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	if (!inode->i_op || !inode->i_op->removexattr)
	       return 0;

	return inode->i_op->removexattr(dentry, XATTR_NAME_CAPS);
}

static inline int cap_from_disk(struct vfs_cap_data *caps,
				struct linux_binprm *bprm, unsigned size)
{
	__u32 magic_etc;
	unsigned tocopy, i;

	if (size < sizeof(magic_etc))
		return -EINVAL;

	magic_etc = le32_to_cpu(caps->magic_etc);

	switch ((magic_etc & VFS_CAP_REVISION_MASK)) {
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

	if (magic_etc & VFS_CAP_FLAGS_EFFECTIVE) {
		bprm->cap_effective = true;
	} else {
		bprm->cap_effective = false;
	}

	for (i = 0; i < tocopy; ++i) {
		bprm->cap_permitted.cap[i] =
			le32_to_cpu(caps->data[i].permitted);
		bprm->cap_inheritable.cap[i] =
			le32_to_cpu(caps->data[i].inheritable);
	}
	while (i < VFS_CAP_U32) {
		bprm->cap_permitted.cap[i] = 0;
		bprm->cap_inheritable.cap[i] = 0;
		i++;
	}

	return 0;
}

/* Locate any VFS capabilities: */
static int get_file_caps(struct linux_binprm *bprm)
{
	struct dentry *dentry;
	int rc = 0;
	struct vfs_cap_data vcaps;
	struct inode *inode;

	if (bprm->file->f_vfsmnt->mnt_flags & MNT_NOSUID) {
		bprm_clear_caps(bprm);
		return 0;
	}

	dentry = dget(bprm->file->f_dentry);
	inode = dentry->d_inode;
	if (!inode->i_op || !inode->i_op->getxattr)
		goto out;

	rc = inode->i_op->getxattr(dentry, XATTR_NAME_CAPS, &vcaps,
				   XATTR_CAPS_SZ);
	if (rc == -ENODATA || rc == -EOPNOTSUPP) {
		/* no data, that's ok */
		rc = 0;
		goto out;
	}
	if (rc < 0)
		goto out;

	rc = cap_from_disk(&vcaps, bprm, rc);
	if (rc)
		printk(KERN_NOTICE "%s: cap_from_disk returned %d for %s\n",
			__FUNCTION__, rc, bprm->filename);

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

static inline int get_file_caps(struct linux_binprm *bprm)
{
	bprm_clear_caps(bprm);
	return 0;
}
#endif

int cap_bprm_set_security (struct linux_binprm *bprm)
{
	int ret;

	ret = get_file_caps(bprm);
	if (ret)
		printk(KERN_NOTICE "%s: get_file_caps returned %d for %s\n",
			__FUNCTION__, ret, bprm->filename);

	/*  To support inheritance of root-permissions and suid-root
	 *  executables under compatibility mode, we raise all three
	 *  capability sets for the file.
	 *
	 *  If only the real uid is 0, we only raise the inheritable
	 *  and permitted sets of the executable file.
	 */

	if (!issecure (SECURE_NOROOT)) {
		if (bprm->e_uid == 0 || current->uid == 0) {
			cap_set_full (bprm->cap_inheritable);
			cap_set_full (bprm->cap_permitted);
		}
		if (bprm->e_uid == 0)
			bprm->cap_effective = true;
	}

	return ret;
}

void cap_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{
	/* Derived from fs/exec.c:compute_creds. */
	kernel_cap_t new_permitted, working;

	new_permitted = cap_intersect(bprm->cap_permitted,
				 current->cap_bset);
	working = cap_intersect(bprm->cap_inheritable,
				 current->cap_inheritable);
	new_permitted = cap_combine(new_permitted, working);

	if (bprm->e_uid != current->uid || bprm->e_gid != current->gid ||
	    !cap_issubset (new_permitted, current->cap_permitted)) {
		set_dumpable(current->mm, suid_dumpable);
		current->pdeath_signal = 0;

		if (unsafe & ~LSM_UNSAFE_PTRACE_CAP) {
			if (!capable(CAP_SETUID)) {
				bprm->e_uid = current->uid;
				bprm->e_gid = current->gid;
			}
			if (!capable (CAP_SETPCAP)) {
				new_permitted = cap_intersect (new_permitted,
							current->cap_permitted);
			}
		}
	}

	current->suid = current->euid = current->fsuid = bprm->e_uid;
	current->sgid = current->egid = current->fsgid = bprm->e_gid;

	/* For init, we want to retain the capabilities set
	 * in the init_task struct. Thus we skip the usual
	 * capability rules */
	if (!is_global_init(current)) {
		current->cap_permitted = new_permitted;
		if (bprm->cap_effective)
			current->cap_effective = new_permitted;
		else
			cap_clear(current->cap_effective);
	}

	/* AUD: Audit candidate if current->cap_effective is set */

	current->keep_capabilities = 0;
}

int cap_bprm_secureexec (struct linux_binprm *bprm)
{
	if (current->uid != 0) {
		if (bprm->cap_effective)
			return 1;
		if (!cap_isclear(bprm->cap_permitted))
			return 1;
		if (!cap_isclear(bprm->cap_inheritable))
			return 1;
	}

	return (current->euid != current->uid ||
		current->egid != current->gid);
}

int cap_inode_setxattr(struct dentry *dentry, char *name, void *value,
		       size_t size, int flags)
{
	if (!strcmp(name, XATTR_NAME_CAPS)) {
		if (!capable(CAP_SETFCAP))
			return -EPERM;
		return 0;
	} else if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1)  &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

int cap_inode_removexattr(struct dentry *dentry, char *name)
{
	if (!strcmp(name, XATTR_NAME_CAPS)) {
		if (!capable(CAP_SETFCAP))
			return -EPERM;
		return 0;
	} else if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1)  &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/* moved from kernel/sys.c. */
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
static inline void cap_emulate_setxuid (int old_ruid, int old_euid,
					int old_suid)
{
	if ((old_ruid == 0 || old_euid == 0 || old_suid == 0) &&
	    (current->uid != 0 && current->euid != 0 && current->suid != 0) &&
	    !current->keep_capabilities) {
		cap_clear (current->cap_permitted);
		cap_clear (current->cap_effective);
	}
	if (old_euid == 0 && current->euid != 0) {
		cap_clear (current->cap_effective);
	}
	if (old_euid != 0 && current->euid == 0) {
		current->cap_effective = current->cap_permitted;
	}
}

int cap_task_post_setuid (uid_t old_ruid, uid_t old_euid, uid_t old_suid,
			  int flags)
{
	switch (flags) {
	case LSM_SETID_RE:
	case LSM_SETID_ID:
	case LSM_SETID_RES:
		/* Copied from kernel/sys.c:setreuid/setuid/setresuid. */
		if (!issecure (SECURE_NO_SETUID_FIXUP)) {
			cap_emulate_setxuid (old_ruid, old_euid, old_suid);
		}
		break;
	case LSM_SETID_FS:
		{
			uid_t old_fsuid = old_ruid;

			/* Copied from kernel/sys.c:setfsuid. */

			/*
			 * FIXME - is fsuser used for all CAP_FS_MASK capabilities?
			 *          if not, we might be a bit too harsh here.
			 */

			if (!issecure (SECURE_NO_SETUID_FIXUP)) {
				if (old_fsuid == 0 && current->fsuid != 0) {
					current->cap_effective =
						cap_drop_fs_set(
						    current->cap_effective);
				}
				if (old_fsuid != 0 && current->fsuid == 0) {
					current->cap_effective =
						cap_raise_fs_set(
						    current->cap_effective,
						    current->cap_permitted);
				}
			}
			break;
		}
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
static inline int cap_safe_nice(struct task_struct *p)
{
	if (!cap_issubset(p->cap_permitted, current->cap_permitted) &&
	    !__capable(current, CAP_SYS_NICE))
		return -EPERM;
	return 0;
}

int cap_task_setscheduler (struct task_struct *p, int policy,
			   struct sched_param *lp)
{
	return cap_safe_nice(p);
}

int cap_task_setioprio (struct task_struct *p, int ioprio)
{
	return cap_safe_nice(p);
}

int cap_task_setnice (struct task_struct *p, int nice)
{
	return cap_safe_nice(p);
}

int cap_task_kill(struct task_struct *p, struct siginfo *info,
				int sig, u32 secid)
{
	if (info != SEND_SIG_NOINFO && (is_si_special(info) || SI_FROMKERNEL(info)))
		return 0;

	/*
	 * Running a setuid root program raises your capabilities.
	 * Killing your own setuid root processes was previously
	 * allowed.
	 * We must preserve legacy signal behavior in this case.
	 */
	if (p->euid == 0 && p->uid == current->uid)
		return 0;

	/* sigcont is permitted within same session */
	if (sig == SIGCONT && (task_session_nr(current) == task_session_nr(p)))
		return 0;

	if (secid)
		/*
		 * Signal sent as a particular user.
		 * Capabilities are ignored.  May be wrong, but it's the
		 * only thing we can do at the moment.
		 * Used only by usb drivers?
		 */
		return 0;
	if (cap_issubset(p->cap_permitted, current->cap_permitted))
		return 0;
	if (capable(CAP_KILL))
		return 0;

	return -EPERM;
}

/*
 * called from kernel/sys.c for prctl(PR_CABSET_DROP)
 * done without task_capability_lock() because it introduces
 * no new races - i.e. only another task doing capget() on
 * this task could get inconsistent info.  There can be no
 * racing writer bc a task can only change its own caps.
 */
long cap_prctl_drop(unsigned long cap)
{
	if (!capable(CAP_SETPCAP))
		return -EPERM;
	if (!cap_valid(cap))
		return -EINVAL;
	cap_lower(current->cap_bset, cap);
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
int cap_task_kill(struct task_struct *p, struct siginfo *info,
				int sig, u32 secid)
{
	return 0;
}
#endif

void cap_task_reparent_to_init (struct task_struct *p)
{
	cap_set_init_eff(p->cap_effective);
	cap_clear(p->cap_inheritable);
	cap_set_full(p->cap_permitted);
	p->keep_capabilities = 0;
	return;
}

int cap_syslog (int type)
{
	if ((type != 3 && type != 10) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

int cap_vm_enough_memory(struct mm_struct *mm, long pages)
{
	int cap_sys_admin = 0;

	if (cap_capable(current, CAP_SYS_ADMIN) == 0)
		cap_sys_admin = 1;
	return __vm_enough_memory(mm, pages, cap_sys_admin);
}

