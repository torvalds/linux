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
 * Security hooks for program execution operations.
 *
 * @bprm_creds_for_exec:
 *	If the setup in prepare_exec_creds did not setup @bprm->cred->security
 *	properly for executing @bprm->file, update the LSM's portion of
 *	@bprm->cred->security to be what commit_creds needs to install for the
 *	new program.  This hook may also optionally check permissions
 *	(e.g. for transitions between security domains).
 *	The hook must set @bprm->secureexec to 1 if AT_SECURE should be set to
 *	request libc enable secure mode.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_creds_from_file:
 *	If @file is setpcap, suid, sgid or otherwise marked to change
 *	privilege upon exec, update @bprm->cred to reflect that change.
 *	This is called after finding the binary that will be executed.
 *	without an interpreter.  This ensures that the credentials will not
 *	be derived from a script that the binary will need to reopen, which
 *	when reopend may end up being a completely different file.  This
 *	hook may also optionally check permissions (e.g. for transitions
 *	between security domains).
 *	The hook must set @bprm->secureexec to 1 if AT_SECURE should be set to
 *	request libc enable secure mode.
 *	The hook must add to @bprm->per_clear any personality flags that
 * 	should be cleared from current->personality.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_check_security:
 *	This hook mediates the point when a search for a binary handler will
 *	begin.  It allows a check against the @bprm->cred->security value
 *	which was set in the preceding creds_for_exec call.  The argv list and
 *	envp list are reliably available in @bprm.  This hook may be called
 *	multiple times during a single execve.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_committing_creds:
 *	Prepare to install the new security attributes of a process being
 *	transformed by an execve operation, based on the old credentials
 *	pointed to by @current->cred and the information set in @bprm->cred by
 *	the bprm_creds_for_exec hook.  @bprm points to the linux_binprm
 *	structure.  This hook is a good place to perform state changes on the
 *	process such as closing open file descriptors to which access will no
 *	longer be granted when the attributes are changed.  This is called
 *	immediately before commit_creds().
 * @bprm_committed_creds:
 *	Tidy up after the installation of the new security attributes of a
 *	process being transformed by an execve operation.  The new credentials
 *	have, by this point, been set to @current->cred.  @bprm points to the
 *	linux_binprm structure.  This hook is a good place to perform state
 *	changes on the process such as clearing out non-inheritable signal
 *	state.  This is called immediately after commit_creds().
 *
 * Security hooks for mount using fs_context.
 *	[See also Documentation/filesystems/mount_api.rst]
 *
 * @fs_context_dup:
 *	Allocate and attach a security structure to sc->security.  This pointer
 *	is initialised to NULL by the caller.
 *	@fc indicates the new filesystem context.
 *	@src_fc indicates the original filesystem context.
 * @fs_context_parse_param:
 *	Userspace provided a parameter to configure a superblock.  The LSM may
 *	reject it with an error and may use it for itself, in which case it
 *	should return 0; otherwise it should return -ENOPARAM to pass it on to
 *	the filesystem.
 *	@fc indicates the filesystem context.
 *	@param The parameter
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
 * @sb_free_mnt_opts:
 * 	Free memory associated with @mnt_ops.
 * @sb_eat_lsm_opts:
 * 	Eat (scan @orig options) and save them in @mnt_opts.
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
 *	@path contains the path for mount point object.
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
 *	@orig the original mount data copied from userspace.
 *	@copy copied data which will be passed to the security module.
 *	Returns 0 if the copy was successful.
 * @sb_remount:
 *	Extracts security system specific mount options and verifies no changes
 *	are being made to those options.
 *	@sb superblock being remounted
 *	@data contains the filesystem-specific data.
 *	Return 0 if permission is granted.
 * @sb_kern_mount:
 * 	Mount this @sb if allowed by permissions.
 * @sb_show_options:
 * 	Show (print on @m) mount options for this @sb.
 * @sb_umount:
 *	Check permission before the @mnt file system is unmounted.
 *	@mnt contains the mounted file system.
 *	@flags contains the unmount flags, e.g. MNT_FORCE.
 *	Return 0 if permission is granted.
 * @sb_pivotroot:
 *	Check permission before pivoting the root filesystem.
 *	@old_path contains the path for the new location of the
 *	current root (put_old).
 *	@new_path contains the path for the new root (new_root).
 *	Return 0 if permission is granted.
 * @sb_set_mnt_opts:
 *	Set the security relevant mount options used for a superblock
 *	@sb the superblock to set security mount options for
 *	@opts binary data structure containing all lsm mount data
 * @sb_clone_mnt_opts:
 *	Copy all security options from a given superblock to another
 *	@oldsb old superblock which contain information to clone
 *	@newsb new superblock which needs filled in
 * @sb_add_mnt_opt:
 * 	Add one mount @option to @mnt_opts.
 * @sb_parse_opts_str:
 *	Parse a string of security data filling in the opts structure
 *	@options string containing all mount options known by the LSM
 *	@opts binary data structure usable by the LSM
 * @move_mount:
 *	Check permission before a mount is moved.
 *	@from_path indicates the mount that is going to be moved.
 *	@to_path indicates the mountpoint that will be mounted upon.
 * @dentry_init_security:
 *	Compute a context for a dentry as the inode is not yet available
 *	since NFSv4 has no label backed by an EA anyway.
 *	@dentry dentry to use in calculating the context.
 *	@mode mode used to determine resource type.
 *	@name name of the last path component used to create file
 *	@ctx pointer to place the pointer to the resulting context in.
 *	@ctxlen point to place the length of the resulting context.
 * @dentry_create_files_as:
 *	Compute a context for a dentry as the inode is not yet available
 *	and set that context in passed in creds so that new files are
 *	created using that context. Context is calculated using the
 *	passed in creds and not the creds of the caller.
 *	@dentry dentry to use in calculating the context.
 *	@mode mode used to determine resource type.
 *	@name name of the last path component used to create file
 *	@old creds which should be used for context calculation
 *	@new creds to modify
 *
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
 *	Obtain the security attribute name suffix and value to set on a newly
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
 *	@qstr contains the last path component of the new object
 *	@name will be set to the allocated name suffix (e.g. selinux).
 *	@value will be set to the allocated attribute value.
 *	@len will be set to the length of the value.
 *	Returns 0 if @name and @value have been successfully set,
 *	-EOPNOTSUPP if no security attribute is needed, or
 *	-ENOMEM on memory allocation failure.
 * @inode_init_security_anon:
 *      Set up the incore security field for the new anonymous inode
 *      and return whether the inode creation is permitted by the security
 *      module or not.
 *      @inode contains the inode structure
 *      @name name of the anonymous inode class
 *      @context_inode optional related inode
 *	Returns 0 on success, -EACCES if the security module denies the
 *	creation of this inode, or another -errno upon other errors.
 * @inode_create:
 *	Check permission to create a regular file.
 *	@dir contains inode structure of the parent of the new file.
 *	@dentry contains the dentry structure for the file to be created.
 *	@mode contains the file mode of the file to be created.
 *	Return 0 if permission is granted.
 * @inode_link:
 *	Check permission before creating a new hard link to a file.
 *	@old_dentry contains the dentry structure for an existing
 *	link to the file.
 *	@dir contains the inode structure of the parent directory
 *	of the new link.
 *	@new_dentry contains the dentry structure for the new link.
 *	Return 0 if permission is granted.
 * @path_link:
 *	Check permission before creating a new hard link to a file.
 *	@old_dentry contains the dentry structure for an existing link
 *	to the file.
 *	@new_dir contains the path structure of the parent directory of
 *	the new link.
 *	@new_dentry contains the dentry structure for the new link.
 *	Return 0 if permission is granted.
 * @inode_unlink:
 *	Check the permission to remove a hard link to a file.
 *	@dir contains the inode structure of parent directory of the file.
 *	@dentry contains the dentry structure for file to be unlinked.
 *	Return 0 if permission is granted.
 * @path_unlink:
 *	Check the permission to remove a hard link to a file.
 *	@dir contains the path structure of parent directory of the file.
 *	@dentry contains the dentry structure for file to be unlinked.
 *	Return 0 if permission is granted.
 * @inode_symlink:
 *	Check the permission to create a symbolic link to a file.
 *	@dir contains the inode structure of parent directory of
 *	the symbolic link.
 *	@dentry contains the dentry structure of the symbolic link.
 *	@old_name contains the pathname of file.
 *	Return 0 if permission is granted.
 * @path_symlink:
 *	Check the permission to create a symbolic link to a file.
 *	@dir contains the path structure of parent directory of
 *	the symbolic link.
 *	@dentry contains the dentry structure of the symbolic link.
 *	@old_name contains the pathname of file.
 *	Return 0 if permission is granted.
 * @inode_mkdir:
 *	Check permissions to create a new directory in the existing directory
 *	associated with inode structure @dir.
 *	@dir contains the inode structure of parent of the directory
 *	to be created.
 *	@dentry contains the dentry structure of new directory.
 *	@mode contains the mode of new directory.
 *	Return 0 if permission is granted.
 * @path_mkdir:
 *	Check permissions to create a new directory in the existing directory
 *	associated with path structure @path.
 *	@dir contains the path structure of parent of the directory
 *	to be created.
 *	@dentry contains the dentry structure of new directory.
 *	@mode contains the mode of new directory.
 *	Return 0 if permission is granted.
 * @inode_rmdir:
 *	Check the permission to remove a directory.
 *	@dir contains the inode structure of parent of the directory
 *	to be removed.
 *	@dentry contains the dentry structure of directory to be removed.
 *	Return 0 if permission is granted.
 * @path_rmdir:
 *	Check the permission to remove a directory.
 *	@dir contains the path structure of parent of the directory to be
 *	removed.
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
 * @path_mknod:
 *	Check permissions when creating a file. Note that this hook is called
 *	even if mknod operation is being done for a regular file.
 *	@dir contains the path structure of parent of the new file.
 *	@dentry contains the dentry structure of the new file.
 *	@mode contains the mode of the new file.
 *	@dev contains the undecoded device number. Use new_decode_dev() to get
 *	the decoded device number.
 *	Return 0 if permission is granted.
 * @inode_rename:
 *	Check for permission to rename a file or directory.
 *	@old_dir contains the inode structure for parent of the old link.
 *	@old_dentry contains the dentry structure of the old link.
 *	@new_dir contains the inode structure for parent of the new link.
 *	@new_dentry contains the dentry structure of the new link.
 *	Return 0 if permission is granted.
 * @path_rename:
 *	Check for permission to rename a file or directory.
 *	@old_dir contains the path structure for parent of the old link.
 *	@old_dentry contains the dentry structure of the old link.
 *	@new_dir contains the path structure for parent of the new link.
 *	@new_dentry contains the dentry structure of the new link.
 *	Return 0 if permission is granted.
 * @path_chmod:
 *	Check for permission to change a mode of the file @path. The new
 *	mode is specified in @mode.
 *	@path contains the path structure of the file to change the mode.
 *	@mode contains the new DAC's permission, which is a bitmask of
 *	constants from <include/uapi/linux/stat.h>
 *	Return 0 if permission is granted.
 * @path_chown:
 *	Check for permission to change owner/group of a file or directory.
 *	@path contains the path structure.
 *	@uid contains new owner's ID.
 *	@gid contains new group's ID.
 *	Return 0 if permission is granted.
 * @path_chroot:
 *	Check for permission to change root directory.
 *	@path contains the path structure.
 *	Return 0 if permission is granted.
 * @path_notify:
 *	Check permissions before setting a watch on events as defined by @mask,
 *	on an object at @path, whose type is defined by @obj_type.
 * @inode_readlink:
 *	Check the permission to read the symbolic link.
 *	@dentry contains the dentry structure for the file link.
 *	Return 0 if permission is granted.
 * @inode_follow_link:
 *	Check permission to follow a symbolic link when looking up a pathname.
 *	@dentry contains the dentry structure for the link.
 *	@inode contains the inode, which itself is not stable in RCU-walk
 *	@rcu indicates whether we are in RCU-walk mode.
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
 *	Return 0 if permission is granted.
 * @inode_setattr:
 *	Check permission before setting file attributes.  Note that the kernel
 *	call to notify_change is performed from several locations, whenever
 *	file attributes change (such as when a file is truncated, chown/chmod
 *	operations, transferring disk quotas, etc).
 *	@dentry contains the dentry structure for the file.
 *	@attr is the iattr structure containing the new file attributes.
 *	Return 0 if permission is granted.
 * @path_truncate:
 *	Check permission before truncating a file.
 *	@path contains the path structure for the file.
 *	Return 0 if permission is granted.
 * @inode_getattr:
 *	Check permission before obtaining file attributes.
 *	@path contains the path structure for the file.
 *	Return 0 if permission is granted.
 * @inode_setxattr:
 *	Check permission before setting the extended attributes
 *	@value identified by @name for @dentry.
 *	Return 0 if permission is granted.
 * @inode_post_setxattr:
 *	Update inode security field after successful setxattr operation.
 *	@value identified by @name for @dentry.
 * @inode_getxattr:
 *	Check permission before obtaining the extended attributes
 *	identified by @name for @dentry.
 *	Return 0 if permission is granted.
 * @inode_listxattr:
 *	Check permission before obtaining the list of extended attribute
 *	names for @dentry.
 *	Return 0 if permission is granted.
 * @inode_removexattr:
 *	Check permission before removing the extended attribute
 *	identified by @name for @dentry.
 *	Return 0 if permission is granted.
 * @inode_getsecurity:
 *	Retrieve a copy of the extended attribute representation of the
 *	security label associated with @name for @inode via @buffer.  Note that
 *	@name is the remainder of the attribute name after the security prefix
 *	has been removed. @alloc is used to specify of the call should return a
 *	value via the buffer or just the value length Return size of buffer on
 *	success.
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
 * @inode_need_killpriv:
 *	Called when an inode has been changed.
 *	@dentry is the dentry being changed.
 *	Return <0 on error to abort the inode change operation.
 *	Return 0 if inode_killpriv does not need to be called.
 *	Return >0 if inode_killpriv does need to be called.
 * @inode_killpriv:
 *	The setuid bit is being removed.  Remove similar security labels.
 *	Called with the dentry->d_inode->i_mutex held.
 *	@mnt_userns: user namespace of the mount
 *	@dentry is the dentry being changed.
 *	Return 0 on success.  If error is returned, then the operation
 *	causing setuid bit removal is failed.
 * @inode_getsecid:
 *	Get the secid associated with the node.
 *	@inode contains a pointer to the inode.
 *	@secid contains a pointer to the location where result will be saved.
 *	In case of failure, @secid will be set to zero.
 * @inode_copy_up:
 *	A file is about to be copied up from lower layer to upper layer of
 *	overlay filesystem. Security module can prepare a set of new creds
 *	and modify as need be and return new creds. Caller will switch to
 *	new creds temporarily to create new file and release newly allocated
 *	creds.
 *	@src indicates the union dentry of file that is being copied up.
 *	@new pointer to pointer to return newly allocated creds.
 *	Returns 0 on success or a negative error code on error.
 * @inode_copy_up_xattr:
 *	Filter the xattrs being copied up when a unioned file is copied
 *	up from a lower layer to the union/overlay layer.
 *	@name indicates the name of the xattr.
 *	Returns 0 to accept the xattr, 1 to discard the xattr, -EOPNOTSUPP if
 *	security module does not know about attribute or a negative error code
 *	to abort the copy up. Note that the caller is responsible for reading
 *	and writing the xattrs as this hook is merely a filter.
 * @d_instantiate:
 * 	Fill in @inode security information for a @dentry if allowed.
 * @getprocattr:
 * 	Read attribute @name for process @p and store it into @value if allowed.
 * @setprocattr:
 * 	Write (set) attribute @name to @value, size @size if allowed.
 *
 * Security hooks for kernfs node operations
 *
 * @kernfs_init_security:
 *	Initialize the security context of a newly created kernfs node based
 *	on its own and its parent's attributes.
 *
 *	@kn_dir the parent kernfs node
 *	@kn the new child kernfs node
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
 *	Check permission for an ioctl operation on @file.  Note that @arg
 *	sometimes represents a user space pointer; in other cases, it may be a
 *	simple integer value.  When @arg represents a user space pointer, it
 *	should never be used by the security module.
 *	Return 0 if permission is granted.
 * @mmap_addr :
 *	Check permissions for a mmap operation at @addr.
 *	@addr contains virtual address that will be used for the operation.
 *	Return 0 if permission is granted.
 * @mmap_file :
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
 *	Note the hook mediates both flock and fcntl style locks.
 *	@file contains the file structure.
 *	@cmd contains the posix-translated lock operation to perform
 *	(e.g. F_RDLCK, F_WRLCK).
 *	Return 0 if permission is granted.
 * @file_fcntl:
 *	Check permission before allowing the file operation specified by @cmd
 *	from being performed on the file @file.  Note that @arg sometimes
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
 *	can always be obtained: container_of(fown, struct file, f_owner)
 *	@tsk contains the structure of task receiving signal.
 *	@fown contains the file owner information.
 *	@sig is the signal that will be sent.  When 0, kernel sends SIGIO.
 *	Return 0 if permission is granted.
 * @file_receive:
 *	This hook allows security modules to control the ability of a process
 *	to receive an open file descriptor via socket IPC.
 *	@file contains the file structure being received.
 *	Return 0 if permission is granted.
 * @file_open:
 *	Save open-time permission checking state for later use upon
 *	file_permission, and recheck access if anything has changed
 *	since inode_permission.
 *
 * Security hooks for task operations.
 *
 * @task_alloc:
 *	@task task being allocated.
 *	@clone_flags contains the flags indicating what should be shared.
 *	Handle allocation of task-related resources.
 *	Returns a zero on success, negative values on failure.
 * @task_free:
 *	@task task about to be freed.
 *	Handle release of task-related resources. (Note that this can be called
 *	from interrupt context.)
 * @cred_alloc_blank:
 *	@cred points to the credentials.
 *	@gfp indicates the atomicity of any memory allocations.
 *	Only allocate sufficient memory and attach to @cred such that
 *	cred_transfer() will not get ENOMEM.
 * @cred_free:
 *	@cred points to the credentials.
 *	Deallocate and clear the cred->security field in a set of credentials.
 * @cred_prepare:
 *	@new points to the new credentials.
 *	@old points to the original credentials.
 *	@gfp indicates the atomicity of any memory allocations.
 *	Prepare a new set of credentials by copying the data from the old set.
 * @cred_transfer:
 *	@new points to the new credentials.
 *	@old points to the original credentials.
 *	Transfer data from original creds to new creds
 * @cred_getsecid:
 *	Retrieve the security identifier of the cred structure @c
 *	@c contains the credentials, secid will be placed into @secid.
 *	In case of failure, @secid will be set to zero.
 * @kernel_act_as:
 *	Set the credentials for a kernel service to act as (subjective context).
 *	@new points to the credentials to be modified.
 *	@secid specifies the security ID to be set
 *	The current task must be the one that nominated @secid.
 *	Return 0 if successful.
 * @kernel_create_files_as:
 *	Set the file creation context in a set of credentials to be the same as
 *	the objective context of the specified inode.
 *	@new points to the credentials to be modified.
 *	@inode points to the inode to use as a reference.
 *	The current task must be the one that nominated @inode.
 *	Return 0 if successful.
 * @kernel_module_request:
 *	Ability to trigger the kernel to automatically upcall to userspace for
 *	userspace to load a kernel module with the given name.
 *	@kmod_name name of the module requested by the kernel
 *	Return 0 if successful.
 * @kernel_load_data:
 *	Load data provided by userspace.
 *	@id kernel load data identifier
 *	@contents if a subsequent @kernel_post_load_data will be called.
 *	Return 0 if permission is granted.
 * @kernel_post_load_data:
 *	Load data provided by a non-file source (usually userspace buffer).
 *	@buf pointer to buffer containing the data contents.
 *	@size length of the data contents.
 *	@id kernel load data identifier
 *	@description a text description of what was loaded, @id-specific
 *	Return 0 if permission is granted.
 *	This must be paired with a prior @kernel_load_data call that had
 *	@contents set to true.
 * @kernel_read_file:
 *	Read a file specified by userspace.
 *	@file contains the file structure pointing to the file being read
 *	by the kernel.
 *	@id kernel read file identifier
 *	@contents if a subsequent @kernel_post_read_file will be called.
 *	Return 0 if permission is granted.
 * @kernel_post_read_file:
 *	Read a file specified by userspace.
 *	@file contains the file structure pointing to the file being read
 *	by the kernel.
 *	@buf pointer to buffer containing the file contents.
 *	@size length of the file contents.
 *	@id kernel read file identifier
 *	This must be paired with a prior @kernel_read_file call that had
 *	@contents set to true.
 *	Return 0 if permission is granted.
 * @task_fix_setuid:
 *	Update the module's state after setting one or more of the user
 *	identity attributes of the current process.  The @flags parameter
 *	indicates which of the set*uid system calls invoked this hook.  If
 *	@new is the set of credentials that will be installed.  Modifications
 *	should be made to this rather than to @current->cred.
 *	@old is the set of credentials that are being replaces
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 on success.
 * @task_fix_setgid:
 *	Update the module's state after setting one or more of the group
 *	identity attributes of the current process.  The @flags parameter
 *	indicates which of the set*gid system calls invoked this hook.
 *	@new is the set of credentials that will be installed.  Modifications
 *	should be made to this rather than to @current->cred.
 *	@old is the set of credentials that are being replaced.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 on success.
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
 *	In case of failure, @secid will be set to zero.
 *
 * @task_setnice:
 *	Check permission before setting the nice value of @p to @nice.
 *	@p contains the task_struct of process.
 *	@nice contains the new nice value.
 *	Return 0 if permission is granted.
 * @task_setioprio:
 *	Check permission before setting the ioprio value of @p to @ioprio.
 *	@p contains the task_struct of process.
 *	@ioprio contains the new ioprio value
 *	Return 0 if permission is granted.
 * @task_getioprio:
 *	Check permission before getting the ioprio value of @p.
 *	@p contains the task_struct of process.
 *	Return 0 if permission is granted.
 * @task_prlimit:
 *	Check permission before getting and/or setting the resource limits of
 *	another task.
 *	@cred points to the cred structure for the current task.
 *	@tcred points to the cred structure for the target task.
 *	@flags contains the LSM_PRLIMIT_* flag bits indicating whether the
 *	resource limits are being read, modified, or both.
 *	Return 0 if permission is granted.
 * @task_setrlimit:
 *	Check permission before setting the resource limits of process @p
 *	for @resource to @new_rlim.  The old resource limit values can
 *	be examined by dereferencing (p->signal->rlim + resource).
 *	@p points to the task_struct for the target task's group leader.
 *	@resource contains the resource whose limit is being set.
 *	@new_rlim contains the new limits for @resource.
 *	Return 0 if permission is granted.
 * @task_setscheduler:
 *	Check permission before setting scheduling policy and/or parameters of
 *	process @p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_getscheduler:
 *	Check permission before obtaining scheduling information for process
 *	@p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_movememory:
 *	Check permission before moving memory owned by process @p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_kill:
 *	Check permission before sending signal @sig to @p.  @info can be NULL,
 *	the constant 1, or a pointer to a kernel_siginfo structure.  If @info is 1 or
 *	SI_FROMKERNEL(info) is true, then the signal should be viewed as coming
 *	from the kernel and should typically be permitted.
 *	SIGIO signals are handled separately by the send_sigiotask hook in
 *	file_security_ops.
 *	@p contains the task_struct for process.
 *	@info contains the signal information.
 *	@sig contains the signal value.
 *	@cred contains the cred of the process where the signal originated, or
 *	NULL if the current task is the originator.
 *	Return 0 if permission is granted.
 * @task_prctl:
 *	Check permission before performing a process control operation on the
 *	current process.
 *	@option contains the operation.
 *	@arg2 contains a argument.
 *	@arg3 contains a argument.
 *	@arg4 contains a argument.
 *	@arg5 contains a argument.
 *	Return -ENOSYS if no-one wanted to handle this op, any other value to
 *	cause prctl() to return immediately with that value.
 * @task_to_inode:
 *	Set the security attributes for an inode based on an associated task's
 *	security attributes, e.g. for /proc/pid inodes.
 *	@p contains the task_struct for the task.
 *	@inode contains the inode structure for the inode.
 *
 * Security hooks for Netlink messaging.
 *
 * @netlink_send:
 *	Save security information for a netlink message so that permission
 *	checking can be performed when the message is processed.  The security
 *	information can be saved using the eff_cap field of the
 *	netlink_skb_parms structure.  Also may be used to provide fine
 *	grained control over message transmission.
 *	@sk associated sock of task sending the message.
 *	@skb contains the sk_buff structure for the netlink message.
 *	Return 0 if the information was successfully saved and message
 *	is allowed to be transmitted.
 *
 * Security hooks for Unix domain networking.
 *
 * @unix_stream_connect:
 *	Check permissions before establishing a Unix domain stream connection
 *	between @sock and @other.
 *	@sock contains the sock structure.
 *	@other contains the peer sock structure.
 *	@newsk contains the new sock structure.
 *	Return 0 if permission is granted.
 * @unix_may_send:
 *	Check permissions before connecting or sending datagrams from @sock to
 *	@other.
 *	@sock contains the socket structure.
 *	@other contains the peer socket structure.
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
 *	allocate and attach security information to
 *	SOCK_INODE(sock)->i_security.  This hook may be used to update the
 *	SOCK_INODE(sock)->i_security field with additional information that
 *	wasn't available when the inode was allocated.
 *	@sock contains the newly created socket structure.
 *	@family contains the requested protocol family.
 *	@type contains the requested communications type.
 *	@protocol contains the requested protocol.
 *	@kern set to 1 if a kernel socket.
 * @socket_socketpair:
 *	Check permissions before creating a fresh pair of sockets.
 *	@socka contains the first socket structure.
 *	@sockb contains the second socket structure.
 *	Return 0 if permission is granted and the connection was established.
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
 *	@how contains the flag indicating how future sends and receives
 *	are handled.
 *	Return 0 if permission is granted.
 * @socket_sock_rcv_skb:
 *	Check permissions on incoming network packets.  This hook is distinct
 *	from Netfilter's IP input hooks since it is the first time that the
 *	incoming sk_buff @skb has been associated with a particular socket, @sk.
 *	Must not sleep inside this hook because some callers hold spinlocks.
 *	@sk contains the sock (not socket) associated with the incoming sk_buff.
 *	@skb contains the incoming network data.
 * @socket_getpeersec_stream:
 *	This hook allows the security module to provide peer socket security
 *	state for unix or connected tcp sockets to userspace via getsockopt
 *	SO_GETPEERSEC.  For tcp sockets this can be meaningful if the
 *	socket is associated with an ipsec SA.
 *	@sock is the local socket.
 *	@optval userspace memory where the security state is to be copied.
 *	@optlen userspace int where the module should copy the actual length
 *	of the security state.
 *	@len as input is the maximum length to copy to userspace provided
 *	by the caller.
 *	Return 0 if all is well, otherwise, typical getsockopt return
 *	values.
 * @socket_getpeersec_dgram:
 *	This hook allows the security module to provide peer socket security
 *	state for udp sockets on a per-packet basis to userspace via
 *	getsockopt SO_GETPEERSEC. The application must first have indicated
 *	the IP_PASSSEC option via getsockopt. It can then retrieve the
 *	security state returned by this hook for a packet via the SCM_SECURITY
 *	ancillary message type.
 *	@sock contains the peer socket. May be NULL.
 *	@skb is the sk_buff for the packet being queried. May be NULL.
 *	@secid pointer to store the secid of the packet.
 *	Return 0 on success, error on failure.
 * @sk_alloc_security:
 *	Allocate and attach a security structure to the sk->sk_security field,
 *	which is used to copy security attributes between local stream sockets.
 * @sk_free_security:
 *	Deallocate security structure.
 * @sk_clone_security:
 *	Clone/copy security structure.
 * @sk_getsecid:
 *	Retrieve the LSM-specific secid for the sock to enable caching
 *	of network authorizations.
 * @sock_graft:
 *	Sets the socket's isec sid to the sock's sid.
 * @inet_conn_request:
 *	Sets the openreq's sid to socket's sid with MLS portion taken
 *	from peer sid.
 * @inet_csk_clone:
 *	Sets the new child socket's sid to the openreq sid.
 * @inet_conn_established:
 *	Sets the connection's peersid to the secmark on skb.
 * @secmark_relabel_packet:
 *	check if the process should be allowed to relabel packets to
 *	the given secid
 * @secmark_refcount_inc:
 *	tells the LSM to increment the number of secmark labeling rules loaded
 * @secmark_refcount_dec:
 *	tells the LSM to decrement the number of secmark labeling rules loaded
 * @req_classify_flow:
 *	Sets the flow's sid to the openreq sid.
 * @tun_dev_alloc_security:
 *	This hook allows a module to allocate a security structure for a TUN
 *	device.
 *	@security pointer to a security structure pointer.
 *	Returns a zero on success, negative values on failure.
 * @tun_dev_free_security:
 *	This hook allows a module to free the security structure for a TUN
 *	device.
 *	@security pointer to the TUN device's security structure
 * @tun_dev_create:
 *	Check permissions prior to creating a new TUN device.
 * @tun_dev_attach_queue:
 *	Check permissions prior to attaching to a TUN device queue.
 *	@security pointer to the TUN device's security structure.
 * @tun_dev_attach:
 *	This hook can be used by the module to update any security state
 *	associated with the TUN device's sock structure.
 *	@sk contains the existing sock structure.
 *	@security pointer to the TUN device's security structure.
 * @tun_dev_open:
 *	This hook can be used by the module to update any security state
 *	associated with the TUN device's security structure.
 *	@security pointer to the TUN devices's security structure.
 *
 * Security hooks for SCTP
 *
 * @sctp_assoc_request:
 *	Passes the @ep and @chunk->skb of the association INIT packet to
 *	the security module.
 *	@ep pointer to sctp endpoint structure.
 *	@skb pointer to skbuff of association packet.
 *	Return 0 on success, error on failure.
 * @sctp_bind_connect:
 *	Validiate permissions required for each address associated with sock
 *	@sk. Depending on @optname, the addresses will be treated as either
 *	for a connect or bind service. The @addrlen is calculated on each
 *	ipv4 and ipv6 address using sizeof(struct sockaddr_in) or
 *	sizeof(struct sockaddr_in6).
 *	@sk pointer to sock structure.
 *	@optname name of the option to validate.
 *	@address list containing one or more ipv4/ipv6 addresses.
 *	@addrlen total length of address(s).
 *	Return 0 on success, error on failure.
 * @sctp_sk_clone:
 *	Called whenever a new socket is created by accept(2) (i.e. a TCP
 *	style socket) or when a socket is 'peeled off' e.g userspace
 *	calls sctp_peeloff(3).
 *	@ep pointer to current sctp endpoint structure.
 *	@sk pointer to current sock structure.
 *	@sk pointer to new sock structure.
 *
 * Security hooks for Infiniband
 *
 * @ib_pkey_access:
 *	Check permission to access a pkey when modifing a QP.
 *	@subnet_prefix the subnet prefix of the port being used.
 *	@pkey the pkey to be accessed.
 *	@sec pointer to a security structure.
 * @ib_endport_manage_subnet:
 *	Check permissions to send and receive SMPs on a end port.
 *	@dev_name the IB device name (i.e. mlx4_0).
 *	@port_num the port number.
 *	@sec pointer to a security structure.
 * @ib_alloc_security:
 *	Allocate a security structure for Infiniband objects.
 *	@sec pointer to a security structure pointer.
 *	Returns 0 on success, non-zero on failure
 * @ib_free_security:
 *	Deallocate an Infiniband security structure.
 *	@sec contains the security structure to be freed.
 *
 * Security hooks for XFRM operations.
 *
 * @xfrm_policy_alloc_security:
 *	@ctxp is a pointer to the xfrm_sec_ctx being added to Security Policy
 *	Database used by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level policy update program (e.g., setkey).
 *	Allocate a security structure to the xp->security field; the security
 *	field is initialized to NULL when the xfrm_policy is allocated.
 *	Return 0 if operation was successful (memory to allocate, legal context)
 *	@gfp is to specify the context for the allocation
 * @xfrm_policy_clone_security:
 *	@old_ctx contains an existing xfrm_sec_ctx.
 *	@new_ctxp contains a new xfrm_sec_ctx being cloned from old.
 *	Allocate a security structure in new_ctxp that contains the
 *	information from the old_ctx structure.
 *	Return 0 if operation was successful (memory to allocate).
 * @xfrm_policy_free_security:
 *	@ctx contains the xfrm_sec_ctx
 *	Deallocate xp->security.
 * @xfrm_policy_delete_security:
 *	@ctx contains the xfrm_sec_ctx.
 *	Authorize deletion of xp->security.
 * @xfrm_state_alloc:
 *	@x contains the xfrm_state being added to the Security Association
 *	Database by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level SA generation program (e.g., setkey or racoon).
 *	Allocate a security structure to the x->security field; the security
 *	field is initialized to NULL when the xfrm_state is allocated. Set the
 *	context to correspond to sec_ctx. Return 0 if operation was successful
 *	(memory to allocate, legal context).
 * @xfrm_state_alloc_acquire:
 *	@x contains the xfrm_state being added to the Security Association
 *	Database by the XFRM system.
 *	@polsec contains the policy's security context.
 *	@secid contains the secid from which to take the mls portion of the
 *	context.
 *	Allocate a security structure to the x->security field; the security
 *	field is initialized to NULL when the xfrm_state is allocated. Set the
 *	context to correspond to secid. Return 0 if operation was successful
 *	(memory to allocate, legal context).
 * @xfrm_state_free_security:
 *	@x contains the xfrm_state.
 *	Deallocate x->security.
 * @xfrm_state_delete_security:
 *	@x contains the xfrm_state.
 *	Authorize deletion of x->security.
 * @xfrm_policy_lookup:
 *	@ctx contains the xfrm_sec_ctx for which the access control is being
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
 *	@flic contains the flowi_common struct to check for a match.
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
 *	key.
 *	@key_ref refers to the key (key pointer + possession attribute bit).
 *	@cred points to the credentials to provide the context against which to
 *	evaluate the security data on the key.
 *	@perm describes the combination of permissions required of this key.
 *	Return 0 if permission is granted, -ve error otherwise.
 * @key_getsecurity:
 *	Get a textual representation of the security context attached to a key
 *	for the purposes of honouring KEYCTL_GETSECURITY.  This function
 *	allocates the storage for the NUL-terminated string and the caller
 *	should free it.
 *	@key points to the key to be queried.
 *	@_buffer points to a pointer that should be set to point to the
 *	resulting string (if no label or an error occurs).
 *	Return the length of the string (including terminating NUL) or -ve if
 *	an error.
 *	May also return 0 (and a NULL buffer pointer) if there is no label.
 *
 * Security hooks affecting all System V IPC operations.
 *
 * @ipc_permission:
 *	Check permissions for access to IPC
 *	@ipcp contains the kernel IPC permission structure
 *	@flag contains the desired (requested) permission set
 *	Return 0 if permission is granted.
 * @ipc_getsecid:
 *	Get the secid associated with the ipc object.
 *	@ipcp contains the kernel IPC permission structure.
 *	@secid contains a pointer to the location where result will be saved.
 *	In case of failure, @secid will be set to zero.
 *
 * Security hooks for individual messages held in System V IPC message queues
 *
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
 *	@perm->security field. The security field is initialized to
 *	NULL when the structure is first created.
 *	@perm contains the IPC permissions of the message queue.
 *	Return 0 if operation was successful and permission is granted.
 * @msg_queue_free_security:
 *	Deallocate security field @perm->security for the message queue.
 *	@perm contains the IPC permissions of the message queue.
 * @msg_queue_associate:
 *	Check permission when a message queue is requested through the
 *	msgget system call. This hook is only called when returning the
 *	message queue identifier for an existing message queue, not when a
 *	new message queue is created.
 *	@perm contains the IPC permissions of the message queue.
 *	@msqflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgctl:
 *	Check permission when a message control operation specified by @cmd
 *	is to be performed on the message queue with permissions @perm.
 *	The @perm may be NULL, e.g. for IPC_INFO or MSG_INFO.
 *	@perm contains the IPC permissions of the msg queue. May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @msg_queue_msgsnd:
 *	Check permission before a message, @msg, is enqueued on the message
 *	queue with permissions @perm.
 *	@perm contains the IPC permissions of the message queue.
 *	@msg contains the message to be enqueued.
 *	@msqflg contains operational flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgrcv:
 *	Check permission before a message, @msg, is removed from the message
 *	queue. The @target task structure contains a pointer to the
 *	process that will be receiving the message (not equal to the current
 *	process when inline receives are being performed).
 *	@perm contains the IPC permissions of the message queue.
 *	@msg contains the message destination.
 *	@target contains the task structure for recipient process.
 *	@type contains the type of message requested.
 *	@mode contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Shared Memory Segments
 *
 * @shm_alloc_security:
 *	Allocate and attach a security structure to the @perm->security
 *	field. The security field is initialized to NULL when the structure is
 *	first created.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	Return 0 if operation was successful and permission is granted.
 * @shm_free_security:
 *	Deallocate the security structure @perm->security for the memory segment.
 *	@perm contains the IPC permissions of the shared memory structure.
 * @shm_associate:
 *	Check permission when a shared memory region is requested through the
 *	shmget system call. This hook is only called when returning the shared
 *	memory region identifier for an existing region, not when a new shared
 *	memory region is created.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@shmflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @shm_shmctl:
 *	Check permission when a shared memory control operation specified by
 *	@cmd is to be performed on the shared memory region with permissions @perm.
 *	The @perm may be NULL, e.g. for IPC_INFO or SHM_INFO.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @shm_shmat:
 *	Check permissions prior to allowing the shmat system call to attach the
 *	shared memory segment with permissions @perm to the data segment of the
 *	calling process. The attaching address is specified by @shmaddr.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@shmaddr contains the address to attach memory region to.
 *	@shmflg contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Semaphores
 *
 * @sem_alloc_security:
 *	Allocate and attach a security structure to the @perm->security
 *	field. The security field is initialized to NULL when the structure is
 *	first created.
 *	@perm contains the IPC permissions of the semaphore.
 *	Return 0 if operation was successful and permission is granted.
 * @sem_free_security:
 *	Deallocate security structure @perm->security for the semaphore.
 *	@perm contains the IPC permissions of the semaphore.
 * @sem_associate:
 *	Check permission when a semaphore is requested through the semget
 *	system call. This hook is only called when returning the semaphore
 *	identifier for an existing semaphore, not when a new one must be
 *	created.
 *	@perm contains the IPC permissions of the semaphore.
 *	@semflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @sem_semctl:
 *	Check permission when a semaphore operation specified by @cmd is to be
 *	performed on the semaphore. The @perm may be NULL, e.g. for
 *	IPC_INFO or SEM_INFO.
 *	@perm contains the IPC permissions of the semaphore. May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @sem_semop:
 *	Check permissions before performing operations on members of the
 *	semaphore set. If the @alter flag is nonzero, the semaphore set
 *	may be modified.
 *	@perm contains the IPC permissions of the semaphore.
 *	@sops contains the operations to perform.
 *	@nsops contains the number of operations to perform.
 *	@alter contains the flag indicating whether changes are to be made.
 *	Return 0 if permission is granted.
 *
 * @binder_set_context_mgr:
 *	Check whether @mgr is allowed to be the binder context manager.
 *	@mgr contains the task_struct for the task being registered.
 *	Return 0 if permission is granted.
 * @binder_transaction:
 *	Check whether @from is allowed to invoke a binder transaction call
 *	to @to.
 *	@from contains the task_struct for the sending task.
 *	@to contains the task_struct for the receiving task.
 * @binder_transfer_binder:
 *	Check whether @from is allowed to transfer a binder reference to @to.
 *	@from contains the task_struct for the sending task.
 *	@to contains the task_struct for the receiving task.
 * @binder_transfer_file:
 *	Check whether @from is allowed to transfer @file to @to.
 *	@from contains the task_struct for the sending task.
 *	@file contains the struct file being transferred.
 *	@to contains the task_struct for the receiving task.
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
 *	@ns contains the user namespace we want the capability in
 *	@cap contains the capability <include/linux/capability.h>.
 *	@opts contains options for the capable check <include/linux/security.h>
 *	Return 0 if the capability is granted for @tsk.
 * @quotactl:
 * 	Check whether the quotactl syscall is allowed for this @sb.
 * @quota_on:
 * 	Check whether QUOTAON is allowed for this @dentry.
 * @syslog:
 *	Check permission before accessing the kernel message ring or changing
 *	logging to the console.
 *	See the syslog(2) manual page for an explanation of the @type values.
 *	@type contains the SYSLOG_ACTION_* constant from <include/linux/syslog.h>
 *	Return 0 if permission is granted.
 * @settime:
 *	Check permission to change the system time.
 *	struct timespec64 is defined in <include/linux/time64.h> and timezone
 *	is defined in <include/linux/time.h>
 *	@ts contains new time
 *	@tz contains new timezone
 *	Return 0 if permission is granted.
 * @vm_enough_memory:
 *	Check permissions for allocating a new virtual mapping.
 *	@mm contains the mm struct it is being added to.
 *	@pages contains the number of pages.
 *	Return 0 if permission is granted.
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
 *	@seclen pointer which contains the length of the data
 * @secctx_to_secid:
 *	Convert security context to secid.
 *	@secid contains the pointer to the generated security ID.
 *	@secdata contains the security context.
 *
 * @release_secctx:
 *	Release the security context.
 *	@secdata contains the security context.
 *	@seclen contains the length of the security context.
 *
 * Security hooks for Audit
 *
 * @audit_rule_init:
 *	Allocate and initialize an LSM audit rule structure.
 *	@field contains the required Audit action.
 *	Fields flags are defined in <include/linux/audit.h>
 *	@op contains the operator the rule uses.
 *	@rulestr contains the context where the rule will be applied to.
 *	@lsmrule contains a pointer to receive the result.
 *	Return 0 if @lsmrule has been successfully set,
 *	-EINVAL in case of an invalid rule.
 *
 * @audit_rule_known:
 *	Specifies whether given @krule contains any fields related to
 *	current LSM.
 *	@krule contains the audit rule of interest.
 *	Return 1 in case of relation found, 0 otherwise.
 *
 * @audit_rule_match:
 *	Determine if given @secid matches a rule previously approved
 *	by @audit_rule_known.
 *	@secid contains the security id in question.
 *	@field contains the field which relates to current LSM.
 *	@op contains the operator that will be used for matching.
 *	@lrule points to the audit rule that will be checked against.
 *	Return 1 if secid matches the rule, 0 if it does not, -ERRNO on failure.
 *
 * @audit_rule_free:
 *	Deallocate the LSM audit rule structure previously allocated by
 *	audit_rule_init.
 *	@lsmrule contains the allocated rule
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
 *
 * @inode_getsecctx:
 *	On success, returns 0 and fills out @ctx and @ctxlen with the security
 *	context for the given @inode.
 *	@inode we wish to get the security context of.
 *	@ctx is a pointer in which to place the allocated security context.
 *	@ctxlen points to the place to put the length of @ctx.
 *
 * Security hooks for the general notification queue:
 *
 * @post_notification:
 *	Check to see if a watch notification can be posted to a particular
 *	queue.
 *	@w_cred: The credentials of the whoever set the watch.
 *	@cred: The event-triggerer's credentials
 *	@n: The notification being posted
 *
 * @watch_key:
 *	Check to see if a process is allowed to watch for event notifications
 *	from a key or keyring.
 *	@key: The key to watch.
 *
 * Security hooks for using the eBPF maps and programs functionalities through
 * eBPF syscalls.
 *
 * @bpf:
 *	Do a initial check for all bpf syscalls after the attribute is copied
 *	into the kernel. The actual security module can implement their own
 *	rules to check the specific cmd they need.
 *
 * @bpf_map:
 *	Do a check when the kernel generate and return a file descriptor for
 *	eBPF maps.
 *
 *	@map: bpf map that we want to access
 *	@mask: the access flags
 *
 * @bpf_prog:
 *	Do a check when the kernel generate and return a file descriptor for
 *	eBPF programs.
 *
 *	@prog: bpf prog that userspace want to use.
 *
 * @bpf_map_alloc_security:
 *	Initialize the security field inside bpf map.
 *
 * @bpf_map_free_security:
 *	Clean up the security information stored inside bpf map.
 *
 * @bpf_prog_alloc_security:
 *	Initialize the security field inside bpf program.
 *
 * @bpf_prog_free_security:
 *	Clean up the security information stored inside bpf prog.
 *
 * @locked_down:
 *     Determine whether a kernel feature that potentially enables arbitrary
 *     code execution in kernel space should be permitted.
 *
 *     @what: kernel feature being accessed
 *
 * Security hooks for perf events
 *
 * @perf_event_open:
 * 	Check whether the @type of perf_event_open syscall is allowed.
 * @perf_event_alloc:
 * 	Allocate and save perf_event security info.
 * @perf_event_free:
 * 	Release (free) perf_event security info.
 * @perf_event_read:
 * 	Read perf_event security info if allowed.
 * @perf_event_write:
 * 	Write perf_event security info if allowed.
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
	char				*lsm;
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
				char *lsm);

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
