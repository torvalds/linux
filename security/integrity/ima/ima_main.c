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
 *	and ima_file_check.
 */
#include <linux/module.h>
#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/mount.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/ima.h>

#include "ima.h"

int ima_initialized;

#ifdef CONFIG_IMA_APPRAISE
int ima_appraise = IMA_APPRAISE_ENFORCE;
#else
int ima_appraise;
#endif

int ima_hash_algo = HASH_ALGO_SHA1;
static int hash_setup_done;

static int __init hash_setup(char *str)
{
	struct ima_template_desc *template_desc = ima_template_desc_current();
	int i;

	if (hash_setup_done)
		return 1;

	if (strcmp(template_desc->name, IMA_TEMPLATE_IMA_NAME) == 0) {
		if (strncmp(str, "sha1", 4) == 0)
			ima_hash_algo = HASH_ALGO_SHA1;
		else if (strncmp(str, "md5", 3) == 0)
			ima_hash_algo = HASH_ALGO_MD5;
		else
			return 1;
		goto out;
	}

	for (i = 0; i < HASH_ALGO__LAST; i++) {
		if (strcmp(str, hash_algo_name[i]) == 0) {
			ima_hash_algo = i;
			break;
		}
	}
	if (i == HASH_ALGO__LAST)
		return 1;
out:
	hash_setup_done = 1;
	return 1;
}
__setup("ima_hash=", hash_setup);

/*
 * ima_rdwr_violation_check
 *
 * Only invalidate the PCR for measured files:
 *	- Opening a file for write when already open for read,
 *	  results in a time of measure, time of use (ToMToU) error.
 *	- Opening a file for read when already open for write,
 *	  could result in a file measurement error.
 *
 */
static void ima_rdwr_violation_check(struct file *file,
				     struct integrity_iint_cache *iint,
				     int must_measure,
				     char **pathbuf,
				     const char **pathname)
{
	struct inode *inode = file_inode(file);
	char filename[NAME_MAX];
	fmode_t mode = file->f_mode;
	bool send_tomtou = false, send_writers = false;

	if (mode & FMODE_WRITE) {
		if (atomic_read(&inode->i_readcount) && IS_IMA(inode)) {
			if (!iint)
				iint = integrity_iint_find(inode);
			/* IMA_MEASURE is set from reader side */
			if (iint && (iint->flags & IMA_MEASURE))
				send_tomtou = true;
		}
	} else {
		if ((atomic_read(&inode->i_writecount) > 0) && must_measure)
			send_writers = true;
	}

	if (!send_tomtou && !send_writers)
		return;

	*pathname = ima_d_path(&file->f_path, pathbuf, filename);

	if (send_tomtou)
		ima_add_violation(file, *pathname, iint,
				  "invalid_pcr", "ToMToU");
	if (send_writers)
		ima_add_violation(file, *pathname, iint,
				  "invalid_pcr", "open_writers");
}

static void ima_check_last_writer(struct integrity_iint_cache *iint,
				  struct inode *inode, struct file *file)
{
	fmode_t mode = file->f_mode;

	if (!(mode & FMODE_WRITE))
		return;

	inode_lock(inode);
	if (atomic_read(&inode->i_writecount) == 1) {
		if ((iint->version != inode->i_version) ||
		    (iint->flags & IMA_NEW_FILE)) {
			iint->flags &= ~(IMA_DONE_MASK | IMA_NEW_FILE);
			iint->measured_pcrs = 0;
			if (iint->flags & IMA_APPRAISE)
				ima_update_xattr(iint, file);
		}
	}
	inode_unlock(inode);
}

/**
 * ima_file_free - called on __fput()
 * @file: pointer to file structure being freed
 *
 * Flag files that changed, based on i_version
 */
void ima_file_free(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct integrity_iint_cache *iint;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return;

	iint = integrity_iint_find(inode);
	if (!iint)
		return;

	ima_check_last_writer(iint, inode, file);
}

static int process_measurement(struct file *file, char *buf, loff_t size,
			       int mask, enum ima_hooks func, int opened)
{
	struct inode *inode = file_inode(file);
	struct integrity_iint_cache *iint = NULL;
	struct ima_template_desc *template_desc;
	char *pathbuf = NULL;
	char filename[NAME_MAX];
	const char *pathname = NULL;
	int rc = -ENOMEM, action, must_appraise;
	int pcr = CONFIG_IMA_MEASURE_PCR_IDX;
	struct evm_ima_xattr_data *xattr_value = NULL;
	int xattr_len = 0;
	bool violation_check;
	enum hash_algo hash_algo;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return 0;

	/* Return an IMA_MEASURE, IMA_APPRAISE, IMA_AUDIT action
	 * bitmask based on the appraise/audit/measurement policy.
	 * Included is the appraise submask.
	 */
	action = ima_get_action(inode, mask, func, &pcr);
	violation_check = ((func == FILE_CHECK || func == MMAP_CHECK) &&
			   (ima_policy_flag & IMA_MEASURE));
	if (!action && !violation_check)
		return 0;

	must_appraise = action & IMA_APPRAISE;

	/*  Is the appraise rule hook specific?  */
	if (action & IMA_FILE_APPRAISE)
		func = FILE_CHECK;

	inode_lock(inode);

	if (action) {
		iint = integrity_inode_get(inode);
		if (!iint)
			goto out;
	}

	if (violation_check) {
		ima_rdwr_violation_check(file, iint, action & IMA_MEASURE,
					 &pathbuf, &pathname);
		if (!action) {
			rc = 0;
			goto out_free;
		}
	}

	/* Determine if already appraised/measured based on bitmask
	 * (IMA_MEASURE, IMA_MEASURED, IMA_XXXX_APPRAISE, IMA_XXXX_APPRAISED,
	 *  IMA_AUDIT, IMA_AUDITED)
	 */
	iint->flags |= action;
	action &= IMA_DO_MASK;
	action &= ~((iint->flags & (IMA_DONE_MASK ^ IMA_MEASURED)) >> 1);

	/* If target pcr is already measured, unset IMA_MEASURE action */
	if ((action & IMA_MEASURE) && (iint->measured_pcrs & (0x1 << pcr)))
		action ^= IMA_MEASURE;

	/* Nothing to do, just return existing appraised status */
	if (!action) {
		if (must_appraise)
			rc = ima_get_cache_status(iint, func);
		goto out_digsig;
	}

	template_desc = ima_template_desc_current();
	if ((action & IMA_APPRAISE_SUBMASK) ||
		    strcmp(template_desc->name, IMA_TEMPLATE_IMA_NAME) != 0)
		/* read 'security.ima' */
		xattr_len = ima_read_xattr(file_dentry(file), &xattr_value);

	hash_algo = ima_get_hash_algo(xattr_value, xattr_len);

	rc = ima_collect_measurement(iint, file, buf, size, hash_algo);
	if (rc != 0) {
		if (file->f_flags & O_DIRECT)
			rc = (iint->flags & IMA_PERMIT_DIRECTIO) ? 0 : -EACCES;
		goto out_digsig;
	}

	if (!pathbuf)	/* ima_rdwr_violation possibly pre-fetched */
		pathname = ima_d_path(&file->f_path, &pathbuf, filename);

	if (action & IMA_MEASURE)
		ima_store_measurement(iint, file, pathname,
				      xattr_value, xattr_len, pcr);
	if (action & IMA_APPRAISE_SUBMASK)
		rc = ima_appraise_measurement(func, iint, file, pathname,
					      xattr_value, xattr_len, opened);
	if (action & IMA_AUDIT)
		ima_audit_measurement(iint, pathname);

out_digsig:
	if ((mask & MAY_WRITE) && (iint->flags & IMA_DIGSIG) &&
	     !(iint->flags & IMA_NEW_FILE))
		rc = -EACCES;
	kfree(xattr_value);
out_free:
	if (pathbuf)
		__putname(pathbuf);
out:
	inode_unlock(inode);
	if ((rc && must_appraise) && (ima_appraise & IMA_APPRAISE_ENFORCE))
		return -EACCES;
	return 0;
}

/**
 * ima_file_mmap - based on policy, collect/store measurement.
 * @file: pointer to the file to be measured (May be NULL)
 * @prot: contains the protection that will be applied by the kernel.
 *
 * Measure files being mmapped executable based on the ima_must_measure()
 * policy decision.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
int ima_file_mmap(struct file *file, unsigned long prot)
{
	if (file && (prot & PROT_EXEC))
		return process_measurement(file, NULL, 0, MAY_EXEC,
					   MMAP_CHECK, 0);
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
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
int ima_bprm_check(struct linux_binprm *bprm)
{
	return process_measurement(bprm->file, NULL, 0, MAY_EXEC,
				   BPRM_CHECK, 0);
}

/**
 * ima_path_check - based on policy, collect/store measurement.
 * @file: pointer to the file to be measured
 * @mask: contains MAY_READ, MAY_WRITE, MAY_EXEC or MAY_APPEND
 *
 * Measure files based on the ima_must_measure() policy decision.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
int ima_file_check(struct file *file, int mask, int opened)
{
	return process_measurement(file, NULL, 0,
				   mask & (MAY_READ | MAY_WRITE | MAY_EXEC |
					   MAY_APPEND), FILE_CHECK, opened);
}
EXPORT_SYMBOL_GPL(ima_file_check);

/**
 * ima_post_path_mknod - mark as a new inode
 * @dentry: newly created dentry
 *
 * Mark files created via the mknodat syscall as new, so that the
 * file data can be written later.
 */
void ima_post_path_mknod(struct dentry *dentry)
{
	struct integrity_iint_cache *iint;
	struct inode *inode = dentry->d_inode;
	int must_appraise;

	must_appraise = ima_must_appraise(inode, MAY_ACCESS, FILE_CHECK);
	if (!must_appraise)
		return;

	iint = integrity_inode_get(inode);
	if (iint)
		iint->flags |= IMA_NEW_FILE;
}

/**
 * ima_read_file - pre-measure/appraise hook decision based on policy
 * @file: pointer to the file to be measured/appraised/audit
 * @read_id: caller identifier
 *
 * Permit reading a file based on policy. The policy rules are written
 * in terms of the policy identifier.  Appraising the integrity of
 * a file requires a file descriptor.
 *
 * For permission return 0, otherwise return -EACCES.
 */
int ima_read_file(struct file *file, enum kernel_read_file_id read_id)
{
	if (!file && read_id == READING_MODULE) {
#ifndef CONFIG_MODULE_SIG_FORCE
		if ((ima_appraise & IMA_APPRAISE_MODULES) &&
		    (ima_appraise & IMA_APPRAISE_ENFORCE))
			return -EACCES;	/* INTEGRITY_UNKNOWN */
#endif
		return 0;	/* We rely on module signature checking */
	}
	return 0;
}

static int read_idmap[READING_MAX_ID] = {
	[READING_FIRMWARE] = FIRMWARE_CHECK,
	[READING_MODULE] = MODULE_CHECK,
	[READING_KEXEC_IMAGE] = KEXEC_KERNEL_CHECK,
	[READING_KEXEC_INITRAMFS] = KEXEC_INITRAMFS_CHECK,
	[READING_POLICY] = POLICY_CHECK
};

/**
 * ima_post_read_file - in memory collect/appraise/audit measurement
 * @file: pointer to the file to be measured/appraised/audit
 * @buf: pointer to in memory file contents
 * @size: size of in memory file contents
 * @read_id: caller identifier
 *
 * Measure/appraise/audit in memory file based on policy.  Policy rules
 * are written in terms of a policy identifier.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
int ima_post_read_file(struct file *file, void *buf, loff_t size,
		       enum kernel_read_file_id read_id)
{
	enum ima_hooks func;

	if (!file && read_id == READING_FIRMWARE) {
		if ((ima_appraise & IMA_APPRAISE_FIRMWARE) &&
		    (ima_appraise & IMA_APPRAISE_ENFORCE))
			return -EACCES;	/* INTEGRITY_UNKNOWN */
		return 0;
	}

	if (!file && read_id == READING_MODULE) /* MODULE_SIG_FORCE enabled */
		return 0;

	if (!file || !buf || size == 0) { /* should never happen */
		if (ima_appraise & IMA_APPRAISE_ENFORCE)
			return -EACCES;
		return 0;
	}

	func = read_idmap[read_id] ?: FILE_CHECK;
	return process_measurement(file, buf, size, MAY_READ, func, 0);
}

static int __init init_ima(void)
{
	int error;

	ima_init_template_list();
	hash_setup(CONFIG_IMA_DEFAULT_HASH);
	error = ima_init();
	if (!error) {
		ima_initialized = 1;
		ima_update_policy_flag();
	}
	return error;
}

late_initcall(init_ima);	/* Start IMA after the TPM is available */

MODULE_DESCRIPTION("Integrity Measurement Architecture");
MODULE_LICENSE("GPL");
