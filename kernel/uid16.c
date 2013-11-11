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
	return sys_chown(filename, low2highuid(user), low2highgid(group));
}

SYSCALL_DEFINE3(lchown16, const char __user *, filename, old_uid_t, user, old_gid_t, group)
{
	return sys_lchown(filename, low2highuid(user), low2highgid(group));
}

SYSCALL_DEFINE3(fchown16, unsigned int, fd, old_uid_t, user, old_gid_t, group)
{
	return sys_fchown(fd, low2highuid(user), low2highgid(group));
}

SYSCALL_DEFINE2(setregid16, old_gid_t, rgid, old_gid_t, egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

SYSCALL_DEFINE1(setgid16, old_gid_t, gid)
{
	return sys_setgid(low2highgid(gid));
}

SYSCALL_DEFINE2(setreuid16, old_uid_t, ruid, old_uid_t, euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

SYSCALL_DEFINE1(setuid16, old_uid_t, uid)
{
	return sys_setuid(low2highuid(uid));
}

SYSCALL_DEFINE3(setresuid16, old_uid_t, ruid, old_uid_t, euid, old_uid_t, suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
				 low2highuid(suid));
}

SYSCALL_DEFINE3(getresuid16, old_uid_t __user *, ruidp, old_uid_t __user *, euidp, old_uid_t __user *, suidp)
{
	const struct cred *cred = current_cred();
	int retval;
	old_uid_t ruid, euid, suid;

	ruid = high2lowuid(from_kuid_munged(cred->user_ns, cred->uid));
	euid = high2lowuid(from_kuid_munged(cred->user_ns, cred->euid));
	suid = high2lowuid(from_kuid_munged(cred->user_ns, cred->suid));

	if (!(retval   = put_user(ruid, ruidp)) &&
	    !(retval   = put_user(euid, euidp)))
		retval = put_user(suid, suidp);

	return retval;
}

SYSCALL_DEFINE3(setresgid16, old_gid_t, rgid, old_gid_t, egid, old_gid_t, sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
				 low2highgid(sgid));
}


SYSCALL_DEFINE3(getresgid16, old_gid_t __user *, rgidp, old_gid_t __user *, egidp, old_gid_t __user *, sgidp)
{
	const struct cred *cred = current_cred();
	int retval;
	old_gid_t rgid, egid, sgid;

	rgid = high2lowgid(from_kgid_munged(cred->user_ns, cred->gid));
	egid = high2lowgid(from_kgid_munged(cred->user_ns, cred->egid));
	sgid = high2lowgid(from_kgid_munged(cred->user_ns, cred->sgid));

	if (!(retval   = put_user(rgid, rgidp)) &&
	    !(retval   = put_user(egid, egidp)))
		retval = put_user(sgid, sgidp);

	return retval;
}

SYSCALL_DEFINE1(setfsuid16, old_uid_t, uid)
{
	return sys_setfsuid(low2highuid(uid));
}

SYSCALL_DEFINE1(setfsgid16, old_gid_t, gid)
{
	return sys_setfsgid(low2highgid(gid));
}

static int groups16_to_user(old_gid_t __user *grouplist,
    struct group_info *group_info)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	old_gid_t group;
	kgid_t kgid;

	for (i = 0; i < group_info->ngroups; i++) {
		kgid = GROUP_AT(group_info, i);
		group = high2lowgid(from_kgid_munged(user_ns, kgid));
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info,
    old_gid_t __user *grouplist)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	old_gid_t group;
	kgid_t kgid;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;

		kgid = make_kgid(user_ns, low2highgid(group));
		if (!gid_valid(kgid))
			return -EINVAL;

		GROUP_AT(group_info, i) = kgid;
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
	return high2lowuid(from_kuid_munged(current_user_ns(), current_uid()));
}

SYSCALL_DEFINE0(geteuid16)
{
	return high2lowuid(from_kuid_munged(current_user_ns(), current_euid()));
}

SYSCALL_DEFINE0(getgid16)
{
	return high2lowgid(from_kgid_munged(current_user_ns(), current_gid()));
}

SYSCALL_DEFINE0(getegid16)
{
	return high2lowgid(from_kgid_munged(current_user_ns(), current_egid()));
}
