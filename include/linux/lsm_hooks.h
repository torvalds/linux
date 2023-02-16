/*
 * Linux Security Module interfaces
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2015 Intel Corporation.
 * Copyright (C) 2015 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2016 Mellanox Techonologies
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_LSM_HOOKS_H
#define __LINUX_LSM_HOOKS_H

#include <linux/security.h>
#include <linux/init.h>
#include <linux/rculist.h>

/**
 * union security_list_options - Linux Security Module hook function list
 *
 * @ptrace_access_check:
 *	Check permission before allowing the current process to trace the
 *	@child process.
 *	Security modules may also want to perform a process tracing check
 *	during an execve in the set_security or apply_creds hooks of
 *	tracing check during an execve in the bprm_set_creds hook of
 *	binprm_security_ops if the process is being traced and its security
 *	attributes would be changed by the execve.
 *	@child contains the task_struct structure for the target process.
 *	@mode contains the PTRACE_MODE flags indicating the form of access.
 *	Return 0 if permission is granted.
 * @ptrace_traceme:
 *	Check that the @parent process has sufficient permission to trace the
 *	current process before allowing the current process to present itself
 *	to the @parent process for tracing.
 *	@parent contains the task_struct structure for debugger process.
 *	Return 0 if permission is granted.
 * @capget:
 *	Get the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  The hook may also perform permission checking to
 *	determine if the current process is allowed to see the capability sets
 *	of the @target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if the capability sets were successfully obtained.
 * @capset:
 *	Set the @effective, @inheritable, and @permitted capability sets for
 *	the current process.
 *	@new contains the new credentials structure for target process.
 *	@old contains the current credentials structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 and update @new if permission is granted.
 * @capable:
 *	Check whether the @tsk process has the @cap capability in the indicated
 *	credentials.
 *	@cred contains the credentials to use.
 *	@ns contains the user namespace we want the capability in.
 *	@cap contains the capability <include/linux/capability.h>.
 *	@opts contains options for the capable check <include/linux/security.h>.
 *	Return 0 if the capability is granted for @tsk.
 * @quotactl:
 *	Check whether the quotactl syscall is allowed for this @sb.
 *	Return 0 if permission is granted.
 * @quota_on:
 *	Check whether QUOTAON is allowed for this @dentry.
 *	Return 0 if permission is granted.
 * @syslog:
 *	Check permission before accessing the kernel message ring or changing
 *	logging to the console.
 *	See the syslog(2) manual page for an explanation of the @type values.
 *	@type contains the SYSLOG_ACTION_* constant from
 *	<include/linux/syslog.h>.
 *	Return 0 if permission is granted.
 * @settime:
 *	Check permission to change the system time.
 *	struct timespec64 is defined in <include/linux/time64.h> and timezone
 *	is defined in <include/linux/time.h>
 *	@ts contains new time.
 *	@tz contains new timezone.
 *	Return 0 if permission is granted.
 * @vm_enough_memory:
 *	Check permissions for allocating a new virtual mapping.
 *	@mm contains the mm struct it is being added to.
 *	@pages contains the number of pages.
 *	Return 0 if permission is granted by the LSM infrastructure to the
 *	caller. If all LSMs return a positive value, __vm_enough_memory() will
 *	be called with cap_sys_admin set. If at least one LSM returns 0 or
 *	negative, __vm_enough_memory() will be called with cap_sys_admin
 *	cleared.
 *
 * @ismaclabel:
 *	Check if the extended attribute specified by @name
 *	represents a MAC label. Returns 1 if name is a MAC
 *	attribute otherwise returns 0.
 *	@name full extended attribute name to check against
 *	LSM as a MAC label.
 *
 * @secid_to_secctx:
 *	Convert secid to security context.  If secdata is NULL the length of
 *	the result will be returned in seclen, but no secdata will be returned.
 *	This does mean that the length could change between calls to check the
 *	length and the next call which actually allocates and returns the
 *	secdata.
 *	@secid contains the security ID.
 *	@secdata contains the pointer that stores the converted security
 *	context.
 *	@seclen pointer which contains the length of the data.
 *	Return 0 on success, error on failure.
 * @secctx_to_secid:
 *	Convert security context to secid.
 *	@secid contains the pointer to the generated security ID.
 *	@secdata contains the security context.
 *	Return 0 on success, error on failure.
 *
 * @release_secctx:
 *	Release the security context.
 *	@secdata contains the security context.
 *	@seclen contains the length of the security context.
 *
 * @inode_invalidate_secctx:
 *	Notify the security module that it must revalidate the security context
 *	of an inode.
 *
 * @inode_notifysecctx:
 *	Notify the security module of what the security context of an inode
 *	should be.  Initializes the incore security context managed by the
 *	security module for this inode.  Example usage:  NFS client invokes
 *	this hook to initialize the security context in its incore inode to the
 *	value provided by the server for the file when the server returned the
 *	file's attributes to the client.
 *	Must be called with inode->i_mutex locked.
 *	@inode we wish to set the security context of.
 *	@ctx contains the string which we wish to set in the inode.
 *	@ctxlen contains the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * @inode_setsecctx:
 *	Change the security context of an inode.  Updates the
 *	incore security context managed by the security module and invokes the
 *	fs code as needed (via __vfs_setxattr_noperm) to update any backing
 *	xattrs that represent the context.  Example usage:  NFS server invokes
 *	this hook to change the security context in its incore inode and on the
 *	backing filesystem to a value provided by the client on a SETATTR
 *	operation.
 *	Must be called with inode->i_mutex locked.
 *	@dentry contains the inode we wish to set the security context of.
 *	@ctx contains the string which we wish to set in the inode.
 *	@ctxlen contains the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * @inode_getsecctx:
 *	On success, returns 0 and fills out @ctx and @ctxlen with the security
 *	context for the given @inode.
 *	@inode we wish to get the security context of.
 *	@ctx is a pointer in which to place the allocated security context.
 *	@ctxlen points to the place to put the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * Security hooks for the general notification queue:
 *
 * @post_notification:
 *	Check to see if a watch notification can be posted to a particular
 *	queue.
 *	@w_cred: The credentials of the whoever set the watch.
 *	@cred: The event-triggerer's credentials.
 *	@n: The notification being posted.
 *	Return 0 if permission is granted.
 *
 * @watch_key:
 *	Check to see if a process is allowed to watch for event notifications
 *	from a key or keyring.
 *	@key: The key to watch.
 *	Return 0 if permission is granted.
 *
 * @locked_down:
 *	Determine whether a kernel feature that potentially enables arbitrary
 *	code execution in kernel space should be permitted.
 *	@what: kernel feature being accessed.
 *	Return 0 if permission is granted.
 *
 * Security hooks for perf events
 *
 * @perf_event_open:
 *	Check whether the @type of perf_event_open syscall is allowed.
 *	Return 0 if permission is granted.
 * @perf_event_alloc:
 *	Allocate and save perf_event security info.
 *	Return 0 on success, error on failure.
 * @perf_event_free:
 *	Release (free) perf_event security info.
 * @perf_event_read:
 *	Read perf_event security info if allowed.
 *	Return 0 if permission is granted.
 * @perf_event_write:
 *	Write perf_event security info if allowed.
 *	Return 0 if permission is granted.
 *
 * Security hooks for io_uring
 *
 * @uring_override_creds:
 *	Check if the current task, executing an io_uring operation, is allowed
 *	to override it's credentials with @new.
 *	@new: the new creds to use.
 *	Return 0 if permission is granted.
 *
 * @uring_sqpoll:
 *	Check whether the current task is allowed to spawn a io_uring polling
 *	thread (IORING_SETUP_SQPOLL).
 *	Return 0 if permission is granted.
 *
 * @uring_cmd:
 *	Check whether the file_operations uring_cmd is allowed to run.
 *	Return 0 if permission is granted.
 *
 */
union security_list_options {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) RET (*NAME)(__VA_ARGS__);
	#include "lsm_hook_defs.h"
	#undef LSM_HOOK
};

struct security_hook_heads {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) struct hlist_head NAME;
	#include "lsm_hook_defs.h"
	#undef LSM_HOOK
} __randomize_layout;

/*
 * Security module hook list structure.
 * For use with generic list macros for common operations.
 */
struct security_hook_list {
	struct hlist_node		list;
	struct hlist_head		*head;
	union security_list_options	hook;
	const char			*lsm;
} __randomize_layout;

/*
 * Security blob size or offset data.
 */
struct lsm_blob_sizes {
	int	lbs_cred;
	int	lbs_file;
	int	lbs_inode;
	int	lbs_superblock;
	int	lbs_ipc;
	int	lbs_msg_msg;
	int	lbs_task;
};

/*
 * LSM_RET_VOID is used as the default value in LSM_HOOK definitions for void
 * LSM hooks (in include/linux/lsm_hook_defs.h).
 */
#define LSM_RET_VOID ((void) 0)

/*
 * Initializing a security_hook_list structure takes
 * up a lot of space in a source file. This macro takes
 * care of the common case and reduces the amount of
 * text involved.
 */
#define LSM_HOOK_INIT(HEAD, HOOK) \
	{ .head = &security_hook_heads.HEAD, .hook = { .HEAD = HOOK } }

extern struct security_hook_heads security_hook_heads;
extern char *lsm_names;

extern void security_add_hooks(struct security_hook_list *hooks, int count,
				const char *lsm);

#define LSM_FLAG_LEGACY_MAJOR	BIT(0)
#define LSM_FLAG_EXCLUSIVE	BIT(1)

enum lsm_order {
	LSM_ORDER_FIRST = -1,	/* This is only for capabilities. */
	LSM_ORDER_MUTABLE = 0,
};

struct lsm_info {
	const char *name;	/* Required. */
	enum lsm_order order;	/* Optional: default is LSM_ORDER_MUTABLE */
	unsigned long flags;	/* Optional: flags describing LSM */
	int *enabled;		/* Optional: controlled by CONFIG_LSM */
	int (*init)(void);	/* Required. */
	struct lsm_blob_sizes *blobs; /* Optional: for blob sharing. */
};

extern struct lsm_info __start_lsm_info[], __end_lsm_info[];
extern struct lsm_info __start_early_lsm_info[], __end_early_lsm_info[];

#define DEFINE_LSM(lsm)							\
	static struct lsm_info __lsm_##lsm				\
		__used __section(".lsm_info.init")			\
		__aligned(sizeof(unsigned long))

#define DEFINE_EARLY_LSM(lsm)						\
	static struct lsm_info __early_lsm_##lsm			\
		__used __section(".early_lsm_info.init")		\
		__aligned(sizeof(unsigned long))

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
/*
 * Assuring the safety of deleting a security module is up to
 * the security module involved. This may entail ordering the
 * module's hook list in a particular way, refusing to disable
 * the module once a policy is loaded or any number of other
 * actions better imagined than described.
 *
 * The name of the configuration option reflects the only module
 * that currently uses the mechanism. Any developer who thinks
 * disabling their module is a good idea needs to be at least as
 * careful as the SELinux team.
 */
static inline void security_delete_hooks(struct security_hook_list *hooks,
						int count)
{
	int i;

	for (i = 0; i < count; i++)
		hlist_del_rcu(&hooks[i].list);
}
#endif /* CONFIG_SECURITY_SELINUX_DISABLE */

/* Currently required to handle SELinux runtime hook disable. */
#ifdef CONFIG_SECURITY_WRITABLE_HOOKS
#define __lsm_ro_after_init
#else
#define __lsm_ro_after_init	__ro_after_init
#endif /* CONFIG_SECURITY_WRITABLE_HOOKS */

extern int lsm_inode_alloc(struct inode *inode);

#endif /* ! __LINUX_LSM_HOOKS_H */
