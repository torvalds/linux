/*
 *  Simplified MAC Kernel (smack) security module
 *
 *  This file contains the smack hook function implementations.
 *
 *  Authors:
 *	Casey Schaufler <casey@schaufler-ca.com>
 *	Jarkko Sakkinen <jarkko.sakkinen@intel.com>
 *
 *  Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *  Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *                Paul Moore <paul@paul-moore.com>
 *  Copyright (C) 2010 Nokia Corporation
 *  Copyright (C) 2011 Intel Corporation.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */

#include <linux/xattr.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <linux/kd.h>
#include <asm/ioctls.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/pipe_fs_i.h>
#include <net/cipso_ipv4.h>
#include <linux/audit.h>
#include <linux/magic.h>
#include <linux/dcache.h>
#include <linux/personality.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/binfmts.h>
#include "smack.h"

#define task_security(task)	(task_cred_xxx((task), security))

#define TRANS_TRUE	"TRUE"
#define TRANS_TRUE_SIZE	4

/**
 * smk_fetch - Fetch the smack label from a file.
 * @ip: a pointer to the inode
 * @dp: a pointer to the dentry
 *
 * Returns a pointer to the master list entry for the Smack label
 * or NULL if there was no label to fetch.
 */
static char *smk_fetch(const char *name, struct inode *ip, struct dentry *dp)
{
	int rc;
	char *buffer;
	char *result = NULL;

	if (ip->i_op->getxattr == NULL)
		return NULL;

	buffer = kzalloc(SMK_LONGLABEL, GFP_KERNEL);
	if (buffer == NULL)
		return NULL;

	rc = ip->i_op->getxattr(dp, name, buffer, SMK_LONGLABEL);
	if (rc > 0)
		result = smk_import(buffer, rc);

	kfree(buffer);

	return result;
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

	isp = kzalloc(sizeof(struct inode_smack), GFP_NOFS);
	if (isp == NULL)
		return NULL;

	isp->smk_inode = smack;
	isp->smk_flags = 0;
	mutex_init(&isp->smk_lock);

	return isp;
}

/**
 * new_task_smack - allocate a task security blob
 * @smack: a pointer to the Smack label to use in the blob
 *
 * Returns the new blob or NULL if there's no memory available
 */
static struct task_smack *new_task_smack(char *task, char *forked, gfp_t gfp)
{
	struct task_smack *tsp;

	tsp = kzalloc(sizeof(struct task_smack), gfp);
	if (tsp == NULL)
		return NULL;

	tsp->smk_task = task;
	tsp->smk_forked = forked;
	INIT_LIST_HEAD(&tsp->smk_rules);
	mutex_init(&tsp->smk_rules_lock);

	return tsp;
}

/**
 * smk_copy_rules - copy a rule set
 * @nhead - new rules header pointer
 * @ohead - old rules header pointer
 *
 * Returns 0 on success, -ENOMEM on error
 */
static int smk_copy_rules(struct list_head *nhead, struct list_head *ohead,
				gfp_t gfp)
{
	struct smack_rule *nrp;
	struct smack_rule *orp;
	int rc = 0;

	INIT_LIST_HEAD(nhead);

	list_for_each_entry_rcu(orp, ohead, list) {
		nrp = kzalloc(sizeof(struct smack_rule), gfp);
		if (nrp == NULL) {
			rc = -ENOMEM;
			break;
		}
		*nrp = *orp;
		list_add_rcu(&nrp->list, nhead);
	}
	return rc;
}

/*
 * LSM hooks.
 * We he, that is fun!
 */

/**
 * smack_ptrace_access_check - Smack approval on PTRACE_ATTACH
 * @ctp: child task pointer
 * @mode: ptrace attachment mode
 *
 * Returns 0 if access is OK, an error code otherwise
 *
 * Do the capability checks, and require read and write.
 */
static int smack_ptrace_access_check(struct task_struct *ctp, unsigned int mode)
{
	int rc;
	struct smk_audit_info ad;
	char *tsp;

	rc = cap_ptrace_access_check(ctp, mode);
	if (rc != 0)
		return rc;

	tsp = smk_of_task(task_security(ctp));
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, ctp);

	rc = smk_curacc(tsp, MAY_READWRITE, &ad);
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
	struct smk_audit_info ad;
	char *tsp;

	rc = cap_ptrace_traceme(ptp);
	if (rc != 0)
		return rc;

	tsp = smk_of_task(task_security(ptp));
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, ptp);

	rc = smk_curacc(tsp, MAY_READWRITE, &ad);
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
static int smack_syslog(int typefrom_file)
{
	int rc = 0;
	char *sp = smk_of_current();

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
 * @orig: where to start
 * @smackopts: mount options string
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
	int rc;
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	rc = smk_curacc(sbp->smk_floor, MAY_READ, &ad);
	return rc;
}

/**
 * smack_sb_mount - Smack check for mounting
 * @dev_name: unused
 * @path: mount point
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
	struct superblock_smack *sbp = path->dentry->d_sb->s_security;
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
	smk_ad_setfield_u_fs_path(&ad, *path);

	return smk_curacc(sbp->smk_floor, MAY_WRITE, &ad);
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
	struct smk_audit_info ad;
	struct path path;

	path.dentry = mnt->mnt_root;
	path.mnt = mnt;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
	smk_ad_setfield_u_fs_path(&ad, path);

	sbp = path.dentry->d_sb->s_security;
	return smk_curacc(sbp->smk_floor, MAY_WRITE, &ad);
}

/*
 * BPRM hooks
 */

/**
 * smack_bprm_set_creds - set creds for exec
 * @bprm: the exec information
 *
 * Returns 0 if it gets a blob, -ENOMEM otherwise
 */
static int smack_bprm_set_creds(struct linux_binprm *bprm)
{
	struct inode *inode = bprm->file->f_path.dentry->d_inode;
	struct task_smack *bsp = bprm->cred->security;
	struct inode_smack *isp;
	int rc;

	rc = cap_bprm_set_creds(bprm);
	if (rc != 0)
		return rc;

	if (bprm->cred_prepared)
		return 0;

	isp = inode->i_security;
	if (isp->smk_task == NULL || isp->smk_task == bsp->smk_task)
		return 0;

	if (bprm->unsafe)
		return -EPERM;

	bsp->smk_task = isp->smk_task;
	bprm->per_clear |= PER_CLEAR_ON_SETID;

	return 0;
}

/**
 * smack_bprm_committing_creds - Prepare to install the new credentials
 * from bprm.
 *
 * @bprm: binprm for exec
 */
static void smack_bprm_committing_creds(struct linux_binprm *bprm)
{
	struct task_smack *bsp = bprm->cred->security;

	if (bsp->smk_task != bsp->smk_forked)
		current->pdeath_signal = 0;
}

/**
 * smack_bprm_secureexec - Return the decision to use secureexec.
 * @bprm: binprm for exec
 *
 * Returns 0 on success.
 */
static int smack_bprm_secureexec(struct linux_binprm *bprm)
{
	struct task_smack *tsp = current_security();
	int ret = cap_bprm_secureexec(bprm);

	if (!ret && (tsp->smk_task != tsp->smk_forked))
		ret = 1;

	return ret;
}

/*
 * Inode hooks
 */

/**
 * smack_inode_alloc_security - allocate an inode blob
 * @inode: the inode in need of a blob
 *
 * Returns 0 if it gets a blob, -ENOMEM otherwise
 */
static int smack_inode_alloc_security(struct inode *inode)
{
	inode->i_security = new_inode_smack(smk_of_current());
	if (inode->i_security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * smack_inode_free_security - free an inode blob
 * @inode: the inode with a blob
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
 * @qstr: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * Returns 0 if it all works out, -ENOMEM if there's no memory
 */
static int smack_inode_init_security(struct inode *inode, struct inode *dir,
				     const struct qstr *qstr, char **name,
				     void **value, size_t *len)
{
	struct smack_known *skp;
	struct inode_smack *issp = inode->i_security;
	char *csp = smk_of_current();
	char *isp = smk_of_inode(inode);
	char *dsp = smk_of_inode(dir);
	int may;

	if (name) {
		*name = kstrdup(XATTR_SMACK_SUFFIX, GFP_NOFS);
		if (*name == NULL)
			return -ENOMEM;
	}

	if (value) {
		skp = smk_find_entry(csp);
		rcu_read_lock();
		may = smk_access_entry(csp, dsp, &skp->smk_rules);
		rcu_read_unlock();

		/*
		 * If the access rule allows transmutation and
		 * the directory requests transmutation then
		 * by all means transmute.
		 * Mark the inode as changed.
		 */
		if (may > 0 && ((may & MAY_TRANSMUTE) != 0) &&
		    smk_inode_transmutable(dir)) {
			isp = dsp;
			issp->smk_flags |= SMK_INODE_CHANGED;
		}

		*value = kstrdup(isp, GFP_NOFS);
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
	char *isp;
	struct smk_audit_info ad;
	int rc;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, old_dentry);

	isp = smk_of_inode(old_dentry->d_inode);
	rc = smk_curacc(isp, MAY_WRITE, &ad);

	if (rc == 0 && new_dentry->d_inode != NULL) {
		isp = smk_of_inode(new_dentry->d_inode);
		smk_ad_setfield_u_fs_path_dentry(&ad, new_dentry);
		rc = smk_curacc(isp, MAY_WRITE, &ad);
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
	struct smk_audit_info ad;
	int rc;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	/*
	 * You need write access to the thing you're unlinking
	 */
	rc = smk_curacc(smk_of_inode(ip), MAY_WRITE, &ad);
	if (rc == 0) {
		/*
		 * You also need write access to the containing directory
		 */
		smk_ad_setfield_u_fs_path_dentry(&ad, NULL);
		smk_ad_setfield_u_fs_inode(&ad, dir);
		rc = smk_curacc(smk_of_inode(dir), MAY_WRITE, &ad);
	}
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
	struct smk_audit_info ad;
	int rc;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	/*
	 * You need write access to the thing you're removing
	 */
	rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE, &ad);
	if (rc == 0) {
		/*
		 * You also need write access to the containing directory
		 */
		smk_ad_setfield_u_fs_path_dentry(&ad, NULL);
		smk_ad_setfield_u_fs_inode(&ad, dir);
		rc = smk_curacc(smk_of_inode(dir), MAY_WRITE, &ad);
	}

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
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, old_dentry);

	isp = smk_of_inode(old_dentry->d_inode);
	rc = smk_curacc(isp, MAY_READWRITE, &ad);

	if (rc == 0 && new_dentry->d_inode != NULL) {
		isp = smk_of_inode(new_dentry->d_inode);
		smk_ad_setfield_u_fs_path_dentry(&ad, new_dentry);
		rc = smk_curacc(isp, MAY_READWRITE, &ad);
	}
	return rc;
}

/**
 * smack_inode_permission - Smack version of permission()
 * @inode: the inode in question
 * @mask: the access requested
 *
 * This is the important Smack hook.
 *
 * Returns 0 if access is permitted, -EACCES otherwise
 */
static int smack_inode_permission(struct inode *inode, int mask)
{
	struct smk_audit_info ad;
	int no_block = mask & MAY_NOT_BLOCK;

	mask &= (MAY_READ|MAY_WRITE|MAY_EXEC|MAY_APPEND);
	/*
	 * No permission to check. Existence test. Yup, it's there.
	 */
	if (mask == 0)
		return 0;

	/* May be droppable after audit */
	if (no_block)
		return -ECHILD;
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_INODE);
	smk_ad_setfield_u_fs_inode(&ad, inode);
	return smk_curacc(smk_of_inode(inode), mask, &ad);
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
	struct smk_audit_info ad;
	/*
	 * Need to allow for clearing the setuid bit.
	 */
	if (iattr->ia_valid & ATTR_FORCE)
		return 0;
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE, &ad);
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
	struct smk_audit_info ad;
	struct path path;

	path.dentry = dentry;
	path.mnt = mnt;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
	smk_ad_setfield_u_fs_path(&ad, path);
	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_READ, &ad);
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
	struct smk_audit_info ad;
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SMACK) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPIN) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPOUT) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKEXEC) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKMMAP) == 0) {
		if (!capable(CAP_MAC_ADMIN))
			rc = -EPERM;
		/*
		 * check label validity here so import wont fail on
		 * post_setxattr
		 */
		if (size == 0 || size >= SMK_LONGLABEL ||
		    smk_import(value, size) == NULL)
			rc = -EINVAL;
	} else if (strcmp(name, XATTR_NAME_SMACKTRANSMUTE) == 0) {
		if (!capable(CAP_MAC_ADMIN))
			rc = -EPERM;
		if (size != TRANS_TRUE_SIZE ||
		    strncmp(value, TRANS_TRUE, TRANS_TRUE_SIZE) != 0)
			rc = -EINVAL;
	} else
		rc = cap_inode_setxattr(dentry, name, value, size, flags);

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	if (rc == 0)
		rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE, &ad);

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
	char *nsp;
	struct inode_smack *isp = dentry->d_inode->i_security;

	if (strcmp(name, XATTR_NAME_SMACK) == 0) {
		nsp = smk_import(value, size);
		if (nsp != NULL)
			isp->smk_inode = nsp;
		else
			isp->smk_inode = smack_known_invalid.smk_known;
	} else if (strcmp(name, XATTR_NAME_SMACKEXEC) == 0) {
		nsp = smk_import(value, size);
		if (nsp != NULL)
			isp->smk_task = nsp;
		else
			isp->smk_task = smack_known_invalid.smk_known;
	} else if (strcmp(name, XATTR_NAME_SMACKMMAP) == 0) {
		nsp = smk_import(value, size);
		if (nsp != NULL)
			isp->smk_mmap = nsp;
		else
			isp->smk_mmap = smack_known_invalid.smk_known;
	} else if (strcmp(name, XATTR_NAME_SMACKTRANSMUTE) == 0)
		isp->smk_flags |= SMK_INODE_TRANSMUTE;

	return;
}

/**
 * smack_inode_getxattr - Smack check on getxattr
 * @dentry: the object
 * @name: unused
 *
 * Returns 0 if access is permitted, an error code otherwise
 */
static int smack_inode_getxattr(struct dentry *dentry, const char *name)
{
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);

	return smk_curacc(smk_of_inode(dentry->d_inode), MAY_READ, &ad);
}

/**
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
	struct inode_smack *isp;
	struct smk_audit_info ad;
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SMACK) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPIN) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKIPOUT) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKEXEC) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKTRANSMUTE) == 0 ||
	    strcmp(name, XATTR_NAME_SMACKMMAP)) {
		if (!capable(CAP_MAC_ADMIN))
			rc = -EPERM;
	} else
		rc = cap_inode_removexattr(dentry, name);

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_DENTRY);
	smk_ad_setfield_u_fs_path_dentry(&ad, dentry);
	if (rc == 0)
		rc = smk_curacc(smk_of_inode(dentry->d_inode), MAY_WRITE, &ad);

	if (rc == 0) {
		isp = dentry->d_inode->i_security;
		isp->smk_task = NULL;
		isp->smk_mmap = NULL;
	}

	return rc;
}

/**
 * smack_inode_getsecurity - get smack xattrs
 * @inode: the object
 * @name: attribute name
 * @buffer: where to put the result
 * @alloc: unused
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
	file->f_security = smk_of_current();
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
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
	smk_ad_setfield_u_fs_path(&ad, file->f_path);

	if (_IOC_DIR(cmd) & _IOC_WRITE)
		rc = smk_curacc(file->f_security, MAY_WRITE, &ad);

	if (rc == 0 && (_IOC_DIR(cmd) & _IOC_READ))
		rc = smk_curacc(file->f_security, MAY_READ, &ad);

	return rc;
}

/**
 * smack_file_lock - Smack check on file locking
 * @file: the object
 * @cmd: unused
 *
 * Returns 0 if current has write access, error code otherwise
 */
static int smack_file_lock(struct file *file, unsigned int cmd)
{
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
	smk_ad_setfield_u_fs_path(&ad, file->f_path);
	return smk_curacc(file->f_security, MAY_WRITE, &ad);
}

/**
 * smack_file_fcntl - Smack check on fcntl
 * @file: the object
 * @cmd: what action to check
 * @arg: unused
 *
 * Generally these operations are harmless.
 * File locking operations present an obvious mechanism
 * for passing information, so they require write access.
 *
 * Returns 0 if current has access, error code otherwise
 */
static int smack_file_fcntl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct smk_audit_info ad;
	int rc = 0;


	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
	case F_SETOWN:
	case F_SETSIG:
		smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_PATH);
		smk_ad_setfield_u_fs_path(&ad, file->f_path);
		rc = smk_curacc(file->f_security, MAY_WRITE, &ad);
		break;
	default:
		break;
	}

	return rc;
}

/**
 * smack_file_mmap :
 * Check permissions for a mmap operation.  The @file may be NULL, e.g.
 * if mapping anonymous memory.
 * @file contains the file structure for file to map (may be NULL).
 * @reqprot contains the protection requested by the application.
 * @prot contains the protection that will be applied by the kernel.
 * @flags contains the operational flags.
 * Return 0 if permission is granted.
 */
static int smack_file_mmap(struct file *file,
			   unsigned long reqprot, unsigned long prot,
			   unsigned long flags, unsigned long addr,
			   unsigned long addr_only)
{
	struct smack_known *skp;
	struct smack_rule *srp;
	struct task_smack *tsp;
	char *sp;
	char *msmack;
	char *osmack;
	struct inode_smack *isp;
	struct dentry *dp;
	int may;
	int mmay;
	int tmay;
	int rc;

	/* do DAC check on address space usage */
	rc = cap_file_mmap(file, reqprot, prot, flags, addr, addr_only);
	if (rc || addr_only)
		return rc;

	if (file == NULL || file->f_dentry == NULL)
		return 0;

	dp = file->f_dentry;

	if (dp->d_inode == NULL)
		return 0;

	isp = dp->d_inode->i_security;
	if (isp->smk_mmap == NULL)
		return 0;
	msmack = isp->smk_mmap;

	tsp = current_security();
	sp = smk_of_current();
	skp = smk_find_entry(sp);
	rc = 0;

	rcu_read_lock();
	/*
	 * For each Smack rule associated with the subject
	 * label verify that the SMACK64MMAP also has access
	 * to that rule's object label.
	 */
	list_for_each_entry_rcu(srp, &skp->smk_rules, list) {
		osmack = srp->smk_object;
		/*
		 * Matching labels always allows access.
		 */
		if (msmack == osmack)
			continue;
		/*
		 * If there is a matching local rule take
		 * that into account as well.
		 */
		may = smk_access_entry(srp->smk_subject, osmack,
					&tsp->smk_rules);
		if (may == -ENOENT)
			may = srp->smk_access;
		else
			may &= srp->smk_access;
		/*
		 * If may is zero the SMACK64MMAP subject can't
		 * possibly have less access.
		 */
		if (may == 0)
			continue;

		/*
		 * Fetch the global list entry.
		 * If there isn't one a SMACK64MMAP subject
		 * can't have as much access as current.
		 */
		skp = smk_find_entry(msmack);
		mmay = smk_access_entry(msmack, osmack, &skp->smk_rules);
		if (mmay == -ENOENT) {
			rc = -EACCES;
			break;
		}
		/*
		 * If there is a local entry it modifies the
		 * potential access, too.
		 */
		tmay = smk_access_entry(msmack, osmack, &tsp->smk_rules);
		if (tmay != -ENOENT)
			mmay &= tmay;

		/*
		 * If there is any access available to current that is
		 * not available to a SMACK64MMAP subject
		 * deny access.
		 */
		if ((may | mmay) != mmay) {
			rc = -EACCES;
			break;
		}
	}

	rcu_read_unlock();

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
	file->f_security = smk_of_current();
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
	char *tsp = smk_of_task(tsk->cred->security);
	struct smk_audit_info ad;

	/*
	 * struct fown_struct is never outside the context of a struct file
	 */
	file = container_of(fown, struct file, f_owner);

	/* we don't log here as rc can be overriden */
	rc = smk_access(file->f_security, tsp, MAY_WRITE, NULL);
	if (rc != 0 && has_capability(tsk, CAP_MAC_OVERRIDE))
		rc = 0;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, tsk);
	smack_log(file->f_security, tsp, MAY_WRITE, rc, &ad);
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
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_fs_path(&ad, file->f_path);
	/*
	 * This code relies on bitmasks.
	 */
	if (file->f_mode & FMODE_READ)
		may = MAY_READ;
	if (file->f_mode & FMODE_WRITE)
		may |= MAY_WRITE;

	return smk_curacc(file->f_security, may, &ad);
}

/**
 * smack_dentry_open - Smack dentry open processing
 * @file: the object
 * @cred: unused
 *
 * Set the security blob in the file structure.
 *
 * Returns 0
 */
static int smack_dentry_open(struct file *file, const struct cred *cred)
{
	struct inode_smack *isp = file->f_path.dentry->d_inode->i_security;

	file->f_security = isp->smk_inode;

	return 0;
}

/*
 * Task hooks
 */

/**
 * smack_cred_alloc_blank - "allocate" blank task-level security credentials
 * @new: the new credentials
 * @gfp: the atomicity of any memory allocations
 *
 * Prepare a blank set of credentials for modification.  This must allocate all
 * the memory the LSM module might require such that cred_transfer() can
 * complete without error.
 */
static int smack_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	struct task_smack *tsp;

	tsp = new_task_smack(NULL, NULL, gfp);
	if (tsp == NULL)
		return -ENOMEM;

	cred->security = tsp;

	return 0;
}


/**
 * smack_cred_free - "free" task-level security credentials
 * @cred: the credentials in question
 *
 */
static void smack_cred_free(struct cred *cred)
{
	struct task_smack *tsp = cred->security;
	struct smack_rule *rp;
	struct list_head *l;
	struct list_head *n;

	if (tsp == NULL)
		return;
	cred->security = NULL;

	list_for_each_safe(l, n, &tsp->smk_rules) {
		rp = list_entry(l, struct smack_rule, list);
		list_del(&rp->list);
		kfree(rp);
	}
	kfree(tsp);
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
	struct task_smack *old_tsp = old->security;
	struct task_smack *new_tsp;
	int rc;

	new_tsp = new_task_smack(old_tsp->smk_task, old_tsp->smk_task, gfp);
	if (new_tsp == NULL)
		return -ENOMEM;

	rc = smk_copy_rules(&new_tsp->smk_rules, &old_tsp->smk_rules, gfp);
	if (rc != 0)
		return rc;

	new->security = new_tsp;
	return 0;
}

/**
 * smack_cred_transfer - Transfer the old credentials to the new credentials
 * @new: the new credentials
 * @old: the original credentials
 *
 * Fill in a set of blank credentials from another set of credentials.
 */
static void smack_cred_transfer(struct cred *new, const struct cred *old)
{
	struct task_smack *old_tsp = old->security;
	struct task_smack *new_tsp = new->security;

	new_tsp->smk_task = old_tsp->smk_task;
	new_tsp->smk_forked = old_tsp->smk_task;
	mutex_init(&new_tsp->smk_rules_lock);
	INIT_LIST_HEAD(&new_tsp->smk_rules);


	/* cbs copy rule list */
}

/**
 * smack_kernel_act_as - Set the subjective context in a set of credentials
 * @new: points to the set of credentials to be modified.
 * @secid: specifies the security ID to be set
 *
 * Set the security data for a kernel service.
 */
static int smack_kernel_act_as(struct cred *new, u32 secid)
{
	struct task_smack *new_tsp = new->security;
	char *smack = smack_from_secid(secid);

	if (smack == NULL)
		return -EINVAL;

	new_tsp->smk_task = smack;
	return 0;
}

/**
 * smack_kernel_create_files_as - Set the file creation label in a set of creds
 * @new: points to the set of credentials to be modified
 * @inode: points to the inode to use as a reference
 *
 * Set the file creation context in a set of credentials to the same
 * as the objective context of the specified inode
 */
static int smack_kernel_create_files_as(struct cred *new,
					struct inode *inode)
{
	struct inode_smack *isp = inode->i_security;
	struct task_smack *tsp = new->security;

	tsp->smk_forked = isp->smk_inode;
	tsp->smk_task = isp->smk_inode;
	return 0;
}

/**
 * smk_curacc_on_task - helper to log task related access
 * @p: the task object
 * @access: the access requested
 * @caller: name of the calling function for audit
 *
 * Return 0 if access is permitted
 */
static int smk_curacc_on_task(struct task_struct *p, int access,
				const char *caller)
{
	struct smk_audit_info ad;

	smk_ad_init(&ad, caller, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, p);
	return smk_curacc(smk_of_task(task_security(p)), access, &ad);
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
	return smk_curacc_on_task(p, MAY_WRITE, __func__);
}

/**
 * smack_task_getpgid - Smack access check for getpgid
 * @p: the object task
 *
 * Returns 0 if current can read the object task, error code otherwise
 */
static int smack_task_getpgid(struct task_struct *p)
{
	return smk_curacc_on_task(p, MAY_READ, __func__);
}

/**
 * smack_task_getsid - Smack access check for getsid
 * @p: the object task
 *
 * Returns 0 if current can read the object task, error code otherwise
 */
static int smack_task_getsid(struct task_struct *p)
{
	return smk_curacc_on_task(p, MAY_READ, __func__);
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
	*secid = smack_to_secid(smk_of_task(task_security(p)));
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
		rc = smk_curacc_on_task(p, MAY_WRITE, __func__);
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
		rc = smk_curacc_on_task(p, MAY_WRITE, __func__);
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
	return smk_curacc_on_task(p, MAY_READ, __func__);
}

/**
 * smack_task_setscheduler - Smack check on setting scheduler
 * @p: the task object
 * @policy: unused
 * @lp: unused
 *
 * Return 0 if read access is permitted
 */
static int smack_task_setscheduler(struct task_struct *p)
{
	int rc;

	rc = cap_task_setscheduler(p);
	if (rc == 0)
		rc = smk_curacc_on_task(p, MAY_WRITE, __func__);
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
	return smk_curacc_on_task(p, MAY_READ, __func__);
}

/**
 * smack_task_movememory - Smack check on moving memory
 * @p: the task object
 *
 * Return 0 if write access is permitted
 */
static int smack_task_movememory(struct task_struct *p)
{
	return smk_curacc_on_task(p, MAY_WRITE, __func__);
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
	struct smk_audit_info ad;

	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, p);
	/*
	 * Sending a signal requires that the sender
	 * can write the receiver.
	 */
	if (secid == 0)
		return smk_curacc(smk_of_task(task_security(p)), MAY_WRITE,
				  &ad);
	/*
	 * If the secid isn't 0 we're dealing with some USB IO
	 * specific behavior. This is not clean. For one thing
	 * we can't take privilege into account.
	 */
	return smk_access(smack_from_secid(secid),
			  smk_of_task(task_security(p)), MAY_WRITE, &ad);
}

/**
 * smack_task_wait - Smack access check for waiting
 * @p: task to wait for
 *
 * Returns 0 if current can wait for p, error code otherwise
 */
static int smack_task_wait(struct task_struct *p)
{
	struct smk_audit_info ad;
	char *sp = smk_of_current();
	char *tsp = smk_of_forked(task_security(p));
	int rc;

	/* we don't log here, we can be overriden */
	rc = smk_access(tsp, sp, MAY_WRITE, NULL);
	if (rc == 0)
		goto out_log;

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
		rc = 0;
	/* we log only if we didn't get overriden */
 out_log:
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_TASK);
	smk_ad_setfield_u_tsk(&ad, p);
	smack_log(tsp, sp, MAY_WRITE, rc, &ad);
	return rc;
}

/**
 * smack_task_to_inode - copy task smack into the inode blob
 * @p: task to copy from
 * @inode: inode to copy to
 *
 * Sets the smack pointer in the inode security blob
 */
static void smack_task_to_inode(struct task_struct *p, struct inode *inode)
{
	struct inode_smack *isp = inode->i_security;
	isp->smk_inode = smk_of_task(task_security(p));
}

/*
 * Socket hooks.
 */

/**
 * smack_sk_alloc_security - Allocate a socket blob
 * @sk: the socket
 * @family: unused
 * @gfp_flags: memory allocation flags
 *
 * Assign Smack pointers to current
 *
 * Returns 0 on success, -ENOMEM is there's no memory
 */
static int smack_sk_alloc_security(struct sock *sk, int family, gfp_t gfp_flags)
{
	char *csp = smk_of_current();
	struct socket_smack *ssp;

	ssp = kzalloc(sizeof(struct socket_smack), gfp_flags);
	if (ssp == NULL)
		return -ENOMEM;

	ssp->smk_in = csp;
	ssp->smk_out = csp;
	ssp->smk_packet = NULL;

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
* smack_host_label - check host based restrictions
* @sip: the object end
*
* looks for host based access restrictions
*
* This version will only be appropriate for really small sets of single label
* hosts.  The caller is responsible for ensuring that the RCU read lock is
* taken before calling this function.
*
* Returns the label of the far end or NULL if it's not special.
*/
static char *smack_host_label(struct sockaddr_in *sip)
{
	struct smk_netlbladdr *snp;
	struct in_addr *siap = &sip->sin_addr;

	if (siap->s_addr == 0)
		return NULL;

	list_for_each_entry_rcu(snp, &smk_netlbladdr_list, list)
		/*
		* we break after finding the first match because
		* the list is sorted from longest to shortest mask
		* so we have found the most specific match
		*/
		if ((&snp->smk_host.sin_addr)->s_addr ==
		    (siap->s_addr & (&snp->smk_mask)->s_addr)) {
			/* we have found the special CIPSO option */
			if (snp->smk_label == smack_cipso_option)
				return NULL;
			return snp->smk_label;
		}

	return NULL;
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
	struct smack_known *skp;
	struct socket_smack *ssp = sk->sk_security;
	int rc = 0;

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
		skp = smk_find_entry(ssp->smk_out);
		rc = netlbl_sock_setattr(sk, sk->sk_family, &skp->smk_netlabel);
	}

	bh_unlock_sock(sk);
	local_bh_enable();

	return rc;
}

/**
 * smack_netlbel_send - Set the secattr on a socket and perform access checks
 * @sk: the socket
 * @sap: the destination address
 *
 * Set the correct secattr for the given socket based on the destination
 * address and perform any outbound access checks needed.
 *
 * Returns 0 on success or an error code.
 *
 */
static int smack_netlabel_send(struct sock *sk, struct sockaddr_in *sap)
{
	int rc;
	int sk_lbl;
	char *hostsp;
	struct socket_smack *ssp = sk->sk_security;
	struct smk_audit_info ad;

	rcu_read_lock();
	hostsp = smack_host_label(sap);
	if (hostsp != NULL) {
#ifdef CONFIG_AUDIT
		struct lsm_network_audit net;

		smk_ad_init_net(&ad, __func__, LSM_AUDIT_DATA_NET, &net);
		ad.a.u.net->family = sap->sin_family;
		ad.a.u.net->dport = sap->sin_port;
		ad.a.u.net->v4info.daddr = sap->sin_addr.s_addr;
#endif
		sk_lbl = SMACK_UNLABELED_SOCKET;
		rc = smk_access(ssp->smk_out, hostsp, MAY_WRITE, &ad);
	} else {
		sk_lbl = SMACK_CIPSO_SOCKET;
		rc = 0;
	}
	rcu_read_unlock();
	if (rc != 0)
		return rc;

	return smack_netlabel(sk, sk_lbl);
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

	if (value == NULL || size > SMK_LONGLABEL || size == 0)
		return -EACCES;

	sp = smk_import(value, size);
	if (sp == NULL)
		return -EINVAL;

	if (strcmp(name, XATTR_SMACK_SUFFIX) == 0) {
		nsp->smk_inode = sp;
		nsp->smk_flags |= SMK_INODE_INSTANT;
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
		if (sock->sk->sk_family != PF_UNIX) {
			rc = smack_netlabel(sock->sk, SMACK_CIPSO_SOCKET);
			if (rc != 0)
				printk(KERN_WARNING
					"Smack: \"%s\" netlbl error %d.\n",
					__func__, -rc);
		}
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
	if (sock->sk == NULL || sock->sk->sk_family != PF_INET)
		return 0;
	if (addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;

	return smack_netlabel_send(sock->sk, (struct sockaddr_in *)sap);
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
	msg->security = smk_of_current();
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

	isp->security = smk_of_current();
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
 * smk_curacc_shm : check if current has access on shm
 * @shp : the object
 * @access : access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smk_curacc_shm(struct shmid_kernel *shp, int access)
{
	char *ssp = smack_of_shm(shp);
	struct smk_audit_info ad;

#ifdef CONFIG_AUDIT
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_IPC);
	ad.a.u.ipc_id = shp->shm_perm.id;
#endif
	return smk_curacc(ssp, access, &ad);
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
	int may;

	may = smack_flags_to_may(shmflg);
	return smk_curacc_shm(shp, may);
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
	return smk_curacc_shm(shp, may);
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
	int may;

	may = smack_flags_to_may(shmflg);
	return smk_curacc_shm(shp, may);
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

	isp->security = smk_of_current();
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
 * smk_curacc_sem : check if current has access on sem
 * @sma : the object
 * @access : access requested
 *
 * Returns 0 if current has the requested access, error code otherwise
 */
static int smk_curacc_sem(struct sem_array *sma, int access)
{
	char *ssp = smack_of_sem(sma);
	struct smk_audit_info ad;

#ifdef CONFIG_AUDIT
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_IPC);
	ad.a.u.ipc_id = sma->sem_perm.id;
#endif
	return smk_curacc(ssp, access, &ad);
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
	int may;

	may = smack_flags_to_may(semflg);
	return smk_curacc_sem(sma, may);
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

	return smk_curacc_sem(sma, may);
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
	return smk_curacc_sem(sma, MAY_READWRITE);
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

	kisp->security = smk_of_current();
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
 * smk_curacc_msq : helper to check if current has access on msq
 * @msq : the msq
 * @access : access requested
 *
 * return 0 if current has access, error otherwise
 */
static int smk_curacc_msq(struct msg_queue *msq, int access)
{
	char *msp = smack_of_msq(msq);
	struct smk_audit_info ad;

#ifdef CONFIG_AUDIT
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_IPC);
	ad.a.u.ipc_id = msq->q_perm.id;
#endif
	return smk_curacc(msp, access, &ad);
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
	int may;

	may = smack_flags_to_may(msqflg);
	return smk_curacc_msq(msq, may);
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

	return smk_curacc_msq(msq, may);
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
	int may;

	may = smack_flags_to_may(msqflg);
	return smk_curacc_msq(msq, may);
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
	return smk_curacc_msq(msq, MAY_READWRITE);
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
	int may = smack_flags_to_may(flag);
	struct smk_audit_info ad;

#ifdef CONFIG_AUDIT
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_IPC);
	ad.a.u.ipc_id = ipp->id;
#endif
	return smk_curacc(isp, may, &ad);
}

/**
 * smack_ipc_getsecid - Extract smack security id
 * @ipp: the object permissions
 * @secid: where result will be saved
 */
static void smack_ipc_getsecid(struct kern_ipc_perm *ipp, u32 *secid)
{
	char *smack = ipp->security;

	*secid = smack_to_secid(smack);
}

/**
 * smack_d_instantiate - Make sure the blob is correct on an inode
 * @opt_dentry: dentry where inode will be attached
 * @inode: the object
 *
 * Set the inode's security blob if it hasn't been done already.
 */
static void smack_d_instantiate(struct dentry *opt_dentry, struct inode *inode)
{
	struct super_block *sbp;
	struct superblock_smack *sbsp;
	struct inode_smack *isp;
	char *csp = smk_of_current();
	char *fetched;
	char *final;
	char trattr[TRANS_TRUE_SIZE];
	int transflag = 0;
	int rc;
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
		 * Casey says that it's a little embarrassing
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
		 * Socket access is controlled by the socket
		 * structures associated with the task involved.
		 */
		final = smack_known_star.smk_known;
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
		 */

		/*
		 * UNIX domain sockets use lower level socket data.
		 */
		if (S_ISSOCK(inode->i_mode)) {
			final = smack_known_star.smk_known;
			break;
		}
		/*
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
		dp = dget(opt_dentry);
		fetched = smk_fetch(XATTR_NAME_SMACK, inode, dp);
		if (fetched != NULL)
			final = fetched;

		/*
		 * Transmuting directory
		 */
		if (S_ISDIR(inode->i_mode)) {
			/*
			 * If this is a new directory and the label was
			 * transmuted when the inode was initialized
			 * set the transmute attribute on the directory
			 * and mark the inode.
			 *
			 * If there is a transmute attribute on the
			 * directory mark the inode.
			 */
			if (isp->smk_flags & SMK_INODE_CHANGED) {
				isp->smk_flags &= ~SMK_INODE_CHANGED;
				rc = inode->i_op->setxattr(dp,
					XATTR_NAME_SMACKTRANSMUTE,
					TRANS_TRUE, TRANS_TRUE_SIZE,
					0);
			} else {
				rc = inode->i_op->getxattr(dp,
					XATTR_NAME_SMACKTRANSMUTE, trattr,
					TRANS_TRUE_SIZE);
				if (rc >= 0 && strncmp(trattr, TRANS_TRUE,
						       TRANS_TRUE_SIZE) != 0)
					rc = -EINVAL;
			}
			if (rc >= 0)
				transflag = SMK_INODE_TRANSMUTE;
		}
		isp->smk_task = smk_fetch(XATTR_NAME_SMACKEXEC, inode, dp);
		isp->smk_mmap = smk_fetch(XATTR_NAME_SMACKMMAP, inode, dp);

		dput(dp);
		break;
	}

	if (final == NULL)
		isp->smk_inode = csp;
	else
		isp->smk_inode = final;

	isp->smk_flags |= (SMK_INODE_INSTANT | transflag);

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

	cp = kstrdup(smk_of_task(task_security(p)), GFP_KERNEL);
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
	int rc;
	struct task_smack *tsp;
	struct task_smack *oldtsp;
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

	if (value == NULL || size == 0 || size >= SMK_LONGLABEL)
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

	oldtsp = p->cred->security;
	new = prepare_creds();
	if (new == NULL)
		return -ENOMEM;

	tsp = new_task_smack(newsmack, oldtsp->smk_forked, GFP_KERNEL);
	if (tsp == NULL) {
		kfree(new);
		return -ENOMEM;
	}
	rc = smk_copy_rules(&tsp->smk_rules, &oldtsp->smk_rules, GFP_KERNEL);
	if (rc != 0)
		return rc;

	new->security = tsp;
	commit_creds(new);
	return size;
}

/**
 * smack_unix_stream_connect - Smack access on UDS
 * @sock: one sock
 * @other: the other sock
 * @newsk: unused
 *
 * Return 0 if a subject with the smack of sock could access
 * an object with the smack of other, otherwise an error code
 */
static int smack_unix_stream_connect(struct sock *sock,
				     struct sock *other, struct sock *newsk)
{
	struct socket_smack *ssp = sock->sk_security;
	struct socket_smack *osp = other->sk_security;
	struct socket_smack *nsp = newsk->sk_security;
	struct smk_audit_info ad;
	int rc = 0;

#ifdef CONFIG_AUDIT
	struct lsm_network_audit net;

	smk_ad_init_net(&ad, __func__, LSM_AUDIT_DATA_NET, &net);
	smk_ad_setfield_u_net_sk(&ad, other);
#endif

	if (!capable(CAP_MAC_OVERRIDE))
		rc = smk_access(ssp->smk_out, osp->smk_in, MAY_WRITE, &ad);

	/*
	 * Cross reference the peer labels for SO_PEERSEC.
	 */
	if (rc == 0) {
		nsp->smk_packet = ssp->smk_out;
		ssp->smk_packet = osp->smk_out;
	}

	return rc;
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
	struct socket_smack *ssp = sock->sk->sk_security;
	struct socket_smack *osp = other->sk->sk_security;
	struct smk_audit_info ad;
	int rc = 0;

#ifdef CONFIG_AUDIT
	struct lsm_network_audit net;

	smk_ad_init_net(&ad, __func__, LSM_AUDIT_DATA_NET, &net);
	smk_ad_setfield_u_net_sk(&ad, other->sk);
#endif

	if (!capable(CAP_MAC_OVERRIDE))
		rc = smk_access(ssp->smk_out, osp->smk_in, MAY_WRITE, &ad);

	return rc;
}

/**
 * smack_socket_sendmsg - Smack check based on destination host
 * @sock: the socket
 * @msg: the message
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

	/*
	 * Perfectly reasonable for this to be NULL
	 */
	if (sip == NULL || sip->sin_family != AF_INET)
		return 0;

	return smack_netlabel_send(sock->sk, sip);
}

/**
 * smack_from_secattr - Convert a netlabel attr.mls.lvl/attr.mls.cat pair to smack
 * @sap: netlabel secattr
 * @ssp: socket security information
 *
 * Returns a pointer to a Smack label found on the label list.
 */
static char *smack_from_secattr(struct netlbl_lsm_secattr *sap,
				struct socket_smack *ssp)
{
	struct smack_known *kp;
	char *sp;
	int found = 0;

	if ((sap->flags & NETLBL_SECATTR_MLS_LVL) != 0) {
		/*
		 * Looks like a CIPSO packet.
		 * If there are flags but no level netlabel isn't
		 * behaving the way we expect it to.
		 *
		 * Look it up in the label table
		 * Without guidance regarding the smack value
		 * for the packet fall back on the network
		 * ambient value.
		 */
		rcu_read_lock();
		list_for_each_entry(kp, &smack_known_list, list) {
			if (sap->attr.mls.lvl != kp->smk_netlabel.attr.mls.lvl)
				continue;
			if (memcmp(sap->attr.mls.cat,
				kp->smk_netlabel.attr.mls.cat,
				SMK_CIPSOLEN) != 0)
				continue;
			found = 1;
			break;
		}
		rcu_read_unlock();

		if (found)
			return kp->smk_known;

		if (ssp != NULL && ssp->smk_in == smack_known_star.smk_known)
			return smack_known_web.smk_known;
		return smack_known_star.smk_known;
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
		return sp;
	}
	/*
	 * Without guidance regarding the smack value
	 * for the packet fall back on the network
	 * ambient value.
	 */
	return smack_net_ambient;
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
	char *csp;
	int rc;
	struct smk_audit_info ad;
#ifdef CONFIG_AUDIT
	struct lsm_network_audit net;
#endif
	if (sk->sk_family != PF_INET && sk->sk_family != PF_INET6)
		return 0;

	/*
	 * Translate what netlabel gave us.
	 */
	netlbl_secattr_init(&secattr);

	rc = netlbl_skbuff_getattr(skb, sk->sk_family, &secattr);
	if (rc == 0)
		csp = smack_from_secattr(&secattr, ssp);
	else
		csp = smack_net_ambient;

	netlbl_secattr_destroy(&secattr);

#ifdef CONFIG_AUDIT
	smk_ad_init_net(&ad, __func__, LSM_AUDIT_DATA_NET, &net);
	ad.a.u.net->family = sk->sk_family;
	ad.a.u.net->netif = skb->skb_iif;
	ipv4_skb_to_auditdata(skb, &ad.a, NULL);
#endif
	/*
	 * Receiving a packet requires that the other end
	 * be able to write here. Read access is not required.
	 * This is the simplist possible security model
	 * for networking.
	 */
	rc = smk_access(csp, ssp->smk_in, MAY_WRITE, &ad);
	if (rc != 0)
		netlbl_skbuff_err(skb, rc, 0);
	return rc;
}

/**
 * smack_socket_getpeersec_stream - pull in packet label
 * @sock: the socket
 * @optval: user's destination
 * @optlen: size thereof
 * @len: max thereof
 *
 * returns zero on success, an error code otherwise
 */
static int smack_socket_getpeersec_stream(struct socket *sock,
					  char __user *optval,
					  int __user *optlen, unsigned len)
{
	struct socket_smack *ssp;
	char *rcp = "";
	int slen = 1;
	int rc = 0;

	ssp = sock->sk->sk_security;
	if (ssp->smk_packet != NULL) {
		rcp = ssp->smk_packet;
		slen = strlen(rcp) + 1;
	}

	if (slen > len)
		rc = -ERANGE;
	else if (copy_to_user(optval, rcp, slen) != 0)
		rc = -EFAULT;

	if (put_user(slen, optlen) != 0)
		rc = -EFAULT;

	return rc;
}


/**
 * smack_socket_getpeersec_dgram - pull in packet label
 * @sock: the peer socket
 * @skb: packet data
 * @secid: pointer to where to put the secid of the packet
 *
 * Sets the netlabel socket state on sk from parent
 */
static int smack_socket_getpeersec_dgram(struct socket *sock,
					 struct sk_buff *skb, u32 *secid)

{
	struct netlbl_lsm_secattr secattr;
	struct socket_smack *ssp = NULL;
	char *sp;
	int family = PF_UNSPEC;
	u32 s = 0;	/* 0 is the invalid secid */
	int rc;

	if (skb != NULL) {
		if (skb->protocol == htons(ETH_P_IP))
			family = PF_INET;
		else if (skb->protocol == htons(ETH_P_IPV6))
			family = PF_INET6;
	}
	if (family == PF_UNSPEC && sock != NULL)
		family = sock->sk->sk_family;

	if (family == PF_UNIX) {
		ssp = sock->sk->sk_security;
		s = smack_to_secid(ssp->smk_out);
	} else if (family == PF_INET || family == PF_INET6) {
		/*
		 * Translate what netlabel gave us.
		 */
		if (sock != NULL && sock->sk != NULL)
			ssp = sock->sk->sk_security;
		netlbl_secattr_init(&secattr);
		rc = netlbl_skbuff_getattr(skb, family, &secattr);
		if (rc == 0) {
			sp = smack_from_secattr(&secattr, ssp);
			s = smack_to_secid(sp);
		}
		netlbl_secattr_destroy(&secattr);
	}
	*secid = s;
	if (s == 0)
		return -EINVAL;
	return 0;
}

/**
 * smack_sock_graft - Initialize a newly created socket with an existing sock
 * @sk: child sock
 * @parent: parent socket
 *
 * Set the smk_{in,out} state of an existing sock based on the process that
 * is creating the new socket.
 */
static void smack_sock_graft(struct sock *sk, struct socket *parent)
{
	struct socket_smack *ssp;

	if (sk == NULL ||
	    (sk->sk_family != PF_INET && sk->sk_family != PF_INET6))
		return;

	ssp = sk->sk_security;
	ssp->smk_in = ssp->smk_out = smk_of_current();
	/* cssp->smk_packet is already set in smack_inet_csk_clone() */
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
	u16 family = sk->sk_family;
	struct smack_known *skp;
	struct socket_smack *ssp = sk->sk_security;
	struct netlbl_lsm_secattr secattr;
	struct sockaddr_in addr;
	struct iphdr *hdr;
	char *sp;
	char *hsp;
	int rc;
	struct smk_audit_info ad;
#ifdef CONFIG_AUDIT
	struct lsm_network_audit net;
#endif

	/* handle mapped IPv4 packets arriving via IPv6 sockets */
	if (family == PF_INET6 && skb->protocol == htons(ETH_P_IP))
		family = PF_INET;

	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, family, &secattr);
	if (rc == 0)
		sp = smack_from_secattr(&secattr, ssp);
	else
		sp = smack_known_huh.smk_known;
	netlbl_secattr_destroy(&secattr);

#ifdef CONFIG_AUDIT
	smk_ad_init_net(&ad, __func__, LSM_AUDIT_DATA_NET, &net);
	ad.a.u.net->family = family;
	ad.a.u.net->netif = skb->skb_iif;
	ipv4_skb_to_auditdata(skb, &ad.a, NULL);
#endif
	/*
	 * Receiving a packet requires that the other end be able to write
	 * here. Read access is not required.
	 */
	rc = smk_access(sp, ssp->smk_in, MAY_WRITE, &ad);
	if (rc != 0)
		return rc;

	/*
	 * Save the peer's label in the request_sock so we can later setup
	 * smk_packet in the child socket so that SO_PEERCRED can report it.
	 */
	req->peer_secid = smack_to_secid(sp);

	/*
	 * We need to decide if we want to label the incoming connection here
	 * if we do we only need to label the request_sock and the stack will
	 * propagate the wire-label to the sock when it is created.
	 */
	hdr = ip_hdr(skb);
	addr.sin_addr.s_addr = hdr->saddr;
	rcu_read_lock();
	hsp = smack_host_label(&addr);
	rcu_read_unlock();

	if (hsp == NULL) {
		skp = smk_find_entry(sp);
		rc = netlbl_req_setattr(req, &skp->smk_netlabel);
	} else
		netlbl_req_delattr(req);

	return rc;
}

/**
 * smack_inet_csk_clone - Copy the connection information to the new socket
 * @sk: the new socket
 * @req: the connection's request_sock
 *
 * Transfer the connection's peer label to the newly created socket.
 */
static void smack_inet_csk_clone(struct sock *sk,
				 const struct request_sock *req)
{
	struct socket_smack *ssp = sk->sk_security;

	if (req->peer_secid != 0)
		ssp->smk_packet = smack_from_secid(req->peer_secid);
	else
		ssp->smk_packet = NULL;
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
	key->security = smk_of_task(cred->security);
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
	struct smk_audit_info ad;
	char *tsp = smk_of_task(cred->security);

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
	if (tsp == NULL)
		return -EACCES;
#ifdef CONFIG_AUDIT
	smk_ad_init(&ad, __func__, LSM_AUDIT_DATA_KEY);
	ad.a.u.key_struct.key = keyp->serial;
	ad.a.u.key_struct.key_desc = keyp->description;
#endif
	return smk_access(tsp, keyp->security,
				 MAY_READWRITE, &ad);
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
		audit_log(actx, GFP_ATOMIC, AUDIT_SELINUX_ERR,
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

/**
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

	if (secdata)
		*secdata = sp;
	*seclen = strlen(sp);
	return 0;
}

/**
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

/**
 * smack_release_secctx - don't do anything.
 * @secdata: unused
 * @seclen: unused
 *
 * Exists to make sure nothing gets done, and properly
 */
static void smack_release_secctx(char *secdata, u32 seclen)
{
}

static int smack_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return smack_inode_setsecurity(inode, XATTR_SMACK_SUFFIX, ctx, ctxlen, 0);
}

static int smack_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return __vfs_setxattr_noperm(dentry, XATTR_NAME_SMACK, ctx, ctxlen, 0);
}

static int smack_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	int len = 0;
	len = smack_inode_getsecurity(inode, XATTR_SMACK_SUFFIX, ctx, true);

	if (len < 0)
		return len;
	*ctxlen = len;
	return 0;
}

struct security_operations smack_ops = {
	.name =				"smack",

	.ptrace_access_check =		smack_ptrace_access_check,
	.ptrace_traceme =		smack_ptrace_traceme,
	.syslog = 			smack_syslog,

	.sb_alloc_security = 		smack_sb_alloc_security,
	.sb_free_security = 		smack_sb_free_security,
	.sb_copy_data = 		smack_sb_copy_data,
	.sb_kern_mount = 		smack_sb_kern_mount,
	.sb_statfs = 			smack_sb_statfs,
	.sb_mount = 			smack_sb_mount,
	.sb_umount = 			smack_sb_umount,

	.bprm_set_creds =		smack_bprm_set_creds,
	.bprm_committing_creds =	smack_bprm_committing_creds,
	.bprm_secureexec =		smack_bprm_secureexec,

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
	.file_mmap =			smack_file_mmap,
	.file_set_fowner = 		smack_file_set_fowner,
	.file_send_sigiotask = 		smack_file_send_sigiotask,
	.file_receive = 		smack_file_receive,

	.dentry_open =			smack_dentry_open,

	.cred_alloc_blank =		smack_cred_alloc_blank,
	.cred_free =			smack_cred_free,
	.cred_prepare =			smack_cred_prepare,
	.cred_transfer =		smack_cred_transfer,
	.kernel_act_as =		smack_kernel_act_as,
	.kernel_create_files_as =	smack_kernel_create_files_as,
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
	.inet_csk_clone =		smack_inet_csk_clone,

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
	.inode_notifysecctx =		smack_inode_notifysecctx,
	.inode_setsecctx =		smack_inode_setsecctx,
	.inode_getsecctx =		smack_inode_getsecctx,
};


static __init void init_smack_known_list(void)
{
	/*
	 * Initialize rule list locks
	 */
	mutex_init(&smack_known_huh.smk_rules_lock);
	mutex_init(&smack_known_hat.smk_rules_lock);
	mutex_init(&smack_known_floor.smk_rules_lock);
	mutex_init(&smack_known_star.smk_rules_lock);
	mutex_init(&smack_known_invalid.smk_rules_lock);
	mutex_init(&smack_known_web.smk_rules_lock);
	/*
	 * Initialize rule lists
	 */
	INIT_LIST_HEAD(&smack_known_huh.smk_rules);
	INIT_LIST_HEAD(&smack_known_hat.smk_rules);
	INIT_LIST_HEAD(&smack_known_star.smk_rules);
	INIT_LIST_HEAD(&smack_known_floor.smk_rules);
	INIT_LIST_HEAD(&smack_known_invalid.smk_rules);
	INIT_LIST_HEAD(&smack_known_web.smk_rules);
	/*
	 * Create the known labels list
	 */
	list_add(&smack_known_huh.list, &smack_known_list);
	list_add(&smack_known_hat.list, &smack_known_list);
	list_add(&smack_known_star.list, &smack_known_list);
	list_add(&smack_known_floor.list, &smack_known_list);
	list_add(&smack_known_invalid.list, &smack_known_list);
	list_add(&smack_known_web.list, &smack_known_list);
}

/**
 * smack_init - initialize the smack system
 *
 * Returns 0
 */
static __init int smack_init(void)
{
	struct cred *cred;
	struct task_smack *tsp;

	if (!security_module_enable(&smack_ops))
		return 0;

	tsp = new_task_smack(smack_known_floor.smk_known,
				smack_known_floor.smk_known, GFP_KERNEL);
	if (tsp == NULL)
		return -ENOMEM;

	printk(KERN_INFO "Smack:  Initializing.\n");

	/*
	 * Set the security state for the initial task.
	 */
	cred = (struct cred *) current->cred;
	cred->security = tsp;

	/* initialize the smack_known_list */
	init_smack_known_list();

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
