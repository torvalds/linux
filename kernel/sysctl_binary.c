// SPDX-License-Identifier: GPL-2.0
#include <linux/stat.h>
#include <linux/sysctl.h>
#include "../fs/xfs/xfs_sysctl.h"
#include <linux/sunrpc/debug.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/file.h>
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/uuid.h>
#include <linux/slab.h>
#include <linux/compat.h>

static ssize_t binary_sysctl(const int *name, int nlen,
	void __user *oldval, size_t oldlen, void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

static void deprecated_sysctl_warning(const int *name, int nlen)
{
	int i;

	/*
	 * CTL_KERN/KERN_VERSION is used by older glibc and cannot
	 * ever go away.
	 */
	if (nlen >= 2 && name[0] == CTL_KERN && name[1] == KERN_VERSION)
		return;

	if (printk_ratelimit()) {
		printk(KERN_INFO
			"warning: process `%s' used the deprecated sysctl "
			"system call with ", current->comm);
		for (i = 0; i < nlen; i++)
			printk(KERN_CONT "%d.", name[i]);
		printk(KERN_CONT "\n");
	}
	return;
}

#define WARN_ONCE_HASH_BITS 8
#define WARN_ONCE_HASH_SIZE (1<<WARN_ONCE_HASH_BITS)

static DECLARE_BITMAP(warn_once_bitmap, WARN_ONCE_HASH_SIZE);

#define FNV32_OFFSET 2166136261U
#define FNV32_PRIME 0x01000193

/*
 * Print each legacy sysctl (approximately) only once.
 * To avoid making the tables non-const use a external
 * hash-table instead.
 * Worst case hash collision: 6, but very rarely.
 * NOTE! We don't use the SMP-safe bit tests. We simply
 * don't care enough.
 */
static void warn_on_bintable(const int *name, int nlen)
{
	int i;
	u32 hash = FNV32_OFFSET;

	for (i = 0; i < nlen; i++)
		hash = (hash ^ name[i]) * FNV32_PRIME;
	hash %= WARN_ONCE_HASH_SIZE;
	if (__test_and_set_bit(hash, warn_once_bitmap))
		return;
	deprecated_sysctl_warning(name, nlen);
}

static ssize_t do_sysctl(int __user *args_name, int nlen,
	void __user *oldval, size_t oldlen, void __user *newval, size_t newlen)
{
	int name[CTL_MAXNAME];
	int i;

	/* Check args->nlen. */
	if (nlen < 0 || nlen > CTL_MAXNAME)
		return -ENOTDIR;
	/* Read in the sysctl name for simplicity */
	for (i = 0; i < nlen; i++)
		if (get_user(name[i], args_name + i))
			return -EFAULT;

	warn_on_bintable(name, nlen);

	return binary_sysctl(name, nlen, oldval, oldlen, newval, newlen);
}

SYSCALL_DEFINE1(sysctl, struct __sysctl_args __user *, args)
{
	struct __sysctl_args tmp;
	size_t oldlen = 0;
	ssize_t result;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && !tmp.oldlenp)
		return -EFAULT;

	if (tmp.oldlenp && get_user(oldlen, tmp.oldlenp))
		return -EFAULT;

	result = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, oldlen,
			   tmp.newval, tmp.newlen);

	if (result >= 0) {
		oldlen = result;
		result = 0;
	}

	if (tmp.oldlenp && put_user(oldlen, tmp.oldlenp))
		return -EFAULT;

	return result;
}


#ifdef CONFIG_COMPAT

struct compat_sysctl_args {
	compat_uptr_t	name;
	int		nlen;
	compat_uptr_t	oldval;
	compat_uptr_t	oldlenp;
	compat_uptr_t	newval;
	compat_size_t	newlen;
	compat_ulong_t	__unused[4];
};

COMPAT_SYSCALL_DEFINE1(sysctl, struct compat_sysctl_args __user *, args)
{
	struct compat_sysctl_args tmp;
	compat_size_t __user *compat_oldlenp;
	size_t oldlen = 0;
	ssize_t result;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && !tmp.oldlenp)
		return -EFAULT;

	compat_oldlenp = compat_ptr(tmp.oldlenp);
	if (compat_oldlenp && get_user(oldlen, compat_oldlenp))
		return -EFAULT;

	result = do_sysctl(compat_ptr(tmp.name), tmp.nlen,
			   compat_ptr(tmp.oldval), oldlen,
			   compat_ptr(tmp.newval), tmp.newlen);

	if (result >= 0) {
		oldlen = result;
		result = 0;
	}

	if (compat_oldlenp && put_user(oldlen, compat_oldlenp))
		return -EFAULT;

	return result;
}

#endif /* CONFIG_COMPAT */
