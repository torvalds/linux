/*
 *  Simplified MAC Kernel (smack) security module
 *
 *  This file contains the smack hook function implementations.
 *
 *  Author:
 *	Casey Schaufler <casey@schaufler-ca.com>
 *
 *  Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */

#include <linux/xattr.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <linux/ext2_fs.h>
#include <linux/kd.h>
#include <asm/ioctls.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/mutex.h>
#include <linux/pipe_fs_i.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <linux/audit.h>

#include "smack.h"

#define task_security(task)	(task_cred_xxx((task), security))

/*
 * I hope these are the hokeyist lines of code in the module. Casey.
 */
#define DEVPTS_SUPER_MAGIC	0x1cd1
#define SOCKFS_MAGIC		0x534F434B
#define TMPFS_MAGIC		0x01021994

/**
 * smk_fetch - Fetch the smack label from a file.
 * @ip: a pointer to the inode
 * @dp: a pointer to the dentry
 *
 * Returns a pointer to the master list entry for the Smack label
 * or NULL if there was no label to fetch.
 */
static char *smk_fetch(struct inode *ip, struct dentry *dp)
{
	int rc;
	char in[SMK_LABELLEN];

	if (ip->i_op->getxattr == NULL)
		return NULL;

	rc = ip->i_op->getxattr(dp, XATTR_NAME_SMACK, in, SMK_LABELLEN);
	if (rc < 0)
		return NULL;

	return smk_import(in, rc);
}

/**
 * new_inode_smack - allocate an inode security blob
 * @smack: a pointer to the Smack label to use in the blob
 *
 * Returns the new blob or NULL if there's no memory available
 */
struct inode_smack *new_inode_smack(char *smack)
{
	struct inode_smack *isp;

	isp = kzalloc(sizeof(struct inode_smack), GFP_KERNEL);
	if (isp == NULL)
		return NULL;

	isp->smk_inode = smack;
	isp->smk_flags = 0;
	mutex_init(&isp->smk_lock);

	return isp;
}

/*
 * LSM hooks.
 * We he, that is fun!
 */

/**
 * smack_ptrace_may_access - Smack approval on PTRACE_ATTACH
 * @ctp: child task pointer
 *
 * Returns 0 if access is OK, an error code otherwise
 *
 * Do the capability checks, and require read and write.
 */
static int smack_ptrace_may_access(struct task_struct *ctp, unsigned int mode)
{
	int rc;

	rc = cap_ptrace_may_access(ctp, mode);
	if (rc != 0)
		return rc;

	rc = smk_access(current_security(), task_security(ctp), MAY_READWRITE);
	if (rc != 0 && capable(CAP_MAC_OVERRIDE))
		return 0;
	return rc;
}

/**
 * smack_ptrace_traceme - Smack approval on PTRACE_TRACEME
 * @ptp: parent task pointer
 *
 * Returns 0 if access is OK, an error code otherwise
 *
 * Do the capability checks, and require read and write.
 */
static int smack_ptrace_traceme(struct task_struct *ptp)
{
	int rc;

	rc = cap_ptrace_traceme(ptp);
	if (rc != 0)
		return rc;

	rc = smk_access(task_security(ptp), current_security(), MAY_READWRITE);
	if (rc != 0 && has_capability(ptp, CAP_MAC_OVERRIDE))
		return 0;
	return rc;
}

/**
 * smack_syslog - Smack approval on syslog
 * @type: message type
 *
 * Require that the task has the floor label
 *
 * Returns 0 on success, error code otherwise.
 */
static int smack_syslog(int type)
{
	int rc;
	char *sp = current_security();

	rc = cap_syslog(type);
	if (rc != 0)
		return rc;

	if (capable(CAP_MAC_OVERRIDE))
		return 0;

	 if (sp != smack_known_floor.smk_known)
		rc = -EACCES;

	return rc;
}


/*
 * Superblock Hooks.
 */

/**
 * smack_sb_alloc_security - allocate a superblock blob
 * @sb: the superblock getting the blob
 *
 * Returns 0 on success or -ENOMEM on error.
 */
static int smack_sb_alloc_security(struct super_block *sb)
{
	struct superblock_smack *sbsp;

	sbsp = kzalloc(sizeof(struct superblock_smack), GFP_KERNEL);

	if (sbsp == NULL)
		return -ENOMEM;

	sbsp->smk_root = smack_known_floor.smk_known;
	sbsp->smk_default = smack_known_floor.smk_known;
	sbsp->smk_floor = smack_known_floor.smk_known;
	sbsp->smk_hat = smack_known_hat.smk_known;
	sbsp->smk_initialized = 0;
	spin_lock_init(&sbsp->smk_sblock);

	sb->s_security = sbsp;

	return 0;
}

/**
 * smack_sb_free_security - free a superblock blob
 * @sb: the superblock getting the blob
 *
 */
static void smack_sb_free_security(struct super_block *sb)
{
	kfree(sb->s_security);
	sb->s_security = NULL;
}

/**
 * smack_sb_copy_data - copy mount options data for processing
 * @type: file system type
 * @orig: where to start
 * @smackopts
 *
 * Returns 0 on success or -ENOMEM on error.
 *
 * Copy the Smack specific mount options out of the mount
 * options list.
 */
static int smack_sb_copy_data(char *orig, char *smackopts)
{
	char *cp, *commap, *otheropts, *dp;

	otheropts = (char *)get_zeroed_page(GFP_KERNEL);
	if (otheropts == NULL)
		return -ENOMEM;

	for (cp = orig, commap = orig; commap != NULL; cp = commap + 1) {
		if (strstr(cp, SMK_FSDEFAULT) == cp)
			dp = smackopts;
		else if (strstr(cp, SMK_FSFLOOR) == cp)
			dp = smackopts;
		else if (strstr(cp, SMK_FSHAT) == cp)
			dp = smackopts;
		else if (strstr(cp, SMK_FSROOT) == cp)
			dp = smackopts;
		else
			dp = otheropts;

		commap = strchr(cp, ',');
		if (commap != NULL)
			*commap = '\0';

		if (*dp != '\0')
			strcat(dp, ",");
		strcat(dp, cp);
	}

	strcpy(orig, otheropts);
	free_page((unsigned long)otheropts);

	return 0;
}

/**
 * smack_sb_kern_mount - Smack specific mount processing
 * @sb: the file system superblock
 * @flags: the mount flags
 * @data: the smack mount options
 *
 * Returns 0 on success, an error code on failure
 */
static int smack_sb_kern_mount(struct super_block *sb, int flags, void *data)
{
	struct dentry *root = sb->s_root;
	struct inode *inode = root->d_inode;
	struct superblock_smack *sp = sb->s_security;
	struct inode_smack *isp;
	char *op;
	char *commap;
	char *nsp;

	spin_lock(&sp->smk_sblock);
	if (sp->smk_initialized != 0) {
		spin_unlock(&sp->smk_sblock);
		return 0;
	}
	sp->smk_initialized = 1;
	spin_unlock(&sp->smk_sblock);

	for (op = data; op != NULL; op = commap) {
		commap = strchr(op, ',');
		if (commap != NULL)
			*commap++ = '\0';

		if (strncmp(op, SMK_FSHAT, strlen(SMK_FSHAT)) == 0) {
			op += strlen(SMK_FSHAT);
			nsp = smk_import(op, 0);
			if (nsp != NULL)
				sp->smk_hat = nsp;
		} else if (strncmp(op, SMK_FSFLOOR, strlen(SMK_FSFLOOR)) == 0) {
			op += strlen(SMK_FSFLOOR);
			nsp = smk_import(op, 0);
			if (nsp != NULL)
				sp->smk_floor = nsp;
		} else if (strncmp(op, SMK_FSDEFAULT,
				   strlen(SMK_FSDEFAULT)) == 0) {
			op += strlen(SMK_FSDEFAULT);
			nsp = smk_import(op, 0);
			if (nsp != NULL)
				sp->smk_default = nsp;
		} else if (strncmp(op, SMK_FSROOT, strlen(SMK_FSROOT)) == 0) {
			op += strlen(SMK_FSROOT);
			nsp = smk_import(op, 0);
			if (nsp != NULL)
				sp->smk_root = nsp;
		}
	}

	/*
	 * Initialize the root inode.
	 */
	isp = inode->i_security;
	if (isp == NULL)
		inode->i_security = new_inode_smack(sp->smk_root);
	else
		isp->smk_inode = sp->smk_root;

	return 0;
}

/**
 * smack_sb_statfs - Smack check on statfs
 * @dentry: identifies the file system in question
 *
 * Returns 0 if current can read the floor of the filesystem,
 * and error code otherwise
 */
static int smack_sb_statfs(struct dentry *dentry)
{
	struct superblock_smack *sbp = dentry->d_sb->s_security;

	return smk_curacc(sbp->smk_floor, MAY_READ);
}

/**
 * smack_sb_mount - Smack check for mounting
 * @dev_name: unused
 * @nd: mount point
 * @type: unused
 * @flags: unused
 * @data: unused
 *
 * Returns 0 if current can write the floor of the filesystem
 * being mounted on, an error code otherwise.
 */
static int smack_sb_mount(char *dev_name, struct path *path,
			  char *type, unsigned long flags, void *data)
{
	struct superblock_smack *sbp = path->mnt->mnt_sb->s_security;

	return smk_curacc(sbp->smk_floor, MAY_WRITE);
}

/**
 * smack_sb_umount - Smack check for unmounting
 * @mnt: file system to unmount
 * @flags: unused
 *
 * Returns 0 if current can write the floor of the filesystem
 * being unmounted, an error code otherwise.
 */
static int smack_sb_umount(struct vfsmount *mnt, int flags)
{
	struct superblock_smack *sbp;

	sbp = mnt->mnt_sb->s_security;

	return smk_curacc(sbp->smk_floor, MAY_WRITE);
}

/*
 * Inode hooks
 */

/**
 * smack_inode_alloc_security - allocate an inode blob
 * @inode - the inode in need of a blob
 *
 * Returns 0 if it gets a blob, -ENOMEM otherwise
 */
static int smack_inode_alloc_security(struct inode *inode)
{
	inode->i_security = new_inode_smack(current_security());
	if (inode->i_security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * smack_inode_free_security - free an inode blob
 * @inode - the inode with a blob
 *
 * Clears the blob pointer in inode
 */
static void smack_inode_free_security(struct inode *inode)
{
	kfree(inode->i_security);
	inode->i_security = NULL;
}

/**
 * smack_inode_init_security - copy out the smack from an inode
 * @inode: the inode
 * @dir: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * Returns 0 if it all works out, -ENOMEM if there's no memory
 */
static int smack_inode_init_security(struct inode *inode, struct inode *dir,
				     char **name, void **value, size_t *len)
{
	char *isp = smk_of_inode(inode);

	if (name) {
		*name = kstrdup(XATTR_SMACK_SUFFIX, GFP_KERNEL);
		if (*name == NULL)
			return -ENOMEM;
	}

	if (value) {
		*value = kstrdup(isp, GFP_KERNEL);
		if (*value == NULL)
			return -ENOMEM;
	}

	if (len)
		*len = strlen(isp) + 1;

	return 0;
}

/**
 * smack_inode_link - Smack check on link
 * @old_dentry: the existing object
 * @dir: unused
 * @new_dentry: the new object
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_link(struct dentry *old_dentry, struct inode *dir,
			    struct dentry *new_dentry)
{
	int rc;
	char *isp;

	isp = smk_of_inode(old_dentry->d_inode);
	rc = smk_curacc(isp, MAY_WRITE);

	if (rc == 0 && new_dentry->d_inode != NULL) {
		isp = smk_of_inode(new_dentry->d_inode);
		rc = smk_curacc(isp, MAY_WRITE);
	}

	return rc;
}

/**
 * smack_inode_unlink - Smack check on inode deletion
 * @dir: containing directory object
 * @dentry: file to unlink
 *
 * Returns 0 if current can write the containing directory
 * and the object, error code otherwise
 */
static int smack_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *ip = dentry->d_inode;
	int rc;

	/*
	 * You need write access to the thing you're unlinking
	 */
	rc = smk_curacc(smk_of_inode(ip), MAY_WRITE);
	if (rc == 0)
		/*
		 * You also need write access to the containing directory
		 */
		rc = smk_curacc(smk_of_inode(dir), MAY_WRITE);

	return rc;
}

/**
 * smack_inode_rmdir - Smack check on directory deletion
 * @dir: containing directory object
 * @dentry: directory to unlink
 *
 * Returns 0 if current can write the containing directory
 * and the directory, error code otherwise
 */
static int smack_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	int rc;

	/*
	 * You need write access to the thing you're removing
	 */
	rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE);
	if (rc == 0)
		/*
		 * You also need write access to the containing directory
		 */
		rc = smk_curacc(smk_of_inode(dir), MAY_WRITE);

	return rc;
}

/**
 * smack_inode_rename - Smack check on rename
 * @old_inode: the old directory
 * @old_dentry: unused
 * @new_inode: the new directory
 * @new_dentry: unused
 *
 * Read and write access is required on both the old and
 * new directories.
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_rename(struct inode *old_inode,
			      struct dentry *old_dentry,
			      struct inode *new_inode,
			      struct dentry *new_dentry)
{
	int rc;
	char *isp;

	isp = smk_of_inode(old_dentry->d_inode);
	rc = smk_curacc(isp, MAY_READWRITE);

	if (rc == 0 && new_dentry->d_inode != NULL) {
		isp = smk_of_inode(new_dentry->d_inode);
		rc = smk_curacc(isp, MAY_READWRITE);
	}

	return rc;
}

/**
 * smack_inode_permission - Smack version of permission()
 * @inode: the inode in question
 * @mask: the access requested
 * @nd: unused
 *
 * This is the important Smack hook.
 *
 * Returns 0 if access is permitted, -EACCES otherwise
 */
static int smack_inode_permission(struct inode *inode, int mask)
{
	/*
	 * No permission to check. Existence test. Yup, it's there.
	 */
	if (mask == 0)
		return 0;

	return smk_curacc(smk_of_inode(inode), mask);
}

/**
 * smack_inode_setattr - Smack check for setting attributes
 * @dentry: the object
 * @iattr: for the force flag
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	/*
	 * Need to allow for clearing the setuid bit.
	 */
	if (iattr->ia_valid & ATTR_FORCE)
		return 0;

	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE);
}

/**
 * smack_inode_getattr - Smack check for getting attributes
 * @mnt: unused
 * @dentry: the object
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_READ);
}

/**
 * smack_inode_setxattr - Smack check for setting xattrs
 * @dentry: the object
 * @name: name of the attribute
 * @value: unused
 * @size: unused
 * @flags: unused
 *
 * This protects the Smack attribute explicitly.
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_setxattr(struct dentry *dentry, const char *name,
				const void *value, size_t size, int flags)
{
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SMACK) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPIN) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPOUT) == 0) {
		if (!capable(CAP_MAC_ADMIN))
			rc = -EPERM;
	} else
		rc = cap_inode_setxattr(dentry, name, value, size, flags);

	if (rc == 0)
		rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE);

	return rc;
}

/**
 * smack_inode_post_setxattr - Apply the Smack update approved above
 * @dentry: object
 * @name: attribute name
 * @value: attribute value
 * @size: attribute size
 * @flags: unused
 *
 * Set the pointer in the inode blob to the entry found
 * in the master label list.
 */
static void smack_inode_post_setxattr(struct dentry *dentry, const char *name,
				      const void *value, size_t size, int flags)
{
	struct inode_smack *isp;
	char *nsp;

	/*
	 * Not SMACK
	 */
	if (strcmp(name, XATTR_NAME_SMACK))
		return;

	if (size >= SMK_LABELLEN)
		return;

	isp = dentry->d_inode->i_security;

	/*
	 * No locking is done here. This is a pointer
	 * assignment.
	 */
	nsp = smk_import(value, size);
	if (nsp != NULL)
		isp->smk_inode = nsp;
	else
		isp->smk_inode = smack_known_invalid.smk_known;

	return;
}

/*
 * smack_inode_getxattr - Smack check on getxattr
 * @dentry: the object
 * @name: unused
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_getxattr(struct dentry *dentry, const char *name)
{
	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_READ);
}

/*
 * smack_inode_removexattr - Smack check on removexattr
 * @dentry: the object
 * @name: name of the attribute
 *
 * Removing the Smack attribute requires CAP_MAC_ADMIN
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_removexattr(struct dentry *dentry, const char *name)
{
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SMACK) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPIN) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPOUT) == 0) {
		if (!capable(CAP_MAC_ADMIN))
			rc = -EPERM;
	} else
		rc = cap_inode_removexattr(dentry, name);

	if (rc == 0)
		rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE);

	return rc;
}

/**
 * smack_inode_getsecurity - get smack xattrs
 * @inode: the object
 * @name: attribute name
 * @buffer: where to put the result
 * @size: size of the buffer
 * @err: unused
 *
 * Returns the size of the attribute or an error code
 */
static int smack_inode_getsecurity(const struct inode *inode,
				   const char *name, void **buffer,
				   bool alloc)
{
	struct socket_smack *ssp;
	struct socket *sock;
	struct super_block *sbp;
	struct inode *ip = (struct inode *)inode;
	char *isp;
	int ilen;
	int rc = 0;

	if (strcmp(name, XATTR_SMACK_SUFFIX) == 0) {
		isp = smk_of_inode(inode);
		ilen = strlen(isp) + 1;
		*buffer = isp;
		return ilen;
	}

	/*
	 * The rest of the Smack xattrs are only on sockets.
	 */
	sbp = ip->i_sb;
	if (sbp->s_magic != SOCKFS_MAGIC)
		return -EOPNOTSUPP;

	sock = SOCKET_I(ip);
	if (sock == NULL || sock->sk == NULL)
		return -EOPNOTSUPP;

	ssp = sock->sk->sk_security;

	if (strcmp(name, XATTR_SMACK_IPIN) == 0)
		isp = ssp->smk_in;
	else if (strcmp(name, XATTR_SMACK_IPOUT) == 0)
		isp = ssp->smk_out;
	else
		return -EOPNOTSUPP;

	ilen = strlen(isp) + 1;
	if (rc == 0) {
		*buffer = isp;
		rc = ilen;
	}

	return rc;
}


/**
 * smack_inode_listsecurity - list the Smack attributes
 * @inode: the object
 * @buffer: where they go
 * @buffer_size: size of buffer
 *
 * Returns 0 on success, -EINVAL otherwise
 */
static int smack_inode_listsecurity(struct inode *inode, char *buffer,
				    size_t buffer_size)
{
	int len = strlen(XATTR_NAME_SMACK);

	if (buffer != NULL && len <= buffer_size) {
		memcpy(buffer, XATTR_NAME_SMACK, len);
		return len;
	}
	return -EINVAL;
}

/**
 * smack_inode_getsecid - Extract inode's security id
 * @inode: inode to extract the info from
 * @secid: where result will be saved
 */
static void smack_inode_getsecid(const struct inode *inode, u32 *secid)
{
	struct inode_smack *isp = inode->i_security;

	*secid = smack_to_secid(isp->smk_inode);
}

/*
 * File Hooks
 */

/**
 * smack_file_permission - Smack check on file operations
 * @file: unused
 * @mask: unused
 *
 * Returns 0
 *
 * Should access checks be done on each read or write?
 * UNICOS and SELinux say yes.
 * Trusted Solaris, Trusted Irix, and just about everyone else says no.
 *
 * I'll say no for now. Smack does not do the frequent
 * label changing that SELinux does.
 */
static int smack_file_permission(struct file *file, int mask)
{
	return 0;
}

/**
 * smack_file_alloc_security - assign a file security blob
 * @file: the object
 *
 * The security blob for a file is a pointer to the master
 * label list, so no allocation is done.
 *
 * Returns 0
 */
static int smack_file_alloc_security(struct file *file)
{
	file->f_security = current_security();
	return 0;
}

/**
 * smack_file_free_security - clear a file security blob
 * @file: the object
 *
 * The security blob for a file is a pointer to the master
 * label list, so no memory is freed.
 */
static void smack_file_free_security(struct file *file)
{
	file->f_security = NULL;
}

/**
 * smack_file_ioctl - Smack check on ioctls
 * @file: the object
 * @cmd: what to do
 * @arg: unused
 *
 * Relies heavily on the correct use of the ioctl command conventions.
 *
 * Returns 0 if allowed, error code otherwise
 */
static int smack_file_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int rc = 0;

	if (_IOC_DIR(cmd) & _IOC_WRITE)
		rc = smk_curacc(file->f_security, MAY_WRITE);

	if (rc == 0 && (_IOC_DIR(cmd) & _IOC_READ))
		rc = smk_curacc(file->f_security, MAY_READ);

	return rc;
}

/**
 * smack_file_lock - Smack check on file locking
 * @file: the object
 * @cmd unused
 *
 * Returns 0 if current has write access, error code otherwise
 */
static int smack_file_lock(struct file *file, unsigned int cmd)
{
	return smk_curacc(file->f_security, MAY_WRITE);
}

/**
 * smack_file_fcntl - Smack check on fcntl
 * @file: the object
 * @cmd: what action to check
 * @arg: unused
 *
 * Returns 0 if current has access, error code otherwise
 */
static int smack_file_fcntl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int rc;

	switch (cmd) {
	case F_DUPFD:
	case F_GETFD:
	case F_GETFL:
	case F_GETLK:
	case F_GETOWN:
	case F_GETSIG:
		rc = smk_curacc(file->f_security, MAY_READ);
		break;
	case F_SETFD:
	case F_SETFL:
	case F_SETLK:
	case F_SETLKW:
	case F_SETOWN:
	case F_SETSIG:
		rc = smk_curacc(file->f_security, MAY_WRITE);
		break;
	default:
		rc = smk_curacc(file->f_security, MAY_READWRITE);
	}

	return rc;
}

/**
 * smack_file_set_fowner - set the file security blob value
 * @file: object in question
 *
 * Returns 0
 * Further research may be required on this one.
 */
static int smack_file_set_fowner(struct file *file)
{
	file->f_security = current_security();
	return 0;
}

/**
 * smack_file_send_sigiotask - Smack on sigio
 * @tsk: The target task
 * @fown: the object the signal come from
 * @signum: unused
 *
 * Allow a privileged task to get signals even if it shouldn't
 *
 * Returns 0 if a subject with the object's smack could
 * write to the task, an error code otherwise.
 */
static int smack_file_send_sigiotask(struct task_struct *tsk,
				     struct fown_struct *fown, int signum)
{
	struct file *file;
	int rc;

	/*
	 * struct fown_struct is never outside the context of a struct file
	 */
	file = container_of(fown, struct file, f_owner);
	rc = smk_access(file->f_security, tsk->cred->security, MAY_WRITE);
	if (rc != 0 && has_capability(tsk, CAP_MAC_OVERRIDE))
		return 0;
	return rc;
}

/**
 * smack_file_receive - Smack file receive check
 * @file: the object
 *
 * Returns 0 if current has access, error code otherwise
 */
static int smack_file_receive(struct file *file)
{
	int may = 0;

	/*
	 * This code relies on bitmasks.
	 */
	if (file->f_mode & FMODE_READ)
		may = MAY_READ;
	if (file->f_mode & FMODE_WRITE)
		may |= MAY_WRITE;

	return smk_curacc(file->f_security, may);
}

/*
 * Task hooks
 */

/**
 * smack_cred_free - "free" task-level security credentials
 * @cred: the credentials in question
 *
 * Smack isn't using copies of blobs. Everyone
 * points to an immutable list. The blobs never go away.
 * There is no leak here.
 */
static void smack_cred_free(struct cred *cred)
{
	cred->security = NULL;
}

/**
 * smack_cred_prepare - prepare new set of credentials for modification
 * @new: the new credentials
 * @old: the original credentials
 * @gfp: the atomicity of any memory allocations
 *
 * Prepare a new set of credentials for modification.
 */
static int smack_cred_prepare(struct cred *new, const struct cred *old,
			      gfp_t gfp)
{
	new->security = old->security;
	return 0;
}

/*
 * commit new credentials
 * @new: the new credentials
 * @old: the original credentials
 */
static void smack_cred_commit(struct cred *new, const struct cred *old)
{
}

/**
 * smack_kernel_act_as - Set the subjective context in a set of credentials
 * @new points to the set of credentials to be modified.
 * @secid specifies the security ID to be set
 *
 * Set the security data for a kernel service.
 */
static int smack_kernel_act_as(struct cred *new, u32 secid)
{
	char *smack = smack_from_secid(secid);

	if (smack == NULL)
		return -EINVAL;

	new->security = smack;
	return 0;
}

/**
 * smack_kernel_create_files_as - Set the file creation label in a set of creds
 * @new points to the set of credentials to be modified
 * @inode points to the inode to use as a reference
 *
 * Set the file creation context in a set of credentials to the same
 * as the objective context of the specified inode
 */
static int smack_kernel_create_files_as(struct cred *new,
					struct inode *inode)
{
	struct inode_smack *isp = inode->i_security;

	new->security = isp->smk_inode;
	return 0;
}

/**
 * smack_task_setpgid - Smack check on setting pgid
 * @p: the task object
 * @pgid: unused
 *
 * Return 0 if write access is permitted
 */
static int smack_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return smk_curacc(task_security(p), MAY_WRITE);
}

/**
 * smack_task_getpgid - Smack access check for getpgid
 * @p: the object task
 *
 * Returns 0 if current can read the object task, error code otherwise
 */
static int smack_task_getpgid(struct task_struct *p)
{
	return smk_curacc(task_security(p), MAY_READ);
}

/**
 * smack_task_getsid - Smack access check for getsid
 * @p: the object task
 *
 * Returns 0 if current can read the object task, error code otherwise
 */
static int smack_task_getsid(struct task_struct *p)
{
	return smk_curacc(task_security(p), MAY_READ);
}

/**
 * smack_task_getsecid - get the secid of the task
 * @p: the object task
 * @secid: where to put the result
 *
 * Sets the secid to contain a u32 version of the smack label.
 */
static void smack_task_getsecid(struct task_struct *p, u32 *secid)
{
	*secid = smack_to_secid(task_security(p));
}

/**
 * smack_task_setnice - Smack check on setting nice
 * @p: the task object
 * @nice: unused
 *
 * Return 0 if write access is permitted
 */
static int smack_task_setnice(struct task_struct *p, int nice)
{
	int rc;

	rc = cap_task_setnice(p, nice);
	if (rc == 0)
		rc = smk_curacc(task_security(p), MAY_WRITE);
	return rc;
}

/**
 * smack_task_setioprio - Smack check on setting ioprio
 * @p: the task object
 * @ioprio: unused
 *
 * Return 0 if write access is permitted
 */
static int smack_task_setioprio(struct task_struct *p, int ioprio)
{
	int rc;

	rc = cap_task_setioprio(p, ioprio);
	if (rc == 0)
		rc = smk_curacc(task_security(p), MAY_WRITE);
	return rc;
}

/**
 * smack_task_getioprio - Smack check on reading ioprio
 * @p: the task object
 *
 * Return 0 if read access is permitted
 */
static int smack_task_getioprio(struct task_struct *p)
{
	return smk_curacc(task_security(p), MAY_READ);
}

/**
 * smack_task_setscheduler - Smack check on setting scheduler
 * @p: the task object
 * @policy: unused
 * @lp: unused
 *
 * Return 0 if read access is permitted
 */
static int smack_task_setscheduler(struct task_struct *p, int policy,
				   struct sched_param *lp)
{
	int rc;

	rc = cap_task_setscheduler(p, policy, lp);
	if (rc == 0)
		rc = smk_curacc(task_security(p), MAY_WRITE);
	return rc;
}

/**
 * smack_task_getscheduler - Smack check on reading scheduler
 * @p: the task object
 *
 * Return 0 if read access is permitted
 */
static int smack_task_getscheduler(struct task_struct *p)
{
	return smk_curacc(task_security(p), MAY_READ);
}

/**
 * smack_task_movememory - Smack check on moving memory
 * @p: the task object
 *
 * Return 0 if write access is permitted
 */
static int smack_task_movememory(struct task_struct *p)
{
	return smk_curacc(task_security(p), MAY_WRITE);
}

/**
 * smack_task_kill - Smack check on signal delivery
 * @p: the task object
 * @info: unused
 * @sig: unused
 * @secid: identifies the smack to use in lieu of current's
 *
 * Return 0 if write access is permitted
 *
 * The secid behavior is an artifact of an SELinux hack
 * in the USB code. Someday it may go away.
 */
static int smack_task_kill(struct task_struct *p, struct siginfo *info,
			   int sig, u32 secid)
{
	/*
	 * Sending a signal requires that the sender
	 * can write the receiver.
	 */
	if (secid == 0)
		return smk_curacc(task_security(p), MAY_WRITE);
	/*
	 * If the secid isn't 0 we're dealing with some USB IO
	 * specific behavior. This is not clean. For one thing
	 * we can't take privilege into account.
	 */
	return smk_access(smack_from_secid(secid), task_security(p), MAY_WRITE);
}

/**
 * smack_task_wait - Smack access check for waiting
 * @p: task to wait for
 *
 * Returns 0 if current can wait for p, error code otherwise
 */
static int smack_task_wait(struct task_struct *p)
{
	int rc;

	rc = smk_access(current_security(), task_security(p), MAY_WRITE);
	if (rc == 0)
		return 0;

	/*
	 * Allow the operation to succeed if either task
	 * has privilege to perform operations that might
	 * account for the smack labels having gotten to
	 * be different in the first place.
	 *
	 * This breaks the strict subject/object access
	 * control ideal, taking the object's privilege
	 * state into account in the decision as well as
	 * the smack value.
	 */
	if (capable(CAP_MAC_OVERRIDE) || has_capability(p, CAP_MAC_OVERRIDE))
		return 0;

	return rc;
}

/**
 * smack_task_to_inode - copy task smack into the inode blob
 * @p: task to copy from
 * inode: inode to copy to
 *
 * Sets the smack pointer in the inode security blob
 */
static void smack_task_to_inode(struct task_struct *p, struct inode *inode)
{
	struct inode_smack *isp = inode->i_security;
	isp->smk_inode = task_security(p);
}

/*
 * Socket hooks.
 */

/**
 * smack_sk_alloc_security - Allocate a socket blob
 * @sk: the socket
 * @family: unused
 * @priority: memory allocation priority
 *
 * Assign Smack pointers to current
 *
 * Returns 0 on success, -ENOMEM is there's no memory
 */
static int smack_sk_alloc_security(struct sock *sk, int family, gfp_t gfp_flags)
{
	char *csp = current_security();
	struct socket_smack *ssp;

	ssp = kzalloc(sizeof(struct socket_smack), gfp_flags);
	if (ssp == NULL)
		return -ENOMEM;

	ssp->smk_in = csp;
	ssp->smk_out = csp;
	ssp->smk_labeled = SMACK_CIPSO_SOCKET;
	ssp->smk_packet[0] = '\0';

	sk->sk_security = ssp;

	return 0;
}

/**
 * smack_sk_free_security - Free a socket blob
 * @sk: the socket
 *
 * Clears the blob pointer
 */
static void smack_sk_free_security(struct sock *sk)
{
	kfree(sk->sk_security);
}

/**
 * smack_set_catset - convert a capset to netlabel mls categories
 * @catset: the Smack categories
 * @sap: where to put the netlabel categories
 *
 * Allocates and fills attr.mls.cat
 */
static void smack_set_catset(char *catset, struct netlbl_lsm_secattr *sap)
{
	unsigned char *cp;
	unsigned char m;
	int cat;
	int rc;
	int byte;

	if (!catset)
		return;

	sap->flags |= NETLBL_SECATTR_MLS_CAT;
	sap->attr.mls.cat = netlbl_secattr_catmap_alloc(GFP_ATOMIC);
	sap->attr.mls.cat->startbit = 0;

	for (cat = 1, cp = catset, byte = 0; byte < SMK_LABELLEN; cp++, byte++)
		for (m = 0x80; m != 0; m >>= 1, cat++) {
			if ((m & *cp) == 0)
				continue;
			rc = netlbl_secattr_catmap_setbit(sap->attr.mls.cat,
							  cat, GFP_ATOMIC);
		}
}

/**
 * smack_to_secattr - fill a secattr from a smack value
 * @smack: the smack value
 * @nlsp: where the result goes
 *
 * Casey says that CIPSO is good enough for now.
 * It can be used to effect.
 * It can also be abused to effect when necessary.
 * Appologies to the TSIG group in general and GW in particular.
 */
static void smack_to_secattr(char *smack, struct netlbl_lsm_secattr *nlsp)
{
	struct smack_cipso cipso;
	int rc;

	nlsp->domain = smack;
	nlsp->flags = NETLBL_SECATTR_DOMAIN | NETLBL_SECATTR_MLS_LVL;

	rc = smack_to_cipso(smack, &cipso);
	if (rc == 0) {
		nlsp->attr.mls.lvl = cipso.smk_level;
		smack_set_catset(cipso.smk_catset, nlsp);
	} else {
		nlsp->attr.mls.lvl = smack_cipso_direct;
		smack_set_catset(smack, nlsp);
	}
}

/**
 * smack_netlabel - Set the secattr on a socket
 * @sk: the socket
 * @labeled: socket label scheme
 *
 * Convert the outbound smack value (smk_out) to a
 * secattr and attach it to the socket.
 *
 * Returns 0 on success or an error code
 */
static int smack_netlabel(struct sock *sk, int labeled)
{
	struct socket_smack *ssp;
	struct netlbl_lsm_secattr secattr;
	int rc = 0;

	ssp = sk->sk_security;
	/*
	 * Usually the netlabel code will handle changing the
	 * packet labeling based on the label.
	 * The case of a single label host is different, because
	 * a single label host should never get a labeled packet
	 * even though the label is usually associated with a packet
	 * label.
	 */
	local_bh_disable();
	bh_lock_sock_nested(sk);

	if (ssp->smk_out == smack_net_ambient ||
	    labeled == SMACK_UNLABELED_SOCKET)
		netlbl_sock_delattr(sk);
	else {
		netlbl_secattr_init(&secattr);
		smack_to_secattr(ssp->smk_out, &secattr);
		rc = netlbl_sock_setattr(sk, &secattr);
		netlbl_secattr_destroy(&secattr);
	}

	bh_unlock_sock(sk);
	local_bh_enable();
	/*
	 * Remember the label scheme used so that it is not
	 * necessary to do the netlabel setting if it has not
	 * changed the next time through.
	 *
	 * The -EDESTADDRREQ case is an indication that there's
	 * a single level host involved.
	 */
	if (rc == 0)
		ssp->smk_labeled = labeled;

	return rc;
}

/**
 * smack_inode_setsecurity - set smack xattrs
 * @inode: the object
 * @name: attribute name
 * @value: attribute value
 * @size: size of the attribute
 * @flags: unused
 *
 * Sets the named attribute in the appropriate blob
 *
 * Returns 0 on success, or an error code
 */
static int smack_inode_setsecurity(struct inode *inode, const char *name,
				   const void *value, size_t size, int flags)
{
	char *sp;
	struct inode_smack *nsp = inode->i_security;
	struct socket_smack *ssp;
	struct socket *sock;
	int rc = 0;

	if (value == NULL || size > SMK_LABELLEN)
		return -EACCES;

	sp = smk_import(value, size);
	if (sp == NULL)
		return -EINVAL;

	if (strcmp(name, XATTR_SMACK_SUFFIX) == 0) {
		nsp->smk_inode = sp;
		return 0;
	}
	/*
	 * The rest of the Smack xattrs are only on sockets.
	 */
	if (inode->i_sb->s_magic != SOCKFS_MAGIC)
		return -EOPNOTSUPP;

	sock = SOCKET_I(inode);
	if (sock == NULL || sock->sk == NULL)
		return -EOPNOTSUPP;

	ssp = sock->sk->sk_security;

	if (strcmp(name, XATTR_SMACK_IPIN) == 0)
		ssp->smk_in = sp;
	else if (strcmp(name, XATTR_SMACK_IPOUT) == 0) {
		ssp->smk_out = sp;
		rc = smack_netlabel(sock->sk, SMACK_CIPSO_SOCKET);
		if (rc != 0)
			printk(KERN_WARNING "Smack: \"%s\" netlbl error %d.\n",
			       __func__, -rc);
	} else
		return -EOPNOTSUPP;

	return 0;
}

/**
 * smack_socket_post_create - finish socket setup
 * @sock: the socket
 * @family: protocol family
 * @type: unused
 * @protocol: unused
 * @kern: unused
 *
 * Sets the netlabel information on the socket
 *
 * Returns 0 on success, and error code otherwise
 */
static int smack_socket_post_create(struct socket *sock, int family,
				    int type, int protocol, int kern)
{
	if (family != PF_INET || sock->sk == NULL)
		return 0;
	/*
	 * Set the outbound netlbl.
	 */
	return smack_netlabel(sock->sk, SMACK_CIPSO_SOCKET);
}


/**
 * smack_host_label - check host based restrictions
 * @sip: the object end
 *
 * looks for host based access restrictions
 *
 * This version will only be appropriate for really small
 * sets of single label hosts. Because of the masking
 * it cannot shortcut out on the first match. There are
 * numerious ways to address the problem, but none of them
 * have been applied here.
 *
 * Returns the label of the far end or NULL if it's not special.
 */
static char *smack_host_label(struct sockaddr_in *sip)
{
	struct smk_netlbladdr *snp;
	char *bestlabel = NULL;
	struct in_addr *siap = &sip->sin_addr;
	struct in_addr *liap;
	struct in_addr *miap;
	struct in_addr bestmask;

	if (siap->s_addr == 0)
		return NULL;

	bestmask.s_addr = 0;

	for (snp = smack_netlbladdrs; snp != NULL; snp = snp->smk_next) {
		liap = &snp->smk_host.sin_addr;
		miap = &snp->smk_mask;
		/*
		 * If the addresses match after applying the list entry mask
		 * the entry matches the address. If it doesn't move along to
		 * the next entry.
		 */
		if ((liap->s_addr & miap->s_addr) !=
		    (siap->s_addr & miap->s_addr))
			continue;
		/*
		 * If the list entry mask identifies a single address
		 * it can't get any more specific.
		 */
		if (miap->s_addr == 0xffffffff)
			return snp->smk_label;
		/*
		 * If the list entry mask is less specific than the best
		 * already found this entry is uninteresting.
		 */
		if ((miap->s_addr | bestmask.s_addr) == bestmask.s_addr)
			continue;
		/*
		 * This is better than any entry found so far.
		 */
		bestmask.s_addr = miap->s_addr;
		bestlabel = snp->smk_label;
	}

	return bestlabel;
}

/**
 * smack_socket_connect - connect access check
 * @sock: the socket
 * @sap: the other end
 * @addrlen: size of sap
 *
 * Verifies that a connection may be possible
 *
 * Returns 0 on success, and error code otherwise
 */
static int smack_socket_connect(struct socket *sock, struct sockaddr *sap,
				int addrlen)
{
	struct socket_smack *ssp = sock->sk->sk_security;
	char *hostsp;
	int rc;

	if (sock->sk == NULL || sock->sk->sk_family != PF_INET)
		return 0;

	if (addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;

	hostsp = smack_host_label((struct sockaddr_in *)sap);
	if (hostsp == NULL) {
		if (ssp->smk_labeled != SMACK_CIPSO_SOCKET)
			return smack_netlabel(sock->sk, SMACK_CIPSO_SOCKET);
		return 0;
	}

	rc = smk_access(ssp->smk_out, hostsp, MAY_WRITE);
	if (rc != 0)
		return rc;

	if (ssp->smk_labeled != SMACK_UNLABELED_SOCKET)
		return smack_netlabel(sock->sk, SMACK_UNLABELED_SOCKET);
	return 0;
}

/**
 * smack_flags_to_may - convert S_ to MAY_ values
 * @flags: the S_ value
 *
 * Returns the equivalent MAY_ value
 */
static int smack_flags_to_may(int flags)
{
	int may = 0;

	if (flags & S_IRUGO)
		may |= MAY_READ;
	if (flags & S_IWUGO)
		may |= MAY_WRITE;
	if (flags & S_IXUGO)
		may |= MAY_EXEC;

	return may;
}

/**
 * smack_msg_msg_alloc_security - Set the security blob for msg_msg
 * @msg: the object
 *
 * Returns 0
 */
static int smack_msg_msg_alloc_security(struct msg_msg *msg)
{
	msg->security = current_security();
	return 0;
}

/**
 * smack_msg_msg_free_security - Clear the security blob for msg_msg
 * @msg: the object
 *
 * Clears the blob pointer
 */
static void smack_msg_msg_free_security(struct msg_msg *msg)
{
	msg->security = NULL;
}

/**
 * smack_of_shm - the smack pointer for the shm
 * @shp: the object
 *
 * Returns a pointer to the smack value
 */
static char *smack_of_shm(struct shmid_kernel *shp)
{
	return (char *)shp->shm_perm.security;
}

/**
 * smack_shm_alloc_security - Set the security blob for shm
 * @shp: the object
 *
 * Returns 0
 */
static int smack_shm_alloc_security(struct shmid_kernel *shp)
{
	struct kern_ipc_perm *isp = &shp->shm_perm;

	isp->security = current_security();
	return 0;
}

/**
 * smack_shm_free_security - Clear the security blob for shm
 * @shp: the object
 *
 * Clears the blob pointer
 */
static void smack_shm_free_security(struct shmid_kernel *shp)
{
	struct kern_ipc_perm *isp = &shp->shm_perm;

	isp->security = NULL;
}

/**
 * smack_shm_associate - Smack access check for shm
 * @shp: the object
 * @shmflg: access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	char *ssp = smack_of_shm(shp);
	int may;

	may = smack_flags_to_may(shmflg);
	return smk_curacc(ssp, may);
}

/**
 * smack_shm_shmctl - Smack access check for shm
 * @shp: the object
 * @cmd: what it wants to do
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	char *ssp;
	int may;

	switch (cmd) {
	case IPC_STAT:
	case SHM_STAT:
		may = MAY_READ;
		break;
	case IPC_SET:
	case SHM_LOCK:
	case SHM_UNLOCK:
	case IPC_RMID:
		may = MAY_READWRITE;
		break;
	case IPC_INFO:
	case SHM_INFO:
		/*
		 * System level information.
		 */
		return 0;
	default:
		return -EINVAL;
	}

	ssp = smack_of_shm(shp);
	return smk_curacc(ssp, may);
}

/**
 * smack_shm_shmat - Smack access for shmat
 * @shp: the object
 * @shmaddr: unused
 * @shmflg: access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr,
			   int shmflg)
{
	char *ssp = smack_of_shm(shp);
	int may;

	may = smack_flags_to_may(shmflg);
	return smk_curacc(ssp, may);
}

/**
 * smack_of_sem - the smack pointer for the sem
 * @sma: the object
 *
 * Returns a pointer to the smack value
 */
static char *smack_of_sem(struct sem_array *sma)
{
	return (char *)sma->sem_perm.security;
}

/**
 * smack_sem_alloc_security - Set the security blob for sem
 * @sma: the object
 *
 * Returns 0
 */
static int smack_sem_alloc_security(struct sem_array *sma)
{
	struct kern_ipc_perm *isp = &sma->sem_perm;

	isp->security = current_security();
	return 0;
}

/**
 * smack_sem_free_security - Clear the security blob for sem
 * @sma: the object
 *
 * Clears the blob pointer
 */
static void smack_sem_free_security(struct sem_array *sma)
{
	struct kern_ipc_perm *isp = &sma->sem_perm;

	isp->security = NULL;
}

/**
 * smack_sem_associate - Smack access check for sem
 * @sma: the object
 * @semflg: access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_sem_associate(struct sem_array *sma, int semflg)
{
	char *ssp = smack_of_sem(sma);
	int may;

	may = smack_flags_to_may(semflg);
	return smk_curacc(ssp, may);
}

/**
 * smack_sem_shmctl - Smack access check for sem
 * @sma: the object
 * @cmd: what it wants to do
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_sem_semctl(struct sem_array *sma, int cmd)
{
	char *ssp;
	int may;

	switch (cmd) {
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETVAL:
	case GETALL:
	case IPC_STAT:
	case SEM_STAT:
		may = MAY_READ;
		break;
	case SETVAL:
	case SETALL:
	case IPC_RMID:
	case IPC_SET:
		may = MAY_READWRITE;
		break;
	case IPC_INFO:
	case SEM_INFO:
		/*
		 * System level information
		 */
		return 0;
	default:
		return -EINVAL;
	}

	ssp = smack_of_sem(sma);
	return smk_curacc(ssp, may);
}

/**
 * smack_sem_semop - Smack checks of semaphore operations
 * @sma: the object
 * @sops: unused
 * @nsops: unused
 * @alter: unused
 *
 * Treated as read and write in all cases.
 *
 * Returns 0 if access is allowed, error code otherwise
 */
static int smack_sem_semop(struct sem_array *sma, struct sembuf *sops,
			   unsigned nsops, int alter)
{
	char *ssp = smack_of_sem(sma);

	return smk_curacc(ssp, MAY_READWRITE);
}

/**
 * smack_msg_alloc_security - Set the security blob for msg
 * @msq: the object
 *
 * Returns 0
 */
static int smack_msg_queue_alloc_security(struct msg_queue *msq)
{
	struct kern_ipc_perm *kisp = &msq->q_perm;

	kisp->security = current_security();
	return 0;
}

/**
 * smack_msg_free_security - Clear the security blob for msg
 * @msq: the object
 *
 * Clears the blob pointer
 */
static void smack_msg_queue_free_security(struct msg_queue *msq)
{
	struct kern_ipc_perm *kisp = &msq->q_perm;

	kisp->security = NULL;
}

/**
 * smack_of_msq - the smack pointer for the msq
 * @msq: the object
 *
 * Returns a pointer to the smack value
 */
static char *smack_of_msq(struct msg_queue *msq)
{
	return (char *)msq->q_perm.security;
}

/**
 * smack_msg_queue_associate - Smack access check for msg_queue
 * @msq: the object
 * @msqflg: access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	char *msp = smack_of_msq(msq);
	int may;

	may = smack_flags_to_may(msqflg);
	return smk_curacc(msp, may);
}

/**
 * smack_msg_queue_msgctl - Smack access check for msg_queue
 * @msq: the object
 * @cmd: what it wants to do
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	char *msp;
	int may;

	switch (cmd) {
	case IPC_STAT:
	case MSG_STAT:
		may = MAY_READ;
		break;
	case IPC_SET:
	case IPC_RMID:
		may = MAY_READWRITE;
		break;
	case IPC_INFO:
	case MSG_INFO:
		/*
		 * System level information
		 */
		return 0;
	default:
		return -EINVAL;
	}

	msp = smack_of_msq(msq);
	return smk_curacc(msp, may);
}

/**
 * smack_msg_queue_msgsnd - Smack access check for msg_queue
 * @msq: the object
 * @msg: unused
 * @msqflg: access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smack_msg_queue_msgsnd(struct msg_queue *msq, struct msg_msg *msg,
				  int msqflg)
{
	char *msp = smack_of_msq(msq);
	int rc;

	rc = smack_flags_to_may(msqflg);
	return smk_curacc(msp, rc);
}

/**
 * smack_msg_queue_msgsnd - Smack access check for msg_queue
 * @msq: the object
 * @msg: unused
 * @target: unused
 * @type: unused
 * @mode: unused
 *
 * Returns 0 if current has read and write access, error code otherwise
 */
static int smack_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
			struct task_struct *target, long type, int mode)
{
	char *msp = smack_of_msq(msq);

	return smk_curacc(msp, MAY_READWRITE);
}

/**
 * smack_ipc_permission - Smack access for ipc_permission()
 * @ipp: the object permissions
 * @flag: access requested
 *
 * Returns 0 if current has read and write access, error code otherwise
 */
static int smack_ipc_permission(struct kern_ipc_perm *ipp, short flag)
{
	char *isp = ipp->security;
	int may;

	may = smack_flags_to_may(flag);
	return smk_curacc(isp, may);
}

/**
 * smack_ipc_getsecid - Extract smack security id
 * @ipcp: the object permissions
 * @secid: where result will be saved
 */
static void smack_ipc_getsecid(struct kern_ipc_perm *ipp, u32 *secid)
{
	char *smack = ipp->security;

	*secid = smack_to_secid(smack);
}

/**
 * smack_d_instantiate - Make sure the blob is correct on an inode
 * @opt_dentry: unused
 * @inode: the object
 *
 * Set the inode's security blob if it hasn't been done already.
 */
static void smack_d_instantiate(struct dentry *opt_dentry, struct inode *inode)
{
	struct super_block *sbp;
	struct superblock_smack *sbsp;
	struct inode_smack *isp;
	char *csp = current_security();
	char *fetched;
	char *final;
	struct dentry *dp;

	if (inode == NULL)
		return;

	isp = inode->i_security;

	mutex_lock(&isp->smk_lock);
	/*
	 * If the inode is already instantiated
	 * take the quick way out
	 */
	if (isp->smk_flags & SMK_INODE_INSTANT)
		goto unlockandout;

	sbp = inode->i_sb;
	sbsp = sbp->s_security;
	/*
	 * We're going to use the superblock default label
	 * if there's no label on the file.
	 */
	final = sbsp->smk_default;

	/*
	 * If this is the root inode the superblock
	 * may be in the process of initialization.
	 * If that is the case use the root value out
	 * of the superblock.
	 */
	if (opt_dentry->d_parent == opt_dentry) {
		isp->smk_inode = sbsp->smk_root;
		isp->smk_flags |= SMK_INODE_INSTANT;
		goto unlockandout;
	}

	/*
	 * This is pretty hackish.
	 * Casey says that we shouldn't have to do
	 * file system specific code, but it does help
	 * with keeping it simple.
	 */
	switch (sbp->s_magic) {
	case SMACK_MAGIC:
		/*
		 * Casey says that it's a little embarassing
		 * that the smack file system doesn't do
		 * extended attributes.
		 */
		final = smack_known_star.smk_known;
		break;
	case PIPEFS_MAGIC:
		/*
		 * Casey says pipes are easy (?)
		 */
		final = smack_known_star.smk_known;
		break;
	case DEVPTS_SUPER_MAGIC:
		/*
		 * devpts seems content with the label of the task.
		 * Programs that change smack have to treat the
		 * pty with respect.
		 */
		final = csp;
		break;
	case SOCKFS_MAGIC:
		/*
		 * Casey says sockets get the smack of the task.
		 */
		final = csp;
		break;
	case PROC_SUPER_MAGIC:
		/*
		 * Casey says procfs appears not to care.
		 * The superblock default suffices.
		 */
		break;
	case TMPFS_MAGIC:
		/*
		 * Device labels should come from the filesystem,
		 * but watch out, because they're volitile,
		 * getting recreated on every reboot.
		 */
		final = smack_known_star.smk_known;
		/*
		 * No break.
		 *
		 * If a smack value has been set we want to use it,
		 * but since tmpfs isn't giving us the opportunity
		 * to set mount options simulate setting the
		 * superblock default.
		 */
	default:
		/*
		 * This isn't an understood special case.
		 * Get the value from the xattr.
		 *
		 * No xattr support means, alas, no SMACK label.
		 * Use the aforeapplied default.
		 * It would be curious if the label of the task
		 * does not match that assigned.
		 */
		if (inode->i_op->getxattr == NULL)
			break;
		/*
		 * Get the dentry for xattr.
		 */
		if (opt_dentry == NULL) {
			dp = d_find_alias(inode);
			if (dp == NULL)
				break;
		} else {
			dp = dget(opt_dentry);
			if (dp == NULL)
				break;
		}

		fetched = smk_fetch(inode, dp);
		if (fetched != NULL)
			final = fetched;

		dput(dp);
		break;
	}

	if (final == NULL)
		isp->smk_inode = csp;
	else
		isp->smk_inode = final;

	isp->smk_flags |= SMK_INODE_INSTANT;

unlockandout:
	mutex_unlock(&isp->smk_lock);
	return;
}

/**
 * smack_getprocattr - Smack process attribute access
 * @p: the object task
 * @name: the name of the attribute in /proc/.../attr
 * @value: where to put the result
 *
 * Places a copy of the task Smack into value
 *
 * Returns the length of the smack label or an error code
 */
static int smack_getprocattr(struct task_struct *p, char *name, char **value)
{
	char *cp;
	int slen;

	if (strcmp(name, "current") != 0)
		return -EINVAL;

	cp = kstrdup(task_security(p), GFP_KERNEL);
	if (cp == NULL)
		return -ENOMEM;

	slen = strlen(cp);
	*value = cp;
	return slen;
}

/**
 * smack_setprocattr - Smack process attribute setting
 * @p: the object task
 * @name: the name of the attribute in /proc/.../attr
 * @value: the value to set
 * @size: the size of the value
 *
 * Sets the Smack value of the task. Only setting self
 * is permitted and only with privilege
 *
 * Returns the length of the smack label or an error code
 */
static int smack_setprocattr(struct task_struct *p, char *name,
			     void *value, size_t size)
{
	struct cred *new;
	char *newsmack;

	/*
	 * Changing another process' Smack value is too dangerous
	 * and supports no sane use case.
	 */
	if (p != current)
		return -EPERM;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (value == NULL || size == 0 || size >= SMK_LABELLEN)
		return -EINVAL;

	if (strcmp(name, "current") != 0)
		return -EINVAL;

	newsmack = smk_import(value, size);
	if (newsmack == NULL)
		return -EINVAL;

	/*
	 * No process is ever allowed the web ("@") label.
	 */
	if (newsmack == smack_known_web.smk_known)
		return -EPERM;

	new = prepare_creds();
	if (new == NULL)
		return -ENOMEM;
	new->security = newsmack;
	commit_creds(new);
	return size;
}

/**
 * smack_unix_stream_connect - Smack access on UDS
 * @sock: one socket
 * @other: the other socket
 * @newsk: unused
 *
 * Return 0 if a subject with the smack of sock could access
 * an object with the smack of other, otherwise an error code
 */
static int smack_unix_stream_connect(struct socket *sock,
				     struct socket *other, struct sock *newsk)
{
	struct inode *sp = SOCK_INODE(sock);
	struct inode *op = SOCK_INODE(other);

	return smk_access(smk_of_inode(sp), smk_of_inode(op), MAY_READWRITE);
}

/**
 * smack_unix_may_send - Smack access on UDS
 * @sock: one socket
 * @other: the other socket
 *
 * Return 0 if a subject with the smack of sock could access
 * an object with the smack of other, otherwise an error code
 */
static int smack_unix_may_send(struct socket *sock, struct socket *other)
{
	struct inode *sp = SOCK_INODE(sock);
	struct inode *op = SOCK_INODE(other);

	return smk_access(smk_of_inode(sp), smk_of_inode(op), MAY_WRITE);
}

/**
 * smack_socket_sendmsg - Smack check based on destination host
 * @sock: the socket
 * @msghdr: the message
 * @size: the size of the message
 *
 * Return 0 if the current subject can write to the destination
 * host. This is only a question if the destination is a single
 * label host.
 */
static int smack_socket_sendmsg(struct socket *sock, struct msghdr *msg,
				int size)
{
	struct sockaddr_in *sip = (struct sockaddr_in *) msg->msg_name;
	struct socket_smack *ssp = sock->sk->sk_security;
	char *hostsp;
	int rc;

	/*
	 * Perfectly reasonable for this to be NULL
	 */
	if (sip == NULL || sip->sin_family != PF_INET)
		return 0;

	hostsp = smack_host_label(sip);
	if (hostsp == NULL) {
		if (ssp->smk_labeled != SMACK_CIPSO_SOCKET)
			return smack_netlabel(sock->sk, SMACK_CIPSO_SOCKET);
		return 0;
	}

	rc = smk_access(ssp->smk_out, hostsp, MAY_WRITE);
	if (rc != 0)
		return rc;

	if (ssp->smk_labeled != SMACK_UNLABELED_SOCKET)
		return smack_netlabel(sock->sk, SMACK_UNLABELED_SOCKET);

	return 0;

}


/**
 * smack_from_secattr - Convert a netlabel attr.mls.lvl/attr.mls.cat
 * 	pair to smack
 * @sap: netlabel secattr
 * @sip: where to put the result
 *
 * Copies a smack label into sip
 */
static void smack_from_secattr(struct netlbl_lsm_secattr *sap, char *sip)
{
	char smack[SMK_LABELLEN];
	char *sp;
	int pcat;

	if ((sap->flags & NETLBL_SECATTR_MLS_LVL) != 0) {
		/*
		 * Looks like a CIPSO packet.
		 * If there are flags but no level netlabel isn't
		 * behaving the way we expect it to.
		 *
		 * Get the categories, if any
		 * Without guidance regarding the smack value
		 * for the packet fall back on the network
		 * ambient value.
		 */
		memset(smack, '\0', SMK_LABELLEN);
		if ((sap->flags & NETLBL_SECATTR_MLS_CAT) != 0)
			for (pcat = -1;;) {
				pcat = netlbl_secattr_catmap_walk(
					sap->attr.mls.cat, pcat + 1);
				if (pcat < 0)
					break;
				smack_catset_bit(pcat, smack);
			}
		/*
		 * If it is CIPSO using smack direct mapping
		 * we are already done. WeeHee.
		 */
		if (sap->attr.mls.lvl == smack_cipso_direct) {
			memcpy(sip, smack, SMK_MAXLEN);
			return;
		}
		/*
		 * Look it up in the supplied table if it is not
		 * a direct mapping.
		 */
		smack_from_cipso(sap->attr.mls.lvl, smack, sip);
		return;
	}
	if ((sap->flags & NETLBL_SECATTR_SECID) != 0) {
		/*
		 * Looks like a fallback, which gives us a secid.
		 */
		sp = smack_from_secid(sap->attr.secid);
		/*
		 * This has got to be a bug because it is
		 * impossible to specify a fallback without
		 * specifying the label, which will ensure
		 * it has a secid, and the only way to get a
		 * secid is from a fallback.
		 */
		BUG_ON(sp == NULL);
		strncpy(sip, sp, SMK_MAXLEN);
		return;
	}
	/*
	 * Without guidance regarding the smack value
	 * for the packet fall back on the network
	 * ambient value.
	 */
	strncpy(sip, smack_net_ambient, SMK_MAXLEN);
	return;
}

/**
 * smack_socket_sock_rcv_skb - Smack packet delivery access check
 * @sk: socket
 * @skb: packet
 *
 * Returns 0 if the packet should be delivered, an error code otherwise
 */
static int smack_socket_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	struct netlbl_lsm_secattr secattr;
	struct socket_smack *ssp = sk->sk_security;
	char smack[SMK_LABELLEN];
	char *csp;
	int rc;

	if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
		return 0;

	/*
	 * Translate what netlabel gave us.
	 */
	netlbl_secattr_init(&secattr);

	rc = netlbl_skbuff_getattr(skb, sk->sk_family, &secattr);
	if (rc == 0) {
		smack_from_secattr(&secattr, smack);
		csp = smack;
	} else
		csp = smack_net_ambient;

	netlbl_secattr_destroy(&secattr);

	/*
	 * Receiving a packet requires that the other end
	 * be able to write here. Read access is not required.
	 * This is the simplist possible security model
	 * for networking.
	 */
	rc = smk_access(csp, ssp->smk_in, MAY_WRITE);
	if (rc != 0)
		netlbl_skbuff_err(skb, rc, 0);
	return rc;
}

/**
 * smack_socket_getpeersec_stream - pull in packet label
 * @sock: the socket
 * @optval: user's destination
 * @optlen: size thereof
 * @len: max thereoe
 *
 * returns zero on success, an error code otherwise
 */
static int smack_socket_getpeersec_stream(struct socket *sock,
					  char __user *optval,
					  int __user *optlen, unsigned len)
{
	struct socket_smack *ssp;
	int slen;
	int rc = 0;

	ssp = sock->sk->sk_security;
	slen = strlen(ssp->smk_packet) + 1;

	if (slen > len)
		rc = -ERANGE;
	else if (copy_to_user(optval, ssp->smk_packet, slen) != 0)
		rc = -EFAULT;

	if (put_user(slen, optlen) != 0)
		rc = -EFAULT;

	return rc;
}


/**
 * smack_socket_getpeersec_dgram - pull in packet label
 * @sock: the socket
 * @skb: packet data
 * @secid: pointer to where to put the secid of the packet
 *
 * Sets the netlabel socket state on sk from parent
 */
static int smack_socket_getpeersec_dgram(struct socket *sock,
					 struct sk_buff *skb, u32 *secid)

{
	struct netlbl_lsm_secattr secattr;
	struct sock *sk;
	char smack[SMK_LABELLEN];
	int family = PF_INET;
	u32 s;
	int rc;

	/*
	 * Only works for families with packets.
	 */
	if (sock != NULL) {
		sk = sock->sk;
		if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
			return 0;
		family = sk->sk_family;
	}
	/*
	 * Translate what netlabel gave us.
	 */
	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, family, &secattr);
	if (rc == 0)
		smack_from_secattr(&secattr, smack);
	netlbl_secattr_destroy(&secattr);

	/*
	 * Give up if we couldn't get anything
	 */
	if (rc != 0)
		return rc;

	s = smack_to_secid(smack);
	if (s == 0)
		return -EINVAL;

	*secid = s;
	return 0;
}

/**
 * smack_sock_graft - graft access state between two sockets
 * @sk: fresh sock
 * @parent: donor socket
 *
 * Sets the netlabel socket state on sk from parent
 */
static void smack_sock_graft(struct sock *sk, struct socket *parent)
{
	struct socket_smack *ssp;
	int rc;

	if (sk == NULL)
		return;

	if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
		return;

	ssp = sk->sk_security;
	ssp->smk_in = ssp->smk_out = current_security();
	ssp->smk_packet[0] = '\0';

	rc = smack_netlabel(sk, SMACK_CIPSO_SOCKET);
	if (rc != 0)
		printk(KERN_WARNING "Smack: \"%s\" netlbl error %d.\n",
		       __func__, -rc);
}

/**
 * smack_inet_conn_request - Smack access check on connect
 * @sk: socket involved
 * @skb: packet
 * @req: unused
 *
 * Returns 0 if a task with the packet label could write to
 * the socket, otherwise an error code
 */
static int smack_inet_conn_request(struct sock *sk, struct sk_buff *skb,
				   struct request_sock *req)
{
	struct netlbl_lsm_secattr skb_secattr;
	struct socket_smack *ssp = sk->sk_security;
	char smack[SMK_LABELLEN];
	int rc;

	if (skb == NULL)
		return -EACCES;

	netlbl_secattr_init(&skb_secattr);
	rc = netlbl_skbuff_getattr(skb, sk->sk_family, &skb_secattr);
	if (rc == 0)
		smack_from_secattr(&skb_secattr, smack);
	else
		strncpy(smack, smack_known_huh.smk_known, SMK_MAXLEN);
	netlbl_secattr_destroy(&skb_secattr);
	/*
	 * Receiving a packet requires that the other end
	 * be able to write here. Read access is not required.
	 *
	 * If the request is successful save the peer's label
	 * so that SO_PEERCRED can report it.
	 */
	rc = smk_access(smack, ssp->smk_in, MAY_WRITE);
	if (rc == 0)
		strncpy(ssp->smk_packet, smack, SMK_MAXLEN);

	return rc;
}

/*
 * Key management security hooks
 *
 * Casey has not tested key support very heavily.
 * The permission check is most likely too restrictive.
 * If you care about keys please have a look.
 */
#ifdef CONFIG_KEYS

/**
 * smack_key_alloc - Set the key security blob
 * @key: object
 * @cred: the credentials to use
 * @flags: unused
 *
 * No allocation required
 *
 * Returns 0
 */
static int smack_key_alloc(struct key *key, const struct cred *cred,
			   unsigned long flags)
{
	key->security = cred->security;
	return 0;
}

/**
 * smack_key_free - Clear the key security blob
 * @key: the object
 *
 * Clear the blob pointer
 */
static void smack_key_free(struct key *key)
{
	key->security = NULL;
}

/*
 * smack_key_permission - Smack access on a key
 * @key_ref: gets to the object
 * @cred: the credentials to use
 * @perm: unused
 *
 * Return 0 if the task has read and write to the object,
 * an error code otherwise
 */
static int smack_key_permission(key_ref_t key_ref,
				const struct cred *cred, key_perm_t perm)
{
	struct key *keyp;

	keyp = key_ref_to_ptr(key_ref);
	if (keyp == NULL)
		return -EINVAL;
	/*
	 * If the key hasn't been initialized give it access so that
	 * it may do so.
	 */
	if (keyp->security == NULL)
		return 0;
	/*
	 * This should not occur
	 */
	if (cred->security == NULL)
		return -EACCES;

	return smk_access(cred->security, keyp->security, MAY_READWRITE);
}
#endif /* CONFIG_KEYS */

/*
 * Smack Audit hooks
 *
 * Audit requires a unique representation of each Smack specific
 * rule. This unique representation is used to distinguish the
 * object to be audited from remaining kernel objects and also
 * works as a glue between the audit hooks.
 *
 * Since repository entries are added but never deleted, we'll use
 * the smack_known label address related to the given audit rule as
 * the needed unique representation. This also better fits the smack
 * model where nearly everything is a label.
 */
#ifdef CONFIG_AUDIT

/**
 * smack_audit_rule_init - Initialize a smack audit rule
 * @field: audit rule fields given from user-space (audit.h)
 * @op: required testing operator (=, !=, >, <, ...)
 * @rulestr: smack label to be audited
 * @vrule: pointer to save our own audit rule representation
 *
 * Prepare to audit cases where (@field @op @rulestr) is true.
 * The label to be audited is created if necessay.
 */
static int smack_audit_rule_init(u32 field, u32 op, char *rulestr, void **vrule)
{
	char **rule = (char **)vrule;
	*rule = NULL;

	if (field != AUDIT_SUBJ_USER && field != AUDIT_OBJ_USER)
		return -EINVAL;

	if (op != Audit_equal && op != Audit_not_equal)
		return -EINVAL;

	*rule = smk_import(rulestr, 0);

	return 0;
}

/**
 * smack_audit_rule_known - Distinguish Smack audit rules
 * @krule: rule of interest, in Audit kernel representation format
 *
 * This is used to filter Smack rules from remaining Audit ones.
 * If it's proved that this rule belongs to us, the
 * audit_rule_match hook will be called to do the final judgement.
 */
static int smack_audit_rule_known(struct audit_krule *krule)
{
	struct audit_field *f;
	int i;

	for (i = 0; i < krule->field_count; i++) {
		f = &krule->fields[i];

		if (f->type == AUDIT_SUBJ_USER || f->type == AUDIT_OBJ_USER)
			return 1;
	}

	return 0;
}

/**
 * smack_audit_rule_match - Audit given object ?
 * @secid: security id for identifying the object to test
 * @field: audit rule flags given from user-space
 * @op: required testing operator
 * @vrule: smack internal rule presentation
 * @actx: audit context associated with the check
 *
 * The core Audit hook. It's used to take the decision of
 * whether to audit or not to audit a given object.
 */
static int smack_audit_rule_match(u32 secid, u32 field, u32 op, void *vrule,
				  struct audit_context *actx)
{
	char *smack;
	char *rule = vrule;

	if (!rule) {
		audit_log(actx, GFP_KERNEL, AUDIT_SELINUX_ERR,
			  "Smack: missing rule\n");
		return -ENOENT;
	}

	if (field != AUDIT_SUBJ_USER && field != AUDIT_OBJ_USER)
		return 0;

	smack = smack_from_secid(secid);

	/*
	 * No need to do string comparisons. If a match occurs,
	 * both pointers will point to the same smack_known
	 * label.
	 */
	if (op == Audit_equal)
		return (rule == smack);
	if (op == Audit_not_equal)
		return (rule != smack);

	return 0;
}

/**
 * smack_audit_rule_free - free smack rule representation
 * @vrule: rule to be freed.
 *
 * No memory was allocated.
 */
static void smack_audit_rule_free(void *vrule)
{
	/* No-op */
}

#endif /* CONFIG_AUDIT */

/*
 * smack_secid_to_secctx - return the smack label for a secid
 * @secid: incoming integer
 * @secdata: destination
 * @seclen: how long it is
 *
 * Exists for networking code.
 */
static int smack_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	char *sp = smack_from_secid(secid);

	*secdata = sp;
	*seclen = strlen(sp);
	return 0;
}

/*
 * smack_secctx_to_secid - return the secid for a smack label
 * @secdata: smack label
 * @seclen: how long result is
 * @secid: outgoing integer
 *
 * Exists for audit and networking code.
 */
static int smack_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	*secid = smack_to_secid(secdata);
	return 0;
}

/*
 * smack_release_secctx - don't do anything.
 * @key_ref: unused
 * @context: unused
 * @perm: unused
 *
 * Exists to make sure nothing gets done, and properly
 */
static void smack_release_secctx(char *secdata, u32 seclen)
{
}

struct security_operations smack_ops = {
	.name =				"smack",

	.ptrace_may_access =		smack_ptrace_may_access,
	.ptrace_traceme =		smack_ptrace_traceme,
	.capget = 			cap_capget,
	.capset = 			cap_capset,
	.capable = 			cap_capable,
	.syslog = 			smack_syslog,
	.settime = 			cap_settime,
	.vm_enough_memory = 		cap_vm_enough_memory,

	.bprm_set_creds = 		cap_bprm_set_creds,
	.bprm_secureexec = 		cap_bprm_secureexec,

	.sb_alloc_security = 		smack_sb_alloc_security,
	.sb_free_security = 		smack_sb_free_security,
	.sb_copy_data = 		smack_sb_copy_data,
	.sb_kern_mount = 		smack_sb_kern_mount,
	.sb_statfs = 			smack_sb_statfs,
	.sb_mount = 			smack_sb_mount,
	.sb_umount = 			smack_sb_umount,

	.inode_alloc_security = 	smack_inode_alloc_security,
	.inode_free_security = 		smack_inode_free_security,
	.inode_init_security = 		smack_inode_init_security,
	.inode_link = 			smack_inode_link,
	.inode_unlink = 		smack_inode_unlink,
	.inode_rmdir = 			smack_inode_rmdir,
	.inode_rename = 		smack_inode_rename,
	.inode_permission = 		smack_inode_permission,
	.inode_setattr = 		smack_inode_setattr,
	.inode_getattr = 		smack_inode_getattr,
	.inode_setxattr = 		smack_inode_setxattr,
	.inode_post_setxattr = 		smack_inode_post_setxattr,
	.inode_getxattr = 		smack_inode_getxattr,
	.inode_removexattr = 		smack_inode_removexattr,
	.inode_need_killpriv =		cap_inode_need_killpriv,
	.inode_killpriv =		cap_inode_killpriv,
	.inode_getsecurity = 		smack_inode_getsecurity,
	.inode_setsecurity = 		smack_inode_setsecurity,
	.inode_listsecurity = 		smack_inode_listsecurity,
	.inode_getsecid =		smack_inode_getsecid,

	.file_permission = 		smack_file_permission,
	.file_alloc_security = 		smack_file_alloc_security,
	.file_free_security = 		smack_file_free_security,
	.file_ioctl = 			smack_file_ioctl,
	.file_lock = 			smack_file_lock,
	.file_fcntl = 			smack_file_fcntl,
	.file_set_fowner = 		smack_file_set_fowner,
	.file_send_sigiotask = 		smack_file_send_sigiotask,
	.file_receive = 		smack_file_receive,

	.cred_free =			smack_cred_free,
	.cred_prepare =			smack_cred_prepare,
	.cred_commit =			smack_cred_commit,
	.kernel_act_as =		smack_kernel_act_as,
	.kernel_create_files_as =	smack_kernel_create_files_as,
	.task_fix_setuid =		cap_task_fix_setuid,
	.task_setpgid = 		smack_task_setpgid,
	.task_getpgid = 		smack_task_getpgid,
	.task_getsid = 			smack_task_getsid,
	.task_getsecid = 		smack_task_getsecid,
	.task_setnice = 		smack_task_setnice,
	.task_setioprio = 		smack_task_setioprio,
	.task_getioprio = 		smack_task_getioprio,
	.task_setscheduler = 		smack_task_setscheduler,
	.task_getscheduler = 		smack_task_getscheduler,
	.task_movememory = 		smack_task_movememory,
	.task_kill = 			smack_task_kill,
	.task_wait = 			smack_task_wait,
	.task_to_inode = 		smack_task_to_inode,
	.task_prctl =			cap_task_prctl,

	.ipc_permission = 		smack_ipc_permission,
	.ipc_getsecid =			smack_ipc_getsecid,

	.msg_msg_alloc_security = 	smack_msg_msg_alloc_security,
	.msg_msg_free_security = 	smack_msg_msg_free_security,

	.msg_queue_alloc_security = 	smack_msg_queue_alloc_security,
	.msg_queue_free_security = 	smack_msg_queue_free_security,
	.msg_queue_associate = 		smack_msg_queue_associate,
	.msg_queue_msgctl = 		smack_msg_queue_msgctl,
	.msg_queue_msgsnd = 		smack_msg_queue_msgsnd,
	.msg_queue_msgrcv = 		smack_msg_queue_msgrcv,

	.shm_alloc_security = 		smack_shm_alloc_security,
	.shm_free_security = 		smack_shm_free_security,
	.shm_associate = 		smack_shm_associate,
	.shm_shmctl = 			smack_shm_shmctl,
	.shm_shmat = 			smack_shm_shmat,

	.sem_alloc_security = 		smack_sem_alloc_security,
	.sem_free_security = 		smack_sem_free_security,
	.sem_associate = 		smack_sem_associate,
	.sem_semctl = 			smack_sem_semctl,
	.sem_semop = 			smack_sem_semop,

	.netlink_send =			cap_netlink_send,
	.netlink_recv = 		cap_netlink_recv,

	.d_instantiate = 		smack_d_instantiate,

	.getprocattr = 			smack_getprocattr,
	.setprocattr = 			smack_setprocattr,

	.unix_stream_connect = 		smack_unix_stream_connect,
	.unix_may_send = 		smack_unix_may_send,

	.socket_post_create = 		smack_socket_post_create,
	.socket_connect =		smack_socket_connect,
	.socket_sendmsg =		smack_socket_sendmsg,
	.socket_sock_rcv_skb = 		smack_socket_sock_rcv_skb,
	.socket_getpeersec_stream =	smack_socket_getpeersec_stream,
	.socket_getpeersec_dgram =	smack_socket_getpeersec_dgram,
	.sk_alloc_security = 		smack_sk_alloc_security,
	.sk_free_security = 		smack_sk_free_security,
	.sock_graft = 			smack_sock_graft,
	.inet_conn_request = 		smack_inet_conn_request,

 /* key management security hooks */
#ifdef CONFIG_KEYS
	.key_alloc = 			smack_key_alloc,
	.key_free = 			smack_key_free,
	.key_permission = 		smack_key_permission,
#endif /* CONFIG_KEYS */

 /* Audit hooks */
#ifdef CONFIG_AUDIT
	.audit_rule_init =		smack_audit_rule_init,
	.audit_rule_known =		smack_audit_rule_known,
	.audit_rule_match =		smack_audit_rule_match,
	.audit_rule_free =		smack_audit_rule_free,
#endif /* CONFIG_AUDIT */

	.secid_to_secctx = 		smack_secid_to_secctx,
	.secctx_to_secid = 		smack_secctx_to_secid,
	.release_secctx = 		smack_release_secctx,
};

/**
 * smack_init - initialize the smack system
 *
 * Returns 0
 */
static __init int smack_init(void)
{
	struct cred *cred;

	if (!security_module_enable(&smack_ops))
		return 0;

	printk(KERN_INFO "Smack:  Initializing.\n");

	/*
	 * Set the security state for the initial task.
	 */
	cred = (struct cred *) current->cred;
	cred->security = &smack_known_floor.smk_known;

	/*
	 * Initialize locks
	 */
	spin_lock_init(&smack_known_huh.smk_cipsolock);
	spin_lock_init(&smack_known_hat.smk_cipsolock);
	spin_lock_init(&smack_known_star.smk_cipsolock);
	spin_lock_init(&smack_known_floor.smk_cipsolock);
	spin_lock_init(&smack_known_invalid.smk_cipsolock);

	/*
	 * Register with LSM
	 */
	if (register_security(&smack_ops))
		panic("smack: Unable to register with kernel.\n");

	return 0;
}

/*
 * Smack requires early initialization in order to label
 * all processes and objects when they are created.
 */
security_initcall(smack_init);
