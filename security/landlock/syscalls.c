// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - System call implementations and user space interfaces
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <asm/current.h>
#include <linux/anon_inodes.h>
#include <linux/build_bug.h>
#include <linux/capability.h>
#include <linux/compiler_types.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/stddef.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/landlock.h>

#include "cred.h"
#include "fs.h"
#include "limits.h"
#include "net.h"
#include "ruleset.h"
#include "setup.h"

static bool is_initialized(void)
{
	if (likely(landlock_initialized))
		return true;

	pr_warn_once(
		"Disabled but requested by user space. "
		"You should enable Landlock at boot time: "
		"https://docs.kernel.org/userspace-api/landlock.html#boot-time-configuration\n");
	return false;
}

/**
 * copy_min_struct_from_user - Safe future-proof argument copying
 *
 * Extend copy_struct_from_user() to check for consistent user buffer.
 *
 * @dst: Kernel space pointer or NULL.
 * @ksize: Actual size of the data pointed to by @dst.
 * @ksize_min: Minimal required size to be copied.
 * @src: User space pointer or NULL.
 * @usize: (Alleged) size of the data pointed to by @src.
 */
static __always_inline int
copy_min_struct_from_user(void *const dst, const size_t ksize,
			  const size_t ksize_min, const void __user *const src,
			  const size_t usize)
{
	/* Checks buffer inconsistencies. */
	BUILD_BUG_ON(!dst);
	if (!src)
		return -EFAULT;

	/* Checks size ranges. */
	BUILD_BUG_ON(ksize <= 0);
	BUILD_BUG_ON(ksize < ksize_min);
	if (usize < ksize_min)
		return -EINVAL;
	if (usize > PAGE_SIZE)
		return -E2BIG;

	/* Copies user buffer and fills with zeros. */
	return copy_struct_from_user(dst, ksize, src, usize);
}

/*
 * This function only contains arithmetic operations with constants, leading to
 * BUILD_BUG_ON().  The related code is evaluated and checked at build time,
 * but it is then ignored thanks to compiler optimizations.
 */
static void build_check_abi(void)
{
	struct landlock_ruleset_attr ruleset_attr;
	struct landlock_path_beneath_attr path_beneath_attr;
	struct landlock_net_port_attr net_port_attr;
	size_t ruleset_size, path_beneath_size, net_port_size;

	/*
	 * For each user space ABI structures, first checks that there is no
	 * hole in them, then checks that all architectures have the same
	 * struct size.
	 */
	ruleset_size = sizeof(ruleset_attr.handled_access_fs);
	ruleset_size += sizeof(ruleset_attr.handled_access_net);
	BUILD_BUG_ON(sizeof(ruleset_attr) != ruleset_size);
	BUILD_BUG_ON(sizeof(ruleset_attr) != 16);

	path_beneath_size = sizeof(path_beneath_attr.allowed_access);
	path_beneath_size += sizeof(path_beneath_attr.parent_fd);
	BUILD_BUG_ON(sizeof(path_beneath_attr) != path_beneath_size);
	BUILD_BUG_ON(sizeof(path_beneath_attr) != 12);

	net_port_size = sizeof(net_port_attr.allowed_access);
	net_port_size += sizeof(net_port_attr.port);
	BUILD_BUG_ON(sizeof(net_port_attr) != net_port_size);
	BUILD_BUG_ON(sizeof(net_port_attr) != 16);
}

/* Ruleset handling */

static int fop_ruleset_release(struct inode *const inode,
			       struct file *const filp)
{
	struct landlock_ruleset *ruleset = filp->private_data;

	landlock_put_ruleset(ruleset);
	return 0;
}

static ssize_t fop_dummy_read(struct file *const filp, char __user *const buf,
			      const size_t size, loff_t *const ppos)
{
	/* Dummy handler to enable FMODE_CAN_READ. */
	return -EINVAL;
}

static ssize_t fop_dummy_write(struct file *const filp,
			       const char __user *const buf, const size_t size,
			       loff_t *const ppos)
{
	/* Dummy handler to enable FMODE_CAN_WRITE. */
	return -EINVAL;
}

/*
 * A ruleset file descriptor enables to build a ruleset by adding (i.e.
 * writing) rule after rule, without relying on the task's context.  This
 * reentrant design is also used in a read way to enforce the ruleset on the
 * current task.
 */
static const struct file_operations ruleset_fops = {
	.release = fop_ruleset_release,
	.read = fop_dummy_read,
	.write = fop_dummy_write,
};

#define LANDLOCK_ABI_VERSION 4

/**
 * sys_landlock_create_ruleset - Create a new ruleset
 *
 * @attr: Pointer to a &struct landlock_ruleset_attr identifying the scope of
 *        the new ruleset.
 * @size: Size of the pointed &struct landlock_ruleset_attr (needed for
 *        backward and forward compatibility).
 * @flags: Supported value: %LANDLOCK_CREATE_RULESET_VERSION.
 *
 * This system call enables to create a new Landlock ruleset, and returns the
 * related file descriptor on success.
 *
 * If @flags is %LANDLOCK_CREATE_RULESET_VERSION and @attr is NULL and @size is
 * 0, then the returned value is the highest supported Landlock ABI version
 * (starting at 1).
 *
 * Possible returned errors are:
 *
 * - %EOPNOTSUPP: Landlock is supported by the kernel but disabled at boot time;
 * - %EINVAL: unknown @flags, or unknown access, or too small @size;
 * - %E2BIG or %EFAULT: @attr or @size inconsistencies;
 * - %ENOMSG: empty &landlock_ruleset_attr.handled_access_fs.
 */
SYSCALL_DEFINE3(landlock_create_ruleset,
		const struct landlock_ruleset_attr __user *const, attr,
		const size_t, size, const __u32, flags)
{
	struct landlock_ruleset_attr ruleset_attr;
	struct landlock_ruleset *ruleset;
	int err, ruleset_fd;

	/* Build-time checks. */
	build_check_abi();

	if (!is_initialized())
		return -EOPNOTSUPP;

	if (flags) {
		if ((flags == LANDLOCK_CREATE_RULESET_VERSION) && !attr &&
		    !size)
			return LANDLOCK_ABI_VERSION;
		return -EINVAL;
	}

	/* Copies raw user space buffer. */
	err = copy_min_struct_from_user(&ruleset_attr, sizeof(ruleset_attr),
					offsetofend(typeof(ruleset_attr),
						    handled_access_fs),
					attr, size);
	if (err)
		return err;

	/* Checks content (and 32-bits cast). */
	if ((ruleset_attr.handled_access_fs | LANDLOCK_MASK_ACCESS_FS) !=
	    LANDLOCK_MASK_ACCESS_FS)
		return -EINVAL;

	/* Checks network content (and 32-bits cast). */
	if ((ruleset_attr.handled_access_net | LANDLOCK_MASK_ACCESS_NET) !=
	    LANDLOCK_MASK_ACCESS_NET)
		return -EINVAL;

	/* Checks arguments and transforms to kernel struct. */
	ruleset = landlock_create_ruleset(ruleset_attr.handled_access_fs,
					  ruleset_attr.handled_access_net);
	if (IS_ERR(ruleset))
		return PTR_ERR(ruleset);

	/* Creates anonymous FD referring to the ruleset. */
	ruleset_fd = anon_inode_getfd("[landlock-ruleset]", &ruleset_fops,
				      ruleset, O_RDWR | O_CLOEXEC);
	if (ruleset_fd < 0)
		landlock_put_ruleset(ruleset);
	return ruleset_fd;
}

/*
 * Returns an owned ruleset from a FD. It is thus needed to call
 * landlock_put_ruleset() on the return value.
 */
static struct landlock_ruleset *get_ruleset_from_fd(const int fd,
						    const fmode_t mode)
{
	struct fd ruleset_f;
	struct landlock_ruleset *ruleset;

	ruleset_f = fdget(fd);
	if (!ruleset_f.file)
		return ERR_PTR(-EBADF);

	/* Checks FD type and access right. */
	if (ruleset_f.file->f_op != &ruleset_fops) {
		ruleset = ERR_PTR(-EBADFD);
		goto out_fdput;
	}
	if (!(ruleset_f.file->f_mode & mode)) {
		ruleset = ERR_PTR(-EPERM);
		goto out_fdput;
	}
	ruleset = ruleset_f.file->private_data;
	if (WARN_ON_ONCE(ruleset->num_layers != 1)) {
		ruleset = ERR_PTR(-EINVAL);
		goto out_fdput;
	}
	landlock_get_ruleset(ruleset);

out_fdput:
	fdput(ruleset_f);
	return ruleset;
}

/* Path handling */

/*
 * @path: Must call put_path(@path) after the call if it succeeded.
 */
static int get_path_from_fd(const s32 fd, struct path *const path)
{
	struct fd f;
	int err = 0;

	BUILD_BUG_ON(!__same_type(
		fd, ((struct landlock_path_beneath_attr *)NULL)->parent_fd));

	/* Handles O_PATH. */
	f = fdget_raw(fd);
	if (!f.file)
		return -EBADF;
	/*
	 * Forbids ruleset FDs, internal filesystems (e.g. nsfs), including
	 * pseudo filesystems that will never be mountable (e.g. sockfs,
	 * pipefs).
	 */
	if ((f.file->f_op == &ruleset_fops) ||
	    (f.file->f_path.mnt->mnt_flags & MNT_INTERNAL) ||
	    (f.file->f_path.dentry->d_sb->s_flags & SB_NOUSER) ||
	    d_is_negative(f.file->f_path.dentry) ||
	    IS_PRIVATE(d_backing_inode(f.file->f_path.dentry))) {
		err = -EBADFD;
		goto out_fdput;
	}
	*path = f.file->f_path;
	path_get(path);

out_fdput:
	fdput(f);
	return err;
}

static int add_rule_path_beneath(struct landlock_ruleset *const ruleset,
				 const void __user *const rule_attr)
{
	struct landlock_path_beneath_attr path_beneath_attr;
	struct path path;
	int res, err;
	access_mask_t mask;

	/* Copies raw user space buffer. */
	res = copy_from_user(&path_beneath_attr, rule_attr,
			     sizeof(path_beneath_attr));
	if (res)
		return -EFAULT;

	/*
	 * Informs about useless rule: empty allowed_access (i.e. deny rules)
	 * are ignored in path walks.
	 */
	if (!path_beneath_attr.allowed_access)
		return -ENOMSG;

	/* Checks that allowed_access matches the @ruleset constraints. */
	mask = landlock_get_raw_fs_access_mask(ruleset, 0);
	if ((path_beneath_attr.allowed_access | mask) != mask)
		return -EINVAL;

	/* Gets and checks the new rule. */
	err = get_path_from_fd(path_beneath_attr.parent_fd, &path);
	if (err)
		return err;

	/* Imports the new rule. */
	err = landlock_append_fs_rule(ruleset, &path,
				      path_beneath_attr.allowed_access);
	path_put(&path);
	return err;
}

static int add_rule_net_port(struct landlock_ruleset *ruleset,
			     const void __user *const rule_attr)
{
	struct landlock_net_port_attr net_port_attr;
	int res;
	access_mask_t mask;

	/* Copies raw user space buffer. */
	res = copy_from_user(&net_port_attr, rule_attr, sizeof(net_port_attr));
	if (res)
		return -EFAULT;

	/*
	 * Informs about useless rule: empty allowed_access (i.e. deny rules)
	 * are ignored by network actions.
	 */
	if (!net_port_attr.allowed_access)
		return -ENOMSG;

	/* Checks that allowed_access matches the @ruleset constraints. */
	mask = landlock_get_net_access_mask(ruleset, 0);
	if ((net_port_attr.allowed_access | mask) != mask)
		return -EINVAL;

	/* Denies inserting a rule with port greater than 65535. */
	if (net_port_attr.port > U16_MAX)
		return -EINVAL;

	/* Imports the new rule. */
	return landlock_append_net_rule(ruleset, net_port_attr.port,
					net_port_attr.allowed_access);
}

/**
 * sys_landlock_add_rule - Add a new rule to a ruleset
 *
 * @ruleset_fd: File descriptor tied to the ruleset that should be extended
 *		with the new rule.
 * @rule_type: Identify the structure type pointed to by @rule_attr:
 *             %LANDLOCK_RULE_PATH_BENEATH or %LANDLOCK_RULE_NET_PORT.
 * @rule_attr: Pointer to a rule (only of type &struct
 *             landlock_path_beneath_attr for now).
 * @flags: Must be 0.
 *
 * This system call enables to define a new rule and add it to an existing
 * ruleset.
 *
 * Possible returned errors are:
 *
 * - %EOPNOTSUPP: Landlock is supported by the kernel but disabled at boot time;
 * - %EAFNOSUPPORT: @rule_type is %LANDLOCK_RULE_NET_PORT but TCP/IP is not
 *   supported by the running kernel;
 * - %EINVAL: @flags is not 0, or inconsistent access in the rule (i.e.
 *   &landlock_path_beneath_attr.allowed_access or
 *   &landlock_net_port_attr.allowed_access is not a subset of the
 *   ruleset handled accesses), or &landlock_net_port_attr.port is
 *   greater than 65535;
 * - %ENOMSG: Empty accesses (e.g. &landlock_path_beneath_attr.allowed_access);
 * - %EBADF: @ruleset_fd is not a file descriptor for the current thread, or a
 *   member of @rule_attr is not a file descriptor as expected;
 * - %EBADFD: @ruleset_fd is not a ruleset file descriptor, or a member of
 *   @rule_attr is not the expected file descriptor type;
 * - %EPERM: @ruleset_fd has no write access to the underlying ruleset;
 * - %EFAULT: @rule_attr inconsistency.
 */
SYSCALL_DEFINE4(landlock_add_rule, const int, ruleset_fd,
		const enum landlock_rule_type, rule_type,
		const void __user *const, rule_attr, const __u32, flags)
{
	struct landlock_ruleset *ruleset;
	int err;

	if (!is_initialized())
		return -EOPNOTSUPP;

	/* No flag for now. */
	if (flags)
		return -EINVAL;

	/* Gets and checks the ruleset. */
	ruleset = get_ruleset_from_fd(ruleset_fd, FMODE_CAN_WRITE);
	if (IS_ERR(ruleset))
		return PTR_ERR(ruleset);

	switch (rule_type) {
	case LANDLOCK_RULE_PATH_BENEATH:
		err = add_rule_path_beneath(ruleset, rule_attr);
		break;
	case LANDLOCK_RULE_NET_PORT:
		err = add_rule_net_port(ruleset, rule_attr);
		break;
	default:
		err = -EINVAL;
		break;
	}
	landlock_put_ruleset(ruleset);
	return err;
}

/* Enforcement */

/**
 * sys_landlock_restrict_self - Enforce a ruleset on the calling thread
 *
 * @ruleset_fd: File descriptor tied to the ruleset to merge with the target.
 * @flags: Must be 0.
 *
 * This system call enables to enforce a Landlock ruleset on the current
 * thread.  Enforcing a ruleset requires that the task has %CAP_SYS_ADMIN in its
 * namespace or is running with no_new_privs.  This avoids scenarios where
 * unprivileged tasks can affect the behavior of privileged children.
 *
 * Possible returned errors are:
 *
 * - %EOPNOTSUPP: Landlock is supported by the kernel but disabled at boot time;
 * - %EINVAL: @flags is not 0.
 * - %EBADF: @ruleset_fd is not a file descriptor for the current thread;
 * - %EBADFD: @ruleset_fd is not a ruleset file descriptor;
 * - %EPERM: @ruleset_fd has no read access to the underlying ruleset, or the
 *   current thread is not running with no_new_privs, or it doesn't have
 *   %CAP_SYS_ADMIN in its namespace.
 * - %E2BIG: The maximum number of stacked rulesets is reached for the current
 *   thread.
 */
SYSCALL_DEFINE2(landlock_restrict_self, const int, ruleset_fd, const __u32,
		flags)
{
	struct landlock_ruleset *new_dom, *ruleset;
	struct cred *new_cred;
	struct landlock_cred_security *new_llcred;
	int err;

	if (!is_initialized())
		return -EOPNOTSUPP;

	/*
	 * Similar checks as for seccomp(2), except that an -EPERM may be
	 * returned.
	 */
	if (!task_no_new_privs(current) &&
	    !ns_capable_noaudit(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	/* No flag for now. */
	if (flags)
		return -EINVAL;

	/* Gets and checks the ruleset. */
	ruleset = get_ruleset_from_fd(ruleset_fd, FMODE_CAN_READ);
	if (IS_ERR(ruleset))
		return PTR_ERR(ruleset);

	/* Prepares new credentials. */
	new_cred = prepare_creds();
	if (!new_cred) {
		err = -ENOMEM;
		goto out_put_ruleset;
	}
	new_llcred = landlock_cred(new_cred);

	/*
	 * There is no possible race condition while copying and manipulating
	 * the current credentials because they are dedicated per thread.
	 */
	new_dom = landlock_merge_ruleset(new_llcred->domain, ruleset);
	if (IS_ERR(new_dom)) {
		err = PTR_ERR(new_dom);
		goto out_put_creds;
	}

	/* Replaces the old (prepared) domain. */
	landlock_put_ruleset(new_llcred->domain);
	new_llcred->domain = new_dom;

	landlock_put_ruleset(ruleset);
	return commit_creds(new_cred);

out_put_creds:
	abort_creds(new_cred);

out_put_ruleset:
	landlock_put_ruleset(ruleset);
	return err;
}
