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
 *             implements the IMA hooks: ima_bprm_check, ima_file_mmap,
 *             and ima_path_check.
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
	if (iint->opencount <= 0) {
		printk(KERN_INFO
		       "%s: %s open/free imbalance (r:%ld w:%ld o:%ld f:%ld)\n",
		       __FUNCTION__, file->f_dentry->d_name.name,
		       iint->readcount, iint->writecount,
		       iint->opencount, atomic_long_read(&file->f_count));
		if (!(iint->flags & IMA_IINT_DUMP_STACK)) {
			dump_stack();
			iint->flags |= IMA_IINT_DUMP_STACK;
		}
	}
	iint->opencount--;

	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		iint->readcount--;

	if (file->f_mode & FMODE_WRITE) {
		iint->writecount--;
		if (iint->writecount == 0) {
			if (iint->version != inode->i_version)
				iint->flags &= ~IMA_MEASURED;
		}
	}
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

	iint->opencount++;
	iint->readcount++;

	rc = ima_collect_measurement(iint, file);
	if (!rc)
		ima_store_measurement(iint, file, filename);
	return rc;
}

static void ima_update_counts(struct ima_iint_cache *iint, int mask)
{
	iint->opencount++;
	if ((mask & MAY_WRITE) || (mask == 0))
		iint->writecount++;
	else if (mask & (MAY_READ | MAY_EXEC))
		iint->readcount++;
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
int ima_path_check(struct path *path, int mask, int update_counts)
{
	struct inode *inode = path->dentry->d_inode;
	struct ima_iint_cache *iint;
	struct file *file = NULL;
	int rc;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return 0;
	iint = ima_iint_find_insert_get(inode);
	if (!iint)
		return 0;

	mutex_lock(&iint->mutex);
	if (update_counts)
		ima_update_counts(iint, mask);

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
	iint = ima_iint_find_insert_get(inode);
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
 * ima_counts_put - decrement file counts
 *
 * File counts are incremented in ima_path_check. On file open
 * error, such as ETXTBSY, decrement the counts to prevent
 * unnecessary imbalance messages.
 */
void ima_counts_put(struct path *path, int mask)
{
	struct inode *inode = path->dentry->d_inode;
	struct ima_iint_cache *iint;

	if (!ima_initialized || !S_ISREG(inode->i_mode))
		return;
	iint = ima_iint_find_insert_get(inode);
	if (!iint)
		return;

	mutex_lock(&iint->mutex);
	iint->opencount--;
	if ((mask & MAY_WRITE) || (mask == 0))
		iint->writecount--;
	else if (mask & (MAY_READ | MAY_EXEC))
		iint->readcount--;
	mutex_unlock(&iint->mutex);
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
	iint = ima_iint_find_insert_get(inode);
	if (!iint)
		return;
	mutex_lock(&iint->mutex);
	iint->opencount++;
	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		iint->readcount++;

	if (file->f_mode & FMODE_WRITE)
		iint->writecount++;
	mutex_unlock(&iint->mutex);
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
