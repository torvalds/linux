/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Serge Hallyn <serue@us.ibm.com>
 * Kylene Hall <kylene@us.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_main.c
 *	implements the IMA hooks: ima_bprm_check, ima_file_mmap,
 *	and ima_path_check.
 */
#include <linux/module.h>
#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/mount.h>
#include <linux/mman.h>

#include "ima.h"

int ima_initialized;

char *ima_hash = "sha1";
static int __init hash_setup(char *str)
{
	if (strncmp(str, "md5", 3) == 0)
		ima_hash = "md5";
	return 1;
}
__setup("ima_hash=", hash_setup);

struct ima_imbalance {
	struct hlist_node node;
	unsigned long fsmagic;
};

/*
 * ima_limit_imbalance - emit one imbalance message per filesystem type
 *
 * Maintain list of filesystem types that do not measure files properly.
 * Return false if unknown, true if known.
 */
static bool ima_limit_imbalance(struct file *file)
{
	static DEFINE_SPINLOCK(ima_imbalance_lock);
	static HLIST_HEAD(ima_imbalance_list);

	struct super_block *sb = file->f_dentry->d_sb;
	struct ima_imbalance *entry;
	struct hlist_node *node;
	bool found = false;

	rcu_read_lock();
	hlist_for_each_entry_rcu(entry, node, &ima_imbalance_list, node) {
		if (entry->fsmagic == sb->s_magic) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	if (found)
		goto out;

	entry = kmalloc(sizeof(*entry), GFP_NOFS);
	if (!entry)
		goto out;
	entry->fsmagic = sb->s_magic;
	spin_lock(&ima_imbalance_lock);
	/*
	 * we could have raced and something else might have added this fs
	 * to the list, but we don't really care
	 */
	hlist_add_head_rcu(&entry->node, &ima_imbalance_list);
	spin_unlock(&ima_imbalance_lock);
	printk(KERN_INFO "IMA: unmeasured files on fsmagic: %lX\n",
	       entry->fsmagic);
out:
	return found;
}

/*
 * Update the counts given an fmode_t
 */
static void ima_inc_counts(struct ima_iint_cache *iint, fmode_t mode)
{
	BUG_ON(!mutex_is_locked(&iint->mutex));

	iint->opencount++;
	if ((mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		iint->readcount++;
	if (mode & FMODE_WRITE)
		iint->writecount++;
}

/*
 * Decrement ima counts
 */
static void ima_dec_counts(struct ima_iint_cache *iint, struct inode *inode,
			   struct file *file)
{
	mode_t mode = file->f_mode;
	BUG_ON(!mutex_is_locked(&iint->mutex));

	iint->opencount--;
	if ((mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		iint->readcount--;
	if (mode & FMODE_WRITE) {
		iint->writecount--;
		if (iint->writecount == 0) {
			if (iint->version != inode->i_version)
				iint->flags &= ~IMA_MEASURED;
		}
	}

	if (((iint->opencount < 0) ||
	     (iint->readcount < 0) ||
	     (iint->writecount < 0)) &&
	    !ima_limit_imbalance(file)) {
		printk(KERN_INFO "%s: open/free imbalance (r:%ld w:%ld o:%ld)\n",
		       __FUNCTION__, iint->readcount, iint->writecount,
		       iint->opencount);
		dump_stack();
	}
}

/**
 * ima_file_free - called on __fput()
 * @file: pointer to file structure being freed
 *
 * Flag files that changed, based on i_version;
 * and decrement the iint readcount/writecount.
 */
void ima_file_free(struct file *file)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct ima_iint_cache *iint;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return;
	iint = ima_iint_find_get(inode);
	if (!iint)
		return;

	mutex_lock(&iint->mutex);
	ima_dec_counts(iint, inode, file);
	mutex_unlock(&iint->mutex);
	kref_put(&iint->refcount, iint_free);
}

/* ima_read_write_check - reflect possible reading/writing errors in the PCR.
 *
 * When opening a file for read, if the file is already open for write,
 * the file could change, resulting in a file measurement error.
 *
 * Opening a file for write, if the file is already open for read, results
 * in a time of measure, time of use (ToMToU) error.
 *
 * In either case invalidate the PCR.
 */
enum iint_pcr_error { TOMTOU, OPEN_WRITERS };
static void ima_read_write_check(enum iint_pcr_error error,
				 struct ima_iint_cache *iint,
				 struct inode *inode,
				 const unsigned char *filename)
{
	switch (error) {
	case TOMTOU:
		if (iint->readcount > 0)
			ima_add_violation(inode, filename, "invalid_pcr",
					  "ToMToU");
		break;
	case OPEN_WRITERS:
		if (iint->writecount > 0)
			ima_add_violation(inode, filename, "invalid_pcr",
					  "open_writers");
		break;
	}
}

static int get_path_measurement(struct ima_iint_cache *iint, struct file *file,
				const unsigned char *filename)
{
	int rc = 0;

	ima_inc_counts(iint, file->f_mode);

	rc = ima_collect_measurement(iint, file);
	if (!rc)
		ima_store_measurement(iint, file, filename);
	return rc;
}

/**
 * ima_path_check - based on policy, collect/store measurement.
 * @path: contains a pointer to the path to be measured
 * @mask: contains MAY_READ, MAY_WRITE or MAY_EXECUTE
 *
 * Measure the file being open for readonly, based on the
 * ima_must_measure() policy decision.
 *
 * Keep read/write counters for all files, but only
 * invalidate the PCR for measured files:
 * 	- Opening a file for write when already open for read,
 *	  results in a time of measure, time of use (ToMToU) error.
 *	- Opening a file for read when already open for write,
 * 	  could result in a file measurement error.
 *
 * Always return 0 and audit dentry_open failures.
 * (Return code will be based upon measurement appraisal.)
 */
int ima_path_check(struct path *path, int mask)
{
	struct inode *inode = path->dentry->d_inode;
	struct ima_iint_cache *iint;
	struct file *file = NULL;
	int rc;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return 0;
	iint = ima_iint_find_get(inode);
	if (!iint)
		return 0;

	mutex_lock(&iint->mutex);

	rc = ima_must_measure(iint, inode, MAY_READ, PATH_CHECK);
	if (rc < 0)
		goto out;

	if ((mask & MAY_WRITE) || (mask == 0))
		ima_read_write_check(TOMTOU, iint, inode,
				     path->dentry->d_name.name);

	if ((mask & (MAY_WRITE | MAY_READ | MAY_EXEC)) != MAY_READ)
		goto out;

	ima_read_write_check(OPEN_WRITERS, iint, inode,
			     path->dentry->d_name.name);
	if (!(iint->flags & IMA_MEASURED)) {
		struct dentry *dentry = dget(path->dentry);
		struct vfsmount *mnt = mntget(path->mnt);

		file = dentry_open(dentry, mnt, O_RDONLY | O_LARGEFILE,
				   current_cred());
		if (IS_ERR(file)) {
			int audit_info = 0;

			integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
					    dentry->d_name.name,
					    "add_measurement",
					    "dentry_open failed",
					    1, audit_info);
			file = NULL;
			goto out;
		}
		rc = get_path_measurement(iint, file, dentry->d_name.name);
	}
out:
	mutex_unlock(&iint->mutex);
	if (file)
		fput(file);
	kref_put(&iint->refcount, iint_free);
	return 0;
}
EXPORT_SYMBOL_GPL(ima_path_check);

static int process_measurement(struct file *file, const unsigned char *filename,
			       int mask, int function)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct ima_iint_cache *iint;
	int rc;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return 0;
	iint = ima_iint_find_get(inode);
	if (!iint)
		return -ENOMEM;

	mutex_lock(&iint->mutex);
	rc = ima_must_measure(iint, inode, mask, function);
	if (rc != 0)
		goto out;

	rc = ima_collect_measurement(iint, file);
	if (!rc)
		ima_store_measurement(iint, file, filename);
out:
	mutex_unlock(&iint->mutex);
	kref_put(&iint->refcount, iint_free);
	return rc;
}

/*
 * ima_counts_get - increment file counts
 *
 * - for IPC shm and shmat file.
 * - for nfsd exported files.
 *
 * Increment the counts for these files to prevent unnecessary
 * imbalance messages.
 */
void ima_counts_get(struct file *file)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct ima_iint_cache *iint;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return;
	iint = ima_iint_find_get(inode);
	if (!iint)
		return;
	mutex_lock(&iint->mutex);
	ima_inc_counts(iint, file->f_mode);
	mutex_unlock(&iint->mutex);

	kref_put(&iint->refcount, iint_free);
}
EXPORT_SYMBOL_GPL(ima_counts_get);

/**
 * ima_file_mmap - based on policy, collect/store measurement.
 * @file: pointer to the file to be measured (May be NULL)
 * @prot: contains the protection that will be applied by the kernel.
 *
 * Measure files being mmapped executable based on the ima_must_measure()
 * policy decision.
 *
 * Return 0 on success, an error code on failure.
 * (Based on the results of appraise_measurement().)
 */
int ima_file_mmap(struct file *file, unsigned long prot)
{
	int rc;

	if (!file)
		return 0;
	if (prot & PROT_EXEC)
		rc = process_measurement(file, file->f_dentry->d_name.name,
					 MAY_EXEC, FILE_MMAP);
	return 0;
}

/**
 * ima_bprm_check - based on policy, collect/store measurement.
 * @bprm: contains the linux_binprm structure
 *
 * The OS protects against an executable file, already open for write,
 * from being executed in deny_write_access() and an executable file,
 * already open for execute, from being modified in get_write_access().
 * So we can be certain that what we verify and measure here is actually
 * what is being executed.
 *
 * Return 0 on success, an error code on failure.
 * (Based on the results of appraise_measurement().)
 */
int ima_bprm_check(struct linux_binprm *bprm)
{
	int rc;

	rc = process_measurement(bprm->file, bprm->filename,
				 MAY_EXEC, BPRM_CHECK);
	return 0;
}

static int __init init_ima(void)
{
	int error;

	ima_iintcache_init();
	error = ima_init();
	ima_initialized = 1;
	return error;
}

static void __exit cleanup_ima(void)
{
	ima_cleanup();
}

late_initcall(init_ima);	/* Start IMA after the TPM is available */

MODULE_DESCRIPTION("Integrity Measurement Architecture");
MODULE_LICENSE("GPL");
