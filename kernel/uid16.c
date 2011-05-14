/*
 *	Wrapper functions for 16bit uid back compatibility. All nicely tied
 *	together in the faint hope we can take the out in five years time.
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

SYSCALL_DEFINE3(chown16, const char __user *, filename, old_uid_t, user, old_gid_t, group)
{
	long ret = sys_chown(filename, low2highuid(user), low2highgid(group));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, filename, user, group);
	return ret;
}

SYSCALL_DEFINE3(lchown16, const char __user *, filename, old_uid_t, user, old_gid_t, group)
{
	long ret = sys_lchown(filename, low2highuid(user), low2highgid(group));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, filename, user, group);
	return ret;
}

SYSCALL_DEFINE3(fchown16, unsigned int, fd, old_uid_t, user, old_gid_t, group)
{
	long ret = sys_fchown(fd, low2highuid(user), low2highgid(group));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, fd, user, group);
	return ret;
}

SYSCALL_DEFINE2(setregid16, old_gid_t, rgid, old_gid_t, egid)
{
	long ret = sys_setregid(low2highgid(rgid), low2highgid(egid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(2, ret, rgid, egid);
	return ret;
}

SYSCALL_DEFINE1(setgid16, old_gid_t, gid)
{
	long ret = sys_setgid(low2highgid(gid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(1, ret, gid);
	return ret;
}

SYSCALL_DEFINE2(setreuid16, old_uid_t, ruid, old_uid_t, euid)
{
	long ret = sys_setreuid(low2highuid(ruid), low2highuid(euid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(2, ret, ruid, euid);
	return ret;
}

SYSCALL_DEFINE1(setuid16, old_uid_t, uid)
{
	long ret = sys_setuid(low2highuid(uid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(1, ret, uid);
	return ret;
}

SYSCALL_DEFINE3(setresuid16, old_uid_t, ruid, old_uid_t, euid, old_uid_t, suid)
{
	long ret = sys_setresuid(low2highuid(ruid), low2highuid(euid),
				 low2highuid(suid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, ruid, euid, suid);
	return ret;
}

SYSCALL_DEFINE3(getresuid16, old_uid_t __user *, ruid, old_uid_t __user *, euid, old_uid_t __user *, suid)
{
	const struct cred *cred = current_cred();
	int retval;

	if (!(retval   = put_user(high2lowuid(cred->uid),  ruid)) &&
	    !(retval   = put_user(high2lowuid(cred->euid), euid)))
		retval = put_user(high2lowuid(cred->suid), suid);

	return retval;
}

SYSCALL_DEFINE3(setresgid16, old_gid_t, rgid, old_gid_t, egid, old_gid_t, sgid)
{
	long ret = sys_setresgid(low2highgid(rgid), low2highgid(egid),
				 low2highgid(sgid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, rgid, egid, sgid);
	return ret;
}


SYSCALL_DEFINE3(getresgid16, old_gid_t __user *, rgid, old_gid_t __user *, egid, old_gid_t __user *, sgid)
{
	const struct cred *cred = current_cred();
	int retval;

	if (!(retval   = put_user(high2lowgid(cred->gid),  rgid)) &&
	    !(retval   = put_user(high2lowgid(cred->egid), egid)))
		retval = put_user(high2lowgid(cred->sgid), sgid);

	return retval;
}

SYSCALL_DEFINE1(setfsuid16, old_uid_t, uid)
{
	long ret = sys_setfsuid(low2highuid(uid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(1, ret, uid);
	return ret;
}

SYSCALL_DEFINE1(setfsgid16, old_gid_t, gid)
{
	long ret = sys_setfsgid(low2highgid(gid));
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(1, ret, gid);
	return ret;
}

static int groups16_to_user(old_gid_t __user *grouplist,
    struct group_info *group_info)
{
	int i;
	old_gid_t group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = high2lowgid(GROUP_AT(group_info, i));
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info,
    old_gid_t __user *grouplist)
{
	int i;
	old_gid_t group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = low2highgid(group);
	}

	return 0;
}

SYSCALL_DEFINE2(getgroups16, int, gidsetsize, old_gid_t __user *, grouplist)
{
	const struct cred *cred = current_cred();
	int i;

	if (gidsetsize < 0)
		return -EINVAL;

	i = cred->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, cred->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	return i;
}

SYSCALL_DEFINE2(setgroups16, int, gidsetsize, old_gid_t __user *, grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!nsown_capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups16_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

SYSCALL_DEFINE0(getuid16)
{
	return high2lowuid(current_uid());
}

SYSCALL_DEFINE0(geteuid16)
{
	return high2lowuid(current_euid());
}

SYSCALL_DEFINE0(getgid16)
{
	return high2lowgid(current_gid());
}

SYSCALL_DEFINE0(getegid16)
{
	return high2lowgid(current_egid());
}
