// SPDX-License-Identifier: GPL-2.0-only
/*
 * Integrity Measurement Architecture
 *
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Serge Hallyn <serue@us.ibm.com>
 * Kylene Hall <kylene@us.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: ima_main.c
 *	implements the IMA hooks: ima_bprm_check, ima_file_mmap,
 *	and ima_file_check.
 */

#include <linux/module.h>
#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/kernel_read_file.h>
#include <linux/mount.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/ima.h>
#include <linux/fs.h>
#include <linux/iversion.h>

#include "ima.h"

#ifdef CONFIG_IMA_APPRAISE
int ima_appraise = IMA_APPRAISE_ENFORCE;
#else
int ima_appraise;
#endif

int __ro_after_init ima_hash_algo = HASH_ALGO_SHA1;
static int hash_setup_done;

static struct notifier_block ima_lsm_policy_notifier = {
	.notifier_call = ima_lsm_policy_change,
};

static int __init hash_setup(char *str)
{
	struct ima_template_desc *template_desc = ima_template_desc_current();
	int i;

	if (hash_setup_done)
		return 1;

	if (strcmp(template_desc->name, IMA_TEMPLATE_IMA_NAME) == 0) {
		if (strncmp(str, "sha1", 4) == 0) {
			ima_hash_algo = HASH_ALGO_SHA1;
		} else if (strncmp(str, "md5", 3) == 0) {
			ima_hash_algo = HASH_ALGO_MD5;
		} else {
			pr_err("invalid hash algorithm \"%s\" for template \"%s\"",
				str, IMA_TEMPLATE_IMA_NAME);
			return 1;
		}
		goto out;
	}

	i = match_string(hash_algo_name, HASH_ALGO__LAST, str);
	if (i < 0) {
		pr_err("invalid hash algorithm \"%s\"", str);
		return 1;
	}

	ima_hash_algo = i;
out:
	hash_setup_done = 1;
	return 1;
}
__setup("ima_hash=", hash_setup);

enum hash_algo ima_get_current_hash_algo(void)
{
	return ima_hash_algo;
}

/* Prevent mmap'ing a file execute that is already mmap'ed write */
static int mmap_violation_check(enum ima_hooks func, struct file *file,
				char **pathbuf, const char **pathname,
				char *filename)
{
	struct inode *inode;
	int rc = 0;

	if ((func == MMAP_CHECK || func == MMAP_CHECK_REQPROT) &&
	    mapping_writably_mapped(file->f_mapping)) {
		rc = -ETXTBSY;
		inode = file_inode(file);

		if (!*pathbuf)	/* ima_rdwr_violation possibly pre-fetched */
			*pathname = ima_d_path(&file->f_path, pathbuf,
					       filename);
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode, *pathname,
				    "mmap_file", "mmapped_writers", rc, 0);
	}
	return rc;
}

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
				     struct ima_iint_cache *iint,
				     int must_measure,
				     char **pathbuf,
				     const char **pathname,
				     char *filename)
{
	struct inode *inode = file_inode(file);
	fmode_t mode = file->f_mode;
	bool send_tomtou = false, send_writers = false;

	if (mode & FMODE_WRITE) {
		if (atomic_read(&inode->i_readcount) && IS_IMA(inode)) {
			if (!iint)
				iint = ima_iint_find(inode);
			/* IMA_MEASURE is set from reader side */
			if (iint && test_bit(IMA_MUST_MEASURE,
						&iint->atomic_flags))
				send_tomtou = true;
		}
	} else {
		if (must_measure)
			set_bit(IMA_MUST_MEASURE, &iint->atomic_flags);
		if (inode_is_open_for_write(inode) && must_measure)
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

static void ima_check_last_writer(struct ima_iint_cache *iint,
				  struct inode *inode, struct file *file)
{
	fmode_t mode = file->f_mode;
	bool update;

	if (!(mode & FMODE_WRITE))
		return;

	mutex_lock(&iint->mutex);
	if (atomic_read(&inode->i_writecount) == 1) {
		struct kstat stat;

		update = test_and_clear_bit(IMA_UPDATE_XATTR,
					    &iint->atomic_flags);
		if ((iint->flags & IMA_NEW_FILE) ||
		    vfs_getattr_nosec(&file->f_path, &stat,
				      STATX_CHANGE_COOKIE,
				      AT_STATX_SYNC_AS_STAT) ||
		    !(stat.result_mask & STATX_CHANGE_COOKIE) ||
		    stat.change_cookie != iint->version) {
			iint->flags &= ~(IMA_DONE_MASK | IMA_NEW_FILE);
			iint->measured_pcrs = 0;
			if (update)
				ima_update_xattr(iint, file);
		}
	}
	mutex_unlock(&iint->mutex);
}

/**
 * ima_file_free - called on __fput()
 * @file: pointer to file structure being freed
 *
 * Flag files that changed, based on i_version
 */
static void ima_file_free(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct ima_iint_cache *iint;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return;

	iint = ima_iint_find(inode);
	if (!iint)
		return;

	ima_check_last_writer(iint, inode, file);
}

static int process_measurement(struct file *file, const struct cred *cred,
			       u32 secid, char *buf, loff_t size, int mask,
			       enum ima_hooks func)
{
	struct inode *backing_inode, *inode = file_inode(file);
	struct ima_iint_cache *iint = NULL;
	struct ima_template_desc *template_desc = NULL;
	char *pathbuf = NULL;
	char filename[NAME_MAX];
	const char *pathname = NULL;
	int rc = 0, action, must_appraise = 0;
	int pcr = CONFIG_IMA_MEASURE_PCR_IDX;
	struct evm_ima_xattr_data *xattr_value = NULL;
	struct modsig *modsig = NULL;
	int xattr_len = 0;
	bool violation_check;
	enum hash_algo hash_algo;
	unsigned int allowed_algos = 0;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return 0;

	/* Return an IMA_MEASURE, IMA_APPRAISE, IMA_AUDIT action
	 * bitmask based on the appraise/audit/measurement policy.
	 * Included is the appraise submask.
	 */
	action = ima_get_action(file_mnt_idmap(file), inode, cred, secid,
				mask, func, &pcr, &template_desc, NULL,
				&allowed_algos);
	violation_check = ((func == FILE_CHECK || func == MMAP_CHECK ||
			    func == MMAP_CHECK_REQPROT) &&
			   (ima_policy_flag & IMA_MEASURE));
	if (!action && !violation_check)
		return 0;

	must_appraise = action & IMA_APPRAISE;

	/*  Is the appraise rule hook specific?  */
	if (action & IMA_FILE_APPRAISE)
		func = FILE_CHECK;

	inode_lock(inode);

	if (action) {
		iint = ima_inode_get(inode);
		if (!iint)
			rc = -ENOMEM;
	}

	if (!rc && violation_check)
		ima_rdwr_violation_check(file, iint, action & IMA_MEASURE,
					 &pathbuf, &pathname, filename);

	inode_unlock(inode);

	if (rc)
		goto out;
	if (!action)
		goto out;

	mutex_lock(&iint->mutex);

	if (test_and_clear_bit(IMA_CHANGE_ATTR, &iint->atomic_flags))
		/* reset appraisal flags if ima_inode_post_setattr was called */
		iint->flags &= ~(IMA_APPRAISE | IMA_APPRAISED |
				 IMA_APPRAISE_SUBMASK | IMA_APPRAISED_SUBMASK |
				 IMA_NONACTION_FLAGS);

	/*
	 * Re-evaulate the file if either the xattr has changed or the
	 * kernel has no way of detecting file change on the filesystem.
	 * (Limited to privileged mounted filesystems.)
	 */
	if (test_and_clear_bit(IMA_CHANGE_XATTR, &iint->atomic_flags) ||
	    ((inode->i_sb->s_iflags & SB_I_IMA_UNVERIFIABLE_SIGNATURE) &&
	     !(inode->i_sb->s_iflags & SB_I_UNTRUSTED_MOUNTER) &&
	     !(action & IMA_FAIL_UNVERIFIABLE_SIGS))) {
		iint->flags &= ~IMA_DONE_MASK;
		iint->measured_pcrs = 0;
	}

	/* Detect and re-evaluate changes made to the backing file. */
	backing_inode = d_real_inode(file_dentry(file));
	if (backing_inode != inode &&
	    (action & IMA_DO_MASK) && (iint->flags & IMA_DONE_MASK)) {
		if (!IS_I_VERSION(backing_inode) ||
		    backing_inode->i_sb->s_dev != iint->real_dev ||
		    backing_inode->i_ino != iint->real_ino ||
		    !inode_eq_iversion(backing_inode, iint->version)) {
			iint->flags &= ~IMA_DONE_MASK;
			iint->measured_pcrs = 0;
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

	/* HASH sets the digital signature and update flags, nothing else */
	if ((action & IMA_HASH) &&
	    !(test_bit(IMA_DIGSIG, &iint->atomic_flags))) {
		xattr_len = ima_read_xattr(file_dentry(file),
					   &xattr_value, xattr_len);
		if ((xattr_value && xattr_len > 2) &&
		    (xattr_value->type == EVM_IMA_XATTR_DIGSIG))
			set_bit(IMA_DIGSIG, &iint->atomic_flags);
		iint->flags |= IMA_HASHED;
		action ^= IMA_HASH;
		set_bit(IMA_UPDATE_XATTR, &iint->atomic_flags);
	}

	/* Nothing to do, just return existing appraised status */
	if (!action) {
		if (must_appraise) {
			rc = mmap_violation_check(func, file, &pathbuf,
						  &pathname, filename);
			if (!rc)
				rc = ima_get_cache_status(iint, func);
		}
		goto out_locked;
	}

	if ((action & IMA_APPRAISE_SUBMASK) ||
	    strcmp(template_desc->name, IMA_TEMPLATE_IMA_NAME) != 0) {
		/* read 'security.ima' */
		xattr_len = ima_read_xattr(file_dentry(file),
					   &xattr_value, xattr_len);

		/*
		 * Read the appended modsig if allowed by the policy, and allow
		 * an additional measurement list entry, if needed, based on the
		 * template format and whether the file was already measured.
		 */
		if (iint->flags & IMA_MODSIG_ALLOWED) {
			rc = ima_read_modsig(func, buf, size, &modsig);

			if (!rc && ima_template_has_modsig(template_desc) &&
			    iint->flags & IMA_MEASURED)
				action |= IMA_MEASURE;
		}
	}

	hash_algo = ima_get_hash_algo(xattr_value, xattr_len);

	rc = ima_collect_measurement(iint, file, buf, size, hash_algo, modsig);
	if (rc != 0 && rc != -EBADF && rc != -EINVAL)
		goto out_locked;

	if (!pathbuf)	/* ima_rdwr_violation possibly pre-fetched */
		pathname = ima_d_path(&file->f_path, &pathbuf, filename);

	if (action & IMA_MEASURE)
		ima_store_measurement(iint, file, pathname,
				      xattr_value, xattr_len, modsig, pcr,
				      template_desc);
	if (rc == 0 && (action & IMA_APPRAISE_SUBMASK)) {
		rc = ima_check_blacklist(iint, modsig, pcr);
		if (rc != -EPERM) {
			inode_lock(inode);
			rc = ima_appraise_measurement(func, iint, file,
						      pathname, xattr_value,
						      xattr_len, modsig);
			inode_unlock(inode);
		}
		if (!rc)
			rc = mmap_violation_check(func, file, &pathbuf,
						  &pathname, filename);
	}
	if (action & IMA_AUDIT)
		ima_audit_measurement(iint, pathname);

	if ((file->f_flags & O_DIRECT) && (iint->flags & IMA_PERMIT_DIRECTIO))
		rc = 0;

	/* Ensure the digest was generated using an allowed algorithm */
	if (rc == 0 && must_appraise && allowed_algos != 0 &&
	    (allowed_algos & (1U << hash_algo)) == 0) {
		rc = -EACCES;

		integrity_audit_msg(AUDIT_INTEGRITY_DATA, file_inode(file),
				    pathname, "collect_data",
				    "denied-hash-algorithm", rc, 0);
	}
out_locked:
	if ((mask & MAY_WRITE) && test_bit(IMA_DIGSIG, &iint->atomic_flags) &&
	     !(iint->flags & IMA_NEW_FILE))
		rc = -EACCES;
	mutex_unlock(&iint->mutex);
	kfree(xattr_value);
	ima_free_modsig(modsig);
out:
	if (pathbuf)
		__putname(pathbuf);
	if (must_appraise) {
		if (rc && (ima_appraise & IMA_APPRAISE_ENFORCE))
			return -EACCES;
		if (file->f_mode & FMODE_WRITE)
			set_bit(IMA_UPDATE_XATTR, &iint->atomic_flags);
	}
	return 0;
}

/**
 * ima_file_mmap - based on policy, collect/store measurement.
 * @file: pointer to the file to be measured (May be NULL)
 * @reqprot: protection requested by the application
 * @prot: protection that will be applied by the kernel
 * @flags: operational flags
 *
 * Measure files being mmapped executable based on the ima_must_measure()
 * policy decision.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
static int ima_file_mmap(struct file *file, unsigned long reqprot,
			 unsigned long prot, unsigned long flags)
{
	u32 secid;
	int ret;

	if (!file)
		return 0;

	security_current_getsecid_subj(&secid);

	if (reqprot & PROT_EXEC) {
		ret = process_measurement(file, current_cred(), secid, NULL,
					  0, MAY_EXEC, MMAP_CHECK_REQPROT);
		if (ret)
			return ret;
	}

	if (prot & PROT_EXEC)
		return process_measurement(file, current_cred(), secid, NULL,
					   0, MAY_EXEC, MMAP_CHECK);

	return 0;
}

/**
 * ima_file_mprotect - based on policy, limit mprotect change
 * @vma: vm_area_struct protection is set to
 * @reqprot: protection requested by the application
 * @prot: protection that will be applied by the kernel
 *
 * Files can be mmap'ed read/write and later changed to execute to circumvent
 * IMA's mmap appraisal policy rules.  Due to locking issues (mmap semaphore
 * would be taken before i_mutex), files can not be measured or appraised at
 * this point.  Eliminate this integrity gap by denying the mprotect
 * PROT_EXECUTE change, if an mmap appraise policy rule exists.
 *
 * On mprotect change success, return 0.  On failure, return -EACESS.
 */
static int ima_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			     unsigned long prot)
{
	struct ima_template_desc *template = NULL;
	struct file *file;
	char filename[NAME_MAX];
	char *pathbuf = NULL;
	const char *pathname = NULL;
	struct inode *inode;
	int result = 0;
	int action;
	u32 secid;
	int pcr;

	/* Is mprotect making an mmap'ed file executable? */
	if (!(ima_policy_flag & IMA_APPRAISE) || !vma->vm_file ||
	    !(prot & PROT_EXEC) || (vma->vm_flags & VM_EXEC))
		return 0;

	security_current_getsecid_subj(&secid);
	inode = file_inode(vma->vm_file);
	action = ima_get_action(file_mnt_idmap(vma->vm_file), inode,
				current_cred(), secid, MAY_EXEC, MMAP_CHECK,
				&pcr, &template, NULL, NULL);
	action |= ima_get_action(file_mnt_idmap(vma->vm_file), inode,
				 current_cred(), secid, MAY_EXEC,
				 MMAP_CHECK_REQPROT, &pcr, &template, NULL,
				 NULL);

	/* Is the mmap'ed file in policy? */
	if (!(action & (IMA_MEASURE | IMA_APPRAISE_SUBMASK)))
		return 0;

	if (action & IMA_APPRAISE_SUBMASK)
		result = -EPERM;

	file = vma->vm_file;
	pathname = ima_d_path(&file->f_path, &pathbuf, filename);
	integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode, pathname,
			    "collect_data", "failed-mprotect", result, 0);
	if (pathbuf)
		__putname(pathbuf);

	return result;
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
static int ima_bprm_check(struct linux_binprm *bprm)
{
	int ret;
	u32 secid;

	security_current_getsecid_subj(&secid);
	ret = process_measurement(bprm->file, current_cred(), secid, NULL, 0,
				  MAY_EXEC, BPRM_CHECK);
	if (ret)
		return ret;

	security_cred_getsecid(bprm->cred, &secid);
	return process_measurement(bprm->file, bprm->cred, secid, NULL, 0,
				   MAY_EXEC, CREDS_CHECK);
}

/**
 * ima_file_check - based on policy, collect/store measurement.
 * @file: pointer to the file to be measured
 * @mask: contains MAY_READ, MAY_WRITE, MAY_EXEC or MAY_APPEND
 *
 * Measure files based on the ima_must_measure() policy decision.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
static int ima_file_check(struct file *file, int mask)
{
	u32 secid;

	security_current_getsecid_subj(&secid);
	return process_measurement(file, current_cred(), secid, NULL, 0,
				   mask & (MAY_READ | MAY_WRITE | MAY_EXEC |
					   MAY_APPEND), FILE_CHECK);
}

static int __ima_inode_hash(struct inode *inode, struct file *file, char *buf,
			    size_t buf_size)
{
	struct ima_iint_cache *iint = NULL, tmp_iint;
	int rc, hash_algo;

	if (ima_policy_flag) {
		iint = ima_iint_find(inode);
		if (iint)
			mutex_lock(&iint->mutex);
	}

	if ((!iint || !(iint->flags & IMA_COLLECTED)) && file) {
		if (iint)
			mutex_unlock(&iint->mutex);

		memset(&tmp_iint, 0, sizeof(tmp_iint));
		mutex_init(&tmp_iint.mutex);

		rc = ima_collect_measurement(&tmp_iint, file, NULL, 0,
					     ima_hash_algo, NULL);
		if (rc < 0) {
			/* ima_hash could be allocated in case of failure. */
			if (rc != -ENOMEM)
				kfree(tmp_iint.ima_hash);

			return -EOPNOTSUPP;
		}

		iint = &tmp_iint;
		mutex_lock(&iint->mutex);
	}

	if (!iint)
		return -EOPNOTSUPP;

	/*
	 * ima_file_hash can be called when ima_collect_measurement has still
	 * not been called, we might not always have a hash.
	 */
	if (!iint->ima_hash || !(iint->flags & IMA_COLLECTED)) {
		mutex_unlock(&iint->mutex);
		return -EOPNOTSUPP;
	}

	if (buf) {
		size_t copied_size;

		copied_size = min_t(size_t, iint->ima_hash->length, buf_size);
		memcpy(buf, iint->ima_hash->digest, copied_size);
	}
	hash_algo = iint->ima_hash->algo;
	mutex_unlock(&iint->mutex);

	if (iint == &tmp_iint)
		kfree(iint->ima_hash);

	return hash_algo;
}

/**
 * ima_file_hash - return a measurement of the file
 * @file: pointer to the file
 * @buf: buffer in which to store the hash
 * @buf_size: length of the buffer
 *
 * On success, return the hash algorithm (as defined in the enum hash_algo).
 * If buf is not NULL, this function also outputs the hash into buf.
 * If the hash is larger than buf_size, then only buf_size bytes will be copied.
 * It generally just makes sense to pass a buffer capable of holding the largest
 * possible hash: IMA_MAX_DIGEST_SIZE.
 * The file hash returned is based on the entire file, including the appended
 * signature.
 *
 * If the measurement cannot be performed, return -EOPNOTSUPP.
 * If the parameters are incorrect, return -EINVAL.
 */
int ima_file_hash(struct file *file, char *buf, size_t buf_size)
{
	if (!file)
		return -EINVAL;

	return __ima_inode_hash(file_inode(file), file, buf, buf_size);
}
EXPORT_SYMBOL_GPL(ima_file_hash);

/**
 * ima_inode_hash - return the stored measurement if the inode has been hashed
 * and is in the iint cache.
 * @inode: pointer to the inode
 * @buf: buffer in which to store the hash
 * @buf_size: length of the buffer
 *
 * On success, return the hash algorithm (as defined in the enum hash_algo).
 * If buf is not NULL, this function also outputs the hash into buf.
 * If the hash is larger than buf_size, then only buf_size bytes will be copied.
 * It generally just makes sense to pass a buffer capable of holding the largest
 * possible hash: IMA_MAX_DIGEST_SIZE.
 * The hash returned is based on the entire contents, including the appended
 * signature.
 *
 * If IMA is disabled or if no measurement is available, return -EOPNOTSUPP.
 * If the parameters are incorrect, return -EINVAL.
 */
int ima_inode_hash(struct inode *inode, char *buf, size_t buf_size)
{
	if (!inode)
		return -EINVAL;

	return __ima_inode_hash(inode, NULL, buf, buf_size);
}
EXPORT_SYMBOL_GPL(ima_inode_hash);

/**
 * ima_post_create_tmpfile - mark newly created tmpfile as new
 * @idmap: idmap of the mount the inode was found from
 * @inode: inode of the newly created tmpfile
 *
 * No measuring, appraising or auditing of newly created tmpfiles is needed.
 * Skip calling process_measurement(), but indicate which newly, created
 * tmpfiles are in policy.
 */
static void ima_post_create_tmpfile(struct mnt_idmap *idmap,
				    struct inode *inode)

{
	struct ima_iint_cache *iint;
	int must_appraise;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return;

	must_appraise = ima_must_appraise(idmap, inode, MAY_ACCESS,
					  FILE_CHECK);
	if (!must_appraise)
		return;

	/* Nothing to do if we can't allocate memory */
	iint = ima_inode_get(inode);
	if (!iint)
		return;

	/* needed for writing the security xattrs */
	set_bit(IMA_UPDATE_XATTR, &iint->atomic_flags);
	iint->ima_file_status = INTEGRITY_PASS;
}

/**
 * ima_post_path_mknod - mark as a new inode
 * @idmap: idmap of the mount the inode was found from
 * @dentry: newly created dentry
 *
 * Mark files created via the mknodat syscall as new, so that the
 * file data can be written later.
 */
static void ima_post_path_mknod(struct mnt_idmap *idmap, struct dentry *dentry)
{
	struct ima_iint_cache *iint;
	struct inode *inode = dentry->d_inode;
	int must_appraise;

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return;

	must_appraise = ima_must_appraise(idmap, inode, MAY_ACCESS,
					  FILE_CHECK);
	if (!must_appraise)
		return;

	/* Nothing to do if we can't allocate memory */
	iint = ima_inode_get(inode);
	if (!iint)
		return;

	/* needed for re-opening empty files */
	iint->flags |= IMA_NEW_FILE;
}

/**
 * ima_read_file - pre-measure/appraise hook decision based on policy
 * @file: pointer to the file to be measured/appraised/audit
 * @read_id: caller identifier
 * @contents: whether a subsequent call will be made to ima_post_read_file()
 *
 * Permit reading a file based on policy. The policy rules are written
 * in terms of the policy identifier.  Appraising the integrity of
 * a file requires a file descriptor.
 *
 * For permission return 0, otherwise return -EACCES.
 */
static int ima_read_file(struct file *file, enum kernel_read_file_id read_id,
			 bool contents)
{
	enum ima_hooks func;
	u32 secid;

	/*
	 * Do devices using pre-allocated memory run the risk of the
	 * firmware being accessible to the device prior to the completion
	 * of IMA's signature verification any more than when using two
	 * buffers? It may be desirable to include the buffer address
	 * in this API and walk all the dma_map_single() mappings to check.
	 */

	/*
	 * There will be a call made to ima_post_read_file() with
	 * a filled buffer, so we don't need to perform an extra
	 * read early here.
	 */
	if (contents)
		return 0;

	/* Read entire file for all partial reads. */
	func = read_idmap[read_id] ?: FILE_CHECK;
	security_current_getsecid_subj(&secid);
	return process_measurement(file, current_cred(), secid, NULL,
				   0, MAY_READ, func);
}

const int read_idmap[READING_MAX_ID] = {
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
static int ima_post_read_file(struct file *file, char *buf, loff_t size,
			      enum kernel_read_file_id read_id)
{
	enum ima_hooks func;
	u32 secid;

	/* permit signed certs */
	if (!file && read_id == READING_X509_CERTIFICATE)
		return 0;

	if (!file || !buf || size == 0) { /* should never happen */
		if (ima_appraise & IMA_APPRAISE_ENFORCE)
			return -EACCES;
		return 0;
	}

	func = read_idmap[read_id] ?: FILE_CHECK;
	security_current_getsecid_subj(&secid);
	return process_measurement(file, current_cred(), secid, buf, size,
				   MAY_READ, func);
}

/**
 * ima_load_data - appraise decision based on policy
 * @id: kernel load data caller identifier
 * @contents: whether the full contents will be available in a later
 *	      call to ima_post_load_data().
 *
 * Callers of this LSM hook can not measure, appraise, or audit the
 * data provided by userspace.  Enforce policy rules requiring a file
 * signature (eg. kexec'ed kernel image).
 *
 * For permission return 0, otherwise return -EACCES.
 */
static int ima_load_data(enum kernel_load_data_id id, bool contents)
{
	bool ima_enforce, sig_enforce;

	ima_enforce =
		(ima_appraise & IMA_APPRAISE_ENFORCE) == IMA_APPRAISE_ENFORCE;

	switch (id) {
	case LOADING_KEXEC_IMAGE:
		if (IS_ENABLED(CONFIG_KEXEC_SIG)
		    && arch_ima_get_secureboot()) {
			pr_err("impossible to appraise a kernel image without a file descriptor; try using kexec_file_load syscall.\n");
			return -EACCES;
		}

		if (ima_enforce && (ima_appraise & IMA_APPRAISE_KEXEC)) {
			pr_err("impossible to appraise a kernel image without a file descriptor; try using kexec_file_load syscall.\n");
			return -EACCES;	/* INTEGRITY_UNKNOWN */
		}
		break;
	case LOADING_FIRMWARE:
		if (ima_enforce && (ima_appraise & IMA_APPRAISE_FIRMWARE) && !contents) {
			pr_err("Prevent firmware sysfs fallback loading.\n");
			return -EACCES;	/* INTEGRITY_UNKNOWN */
		}
		break;
	case LOADING_MODULE:
		sig_enforce = is_module_sig_enforced();

		if (ima_enforce && (!sig_enforce
				    && (ima_appraise & IMA_APPRAISE_MODULES))) {
			pr_err("impossible to appraise a module without a file descriptor. sig_enforce kernel parameter might help\n");
			return -EACCES;	/* INTEGRITY_UNKNOWN */
		}
		break;
	default:
		break;
	}
	return 0;
}

/**
 * ima_post_load_data - appraise decision based on policy
 * @buf: pointer to in memory file contents
 * @size: size of in memory file contents
 * @load_id: kernel load data caller identifier
 * @description: @load_id-specific description of contents
 *
 * Measure/appraise/audit in memory buffer based on policy.  Policy rules
 * are written in terms of a policy identifier.
 *
 * On success return 0.  On integrity appraisal error, assuming the file
 * is in policy and IMA-appraisal is in enforcing mode, return -EACCES.
 */
static int ima_post_load_data(char *buf, loff_t size,
			      enum kernel_load_data_id load_id,
			      char *description)
{
	if (load_id == LOADING_FIRMWARE) {
		if ((ima_appraise & IMA_APPRAISE_FIRMWARE) &&
		    (ima_appraise & IMA_APPRAISE_ENFORCE)) {
			pr_err("Prevent firmware loading_store.\n");
			return -EACCES; /* INTEGRITY_UNKNOWN */
		}
		return 0;
	}

	return 0;
}

/**
 * process_buffer_measurement - Measure the buffer or the buffer data hash
 * @idmap: idmap of the mount the inode was found from
 * @inode: inode associated with the object being measured (NULL for KEY_CHECK)
 * @buf: pointer to the buffer that needs to be added to the log.
 * @size: size of buffer(in bytes).
 * @eventname: event name to be used for the buffer entry.
 * @func: IMA hook
 * @pcr: pcr to extend the measurement
 * @func_data: func specific data, may be NULL
 * @buf_hash: measure buffer data hash
 * @digest: buffer digest will be written to
 * @digest_len: buffer length
 *
 * Based on policy, either the buffer data or buffer data hash is measured
 *
 * Return: 0 if the buffer has been successfully measured, 1 if the digest
 * has been written to the passed location but not added to a measurement entry,
 * a negative value otherwise.
 */
int process_buffer_measurement(struct mnt_idmap *idmap,
			       struct inode *inode, const void *buf, int size,
			       const char *eventname, enum ima_hooks func,
			       int pcr, const char *func_data,
			       bool buf_hash, u8 *digest, size_t digest_len)
{
	int ret = 0;
	const char *audit_cause = "ENOMEM";
	struct ima_template_entry *entry = NULL;
	struct ima_iint_cache iint = {};
	struct ima_event_data event_data = {.iint = &iint,
					    .filename = eventname,
					    .buf = buf,
					    .buf_len = size};
	struct ima_template_desc *template;
	struct ima_max_digest_data hash;
	char digest_hash[IMA_MAX_DIGEST_SIZE];
	int digest_hash_len = hash_digest_size[ima_hash_algo];
	int violation = 0;
	int action = 0;
	u32 secid;

	if (digest && digest_len < digest_hash_len)
		return -EINVAL;

	if (!ima_policy_flag && !digest)
		return -ENOENT;

	template = ima_template_desc_buf();
	if (!template) {
		ret = -EINVAL;
		audit_cause = "ima_template_desc_buf";
		goto out;
	}

	/*
	 * Both LSM hooks and auxilary based buffer measurements are
	 * based on policy.  To avoid code duplication, differentiate
	 * between the LSM hooks and auxilary buffer measurements,
	 * retrieving the policy rule information only for the LSM hook
	 * buffer measurements.
	 */
	if (func) {
		security_current_getsecid_subj(&secid);
		action = ima_get_action(idmap, inode, current_cred(),
					secid, 0, func, &pcr, &template,
					func_data, NULL);
		if (!(action & IMA_MEASURE) && !digest)
			return -ENOENT;
	}

	if (!pcr)
		pcr = CONFIG_IMA_MEASURE_PCR_IDX;

	iint.ima_hash = &hash.hdr;
	iint.ima_hash->algo = ima_hash_algo;
	iint.ima_hash->length = hash_digest_size[ima_hash_algo];

	ret = ima_calc_buffer_hash(buf, size, iint.ima_hash);
	if (ret < 0) {
		audit_cause = "hashing_error";
		goto out;
	}

	if (buf_hash) {
		memcpy(digest_hash, hash.hdr.digest, digest_hash_len);

		ret = ima_calc_buffer_hash(digest_hash, digest_hash_len,
					   iint.ima_hash);
		if (ret < 0) {
			audit_cause = "hashing_error";
			goto out;
		}

		event_data.buf = digest_hash;
		event_data.buf_len = digest_hash_len;
	}

	if (digest)
		memcpy(digest, iint.ima_hash->digest, digest_hash_len);

	if (!ima_policy_flag || (func && !(action & IMA_MEASURE)))
		return 1;

	ret = ima_alloc_init_template(&event_data, &entry, template);
	if (ret < 0) {
		audit_cause = "alloc_entry";
		goto out;
	}

	ret = ima_store_template(entry, violation, NULL, event_data.buf, pcr);
	if (ret < 0) {
		audit_cause = "store_entry";
		ima_free_template_entry(entry);
	}

out:
	if (ret < 0)
		integrity_audit_message(AUDIT_INTEGRITY_PCR, NULL, eventname,
					func_measure_str(func),
					audit_cause, ret, 0, ret);

	return ret;
}

/**
 * ima_kexec_cmdline - measure kexec cmdline boot args
 * @kernel_fd: file descriptor of the kexec kernel being loaded
 * @buf: pointer to buffer
 * @size: size of buffer
 *
 * Buffers can only be measured, not appraised.
 */
void ima_kexec_cmdline(int kernel_fd, const void *buf, int size)
{
	struct fd f;

	if (!buf || !size)
		return;

	f = fdget(kernel_fd);
	if (!f.file)
		return;

	process_buffer_measurement(file_mnt_idmap(f.file), file_inode(f.file),
				   buf, size, "kexec-cmdline", KEXEC_CMDLINE, 0,
				   NULL, false, NULL, 0);
	fdput(f);
}

/**
 * ima_measure_critical_data - measure kernel integrity critical data
 * @event_label: unique event label for grouping and limiting critical data
 * @event_name: event name for the record in the IMA measurement list
 * @buf: pointer to buffer data
 * @buf_len: length of buffer data (in bytes)
 * @hash: measure buffer data hash
 * @digest: buffer digest will be written to
 * @digest_len: buffer length
 *
 * Measure data critical to the integrity of the kernel into the IMA log
 * and extend the pcr.  Examples of critical data could be various data
 * structures, policies, and states stored in kernel memory that can
 * impact the integrity of the system.
 *
 * Return: 0 if the buffer has been successfully measured, 1 if the digest
 * has been written to the passed location but not added to a measurement entry,
 * a negative value otherwise.
 */
int ima_measure_critical_data(const char *event_label,
			      const char *event_name,
			      const void *buf, size_t buf_len,
			      bool hash, u8 *digest, size_t digest_len)
{
	if (!event_name || !event_label || !buf || !buf_len)
		return -ENOPARAM;

	return process_buffer_measurement(&nop_mnt_idmap, NULL, buf, buf_len,
					  event_name, CRITICAL_DATA, 0,
					  event_label, hash, digest,
					  digest_len);
}
EXPORT_SYMBOL_GPL(ima_measure_critical_data);

#ifdef CONFIG_INTEGRITY_ASYMMETRIC_KEYS

/**
 * ima_kernel_module_request - Prevent crypto-pkcs1pad(rsa,*) requests
 * @kmod_name: kernel module name
 *
 * Avoid a verification loop where verifying the signature of the modprobe
 * binary requires executing modprobe itself. Since the modprobe iint->mutex
 * is already held when the signature verification is performed, a deadlock
 * occurs as soon as modprobe is executed within the critical region, since
 * the same lock cannot be taken again.
 *
 * This happens when public_key_verify_signature(), in case of RSA algorithm,
 * use alg_name to store internal information in order to construct an
 * algorithm on the fly, but crypto_larval_lookup() will try to use alg_name
 * in order to load a kernel module with same name.
 *
 * Since we don't have any real "crypto-pkcs1pad(rsa,*)" kernel modules,
 * we are safe to fail such module request from crypto_larval_lookup(), and
 * avoid the verification loop.
 *
 * Return: Zero if it is safe to load the kernel module, -EINVAL otherwise.
 */
static int ima_kernel_module_request(char *kmod_name)
{
	if (strncmp(kmod_name, "crypto-pkcs1pad(rsa,", 20) == 0)
		return -EINVAL;

	return 0;
}

#endif /* CONFIG_INTEGRITY_ASYMMETRIC_KEYS */

static int __init init_ima(void)
{
	int error;

	ima_appraise_parse_cmdline();
	ima_init_template_list();
	hash_setup(CONFIG_IMA_DEFAULT_HASH);
	error = ima_init();

	if (error && strcmp(hash_algo_name[ima_hash_algo],
			    CONFIG_IMA_DEFAULT_HASH) != 0) {
		pr_info("Allocating %s failed, going to use default hash algorithm %s\n",
			hash_algo_name[ima_hash_algo], CONFIG_IMA_DEFAULT_HASH);
		hash_setup_done = 0;
		hash_setup(CONFIG_IMA_DEFAULT_HASH);
		error = ima_init();
	}

	if (error)
		return error;

	error = register_blocking_lsm_notifier(&ima_lsm_policy_notifier);
	if (error)
		pr_warn("Couldn't register LSM notifier, error %d\n", error);

	if (!error)
		ima_update_policy_flags();

	return error;
}

static struct security_hook_list ima_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(bprm_check_security, ima_bprm_check),
	LSM_HOOK_INIT(file_post_open, ima_file_check),
	LSM_HOOK_INIT(inode_post_create_tmpfile, ima_post_create_tmpfile),
	LSM_HOOK_INIT(file_release, ima_file_free),
	LSM_HOOK_INIT(mmap_file, ima_file_mmap),
	LSM_HOOK_INIT(file_mprotect, ima_file_mprotect),
	LSM_HOOK_INIT(kernel_load_data, ima_load_data),
	LSM_HOOK_INIT(kernel_post_load_data, ima_post_load_data),
	LSM_HOOK_INIT(kernel_read_file, ima_read_file),
	LSM_HOOK_INIT(kernel_post_read_file, ima_post_read_file),
	LSM_HOOK_INIT(path_post_mknod, ima_post_path_mknod),
#ifdef CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS
	LSM_HOOK_INIT(key_post_create_or_update, ima_post_key_create_or_update),
#endif
#ifdef CONFIG_INTEGRITY_ASYMMETRIC_KEYS
	LSM_HOOK_INIT(kernel_module_request, ima_kernel_module_request),
#endif
	LSM_HOOK_INIT(inode_free_security, ima_inode_free),
};

static const struct lsm_id ima_lsmid = {
	.name = "ima",
	.id = LSM_ID_IMA,
};

static int __init init_ima_lsm(void)
{
	ima_iintcache_init();
	security_add_hooks(ima_hooks, ARRAY_SIZE(ima_hooks), &ima_lsmid);
	init_ima_appraise_lsm(&ima_lsmid);
	return 0;
}

struct lsm_blob_sizes ima_blob_sizes __ro_after_init = {
	.lbs_inode = sizeof(struct ima_iint_cache *),
};

DEFINE_LSM(ima) = {
	.name = "ima",
	.init = init_ima_lsm,
	.order = LSM_ORDER_LAST,
	.blobs = &ima_blob_sizes,
};

late_initcall(init_ima);	/* Start IMA after the TPM is available */
