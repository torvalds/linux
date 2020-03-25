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
#include <linux/mount.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/ima.h>
#include <linux/iversion.h>
#include <linux/fs.h>

#include "ima.h"

#ifdef CONFIG_IMA_APPRAISE
int ima_appraise = IMA_APPRAISE_ENFORCE;
#else
int ima_appraise;
#endif

int ima_hash_algo = HASH_ALGO_SHA1;
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
		if (strncmp(str, "sha1", 4) == 0)
			ima_hash_algo = HASH_ALGO_SHA1;
		else if (strncmp(str, "md5", 3) == 0)
			ima_hash_algo = HASH_ALGO_MD5;
		else
			return 1;
		goto out;
	}

	i = match_string(hash_algo_name, HASH_ALGO__LAST, str);
	if (i < 0)
		return 1;

	ima_hash_algo = i;
out:
	hash_setup_done = 1;
	return 1;
}
__setup("ima_hash=", hash_setup);

/* Prevent mmap'ing a file execute that is already mmap'ed write */
static int mmap_violation_check(enum ima_hooks func, struct file *file,
				char **pathbuf, const char **pathname,
				char *filename)
{
	struct inode *inode;
	int rc = 0;

	if ((func == MMAP_CHECK) && mapping_writably_mapped(file->f_mapping)) {
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
				     struct integrity_iint_cache *iint,
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
				iint = integrity_iint_find(inode);
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

static void ima_check_last_writer(struct integrity_iint_cache *iint,
				  struct inode *inode, struct file *file)
{
	fmode_t mode = file->f_mode;
	bool update;

	if (!(mode & FMODE_WRITE))
		return;

	mutex_lock(&iint->mutex);
	if (atomic_read(&inode->i_writecount) == 1) {
		update = test_and_clear_bit(IMA_UPDATE_XATTR,
					    &iint->atomic_flags);
		if (!IS_I_VERSION(inode) ||
		    !inode_eq_iversion(inode, iint->version) ||
		    (iint->flags & IMA_NEW_FILE)) {
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

static int process_measurement(struct file *file, const struct cred *cred,
			       u32 secid, char *buf, loff_t size, int mask,
			       enum ima_hooks func)
{
	struct inode *inode = file_inode(file);
	struct integrity_iint_cache *iint = NULL;
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

	if (!ima_policy_flag || !S_ISREG(inode->i_mode))
		return 0;

	/* Return an IMA_MEASURE, IMA_APPRAISE, IMA_AUDIT action
	 * bitmask based on the appraise/audit/measurement policy.
	 * Included is the appraise submask.
	 */
	action = ima_get_action(inode, cred, secid, mask, func, &pcr,
				&template_desc, NULL);
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
				 IMA_ACTION_FLAGS);

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
		xattr_len = ima_read_xattr(file_dentry(file), &xattr_value);
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
		xattr_len = ima_read_xattr(file_dentry(file), &xattr_value);

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
	u32 secid;

	if (file && (prot & PROT_EXEC)) {
		security_task_getsecid(current, &secid);
		return process_measurement(file, current_cred(), secid, NULL,
					   0, MAY_EXEC, MMAP_CHECK);
	}

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
	int ret;
	u32 secid;

	security_task_getsecid(current, &secid);
	ret = process_measurement(bprm->file, current_cred(), secid, NULL, 0,
				  MAY_EXEC, BPRM_CHECK);
	if (ret)
		return ret;

	security_cred_getsecid(bprm->cred, &secid);
	return process_measurement(bprm->file, bprm->cred, secid, NULL, 0,
				   MAY_EXEC, CREDS_CHECK);
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
int ima_file_check(struct file *file, int mask)
{
	u32 secid;

	security_task_getsecid(current, &secid);
	return process_measurement(file, current_cred(), secid, NULL, 0,
				   mask & (MAY_READ | MAY_WRITE | MAY_EXEC |
					   MAY_APPEND), FILE_CHECK);
}
EXPORT_SYMBOL_GPL(ima_file_check);

/**
 * ima_file_hash - return the stored measurement if a file has been hashed and
 * is in the iint cache.
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
 * If IMA is disabled or if no measurement is available, return -EOPNOTSUPP.
 * If the parameters are incorrect, return -EINVAL.
 */
int ima_file_hash(struct file *file, char *buf, size_t buf_size)
{
	struct inode *inode;
	struct integrity_iint_cache *iint;
	int hash_algo;

	if (!file)
		return -EINVAL;

	if (!ima_policy_flag)
		return -EOPNOTSUPP;

	inode = file_inode(file);
	iint = integrity_iint_find(inode);
	if (!iint)
		return -EOPNOTSUPP;

	mutex_lock(&iint->mutex);
	if (buf) {
		size_t copied_size;

		copied_size = min_t(size_t, iint->ima_hash->length, buf_size);
		memcpy(buf, iint->ima_hash->digest, copied_size);
	}
	hash_algo = iint->ima_hash->algo;
	mutex_unlock(&iint->mutex);

	return hash_algo;
}
EXPORT_SYMBOL_GPL(ima_file_hash);

/**
 * ima_post_create_tmpfile - mark newly created tmpfile as new
 * @file : newly created tmpfile
 *
 * No measuring, appraising or auditing of newly created tmpfiles is needed.
 * Skip calling process_measurement(), but indicate which newly, created
 * tmpfiles are in policy.
 */
void ima_post_create_tmpfile(struct inode *inode)
{
	struct integrity_iint_cache *iint;
	int must_appraise;

	must_appraise = ima_must_appraise(inode, MAY_ACCESS, FILE_CHECK);
	if (!must_appraise)
		return;

	/* Nothing to do if we can't allocate memory */
	iint = integrity_inode_get(inode);
	if (!iint)
		return;

	/* needed for writing the security xattrs */
	set_bit(IMA_UPDATE_XATTR, &iint->atomic_flags);
	iint->ima_file_status = INTEGRITY_PASS;
}

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

	/* Nothing to do if we can't allocate memory */
	iint = integrity_inode_get(inode);
	if (!iint)
		return;

	/* needed for re-opening empty files */
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
	/*
	 * READING_FIRMWARE_PREALLOC_BUFFER
	 *
	 * Do devices using pre-allocated memory run the risk of the
	 * firmware being accessible to the device prior to the completion
	 * of IMA's signature verification any more than when using two
	 * buffers?
	 */
	return 0;
}

const int read_idmap[READING_MAX_ID] = {
	[READING_FIRMWARE] = FIRMWARE_CHECK,
	[READING_FIRMWARE_PREALLOC_BUFFER] = FIRMWARE_CHECK,
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
	u32 secid;

	if (!file && read_id == READING_FIRMWARE) {
		if ((ima_appraise & IMA_APPRAISE_FIRMWARE) &&
		    (ima_appraise & IMA_APPRAISE_ENFORCE)) {
			pr_err("Prevent firmware loading_store.\n");
			return -EACCES;	/* INTEGRITY_UNKNOWN */
		}
		return 0;
	}

	/* permit signed certs */
	if (!file && read_id == READING_X509_CERTIFICATE)
		return 0;

	if (!file || !buf || size == 0) { /* should never happen */
		if (ima_appraise & IMA_APPRAISE_ENFORCE)
			return -EACCES;
		return 0;
	}

	func = read_idmap[read_id] ?: FILE_CHECK;
	security_task_getsecid(current, &secid);
	return process_measurement(file, current_cred(), secid, buf, size,
				   MAY_READ, func);
}

/**
 * ima_load_data - appraise decision based on policy
 * @id: kernel load data caller identifier
 *
 * Callers of this LSM hook can not measure, appraise, or audit the
 * data provided by userspace.  Enforce policy rules requring a file
 * signature (eg. kexec'ed kernel image).
 *
 * For permission return 0, otherwise return -EACCES.
 */
int ima_load_data(enum kernel_load_data_id id)
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
		if (ima_enforce && (ima_appraise & IMA_APPRAISE_FIRMWARE)) {
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
	default:
		break;
	}
	return 0;
}

/*
 * process_buffer_measurement - Measure the buffer to ima log.
 * @buf: pointer to the buffer that needs to be added to the log.
 * @size: size of buffer(in bytes).
 * @eventname: event name to be used for the buffer entry.
 * @func: IMA hook
 * @pcr: pcr to extend the measurement
 * @keyring: keyring name to determine the action to be performed
 *
 * Based on policy, the buffer is measured into the ima log.
 */
void process_buffer_measurement(const void *buf, int size,
				const char *eventname, enum ima_hooks func,
				int pcr, const char *keyring)
{
	int ret = 0;
	struct ima_template_entry *entry = NULL;
	struct integrity_iint_cache iint = {};
	struct ima_event_data event_data = {.iint = &iint,
					    .filename = eventname,
					    .buf = buf,
					    .buf_len = size};
	struct ima_template_desc *template = NULL;
	struct {
		struct ima_digest_data hdr;
		char digest[IMA_MAX_DIGEST_SIZE];
	} hash = {};
	int violation = 0;
	int action = 0;
	u32 secid;

	if (!ima_policy_flag)
		return;

	/*
	 * Both LSM hooks and auxilary based buffer measurements are
	 * based on policy.  To avoid code duplication, differentiate
	 * between the LSM hooks and auxilary buffer measurements,
	 * retrieving the policy rule information only for the LSM hook
	 * buffer measurements.
	 */
	if (func) {
		security_task_getsecid(current, &secid);
		action = ima_get_action(NULL, current_cred(), secid, 0, func,
					&pcr, &template, keyring);
		if (!(action & IMA_MEASURE))
			return;
	}

	if (!pcr)
		pcr = CONFIG_IMA_MEASURE_PCR_IDX;

	if (!template) {
		template = lookup_template_desc("ima-buf");
		ret = template_desc_init_fields(template->fmt,
						&(template->fields),
						&(template->num_fields));
		if (ret < 0) {
			pr_err("template %s init failed, result: %d\n",
			       (strlen(template->name) ?
				template->name : template->fmt), ret);
			return;
		}
	}

	iint.ima_hash = &hash.hdr;
	iint.ima_hash->algo = ima_hash_algo;
	iint.ima_hash->length = hash_digest_size[ima_hash_algo];

	ret = ima_calc_buffer_hash(buf, size, iint.ima_hash);
	if (ret < 0)
		goto out;

	ret = ima_alloc_init_template(&event_data, &entry, template);
	if (ret < 0)
		goto out;

	ret = ima_store_template(entry, violation, NULL, buf, pcr);

	if (ret < 0)
		ima_free_template_entry(entry);

out:
	if (ret < 0)
		pr_devel("%s: failed, result: %d\n", __func__, ret);

	return;
}

/**
 * ima_kexec_cmdline - measure kexec cmdline boot args
 * @buf: pointer to buffer
 * @size: size of buffer
 *
 * Buffers can only be measured, not appraised.
 */
void ima_kexec_cmdline(const void *buf, int size)
{
	if (buf && size != 0)
		process_buffer_measurement(buf, size, "kexec-cmdline",
					   KEXEC_CMDLINE, 0, NULL);
}

static int __init init_ima(void)
{
	int error;

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
		ima_update_policy_flag();

	return error;
}

late_initcall(init_ima);	/* Start IMA after the TPM is available */
