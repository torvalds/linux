/*
 * Linux Security plug
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
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

#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/msg.h>
#include <linux/sched.h>
#include <linux/key.h>
#include <linux/xfrm.h>
#include <net/flow.h>

struct ctl_table;

/*
 * These functions are in security/capability.c and are used
 * as the default capabilities functions
 */
extern int cap_capable (struct task_struct *tsk, int cap);
extern int cap_settime (struct timespec *ts, struct timezone *tz);
extern int cap_ptrace (struct task_struct *parent, struct task_struct *child);
extern int cap_capget (struct task_struct *target, kernel_cap_t *effective, kernel_cap_t *inheritable, kernel_cap_t *permitted);
extern int cap_capset_check (struct task_struct *target, kernel_cap_t *effective, kernel_cap_t *inheritable, kernel_cap_t *permitted);
extern void cap_capset_set (struct task_struct *target, kernel_cap_t *effective, kernel_cap_t *inheritable, kernel_cap_t *permitted);
extern int cap_bprm_set_security (struct linux_binprm *bprm);
extern void cap_bprm_apply_creds (struct linux_binprm *bprm, int unsafe);
extern int cap_bprm_secureexec(struct linux_binprm *bprm);
extern int cap_inode_setxattr(struct dentry *dentry, char *name, void *value, size_t size, int flags);
extern int cap_inode_removexattr(struct dentry *dentry, char *name);
extern int cap_task_post_setuid (uid_t old_ruid, uid_t old_euid, uid_t old_suid, int flags);
extern void cap_task_reparent_to_init (struct task_struct *p);
extern int cap_syslog (int type);
extern int cap_vm_enough_memory (long pages);

struct msghdr;
struct sk_buff;
struct sock;
struct sockaddr;
struct socket;
struct flowi;
struct dst_entry;
struct xfrm_selector;
struct xfrm_policy;
struct xfrm_state;
struct xfrm_user_sec_ctx;

extern int cap_netlink_send(struct sock *sk, struct sk_buff *skb);
extern int cap_netlink_recv(struct sk_buff *skb, int cap);

/*
 * Values used in the task_security_ops calls
 */
/* setuid or setgid, id0 == uid or gid */
#define LSM_SETID_ID	1

/* setreuid or setregid, id0 == real, id1 == eff */
#define LSM_SETID_RE	2

/* setresuid or setresgid, id0 == real, id1 == eff, uid2 == saved */
#define LSM_SETID_RES	4

/* setfsuid or setfsgid, id0 == fsuid or fsgid */
#define LSM_SETID_FS	8

/* forward declares to avoid warnings */
struct nfsctl_arg;
struct sched_param;
struct swap_info_struct;
struct request_sock;

/* bprm_apply_creds unsafe reasons */
#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_PTRACE_CAP	4

#ifdef CONFIG_SECURITY

/**
 * struct security_operations - main security structure
 *
 * Security hooks for program execution operations.
 *
 * @bprm_alloc_security:
 *	Allocate and attach a security structure to the @bprm->security field.
 *	The security field is initialized to NULL when the bprm structure is
 *	allocated.
 *	@bprm contains the linux_binprm structure to be modified.
 *	Return 0 if operation was successful.
 * @bprm_free_security:
 *	@bprm contains the linux_binprm structure to be modified.
 *	Deallocate and clear the @bprm->security field.
 * @bprm_apply_creds:
 *	Compute and set the security attributes of a process being transformed
 *	by an execve operation based on the old attributes (current->security)
 *	and the information saved in @bprm->security by the set_security hook.
 *	Since this hook function (and its caller) are void, this hook can not
 *	return an error.  However, it can leave the security attributes of the
 *	process unchanged if an access failure occurs at this point.
 *	bprm_apply_creds is called under task_lock.  @unsafe indicates various
 *	reasons why it may be unsafe to change security state.
 *	@bprm contains the linux_binprm structure.
 * @bprm_post_apply_creds:
 *	Runs after bprm_apply_creds with the task_lock dropped, so that
 *	functions which cannot be called safely under the task_lock can
 *	be used.  This hook is a good place to perform state changes on
 *	the process such as closing open file descriptors to which access
 *	is no longer granted if the attributes were changed.
 *	Note that a security module might need to save state between
 *	bprm_apply_creds and bprm_post_apply_creds to store the decision
 *	on whether the process may proceed.
 *	@bprm contains the linux_binprm structure.
 * @bprm_set_security:
 *	Save security information in the bprm->security field, typically based
 *	on information about the bprm->file, for later use by the apply_creds
 *	hook.  This hook may also optionally check permissions (e.g. for
 *	transitions between security domains).
 *	This hook may be called multiple times during a single execve, e.g. for
 *	interpreters.  The hook can tell whether it has already been called by
 *	checking to see if @bprm->security is non-NULL.  If so, then the hook
 *	may decide either to retain the security information saved earlier or
 *	to replace it.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_check_security:
 * 	This hook mediates the point when a search for a binary handler	will
 * 	begin.  It allows a check the @bprm->security value which is set in
 * 	the preceding set_security call.  The primary difference from
 * 	set_security is that the argv list and envp list are reliably
 * 	available in @bprm.  This hook may be called multiple times
 * 	during a single execve; and in each pass set_security is called
 * 	first.
 * 	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_secureexec:
 *      Return a boolean value (0 or 1) indicating whether a "secure exec" 
 *      is required.  The flag is passed in the auxiliary table
 *      on the initial stack to the ELF interpreter to indicate whether libc 
 *      should enable secure mode.
 *      @bprm contains the linux_binprm structure.
 *
 * Security hooks for filesystem operations.
 *
 * @sb_alloc_security:
 *	Allocate and attach a security structure to the sb->s_security field.
 *	The s_security field is initialized to NULL when the structure is
 *	allocated.
 *	@sb contains the super_block structure to be modified.
 *	Return 0 if operation was successful.
 * @sb_free_security:
 *	Deallocate and clear the sb->s_security field.
 *	@sb contains the super_block structure to be modified.
 * @sb_statfs:
 *	Check permission before obtaining filesystem statistics for the @mnt
 *	mountpoint.
 *	@dentry is a handle on the superblock for the filesystem.
 *	Return 0 if permission is granted.  
 * @sb_mount:
 *	Check permission before an object specified by @dev_name is mounted on
 *	the mount point named by @nd.  For an ordinary mount, @dev_name
 *	identifies a device if the file system type requires a device.  For a
 *	remount (@flags & MS_REMOUNT), @dev_name is irrelevant.  For a
 *	loopback/bind mount (@flags & MS_BIND), @dev_name identifies the
 *	pathname of the object being mounted.
 *	@dev_name contains the name for object being mounted.
 *	@nd contains the nameidata structure for mount point object.
 *	@type contains the filesystem type.
 *	@flags contains the mount flags.
 *	@data contains the filesystem-specific data.
 *	Return 0 if permission is granted.
 * @sb_copy_data:
 *	Allow mount option data to be copied prior to parsing by the filesystem,
 *	so that the security module can extract security-specific mount
 *	options cleanly (a filesystem may modify the data e.g. with strsep()).
 *	This also allows the original mount data to be stripped of security-
 *	specific options to avoid having to make filesystems aware of them.
 *	@type the type of filesystem being mounted.
 *	@orig the original mount data copied from userspace.
 *	@copy copied data which will be passed to the security module.
 *	Returns 0 if the copy was successful.
 * @sb_check_sb:
 *	Check permission before the device with superblock @mnt->sb is mounted
 *	on the mount point named by @nd.
 *	@mnt contains the vfsmount for device being mounted.
 *	@nd contains the nameidata object for the mount point.
 *	Return 0 if permission is granted.
 * @sb_umount:
 *	Check permission before the @mnt file system is unmounted.
 *	@mnt contains the mounted file system.
 *	@flags contains the unmount flags, e.g. MNT_FORCE.
 *	Return 0 if permission is granted.
 * @sb_umount_close:
 *	Close any files in the @mnt mounted filesystem that are held open by
 *	the security module.  This hook is called during an umount operation
 *	prior to checking whether the filesystem is still busy.
 *	@mnt contains the mounted filesystem.
 * @sb_umount_busy:
 *	Handle a failed umount of the @mnt mounted filesystem, e.g.  re-opening
 *	any files that were closed by umount_close.  This hook is called during
 *	an umount operation if the umount fails after a call to the
 *	umount_close hook.
 *	@mnt contains the mounted filesystem.
 * @sb_post_remount:
 *	Update the security module's state when a filesystem is remounted.
 *	This hook is only called if the remount was successful.
 *	@mnt contains the mounted file system.
 *	@flags contains the new filesystem flags.
 *	@data contains the filesystem-specific data.
 * @sb_post_mountroot:
 *	Update the security module's state when the root filesystem is mounted.
 *	This hook is only called if the mount was successful.
 * @sb_post_addmount:
 *	Update the security module's state when a filesystem is mounted.
 *	This hook is called any time a mount is successfully grafetd to
 *	the tree.
 *	@mnt contains the mounted filesystem.
 *	@mountpoint_nd contains the nameidata structure for the mount point.
 * @sb_pivotroot:
 *	Check permission before pivoting the root filesystem.
 *	@old_nd contains the nameidata structure for the new location of the current root (put_old).
 *      @new_nd contains the nameidata structure for the new root (new_root).
 *	Return 0 if permission is granted.
 * @sb_post_pivotroot:
 *	Update module state after a successful pivot.
 *	@old_nd contains the nameidata structure for the old root.
 *      @new_nd contains the nameidata structure for the new root.
 *
 * Security hooks for inode operations.
 *
 * @inode_alloc_security:
 *	Allocate and attach a security structure to @inode->i_security.  The
 *	i_security field is initialized to NULL when the inode structure is
 *	allocated.
 *	@inode contains the inode structure.
 *	Return 0 if operation was successful.
 * @inode_free_security:
 *	@inode contains the inode structure.
 *	Deallocate the inode security structure and set @inode->i_security to
 *	NULL. 
 * @inode_init_security:
 * 	Obtain the security attribute name suffix and value to set on a newly
 *	created inode and set up the incore security field for the new inode.
 *	This hook is called by the fs code as part of the inode creation
 *	transaction and provides for atomic labeling of the inode, unlike
 *	the post_create/mkdir/... hooks called by the VFS.  The hook function
 *	is expected to allocate the name and value via kmalloc, with the caller
 *	being responsible for calling kfree after using them.
 *	If the security module does not use security attributes or does
 *	not wish to put a security attribute on this particular inode,
 *	then it should return -EOPNOTSUPP to skip this processing.
 *	@inode contains the inode structure of the newly created inode.
 *	@dir contains the inode structure of the parent directory.
 *	@name will be set to the allocated name suffix (e.g. selinux).
 *	@value will be set to the allocated attribute value.
 *	@len will be set to the length of the value.
 *	Returns 0 if @name and @value have been successfully set,
 *		-EOPNOTSUPP if no security attribute is needed, or
 *		-ENOMEM on memory allocation failure.
 * @inode_create:
 *	Check permission to create a regular file.
 *	@dir contains inode structure of the parent of the new file.
 *	@dentry contains the dentry structure for the file to be created.
 *	@mode contains the file mode of the file to be created.
 *	Return 0 if permission is granted.
 * @inode_link:
 *	Check permission before creating a new hard link to a file.
 *	@old_dentry contains the dentry structure for an existing link to the file.
 *	@dir contains the inode structure of the parent directory of the new link.
 *	@new_dentry contains the dentry structure for the new link.
 *	Return 0 if permission is granted.
 * @inode_unlink:
 *	Check the permission to remove a hard link to a file. 
 *	@dir contains the inode structure of parent directory of the file.
 *	@dentry contains the dentry structure for file to be unlinked.
 *	Return 0 if permission is granted.
 * @inode_symlink:
 *	Check the permission to create a symbolic link to a file.
 *	@dir contains the inode structure of parent directory of the symbolic link.
 *	@dentry contains the dentry structure of the symbolic link.
 *	@old_name contains the pathname of file.
 *	Return 0 if permission is granted.
 * @inode_mkdir:
 *	Check permissions to create a new directory in the existing directory
 *	associated with inode strcture @dir. 
 *	@dir containst the inode structure of parent of the directory to be created.
 *	@dentry contains the dentry structure of new directory.
 *	@mode contains the mode of new directory.
 *	Return 0 if permission is granted.
 * @inode_rmdir:
 *	Check the permission to remove a directory.
 *	@dir contains the inode structure of parent of the directory to be removed.
 *	@dentry contains the dentry structure of directory to be removed.
 *	Return 0 if permission is granted.
 * @inode_mknod:
 *	Check permissions when creating a special file (or a socket or a fifo
 *	file created via the mknod system call).  Note that if mknod operation
 *	is being done for a regular file, then the create hook will be called
 *	and not this hook.
 *	@dir contains the inode structure of parent of the new file.
 *	@dentry contains the dentry structure of the new file.
 *	@mode contains the mode of the new file.
 *	@dev contains the device number.
 *	Return 0 if permission is granted.
 * @inode_rename:
 *	Check for permission to rename a file or directory.
 *	@old_dir contains the inode structure for parent of the old link.
 *	@old_dentry contains the dentry structure of the old link.
 *	@new_dir contains the inode structure for parent of the new link.
 *	@new_dentry contains the dentry structure of the new link.
 *	Return 0 if permission is granted.
 * @inode_readlink:
 *	Check the permission to read the symbolic link.
 *	@dentry contains the dentry structure for the file link.
 *	Return 0 if permission is granted.
 * @inode_follow_link:
 *	Check permission to follow a symbolic link when looking up a pathname.
 *	@dentry contains the dentry structure for the link.
 *	@nd contains the nameidata structure for the parent directory.
 *	Return 0 if permission is granted.
 * @inode_permission:
 *	Check permission before accessing an inode.  This hook is called by the
 *	existing Linux permission function, so a security module can use it to
 *	provide additional checking for existing Linux permission checks.
 *	Notice that this hook is called when a file is opened (as well as many
 *	other operations), whereas the file_security_ops permission hook is
 *	called when the actual read/write operations are performed.
 *	@inode contains the inode structure to check.
 *	@mask contains the permission mask.
 *     @nd contains the nameidata (may be NULL).
 *	Return 0 if permission is granted.
 * @inode_setattr:
 *	Check permission before setting file attributes.  Note that the kernel
 *	call to notify_change is performed from several locations, whenever
 *	file attributes change (such as when a file is truncated, chown/chmod
 *	operations, transferring disk quotas, etc).
 *	@dentry contains the dentry structure for the file.
 *	@attr is the iattr structure containing the new file attributes.
 *	Return 0 if permission is granted.
 * @inode_getattr:
 *	Check permission before obtaining file attributes.
 *	@mnt is the vfsmount where the dentry was looked up
 *	@dentry contains the dentry structure for the file.
 *	Return 0 if permission is granted.
 * @inode_delete:
 *	@inode contains the inode structure for deleted inode.
 *	This hook is called when a deleted inode is released (i.e. an inode
 *	with no hard links has its use count drop to zero).  A security module
 *	can use this hook to release any persistent label associated with the
 *	inode.
 * @inode_setxattr:
 * 	Check permission before setting the extended attributes
 * 	@value identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_post_setxattr:
 * 	Update inode security field after successful setxattr operation.
 * 	@value identified by @name for @dentry.
 * @inode_getxattr:
 * 	Check permission before obtaining the extended attributes
 * 	identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_listxattr:
 * 	Check permission before obtaining the list of extended attribute 
 * 	names for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_removexattr:
 * 	Check permission before removing the extended attribute
 * 	identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_getsecurity:
 *	Copy the extended attribute representation of the security label 
 *	associated with @name for @inode into @buffer.  @buffer may be
 *	NULL to request the size of the buffer required.  @size indicates
 *	the size of @buffer in bytes.  Note that @name is the remainder
 *	of the attribute name after the security. prefix has been removed.
 *	@err is the return value from the preceding fs getxattr call,
 *	and can be used by the security module to determine whether it
 *	should try and canonicalize the attribute value.
 *	Return number of bytes used/required on success.
 * @inode_setsecurity:
 *	Set the security label associated with @name for @inode from the
 *	extended attribute value @value.  @size indicates the size of the
 *	@value in bytes.  @flags may be XATTR_CREATE, XATTR_REPLACE, or 0.
 *	Note that @name is the remainder of the attribute name after the 
 *	security. prefix has been removed.
 *	Return 0 on success.
 * @inode_listsecurity:
 *	Copy the extended attribute names for the security labels
 *	associated with @inode into @buffer.  The maximum size of @buffer
 *	is specified by @buffer_size.  @buffer may be NULL to request
 *	the size of the buffer required.
 *	Returns number of bytes used/required on success.
 *
 * Security hooks for file operations
 *
 * @file_permission:
 *	Check file permissions before accessing an open file.  This hook is
 *	called by various operations that read or write files.  A security
 *	module can use this hook to perform additional checking on these
 *	operations, e.g.  to revalidate permissions on use to support privilege
 *	bracketing or policy changes.  Notice that this hook is used when the
 *	actual read/write operations are performed, whereas the
 *	inode_security_ops hook is called when a file is opened (as well as
 *	many other operations).
 *	Caveat:  Although this hook can be used to revalidate permissions for
 *	various system call operations that read or write files, it does not
 *	address the revalidation of permissions for memory-mapped files.
 *	Security modules must handle this separately if they need such
 *	revalidation.
 *	@file contains the file structure being accessed.
 *	@mask contains the requested permissions.
 *	Return 0 if permission is granted.
 * @file_alloc_security:
 *	Allocate and attach a security structure to the file->f_security field.
 *	The security field is initialized to NULL when the structure is first
 *	created.
 *	@file contains the file structure to secure.
 *	Return 0 if the hook is successful and permission is granted.
 * @file_free_security:
 *	Deallocate and free any security structures stored in file->f_security.
 *	@file contains the file structure being modified.
 * @file_ioctl:
 *	@file contains the file structure.
 *	@cmd contains the operation to perform.
 *	@arg contains the operational arguments.
 *	Check permission for an ioctl operation on @file.  Note that @arg can
 *	sometimes represents a user space pointer; in other cases, it may be a
 *	simple integer value.  When @arg represents a user space pointer, it
 *	should never be used by the security module.
 *	Return 0 if permission is granted.
 * @file_mmap :
 *	Check permissions for a mmap operation.  The @file may be NULL, e.g.
 *	if mapping anonymous memory.
 *	@file contains the file structure for file to map (may be NULL).
 *	@reqprot contains the protection requested by the application.
 *	@prot contains the protection that will be applied by the kernel.
 *	@flags contains the operational flags.
 *	Return 0 if permission is granted.
 * @file_mprotect:
 *	Check permissions before changing memory access permissions.
 *	@vma contains the memory region to modify.
 *	@reqprot contains the protection requested by the application.
 *	@prot contains the protection that will be applied by the kernel.
 *	Return 0 if permission is granted.
 * @file_lock:
 *	Check permission before performing file locking operations.
 *	Note: this hook mediates both flock and fcntl style locks.
 *	@file contains the file structure.
 *	@cmd contains the posix-translated lock operation to perform
 *	(e.g. F_RDLCK, F_WRLCK).
 *	Return 0 if permission is granted.
 * @file_fcntl:
 *	Check permission before allowing the file operation specified by @cmd
 *	from being performed on the file @file.  Note that @arg can sometimes
 *	represents a user space pointer; in other cases, it may be a simple
 *	integer value.  When @arg represents a user space pointer, it should
 *	never be used by the security module.
 *	@file contains the file structure.
 *	@cmd contains the operation to be performed.
 *	@arg contains the operational arguments.
 *	Return 0 if permission is granted.
 * @file_set_fowner:
 *	Save owner security information (typically from current->security) in
 *	file->f_security for later use by the send_sigiotask hook.
 *	@file contains the file structure to update.
 *	Return 0 on success.
 * @file_send_sigiotask:
 *	Check permission for the file owner @fown to send SIGIO or SIGURG to the
 *	process @tsk.  Note that this hook is sometimes called from interrupt.
 *	Note that the fown_struct, @fown, is never outside the context of a
 *	struct file, so the file structure (and associated security information)
 *	can always be obtained:
 *		container_of(fown, struct file, f_owner)
 * 	@tsk contains the structure of task receiving signal.
 *	@fown contains the file owner information.
 *	@sig is the signal that will be sent.  When 0, kernel sends SIGIO.
 *	Return 0 if permission is granted.
 * @file_receive:
 *	This hook allows security modules to control the ability of a process
 *	to receive an open file descriptor via socket IPC.
 *	@file contains the file structure being received.
 *	Return 0 if permission is granted.
 *
 * Security hooks for task operations.
 *
 * @task_create:
 *	Check permission before creating a child process.  See the clone(2)
 *	manual page for definitions of the @clone_flags.
 *	@clone_flags contains the flags indicating what should be shared.
 *	Return 0 if permission is granted.
 * @task_alloc_security:
 *	@p contains the task_struct for child process.
 *	Allocate and attach a security structure to the p->security field. The
 *	security field is initialized to NULL when the task structure is
 *	allocated.
 *	Return 0 if operation was successful.
 * @task_free_security:
 *	@p contains the task_struct for process.
 *	Deallocate and clear the p->security field.
 * @task_setuid:
 *	Check permission before setting one or more of the user identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*uid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a uid.
 *	@id1 contains a uid.
 *	@id2 contains a uid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_post_setuid:
 *	Update the module's state after setting one or more of the user
 *	identity attributes of the current process.  The @flags parameter
 *	indicates which of the set*uid system calls invoked this hook.  If
 *	@flags is LSM_SETID_FS, then @old_ruid is the old fs uid and the other
 *	parameters are not used.
 *	@old_ruid contains the old real uid (or fs uid if LSM_SETID_FS).
 *	@old_euid contains the old effective uid (or -1 if LSM_SETID_FS).
 *	@old_suid contains the old saved uid (or -1 if LSM_SETID_FS).
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 on success.
 * @task_setgid:
 *	Check permission before setting one or more of the group identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*gid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a gid.
 *	@id1 contains a gid.
 *	@id2 contains a gid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_setpgid:
 *	Check permission before setting the process group identifier of the
 *	process @p to @pgid.
 *	@p contains the task_struct for process being modified.
 *	@pgid contains the new pgid.
 *	Return 0 if permission is granted.
 * @task_getpgid:
 *	Check permission before getting the process group identifier of the
 *	process @p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_getsid:
 *	Check permission before getting the session identifier of the process
 *	@p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_getsecid:
 *	Retrieve the security identifier of the process @p.
 *	@p contains the task_struct for the process and place is into @secid.
 * @task_setgroups:
 *	Check permission before setting the supplementary group set of the
 *	current process.
 *	@group_info contains the new group information.
 *	Return 0 if permission is granted.
 * @task_setnice:
 *	Check permission before setting the nice value of @p to @nice.
 *	@p contains the task_struct of process.
 *	@nice contains the new nice value.
 *	Return 0 if permission is granted.
 * @task_setioprio
 *	Check permission before setting the ioprio value of @p to @ioprio.
 *	@p contains the task_struct of process.
 *	@ioprio contains the new ioprio value
 *	Return 0 if permission is granted.
 * @task_getioprio
 *	Check permission before getting the ioprio value of @p.
 *	@p contains the task_struct of process.
 *	Return 0 if permission is granted.
 * @task_setrlimit:
 *	Check permission before setting the resource limits of the current
 *	process for @resource to @new_rlim.  The old resource limit values can
 *	be examined by dereferencing (current->signal->rlim + resource).
 *	@resource contains the resource whose limit is being set.
 *	@new_rlim contains the new limits for @resource.
 *	Return 0 if permission is granted.
 * @task_setscheduler:
 *	Check permission before setting scheduling policy and/or parameters of
 *	process @p based on @policy and @lp.
 *	@p contains the task_struct for process.
 *	@policy contains the scheduling policy.
 *	@lp contains the scheduling parameters.
 *	Return 0 if permission is granted.
 * @task_getscheduler:
 *	Check permission before obtaining scheduling information for process
 *	@p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_movememory
 *	Check permission before moving memory owned by process @p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_kill:
 *	Check permission before sending signal @sig to @p.  @info can be NULL,
 *	the constant 1, or a pointer to a siginfo structure.  If @info is 1 or
 *	SI_FROMKERNEL(info) is true, then the signal should be viewed as coming
 *	from the kernel and should typically be permitted.
 *	SIGIO signals are handled separately by the send_sigiotask hook in
 *	file_security_ops.
 *	@p contains the task_struct for process.
 *	@info contains the signal information.
 *	@sig contains the signal value.
 *	@secid contains the sid of the process where the signal originated
 *	Return 0 if permission is granted.
 * @task_wait:
 *	Check permission before allowing a process to reap a child process @p
 *	and collect its status information.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_prctl:
 *	Check permission before performing a process control operation on the
 *	current process.
 *	@option contains the operation.
 *	@arg2 contains a argument.
 *	@arg3 contains a argument.
 *	@arg4 contains a argument.
 *	@arg5 contains a argument.
 *	Return 0 if permission is granted.
 * @task_reparent_to_init:
 * 	Set the security attributes in @p->security for a kernel thread that
 * 	is being reparented to the init task.
 *	@p contains the task_struct for the kernel thread.
 * @task_to_inode:
 * 	Set the security attributes for an inode based on an associated task's
 * 	security attributes, e.g. for /proc/pid inodes.
 *	@p contains the task_struct for the task.
 *	@inode contains the inode structure for the inode.
 *
 * Security hooks for Netlink messaging.
 *
 * @netlink_send:
 *	Save security information for a netlink message so that permission
 *	checking can be performed when the message is processed.  The security
 *	information can be saved using the eff_cap field of the
 *      netlink_skb_parms structure.  Also may be used to provide fine
 *	grained control over message transmission.
 *	@sk associated sock of task sending the message.,
 *	@skb contains the sk_buff structure for the netlink message.
 *	Return 0 if the information was successfully saved and message
 *	is allowed to be transmitted.
 * @netlink_recv:
 *	Check permission before processing the received netlink message in
 *	@skb.
 *	@skb contains the sk_buff structure for the netlink message.
 *	@cap indicates the capability required
 *	Return 0 if permission is granted.
 *
 * Security hooks for Unix domain networking.
 *
 * @unix_stream_connect:
 *	Check permissions before establishing a Unix domain stream connection
 *	between @sock and @other.
 *	@sock contains the socket structure.
 *	@other contains the peer socket structure.
 *	Return 0 if permission is granted.
 * @unix_may_send:
 *	Check permissions before connecting or sending datagrams from @sock to
 *	@other.
 *	@sock contains the socket structure.
 *	@sock contains the peer socket structure.
 *	Return 0 if permission is granted.
 *
 * The @unix_stream_connect and @unix_may_send hooks were necessary because
 * Linux provides an alternative to the conventional file name space for Unix
 * domain sockets.  Whereas binding and connecting to sockets in the file name
 * space is mediated by the typical file permissions (and caught by the mknod
 * and permission hooks in inode_security_ops), binding and connecting to
 * sockets in the abstract name space is completely unmediated.  Sufficient
 * control of Unix domain sockets in the abstract name space isn't possible
 * using only the socket layer hooks, since we need to know the actual target
 * socket, which is not looked up until we are inside the af_unix code.
 *
 * Security hooks for socket operations.
 *
 * @socket_create:
 *	Check permissions prior to creating a new socket.
 *	@family contains the requested protocol family.
 *	@type contains the requested communications type.
 *	@protocol contains the requested protocol.
 *	@kern set to 1 if a kernel socket.
 *	Return 0 if permission is granted.
 * @socket_post_create:
 *	This hook allows a module to update or allocate a per-socket security
 *	structure. Note that the security field was not added directly to the
 *	socket structure, but rather, the socket security information is stored
 *	in the associated inode.  Typically, the inode alloc_security hook will
 *	allocate and and attach security information to
 *	sock->inode->i_security.  This hook may be used to update the
 *	sock->inode->i_security field with additional information that wasn't
 *	available when the inode was allocated.
 *	@sock contains the newly created socket structure.
 *	@family contains the requested protocol family.
 *	@type contains the requested communications type.
 *	@protocol contains the requested protocol.
 *	@kern set to 1 if a kernel socket.
 * @socket_bind:
 *	Check permission before socket protocol layer bind operation is
 *	performed and the socket @sock is bound to the address specified in the
 *	@address parameter.
 *	@sock contains the socket structure.
 *	@address contains the address to bind to.
 *	@addrlen contains the length of address.
 *	Return 0 if permission is granted.  
 * @socket_connect:
 *	Check permission before socket protocol layer connect operation
 *	attempts to connect socket @sock to a remote address, @address.
 *	@sock contains the socket structure.
 *	@address contains the address of remote endpoint.
 *	@addrlen contains the length of address.
 *	Return 0 if permission is granted.  
 * @socket_listen:
 *	Check permission before socket protocol layer listen operation.
 *	@sock contains the socket structure.
 *	@backlog contains the maximum length for the pending connection queue.
 *	Return 0 if permission is granted.
 * @socket_accept:
 *	Check permission before accepting a new connection.  Note that the new
 *	socket, @newsock, has been created and some information copied to it,
 *	but the accept operation has not actually been performed.
 *	@sock contains the listening socket structure.
 *	@newsock contains the newly created server socket for connection.
 *	Return 0 if permission is granted.
 * @socket_post_accept:
 *	This hook allows a security module to copy security
 *	information into the newly created socket's inode.
 *	@sock contains the listening socket structure.
 *	@newsock contains the newly created server socket for connection.
 * @socket_sendmsg:
 *	Check permission before transmitting a message to another socket.
 *	@sock contains the socket structure.
 *	@msg contains the message to be transmitted.
 *	@size contains the size of message.
 *	Return 0 if permission is granted.
 * @socket_recvmsg:
 *	Check permission before receiving a message from a socket.
 *	@sock contains the socket structure.
 *	@msg contains the message structure.
 *	@size contains the size of message structure.
 *	@flags contains the operational flags.
 *	Return 0 if permission is granted.  
 * @socket_getsockname:
 *	Check permission before the local address (name) of the socket object
 *	@sock is retrieved.
 *	@sock contains the socket structure.
 *	Return 0 if permission is granted.
 * @socket_getpeername:
 *	Check permission before the remote address (name) of a socket object
 *	@sock is retrieved.
 *	@sock contains the socket structure.
 *	Return 0 if permission is granted.
 * @socket_getsockopt:
 *	Check permissions before retrieving the options associated with socket
 *	@sock.
 *	@sock contains the socket structure.
 *	@level contains the protocol level to retrieve option from.
 *	@optname contains the name of option to retrieve.
 *	Return 0 if permission is granted.
 * @socket_setsockopt:
 *	Check permissions before setting the options associated with socket
 *	@sock.
 *	@sock contains the socket structure.
 *	@level contains the protocol level to set options for.
 *	@optname contains the name of the option to set.
 *	Return 0 if permission is granted.  
 * @socket_shutdown:
 *	Checks permission before all or part of a connection on the socket
 *	@sock is shut down.
 *	@sock contains the socket structure.
 *	@how contains the flag indicating how future sends and receives are handled.
 *	Return 0 if permission is granted.
 * @socket_sock_rcv_skb:
 *	Check permissions on incoming network packets.  This hook is distinct
 *	from Netfilter's IP input hooks since it is the first time that the
 *	incoming sk_buff @skb has been associated with a particular socket, @sk.
 *	@sk contains the sock (not socket) associated with the incoming sk_buff.
 *	@skb contains the incoming network data.
 * @socket_getpeersec:
 *	This hook allows the security module to provide peer socket security
 *	state to userspace via getsockopt SO_GETPEERSEC.
 *	@sock is the local socket.
 *	@optval userspace memory where the security state is to be copied.
 *	@optlen userspace int where the module should copy the actual length
 *	of the security state.
 *	@len as input is the maximum length to copy to userspace provided
 *	by the caller.
 *	Return 0 if all is well, otherwise, typical getsockopt return
 *	values.
 * @sk_alloc_security:
 *      Allocate and attach a security structure to the sk->sk_security field,
 *      which is used to copy security attributes between local stream sockets.
 * @sk_free_security:
 *	Deallocate security structure.
 * @sk_clone_security:
 *	Clone/copy security structure.
 * @sk_getsecid:
 *	Retrieve the LSM-specific secid for the sock to enable caching of network
 *	authorizations.
 * @sock_graft:
 *	Sets the socket's isec sid to the sock's sid.
 * @inet_conn_request:
 *	Sets the openreq's sid to socket's sid with MLS portion taken from peer sid.
 * @inet_csk_clone:
 *	Sets the new child socket's sid to the openreq sid.
 * @inet_conn_established:
 *     Sets the connection's peersid to the secmark on skb.
 * @req_classify_flow:
 *	Sets the flow's sid to the openreq sid.
 *
 * Security hooks for XFRM operations.
 *
 * @xfrm_policy_alloc_security:
 *	@xp contains the xfrm_policy being added to Security Policy Database
 *	used by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level policy update program (e.g., setkey).
 *	Allocate a security structure to the xp->security field; the security
 *	field is initialized to NULL when the xfrm_policy is allocated.
 *	Return 0 if operation was successful (memory to allocate, legal context)
 * @xfrm_policy_clone_security:
 *	@old contains an existing xfrm_policy in the SPD.
 *	@new contains a new xfrm_policy being cloned from old.
 *	Allocate a security structure to the new->security field
 *	that contains the information from the old->security field.
 *	Return 0 if operation was successful (memory to allocate).
 * @xfrm_policy_free_security:
 *	@xp contains the xfrm_policy
 *	Deallocate xp->security.
 * @xfrm_policy_delete_security:
 *	@xp contains the xfrm_policy.
 *	Authorize deletion of xp->security.
 * @xfrm_state_alloc_security:
 *	@x contains the xfrm_state being added to the Security Association
 *	Database by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level SA generation program (e.g., setkey or racoon).
 *	@secid contains the secid from which to take the mls portion of the context.
 *	Allocate a security structure to the x->security field; the security
 *	field is initialized to NULL when the xfrm_state is allocated. Set the
 *	context to correspond to either sec_ctx or polsec, with the mls portion
 *	taken from secid in the latter case.
 *	Return 0 if operation was successful (memory to allocate, legal context).
 * @xfrm_state_free_security:
 *	@x contains the xfrm_state.
 *	Deallocate x->security.
 * @xfrm_state_delete_security:
 *	@x contains the xfrm_state.
 *	Authorize deletion of x->security.
 * @xfrm_policy_lookup:
 *	@xp contains the xfrm_policy for which the access control is being
 *	checked.
 *	@fl_secid contains the flow security label that is used to authorize
 *	access to the policy xp.
 *	@dir contains the direction of the flow (input or output).
 *	Check permission when a flow selects a xfrm_policy for processing
 *	XFRMs on a packet.  The hook is called when selecting either a
 *	per-socket policy or a generic xfrm policy.
 *	Return 0 if permission is granted, -ESRCH otherwise, or -errno
 *	on other errors.
 * @xfrm_state_pol_flow_match:
 *	@x contains the state to match.
 *	@xp contains the policy to check for a match.
 *	@fl contains the flow to check for a match.
 *	Return 1 if there is a match.
 * @xfrm_decode_session:
 *	@skb points to skb to decode.
 *	@secid points to the flow key secid to set.
 *	@ckall says if all xfrms used should be checked for same secid.
 *	Return 0 if ckall is zero or all xfrms used have the same secid.
 *
 * Security hooks affecting all Key Management operations
 *
 * @key_alloc:
 *	Permit allocation of a key and assign security data. Note that key does
 *	not have a serial number assigned at this point.
 *	@key points to the key.
 *	@flags is the allocation flags
 *	Return 0 if permission is granted, -ve error otherwise.
 * @key_free:
 *	Notification of destruction; free security data.
 *	@key points to the key.
 *	No return value.
 * @key_permission:
 *	See whether a specific operational right is granted to a process on a
 *      key.
 *	@key_ref refers to the key (key pointer + possession attribute bit).
 *	@context points to the process to provide the context against which to
 *       evaluate the security data on the key.
 *	@perm describes the combination of permissions required of this key.
 *	Return 1 if permission granted, 0 if permission denied and -ve it the
 *      normal permissions model should be effected.
 *
 * Security hooks affecting all System V IPC operations.
 *
 * @ipc_permission:
 *	Check permissions for access to IPC
 *	@ipcp contains the kernel IPC permission structure
 *	@flag contains the desired (requested) permission set
 *	Return 0 if permission is granted.
 *
 * Security hooks for individual messages held in System V IPC message queues
 * @msg_msg_alloc_security:
 *	Allocate and attach a security structure to the msg->security field.
 *	The security field is initialized to NULL when the structure is first
 *	created.
 *	@msg contains the message structure to be modified.
 *	Return 0 if operation was successful and permission is granted.
 * @msg_msg_free_security:
 *	Deallocate the security structure for this message.
 *	@msg contains the message structure to be modified.
 *
 * Security hooks for System V IPC Message Queues
 *
 * @msg_queue_alloc_security:
 *	Allocate and attach a security structure to the
 *	msq->q_perm.security field. The security field is initialized to
 *	NULL when the structure is first created.
 *	@msq contains the message queue structure to be modified.
 *	Return 0 if operation was successful and permission is granted.
 * @msg_queue_free_security:
 *	Deallocate security structure for this message queue.
 *	@msq contains the message queue structure to be modified.
 * @msg_queue_associate:
 *	Check permission when a message queue is requested through the
 *	msgget system call.  This hook is only called when returning the
 *	message queue identifier for an existing message queue, not when a
 *	new message queue is created.
 *	@msq contains the message queue to act upon.
 *	@msqflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgctl:
 *	Check permission when a message control operation specified by @cmd
 *	is to be performed on the message queue @msq.
 *	The @msq may be NULL, e.g. for IPC_INFO or MSG_INFO.
 *	@msq contains the message queue to act upon.  May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.  
 * @msg_queue_msgsnd:
 *	Check permission before a message, @msg, is enqueued on the message
 *	queue, @msq.
 *	@msq contains the message queue to send message to.
 *	@msg contains the message to be enqueued.
 *	@msqflg contains operational flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgrcv:
 *	Check permission before a message, @msg, is removed from the message
 *	queue, @msq.  The @target task structure contains a pointer to the 
 *	process that will be receiving the message (not equal to the current 
 *	process when inline receives are being performed).
 *	@msq contains the message queue to retrieve message from.
 *	@msg contains the message destination.
 *	@target contains the task structure for recipient process.
 *	@type contains the type of message requested.
 *	@mode contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Shared Memory Segments
 *
 * @shm_alloc_security:
 *	Allocate and attach a security structure to the shp->shm_perm.security
 *	field.  The security field is initialized to NULL when the structure is
 *	first created.
 *	@shp contains the shared memory structure to be modified.
 *	Return 0 if operation was successful and permission is granted.
 * @shm_free_security:
 *	Deallocate the security struct for this memory segment.
 *	@shp contains the shared memory structure to be modified.
 * @shm_associate:
 *	Check permission when a shared memory region is requested through the
 *	shmget system call.  This hook is only called when returning the shared
 *	memory region identifier for an existing region, not when a new shared
 *	memory region is created.
 *	@shp contains the shared memory structure to be modified.
 *	@shmflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @shm_shmctl:
 *	Check permission when a shared memory control operation specified by
 *	@cmd is to be performed on the shared memory region @shp.
 *	The @shp may be NULL, e.g. for IPC_INFO or SHM_INFO.
 *	@shp contains shared memory structure to be modified.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @shm_shmat:
 *	Check permissions prior to allowing the shmat system call to attach the
 *	shared memory segment @shp to the data segment of the calling process.
 *	The attaching address is specified by @shmaddr.
 *	@shp contains the shared memory structure to be modified.
 *	@shmaddr contains the address to attach memory region to.
 *	@shmflg contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Semaphores
 *
 * @sem_alloc_security:
 *	Allocate and attach a security structure to the sma->sem_perm.security
 *	field.  The security field is initialized to NULL when the structure is
 *	first created.
 *	@sma contains the semaphore structure
 *	Return 0 if operation was successful and permission is granted.
 * @sem_free_security:
 *	deallocate security struct for this semaphore
 *	@sma contains the semaphore structure.
 * @sem_associate:
 *	Check permission when a semaphore is requested through the semget
 *	system call.  This hook is only called when returning the semaphore
 *	identifier for an existing semaphore, not when a new one must be
 *	created.
 *	@sma contains the semaphore structure.
 *	@semflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @sem_semctl:
 *	Check permission when a semaphore operation specified by @cmd is to be
 *	performed on the semaphore @sma.  The @sma may be NULL, e.g. for 
 *	IPC_INFO or SEM_INFO.
 *	@sma contains the semaphore structure.  May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @sem_semop
 *	Check permissions before performing operations on members of the
 *	semaphore set @sma.  If the @alter flag is nonzero, the semaphore set 
 *      may be modified.
 *	@sma contains the semaphore structure.
 *	@sops contains the operations to perform.
 *	@nsops contains the number of operations to perform.
 *	@alter contains the flag indicating whether changes are to be made.
 *	Return 0 if permission is granted.
 *
 * @ptrace:
 *	Check permission before allowing the @parent process to trace the
 *	@child process.
 *	Security modules may also want to perform a process tracing check
 *	during an execve in the set_security or apply_creds hooks of
 *	binprm_security_ops if the process is being traced and its security
 *	attributes would be changed by the execve.
 *	@parent contains the task_struct structure for parent process.
 *	@child contains the task_struct structure for child process.
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
 * @capset_check:
 *	Check permission before setting the @effective, @inheritable, and
 *	@permitted capability sets for the @target process.
 *	Caveat:  @target is also set to current if a set of processes is
 *	specified (i.e. all processes other than current and init or a
 *	particular process group).  Hence, the capset_set hook may need to
 *	revalidate permission to the actual target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if permission is granted.
 * @capset_set:
 *	Set the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  Since capset_check cannot always check permission
 *	to the real @target process, this hook may also perform permission
 *	checking to determine if the current process is allowed to set the
 *	capability sets of the @target process.  However, this hook has no way
 *	of returning an error due to the structure of the sys_capset code.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 * @capable:
 *	Check whether the @tsk process has the @cap capability.
 *	@tsk contains the task_struct for the process.
 *	@cap contains the capability <include/linux/capability.h>.
 *	Return 0 if the capability is granted for @tsk.
 * @acct:
 *	Check permission before enabling or disabling process accounting.  If
 *	accounting is being enabled, then @file refers to the open file used to
 *	store accounting records.  If accounting is being disabled, then @file
 *	is NULL.
 *	@file contains the file structure for the accounting file (may be NULL).
 *	Return 0 if permission is granted.
 * @sysctl:
 *	Check permission before accessing the @table sysctl variable in the
 *	manner specified by @op.
 *	@table contains the ctl_table structure for the sysctl variable.
 *	@op contains the operation (001 = search, 002 = write, 004 = read).
 *	Return 0 if permission is granted.
 * @syslog:
 *	Check permission before accessing the kernel message ring or changing
 *	logging to the console.
 *	See the syslog(2) manual page for an explanation of the @type values.  
 *	@type contains the type of action.
 *	Return 0 if permission is granted.
 * @settime:
 *	Check permission to change the system time.
 *	struct timespec and timezone are defined in include/linux/time.h
 *	@ts contains new time
 *	@tz contains new timezone
 *	Return 0 if permission is granted.
 * @vm_enough_memory:
 *	Check permissions for allocating a new virtual mapping.
 *      @pages contains the number of pages.
 *	Return 0 if permission is granted.
 *
 * @register_security:
 * 	allow module stacking.
 * 	@name contains the name of the security module being stacked.
 * 	@ops contains a pointer to the struct security_operations of the module to stack.
 * @unregister_security:
 *	remove a stacked module.
 *	@name contains the name of the security module being unstacked.
 *	@ops contains a pointer to the struct security_operations of the module to unstack.
 * 
 * @secid_to_secctx:
 *	Convert secid to security context.
 *	@secid contains the security ID.
 *	@secdata contains the pointer that stores the converted security context.
 *
 * @release_secctx:
 *	Release the security context.
 *	@secdata contains the security context.
 *	@seclen contains the length of the security context.
 *
 * This is the main security structure.
 */
struct security_operations {
	int (*ptrace) (struct task_struct * parent, struct task_struct * child);
	int (*capget) (struct task_struct * target,
		       kernel_cap_t * effective,
		       kernel_cap_t * inheritable, kernel_cap_t * permitted);
	int (*capset_check) (struct task_struct * target,
			     kernel_cap_t * effective,
			     kernel_cap_t * inheritable,
			     kernel_cap_t * permitted);
	void (*capset_set) (struct task_struct * target,
			    kernel_cap_t * effective,
			    kernel_cap_t * inheritable,
			    kernel_cap_t * permitted);
	int (*capable) (struct task_struct * tsk, int cap);
	int (*acct) (struct file * file);
	int (*sysctl) (struct ctl_table * table, int op);
	int (*quotactl) (int cmds, int type, int id, struct super_block * sb);
	int (*quota_on) (struct dentry * dentry);
	int (*syslog) (int type);
	int (*settime) (struct timespec *ts, struct timezone *tz);
	int (*vm_enough_memory) (long pages);

	int (*bprm_alloc_security) (struct linux_binprm * bprm);
	void (*bprm_free_security) (struct linux_binprm * bprm);
	void (*bprm_apply_creds) (struct linux_binprm * bprm, int unsafe);
	void (*bprm_post_apply_creds) (struct linux_binprm * bprm);
	int (*bprm_set_security) (struct linux_binprm * bprm);
	int (*bprm_check_security) (struct linux_binprm * bprm);
	int (*bprm_secureexec) (struct linux_binprm * bprm);

	int (*sb_alloc_security) (struct super_block * sb);
	void (*sb_free_security) (struct super_block * sb);
	int (*sb_copy_data)(struct file_system_type *type,
			    void *orig, void *copy);
	int (*sb_kern_mount) (struct super_block *sb, void *data);
	int (*sb_statfs) (struct dentry *dentry);
	int (*sb_mount) (char *dev_name, struct nameidata * nd,
			 char *type, unsigned long flags, void *data);
	int (*sb_check_sb) (struct vfsmount * mnt, struct nameidata * nd);
	int (*sb_umount) (struct vfsmount * mnt, int flags);
	void (*sb_umount_close) (struct vfsmount * mnt);
	void (*sb_umount_busy) (struct vfsmount * mnt);
	void (*sb_post_remount) (struct vfsmount * mnt,
				 unsigned long flags, void *data);
	void (*sb_post_mountroot) (void);
	void (*sb_post_addmount) (struct vfsmount * mnt,
				  struct nameidata * mountpoint_nd);
	int (*sb_pivotroot) (struct nameidata * old_nd,
			     struct nameidata * new_nd);
	void (*sb_post_pivotroot) (struct nameidata * old_nd,
				   struct nameidata * new_nd);

	int (*inode_alloc_security) (struct inode *inode);	
	void (*inode_free_security) (struct inode *inode);
	int (*inode_init_security) (struct inode *inode, struct inode *dir,
				    char **name, void **value, size_t *len);
	int (*inode_create) (struct inode *dir,
	                     struct dentry *dentry, int mode);
	int (*inode_link) (struct dentry *old_dentry,
	                   struct inode *dir, struct dentry *new_dentry);
	int (*inode_unlink) (struct inode *dir, struct dentry *dentry);
	int (*inode_symlink) (struct inode *dir,
	                      struct dentry *dentry, const char *old_name);
	int (*inode_mkdir) (struct inode *dir, struct dentry *dentry, int mode);
	int (*inode_rmdir) (struct inode *dir, struct dentry *dentry);
	int (*inode_mknod) (struct inode *dir, struct dentry *dentry,
	                    int mode, dev_t dev);
	int (*inode_rename) (struct inode *old_dir, struct dentry *old_dentry,
	                     struct inode *new_dir, struct dentry *new_dentry);
	int (*inode_readlink) (struct dentry *dentry);
	int (*inode_follow_link) (struct dentry *dentry, struct nameidata *nd);
	int (*inode_permission) (struct inode *inode, int mask, struct nameidata *nd);
	int (*inode_setattr)	(struct dentry *dentry, struct iattr *attr);
	int (*inode_getattr) (struct vfsmount *mnt, struct dentry *dentry);
        void (*inode_delete) (struct inode *inode);
	int (*inode_setxattr) (struct dentry *dentry, char *name, void *value,
			       size_t size, int flags);
	void (*inode_post_setxattr) (struct dentry *dentry, char *name, void *value,
				     size_t size, int flags);
	int (*inode_getxattr) (struct dentry *dentry, char *name);
	int (*inode_listxattr) (struct dentry *dentry);
	int (*inode_removexattr) (struct dentry *dentry, char *name);
	const char *(*inode_xattr_getsuffix) (void);
  	int (*inode_getsecurity)(const struct inode *inode, const char *name, void *buffer, size_t size, int err);
  	int (*inode_setsecurity)(struct inode *inode, const char *name, const void *value, size_t size, int flags);
  	int (*inode_listsecurity)(struct inode *inode, char *buffer, size_t buffer_size);

	int (*file_permission) (struct file * file, int mask);
	int (*file_alloc_security) (struct file * file);
	void (*file_free_security) (struct file * file);
	int (*file_ioctl) (struct file * file, unsigned int cmd,
			   unsigned long arg);
	int (*file_mmap) (struct file * file,
			  unsigned long reqprot,
			  unsigned long prot, unsigned long flags);
	int (*file_mprotect) (struct vm_area_struct * vma,
			      unsigned long reqprot,
			      unsigned long prot);
	int (*file_lock) (struct file * file, unsigned int cmd);
	int (*file_fcntl) (struct file * file, unsigned int cmd,
			   unsigned long arg);
	int (*file_set_fowner) (struct file * file);
	int (*file_send_sigiotask) (struct task_struct * tsk,
				    struct fown_struct * fown, int sig);
	int (*file_receive) (struct file * file);

	int (*task_create) (unsigned long clone_flags);
	int (*task_alloc_security) (struct task_struct * p);
	void (*task_free_security) (struct task_struct * p);
	int (*task_setuid) (uid_t id0, uid_t id1, uid_t id2, int flags);
	int (*task_post_setuid) (uid_t old_ruid /* or fsuid */ ,
				 uid_t old_euid, uid_t old_suid, int flags);
	int (*task_setgid) (gid_t id0, gid_t id1, gid_t id2, int flags);
	int (*task_setpgid) (struct task_struct * p, pid_t pgid);
	int (*task_getpgid) (struct task_struct * p);
	int (*task_getsid) (struct task_struct * p);
	void (*task_getsecid) (struct task_struct * p, u32 * secid);
	int (*task_setgroups) (struct group_info *group_info);
	int (*task_setnice) (struct task_struct * p, int nice);
	int (*task_setioprio) (struct task_struct * p, int ioprio);
	int (*task_getioprio) (struct task_struct * p);
	int (*task_setrlimit) (unsigned int resource, struct rlimit * new_rlim);
	int (*task_setscheduler) (struct task_struct * p, int policy,
				  struct sched_param * lp);
	int (*task_getscheduler) (struct task_struct * p);
	int (*task_movememory) (struct task_struct * p);
	int (*task_kill) (struct task_struct * p,
			  struct siginfo * info, int sig, u32 secid);
	int (*task_wait) (struct task_struct * p);
	int (*task_prctl) (int option, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4,
			   unsigned long arg5);
	void (*task_reparent_to_init) (struct task_struct * p);
	void (*task_to_inode)(struct task_struct *p, struct inode *inode);

	int (*ipc_permission) (struct kern_ipc_perm * ipcp, short flag);

	int (*msg_msg_alloc_security) (struct msg_msg * msg);
	void (*msg_msg_free_security) (struct msg_msg * msg);

	int (*msg_queue_alloc_security) (struct msg_queue * msq);
	void (*msg_queue_free_security) (struct msg_queue * msq);
	int (*msg_queue_associate) (struct msg_queue * msq, int msqflg);
	int (*msg_queue_msgctl) (struct msg_queue * msq, int cmd);
	int (*msg_queue_msgsnd) (struct msg_queue * msq,
				 struct msg_msg * msg, int msqflg);
	int (*msg_queue_msgrcv) (struct msg_queue * msq,
				 struct msg_msg * msg,
				 struct task_struct * target,
				 long type, int mode);

	int (*shm_alloc_security) (struct shmid_kernel * shp);
	void (*shm_free_security) (struct shmid_kernel * shp);
	int (*shm_associate) (struct shmid_kernel * shp, int shmflg);
	int (*shm_shmctl) (struct shmid_kernel * shp, int cmd);
	int (*shm_shmat) (struct shmid_kernel * shp, 
			  char __user *shmaddr, int shmflg);

	int (*sem_alloc_security) (struct sem_array * sma);
	void (*sem_free_security) (struct sem_array * sma);
	int (*sem_associate) (struct sem_array * sma, int semflg);
	int (*sem_semctl) (struct sem_array * sma, int cmd);
	int (*sem_semop) (struct sem_array * sma, 
			  struct sembuf * sops, unsigned nsops, int alter);

	int (*netlink_send) (struct sock * sk, struct sk_buff * skb);
	int (*netlink_recv) (struct sk_buff * skb, int cap);

	/* allow module stacking */
	int (*register_security) (const char *name,
	                          struct security_operations *ops);
	int (*unregister_security) (const char *name,
	                            struct security_operations *ops);

	void (*d_instantiate) (struct dentry *dentry, struct inode *inode);

 	int (*getprocattr)(struct task_struct *p, char *name, char **value);
 	int (*setprocattr)(struct task_struct *p, char *name, void *value, size_t size);
	int (*secid_to_secctx)(u32 secid, char **secdata, u32 *seclen);
	void (*release_secctx)(char *secdata, u32 seclen);

#ifdef CONFIG_SECURITY_NETWORK
	int (*unix_stream_connect) (struct socket * sock,
				    struct socket * other, struct sock * newsk);
	int (*unix_may_send) (struct socket * sock, struct socket * other);

	int (*socket_create) (int family, int type, int protocol, int kern);
	int (*socket_post_create) (struct socket * sock, int family,
				   int type, int protocol, int kern);
	int (*socket_bind) (struct socket * sock,
			    struct sockaddr * address, int addrlen);
	int (*socket_connect) (struct socket * sock,
			       struct sockaddr * address, int addrlen);
	int (*socket_listen) (struct socket * sock, int backlog);
	int (*socket_accept) (struct socket * sock, struct socket * newsock);
	void (*socket_post_accept) (struct socket * sock,
				    struct socket * newsock);
	int (*socket_sendmsg) (struct socket * sock,
			       struct msghdr * msg, int size);
	int (*socket_recvmsg) (struct socket * sock,
			       struct msghdr * msg, int size, int flags);
	int (*socket_getsockname) (struct socket * sock);
	int (*socket_getpeername) (struct socket * sock);
	int (*socket_getsockopt) (struct socket * sock, int level, int optname);
	int (*socket_setsockopt) (struct socket * sock, int level, int optname);
	int (*socket_shutdown) (struct socket * sock, int how);
	int (*socket_sock_rcv_skb) (struct sock * sk, struct sk_buff * skb);
	int (*socket_getpeersec_stream) (struct socket *sock, char __user *optval, int __user *optlen, unsigned len);
	int (*socket_getpeersec_dgram) (struct socket *sock, struct sk_buff *skb, u32 *secid);
	int (*sk_alloc_security) (struct sock *sk, int family, gfp_t priority);
	void (*sk_free_security) (struct sock *sk);
	void (*sk_clone_security) (const struct sock *sk, struct sock *newsk);
	void (*sk_getsecid) (struct sock *sk, u32 *secid);
	void (*sock_graft)(struct sock* sk, struct socket *parent);
	int (*inet_conn_request)(struct sock *sk, struct sk_buff *skb,
					struct request_sock *req);
	void (*inet_csk_clone)(struct sock *newsk, const struct request_sock *req);
	void (*inet_conn_established)(struct sock *sk, struct sk_buff *skb);
	void (*req_classify_flow)(const struct request_sock *req, struct flowi *fl);
#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
	int (*xfrm_policy_alloc_security) (struct xfrm_policy *xp,
			struct xfrm_user_sec_ctx *sec_ctx);
	int (*xfrm_policy_clone_security) (struct xfrm_policy *old, struct xfrm_policy *new);
	void (*xfrm_policy_free_security) (struct xfrm_policy *xp);
	int (*xfrm_policy_delete_security) (struct xfrm_policy *xp);
	int (*xfrm_state_alloc_security) (struct xfrm_state *x,
		struct xfrm_user_sec_ctx *sec_ctx,
		u32 secid);
	void (*xfrm_state_free_security) (struct xfrm_state *x);
	int (*xfrm_state_delete_security) (struct xfrm_state *x);
	int (*xfrm_policy_lookup)(struct xfrm_policy *xp, u32 fl_secid, u8 dir);
	int (*xfrm_state_pol_flow_match)(struct xfrm_state *x,
			struct xfrm_policy *xp, struct flowi *fl);
	int (*xfrm_decode_session)(struct sk_buff *skb, u32 *secid, int ckall);
#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

	/* key management security hooks */
#ifdef CONFIG_KEYS
	int (*key_alloc)(struct key *key, struct task_struct *tsk, unsigned long flags);
	void (*key_free)(struct key *key);
	int (*key_permission)(key_ref_t key_ref,
			      struct task_struct *context,
			      key_perm_t perm);

#endif	/* CONFIG_KEYS */

};

/* global variables */
extern struct security_operations *security_ops;

/* inline stuff */
static inline int security_ptrace (struct task_struct * parent, struct task_struct * child)
{
	return security_ops->ptrace (parent, child);
}

static inline int security_capget (struct task_struct *target,
				   kernel_cap_t *effective,
				   kernel_cap_t *inheritable,
				   kernel_cap_t *permitted)
{
	return security_ops->capget (target, effective, inheritable, permitted);
}

static inline int security_capset_check (struct task_struct *target,
					 kernel_cap_t *effective,
					 kernel_cap_t *inheritable,
					 kernel_cap_t *permitted)
{
	return security_ops->capset_check (target, effective, inheritable, permitted);
}

static inline void security_capset_set (struct task_struct *target,
					kernel_cap_t *effective,
					kernel_cap_t *inheritable,
					kernel_cap_t *permitted)
{
	security_ops->capset_set (target, effective, inheritable, permitted);
}

static inline int security_capable(struct task_struct *tsk, int cap)
{
	return security_ops->capable(tsk, cap);
}

static inline int security_acct (struct file *file)
{
	return security_ops->acct (file);
}

static inline int security_sysctl(struct ctl_table *table, int op)
{
	return security_ops->sysctl(table, op);
}

static inline int security_quotactl (int cmds, int type, int id,
				     struct super_block *sb)
{
	return security_ops->quotactl (cmds, type, id, sb);
}

static inline int security_quota_on (struct dentry * dentry)
{
	return security_ops->quota_on (dentry);
}

static inline int security_syslog(int type)
{
	return security_ops->syslog(type);
}

static inline int security_settime(struct timespec *ts, struct timezone *tz)
{
	return security_ops->settime(ts, tz);
}


static inline int security_vm_enough_memory(long pages)
{
	return security_ops->vm_enough_memory(pages);
}

static inline int security_bprm_alloc (struct linux_binprm *bprm)
{
	return security_ops->bprm_alloc_security (bprm);
}
static inline void security_bprm_free (struct linux_binprm *bprm)
{
	security_ops->bprm_free_security (bprm);
}
static inline void security_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{
	security_ops->bprm_apply_creds (bprm, unsafe);
}
static inline void security_bprm_post_apply_creds (struct linux_binprm *bprm)
{
	security_ops->bprm_post_apply_creds (bprm);
}
static inline int security_bprm_set (struct linux_binprm *bprm)
{
	return security_ops->bprm_set_security (bprm);
}

static inline int security_bprm_check (struct linux_binprm *bprm)
{
	return security_ops->bprm_check_security (bprm);
}

static inline int security_bprm_secureexec (struct linux_binprm *bprm)
{
	return security_ops->bprm_secureexec (bprm);
}

static inline int security_sb_alloc (struct super_block *sb)
{
	return security_ops->sb_alloc_security (sb);
}

static inline void security_sb_free (struct super_block *sb)
{
	security_ops->sb_free_security (sb);
}

static inline int security_sb_copy_data (struct file_system_type *type,
					 void *orig, void *copy)
{
	return security_ops->sb_copy_data (type, orig, copy);
}

static inline int security_sb_kern_mount (struct super_block *sb, void *data)
{
	return security_ops->sb_kern_mount (sb, data);
}

static inline int security_sb_statfs (struct dentry *dentry)
{
	return security_ops->sb_statfs (dentry);
}

static inline int security_sb_mount (char *dev_name, struct nameidata *nd,
				    char *type, unsigned long flags,
				    void *data)
{
	return security_ops->sb_mount (dev_name, nd, type, flags, data);
}

static inline int security_sb_check_sb (struct vfsmount *mnt,
					struct nameidata *nd)
{
	return security_ops->sb_check_sb (mnt, nd);
}

static inline int security_sb_umount (struct vfsmount *mnt, int flags)
{
	return security_ops->sb_umount (mnt, flags);
}

static inline void security_sb_umount_close (struct vfsmount *mnt)
{
	security_ops->sb_umount_close (mnt);
}

static inline void security_sb_umount_busy (struct vfsmount *mnt)
{
	security_ops->sb_umount_busy (mnt);
}

static inline void security_sb_post_remount (struct vfsmount *mnt,
					     unsigned long flags, void *data)
{
	security_ops->sb_post_remount (mnt, flags, data);
}

static inline void security_sb_post_mountroot (void)
{
	security_ops->sb_post_mountroot ();
}

static inline void security_sb_post_addmount (struct vfsmount *mnt,
					      struct nameidata *mountpoint_nd)
{
	security_ops->sb_post_addmount (mnt, mountpoint_nd);
}

static inline int security_sb_pivotroot (struct nameidata *old_nd,
					 struct nameidata *new_nd)
{
	return security_ops->sb_pivotroot (old_nd, new_nd);
}

static inline void security_sb_post_pivotroot (struct nameidata *old_nd,
					       struct nameidata *new_nd)
{
	security_ops->sb_post_pivotroot (old_nd, new_nd);
}

static inline int security_inode_alloc (struct inode *inode)
{
	inode->i_security = NULL;
	return security_ops->inode_alloc_security (inode);
}

static inline void security_inode_free (struct inode *inode)
{
	security_ops->inode_free_security (inode);
}

static inline int security_inode_init_security (struct inode *inode,
						struct inode *dir,
						char **name,
						void **value,
						size_t *len)
{
	if (unlikely (IS_PRIVATE (inode)))
		return -EOPNOTSUPP;
	return security_ops->inode_init_security (inode, dir, name, value, len);
}
	
static inline int security_inode_create (struct inode *dir,
					 struct dentry *dentry,
					 int mode)
{
	if (unlikely (IS_PRIVATE (dir)))
		return 0;
	return security_ops->inode_create (dir, dentry, mode);
}

static inline int security_inode_link (struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *new_dentry)
{
	if (unlikely (IS_PRIVATE (old_dentry->d_inode)))
		return 0;
	return security_ops->inode_link (old_dentry, dir, new_dentry);
}

static inline int security_inode_unlink (struct inode *dir,
					 struct dentry *dentry)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_unlink (dir, dentry);
}

static inline int security_inode_symlink (struct inode *dir,
					  struct dentry *dentry,
					  const char *old_name)
{
	if (unlikely (IS_PRIVATE (dir)))
		return 0;
	return security_ops->inode_symlink (dir, dentry, old_name);
}

static inline int security_inode_mkdir (struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	if (unlikely (IS_PRIVATE (dir)))
		return 0;
	return security_ops->inode_mkdir (dir, dentry, mode);
}

static inline int security_inode_rmdir (struct inode *dir,
					struct dentry *dentry)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_rmdir (dir, dentry);
}

static inline int security_inode_mknod (struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	if (unlikely (IS_PRIVATE (dir)))
		return 0;
	return security_ops->inode_mknod (dir, dentry, mode, dev);
}

static inline int security_inode_rename (struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
					 struct dentry *new_dentry)
{
        if (unlikely (IS_PRIVATE (old_dentry->d_inode) ||
            (new_dentry->d_inode && IS_PRIVATE (new_dentry->d_inode))))
		return 0;
	return security_ops->inode_rename (old_dir, old_dentry,
					   new_dir, new_dentry);
}

static inline int security_inode_readlink (struct dentry *dentry)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_readlink (dentry);
}

static inline int security_inode_follow_link (struct dentry *dentry,
					      struct nameidata *nd)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_follow_link (dentry, nd);
}

static inline int security_inode_permission (struct inode *inode, int mask,
					     struct nameidata *nd)
{
	if (unlikely (IS_PRIVATE (inode)))
		return 0;
	return security_ops->inode_permission (inode, mask, nd);
}

static inline int security_inode_setattr (struct dentry *dentry,
					  struct iattr *attr)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_setattr (dentry, attr);
}

static inline int security_inode_getattr (struct vfsmount *mnt,
					  struct dentry *dentry)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_getattr (mnt, dentry);
}

static inline void security_inode_delete (struct inode *inode)
{
	if (unlikely (IS_PRIVATE (inode)))
		return;
	security_ops->inode_delete (inode);
}

static inline int security_inode_setxattr (struct dentry *dentry, char *name,
					   void *value, size_t size, int flags)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_setxattr (dentry, name, value, size, flags);
}

static inline void security_inode_post_setxattr (struct dentry *dentry, char *name,
						void *value, size_t size, int flags)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return;
	security_ops->inode_post_setxattr (dentry, name, value, size, flags);
}

static inline int security_inode_getxattr (struct dentry *dentry, char *name)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_getxattr (dentry, name);
}

static inline int security_inode_listxattr (struct dentry *dentry)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_listxattr (dentry);
}

static inline int security_inode_removexattr (struct dentry *dentry, char *name)
{
	if (unlikely (IS_PRIVATE (dentry->d_inode)))
		return 0;
	return security_ops->inode_removexattr (dentry, name);
}

static inline const char *security_inode_xattr_getsuffix(void)
{
	return security_ops->inode_xattr_getsuffix();
}

static inline int security_inode_getsecurity(const struct inode *inode, const char *name, void *buffer, size_t size, int err)
{
	if (unlikely (IS_PRIVATE (inode)))
		return 0;
	return security_ops->inode_getsecurity(inode, name, buffer, size, err);
}

static inline int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	if (unlikely (IS_PRIVATE (inode)))
		return 0;
	return security_ops->inode_setsecurity(inode, name, value, size, flags);
}

static inline int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	if (unlikely (IS_PRIVATE (inode)))
		return 0;
	return security_ops->inode_listsecurity(inode, buffer, buffer_size);
}

static inline int security_file_permission (struct file *file, int mask)
{
	return security_ops->file_permission (file, mask);
}

static inline int security_file_alloc (struct file *file)
{
	return security_ops->file_alloc_security (file);
}

static inline void security_file_free (struct file *file)
{
	security_ops->file_free_security (file);
}

static inline int security_file_ioctl (struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return security_ops->file_ioctl (file, cmd, arg);
}

static inline int security_file_mmap (struct file *file, unsigned long reqprot,
				      unsigned long prot,
				      unsigned long flags)
{
	return security_ops->file_mmap (file, reqprot, prot, flags);
}

static inline int security_file_mprotect (struct vm_area_struct *vma,
					  unsigned long reqprot,
					  unsigned long prot)
{
	return security_ops->file_mprotect (vma, reqprot, prot);
}

static inline int security_file_lock (struct file *file, unsigned int cmd)
{
	return security_ops->file_lock (file, cmd);
}

static inline int security_file_fcntl (struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return security_ops->file_fcntl (file, cmd, arg);
}

static inline int security_file_set_fowner (struct file *file)
{
	return security_ops->file_set_fowner (file);
}

static inline int security_file_send_sigiotask (struct task_struct *tsk,
						struct fown_struct *fown,
						int sig)
{
	return security_ops->file_send_sigiotask (tsk, fown, sig);
}

static inline int security_file_receive (struct file *file)
{
	return security_ops->file_receive (file);
}

static inline int security_task_create (unsigned long clone_flags)
{
	return security_ops->task_create (clone_flags);
}

static inline int security_task_alloc (struct task_struct *p)
{
	return security_ops->task_alloc_security (p);
}

static inline void security_task_free (struct task_struct *p)
{
	security_ops->task_free_security (p);
}

static inline int security_task_setuid (uid_t id0, uid_t id1, uid_t id2,
					int flags)
{
	return security_ops->task_setuid (id0, id1, id2, flags);
}

static inline int security_task_post_setuid (uid_t old_ruid, uid_t old_euid,
					     uid_t old_suid, int flags)
{
	return security_ops->task_post_setuid (old_ruid, old_euid, old_suid, flags);
}

static inline int security_task_setgid (gid_t id0, gid_t id1, gid_t id2,
					int flags)
{
	return security_ops->task_setgid (id0, id1, id2, flags);
}

static inline int security_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return security_ops->task_setpgid (p, pgid);
}

static inline int security_task_getpgid (struct task_struct *p)
{
	return security_ops->task_getpgid (p);
}

static inline int security_task_getsid (struct task_struct *p)
{
	return security_ops->task_getsid (p);
}

static inline void security_task_getsecid (struct task_struct *p, u32 *secid)
{
	security_ops->task_getsecid (p, secid);
}

static inline int security_task_setgroups (struct group_info *group_info)
{
	return security_ops->task_setgroups (group_info);
}

static inline int security_task_setnice (struct task_struct *p, int nice)
{
	return security_ops->task_setnice (p, nice);
}

static inline int security_task_setioprio (struct task_struct *p, int ioprio)
{
	return security_ops->task_setioprio (p, ioprio);
}

static inline int security_task_getioprio (struct task_struct *p)
{
	return security_ops->task_getioprio (p);
}

static inline int security_task_setrlimit (unsigned int resource,
					   struct rlimit *new_rlim)
{
	return security_ops->task_setrlimit (resource, new_rlim);
}

static inline int security_task_setscheduler (struct task_struct *p,
					      int policy,
					      struct sched_param *lp)
{
	return security_ops->task_setscheduler (p, policy, lp);
}

static inline int security_task_getscheduler (struct task_struct *p)
{
	return security_ops->task_getscheduler (p);
}

static inline int security_task_movememory (struct task_struct *p)
{
	return security_ops->task_movememory (p);
}

static inline int security_task_kill (struct task_struct *p,
				      struct siginfo *info, int sig,
				      u32 secid)
{
	return security_ops->task_kill (p, info, sig, secid);
}

static inline int security_task_wait (struct task_struct *p)
{
	return security_ops->task_wait (p);
}

static inline int security_task_prctl (int option, unsigned long arg2,
				       unsigned long arg3,
				       unsigned long arg4,
				       unsigned long arg5)
{
	return security_ops->task_prctl (option, arg2, arg3, arg4, arg5);
}

static inline void security_task_reparent_to_init (struct task_struct *p)
{
	security_ops->task_reparent_to_init (p);
}

static inline void security_task_to_inode(struct task_struct *p, struct inode *inode)
{
	security_ops->task_to_inode(p, inode);
}

static inline int security_ipc_permission (struct kern_ipc_perm *ipcp,
					   short flag)
{
	return security_ops->ipc_permission (ipcp, flag);
}

static inline int security_msg_msg_alloc (struct msg_msg * msg)
{
	return security_ops->msg_msg_alloc_security (msg);
}

static inline void security_msg_msg_free (struct msg_msg * msg)
{
	security_ops->msg_msg_free_security(msg);
}

static inline int security_msg_queue_alloc (struct msg_queue *msq)
{
	return security_ops->msg_queue_alloc_security (msq);
}

static inline void security_msg_queue_free (struct msg_queue *msq)
{
	security_ops->msg_queue_free_security (msq);
}

static inline int security_msg_queue_associate (struct msg_queue * msq, 
						int msqflg)
{
	return security_ops->msg_queue_associate (msq, msqflg);
}

static inline int security_msg_queue_msgctl (struct msg_queue * msq, int cmd)
{
	return security_ops->msg_queue_msgctl (msq, cmd);
}

static inline int security_msg_queue_msgsnd (struct msg_queue * msq,
					     struct msg_msg * msg, int msqflg)
{
	return security_ops->msg_queue_msgsnd (msq, msg, msqflg);
}

static inline int security_msg_queue_msgrcv (struct msg_queue * msq,
					     struct msg_msg * msg,
					     struct task_struct * target,
					     long type, int mode)
{
	return security_ops->msg_queue_msgrcv (msq, msg, target, type, mode);
}

static inline int security_shm_alloc (struct shmid_kernel *shp)
{
	return security_ops->shm_alloc_security (shp);
}

static inline void security_shm_free (struct shmid_kernel *shp)
{
	security_ops->shm_free_security (shp);
}

static inline int security_shm_associate (struct shmid_kernel * shp, 
					  int shmflg)
{
	return security_ops->shm_associate(shp, shmflg);
}

static inline int security_shm_shmctl (struct shmid_kernel * shp, int cmd)
{
	return security_ops->shm_shmctl (shp, cmd);
}

static inline int security_shm_shmat (struct shmid_kernel * shp, 
				      char __user *shmaddr, int shmflg)
{
	return security_ops->shm_shmat(shp, shmaddr, shmflg);
}

static inline int security_sem_alloc (struct sem_array *sma)
{
	return security_ops->sem_alloc_security (sma);
}

static inline void security_sem_free (struct sem_array *sma)
{
	security_ops->sem_free_security (sma);
}

static inline int security_sem_associate (struct sem_array * sma, int semflg)
{
	return security_ops->sem_associate (sma, semflg);
}

static inline int security_sem_semctl (struct sem_array * sma, int cmd)
{
	return security_ops->sem_semctl(sma, cmd);
}

static inline int security_sem_semop (struct sem_array * sma, 
				      struct sembuf * sops, unsigned nsops, 
				      int alter)
{
	return security_ops->sem_semop(sma, sops, nsops, alter);
}

static inline void security_d_instantiate (struct dentry *dentry, struct inode *inode)
{
	if (unlikely (inode && IS_PRIVATE (inode)))
		return;
	security_ops->d_instantiate (dentry, inode);
}

static inline int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return security_ops->getprocattr(p, name, value);
}

static inline int security_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	return security_ops->setprocattr(p, name, value, size);
}

static inline int security_netlink_send(struct sock *sk, struct sk_buff * skb)
{
	return security_ops->netlink_send(sk, skb);
}

static inline int security_netlink_recv(struct sk_buff * skb, int cap)
{
	return security_ops->netlink_recv(skb, cap);
}

static inline int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_ops->secid_to_secctx(secid, secdata, seclen);
}

static inline void security_release_secctx(char *secdata, u32 seclen)
{
	return security_ops->release_secctx(secdata, seclen);
}

/* prototypes */
extern int security_init	(void);
extern int register_security	(struct security_operations *ops);
extern int unregister_security	(struct security_operations *ops);
extern int mod_reg_security	(const char *name, struct security_operations *ops);
extern int mod_unreg_security	(const char *name, struct security_operations *ops);
extern struct dentry *securityfs_create_file(const char *name, mode_t mode,
					     struct dentry *parent, void *data,
					     const struct file_operations *fops);
extern struct dentry *securityfs_create_dir(const char *name, struct dentry *parent);
extern void securityfs_remove(struct dentry *dentry);


#else /* CONFIG_SECURITY */

/*
 * This is the default capabilities functionality.  Most of these functions
 * are just stubbed out, but a few must call the proper capable code.
 */

static inline int security_init(void)
{
	return 0;
}

static inline int security_ptrace (struct task_struct *parent, struct task_struct * child)
{
	return cap_ptrace (parent, child);
}

static inline int security_capget (struct task_struct *target,
				   kernel_cap_t *effective,
				   kernel_cap_t *inheritable,
				   kernel_cap_t *permitted)
{
	return cap_capget (target, effective, inheritable, permitted);
}

static inline int security_capset_check (struct task_struct *target,
					 kernel_cap_t *effective,
					 kernel_cap_t *inheritable,
					 kernel_cap_t *permitted)
{
	return cap_capset_check (target, effective, inheritable, permitted);
}

static inline void security_capset_set (struct task_struct *target,
					kernel_cap_t *effective,
					kernel_cap_t *inheritable,
					kernel_cap_t *permitted)
{
	cap_capset_set (target, effective, inheritable, permitted);
}

static inline int security_capable(struct task_struct *tsk, int cap)
{
	return cap_capable(tsk, cap);
}

static inline int security_acct (struct file *file)
{
	return 0;
}

static inline int security_sysctl(struct ctl_table *table, int op)
{
	return 0;
}

static inline int security_quotactl (int cmds, int type, int id,
				     struct super_block * sb)
{
	return 0;
}

static inline int security_quota_on (struct dentry * dentry)
{
	return 0;
}

static inline int security_syslog(int type)
{
	return cap_syslog(type);
}

static inline int security_settime(struct timespec *ts, struct timezone *tz)
{
	return cap_settime(ts, tz);
}

static inline int security_vm_enough_memory(long pages)
{
	return cap_vm_enough_memory(pages);
}

static inline int security_bprm_alloc (struct linux_binprm *bprm)
{
	return 0;
}

static inline void security_bprm_free (struct linux_binprm *bprm)
{ }

static inline void security_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{ 
	cap_bprm_apply_creds (bprm, unsafe);
}

static inline void security_bprm_post_apply_creds (struct linux_binprm *bprm)
{
	return;
}

static inline int security_bprm_set (struct linux_binprm *bprm)
{
	return cap_bprm_set_security (bprm);
}

static inline int security_bprm_check (struct linux_binprm *bprm)
{
	return 0;
}

static inline int security_bprm_secureexec (struct linux_binprm *bprm)
{
	return cap_bprm_secureexec(bprm);
}

static inline int security_sb_alloc (struct super_block *sb)
{
	return 0;
}

static inline void security_sb_free (struct super_block *sb)
{ }

static inline int security_sb_copy_data (struct file_system_type *type,
					 void *orig, void *copy)
{
	return 0;
}

static inline int security_sb_kern_mount (struct super_block *sb, void *data)
{
	return 0;
}

static inline int security_sb_statfs (struct dentry *dentry)
{
	return 0;
}

static inline int security_sb_mount (char *dev_name, struct nameidata *nd,
				    char *type, unsigned long flags,
				    void *data)
{
	return 0;
}

static inline int security_sb_check_sb (struct vfsmount *mnt,
					struct nameidata *nd)
{
	return 0;
}

static inline int security_sb_umount (struct vfsmount *mnt, int flags)
{
	return 0;
}

static inline void security_sb_umount_close (struct vfsmount *mnt)
{ }

static inline void security_sb_umount_busy (struct vfsmount *mnt)
{ }

static inline void security_sb_post_remount (struct vfsmount *mnt,
					     unsigned long flags, void *data)
{ }

static inline void security_sb_post_mountroot (void)
{ }

static inline void security_sb_post_addmount (struct vfsmount *mnt,
					      struct nameidata *mountpoint_nd)
{ }

static inline int security_sb_pivotroot (struct nameidata *old_nd,
					 struct nameidata *new_nd)
{
	return 0;
}

static inline void security_sb_post_pivotroot (struct nameidata *old_nd,
					       struct nameidata *new_nd)
{ }

static inline int security_inode_alloc (struct inode *inode)
{
	return 0;
}

static inline void security_inode_free (struct inode *inode)
{ }

static inline int security_inode_init_security (struct inode *inode,
						struct inode *dir,
						char **name,
						void **value,
						size_t *len)
{
	return -EOPNOTSUPP;
}
	
static inline int security_inode_create (struct inode *dir,
					 struct dentry *dentry,
					 int mode)
{
	return 0;
}

static inline int security_inode_link (struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *new_dentry)
{
	return 0;
}

static inline int security_inode_unlink (struct inode *dir,
					 struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_symlink (struct inode *dir,
					  struct dentry *dentry,
					  const char *old_name)
{
	return 0;
}

static inline int security_inode_mkdir (struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	return 0;
}

static inline int security_inode_rmdir (struct inode *dir,
					struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_mknod (struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	return 0;
}

static inline int security_inode_rename (struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
					 struct dentry *new_dentry)
{
	return 0;
}

static inline int security_inode_readlink (struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_follow_link (struct dentry *dentry,
					      struct nameidata *nd)
{
	return 0;
}

static inline int security_inode_permission (struct inode *inode, int mask,
					     struct nameidata *nd)
{
	return 0;
}

static inline int security_inode_setattr (struct dentry *dentry,
					  struct iattr *attr)
{
	return 0;
}

static inline int security_inode_getattr (struct vfsmount *mnt,
					  struct dentry *dentry)
{
	return 0;
}

static inline void security_inode_delete (struct inode *inode)
{ }

static inline int security_inode_setxattr (struct dentry *dentry, char *name,
					   void *value, size_t size, int flags)
{
	return cap_inode_setxattr(dentry, name, value, size, flags);
}

static inline void security_inode_post_setxattr (struct dentry *dentry, char *name,
						 void *value, size_t size, int flags)
{ }

static inline int security_inode_getxattr (struct dentry *dentry, char *name)
{
	return 0;
}

static inline int security_inode_listxattr (struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_removexattr (struct dentry *dentry, char *name)
{
	return cap_inode_removexattr(dentry, name);
}

static inline const char *security_inode_xattr_getsuffix (void)
{
	return NULL ;
}

static inline int security_inode_getsecurity(const struct inode *inode, const char *name, void *buffer, size_t size, int err)
{
	return -EOPNOTSUPP;
}

static inline int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	return 0;
}

static inline int security_file_permission (struct file *file, int mask)
{
	return 0;
}

static inline int security_file_alloc (struct file *file)
{
	return 0;
}

static inline void security_file_free (struct file *file)
{ }

static inline int security_file_ioctl (struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return 0;
}

static inline int security_file_mmap (struct file *file, unsigned long reqprot,
				      unsigned long prot,
				      unsigned long flags)
{
	return 0;
}

static inline int security_file_mprotect (struct vm_area_struct *vma,
					  unsigned long reqprot,
					  unsigned long prot)
{
	return 0;
}

static inline int security_file_lock (struct file *file, unsigned int cmd)
{
	return 0;
}

static inline int security_file_fcntl (struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return 0;
}

static inline int security_file_set_fowner (struct file *file)
{
	return 0;
}

static inline int security_file_send_sigiotask (struct task_struct *tsk,
						struct fown_struct *fown,
						int sig)
{
	return 0;
}

static inline int security_file_receive (struct file *file)
{
	return 0;
}

static inline int security_task_create (unsigned long clone_flags)
{
	return 0;
}

static inline int security_task_alloc (struct task_struct *p)
{
	return 0;
}

static inline void security_task_free (struct task_struct *p)
{ }

static inline int security_task_setuid (uid_t id0, uid_t id1, uid_t id2,
					int flags)
{
	return 0;
}

static inline int security_task_post_setuid (uid_t old_ruid, uid_t old_euid,
					     uid_t old_suid, int flags)
{
	return cap_task_post_setuid (old_ruid, old_euid, old_suid, flags);
}

static inline int security_task_setgid (gid_t id0, gid_t id1, gid_t id2,
					int flags)
{
	return 0;
}

static inline int security_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return 0;
}

static inline int security_task_getpgid (struct task_struct *p)
{
	return 0;
}

static inline int security_task_getsid (struct task_struct *p)
{
	return 0;
}

static inline void security_task_getsecid (struct task_struct *p, u32 *secid)
{ }

static inline int security_task_setgroups (struct group_info *group_info)
{
	return 0;
}

static inline int security_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}

static inline int security_task_setioprio (struct task_struct *p, int ioprio)
{
	return 0;
}

static inline int security_task_getioprio (struct task_struct *p)
{
	return 0;
}

static inline int security_task_setrlimit (unsigned int resource,
					   struct rlimit *new_rlim)
{
	return 0;
}

static inline int security_task_setscheduler (struct task_struct *p,
					      int policy,
					      struct sched_param *lp)
{
	return 0;
}

static inline int security_task_getscheduler (struct task_struct *p)
{
	return 0;
}

static inline int security_task_movememory (struct task_struct *p)
{
	return 0;
}

static inline int security_task_kill (struct task_struct *p,
				      struct siginfo *info, int sig,
				      u32 secid)
{
	return 0;
}

static inline int security_task_wait (struct task_struct *p)
{
	return 0;
}

static inline int security_task_prctl (int option, unsigned long arg2,
				       unsigned long arg3,
				       unsigned long arg4,
				       unsigned long arg5)
{
	return 0;
}

static inline void security_task_reparent_to_init (struct task_struct *p)
{
	cap_task_reparent_to_init (p);
}

static inline void security_task_to_inode(struct task_struct *p, struct inode *inode)
{ }

static inline int security_ipc_permission (struct kern_ipc_perm *ipcp,
					   short flag)
{
	return 0;
}

static inline int security_msg_msg_alloc (struct msg_msg * msg)
{
	return 0;
}

static inline void security_msg_msg_free (struct msg_msg * msg)
{ }

static inline int security_msg_queue_alloc (struct msg_queue *msq)
{
	return 0;
}

static inline void security_msg_queue_free (struct msg_queue *msq)
{ }

static inline int security_msg_queue_associate (struct msg_queue * msq, 
						int msqflg)
{
	return 0;
}

static inline int security_msg_queue_msgctl (struct msg_queue * msq, int cmd)
{
	return 0;
}

static inline int security_msg_queue_msgsnd (struct msg_queue * msq,
					     struct msg_msg * msg, int msqflg)
{
	return 0;
}

static inline int security_msg_queue_msgrcv (struct msg_queue * msq,
					     struct msg_msg * msg,
					     struct task_struct * target,
					     long type, int mode)
{
	return 0;
}

static inline int security_shm_alloc (struct shmid_kernel *shp)
{
	return 0;
}

static inline void security_shm_free (struct shmid_kernel *shp)
{ }

static inline int security_shm_associate (struct shmid_kernel * shp, 
					  int shmflg)
{
	return 0;
}

static inline int security_shm_shmctl (struct shmid_kernel * shp, int cmd)
{
	return 0;
}

static inline int security_shm_shmat (struct shmid_kernel * shp, 
				      char __user *shmaddr, int shmflg)
{
	return 0;
}

static inline int security_sem_alloc (struct sem_array *sma)
{
	return 0;
}

static inline void security_sem_free (struct sem_array *sma)
{ }

static inline int security_sem_associate (struct sem_array * sma, int semflg)
{
	return 0;
}

static inline int security_sem_semctl (struct sem_array * sma, int cmd)
{
	return 0;
}

static inline int security_sem_semop (struct sem_array * sma, 
				      struct sembuf * sops, unsigned nsops, 
				      int alter)
{
	return 0;
}

static inline void security_d_instantiate (struct dentry *dentry, struct inode *inode)
{ }

static inline int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return -EINVAL;
}

static inline int security_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	return -EINVAL;
}

static inline int security_netlink_send (struct sock *sk, struct sk_buff *skb)
{
	return cap_netlink_send (sk, skb);
}

static inline int security_netlink_recv (struct sk_buff *skb, int cap)
{
	return cap_netlink_recv (skb, cap);
}

static inline struct dentry *securityfs_create_dir(const char *name,
					struct dentry *parent)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *securityfs_create_file(const char *name,
						mode_t mode,
						struct dentry *parent,
						void *data,
						struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}

static inline void securityfs_remove(struct dentry *dentry)
{
}

static inline int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return -EOPNOTSUPP;
}

static inline void security_release_secctx(char *secdata, u32 seclen)
{
}
#endif	/* CONFIG_SECURITY */

#ifdef CONFIG_SECURITY_NETWORK
static inline int security_unix_stream_connect(struct socket * sock,
					       struct socket * other, 
					       struct sock * newsk)
{
	return security_ops->unix_stream_connect(sock, other, newsk);
}


static inline int security_unix_may_send(struct socket * sock, 
					 struct socket * other)
{
	return security_ops->unix_may_send(sock, other);
}

static inline int security_socket_create (int family, int type,
					  int protocol, int kern)
{
	return security_ops->socket_create(family, type, protocol, kern);
}

static inline int security_socket_post_create(struct socket * sock,
					      int family,
					      int type,
					      int protocol, int kern)
{
	return security_ops->socket_post_create(sock, family, type,
						protocol, kern);
}

static inline int security_socket_bind(struct socket * sock, 
				       struct sockaddr * address, 
				       int addrlen)
{
	return security_ops->socket_bind(sock, address, addrlen);
}

static inline int security_socket_connect(struct socket * sock, 
					  struct sockaddr * address, 
					  int addrlen)
{
	return security_ops->socket_connect(sock, address, addrlen);
}

static inline int security_socket_listen(struct socket * sock, int backlog)
{
	return security_ops->socket_listen(sock, backlog);
}

static inline int security_socket_accept(struct socket * sock, 
					 struct socket * newsock)
{
	return security_ops->socket_accept(sock, newsock);
}

static inline void security_socket_post_accept(struct socket * sock, 
					       struct socket * newsock)
{
	security_ops->socket_post_accept(sock, newsock);
}

static inline int security_socket_sendmsg(struct socket * sock, 
					  struct msghdr * msg, int size)
{
	return security_ops->socket_sendmsg(sock, msg, size);
}

static inline int security_socket_recvmsg(struct socket * sock, 
					  struct msghdr * msg, int size, 
					  int flags)
{
	return security_ops->socket_recvmsg(sock, msg, size, flags);
}

static inline int security_socket_getsockname(struct socket * sock)
{
	return security_ops->socket_getsockname(sock);
}

static inline int security_socket_getpeername(struct socket * sock)
{
	return security_ops->socket_getpeername(sock);
}

static inline int security_socket_getsockopt(struct socket * sock, 
					     int level, int optname)
{
	return security_ops->socket_getsockopt(sock, level, optname);
}

static inline int security_socket_setsockopt(struct socket * sock, 
					     int level, int optname)
{
	return security_ops->socket_setsockopt(sock, level, optname);
}

static inline int security_socket_shutdown(struct socket * sock, int how)
{
	return security_ops->socket_shutdown(sock, how);
}

static inline int security_sock_rcv_skb (struct sock * sk, 
					 struct sk_buff * skb)
{
	return security_ops->socket_sock_rcv_skb (sk, skb);
}

static inline int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
						    int __user *optlen, unsigned len)
{
	return security_ops->socket_getpeersec_stream(sock, optval, optlen, len);
}

static inline int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return security_ops->socket_getpeersec_dgram(sock, skb, secid);
}

static inline int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return security_ops->sk_alloc_security(sk, family, priority);
}

static inline void security_sk_free(struct sock *sk)
{
	return security_ops->sk_free_security(sk);
}

static inline void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
	return security_ops->sk_clone_security(sk, newsk);
}

static inline void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
	security_ops->sk_getsecid(sk, &fl->secid);
}

static inline void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
	security_ops->req_classify_flow(req, fl);
}

static inline void security_sock_graft(struct sock* sk, struct socket *parent)
{
	security_ops->sock_graft(sk, parent);
}

static inline int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return security_ops->inet_conn_request(sk, skb, req);
}

static inline void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
	security_ops->inet_csk_clone(newsk, req);
}

static inline void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
	security_ops->inet_conn_established(sk, skb);
}
#else	/* CONFIG_SECURITY_NETWORK */
static inline int security_unix_stream_connect(struct socket * sock,
					       struct socket * other,
					       struct sock * newsk)
{
	return 0;
}

static inline int security_unix_may_send(struct socket * sock, 
					 struct socket * other)
{
	return 0;
}

static inline int security_socket_create (int family, int type,
					  int protocol, int kern)
{
	return 0;
}

static inline int security_socket_post_create(struct socket * sock,
					      int family,
					      int type,
					      int protocol, int kern)
{
	return 0;
}

static inline int security_socket_bind(struct socket * sock, 
				       struct sockaddr * address, 
				       int addrlen)
{
	return 0;
}

static inline int security_socket_connect(struct socket * sock, 
					  struct sockaddr * address, 
					  int addrlen)
{
	return 0;
}

static inline int security_socket_listen(struct socket * sock, int backlog)
{
	return 0;
}

static inline int security_socket_accept(struct socket * sock, 
					 struct socket * newsock)
{
	return 0;
}

static inline void security_socket_post_accept(struct socket * sock, 
					       struct socket * newsock)
{
}

static inline int security_socket_sendmsg(struct socket * sock, 
					  struct msghdr * msg, int size)
{
	return 0;
}

static inline int security_socket_recvmsg(struct socket * sock, 
					  struct msghdr * msg, int size, 
					  int flags)
{
	return 0;
}

static inline int security_socket_getsockname(struct socket * sock)
{
	return 0;
}

static inline int security_socket_getpeername(struct socket * sock)
{
	return 0;
}

static inline int security_socket_getsockopt(struct socket * sock, 
					     int level, int optname)
{
	return 0;
}

static inline int security_socket_setsockopt(struct socket * sock, 
					     int level, int optname)
{
	return 0;
}

static inline int security_socket_shutdown(struct socket * sock, int how)
{
	return 0;
}
static inline int security_sock_rcv_skb (struct sock * sk, 
					 struct sk_buff * skb)
{
	return 0;
}

static inline int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
						    int __user *optlen, unsigned len)
{
	return -ENOPROTOOPT;
}

static inline int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return -ENOPROTOOPT;
}

static inline int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return 0;
}

static inline void security_sk_free(struct sock *sk)
{
}

static inline void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
}

static inline void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
}

static inline void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
}

static inline void security_sock_graft(struct sock* sk, struct socket *parent)
{
}

static inline int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return 0;
}

static inline void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
}

static inline void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
}
#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
static inline int security_xfrm_policy_alloc(struct xfrm_policy *xp, struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_policy_alloc_security(xp, sec_ctx);
}

static inline int security_xfrm_policy_clone(struct xfrm_policy *old, struct xfrm_policy *new)
{
	return security_ops->xfrm_policy_clone_security(old, new);
}

static inline void security_xfrm_policy_free(struct xfrm_policy *xp)
{
	security_ops->xfrm_policy_free_security(xp);
}

static inline int security_xfrm_policy_delete(struct xfrm_policy *xp)
{
	return security_ops->xfrm_policy_delete_security(xp);
}

static inline int security_xfrm_state_alloc(struct xfrm_state *x,
			struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_state_alloc_security(x, sec_ctx, 0);
}

static inline int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				struct xfrm_sec_ctx *polsec, u32 secid)
{
	if (!polsec)
		return 0;
	/*
	 * We want the context to be taken from secid which is usually
	 * from the sock.
	 */
	return security_ops->xfrm_state_alloc_security(x, NULL, secid);
}

static inline int security_xfrm_state_delete(struct xfrm_state *x)
{
	return security_ops->xfrm_state_delete_security(x);
}

static inline void security_xfrm_state_free(struct xfrm_state *x)
{
	security_ops->xfrm_state_free_security(x);
}

static inline int security_xfrm_policy_lookup(struct xfrm_policy *xp, u32 fl_secid, u8 dir)
{
	return security_ops->xfrm_policy_lookup(xp, fl_secid, dir);
}

static inline int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
			struct xfrm_policy *xp, struct flowi *fl)
{
	return security_ops->xfrm_state_pol_flow_match(x, xp, fl);
}

static inline int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return security_ops->xfrm_decode_session(skb, secid, 1);
}

static inline void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
	int rc = security_ops->xfrm_decode_session(skb, &fl->secid, 0);

	BUG_ON(rc);
}
#else	/* CONFIG_SECURITY_NETWORK_XFRM */
static inline int security_xfrm_policy_alloc(struct xfrm_policy *xp, struct xfrm_user_sec_ctx *sec_ctx)
{
	return 0;
}

static inline int security_xfrm_policy_clone(struct xfrm_policy *old, struct xfrm_policy *new)
{
	return 0;
}

static inline void security_xfrm_policy_free(struct xfrm_policy *xp)
{
}

static inline int security_xfrm_policy_delete(struct xfrm_policy *xp)
{
	return 0;
}

static inline int security_xfrm_state_alloc(struct xfrm_state *x,
					struct xfrm_user_sec_ctx *sec_ctx)
{
	return 0;
}

static inline int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
					struct xfrm_sec_ctx *polsec, u32 secid)
{
	return 0;
}

static inline void security_xfrm_state_free(struct xfrm_state *x)
{
}

static inline int security_xfrm_state_delete(struct xfrm_state *x)
{
	return 0;
}

static inline int security_xfrm_policy_lookup(struct xfrm_policy *xp, u32 fl_secid, u8 dir)
{
	return 0;
}

static inline int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
			struct xfrm_policy *xp, struct flowi *fl)
{
	return 1;
}

static inline int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return 0;
}

static inline void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
}

#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_KEYS
#ifdef CONFIG_SECURITY
static inline int security_key_alloc(struct key *key,
				     struct task_struct *tsk,
				     unsigned long flags)
{
	return security_ops->key_alloc(key, tsk, flags);
}

static inline void security_key_free(struct key *key)
{
	security_ops->key_free(key);
}

static inline int security_key_permission(key_ref_t key_ref,
					  struct task_struct *context,
					  key_perm_t perm)
{
	return security_ops->key_permission(key_ref, context, perm);
}

#else

static inline int security_key_alloc(struct key *key,
				     struct task_struct *tsk,
				     unsigned long flags)
{
	return 0;
}

static inline void security_key_free(struct key *key)
{
}

static inline int security_key_permission(key_ref_t key_ref,
					  struct task_struct *context,
					  key_perm_t perm)
{
	return 0;
}

#endif
#endif /* CONFIG_KEYS */

#endif /* ! __LINUX_SECURITY_H */

