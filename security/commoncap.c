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

int cap_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	NETLINK_CB(skb).eff_cap = current->cap_effective;
	return 0;
}

EXPORT_SYMBOL(cap_netlink_send);

int cap_netlink_recv(struct sk_buff *skb, int cap)
{
	if (!cap_raised(NETLINK_CB(skb).eff_cap, cap))
		return -EPERM;
	return 0;
}

EXPORT_SYMBOL(cap_netlink_recv);

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
	*effective = cap_t (target->cap_effective);
	*inheritable = cap_t (target->cap_inheritable);
	*permitted = cap_t (target->cap_permitted);
	return 0;
}

int cap_capset_check (struct task_struct *target, kernel_cap_t *effective,
		      kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	/* Derived from kernel/capability.c:sys_capset. */
	/* verify restrictions on target's new Inheritable set */
	if (!cap_issubset (*inheritable,
			   cap_combine (target->cap_inheritable,
					current->cap_permitted))) {
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

int cap_bprm_set_security (struct linux_binprm *bprm)
{
	/* Copied from fs/exec.c:prepare_binprm. */

	/* We don't have VFS support for capabilities yet */
	cap_clear (bprm->cap_inheritable);
	cap_clear (bprm->cap_permitted);
	cap_clear (bprm->cap_effective);

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
			cap_set_full (bprm->cap_effective);
	}
	return 0;
}

void cap_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{
	/* Derived from fs/exec.c:compute_creds. */
	kernel_cap_t new_permitted, working;

	new_permitted = cap_intersect (bprm->cap_permitted, cap_bset);
	working = cap_intersect (bprm->cap_inheritable,
				 current->cap_inheritable);
	new_permitted = cap_combine (new_permitted, working);

	if (bprm->e_uid != current->uid || bprm->e_gid != current->gid ||
	    !cap_issubset (new_permitted, current->cap_permitted)) {
		set_dumpable(current->mm, suid_dumpable);

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
	if (!is_init(current)) {
		current->cap_permitted = new_permitted;
		current->cap_effective =
		    cap_intersect (new_permitted, bprm->cap_effective);
	}

	/* AUD: Audit candidate if current->cap_effective is set */

	current->keep_capabilities = 0;
}

int cap_bprm_secureexec (struct linux_binprm *bprm)
{
	/* If/when this module is enhanced to incorporate capability
	   bits on files, the test below should be extended to also perform a 
	   test between the old and new capability sets.  For now,
	   it simply preserves the legacy decision algorithm used by
	   the old userland. */
	return (current->euid != current->uid ||
		current->egid != current->gid);
}

int cap_inode_setxattr(struct dentry *dentry, char *name, void *value,
		       size_t size, int flags)
{
	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1)  &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

int cap_inode_removexattr(struct dentry *dentry, char *name)
{
	if (!strncmp(name, XATTR_SECURITY_PREFIX,
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
					cap_t (current->cap_effective) &=
					    ~CAP_FS_MASK;
				}
				if (old_fsuid != 0 && current->fsuid == 0) {
					cap_t (current->cap_effective) |=
					    (cap_t (current->cap_permitted) &
					     CAP_FS_MASK);
				}
			}
			break;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

void cap_task_reparent_to_init (struct task_struct *p)
{
	p->cap_effective = CAP_INIT_EFF_SET;
	p->cap_inheritable = CAP_INIT_INH_SET;
	p->cap_permitted = CAP_FULL_SET;
	p->keep_capabilities = 0;
	return;
}

int cap_syslog (int type)
{
	if ((type != 3 && type != 10) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

int cap_vm_enough_memory(long pages)
{
	int cap_sys_admin = 0;

	if (cap_capable(current, CAP_SYS_ADMIN) == 0)
		cap_sys_admin = 1;
	return __vm_enough_memory(pages, cap_sys_admin);
}

EXPORT_SYMBOL(cap_capable);
EXPORT_SYMBOL(cap_settime);
EXPORT_SYMBOL(cap_ptrace);
EXPORT_SYMBOL(cap_capget);
EXPORT_SYMBOL(cap_capset_check);
EXPORT_SYMBOL(cap_capset_set);
EXPORT_SYMBOL(cap_bprm_set_security);
EXPORT_SYMBOL(cap_bprm_apply_creds);
EXPORT_SYMBOL(cap_bprm_secureexec);
EXPORT_SYMBOL(cap_inode_setxattr);
EXPORT_SYMBOL(cap_inode_removexattr);
EXPORT_SYMBOL(cap_task_post_setuid);
EXPORT_SYMBOL(cap_task_reparent_to_init);
EXPORT_SYMBOL(cap_syslog);
EXPORT_SYMBOL(cap_vm_enough_memory);

MODULE_DESCRIPTION("Standard Linux Common Capabilities Security Module");
MODULE_LICENSE("GPL");
