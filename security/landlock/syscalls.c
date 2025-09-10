// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - System call implementations and user space interfaces
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2021-2025 Microsoft Corporation
 */

#include <asm/current.h>
#include <linux/anon_inodes.h>
#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/capability.h>
#include <linux/cleanup.h>
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
#include "domain.h"
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
	ruleset_size += sizeof(ruleset_attr.scoped);
	BUILD_BUG_ON(sizeof(ruleset_attr) != ruleset_size);
	BUILD_BUG_ON(sizeof(ruleset_attr) != 24);

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

/*
 * The Landlock ABI version should be incremented for each new Landlock-related
 * user space visible change (e.g. Landlock syscalls).  This version should
 * only be incremented once per Linux release, and the date in
 * Documentation/userspace-api/landlock.rst should be updated to reflect the
 * UAPI change.
 */
const int landlock_abi_version = 7;

/**
 * sys_landlock_create_ruleset - Create a new ruleset
 *
 * @attr: Pointer to a &struct landlock_ruleset_attr identifying the scope of
 *        the new ruleset.
 * @size: Size of the pointed &struct landlock_ruleset_attr (needed for
 *        backward and forward compatibility).
 * @flags: Supported values:
 *
 *         - %LANDLOCK_CREATE_RULESET_VERSION
 *         - %LANDLOCK_CREATE_RULESET_ERRATA
 *
 * This system call enables to create a new Landlock ruleset, and returns the
 * related file descriptor on success.
 *
 * If %LANDLOCK_CREATE_RULESET_VERSION or %LANDLOCK_CREATE_RULESET_ERRATA is
 * set, then @attr must be NULL and @size must be 0.
 *
 * Possible returned errors are:
 *
 * - %EOPNOTSUPP: Landlock is supported by the kernel but disabled at boot time;
 * - %EINVAL: unknown @flags, or unknown access, or unknown scope, or too small @size;
 * - %E2BIG: @attr or @size inconsistencies;
 * - %EFAULT: @attr or @size inconsistencies;
 * - %ENOMSG: empty &landlock_ruleset_attr.handled_access_fs.
 *
 * .. kernel-doc:: include/uapi/linux/landlock.h
 *     :identifiers: landlock_create_ruleset_flags
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
		if (attr || size)
			return -EINVAL;

		if (flags == LANDLOCK_CREATE_RULESET_VERSION)
			return landlock_abi_version;

		if (flags == LANDLOCK_CREATE_RULESET_ERRATA)
			return landlock_errata;

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

	/* Checks IPC scoping content (and 32-bits cast). */
	if ((ruleset_attr.scoped | LANDLOCK_MASK_SCOPE) != LANDLOCK_MASK_SCOPE)
		return -EINVAL;

	/* Checks arguments and transforms to kernel struct. */
	ruleset = landlock_create_ruleset(ruleset_attr.handled_access_fs,
					  ruleset_attr.handled_access_net,
					  ruleset_attr.scoped);
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
	CLASS(fd, ruleset_f)(fd);
	struct landlock_ruleset *ruleset;

	if (fd_empty(ruleset_f))
		return ERR_PTR(-EBADF);

	/* Checks FD type and access right. */
	if (fd_file(ruleset_f)->f_op != &ruleset_fops)
		return ERR_PTR(-EBADFD);
	if (!(fd_file(ruleset_f)->f_mode & mode))
		return ERR_PTR(-EPERM);
	ruleset = fd_file(ruleset_f)->private_data;
	if (WARN_ON_ONCE(ruleset->num_layers != 1))
		return ERR_PTR(-EINVAL);
	landlock_get_ruleset(ruleset);
	return ruleset;
}

/* Path handling */

/*
 * @path: Must call put_path(@path) after the call if it succeeded.
 */
static int get_path_from_fd(const s32 fd, struct path *const path)
{
	CLASS(fd_raw, f)(fd);

	BUILD_BUG_ON(!__same_type(
		fd, ((struct landlock_path_beneath_attr *)NULL)->parent_fd));

	if (fd_empty(f))
		return -EBADF;
	/*
	 * Forbids ruleset FDs, internal filesystems (e.g. nsfs), including
	 * pseudo filesystems that will never be mountable (e.g. sockfs,
	 * pipefs).
	 */
	if ((fd_file(f)->f_op == &ruleset_fops) ||
	    (fd_file(f)->f_path.mnt->mnt_flags & MNT_INTERNAL) ||
	    (fd_file(f)->f_path.dentry->d_sb->s_flags & SB_NOUSER) ||
	    IS_PRIVATE(d_backing_inode(fd_file(f)->f_path.dentry)))
		return -EBADFD;

	*path = fd_file(f)->f_path;
	path_get(path);
	return 0;
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
	mask = ruleset->access_masks[0].fs;
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
 * @rule_attr: Pointer to a rule (matching the @rule_type).
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
 * - %EINVAL: @flags is not 0;
 * - %EINVAL: The rule accesses are inconsistent (i.e.
 *   &landlock_path_beneath_attr.allowed_access or
 *   &landlock_net_port_attr.allowed_access is not a subset of the ruleset
 *   handled accesses)
 * - %EINVAL: &landlock_net_port_attr.port is greater than 65535;
 * - %ENOMSG: Empty accesses (e.g. &landlock_path_beneath_attr.allowed_access is
 *   0);
 * - %EBADF: @ruleset_fd is not a file descriptor for the current thread, or a
 *   member of @rule_attr is not a file descriptor as expected;
 * - %EBADFD: @ruleset_fd is not a ruleset file descriptor, or a member of
 *   @rule_attr is not the expected file descriptor type;
 * - %EPERM: @ruleset_fd has no write access to the underlying ruleset;
 * - %EFAULT: @rule_attr was not a valid address.
 */
SYSCALL_DEFINE4(landlock_add_rule, const int, ruleset_fd,
		const enum landlock_rule_type, rule_type,
		const void __user *const, rule_attr, const __u32, flags)
{
	struct landlock_ruleset *ruleset __free(landlock_put_ruleset) = NULL;

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
		return add_rule_path_beneath(ruleset, rule_attr);
	case LANDLOCK_RULE_NET_PORT:
		return add_rule_net_port(ruleset, rule_attr);
	default:
		return -EINVAL;
	}
}

/* Enforcement */

/**
 * sys_landlock_restrict_self - Enforce a ruleset on the calling thread
 *
 * @ruleset_fd: File descriptor tied to the ruleset to merge with the target.
 * @flags: Supported values:
 *
 *         - %LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF
 *         - %LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON
 *         - %LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF
 *
 * This system call enables to enforce a Landlock ruleset on the current
 * thread.  Enforcing a ruleset requires that the task has %CAP_SYS_ADMIN in its
 * namespace or is running with no_new_privs.  This avoids scenarios where
 * unprivileged tasks can affect the behavior of privileged children.
 *
 * Possible returned errors are:
 *
 * - %EOPNOTSUPP: Landlock is supported by the kernel but disabled at boot time;
 * - %EINVAL: @flags contains an unknown bit.
 * - %EBADF: @ruleset_fd is not a file descriptor for the current thread;
 * - %EBADFD: @ruleset_fd is not a ruleset file descriptor;
 * - %EPERM: @ruleset_fd has no read access to the underlying ruleset, or the
 *   current thread is not running with no_new_privs, or it doesn't have
 *   %CAP_SYS_ADMIN in its namespace.
 * - %E2BIG: The maximum number of stacked rulesets is reached for the current
 *   thread.
 *
 * .. kernel-doc:: include/uapi/linux/landlock.h
 *     :identifiers: landlock_restrict_self_flags
 */
SYSCALL_DEFINE2(landlock_restrict_self, const int, ruleset_fd, const __u32,
		flags)
{
	struct landlock_ruleset *new_dom,
		*ruleset __free(landlock_put_ruleset) = NULL;
	struct cred *new_cred;
	struct landlock_cred_security *new_llcred;
	bool __maybe_unused log_same_exec, log_new_exec, log_subdomains,
		prev_log_subdomains;

	if (!is_initialized())
		return -EOPNOTSUPP;

	/*
	 * Similar checks as for seccomp(2), except that an -EPERM may be
	 * returned.
	 */
	if (!task_no_new_privs(current) &&
	    !ns_capable_noaudit(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	if ((flags | LANDLOCK_MASK_RESTRICT_SELF) !=
	    LANDLOCK_MASK_RESTRICT_SELF)
		return -EINVAL;

	/* Translates "off" flag to boolean. */
	log_same_exec = !(flags & LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF);
	/* Translates "on" flag to boolean. */
	log_new_exec = !!(flags & LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON);
	/* Translates "off" flag to boolean. */
	log_subdomains = !(flags & LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF);

	/*
	 * It is allowed to set LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF with
	 * -1 as ruleset_fd, but no other flag must be set.
	 */
	if (!(ruleset_fd == -1 &&
	      flags == LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF)) {
		/* Gets and checks the ruleset. */
		ruleset = get_ruleset_from_fd(ruleset_fd, FMODE_CAN_READ);
		if (IS_ERR(ruleset))
			return PTR_ERR(ruleset);
	}

	/* Prepares new credentials. */
	new_cred = prepare_creds();
	if (!new_cred)
		return -ENOMEM;

	new_llcred = landlock_cred(new_cred);

#ifdef CONFIG_AUDIT
	prev_log_subdomains = !new_llcred->log_subdomains_off;
	new_llcred->log_subdomains_off = !prev_log_subdomains ||
					 !log_subdomains;
#endif /* CONFIG_AUDIT */

	/*
	 * The only case when a ruleset may not be set is if
	 * LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF is set and ruleset_fd is -1.
	 * We could optimize this case by not calling commit_creds() if this flag
	 * was already set, but it is not worth the complexity.
	 */
	if (!ruleset)
		return commit_creds(new_cred);

	/*
	 * There is no possible race condition while copying and manipulating
	 * the current credentials because they are dedicated per thread.
	 */
	new_dom = landlock_merge_ruleset(new_llcred->domain, ruleset);
	if (IS_ERR(new_dom)) {
		abort_creds(new_cred);
		return PTR_ERR(new_dom);
	}

#ifdef CONFIG_AUDIT
	new_dom->hierarchy->log_same_exec = log_same_exec;
	new_dom->hierarchy->log_new_exec = log_new_exec;
	if ((!log_same_exec && !log_new_exec) || !prev_log_subdomains)
		new_dom->hierarchy->log_status = LANDLOCK_LOG_DISABLED;
#endif /* CONFIG_AUDIT */

	/* Replaces the old (prepared) domain. */
	landlock_put_ruleset(new_llcred->domain);
	new_llcred->domain = new_dom;

#ifdef CONFIG_AUDIT
	new_llcred->domain_exec |= BIT(new_dom->num_layers - 1);
#endif /* CONFIG_AUDIT */

	return commit_creds(new_cred);
}
